/* GrymoiR : utilitaires de texte UTF-8 partagés. */
#include "texte.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *grym_allouer(size_t taille) {
    void *p = malloc(taille ? taille : 1);
    if (!p) {
        fputs("GrymoiR : mémoire épuisée\n", stderr);
        exit(EXIT_FAILURE);
    }
    return p;
}

char *grym_dupliquer(const char *s) {
    size_t n = strlen(s);
    char *d = grym_allouer(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

char *grym_formater(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    char *s = grym_allouer((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

void chaine_ajouter(Chaine *c, const char *s) {
    size_t n = strlen(s);
    if (c->n + n + 1 > c->cap) {
        size_t cap = c->cap ? c->cap : 64;
        while (c->n + n + 1 > cap) cap *= 2;
        char *d = realloc(c->d, cap);
        if (!d) {
            fputs("GrymoiR : mémoire épuisée\n", stderr);
            exit(EXIT_FAILURE);
        }
        c->d = d;
        c->cap = cap;
    }
    memcpy(c->d + c->n, s, n + 1);
    c->n += n;
}

char *chaine_rendre(Chaine *c) {
    if (!c->d) return grym_dupliquer("");
    return c->d;
}

/* Décodage minimal : la source a déjà été validée par le lexeur. */
static size_t decoder(const char *s, uint32_t *sortie, size_t max) {
    const unsigned char *p = (const unsigned char *)s;
    size_t k = 0;
    while (*p && k < max) {
        uint32_t v;
        int len;
        if (*p < 0x80)                { v = *p;        len = 1; }
        else if ((*p & 0xE0) == 0xC0) { v = *p & 0x1F; len = 2; }
        else if ((*p & 0xF0) == 0xE0) { v = *p & 0x0F; len = 3; }
        else                          { v = *p & 0x07; len = 4; }
        p++;
        for (int i = 1; i < len && *p; i++, p++) v = (v << 6) | (*p & 0x3F);
        sortie[k++] = v;
    }
    return k;
}

size_t longueur_utf8(const char *s) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}

size_t distance_edition(const char *a, const char *b) {
    enum { MAX = 128 };
    uint32_t x[MAX], y[MAX];
    size_t m = decoder(a, x, MAX), n = decoder(b, y, MAX);
    size_t ligne[MAX + 1];
    for (size_t j = 0; j <= n; j++) ligne[j] = j;
    for (size_t i = 1; i <= m; i++) {
        size_t diag = ligne[0];
        ligne[0] = i;
        for (size_t j = 1; j <= n; j++) {
            size_t haut = ligne[j];
            size_t cout = x[i - 1] == y[j - 1] ? 0 : 1;
            size_t v = diag + cout;
            if (haut + 1 < v) v = haut + 1;
            if (ligne[j - 1] + 1 < v) v = ligne[j - 1] + 1;
            ligne[j] = v;
            diag = haut;
        }
    }
    return ligne[n];
}
