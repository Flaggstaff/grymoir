/* Le code QR (grammaire 2.27) : une image PNG, faite ici, sans bibliothèque d'image.
 * Le codage du QR (ISO 18004) vient de vendor/qrcodegen (Project Nayuki, MIT). */
#ifndef GRYM_QR_H
#define GRYM_QR_H
#include <stddef.h>

/* Le PNG du code QR de `texte` (UTF-8). suisse : le Swiss QR Code des QR-factures (SIX, Implementation Guidelines
 * 2.4, § 6) : correction d'erreur M exactement, version 25 au plus (997 caractères), la croix suisse au centre, sans
 * zone calme dans l'image (le document la laisse autour). Sinon : correction M ou mieux si elle tient, une zone calme
 * de quatre modules. Rend NULL et *erreur si le texte ne tient pas. */
unsigned char *qr_png(const char *texte, int suisse, size_t *taille, char **erreur);

/* Les lignes d'un Swiss QR Code (SIX, Swiss Implementation Guidelines for the QR-bill, version 2.4, § 4.2.2), vérifiées
 * avant d'en faire une image ou une section paiement (2.29) : NULL si elles conviennent, sinon le message, à libérer.
 * L'IBAN du créancier (CH ou LI, 21 caractères, clé mod 97), le QR-IBAN (IID 30000 à 31999) seulement avec une
 * référence QR (QRR) et inversement, la monnaie (CHF, EUR), les adresses structurées (S) ou combinées (K) complètes,
 * un débiteur final entièrement vide ou entièrement rempli. */
char *qr_suisse_verifier(const char *texte);

#endif
