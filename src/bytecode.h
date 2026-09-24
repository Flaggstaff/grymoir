/* GrymoiR : blocs de bytecode, v0.2
 * Spécification : docs/vm.md (révision 1.25).
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
    I_SAUTER_SI_FAUX,
    I_APPELER,
    I_RENDRE,
    I_LIRE_LOCAL,
    I_ECRIRE_LOCAL,
    I_ECHOUER,
    I_EXIGER_ENTIER_NATUREL,
    I_NOUVEAU,
    I_INITIALISER_CHAMP,
    I_LIRE_CHAMP,
    I_ECRIRE_CHAMP,
    I_AUJOURDHUI,
    I_LIRE_FICHIER,
    I_ENREGISTRER,
    I_CONSERVER,
    I_SUPPRIMER,
    I_CHERCHER,
    I_TAILLE_LISTE,
    I_ELEMENT,
    I_ABSENT,
    I_EST_ABSENT,
    I_SUPPRIMER_DEFINITIVEMENT,
    I_RETABLIR,
    I_GAGNER,
    I_PERDRE,
    I_DEMANDER,
    I_CADRER,
    I_AFFICHER_SANS_LIGNE,
    I_STYLE,
    I_EFFACER,
    I_ESSAYER,
    I_FIN_ESSAI,
    I_SAISIR
} CodeInstruction;

#define I_DERNIER I_SAISIR

/* Paramètres d'un descripteur de recherche (le plus grand « ?n »), ou −1 s'il est mal formé. */
long requete_parametres(const char *descripteur);

typedef enum { B_PROGRAMME = 0, B_CALCUL = 1, B_ACTION = 2 } SorteBloc;

/* date : « 2026-09-21 » ; recherche : descripteur « entité ␟ mode ␟ tri ␟ décroissant ␟ condition » */
typedef enum { C_NOMBRE = 1, C_TEXTE = 2, C_BOOLEEN = 3, C_DATE = 4, C_RECHERCHE = 5 } TypeConstante;

typedef struct {
    TypeConstante type;
    char *texte;          /* forme canonique pour un nombre (« 12.50 ») */
} Constante;

typedef struct {
    uint32_t decalage;    /* position de l'instruction dans le code */
    uint32_t ligne, colonne;
} Position;

typedef struct {
    char *nom;            /* nom de la formule ; NULL pour le programme */
    char *classe;         /* méthode : classe de son premier paramètre (grammaire, § 13.6), sinon NULL */
    SorteBloc sorte;
    int nb_parametres;
    int nb_locaux;        /* paramètres compris */
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

/* APPELER : nom de la formule, nombre d'arguments, 1 si un résultat est attendu (calcul). */
void bloc_emettre_appel(Bloc *b, uint16_t nom, uint8_t nb_arguments, int rend, int ligne, int colonne);

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

/* Instructions en clair, une par ligne (docs/vm.md, § 12). */
char *bloc_desassembler(const Bloc *b);

/* Une classe déclarée dans le module (grammaire, § 13). */
typedef struct {
    char *nom;
    int feminin;
    int aptitude;         /* 1 : aptitude (grammaire, § 13.7) */
    char *parent;         /* classe dont elle hérite, ou NULL */
    char **aptitudes;     /* aptitudes adoptées */
    size_t nb_aptitudes;
    int conserve;         /* entité (grammaire, § 16) */
    char *pluriel;        /* pluriel irrégulier, ou NULL */
    char **champs;        /* champs propres */
    char **types;         /* type de chaque champ, ou NULL (classe ordinaire) */
    unsigned char *uniques;   /* bit 1 : unique ; bit 2 : facultatif (§ 16.9) ; bit 4 : disparaît avec (§ 16.12) ;
                                 bit 8 : multiple (§ 16.13) */
    char **departs;       /* valeur de départ, forme canonique (« Suisse », « -3.5 », « 2026-09-21 », « vrai »), ou NULL */
    size_t nb_champs;
} ClasseModule;

/* Un module : le programme (bloc 0), ses formules et ses classes. */
typedef struct {
    Bloc **blocs;
    size_t nb;
    ClasseModule *classes;
    size_t nb_classes;
} Module;

Module *module_creer(void);
void module_ajouter(Module *m, Bloc *b);
ClasseModule *module_ajouter_classe(Module *m, const char *nom, int feminin);
void classe_ajouter_champ(ClasseModule *c, const char *champ);
void classe_ajouter_aptitude(ClasseModule *c, const char *aptitude);
void classe_typer_dernier_champ(ClasseModule *c, const char *type, int unique);
void classe_depart_dernier_champ(ClasseModule *c, const char *depart);
void classe_facultatif_dernier_champ(ClasseModule *c);
void classe_cascade_dernier_champ(ClasseModule *c);
void classe_multiple_dernier_champ(ClasseModule *c);
void module_detruire(Module *m);          /* ignore les entrées mises à NULL */
int module_verifier(const Module *m, char **erreur);

/* Fichier .grymb (docs/vm.md, § 11). */
unsigned char *module_serialiser(const Module *m, size_t *taille);
Module *module_lire(const unsigned char *donnees, size_t taille, char **erreur);   /* lit et vérifie */
int est_fichier_bytecode(const unsigned char *donnees, size_t taille);
char *module_desassembler(const Module *m);

#endif
