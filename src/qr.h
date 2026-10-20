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

#endif
