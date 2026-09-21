/* GrymoiR : arithmétique décimale exacte, v0.1
 * Les entiers naturels sont des tableaux de chiffres décimaux, poids faible d'abord.
 * Algorithmes d'école (addition, multiplication, division longue) : lisibles
 * avant d'être rapides, conformément à la hiérarchie de la charte (art. 2).
 */
#include "decimal.h"
#include "texte.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- */
/* Entiers naturels                                                 */
/* ---------------------------------------------------------------- */

typedef struct {
    uint8_t *d;
    size_t n;   /* n == 0 : zéro ; sinon d[n - 1] != 0 */
} Nat;

static Nat nat_vide(size_t capacite) {
    Nat x;
    x.d = grym_allouer(capacite ? capacite : 1);
    memset(x.d, 0, capacite ? capacite : 1);
    x.n = 0;
    return x;
}

static void nat_normaliser(Nat *x) {
    while (x->n && x->d[x->n - 1] == 0) x->n--;
}

static Nat nat_depuis(const uint8_t *d, size_t n) {
    Nat x = nat_vide(n);
    if (n) memcpy(x.d, d, n);
    x.n = n;
    nat_normaliser(&x);
    return x;
}

static Nat nat_petit(unsigned v) {
    Nat x = nat_vide(12);
    while (v) { x.d[x.n++] = (uint8_t)(v % 10); v /= 10; }
    return x;
}

static void nat_liberer(Nat *x) {
    free(x->d);
    x->d = NULL;
    x->n = 0;
}

static int nat_comparer(const Nat *a, const Nat *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (size_t i = a->n; i > 0; i--)
        if (a->d[i - 1] != b->d[i - 1]) return a->d[i - 1] < b->d[i - 1] ? -1 : 1;
    return 0;
}

static Nat nat_additionner(const Nat *a, const Nat *b) {
    size_t n = (a->n > b->n ? a->n : b->n) + 1;
    Nat r = nat_vide(n);
    unsigned retenue = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned s = retenue + (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
        r.d[i] = (uint8_t)(s % 10);
        retenue = s / 10;
    }
    r.n = n;
    nat_normaliser(&r);
    return r;
}

/* a − b, avec a ≥ b */
static Nat nat_soustraire(const Nat *a, const Nat *b) {
    Nat r = nat_vide(a->n);
    int emprunt = 0;
    for (size_t i = 0; i < a->n; i++) {
        int s = a->d[i] - emprunt - (i < b->n ? b->d[i] : 0);
        emprunt = s < 0;
        r.d[i] = (uint8_t)(s < 0 ? s + 10 : s);
    }
    r.n = a->n;
    nat_normaliser(&r);
    return r;
}

/* a −= b sur place, avec a ≥ b */
static void nat_soustraire_sur_place(Nat *a, const Nat *b) {
    int emprunt = 0;
    for (size_t i = 0; i < a->n; i++) {
        int s = a->d[i] - emprunt - (i < b->n ? b->d[i] : 0);
        emprunt = s < 0;
        a->d[i] = (uint8_t)(s < 0 ? s + 10 : s);
    }
    nat_normaliser(a);
}

static Nat nat_multiplier(const Nat *a, const Nat *b) {
    if (!a->n || !b->n) return nat_vide(1);
    size_t n = a->n + b->n;
    uint32_t *acc = grym_allouer(n * sizeof *acc);
    memset(acc, 0, n * sizeof *acc);
    for (size_t i = 0; i < a->n; i++)
        for (size_t j = 0; j < b->n; j++)
            acc[i + j] += (uint32_t)a->d[i] * b->d[j];
    Nat r = nat_vide(n);
    uint32_t retenue = 0;
    for (size_t k = 0; k < n; k++) {
        uint32_t s = acc[k] + retenue;
        r.d[k] = (uint8_t)(s % 10);
        retenue = s / 10;
    }
    free(acc);
    r.n = n;
    nat_normaliser(&r);
    return r;
}

/* a × 10^k */
static Nat nat_decaler(const Nat *a, size_t k) {
    if (!a->n) return nat_vide(1);
    Nat r = nat_vide(a->n + k);
    memcpy(r.d + k, a->d, a->n);
    r.n = a->n + k;
    return r;
}

/* a ÷ v sur place (v petit) ; renvoie le reste. */
static unsigned nat_diviser_petit(Nat *a, unsigned v) {
    unsigned reste = 0;
    for (size_t i = a->n; i > 0; i--) {
        unsigned cour = reste * 10 + a->d[i - 1];
        a->d[i - 1] = (uint8_t)(cour / v);
        reste = cour % v;
    }
    nat_normaliser(a);
    return reste;
}

/* Division longue, chiffre par chiffre : q = a ÷ b, r = a mod b (b ≠ 0). */
static void nat_diviser(const Nat *a, const Nat *b, Nat *q, Nat *r) {
    *q = nat_vide(a->n);
    Nat reste = nat_vide(b->n + 2);
    for (size_t i = a->n; i > 0; i--) {
        /* reste = reste × 10 + chiffre suivant */
        if (reste.n || a->d[i - 1]) {
            memmove(reste.d + 1, reste.d, reste.n);
            reste.d[0] = a->d[i - 1];
            reste.n++;
            nat_normaliser(&reste);
        }
        uint8_t chiffre = 0;
        while (nat_comparer(&reste, b) >= 0) {
            nat_soustraire_sur_place(&reste, b);
            chiffre++;
        }
        q->d[i - 1] = chiffre;
    }
    q->n = a->n;
    nat_normaliser(q);
    *r = reste;
}

/* ---------------------------------------------------------------- */
/* Décimaux                                                         */
/* ---------------------------------------------------------------- */

static Decimal dec_depuis_nat(Nat x, int negatif, long exp) {
    Decimal d;
    d.ch = x.d;
    d.n = x.n;
    d.negatif = x.n ? negatif : 0;
    d.exp = exp;
    return d;
}

static Nat nat_de(const Decimal *a) {
    Nat x;
    x.d = a->ch;
    x.n = a->n;
    return x;
}

Decimal dec_zero(void) {
    return dec_depuis_nat(nat_vide(1), 0, 0);
}

Decimal dec_depuis_canonique(const char *s) {
    int negatif = s[0] == '-';
    if (negatif) s++;
    size_t l = strlen(s);
    Nat x = nat_vide(l);
    long apres_point = -1;
    for (size_t i = l; i > 0; i--) {
        char c = s[i - 1];
        if (c == '.') { apres_point = (long)x.n; continue; }
        if (c >= '0' && c <= '9') x.d[x.n++] = (uint8_t)(c - '0');
    }
    nat_normaliser(&x);
    return dec_depuis_nat(x, negatif, apres_point < 0 ? 0 : -apres_point);
}

int dec_canonique_valide(const char *s) {
    if (*s == '-') s++;
    int chiffres = 0, point = 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') chiffres++;
        else if (*s == '.' && !point && chiffres) point = 1;
        else return 0;
    }
    return chiffres > 0 && s[-1] != '.';
}

Decimal dec_copier(const Decimal *a) {
    Nat x = nat_depuis(a->ch, a->n);
    return dec_depuis_nat(x, a->negatif, a->exp);
}

void dec_liberer(Decimal *a) {
    free(a->ch);
    a->ch = NULL;
    a->n = 0;
}

Decimal dec_negation(const Decimal *a) {
    Decimal r = dec_copier(a);
    if (r.n) r.negatif = !r.negatif;
    return r;
}

int dec_est_entier(const Decimal *a) {
    if (!a->n || a->exp >= 0) return 1;
    size_t f = (size_t)(-a->exp);
    for (size_t i = 0; i < f && i < a->n; i++) if (a->ch[i]) return 0;
    return 1;
}

int dec_comparer(const Decimal *a, const Decimal *b) {
    int sa = a->n ? (a->negatif ? -1 : 1) : 0;
    int sb = b->n ? (b->negatif ? -1 : 1) : 0;
    if (sa != sb) return sa < sb ? -1 : 1;
    if (sa == 0) return 0;
    long e = a->exp < b->exp ? a->exp : b->exp;
    Nat ma = nat_de(a), mb = nat_de(b);
    Nat xa = nat_decaler(&ma, (size_t)(a->exp - e));
    Nat xb = nat_decaler(&mb, (size_t)(b->exp - e));
    int c = nat_comparer(&xa, &xb);
    nat_liberer(&xa);
    nat_liberer(&xb);
    return sa > 0 ? c : -c;
}

/* Taille affichable : chiffres du coefficient, zéros ajoutés, décimales. */
static StatutDecimal verifier_taille(Decimal *r) {
    if (!r->n) return DEC_OK;
    long entiers = (long)r->n + r->exp;
    if (entiers > DEC_CHIFFRES_MAX || r->exp < -DEC_CHIFFRES_MAX) {
        dec_liberer(r);
        *r = dec_zero();
        return DEC_TROP_GRAND;
    }
    return DEC_OK;
}

StatutDecimal dec_addition(const Decimal *a, const Decimal *b, Decimal *r) {
    long e = a->exp < b->exp ? a->exp : b->exp;
    Nat ma = nat_de(a), mb = nat_de(b);
    Nat xa = nat_decaler(&ma, (size_t)(a->exp - e));
    Nat xb = nat_decaler(&mb, (size_t)(b->exp - e));
    Nat s;
    int negatif;
    if (a->negatif == b->negatif) {
        s = nat_additionner(&xa, &xb);
        negatif = a->negatif;
    } else if (nat_comparer(&xa, &xb) >= 0) {
        s = nat_soustraire(&xa, &xb);
        negatif = a->negatif;
    } else {
        s = nat_soustraire(&xb, &xa);
        negatif = b->negatif;
    }
    nat_liberer(&xa);
    nat_liberer(&xb);
    *r = dec_depuis_nat(s, negatif, e);
    return verifier_taille(r);
}

StatutDecimal dec_soustraction(const Decimal *a, const Decimal *b, Decimal *r) {
    Decimal nb = dec_negation(b);
    StatutDecimal st = dec_addition(a, &nb, r);
    dec_liberer(&nb);
    return st;
}

StatutDecimal dec_multiplication(const Decimal *a, const Decimal *b, Decimal *r) {
    Nat ma = nat_de(a), mb = nat_de(b);
    Nat p = nat_multiplier(&ma, &mb);
    *r = dec_depuis_nat(p, a->negatif != b->negatif, a->exp + b->exp);
    return verifier_taille(r);
}

StatutDecimal dec_division(const Decimal *a, const Decimal *b, Decimal *r) {
    if (!b->n) {
        *r = dec_zero();
        return DEC_DIVISION_PAR_ZERO;
    }
    int negatif = a->negatif != b->negatif;
    long ideal = a->exp - b->exp;   /* exposant « naturel » du quotient */
    if (!a->n) {
        *r = dec_depuis_nat(nat_vide(1), 0, ideal);
        return verifier_taille(r);
    }
    Nat ma = nat_de(a), mb = nat_de(b);

    /* Le quotient est-il fini en décimal ? b = 2^x × 5^y × m, avec m premier avec 10 :
     * a ÷ b est fini si et seulement si m divise a. */
    Nat m = nat_depuis(mb.d, mb.n);
    size_t x = 0, y = 0;
    for (;;) {
        Nat t = nat_depuis(m.d, m.n);
        if (nat_diviser_petit(&t, 2) == 0) { nat_liberer(&m); m = t; x++; }
        else { nat_liberer(&t); break; }
    }
    for (;;) {
        Nat t = nat_depuis(m.d, m.n);
        if (nat_diviser_petit(&t, 5) == 0) { nat_liberer(&m); m = t; y++; }
        else { nat_liberer(&t); break; }
    }
    int fini;
    if (m.n == 1 && m.d[0] == 1) {
        fini = 1;
    } else {
        Nat q, reste;
        nat_diviser(&ma, &m, &q, &reste);
        fini = reste.n == 0;
        nat_liberer(&q);
        nat_liberer(&reste);
    }
    nat_liberer(&m);

    if (fini) {
        size_t k = x > y ? x : y;
        if (k > DEC_CHIFFRES_MAX) {
            *r = dec_zero();
            return DEC_TROP_GRAND;
        }
        Nat num = nat_decaler(&ma, k), q, reste;
        nat_diviser(&num, &mb, &q, &reste);
        nat_liberer(&num);
        nat_liberer(&reste);
        long e = ideal - (long)k;
        /* Zéros de queue superflus : 1,0 ÷ 2 donne 0,5, pas 0,50. */
        size_t z = 0;
        while (z < q.n && q.d[z] == 0 && e < ideal) { z++; e++; }
        if (z) {
            Nat t = nat_depuis(q.d + z, q.n - z);
            nat_liberer(&q);
            q = t;
        }
        *r = dec_depuis_nat(q, negatif, e);
        return verifier_taille(r);
    }

    /* Quotient non fini : au moins 29 chiffres, puis arrondi à 28. */
    long k = DEC_PRECISION + 1 + (long)mb.n - (long)ma.n;
    if (k < 0) k = 0;
    Nat num = nat_decaler(&ma, (size_t)k), q, reste;
    nat_diviser(&num, &mb, &q, &reste);
    nat_liberer(&num);
    int reste_non_nul = reste.n != 0;
    nat_liberer(&reste);

    size_t retirer = q.n - DEC_PRECISION;
    uint8_t premier = q.d[retirer - 1];          /* premier chiffre abandonné */
    int apres = reste_non_nul;                   /* reste non nul au-delà */
    for (size_t i = 0; i + 1 < retirer; i++) if (q.d[i]) apres = 1;
    Nat garde = nat_depuis(q.d + retirer, q.n - retirer);
    nat_liberer(&q);
    long e = ideal - k + (long)retirer;

    int monter = premier > 5 || (premier == 5 && (apres || (garde.d[0] & 1)));
    if (monter) {
        Nat un = nat_petit(1);
        Nat s = nat_additionner(&garde, &un);
        nat_liberer(&un);
        nat_liberer(&garde);
        garde = s;
        if (garde.n > DEC_PRECISION) {   /* 999…9 + 1 : un chiffre de trop, forcément 0 */
            Nat t = nat_depuis(garde.d + 1, garde.n - 1);
            nat_liberer(&garde);
            garde = t;
            e++;
        }
    }
    *r = dec_depuis_nat(garde, negatif, e);
    return verifier_taille(r);
}

/* Valeur entière d'un exposant : 0 si entier, 1 sinon, 2 si trop grand. */
static int exposant_entier(const Decimal *b, long *valeur) {
    *valeur = 0;
    if (!b->n) return 0;
    size_t debut = 0;
    if (b->exp < 0) {
        size_t f = (size_t)(-b->exp);
        if (f >= b->n) return 1;
        for (size_t i = 0; i < f; i++) if (b->ch[i]) return 1;
        debut = f;
    }
    long zeros = b->exp > 0 ? b->exp : 0;
    if ((long)(b->n - debut) + zeros > 9) return 2;
    long v = 0;
    for (size_t i = b->n; i > debut; i--) v = v * 10 + b->ch[i - 1];
    for (long i = 0; i < zeros; i++) v *= 10;
    *valeur = b->negatif ? -v : v;
    return 0;
}

StatutDecimal dec_puissance(const Decimal *a, const Decimal *b, Decimal *r) {
    long n;
    int statut = exposant_entier(b, &n);
    if (statut == 1) { *r = dec_zero(); return DEC_EXPOSANT_NON_ENTIER; }

    int unite = a->n == 1 && a->ch[0] == 1 && a->exp == 0;   /* 1 ou −1 */
    if (statut == 2) {
        if (unite) {
            int pair = b->exp > 0 || (b->ch[(size_t)(b->exp < 0 ? -b->exp : 0)] % 2 == 0);
            *r = dec_depuis_nat(nat_petit(1), a->negatif && !pair, 0);
            return DEC_OK;
        }
        if (!a->n && !b->negatif) { *r = dec_zero(); return DEC_OK; }
        *r = dec_zero();
        return a->n ? DEC_TROP_GRAND : DEC_DIVISION_PAR_ZERO;
    }

    if (n == 0) {                       /* convention : x ^ 0 = 1, y compris 0 ^ 0 */
        *r = dec_depuis_nat(nat_petit(1), 0, 0);
        return DEC_OK;
    }
    unsigned long e = (unsigned long)(n < 0 ? -n : n);

    /* Exponentiation rapide sur la valeur absolue. */
    Decimal base = dec_copier(a);
    base.negatif = 0;
    Decimal p = dec_depuis_nat(nat_petit(1), 0, 0);
    StatutDecimal st = DEC_OK;
    while (e && st == DEC_OK) {
        if (e & 1) {
            Decimal t;
            st = dec_multiplication(&p, &base, &t);
            dec_liberer(&p);
            p = t;
        }
        e >>= 1;
        if (e && st == DEC_OK) {
            Decimal t;
            st = dec_multiplication(&base, &base, &t);
            dec_liberer(&base);
            base = t;
        }
    }
    dec_liberer(&base);
    if (st != DEC_OK) {
        dec_liberer(&p);
        *r = dec_zero();
        return st;
    }
    if (a->negatif && (n % 2 != 0)) p.negatif = 1;

    if (n < 0) {
        Decimal un = dec_depuis_nat(nat_petit(1), 0, 0);
        st = dec_division(&un, &p, r);
        dec_liberer(&un);
        dec_liberer(&p);
        return st;
    }
    *r = p;
    return DEC_OK;
}

char *dec_formater(const Decimal *a) {
    Chaine c = {0};
    if (a->negatif && a->n) chaine_ajouter(&c, "−");

    /* Chiffres de la partie entière et de la partie décimale, poids fort d'abord. */
    size_t f = a->exp < 0 ? (size_t)(-a->exp) : 0;       /* nombre de décimales */
    size_t zeros = a->exp > 0 && a->n ? (size_t)a->exp : 0; /* zéros ajoutés à droite */
    size_t total = a->n + zeros;                          /* chiffres du coefficient étendu */
    size_t ent = total > f ? total - f : 0;               /* chiffres de la partie entière */

    char *tampon = grym_allouer(total + f + 2);
    size_t k = 0;
    /* partie entière */
    if (ent == 0) {
        tampon[k++] = '0';
    }
    for (size_t i = 0; i < ent; i++) {
        size_t pos = total - 1 - i;                       /* rang dans le coefficient étendu */
        tampon[k++] = (char)('0' + (pos >= zeros && pos - zeros < a->n ? a->ch[pos - zeros] : 0));
        size_t reste = ent - 1 - i;
        if (reste && reste % 3 == 0) {
            tampon[k] = '\0';
            chaine_ajouter(&c, tampon);
            chaine_ajouter(&c, "'");
            k = 0;
        }
    }
    tampon[k] = '\0';
    chaine_ajouter(&c, tampon);
    /* partie décimale */
    if (f) {
        chaine_ajouter(&c, ",");
        k = 0;
        for (size_t i = f; i > 0; i--) {
            size_t pos = i - 1;                           /* rang dans le coefficient */
            tampon[k++] = (char)('0' + (pos < a->n ? a->ch[pos] : 0));
        }
        tampon[k] = '\0';
        chaine_ajouter(&c, tampon);
    }
    free(tampon);
    return chaine_rendre(&c);
}
