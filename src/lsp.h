/* GrymoiR : serveur d'aide à la saisie, protocole LSP (Language Server Protocol) 3.17
 * Spécification : docs/lsp.md.
 */
#ifndef GRYM_LSP_H
#define GRYM_LSP_H

#include <stdio.h>

/* Sert les requêtes lues sur `entree`, répond sur `sortie`, jusqu'à « exit ».
 * Renvoie 0 si « shutdown » a précédé « exit », 1 sinon (code de sortie du protocole). */
int lsp_servir(FILE *entree, FILE *sortie);

#endif
