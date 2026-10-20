/* Le code QR en PNG (grammaire 2.27). Voir qr.h. */
#include "qr.h"
#include "texte.h"
#include "../vendor/qrcodegen/qrcodegen.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- PNG minimal : niveaux de gris 8 bits, compression « stockée » (zlib sans compresser), CRC et Adler --- */

static uint32_t crc_table[256];
static void crc_preparer(void) {
    if (crc_table[1]) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc(const unsigned char *o, size_t n, uint32_t c) {
    c ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ o[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

typedef struct { unsigned char *o; size_t n, cap; } Tampon;
static void ajout(Tampon *t, const void *x, size_t n) {
    if (t->n + n > t->cap) {
        t->cap = (t->n + n) * 2 + 64;
        unsigned char *x = grym_allouer(t->cap);
        if (t->n) memcpy(x, t->o, t->n);
        free(t->o);
        t->o = x;
    }
    memcpy(t->o + t->n, x, n);
    t->n += n;
}
static void ajout32(Tampon *t, uint32_t v) {
    unsigned char b[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    ajout(t, b, 4);
}
static void morceau(Tampon *t, const char *type, const unsigned char *d, size_t n) {
    ajout32(t, (uint32_t)n);
    const size_t debut = t->n;
    ajout(t, type, 4);
    if (n) ajout(t, d, n);
    ajout32(t, crc(t->o + debut, n + 4, 0));
}

/* Noir et blanc, un bit par pixel : la mise en page d'un PDF le garde tel quel (une image grise ou en couleurs y
 * devient un JPEG, et ses bords flous gênent la lecture du code). */
static unsigned char *png_gris(const unsigned char *pixels, int l, int h, size_t *taille) {
    crc_preparer();
    /* les lignes, chacune précédée de son filtre (0), huit pixels par octet, le blanc à 1 */
    const size_t ligne = (size_t)(l + 7) / 8 + 1, brut = (size_t)h * ligne;
    unsigned char *r = grym_allouer(brut);
    memset(r, 0, brut);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < l; x++)
            if (pixels[(size_t)y * (size_t)l + (size_t)x] >= 128)
                r[(size_t)y * ligne + 1 + (size_t)x / 8] |= (unsigned char)(0x80 >> (x % 8));
    Tampon z = {0};
    const unsigned char tete[2] = { 0x78, 0x01 };
    ajout(&z, tete, 2);
    for (size_t p = 0; p < brut || brut == 0; ) {
        const size_t n = brut - p > 65535 ? 65535 : brut - p;
        const unsigned char b[5] = { (unsigned char)(p + n == brut), (unsigned char)n, (unsigned char)(n >> 8),
                                     (unsigned char)~n, (unsigned char)(~n >> 8) };
        ajout(&z, b, 5);
        ajout(&z, r + p, n);
        p += n;
        if (brut == 0) break;
    }
    uint32_t a = 1, bb = 0;
    for (size_t i = 0; i < brut; i++) { a = (a + r[i]) % 65521; bb = (bb + a) % 65521; }
    ajout32(&z, bb << 16 | a);
    free(r);
    Tampon t = {0};
    const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    ajout(&t, signature, 8);
    unsigned char ihdr[13] = { (unsigned char)(l >> 24), (unsigned char)(l >> 16), (unsigned char)(l >> 8), (unsigned char)l,
                               (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h,
                               1, 0, 0, 0, 0 };   /* profondeur 1, niveaux de gris */
    morceau(&t, "IHDR", ihdr, 13);
    morceau(&t, "IDAT", z.o, z.n);
    morceau(&t, "IEND", NULL, 0);
    free(z.o);
    *taille = t.n;
    return t.o;
}

/* --- le code --- */

unsigned char *qr_png(const char *texte, int suisse, size_t *taille, char **erreur) {
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    const size_t n = strlen(texte);
    if (n >= qrcodegen_BUFFER_LEN_MAX) {
        *erreur = grym_dupliquer("Un code QR contient au plus 2953 octets.");
        return NULL;
    }
    memcpy(tmp, texte, n);
    /* en octets (UTF-8 tel quel) : le Swiss QR Code est du « binary coding » (SIX IG 2.4, § 6.2) */
    const int ok = qrcodegen_encodeBinary(tmp, n, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                          suisse ? 25 : qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, !suisse);
    if (!ok) {
        *erreur = grym_dupliquer(suisse ? "Un code QR suisse contient au plus 997 caractères (SIX, Implementation Guidelines, § 6.2)."
                                        : "Ce texte ne tient pas dans un code QR.");
        return NULL;
    }
    const int m = qrcodegen_getSize(qr);
    const int calme = suisse ? 0 : 4, echelle = 10;
    const int cote = (m + 2 * calme) * echelle;
    unsigned char *p = grym_allouer((size_t)cote * (size_t)cote);
    memset(p, 255, (size_t)cote * (size_t)cote);
    for (int y = 0; y < m; y++)
        for (int x = 0; x < m; x++)
            if (qrcodegen_getModule(qr, x, y))
                for (int dy = 0; dy < echelle; dy++)
                    memset(p + (size_t)((calme + y) * echelle + dy) * (size_t)cote + (size_t)((calme + x) * echelle), 0, (size_t)echelle);
    if (suisse) {
        /* la croix (§ 6.4.2) : 7 mm sur 46 mm ; un carré blanc, un carré noir dedans, la croix blanche ; proportions
         * de la croix du drapeau (bras de 6 sur 20, sur un carré de 32) ; une approximation du fichier officiel */
        const double c = cote / 2.0, total = cote * 7.0 / 46.0, noir = total * 0.86;
        const double bras = noir * 20.0 / 32.0 / 2.0, larg = noir * 6.0 / 32.0 / 2.0;
        for (int y = 0; y < cote; y++)
            for (int x = 0; x < cote; x++) {
                const double dx = x + 0.5 - c, dy = y + 0.5 - c, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
                if (ax > total / 2 || ay > total / 2) continue;
                unsigned char v = 255;
                if (ax <= noir / 2 && ay <= noir / 2) {
                    v = 0;
                    if ((ax <= larg && ay <= bras) || (ay <= larg && ax <= bras)) v = 255;
                }
                p[(size_t)y * (size_t)cote + (size_t)x] = v;
            }
    }
    unsigned char *png = png_gris(p, cote, cote, taille);
    free(p);
    return png;
}
