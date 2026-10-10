/* GrymoiR : chemins et lecture des fichiers source utilisés (grammaire, § 21). */
#define _XOPEN_SOURCE 700   /* realpath (POSIX 2008, XSI) */

#include "chemins.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

#ifdef _WIN32
/* UTF-8 → UTF-16, pour l'API « large » de Windows ; NULL si le texte n'est pas de l'UTF-8 valide. */
static wchar_t *large(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = grym_allouer((size_t)n * sizeof *w);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    return w;
}

/* UTF-16 → UTF-8. */
static char *etroit(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = grym_allouer((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

FILE *grym_fopen(const char *chemin, const char *mode) {
    wchar_t *c = large(chemin), *m = large(mode);
    FILE *f = c && m ? _wfopen(c, m) : NULL;
    free(c);
    free(m);
    return f;
}

int grym_supprimer(const char *chemin) {
    wchar_t *c = large(chemin);
    int r = c ? _wremove(c) : -1;
    free(c);
    return r;
}
#else
FILE *grym_fopen(const char *chemin, const char *mode) { return fopen(chemin, mode); }

int grym_supprimer(const char *chemin) { return remove(chemin); }
#endif

char *chemin_absolu(const char *chemin) {
#ifdef _WIN32
    wchar_t *l = large(chemin);
    wchar_t *pl = l ? _wfullpath(NULL, l, 0) : NULL;
    free(l);
    if (!pl) return NULL;
    char *r = etroit(pl);
    free(pl);
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
    FILE *f = grym_fopen(chemin, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

char *chemin_lire(const char *chemin, size_t *taille) {
    FILE *f = grym_fopen(chemin, "rb");
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
