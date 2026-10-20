/* L'empreinte d'un mot de passe : voir empreinte.h. SHA-256 selon FIPS 180-4, HMAC selon RFC 2104, PBKDF2 selon
 * RFC 8018 (§ 5.2) ; vérifiés par les essais contre l'implémentation de référence de Python (hashlib). */
#ifdef _WIN32
#define _CRT_RAND_S   /* rand_s : la source aléatoire du système, sans bibliothèque de plus */
#endif
#include "empreinte.h"
#include "texte.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROTD(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

typedef struct { uint32_t h[8]; unsigned char bloc[64]; size_t n; uint64_t total; } Sha;

static void sha_bloc(Sha *s, const unsigned char *b) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = ROTD(w[i - 15], 7) ^ ROTD(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ROTD(w[i - 2], 17) ^ ROTD(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b2 = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t t1 = h + (ROTD(e, 6) ^ ROTD(e, 11) ^ ROTD(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint32_t t2 = (ROTD(a, 2) ^ ROTD(a, 13) ^ ROTD(a, 22)) + ((a & b2) ^ (a & c) ^ (b2 & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b2; b2 = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b2; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_debut(Sha *s) {
    static const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, H0, sizeof H0);
    s->n = 0;
    s->total = 0;
}

static void sha_ajouter(Sha *s, const unsigned char *d, size_t n) {
    s->total += n;
    while (n) {
        const size_t k = 64 - s->n < n ? 64 - s->n : n;
        memcpy(s->bloc + s->n, d, k);
        s->n += k; d += k; n -= k;
        if (s->n == 64) { sha_bloc(s, s->bloc); s->n = 0; }
    }
}

static void sha_fin(Sha *s, unsigned char out[32]) {
    const uint64_t bits = s->total * 8;
    const unsigned char un = 0x80, zero = 0;
    sha_ajouter(s, &un, 1);
    while (s->n != 56) sha_ajouter(s, &zero, 1);
    unsigned char l[8];
    for (int i = 0; i < 8; i++) l[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha_ajouter(s, l, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (unsigned char)(s->h[i] >> 24); out[4 * i + 1] = (unsigned char)(s->h[i] >> 16);
        out[4 * i + 2] = (unsigned char)(s->h[i] >> 8); out[4 * i + 3] = (unsigned char)s->h[i];
    }
}

void sha256(const unsigned char *d, size_t n, unsigned char out[32]) {
    Sha s;
    sha_debut(&s);
    sha_ajouter(&s, d, n);
    sha_fin(&s, out);
}

/* HMAC-SHA256 préparé : les états après la clé xor ipad et opad, réutilisés à chaque itération de PBKDF2. */
typedef struct { Sha interne, externe; } Hmac;

static void hmac_preparer(Hmac *h, const unsigned char *cle, size_t n) {
    unsigned char k[64] = {0}, ip[64], op[64];
    if (n > 64) sha256(cle, n, k); else memcpy(k, cle, n);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha_debut(&h->interne); sha_ajouter(&h->interne, ip, 64);
    sha_debut(&h->externe); sha_ajouter(&h->externe, op, 64);
}

static void hmac(const Hmac *h, const unsigned char *d, size_t n, unsigned char out[32]) {
    Sha s = h->interne;
    unsigned char m[32];
    sha_ajouter(&s, d, n);
    sha_fin(&s, m);
    s = h->externe;
    sha_ajouter(&s, m, 32);
    sha_fin(&s, out);
}

void pbkdf2_sha256(const unsigned char *mdp, size_t nm, const unsigned char *sel, size_t ns, unsigned long iterations,
                   unsigned char *cle, size_t nc) {
    Hmac h;
    hmac_preparer(&h, mdp, nm);
    unsigned char *premier = malloc(ns + 4);
    for (uint32_t bloc = 1; nc; bloc++) {
        unsigned char u[32], t[32];
        memcpy(premier, sel, ns);
        premier[ns] = (unsigned char)(bloc >> 24); premier[ns + 1] = (unsigned char)(bloc >> 16);
        premier[ns + 2] = (unsigned char)(bloc >> 8); premier[ns + 3] = (unsigned char)bloc;
        hmac(&h, premier, ns + 4, u);
        memcpy(t, u, 32);
        for (unsigned long i = 1; i < iterations; i++) {
            hmac(&h, u, 32, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }
        const size_t k = nc < 32 ? nc : 32;
        memcpy(cle, t, k);
        cle += k; nc -= k;
    }
    free(premier);
}

int aleatoire_systeme(unsigned char *o, size_t n) {
#ifdef _WIN32
    for (size_t i = 0; i < n; i += 4) {
        unsigned int r;
        if (rand_s(&r)) return 0;
        for (size_t j = 0; j < 4 && i + j < n; j++) o[i + j] = (unsigned char)(r >> (8 * j));
    }
    return 1;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return 0;
    const int ok = fread(o, 1, n, f) == n;
    fclose(f);
    return ok;
#endif
}

static void hexa(const unsigned char *o, size_t n, char *s) {
    for (size_t i = 0; i < n; i++) sprintf(s + 2 * i, "%02x", o[i]);
}

static int lire_hexa(const char *s, size_t n, unsigned char *o) {
    for (size_t i = 0; i < n; i++) {
        unsigned int v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        o[i] = (unsigned char)v;
    }
    return 1;
}

char *empreinte_creer(const char *mdp) {
    unsigned char sel[16], cle[32];
    if (!aleatoire_systeme(sel, sizeof sel)) return NULL;
    pbkdf2_sha256((const unsigned char *)mdp, strlen(mdp), sel, sizeof sel, EMPREINTE_ITERATIONS, cle, sizeof cle);
    char s[33], c[65];
    hexa(sel, sizeof sel, s);
    hexa(cle, sizeof cle, c);
    return grym_formater("pbkdf2-sha256$%d$%s$%s", EMPREINTE_ITERATIONS, s, c);
}

int empreinte_verifier(const char *mdp, const char *e) {
    unsigned long it;
    char s[33], c[65];
    if (!e || sscanf(e, "pbkdf2-sha256$%lu$%32[0-9a-f]$%64[0-9a-f]", &it, s, c) != 3 || strlen(s) != 32 || strlen(c) != 64
        || it < 1 || it > 100000000UL)
        return 0;
    unsigned char sel[16], attendue[32], cle[32];
    if (!lire_hexa(s, 16, sel) || !lire_hexa(c, 32, attendue)) return 0;
    pbkdf2_sha256((const unsigned char *)mdp, strlen(mdp), sel, sizeof sel, it, cle, sizeof cle);
    unsigned char ecart = 0;
    for (int i = 0; i < 32; i++) ecart |= (unsigned char)(cle[i] ^ attendue[i]);   /* temps constant */
    return ecart == 0;
}
