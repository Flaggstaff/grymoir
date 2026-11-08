/* GrymoiR : l'aiguilleur de « grym servir --sessions » (grammaire 3.18, docs/v2.md, § 11 ; 8 novembre 2026).
 *
 * Plusieurs navigateurs sur une application. L'aiguilleur écoute sur 127.0.0.1 ; chaque visiteur, entré par l'adresse
 * affichée (son jeton), reçoit un cookie de session, et sa propre session : un processus « grym servir » ordinaire, sur
 * un port interne, lancé pour lui. L'aiguilleur lui passe chaque requête, en récrivant ce que la session vérifie (son
 * hôte, son origine, son cookie), et rend la réponse telle quelle. La machine virtuelle ne change pas : chaque session
 * croit servir un seul navigateur. Toutes partagent la base du programme, que SQLite arbitre ; un champ changé par une
 * autre session pendant une question est un conflit (§ 16.6), et chacun se connecte (§ 16.20).
 *
 * Un processus par requête transmise : une réponse lente (un document, une transcription) n'arrête pas les autres
 * visiteurs. Le père seul lance et arrête les sessions ; il lit les en-têtes de chaque requête (cinq secondes au plus),
 * puis confie le corps et la réponse à un fils.
 *
 * Une session inactive (aucune requête) pendant la durée choisie s'arrête ; sa base est déjà validée, puisqu'elle
 * attendait une réponse (§ 17). Une session terminée (le programme est allé au bout) renaît à la visite suivante.
 *
 * POSIX seulement : sous Windows, « --sessions » le dit et s'arrête. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "aiguilleur.h"
#include "empreinte.h"
#include "texte.h"
#include "tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
int aiguilleur_servir(const char *grym, const char *programme, int port, int navigateur, int inactivite,
                      const char *reseau, const char *base, const char *ecriture, void (*ouvrir)(const char *adresse)) {
    (void)grym; (void)programme; (void)port; (void)navigateur; (void)inactivite; (void)reseau; (void)base; (void)ecriture;
    (void)ouvrir;
    fputs("« grym servir --sessions » ne tourne pas encore sous Windows : lance-le sur un Mac ou sous Linux.\n", stderr);
    return EXIT_FAILURE;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { MAX_SESSIONS = 32, MAX_ENTETES = 65536, MAX_CORPS = 64 * 1024 * 1024 };

typedef struct {
    char visiteur[33];   /* le cookie du visiteur ; vide : place libre */
    pid_t pid;           /* la session ; 0 : terminée, à relancer à la prochaine visite */
    int port;            /* son port interne */
    char jeton[33];      /* son jeton : l'aiguilleur le présente pour le visiteur */
    int sortie;          /* sa sortie standard, lue et recopiée ; -1 */
    time_t derniere;     /* sa dernière requête */
} Session;

/* Une connexion d'un navigateur : en clair sur 127.0.0.1, en TLS sur le réseau (§ 12). */
typedef struct {
    int fd;
    TlsConnexion *tls;
} Flux;

static long flux_lire(Flux *f, void *b, size_t n) {
    if (f->tls) return tls_lire(f->tls, b, n);
    for (;;) {
        ssize_t r = read(f->fd, b, n);
        if (r < 0 && errno == EINTR) continue;
        return (long)r;
    }
}

static int tout_ecrire(int f, const char *d, size_t n);

static int flux_ecrire(Flux *f, const char *d, size_t n) {
    return f->tls ? tls_ecrire(f->tls, d, n) : tout_ecrire(f->fd, d, n);
}

typedef struct {
    const char *grym, *programme;
    const char *base, *ecriture;   /* passés aux sessions, ou NULL */
    int ecoute, port;
    char jeton[33];          /* celui de l'adresse affichée */
    char hote[32], origine[48];
    TlsServeur *tls;         /* --reseau : le chiffrement ; NULL sur 127.0.0.1 */
    char **hotes, **origines;   /* --reseau : les noms du certificat, avec le port ; « https://… » */
    size_t nb_hotes;
    Session s[MAX_SESSIONS];
    int inactivite;          /* en secondes */
} Aiguilleur;

static volatile sig_atomic_t arreter = 0;
static void sur_signal(int n) { (void)n; arreter = 1; }

static int hex_aleatoire(char *hex) {
    unsigned char o[16];
    if (!aleatoire_systeme(o, sizeof o)) return 0;
    for (int k = 0; k < 16; k++) snprintf(hex + 2 * k, 3, "%02x", o[k]);
    return 1;
}

/* Comparaison en temps constant, comme serveur.c. */
static int meme(const char *a, size_t na, const char *b) {
    const size_t nb = strlen(b);
    unsigned d = (unsigned)(na ^ nb);
    for (size_t k = 0; k < nb; k++) d |= (unsigned)((k < na ? (unsigned char)a[k] : 0) ^ (unsigned char)b[k]);
    return d == 0;
}

static int tout_ecrire(int f, const char *d, size_t n) {
    while (n) {
        ssize_t e = write(f, d, n);
        if (e < 0 && errno == EINTR) continue;
        if (e <= 0) return 0;
        d += e;
        n -= (size_t)e;
    }
    return 1;
}

static void repondre_simple(Flux *f, const char *statut, const char *entetes, const char *texte) {
    char *r = grym_formater("HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
                            "X-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\nReferrer-Policy: same-origin\r\n"
                            "%sContent-Length: %lu\r\nConnection: close\r\n\r\n%s", statut, entetes ? entetes : "",
                            (unsigned long)strlen(texte), texte);
    flux_ecrire(f, r, strlen(r));
    free(r);
}

static void refuser(Flux *f, const char *pourquoi) {
    char *p = grym_formater("<!doctype html><meta charset=\"utf-8\"><title>GrymoiR</title><p>%s</p>", pourquoi);
    repondre_simple(f, "403 Forbidden", NULL, p);
    free(p);
}

/* La valeur d'un en-tête (nom en minuscules, suivi de « : »), sans les espaces de tête ; NULL. Dans e, jusqu'à fin. */
static const char *entete(const char *e, const char *nom, size_t *longueur) {
    const size_t ln = strlen(nom);
    for (const char *l = strstr(e, "\r\n"); l && l[2] && !(l[2] == '\r' && l[3] == '\n'); l = strstr(l + 2, "\r\n")) {
        const char *d = l + 2;
        size_t k = 0;
        while (k < ln && d[k] && (d[k] | 0x20) == nom[k]) k++;
        if (k == ln && d[k] == ':') {
            d += ln + 1;
            while (*d == ' ' || *d == '\t') d++;
            const char *f = strstr(d, "\r\n");
            *longueur = (size_t)(f - d);
            return d;
        }
    }
    return NULL;
}

/* Le cookie « grym_aiguilleur » de la requête, ou NULL. */
static const char *cookie_visiteur(const char *e, size_t *n) {
    size_t lc = 0;
    const char *c = entete(e, "cookie", &lc);
    const char *nom = "grym_aiguilleur=";
    for (size_t k = 0; c && k + strlen(nom) <= lc; k++)
        if ((k == 0 || c[k - 1] == ' ' || c[k - 1] == ';') && strncmp(c + k, nom, strlen(nom)) == 0) {
            const char *v = c + k + strlen(nom);
            size_t l = 0;
            while (v + l < c + lc && v[l] != ';') l++;
            *n = l;
            return v;
        }
    return NULL;
}

/* Lit les en-têtes (jusqu'à la ligne vide), cinq secondes au plus ; NULL si la requête ne vient pas. Le surplus
 * (le début du corps) reste dans *lu, après *taille_entetes octets. */
static char *lire_entetes(Flux *f, size_t *lu, size_t *taille_entetes) {
    char *b = grym_allouer(MAX_ENTETES + 1);
    size_t n = 0;
    while (n < MAX_ENTETES) {
        long r = flux_lire(f, b + n, MAX_ENTETES - n);
        if (r <= 0) break;
        n += (size_t)r;
        b[n] = 0;
        char *fin = strstr(b, "\r\n\r\n");
        if (fin) {
            *lu = n;
            *taille_entetes = (size_t)(fin - b) + 4;
            return b;
        }
    }
    free(b);
    return NULL;
}

/* Lance la session d'un visiteur : « grym servir programme --sans-navigateur », son adresse lue sur sa sortie. */
static int lancer_session(Aiguilleur *a, Session *s) {
    int t[2];
    if (pipe(t) != 0) return 0;
    pid_t p = fork();
    if (p < 0) { close(t[0]); close(t[1]); return 0; }
    if (p == 0) {
        dup2(t[1], STDOUT_FILENO);
        close(t[0]);
        close(t[1]);
        close(a->ecoute);
        setenv("GRYM_SESSION", "1", 1);   /* « le service à plusieurs postes » (3.18) */
        const char *argv[10];
        int n = 0;
        argv[n++] = a->grym;
        argv[n++] = "servir";
        argv[n++] = a->programme;
        argv[n++] = "--sans-navigateur";
        if (a->base) { argv[n++] = "--base"; argv[n++] = a->base; }
        if (a->ecriture) { argv[n++] = "--ecriture"; argv[n++] = a->ecriture; }
        argv[n] = NULL;
        execvp(a->grym, (char *const *)argv);
        _exit(127);
    }
    close(t[1]);
    /* la première ligne d'adresse : « http://127.0.0.1:PORT/?jeton=JETON », dix secondes au plus */
    char ligne[4096];
    size_t n = 0;
    int trouve = 0;
    const time_t debut = time(NULL);
    while (!trouve && n < sizeof ligne - 1 && time(NULL) - debut < 10) {
        struct pollfd q = { t[0], POLLIN, 0 };
        if (poll(&q, 1, 500) <= 0) continue;
        ssize_t r = read(t[0], ligne + n, sizeof ligne - 1 - n);
        if (r <= 0) break;
        n += (size_t)r;
        ligne[n] = 0;
        const char *h = strstr(ligne, "http://127.0.0.1:");
        const char *j = h ? strstr(h, "/?jeton=") : NULL;
        if (j && strlen(j + 8) >= 32) {
            s->port = atoi(h + 17);
            memcpy(s->jeton, j + 8, 32);
            s->jeton[32] = 0;
            trouve = s->port > 0;
        }
    }
    if (!trouve) {
        kill(p, SIGTERM);
        waitpid(p, NULL, 0);
        close(t[0]);
        return 0;
    }
    fcntl(t[0], F_SETFD, FD_CLOEXEC);
    s->pid = p;
    s->sortie = t[0];
    s->derniere = time(NULL);
    return 1;
}

static void arreter_session(Session *s) {
    if (s->pid > 0) {
        kill(s->pid, SIGTERM);
        waitpid(s->pid, NULL, 0);
    }
    if (s->sortie >= 0) close(s->sortie);
    s->pid = 0;
    s->sortie = -1;
}

/* Fils : lit le reste du corps, transmet la requête récrite à la session, rend sa réponse au navigateur. */
static void transmettre(const Session *s, Flux *f, char *e, size_t lu, size_t te) {
    size_t lcl = 0;
    const char *cl = entete(e, "content-length", &lcl);
    const unsigned long corps = cl ? strtoul(cl, NULL, 10) : 0;
    if (corps > MAX_CORPS) { repondre_simple(f, "413 Payload Too Large", NULL, "Trop gros."); return; }
    int c = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof ad);
    ad.sin_family = AF_INET;
    ad.sin_addr.s_addr = htonl(0x7F000001);
    ad.sin_port = htons((unsigned short)s->port);
    if (c < 0 || connect(c, (struct sockaddr *)&ad, sizeof ad) != 0) {
        repondre_simple(f, "502 Bad Gateway", NULL, "La session ne répond pas : recharge la page.");
        if (c >= 0) close(c);
        return;
    }
    /* la ligne de requête, puis les en-têtes, sans ceux que la session vérifie ; puis les siens */
    Chaine r = {0};
    const char *fl = strstr(e, "\r\n");
    char *premiere = grym_allouer((size_t)(fl - e) + 1);
    memcpy(premiere, e, (size_t)(fl - e));
    premiere[fl - e] = 0;
    chaine_ajouter(&r, premiere);
    free(premiere);
    chaine_ajouter(&r, "\r\n");
    int origine = 0;
    for (const char *l = fl + 2; l < e + te - 2;) {
        const char *fin = strstr(l, "\r\n");
        const size_t n = (size_t)(fin - l);
        char *x = grym_allouer(n + 1);
        memcpy(x, l, n);
        x[n] = 0;
        char *deux = strchr(x, ':');
        int garder = 1;
        if (deux) {
            for (char *p = x; p < deux; p++) *p = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
            *deux = 0;
            if (!strcmp(x, "host") || !strcmp(x, "cookie") || !strcmp(x, "referer") || !strcmp(x, "connection")) garder = 0;
            if (!strcmp(x, "origin")) { garder = 0; origine = 1; }
            *deux = ':';
            if (garder) memcpy(x, l, (size_t)(deux - x));   /* le nom tel qu'il était */
        }
        if (garder) { chaine_ajouter(&r, x); chaine_ajouter(&r, "\r\n"); }
        free(x);
        l = fin + 2;
    }
    char *siens = grym_formater("Host: 127.0.0.1:%d\r\nCookie: grym_%d=%s\r\nConnection: close\r\n\r\n", s->port,
                                s->port, s->jeton);
    chaine_ajouter(&r, siens);
    free(siens);
    if (origine) {
        char *o = grym_formater("Origin: http://127.0.0.1:%d\r\n\r\n", s->port);
        r.n -= 2;   /* l'origine avant la ligne vide */
        r.d[r.n] = 0;
        chaine_ajouter(&r, o);
        free(o);
    }
    int ok = tout_ecrire(c, r.d, r.n) && tout_ecrire(c, e + te, lu - te);
    free(r.d);
    /* le reste du corps, puis la réponse */
    struct timeval t = { 30, 0 };
    setsockopt(f->fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
    char tampon[65536];
    for (unsigned long reste = corps > lu - te ? corps - (lu - te) : 0; ok && reste;) {
        long n = flux_lire(f, tampon, reste < sizeof tampon ? reste : sizeof tampon);
        if (n <= 0) { ok = 0; break; }
        ok = tout_ecrire(c, tampon, (size_t)n);
        reste -= (unsigned long)n;
    }
    for (;;) {
        ssize_t n = read(c, tampon, sizeof tampon);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (!flux_ecrire(f, tampon, (size_t)n)) break;
    }
    close(c);
}

static Session *session_de(Aiguilleur *a, const char *v, size_t n) {
    if (!v || n != 32) return NULL;
    for (int k = 0; k < MAX_SESSIONS; k++)
        if (a->s[k].visiteur[0] && meme(v, n, a->s[k].visiteur)) return &a->s[k];
    return NULL;
}

/* Recopie ce que les sessions écrivent (leurs messages, leurs erreurs) ; ferme les sessions terminées. */
static void veiller(Aiguilleur *a) {
    char b[4096];
    for (int k = 0; k < MAX_SESSIONS; k++) {
        Session *s = &a->s[k];
        if (s->sortie < 0) continue;
        struct pollfd q = { s->sortie, POLLIN, 0 };
        while (poll(&q, 1, 0) > 0) {
            ssize_t n = read(s->sortie, b, sizeof b);
            if (n <= 0) { close(s->sortie); s->sortie = -1; break; }
            fwrite(b, 1, (size_t)n, stdout);
        }
    }
    fflush(stdout);
    for (pid_t p; (p = waitpid(-1, NULL, WNOHANG)) > 0;)
        for (int k = 0; k < MAX_SESSIONS; k++)
            if (a->s[k].pid == p) {
                a->s[k].pid = 0;
                if (a->s[k].sortie >= 0) { close(a->s[k].sortie); a->s[k].sortie = -1; }
            }
    const time_t maintenant = time(NULL);
    for (int k = 0; k < MAX_SESSIONS; k++)
        if (a->s[k].pid > 0 && maintenant - a->s[k].derniere > a->inactivite) {
            printf("Session %.6s… arrêtée : inactive depuis %d min.\n", a->s[k].visiteur, a->inactivite / 60);
            arreter_session(&a->s[k]);
        }
}

static int parmi(const char *x, size_t n, char *const *liste, size_t nb, const char *seul) {
    if (!x) return 0;
    if (!liste) return n == strlen(seul) && strncmp(x, seul, n) == 0;
    for (size_t k = 0; k < nb; k++)
        if (n == strlen(liste[k]) && strncmp(x, liste[k], n) == 0) return 1;
    return 0;
}

static void accueillir(Aiguilleur *a, int fd) {
    Flux flux = { fd, NULL }, *f = &flux;
    struct timeval t5 = { 5, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t5, sizeof t5);   /* la poignée de main et les en-têtes : 5 s au plus */
    if (a->tls) {
        char *pb = NULL;
        flux.tls = tls_accepter(a->tls, fd, &pb);
        if (!flux.tls) { free(pb); close(fd); return; }   /* un navigateur en clair, un balayage du réseau */
    }
    size_t lu = 0, te = 0;
    char *e = lire_entetes(f, &lu, &te);
    if (!e) { tls_fermer(flux.tls, 0); close(fd); return; }
    size_t lh = 0, lo = 0, lv = 0;
    const char *h = entete(e, "host", &lh), *o = entete(e, "origin", &lo);
    const char *v = cookie_visiteur(e, &lv);
    const int post = strncmp(e, "POST ", 5) == 0;
    /* les mêmes gardes que serveur.c : l'hôte attendu (pas de DNS rebinding), l'origine d'un envoi ; sur le réseau,
     * les noms du certificat seulement (§ 12) */
    if (!parmi(h, lh, a->hotes, a->nb_hotes, a->hote)) { refuser(f, "Nom d'hôte inattendu."); goto fin; }
    if (post && !parmi(o, lo, a->origines, a->nb_hotes, a->origine)) { refuser(f, "Origine inattendue."); goto fin; }
    Session *s = session_de(a, v, lv);
    if (!s && strncmp(e, "GET /?jeton=", 12) == 0) {
        /* un nouveau visiteur, avec le jeton de l'adresse affichée : sa session, son cookie */
        const char *j = e + 12, *fj = strchr(j, ' ');
        if (!fj || !meme(j, (size_t)(fj - j), a->jeton)) { refuser(f, "Accès refusé : ouvre l'adresse affichée dans le terminal."); goto fin; }
        for (int k = 0; k < MAX_SESSIONS && !s; k++)
            if (!a->s[k].visiteur[0]) s = &a->s[k];
        if (!s) { repondre_simple(f, "503 Service Unavailable", NULL, "Trop de sessions ouvertes : réessaie plus tard."); goto fin; }
        if (!hex_aleatoire(s->visiteur)) { s->visiteur[0] = 0; refuser(f, "Aucun générateur aléatoire."); goto fin; }
        s->pid = 0;
        s->sortie = -1;
        char *c = grym_formater("Set-Cookie: grym_aiguilleur=%s; HttpOnly; SameSite=Strict; Path=/%s\r\nLocation: /\r\n",
                                s->visiteur, a->tls ? "; Secure" : "");
        repondre_simple(f, "303 See Other", c, "");
        free(c);
        printf("Nouveau visiteur %.6s….\n", s->visiteur);
        fflush(stdout);
        goto fin;
    }
    if (!s) { refuser(f, "Accès refusé : ouvre l'adresse affichée dans le terminal."); goto fin; }
    if (s->pid <= 0) {   /* première visite, ou session terminée ou arrêtée : elle (re)naît */
        if (!lancer_session(a, s)) {
            repondre_simple(f, "502 Bad Gateway", NULL, "La session ne démarre pas : vois le terminal de l'aiguilleur.");
            goto fin;
        }
        printf("Session %.6s… : port interne %d.\n", s->visiteur, s->port);
        fflush(stdout);
    }
    s->derniere = time(NULL);
    pid_t p = fork();
    if (p == 0) {
        close(a->ecoute);
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        transmettre(s, f, e, lu, te);
        tls_fermer(flux.tls, 1);
        _exit(0);
    }
    if (p < 0) repondre_simple(f, "503 Service Unavailable", NULL, "Plus de processus disponible.");
    else {   /* le fils a sa copie de la connexion chiffrée : le père oublie la sienne, sans rien envoyer */
        tls_fermer(flux.tls, 0);
        flux.tls = NULL;
    }
fin:
    free(e);
    if (flux.tls) tls_fermer(flux.tls, 1);
    close(fd);
}

int aiguilleur_servir(const char *grym, const char *programme, int port, int navigateur, int inactivite,
                      const char *reseau, const char *base, const char *ecriture, void (*ouvrir)(const char *adresse)) {
    Aiguilleur a;
    memset(&a, 0, sizeof a);
    a.base = base;
    a.ecriture = ecriture;
    char **noms = NULL;
    size_t nb_noms = 0;
    if (reseau) {   /* § 12 : le certificat du serveur, ses noms ; écoute sur toutes les adresses, en TLS seulement */
        char *pb = NULL;
        a.tls = tls_serveur_creer(reseau, &pb);
        if (a.tls && !tls_noms(reseau, &noms, &nb_noms, &pb)) { tls_serveur_detruire(a.tls); a.tls = NULL; }
        if (!a.tls) { fprintf(stderr, "%s\n", pb); free(pb); return EXIT_FAILURE; }
        if (!port) port = 8443;
    }
    a.grym = grym;
    a.programme = programme;
    a.inactivite = inactivite > 0 ? inactivite * 60 : 30 * 60;
    for (int k = 0; k < MAX_SESSIONS; k++) a.s[k].sortie = -1;
    if (!hex_aleatoire(a.jeton)) { fputs("Aucun générateur aléatoire du système : pas de jeton.\n", stderr); return EXIT_FAILURE; }
    a.ecoute = socket(AF_INET, SOCK_STREAM, 0);
    int un = 1;
    setsockopt(a.ecoute, SOL_SOCKET, SO_REUSEADDR, &un, sizeof un);
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof ad);
    ad.sin_family = AF_INET;
    /* 127.0.0.1, sauf --reseau : alors toutes les adresses, en TLS seulement (docs/v2.md, § 5, règle 1 ; § 12) */
    ad.sin_addr.s_addr = htonl(reseau ? INADDR_ANY : 0x7F000001);
    ad.sin_port = htons((unsigned short)port);
    if (a.ecoute < 0 || bind(a.ecoute, (struct sockaddr *)&ad, sizeof ad) != 0 || listen(a.ecoute, 64) != 0) {
        if (port) fprintf(stderr, "Le port %d est déjà pris : choisis-en un autre avec --port.\n", port);
        else fputs("Impossible d'écouter sur 127.0.0.1.\n", stderr);
        return EXIT_FAILURE;
    }
    fcntl(a.ecoute, F_SETFD, FD_CLOEXEC);
    socklen_t l = sizeof ad;
    getsockname(a.ecoute, (struct sockaddr *)&ad, &l);
    a.port = ntohs(ad.sin_port);
    snprintf(a.hote, sizeof a.hote, "127.0.0.1:%d", a.port);
    snprintf(a.origine, sizeof a.origine, "http://127.0.0.1:%d", a.port);
    char adresse[300];
    snprintf(adresse, sizeof adresse, "http://127.0.0.1:%d/?jeton=%s", a.port, a.jeton);
    if (a.tls) {
        a.hotes = grym_allouer(nb_noms * sizeof *a.hotes);
        a.origines = grym_allouer(nb_noms * sizeof *a.origines);
        for (size_t k = 0; k < nb_noms; k++) {
            const int v6 = strchr(noms[k], ':') != NULL;   /* une adresse IPv6 s'écrit entre crochets */
            a.hotes[k] = grym_formater(v6 ? "[%s]:%d" : "%s:%d", noms[k], a.port);
            a.origines[k] = grym_formater(v6 ? "https://[%s]:%d" : "https://%s:%d", noms[k], a.port);
        }
        a.nb_hotes = nb_noms;
        snprintf(adresse, sizeof adresse, "%s/?jeton=%s", a.origines[0], a.jeton);
    }
    printf("« %s » est servi, une session par navigateur, à l'adresse :\n  %s\n%sCtrl+C pour arrêter.\n", programme, adresse,
           a.tls ? "Sur le réseau local, en TLS : chaque poste a installé l'autorité du cabinet (docs/v2.md, § 12).\n" : "");
    fflush(stdout);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sur_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    if (navigateur && ouvrir) ouvrir(adresse);
    while (!arreter) {
        struct pollfd q = { a.ecoute, POLLIN, 0 };
        const int r = poll(&q, 1, 1000);
        veiller(&a);
        if (r > 0 && (q.revents & POLLIN)) {
            int f = accept(a.ecoute, NULL, NULL);
            if (f >= 0) accueillir(&a, f);
        }
    }
    printf("Arrêt : les sessions se ferment.\n");
    for (int k = 0; k < MAX_SESSIONS; k++) arreter_session(&a.s[k]);
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    close(a.ecoute);
    for (size_t k = 0; k < a.nb_hotes; k++) { free(a.hotes[k]); free(a.origines[k]); }
    free(a.hotes);
    free(a.origines);
    for (size_t k = 0; k < nb_noms; k++) free(noms[k]);
    free(noms);
    tls_serveur_detruire(a.tls);
    return EXIT_SUCCESS;
}
#endif
