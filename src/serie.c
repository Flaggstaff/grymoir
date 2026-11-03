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
