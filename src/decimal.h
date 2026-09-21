/* GrymoiR : arithmétique décimale exacte, v0.1
 * Spécification : docs/grammaire.md, § 3.2 (charte, principe 1).
 *
 * Un nombre vaut coefficient × 10^exposant, le coefficient étant un entier
 * de précision arbitraire. Addition, soustraction et multiplication sont
 * exactes. Une division exacte en décimal reste exacte (1 ÷ 8 = 0,125) ;
 * les autres (1 ÷ 3) sont arrondies à 28 chiffres significatifs,
 * au plus proche, à égalité vers le chiffre pair.
 */
#ifndef GRYM_DECIMAL_H
#define GRYM_DECIMAL_H

#include <stddef.h>
#include <stdint.h>

#define DEC_PRECISION     28    /* chiffres significatifs d'une division non finie */
#define DEC_CHIFFRES_MAX  1000  /* au-delà : « nombre trop grand » */

typedef struct {
    int negatif;
    uint8_t *ch;     /* chiffres du coefficient, poids faible d'abord ; n == 0 pour zéro */
    size_t n;
    long exp;        /* valeur = coefficient × 10^exp */
} Decimal;

typedef enum {
    DEC_OK,
    DEC_DIVISION_PAR_ZERO,
    DEC_TROP_GRAND,
    DEC_EXPOSANT_NON_ENTIER
} StatutDecimal;

Decimal dec_zero(void);
Decimal dec_depuis_canonique(const char *s);   /* « 123.45 », « -3 » : forme du lexeur et du bytecode */
int dec_canonique_valide(const char *s);       /* vrai si s respecte cette forme */
Decimal dec_copier(const Decimal *a);
void dec_liberer(Decimal *a);

Decimal dec_negation(const Decimal *a);
int dec_comparer(const Decimal *a, const Decimal *b);   /* −1, 0 ou 1 ; 1,0 = 1 */
int dec_est_entier(const Decimal *a);                   /* 3 et 3,00 oui ; 2,5 non */
StatutDecimal dec_addition(const Decimal *a, const Decimal *b, Decimal *r);
StatutDecimal dec_soustraction(const Decimal *a, const Decimal *b, Decimal *r);
StatutDecimal dec_multiplication(const Decimal *a, const Decimal *b, Decimal *r);
StatutDecimal dec_division(const Decimal *a, const Decimal *b, Decimal *r);
StatutDecimal dec_puissance(const Decimal *a, const Decimal *b, Decimal *r);

/* Style suisse (§ 4.1) : 1'234,50 ; négatif avec le signe − (U+2212). */
char *dec_formater(const Decimal *a);

#endif
