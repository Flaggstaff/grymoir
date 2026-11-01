/* GrymoiR : le morse d'un son (grammaire 2.78). Voir morse.h.
 *
 * La méthode, en quatre temps :
 *  1. la note : parmi les fenêtres les plus fortes du son, la fréquence de 300 à 1500 Hz qui porte le plus d'énergie
 *     (algorithme de Goertzel, par pas de 10 Hz) ; si elle n'en porte pas l'essentiel, ce n'est pas une note pure : pas
 *     de morse (la voix étale son énergie sur bien des fréquences) ;
 *  2. l'enveloppe : la force de cette note toutes les 4 ms, puis « porteuse » ou « silence » par deux seuils (hystérésis)
 *     posés entre le bruit de fond et les crêtes ;
 *  3. la vitesse : la durée du point, tirée des durées des porteuses (deux groupes : points et traits) et des silences
 *     courts ; un trait dure plus de deux points, un silence de plus de deux points sépare deux lettres, de plus de cinq
 *     deux mots ;
 *  4. la lecture : la table du code morse international (UIT-R M.1677-1), lettres, chiffres, ponctuation, et « <SK> » ;
 *     une suite inconnue s'écrit « ? » ; trop de « ? », ou moins de deux signes reconnus : pas de morse.
 */
#include "morse.h"
#include "texte.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

static const struct { const char *code, *lettre; } TABLE[] = {
    {".-", "A"}, {"-...", "B"}, {"-.-.", "C"}, {"-..", "D"}, {".", "E"}, {"..-.", "F"}, {"--.", "G"}, {"....", "H"},
    {"..", "I"}, {".---", "J"}, {"-.-", "K"}, {".-..", "L"}, {"--", "M"}, {"-.", "N"}, {"---", "O"}, {".--.", "P"},
    {"--.-", "Q"}, {".-.", "R"}, {"...", "S"}, {"-", "T"}, {"..-", "U"}, {"...-", "V"}, {".--", "W"}, {"-..-", "X"},
    {"-.--", "Y"}, {"--..", "Z"},
    {"-----", "0"}, {".----", "1"}, {"..---", "2"}, {"...--", "3"}, {"....-", "4"}, {".....", "5"}, {"-....", "6"},
    {"--...", "7"}, {"---..", "8"}, {"----.", "9"},
    {".-.-.-", "."}, {"--..--", ","}, {"..--..", "?"}, {".----.", "'"}, {"-.-.--", "!"}, {"-..-.", "/"}, {"-.--.", "("},
    {"-.--.-", ")"}, {".-...", "&"}, {"---...", ":"}, {"-.-.-.", ";"}, {"-...-", "="}, {".-.-.", "+"}, {"-....-", "-"},
    {"..--.-", "_"}, {".-..-.", "\""}, {".--.-.", "@"}, {"...-.-", "<SK>"},
};

static const char *lettre_de(const char *code) {
    for (size_t k = 0; k < sizeof TABLE / sizeof *TABLE; k++)
        if (strcmp(TABLE[k].code, code) == 0) return TABLE[k].lettre;
    return NULL;
}

static uint32_t lu32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t lu16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Les échantillons d'un WAV PCM 16 bits, ramenés en mono, de −1 à 1 ; NULL si ce n'en est pas un. */
static float *lire_wav(const unsigned char *o, size_t t, size_t *n, int *frequence) {
    if (!o || t < 44 || memcmp(o, "RIFF", 4) != 0 || memcmp(o + 8, "WAVE", 4) != 0) return NULL;
    int canaux = 0, bits = 0, format = 0;
    const unsigned char *donnees = NULL;
    size_t taille = 0;
    for (size_t p = 12; p + 8 <= t;) {
        const uint32_t l = lu32(o + p + 4);
        if (memcmp(o + p, "fmt ", 4) == 0 && l >= 16 && p + 8 + 16 <= t) {
            format = lu16(o + p + 8);
            canaux = lu16(o + p + 10);
            *frequence = (int)lu32(o + p + 12);
            bits = lu16(o + p + 22);
        } else if (memcmp(o + p, "data", 4) == 0) {
            donnees = o + p + 8;
            taille = l <= t - (p + 8) ? l : t - (p + 8);
            break;
        }
        p += 8 + (size_t)l + (l & 1);
    }
    if (!donnees || format != 1 || bits != 16 || canaux < 1 || canaux > 8 || *frequence < 4000 || *frequence > 192000) return NULL;
    *n = taille / (size_t)(2 * canaux);
    if (*n < (size_t)*frequence / 10) return NULL;   /* moins d'un dixième de seconde */
    float *x = malloc(*n * sizeof *x);
    if (!x) return NULL;
    for (size_t i = 0; i < *n; i++) {
        float s = 0;
        for (int c = 0; c < canaux; c++) s += (float)(int16_t)lu16(donnees + 2 * (i * (size_t)canaux + (size_t)c)) / 32768.0f;
        x[i] = s / (float)canaux;
    }
    return x;
}

/* La puissance d'une fréquence dans un bloc (Goertzel), rapportée à celle d'une note pure d'amplitude 1. */
static double goertzel(const float *x, size_t n, double f, int fs) {
    const double w = 2 * PI * f / fs, c = 2 * cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 4 * p / ((double)n * (double)n);
}

static int comparer_double(const void *a, const void *b) {
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* Deux groupes (k-moyennes à une dimension) : le centre du plus petit et du plus grand. */
static void deux_groupes(const double *v, size_t n, double *bas, double *haut) {
    double a = v[0], b = v[0];
    for (size_t i = 0; i < n; i++) { if (v[i] < a) a = v[i]; if (v[i] > b) b = v[i]; }
    for (int tour = 0; tour < 20; tour++) {
        double sa = 0, sb = 0;
        size_t na = 0, nb = 0;
        for (size_t i = 0; i < n; i++)
            if (fabs(v[i] - a) <= fabs(v[i] - b)) { sa += v[i]; na++; } else { sb += v[i]; nb++; }
        if (na) a = sa / (double)na;
        if (nb) b = sb / (double)nb;
    }
    *bas = a;
    *haut = b;
}

char *morse_decoder(const unsigned char *octets, size_t taille) {
    size_t n = 0;
    int fs = 0;
    float *x = lire_wav(octets, taille, &n, &fs);
    if (!x) return NULL;
    char *resultat = NULL;
    /* 1. la note : les 40 fenêtres de 32 ms les plus fortes */
    const size_t fen = (size_t)fs * 32 / 1000;
    const size_t nf = n / fen;
    double *energies = malloc((nf ? nf : 1) * sizeof *energies);
    size_t *rangs = malloc((nf ? nf : 1) * sizeof *rangs);
    double *mag = NULL;
    unsigned char *porteuse = NULL;
    double *marques = NULL, *silences = NULL;
    if (!energies || !rangs || nf < 3) goto fin;
    for (size_t k = 0; k < nf; k++) {
        double e = 0;
        for (size_t i = 0; i < fen; i++) e += (double)x[k * fen + i] * x[k * fen + i];
        energies[k] = e / (double)fen;
        rangs[k] = k;
    }
    for (size_t a = 0; a < nf && a < 40; a++)   /* les 40 premières, par sélection */
        for (size_t b = a + 1; b < nf; b++)
            if (energies[rangs[b]] > energies[rangs[a]]) { size_t t = rangs[a]; rangs[a] = rangs[b]; rangs[b] = t; }
    const size_t fortes = nf < 40 ? nf : 40;
    double meilleure = 0, f0 = 0, energie_fortes = 0;
    for (size_t q = 0; q < fortes; q++) energie_fortes += energies[rangs[q]];
    for (int f = 300; f <= 1500; f += 10) {
        double p = 0;
        for (size_t q = 0; q < fortes; q++) p += goertzel(x + rangs[q] * fen, fen, f, fs);
        if (p > meilleure) { meilleure = p; f0 = f; }
    }
    /* une note pure d'amplitude A : énergie A²/2, puissance de Goertzel A² ; la part de la note dans le son */
    const double purete = energie_fortes > 0 ? meilleure / (2 * energie_fortes) : 0;
    if (purete < 0.35) goto fin;
    /* 2. l'enveloppe, toutes les 4 ms, sur un bloc de 8 ms */
    const size_t pas = (size_t)fs * 4 / 1000, bloc = 2 * pas;
    const size_t nb = n > bloc ? (n - bloc) / pas + 1 : 0;
    if (nb < 10) goto fin;
    mag = malloc(nb * sizeof *mag);
    porteuse = malloc(nb);
    double *tri = malloc(nb * sizeof *tri);
    if (!mag || !porteuse || !tri) { free(tri); goto fin; }
    for (size_t k = 0; k < nb; k++) tri[k] = mag[k] = sqrt(goertzel(x + k * pas, bloc, f0, fs));
    qsort(tri, nb, sizeof *tri, comparer_double);
    const double fond = tri[nb / 5], crete = tri[nb * 95 / 100];
    free(tri);
    if (crete < 0.01 || crete < 3 * fond) goto fin;
    const double monte = fond + 0.5 * (crete - fond), descend = fond + 0.3 * (crete - fond);
    int etat = 0;
    for (size_t k = 0; k < nb; k++) {
        if (!etat && mag[k] >= monte) etat = 1;
        else if (etat && mag[k] < descend) etat = 0;
        porteuse[k] = (unsigned char)etat;
    }
    /* 3. les durées, en blocs de 4 ms */
    marques = malloc(nb * sizeof *marques);
    silences = malloc(nb * sizeof *silences);
    if (!marques || !silences) goto fin;
    size_t nm = 0, ns = 0, premier = nb, dernier = 0;
    for (size_t k = 0; k < nb; k++) if (porteuse[k]) { if (premier == nb) premier = k; dernier = k; }
    if (premier == nb) goto fin;
    /* les durées, dans l'ordre : positives, des porteuses ; négatives, des silences entre elles */
    double *suite = malloc(nb * sizeof *suite);
    size_t nsuite = 0;
    if (!suite) goto fin;
    for (size_t k = premier; k <= dernier;) {
        size_t l = k;
        while (l <= dernier && porteuse[l] == porteuse[k]) l++;
        const double d = (double)(l - k);
        if (porteuse[k]) { marques[nm++] = d; suite[nsuite++] = d; }
        else { silences[ns++] = d; suite[nsuite++] = -d; }
        k = l;
    }
    if (nm < 2) { free(suite); goto fin; }
    double court, long_;
    deux_groupes(marques, nm, &court, &long_);
    double point = court;
    if (long_ < 2 * court) {   /* une seule sorte de porteuse : points seuls, ou traits seuls ; les silences courts tranchent */
        point = court;
        if (ns) {
            double sc, sl;
            deux_groupes(silences, ns, &sc, &sl);
            if (sc < point / 2) point = sc;   /* les porteuses sont des traits */
        }
    }
    if (point < 1) point = 1;
    /* 4. la lecture */
    Chaine texte = {0};
    char code[16];
    size_t lc = 0;
    int signes = 0, inconnus = 0, trop_long = 0;
    for (size_t i = 0; i <= nsuite; i++) {
        const double d = i < nsuite ? suite[i] : -100 * point;   /* un silence de fin ferme la dernière lettre */
        if (d > 0) {
            if (lc < sizeof code - 1) code[lc++] = d > 2 * point ? '-' : '.';
            else trop_long = 1;
            continue;
        }
        if (-d < 2 * point || !lc) continue;   /* entre deux signes d'une même lettre */
        code[lc] = 0;
        const char *l = trop_long ? NULL : lettre_de(code);
        chaine_ajouter(&texte, l ? l : "?");
        if (l) signes++; else inconnus++;
        lc = 0;
        trop_long = 0;
        if (-d >= 5 * point && i < nsuite) chaine_ajouter(&texte, " ");
    }
    free(suite);
    char *t = chaine_rendre(&texte);
    if (!t || signes < 2 || inconnus * 10 > (signes + inconnus) * 3) { free(t); goto fin; }
    resultat = t;
fin:
    free(x);
    free(energies);
    free(rangs);
    free(mag);
    free(porteuse);
    free(marques);
    free(silences);
    return resultat;
}

/* --- pour les essais --- */

static const char *code_de(char c) {
    char l[2] = { (char)(c >= 'a' && c <= 'z' ? c - 32 : c), 0 };
    for (size_t k = 0; k < sizeof TABLE / sizeof *TABLE; k++)
        if (strcmp(TABLE[k].lettre, l) == 0) return TABLE[k].code;
    return NULL;
}

unsigned char *morse_fabriquer(const char *texte, int mots_par_minute, int hertz, double bruit, size_t *taille) {
    const int fs = 16000;
    const double unite = 1.2 / (mots_par_minute > 0 ? mots_par_minute : 20);   /* PARIS : 50 unités par mot */
    size_t cap = (size_t)fs, n = 0;
    float *x = malloc(cap * sizeof *x);
    uint32_t graine = 12345;
    #define AJOUTER(duree, note) do { \
            const size_t m_ = (size_t)((duree) * fs); \
            for (size_t i_ = 0; i_ < m_; i_++) { \
                if (n == cap) { cap *= 2; x = realloc(x, cap * sizeof *x); } \
                const double rampe_ = fmin(1.0, fmin((double)i_, (double)(m_ - i_)) / (0.005 * fs)); \
                graine = graine * 1103515245u + 12345u; \
                const double b_ = bruit * (((double)(graine >> 8) / 16777216.0) * 2 - 1); \
                x[n++] = (float)((note) ? 0.5 * rampe_ * sin(2 * PI * hertz * (double)i_ / fs) + b_ : b_); \
            } \
        } while (0)
    AJOUTER(0.3, 0);
    for (const char *p = texte; *p; p++) {
        if (*p == ' ') { AJOUTER(4 * unite, 0); continue; }   /* 7 unités en tout entre deux mots */
        const char *c = code_de(*p);
        if (!c) continue;
        for (const char *e = c; *e; e++) {
            AJOUTER((*e == '-' ? 3 : 1) * unite, 1);
            AJOUTER(unite, 0);
        }
        AJOUTER(2 * unite, 0);   /* 3 unités en tout entre deux lettres */
    }
    AJOUTER(0.3, 0);
    #undef AJOUTER
    *taille = 44 + 2 * n;
    unsigned char *o = malloc(*taille);
    memcpy(o, "RIFF", 4);
    const uint32_t r = (uint32_t)(*taille - 8), d = (uint32_t)(2 * n);
    o[4] = r & 255; o[5] = r >> 8 & 255; o[6] = r >> 16 & 255; o[7] = r >> 24;
    memcpy(o + 8, "WAVEfmt ", 8);
    const unsigned char fmt[16] = { 16, 0, 0, 0, 1, 0, 1, 0, 0x80, 0x3e, 0, 0, 0, 0x7d, 0, 0 };
    memcpy(o + 16, fmt, 16);
    o[32] = 2; o[33] = 0; o[34] = 16; o[35] = 0;
    memcpy(o + 36, "data", 4);
    o[40] = d & 255; o[41] = d >> 8 & 255; o[42] = d >> 16 & 255; o[43] = d >> 24;
    for (size_t i = 0; i < n; i++) {
        double v = x[i];
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        const int16_t s = (int16_t)lrint(v * 32767);
        o[44 + 2 * i] = (unsigned char)(s & 255);
        o[45 + 2 * i] = (unsigned char)((uint16_t)s >> 8);
    }
    free(x);
    return o;
}
