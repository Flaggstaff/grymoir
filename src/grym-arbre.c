/* grym-arbre : affiche l'arbre syntaxique d'un fichier .grym (outil de mise au point, v0.1).
 * Usage : grym-arbre fichier.grym     ou     grym-arbre < fichier.grym
 */
#include "analyseur.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

static char *lire_tout(FILE *f, size_t *taille) {
    size_t cap = 4096, n = 0;
    char *d = malloc(cap);
    if (!d) return NULL;
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

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const char *nom = argc > 1 ? argv[1] : "<entrée>";
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : stdin;
    if (!f) {
        fprintf(stderr, "Impossible d'ouvrir « %s ».\n", nom);
        return EXIT_FAILURE;
    }
    size_t taille = 0;
    char *source = lire_tout(f, &taille);
    if (f != stdin) fclose(f);
    if (!source) {
        fprintf(stderr, "Lecture impossible de « %s ».\n", nom);
        return EXIT_FAILURE;
    }

    Portee *portee = portee_creer();
    Programme p;
    Diagnostic d;
    int code = EXIT_SUCCESS;
    if (analyser(source, taille, portee, 0, &p, &d)) {
        for (size_t i = 0; i < p.nb; i++) {
            char *s = noeud_decrire(p.phrases[i]);
            printf("%4d  %s\n", p.phrases[i]->ligne, s);
            free(s);
        }
        programme_liberer(&p);
    } else {
        if (d.ligne) fprintf(stderr, "%s:%d:%d : erreur : %s\n", nom, d.ligne, d.colonne, d.message);
        else         fprintf(stderr, "%s : erreur : %s\n", nom, d.message);
        diagnostic_liberer(&d);
        code = EXIT_FAILURE;
    }
    portee_detruire(portee);
    free(source);
    return code;
}
