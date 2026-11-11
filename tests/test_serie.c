/* Les ports série (grammaire 2.82, 3 novembre 2026) : un faux Yaesu sur un pseudo-terminal répond à ID; FA; MD0; et se
 * tait pour XX; ; src/serie.c doit rendre ses réponses, un texte vide pour un poste muet, et une erreur pour un port
 * absent. Sous Windows, pas de pseudo-terminal : l'essai est sauté. */
#ifndef _WIN32
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#endif
#include "serie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("test_serie : sauté sous Windows (pas de pseudo-terminal)\n");
    return 0;
}
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static int total, echecs;

static void verifier(int ligne, const char *obtenu, const char *attendu) {
    total++;
    if (!obtenu || strcmp(obtenu, attendu)) {
        echecs++;
        printf("ÉCHEC (ligne %d) : attendu « %s », obtenu « %s »\n", ligne, attendu, obtenu ? obtenu : "(rien)");
    }
}

/* Le faux poste : lit des commandes finies par « ; », répond comme un FT-991A (ID 0670). Il prend aussi une fréquence
 * (« FA014205000; », 9 chiffres) et un mode (« MD02; ») sans répondre, comme le poste (manuel CAT du FT-991A, FA et MD) :
 * la lecture qui suit rend ce qui a été posé (11 novembre 2026, « Remettre l'accord » d'Écoutille). */
static void faux_poste(int m) {
    char cmd[64];
    char frequence[10] = "014074000", mode = 'C';
    size_t n = 0;
    for (;;) {
        char c;
        ssize_t r = read(m, &c, 1);
        if (r <= 0) { usleep(5000); continue; }
        if (n < sizeof cmd - 1) cmd[n++] = c;
        if (c != ';') continue;
        cmd[n] = 0;
        n = 0;
        char lu[32];
        const char *rep = "?;";
        if (!strcmp(cmd, "ID;")) rep = "ID0670;";
        else if (!strcmp(cmd, "FA;")) { snprintf(lu, sizeof lu, "FA%s;", frequence); rep = lu; }
        else if (!strcmp(cmd, "MD0;")) { snprintf(lu, sizeof lu, "MD0%c;", mode); rep = lu; }
        else if (!strcmp(cmd, "XX;")) rep = NULL;
        else if (strlen(cmd) == 12 && !strncmp(cmd, "FA", 2) && strspn(cmd + 2, "0123456789") == 9) {
            memcpy(frequence, cmd + 2, 9);
            rep = NULL;
        } else if (strlen(cmd) == 5 && !strncmp(cmd, "MD0", 3)) {
            mode = cmd[3];
            rep = NULL;
        }
        if (rep && write(m, rep, strlen(rep)) < 0) _exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--faux-poste")) {   /* pour tests/ecoutille-pilotage : le chemin dans argv[2] */
        int fm = posix_openpt(O_RDWR | O_NOCTTY);
        if (fm < 0 || grantpt(fm) || unlockpt(fm)) return 1;
        int garde_e = open(ptsname(fm), O_RDWR | O_NOCTTY);
        FILE *f = fopen(argv[2], "w");
        if (!f) return 1;
        fprintf(f, "%s", ptsname(fm));
        fclose(f);
        faux_poste(fm);
        close(garde_e);
        return 0;
    }
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) || unlockpt(m)) { printf("test_serie : pas de pseudo-terminal, sauté\n"); return 0; }
    char *esclave = strdup(ptsname(m));
    int garde = open(esclave, O_RDWR | O_NOCTTY);   /* le côté esclave reste ouvert entre deux échanges */
    pid_t pid = fork();
    if (pid == 0) { faux_poste(m); _exit(0); }
    char *err = NULL, *r;

    r = serie_echanger(esclave, 4800, "ID;", &err);
    verifier(__LINE__, r, "ID0670;");
    free(r);
    r = serie_echanger(esclave, 38400, "FA;", &err);
    verifier(__LINE__, r, "FA014074000;");
    free(r);
    r = serie_echanger(esclave, 4800, "MD0;", &err);
    verifier(__LINE__, r, "MD0C;");
    free(r);
    r = serie_echanger(esclave, 4800, "FA007074000;", &err);   /* poser une fréquence : le poste se tait */
    verifier(__LINE__, r, "");
    free(r);
    r = serie_echanger(esclave, 4800, "FA;", &err);
    verifier(__LINE__, r, "FA007074000;");
    free(r);
    r = serie_echanger(esclave, 4800, "FA014074000;", &err);
    free(r);
    r = serie_echanger(esclave, 4800, "XX;", &err);   /* le poste se tait : un texte vide, pas une erreur */
    verifier(__LINE__, r, "");
    verifier(__LINE__, err ? err : "", "");
    free(r);
    r = serie_echanger("/dev/grymoir-pas-de-port", 4800, "FA;", &err);
    verifier(__LINE__, r ? r : "(rien)", "(rien)");
    verifier(__LINE__, err, "Le port « /dev/grymoir-pas-de-port » ne s'ouvre pas : le poste est-il branché et allumé ?");
    free(err);
    verifier(__LINE__, serie_vitesse_connue(38400) ? "oui" : "non", "oui");
    verifier(__LINE__, serie_vitesse_connue(4801) ? "oui" : "non", "non");
    char **noms = NULL;
    size_t nb = serie_ports(&noms);   /* aucun poste ici : la liste peut être vide, elle doit être triée */
    int trie = 1;
    for (size_t k = 1; k < nb; k++) if (strcmp(noms[k - 1], noms[k]) > 0) trie = 0;
    verifier(__LINE__, trie ? "triés" : "en désordre", "triés");
    serie_liberer(noms, nb);

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(garde);
    close(m);
    free(esclave);
    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs != 0;
}
#endif
