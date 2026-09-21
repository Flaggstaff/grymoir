/* GrymoiR : blocs de bytecode, v0.2
 * Spécification : docs/vm.md (révision 1.0).
 */
#ifndef GRYM_BYTECODE_H
#define GRYM_BYTECODE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    I_CONSTANTE = 1,
    I_LIRE,
    I_ECRIRE,
    I_NEGATION,
    I_ADDITION,
    I_SOUSTRACTION,
    I_MULTIPLICATION,
    I_DIVISION,
    I_PUISSANCE,
    I_AFFICHER,
    I_RETOUR,
    I_EGAL,
    I_DIFFERENT,
    I_INFERIEUR,
    I_SUPERIEUR,
    I_INFERIEUR_OU_EGAL,
    I_SUPERIEUR_OU_EGAL,
    I_NON,
    I_SAUTER,
    I_SAUTER_SI_FAUX
} CodeInstruction;

#define I_DERNIER I_SAUTER_SI_FAUX

typedef enum { C_NOMBRE = 1, C_TEXTE = 2, C_BOOLEEN = 3 } TypeConstante;

typedef struct {
    TypeConstante type;
    char *texte;          /* forme canonique pour un nombre (« 12.50 ») */
} Constante;

typedef struct {
    uint32_t decalage;    /* position de l'instruction dans le code */
    uint32_t ligne, colonne;
} Position;

typedef struct {
    Constante *constantes;
    size_t nb_constantes;
    char **noms;
    size_t nb_noms;
    uint8_t *code;
    size_t taille_code;
    Position *positions;
    size_t nb_positions;
    size_t cap_code, cap_positions;
} Bloc;

Bloc *bloc_creer(void);
void bloc_detruire(Bloc *b);

/* Construction (compilateur). Les index renvoyés tiennent sur deux octets,
 * sinon la fonction renvoie -1 (programme trop grand). */
long bloc_constante(Bloc *b, TypeConstante type, const char *texte);
long bloc_nom(Bloc *b, const char *nom);
void bloc_emettre(Bloc *b, CodeInstruction code, uint16_t operande, int ligne, int colonne);

/* Sauts : émis avec une cible provisoire, corrigée quand elle est connue.
 * bloc_emettre_saut renvoie la position de l'opérande à corriger. */
size_t bloc_emettre_saut(Bloc *b, CodeInstruction code, int ligne, int colonne);
void bloc_corriger_saut(Bloc *b, size_t operande, size_t cible);

const char *instruction_nom(CodeInstruction code);   /* en toutes lettres : « MULTIPLICATION » */
int instruction_a_operande(CodeInstruction code);
size_t instruction_taille(CodeInstruction code);   /* code et opérande, en octets */

/* Position source de l'instruction commençant au décalage donné (0 si inconnue). */
void bloc_position(const Bloc *b, size_t decalage, int *ligne, int *colonne);

/* Vérification complète (docs/vm.md, § 4). Renvoie 1 si le bloc est sain,
 * sinon 0 et un message à libérer dans *erreur. */
int bloc_verifier(const Bloc *b, char **erreur);

/* Fichier .grymb (docs/vm.md, § 8). */
unsigned char *bloc_serialiser(const Bloc *b, size_t *taille);
Bloc *bloc_lire(const unsigned char *donnees, size_t taille, char **erreur);   /* lit et vérifie */
int est_fichier_bytecode(const unsigned char *donnees, size_t taille);

/* Instructions en clair, une par ligne (docs/vm.md, § 9). */
char *bloc_desassembler(const Bloc *b);

#endif
