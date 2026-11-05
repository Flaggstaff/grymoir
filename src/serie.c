/* Les ports série (grammaire 2.82, 3 novembre 2026). Voir serie.h. */
#ifndef _WIN32
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "serie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "texte.h"

#ifndef _WIN32
#include <time.h>
#endif

static const long VITESSES[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};

int serie_vitesse_connue(long bauds) {
    for (size_t k = 0; k < sizeof VITESSES / sizeof VITESSES[0]; k++)
        if (VITESSES[k] == bauds) return 1;
    return 0;
}


static void ajouter(char ***noms, size_t *n, size_t *cap, const char *nom) {
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        char **nv = grym_allouer(*cap * sizeof *nv);
        if (*n) memcpy(nv, *noms, *n * sizeof *nv);
        free(*noms);
        *noms = nv;
    }
    (*noms)[(*n)++] = grym_dupliquer(nom);
}

/* Les octets reçus, sans le zéro qu'un poste n'envoie jamais ; un zéro lu est sauté. */
static void ajouter_octets(Chaine *c, const char *o, size_t n) {
    char un[2] = {0, 0};
    for (size_t k = 0; k < n; k++) {
        if (!o[k]) continue;
        un[0] = o[k];
        chaine_ajouter(c, un);
    }
}

void serie_liberer(char **noms, size_t n) {
    for (size_t k = 0; k < n; k++) free(noms[k]);
    free(noms);
}

#ifdef _WIN32
#include <windows.h>

size_t serie_ports(char ***noms) {
    size_t n = 0, cap = 0;
    *noms = NULL;
    char cible[512];
    for (int k = 1; k <= 256; k++) {
        char nom[16];
        snprintf(nom, sizeof nom, "COM%d", k);
        if (QueryDosDeviceA(nom, cible, sizeof cible)) ajouter(noms, &n, &cap, nom);
    }
    return n;   /* déjà dans l'ordre des numéros */
}

char *serie_echanger(const char *port, long bauds, const char *question, char **erreur) {
    *erreur = NULL;
    char chemin[64];
    snprintf(chemin, sizeof chemin, "\\\\.\\%s", port);
    HANDLE h = CreateFileA(chemin, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        *erreur = e == ERROR_ACCESS_DENIED
                      ? grym_formater("Le port « %s » est déjà ouvert par un autre programme (WSJT-X, un logiciel de carnet ?).", port)
                      : grym_formater("Le port « %s » ne s'ouvre pas : le poste est-il branché et allumé ?", port);
        return NULL;
    }
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate = (DWORD)bauds;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = TWOSTOPBITS;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;   /* baissés : sur le port « Standard » d'un Yaesu, ils passent en émission */
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        *erreur = grym_formater("Le port « %s » refuse la vitesse de %ld bauds.", port, bauds);
        return NULL;
    }
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    COMMTIMEOUTS to;
    memset(&to, 0, sizeof to);
    to.ReadIntervalTimeout = 100;        /* 100 ms de silence entre deux octets : la réponse est finie */
    to.ReadTotalTimeoutConstant = 1000;  /* 1 seconde sans rien */
    to.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(h, &to);
    DWORD ecrits = 0;
    size_t lg = strlen(question);
    if (!WriteFile(h, question, (DWORD)lg, &ecrits, NULL) || ecrits != lg) {
        CloseHandle(h);
        *erreur = grym_formater("Le port « %s » n'accepte pas la question.", port);
        return NULL;
    }
    Chaine r = {0};
    for (;;) {
        char tampon[256];
        DWORD lus = 0;
        if (!ReadFile(h, tampon, sizeof tampon, &lus, NULL) || lus == 0) break;
        ajouter_octets(&r, tampon, lus);
        if (r.n > 65536) break;
        to.ReadTotalTimeoutConstant = 100;   /* la suite : 100 ms au plus */
        SetCommTimeouts(h, &to);
    }
    CloseHandle(h);
    return chaine_rendre(&r);
}

#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static int comparer(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

size_t serie_ports(char ***noms) {
    size_t n = 0, cap = 0;
    *noms = NULL;
    DIR *d = opendir("/dev");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *s = e->d_name;
#ifdef __APPLE__
        int garde = !strncmp(s, "cu.", 3);
#else
        int garde = !strncmp(s, "ttyUSB", 6) || !strncmp(s, "ttyACM", 6);
#endif
        if (garde) {
            char chemin[300];
            snprintf(chemin, sizeof chemin, "/dev/%s", s);
            ajouter(noms, &n, &cap, chemin);
        }
    }
    closedir(d);
    if (n > 1) qsort(*noms, n, sizeof **noms, comparer);
    return n;
}

static speed_t vitesse(long bauds) {
    switch (bauds) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    default: return B115200;
    }
}

static long maintenant_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

char *serie_echanger(const char *port, long bauds, const char *question, char **erreur) {
    *erreur = NULL;
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        *erreur = errno == EBUSY || errno == EACCES
                      ? grym_formater("Le port « %s » est déjà ouvert par un autre programme, ou interdit d'accès.", port)
                      : grym_formater("Le port « %s » ne s'ouvre pas : le poste est-il branché et allumé ?", port);
        return NULL;
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        *erreur = grym_formater("« %s » n'est pas un port série.", port);
        return NULL;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD | CSTOPB;
    tio.c_cflag &= ~(tcflag_t)(PARENB | CSIZE);
    tio.c_cflag |= CS8;
#ifdef CRTSCTS
    tio.c_cflag &= ~(tcflag_t)CRTSCTS;
#endif
    tio.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
    cfsetispeed(&tio, vitesse(bauds));
    cfsetospeed(&tio, vitesse(bauds));
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        *erreur = grym_formater("Le port « %s » refuse la vitesse de %ld bauds.", port, bauds);
        return NULL;
    }
    /* DTR et RTS baissés : sur le second port d'un Yaesu (« Standard »), ils peuvent passer le poste en émission */
    int lignes = TIOCM_DTR | TIOCM_RTS;
    (void)ioctl(fd, TIOCMBIC, &lignes);
    tcflush(fd, TCIOFLUSH);
    size_t lg = strlen(question), ecrits = 0;
    long debut = maintenant_ms();
    while (ecrits < lg) {
        ssize_t w = write(fd, question + ecrits, lg - ecrits);
        if (w > 0) ecrits += (size_t)w;
        else if (w < 0 && errno != EAGAIN && errno != EINTR) break;
        if (maintenant_ms() - debut > 1000) break;
        if (w <= 0) { struct pollfd p = {fd, POLLOUT, 0}; poll(&p, 1, 20); }
    }
    if (ecrits < lg) {
        close(fd);
        *erreur = grym_formater("Le port « %s » n'accepte pas la question.", port);
        return NULL;
    }
    Chaine r = {0};
    int attente = 1000;   /* 1 seconde pour le premier octet, puis 100 ms de silence finissent la réponse */
    for (;;) {
        struct pollfd p = {fd, POLLIN, 0};
        int k = poll(&p, 1, attente);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        char tampon[256];
        ssize_t lus = read(fd, tampon, sizeof tampon);
        if (lus < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (lus <= 0) break;
        ajouter_octets(&r, tampon, (size_t)lus);
        if (r.n > 65536) break;
        attente = 100;
    }
    close(fd);
    return chaine_rendre(&r);
}
#endif

/* ---------------------------------------------------------------- */
/* WSJT-X : ses décodages, reçus en UDP (grammaire 2.88)            */
/* ---------------------------------------------------------------- */
/* Le format est celui de NetworkMessage.hpp, dans les sources de WSJT-X (décrit ici de mémoire ; à confirmer sur un vrai
 * WSJT-X) : un QDataStream gros-boutiste ; l'en-tête
 * (nombre magique 0xadbccbda, version du schéma, sorte du message, identifiant) ; un texte UTF-8 : sa longueur sur 32 bits,
 * 0xffffffff pour nul, puis ses octets ; un booléen : un octet ; une heure (QTime) : les millisecondes depuis minuit, sur
 * 32 bits ; un réel : un double IEEE gros-boutiste. Status (1) : fréquence de cadran (64 bits, Hz), mode, … ; Decode (2) :
 * nouveau, heure, rapport (32 bits signés), écart (double), décalage (32 bits), mode (un caractère), message, douteux,
 * hors antenne. */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Prise;
#define PRISE_INVALIDE INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Prise;
#define PRISE_INVALIDE (-1)
#endif

typedef struct { const unsigned char *d; size_t n, i; int ok; } LectureQt;

static unsigned long long qt_entier(LectureQt *l, int octets) {
    unsigned long long v = 0;
    if (l->i + (size_t)octets > l->n) { l->ok = 0; return 0; }
    for (int k = 0; k < octets; k++) v = (v << 8) | l->d[l->i++];
    return v;
}

/* Un texte UTF-8 de QDataStream ; NULL pour nul ou illisible ; alloué. */
static char *qt_texte(LectureQt *l) {
    const unsigned long long n = qt_entier(l, 4);
    if (!l->ok || n == 0xffffffffULL) return NULL;
    if (l->i + n > l->n) { l->ok = 0; return NULL; }
    char *t = grym_allouer((size_t)n + 1);
    memcpy(t, l->d + l->i, (size_t)n);
    t[n] = '\0';
    l->i += (size_t)n;
    return t;
}

static double qt_reel(LectureQt *l) {
    const unsigned long long b = qt_entier(l, 8);
    double x;
    memcpy(&x, &b, sizeof x);
    return x;
}

static void json_texte(Chaine *c, const char *t) {
    chaine_ajouter(c, "\"");
    for (const char *p = t ? t : ""; *p; p++) {
        char u[8];
        if (*p == '"' || *p == '\\') { u[0] = '\\'; u[1] = *p; u[2] = 0; }
        else if ((unsigned char)*p < 0x20) snprintf(u, sizeof u, "\\u%04x", (unsigned char)*p);
        else { u[0] = *p; u[1] = 0; }
        chaine_ajouter(c, u);
    }
    chaine_ajouter(c, "\"");
}

/* La fréquence et le mode du dernier Status, par identifiant d'instance (WSJT-X, WSJT-X - Rig 2…). */
typedef struct { char *id; unsigned long long hz; char *mode; } EtatWsjtx;
static EtatWsjtx *etats;
static size_t nb_etats;

static EtatWsjtx *etat_de(const char *id) {
    for (size_t k = 0; k < nb_etats; k++) if (!strcmp(etats[k].id, id)) return &etats[k];
    EtatWsjtx *t = grym_allouer((nb_etats + 1) * sizeof *t);
    if (nb_etats) memcpy(t, etats, nb_etats * sizeof *t);
    free(etats);
    etats = t;
    etats[nb_etats] = (EtatWsjtx){ grym_dupliquer(id), 0, NULL };
    return &etats[nb_etats++];
}

/* Le mode d'un Decode est un caractère : ~ FT8, + FT4, # JT65, @ JT9, & MSK144 (de mémoire, d'après les sources de WSJT-X ;
 * non vérifié ici). Le mode du dernier Status, écrit en toutes lettres, passe avant. */
static const char *mode_wsjtx(const char *c) {
    if (!c) return "";
    if (!strcmp(c, "~")) return "FT8";
    if (!strcmp(c, "+")) return "FT4";
    if (!strcmp(c, "#")) return "JT65";
    if (!strcmp(c, "@")) return "JT9";
    if (!strcmp(c, "&")) return "MSK144";
    return c;
}

char *wsjtx_lire_datagramme(const unsigned char *d, size_t n) {
    LectureQt l = { d, n, 0, 1 };
    if (qt_entier(&l, 4) != 0xadbccbdaULL) return NULL;
    const unsigned long long schema = qt_entier(&l, 4), sorte = qt_entier(&l, 4);
    if (!l.ok || schema < 2) return NULL;
    char *id = qt_texte(&l);
    if (!l.ok) { free(id); return NULL; }
    char *r = NULL;
    if (sorte == 1) {   /* Status : la fréquence de cadran et le mode, gardés pour les décodages qui suivent */
        const unsigned long long hz = qt_entier(&l, 8);
        char *mode = qt_texte(&l);
        if (l.ok) {
            EtatWsjtx *e = etat_de(id ? id : "");
            e->hz = hz;
            free(e->mode);
            e->mode = mode;
            mode = NULL;
        }
        free(mode);
    } else if (sorte == 2) {
        const int nouveau = (int)qt_entier(&l, 1);
        const unsigned long long ms = qt_entier(&l, 4);
        const long rapport = (long)(int)(unsigned)qt_entier(&l, 4);
        const double ecart = qt_reel(&l);
        const unsigned long long decalage = qt_entier(&l, 4);
        char *mode = qt_texte(&l);
        char *message = qt_texte(&l);
        const int douteux = (int)qt_entier(&l, 1);
        if (l.ok && nouveau && message) {
            const EtatWsjtx *e = etat_de(id ? id : "");
            Chaine c = {0};
            char h[96];
            snprintf(h, sizeof h, "{\"heure\":\"%02llu:%02llu:%02llu\",\"mode\":", ms / 3600000ULL % 24, ms / 60000ULL % 60, ms / 1000ULL % 60);
            chaine_ajouter(&c, h);
            json_texte(&c, e->mode && *e->mode ? e->mode : mode_wsjtx(mode));
            if (e->hz) { snprintf(h, sizeof h, ",\"fréquence\":%llu", e->hz); chaine_ajouter(&c, h); }
            snprintf(h, sizeof h, ",\"rapport\":%ld,\"décalage\":%llu,\"écart\":%.1f,\"message\":", rapport, decalage, ecart);
            chaine_ajouter(&c, h);
            json_texte(&c, message);
            chaine_ajouter(&c, douteux ? ",\"douteux\":true}" : ",\"douteux\":false}");
            r = chaine_rendre(&c);
        }
        free(mode);
        free(message);
    }
    free(id);
    return r;
}

typedef struct { long port; Prise p; } PriseWsjtx;
static PriseWsjtx *prises;
static size_t nb_prises;

size_t wsjtx_decodages(long port, char ***decodages, char **erreur) {
    *decodages = NULL;
    Prise p = PRISE_INVALIDE;
    for (size_t k = 0; k < nb_prises; k++) if (prises[k].port == port) p = prises[k].p;
    if (p == PRISE_INVALIDE) {
#ifdef _WIN32
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { *erreur = grym_dupliquer("Réseau indisponible (WSAStartup)."); return (size_t)-1; }
#endif
        p = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (p == PRISE_INVALIDE || bind(p, (struct sockaddr *)&a, sizeof a) != 0) {
            if (p != PRISE_INVALIDE) {
#ifdef _WIN32
                closesocket(p);
#else
                close(p);
#endif
            }
            *erreur = grym_formater("Le port UDP %ld ne s'ouvre pas : un autre logiciel l'écoute peut-être déjà (JTAlert, "
                                    "GridTracker). Dans WSJT-X, Réglages, Rapports, « UDP Server » : 127.0.0.1, port %ld.", port, port);
            return (size_t)-1;
        }
#ifdef _WIN32
        u_long oui = 1;
        ioctlsocket(p, FIONBIO, &oui);
#else
        fcntl(p, F_SETFL, fcntl(p, F_GETFL, 0) | O_NONBLOCK);
#endif
        PriseWsjtx *t = grym_allouer((nb_prises + 1) * sizeof *t);
        if (nb_prises) memcpy(t, prises, nb_prises * sizeof *t);
        free(prises);
        prises = t;
        prises[nb_prises++] = (PriseWsjtx){ port, p };
    }
    size_t n = 0, cap = 0;
    unsigned char tampon[65536];
    for (int tours = 0; tours < 10000; tours++) {   /* tout ce qui attend, sans attendre davantage */
        const long recu = (long)recv(p, (char *)tampon, sizeof tampon, 0);
        if (recu <= 0) break;
        char *dec = wsjtx_lire_datagramme(tampon, (size_t)recu);
        if (!dec) continue;
        ajouter(decodages, &n, &cap, dec);
        free(dec);
    }
    return n;
}
