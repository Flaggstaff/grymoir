/* GrymoiR : chemins et lecture des fichiers source utilisés (grammaire, § 21). */
#define _XOPEN_SOURCE 700   /* realpath (POSIX 2008, XSI) */

#include "chemins.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *chemin_absolu(const char *chemin) {
#ifdef _WIN32
    char *r = _fullpath(NULL, chemin, 0);
    if (!r) return NULL;
    /* _fullpath ne change pas la casse : deux écritures d'un même fichier donneraient deux clés */
    for (char *p = r; *p; p++) {
        if (*p == '\\') *p = '/';
        else if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    }
    char *c = grym_dupliquer(r);
    free(r);
    return c;
#else
    char *r = realpath(chemin, NULL);
    if (!r) return NULL;
    char *c = grym_dupliquer(r);
    free(r);
    return c;
#endif
}

int chemin_existe(const char *chemin) {
    FILE *f = fopen(chemin, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

char *chemin_lire(const char *chemin, size_t *taille) {
    FILE *f = fopen(chemin, "rb");
    if (!f) return NULL;
    size_t n = 0, cap = 4096;
    char *d = grym_allouer(cap + 1);
    size_t lu;
    while ((lu = fread(d + n, 1, cap - n, f)) > 0) {
        n += lu;
        if (n == cap) {
            char *e = realloc(d, cap * 2 + 1);
            if (!e) { free(d); fclose(f); return NULL; }
            d = e;
            cap *= 2;
        }
    }
    int erreur = ferror(f);
    fclose(f);
    if (erreur) { free(d); return NULL; }
    d[n] = '\0';
    *taille = n;
    return d;
}
