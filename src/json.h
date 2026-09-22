/* GrymoiR : lecture et écriture de JSON, pour le serveur d'aide à la saisie (LSP)
 * Spécification : docs/lsp.md ; format : RFC 8259.
 */
#ifndef GRYM_JSON_H
#define GRYM_JSON_H

#include "texte.h"

#include <stddef.h>

typedef enum { JSON_NUL, JSON_BOOLEEN, JSON_NOMBRE, JSON_TEXTE, JSON_TABLEAU, JSON_OBJET } TypeJson;

typedef struct Json {
    TypeJson type;
    int vrai;               /* booléen */
    double nombre;          /* nombre */
    char *texte;            /* texte (UTF-8) ; nombre : sa forme écrite */
    struct Json **elements; /* tableau, ou valeurs d'un objet */
    char **cles;            /* objet */
    size_t nb;
} Json;

/* Lit un document JSON complet ; NULL et *erreur (à libérer) s'il est mal formé. */
Json *json_lire(const char *texte, size_t taille, char **erreur);
void json_liberer(Json *j);

/* Valeur d'une clé d'un objet, ou NULL. */
const Json *json_champ(const Json *objet, const char *cle);
/* Chemin « a.b.c » dans des objets imbriqués, ou NULL. */
const Json *json_chemin(const Json *j, const char *chemin);
const char *json_texte(const Json *j);                 /* NULL si ce n'est pas un texte */
long json_entier(const Json *j, long defaut);

/* Écrit un texte JSON entre guillemets, échappé. */
void json_ecrire_texte(Chaine *c, const char *texte);
/* Écrit une valeur lue (pour renvoyer un identifiant de requête tel quel). */
void json_ecrire(Chaine *c, const Json *j);

#endif
