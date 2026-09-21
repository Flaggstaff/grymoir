/* grym-lexeur : affiche les jetons d'un fichier .grym (outil de mise au point, v0.1).
 * Usage : grym-lexeur fichier.grym     ou     grym-lexeur < fichier.grym
 */
#include "lexeur.h"

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
    SetConsoleOutputCP(CP_UTF8);   /* sans cela, la console Windows massacre les accents */
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

    char *erreur = NULL;
    Lexeur *lx = lexeur_creer(source, taille, &erreur);
    if (!lx) {
        fprintf(stderr, "%s : %s\n", nom, erreur);
        free(erreur);
        free(source);
        return EXIT_FAILURE;
    }

    int code = EXIT_SUCCESS;
    for (;;) {
        Jeton j = lexeur_suivant(lx);
        if (j.type == J_FIN) break;
        if (j.type == J_ERREUR) {
            fprintf(stderr, "%s:%d:%d : erreur : %s\n", nom, j.ligne, j.colonne, j.valeur);
            jeton_liberer(&j);
            code = EXIT_FAILURE;
            break;
        }
        printf("%4d:%-3d  %-12s %s\n", j.ligne, j.colonne,
               type_jeton_nom(j.type), j.valeur ? j.valeur : "");
        jeton_liberer(&j);
    }
    lexeur_detruire(lx);
    free(source);
    return code;
}
