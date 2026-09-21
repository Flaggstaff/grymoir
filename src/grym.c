/* grym : programme principal de GrymoiR, v0.1
 *
 *   grym                      boucle interactive (§ 3.3)
 *   grym lancer fichier.grym  exécute un fichier
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* fileno, isatty */
#endif

#include "analyseur.h"
#include "evaluateur.h"
#include "texte.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define terminal() _isatty(_fileno(stdin))
#else
#include <unistd.h>
#define terminal() isatty(fileno(stdin))
#endif

#define VERSION "0.1"

static char *lire_fichier(FILE *f, size_t *taille) {
    size_t cap = 4096, n = 0;
    char *d = grym_allouer(cap);
    for (;;) {
        if (n == cap) {
            char *e = realloc(d, cap *= 2);
            if (!e) { free(d); return NULL; }
            d = e;
        }
        size_t lu = fread(d + n, 1, cap - n, f);
        n += lu;
        if (lu == 0) break;
    }
    *taille = n;
    return d;
}

/* Une ligne de l'entrée standard, sans le saut de ligne ; NULL en fin d'entrée. */
static char *lire_ligne(void) {
    size_t cap = 256, n = 0;
    char *l = grym_allouer(cap);
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (n + 1 >= cap) {
            char *e = realloc(l, cap *= 2);
            if (!e) { free(l); return NULL; }
            l = e;
        }
        l[n++] = (char)c;
    }
    if (c == EOF && n == 0) { free(l); return NULL; }
    if (n && l[n - 1] == '\r') n--;
    l[n] = '\0';
    return l;
}

static void signaler(const char *fichier, const Diagnostic *d) {
    if (d->ligne) fprintf(stderr, "%s:%d:%d : erreur : %s\n", fichier, d->ligne, d->colonne, d->message);
    else          fprintf(stderr, "%s : erreur : %s\n", fichier, d->message);
}

static int lancer(const char *chemin) {
    FILE *f = fopen(chemin, "rb");
    if (!f) {
        fprintf(stderr, "Impossible d'ouvrir « %s ».\n", chemin);
        return EXIT_FAILURE;
    }
    size_t taille = 0;
    char *source = lire_fichier(f, &taille);
    fclose(f);
    if (!source) return EXIT_FAILURE;

    Portee *portee = portee_creer();
    Environnement *env = env_creer();
    Programme p;
    Diagnostic d;
    int code = EXIT_SUCCESS;
    if (!analyser(source, taille, portee, 0, &p, &d)) {
        signaler(chemin, &d);
        diagnostic_liberer(&d);
        code = EXIT_FAILURE;
    } else {
        Chaine sortie = {0};
        int ok = executer(&p, env, &sortie, &d);
        if (sortie.d) fputs(sortie.d, stdout);
        free(sortie.d);
        if (!ok) {
            fflush(stdout);
            signaler(chemin, &d);
            diagnostic_liberer(&d);
            code = EXIT_FAILURE;
        }
        programme_liberer(&p);
    }
    env_detruire(env);
    portee_detruire(portee);
    free(source);
    return code;
}

static int est_quitter(const char *l) {
    while (*l == ' ' || *l == '\t') l++;
    const char *mot = "quitter";
    size_t k = 0;
    while (mot[k] && tolower((unsigned char)l[k]) == mot[k]) k++;
    if (mot[k]) return 0;
    l += k;
    while (*l == ' ' || *l == '\t' || *l == '.') l++;
    return *l == '\0';
}

/* Boucle interactive : une saisie ratée n'a aucun effet (§ 3.3). */
static int boucle(void) {
    int tty = terminal();
    if (tty) printf("GrymoiR %s, boucle interactive. Tapez « quitter » pour sortir.\n", VERSION);
    Portee *portee = portee_creer();
    Environnement *env = env_creer();
    for (;;) {
        if (tty) { fputs("> ", stdout); fflush(stdout); }
        char *ligne = lire_ligne();
        if (!ligne) break;
        if (est_quitter(ligne)) { free(ligne); break; }

        Portee *sauve_portee = portee_cloner(portee);
        Environnement *sauve_env = env_cloner(env);
        Programme p;
        Diagnostic d;
        int ok = analyser(ligne, strlen(ligne), portee, 1, &p, &d);
        Chaine sortie = {0};
        if (ok) {
            ok = executer(&p, env, &sortie, &d);
            programme_liberer(&p);
        }
        if (ok) {
            if (sortie.d) fputs(sortie.d, stdout);
            portee_detruire(sauve_portee);
            env_detruire(sauve_env);
        } else {
            if (tty && d.ligne == 1 && d.colonne > 0) {
                /* Un curseur sous la colonne fautive (les caractères sont supposés d'une case). */
                printf("%*s^\n", d.colonne + 1, "");
            }
            printf("Erreur : %s\n", d.message);
            diagnostic_liberer(&d);
            portee_detruire(portee);
            env_detruire(env);
            portee = sauve_portee;
            env = sauve_env;
        }
        free(sortie.d);
        free(ligne);
        fflush(stdout);
    }
    if (tty) putchar('\n');
    env_detruire(env);
    portee_detruire(portee);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    if (argc == 1) return boucle();
    if (argc == 3 && strcmp(argv[1], "lancer") == 0) return lancer(argv[2]);
    fprintf(stderr,
            "GrymoiR %s\n"
            "Usage :\n"
            "  grym                      boucle interactive\n"
            "  grym lancer fichier.grym  exécute un fichier\n", VERSION);
    return EXIT_FAILURE;
}
