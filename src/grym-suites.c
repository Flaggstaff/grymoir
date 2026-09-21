/* grym-suites : affiche les suites valides à la fin d'un texte (aide à la saisie, v0.1).
 * Le texte lu représente tout ce qui précède le curseur.
 * Usage : printf 'Le total vaut 1.\nLe to' | grym-suites
 */
#include "analyseur.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : stdin;
    if (!f) {
        fprintf(stderr, "Impossible d'ouvrir « %s ».\n", argv[1]);
        return EXIT_FAILURE;
    }
    size_t cap = 4096, n = 0;
    char *d = malloc(cap);
    if (!d) return EXIT_FAILURE;
    for (;;) {
        if (n == cap) {
            char *e = realloc(d, cap *= 2);
            if (!e) { free(d); return EXIT_FAILURE; }
            d = e;
        }
        size_t lu = fread(d + n, 1, cap - n, f);
        n += lu;
        if (lu == 0) break;
    }
    if (f != stdin) fclose(f);

    Suggestions s = suites_valides(d, n);
    for (size_t k = 0; k < s.nb; k++) puts(s.items[k]);
    if (s.nb == 0) fputs("(aucune suite valide)\n", stderr);
    suggestions_liberer(&s);
    free(d);
    return EXIT_SUCCESS;
}
