/* GrymoiR : blocs de bytecode, v0.2
 * Spécification : docs/vm.md (révision 1.32).
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
    I_SAISIR,
    I_COLLER,
    I_ELIDER,
    I_RESAISIR,
    I_FICHE,
    I_ECRAN_OUVRIR,     /* écrans (grammaire, § 22) : opérande : constante texte, la description de l'écran */
    I_ECRAN_LISTE,      /* opérande : élément ; dépile une liste, la montre dans l'écran ouvert */
    I_ECRAN_ATTENDRE,   /* attend un événement (valide ce qui précède) ; empile l'objet (ou absent), puis le code */
    I_ECRAN_ERREUR,     /* dépile le motif d'un événement raté, et le montre dans l'écran */
    I_ECRAN_FERMER,     /* ferme l'écran ouvert */
    I_FERMER_ECRAN,     /* « Fermer l'écran. » dans un événement : l'écran se fermera à la fin de l'événement */
    I_ECRAN_OBJET,      /* empile l'objet de l'écran ouvert : « le pays de l'écran » (§ 22.2) */
    I_FICHE_ECRAN,      /* dépile un objet et ouvre sa fiche, l'écran déduit de son entité (§ 22.3) */
    I_APPARENCE,        /* opérande : couleur + 1 (0 : non écrite) ; dépile le logo (fichier) ou absent (§ 22.5) ;
                           avec 0x100, dépile encore, au-dessus du logo, la police (fichier), la couleur libre et le fond
                           (textes « #RRGGBB »), chacun ou absent (2.13) */
    I_REFUSER,          /* opérande : n ; dépile n valeurs, les écrit comme AFFICHER, séparées par une espace ;
                           l'exécution échoue avec ce message (§ 18.1) */
    I_BASE_MODIFIEE,    /* empile vrai si la transaction en cours a écrit dans la base : avant de revérifier les règles (§ 16.14) */
    I_DECALER,          /* opérande : 0 jours, 1 semaines, 2 mois, 3 années ; dépile n, puis une date (ou une année) ;
                           empile la date n unités plus tard, ramenée au dernier jour du mois (§ 14.2) */
    I_ARRONDIR,         /* opérande : 0 au plus proche, 1 vers le bas, 2 vers le haut, 3 au pair ; dépile le pas, puis
                           la valeur ; empile la valeur arrondie au multiple du pas (§ 3.4) */
    I_CONTIENT,         /* opérande : 0 contient, 1 commence par, 2 finit par ; dépile la partie, puis le texte ;
                           empile vrai ou faux, sans tenir compte des accents ni de la casse (§ 4.5) */
    I_CONVERTIR,        /* opérande : 0 majuscules, 1 minuscules, 2 nombre entier, 3 nombre, 4 date, 5 année,
                           6 vrai ou faux ; remplace le texte au sommet par sa conversion (§ 4.5) */
    I_RESTE,            /* dépile le diviseur, puis le dividende ; empile le reste, du signe du dividende (§ 3.5) */
    I_EXPORTER,         /* opérande : constante texte « entité ␛ colonnes » ; dépile le chemin, puis la liste ; prépare le
                           tableau CSV, écrit avec les fichiers à la fin de l'exécution (§ 15.4) */
    I_TABLEAU,          /* remplace le fichier au sommet par le tableau qu'il contient (§ 15.5) */
    I_COLONNE,          /* dépile le titre, puis une ligne de tableau ; empile la cellule, ou absent si vide (§ 15.5) */
    I_ENSEMBLE,         /* opérande : n ; dépile n valeurs, empile l'ensemble de ces valeurs, sans doublon (§ 13.9) */
    I_AJOUTER,          /* opérande : 0 gagner, 1 perdre ; dépile la valeur, puis l'ensemble ; empile l'ensemble changé */
    I_PARMI,            /* dépile l'ensemble, puis la valeur ; empile vrai si elle en fait partie */
    I_TAILLE_ENSEMBLE,  /* remplace l'ensemble au sommet par son nombre d'éléments */
    I_JOINDRE,          /* dépile le séparateur, puis l'ensemble ; empile ses éléments en texte, séparés */
    I_EXEMPLE,          /* opérande : constante texte, l'exemple écrit ; dépile l'attendu, puis l'obtenu ; écrit ✓ ou ✗ (§ 9.6) */
    I_EXEMPLE_RATE,     /* opérande : la même ; dépile le motif d'une erreur ; écrit ✗ et le motif */
    I_ECART,            /* opérande : 0 jours, 1 semaines, 2 mois, 3 années ; dépile d2, puis d1 ; empile le nombre d'unités
                           entières de d1 à d2, négatif si d2 est avant (§ 14.6) */
    I_HASARD,           /* opérande : 0 dépile la borne haute, puis la basse, empile un entier entre les deux, bornes
                           comprises ; 1 dépile un ensemble, empile l'un de ses éléments (§ 3.6) */
    I_HEURE,            /* opérande : minutes depuis minuit ; empile cette heure (§ 14.7) */
    I_MAINTENANT,       /* empile l'heure, lue une fois au début de l'exécution */
    I_REGLE_UTILE,      /* opérande : constante texte, la clé d'une règle ; empile vrai si la règle doit se revérifier,
                           et la base note alors ce qu'elle lit (docs/vm.md, § 3.3) */
    I_REGLE_FAITE,      /* opérande : la même ; la règle a tenu : ses lectures valent à la validation */
    I_ECRAN_ETAT,       /* opérande : élément × 2 + 1 pour « visible si », + 0 pour « actif si » ; dépile la condition,
                           qui règle l'état de l'élément jusqu'à la prochaine attente (§ 22.6) */
    I_ECRAN_IMAGE       /* opérande : élément × 2 + 1 : la source commence, un champ d'objet absent y vaut absent ;
                           élément × 2 : dépile une image ou absent, que l'écran montre jusqu'à la prochaine attente (§ 22.8) */,
    I_ECRAN_EXIGER      /* opérande : constante texte, les zones qu'un événement lit, séparées par U+001E ; toutes les
                           obligatoires vides arrêtent l'événement d'un coup, chacune avec son message (§ 22.6) */,
    I_CORRESPOND,       /* dépile une empreinte, puis un mot de passe en clair ; empile vrai s'ils correspondent (§ 16.1) */
    I_SIGLES,           /* opérande : constante texte, les sigles du programme en minuscules, séparés par une espace ;
                           les libellés les écriront en capitales (grammaire, § 2.4) */
    I_ECRAN_TEXTE,      /* opérande : élément ; dépile une valeur et la montre, écrite comme AFFICHER, à la place du
                           texte de cet élément jusqu'à la prochaine attente (le texte calculé, § 22.1) */
    I_DOCUMENT,         /* opérande : 0 le PDF, 1 l'aperçu, 2 dépile un fichier et le montre (2.26) ; l'écran ouvert est un document (2.25), lu une fois : sa
                           page HTML, mise en page en PDF par l'interface ; l'écran se ferme. 0 empile le fichier PDF,
                           1 le montre (montrer_document) */
    I_CODE_QR,          /* opérande : 1 le Swiss QR Code (croix, M, version 25 au plus), 0 un code QR ; dépile un
                           texte, empile l'image PNG de son code QR (2.27) */
    I_SAUVEGARDER,      /* dépile un chemin ; une copie de la base, telle que l'exécution la voit, s'y écrira à la fin
                           de l'exécution réussie, jamais par-dessus un fichier : « (2) », « (3) »… (2.29) */
    I_SAUVEGARDER_COFFRE, /* dépile un mot de passe, puis un chemin : comme SAUVEGARDER, la copie chiffrée (coffre.h, 2.30) */
    I_RESTAURER,        /* opérande : bit 1 avec un mot de passe (dépilé d'abord), bit 2 vérifier seulement (2.85) ; dépile un chemin ou un fichier, une copie
                           de base, chiffrée ou non ; vérifiée, elle s'écrit à côté de la base (« .restaurer ») à la fin de
                           l'exécution réussie et la remplace à la prochaine ouverture (2.30) */
    I_CHANGER_MDP_BASE, /* dépile le nouveau mot de passe, puis l'ancien : l'enveloppe des clés de la base refaite (2.37) */
    I_ENVOI_DEBUT,      /* ouvre les valeurs d'un envoi : un secret s'y lit (2.42) */
    I_ENVOI,            /* opérande : bit 0 la lecture (GET) ; bits 1-2 le corps (0 aucun, 1 objet, 2 tableau) ; bits 3-8
                           les paires de l'adresse ; bits 9-15 les éléments du corps ; dépile le corps, les paires, puis
                           l'adresse ; valide ce qui précède, envoie, empile la réponse, un texte (2.42, 2.43) */
    I_CHAMP_JSON,       /* opérande : la constante du chemin ; dépile une réponse, empile le texte du champ, ou absent */
    I_XML_DEBUT,        /* opérande : la constante de la racine ; ouvre un XML (2.46) */
    I_XML_ELEMENT,      /* opérande : la constante du nom ; ouvre un élément */
    I_XML_ATTRIBUT,     /* opérande : la constante du nom ; dépile la valeur (absente : pas d'attribut) */
    I_XML_CONTENU,      /* dépile un texte, écrit dans l'élément ouvert, échappé */
    I_XML_FERMER,       /* ferme l'élément ouvert */
    I_XML_FIN,          /* ferme la racine ; empile le fichier XML (UTF-8) */
    I_ECRAN_FILTRE,     /* opérande : le rang de la zone ; dépile la liste de ses choix, pour l'écran qui s'ouvre (2.48) */
    I_SAUVEGARDER_JOUR, /* dépile le préfixe : la copie datée « p-AAAA-MM-JJ-HHMM », une par jour passé (2.53) */
    I_JSON_LISTE,       /* opérande : le chemin (constante) ; remplace la réponse par l'ensemble des éléments de la liste (2.60) */
    I_RETIRER,          /* dépile le texte à retirer, puis le texte ; empile le texte sans aucune de ses occurrences (2.64) */
    I_SIGNALER,         /* opérande : n ; dépile n valeurs, écrites comme REFUSER : le message montré sans bloquer (2.71) */
    I_AMORCER,          /* dépile un texte (ou absent) : l'amorce des transcriptions suivantes (2.80) */
    I_REMPLACER,        /* dépile le remplaçant, ce qu'on cherche, puis le texte ; empile le texte remplacé (2.81) */
    I_MOTS,             /* dépile un texte ; empile la liste de ses mots, dans l'ordre, doublons compris (2.81) */
    I_PORTS,            /* empile la liste des ports série de l'ordinateur, triés (2.82) */
    I_PORT_SERIE,       /* dépile la vitesse, la question, puis le port ; empile la réponse, un texte (2.82) */
    I_DERNIERE_SAUVEGARDE, /* remplace un préfixe par le chemin de sa copie datée la plus récente, ou absent (2.85) */
    I_HISTORIQUE,       /* opérande 0 : remplace un objet par son historique ; 1 : empile celui de la base ; un texte (2.85) */
    I_AMORCER_LANGUES,  /* comme I_AMORCER, la langue parlée reconnue au lieu du français (2.86) */
    I_LANGUE,           /* remplace un texte par le code de sa langue reconnue (« fr », « en »…), ou absent (2.86) */
    I_SIGNER,           /* dépile un texte (ou absent) : l'auteur des lignes suivantes du journal (2.87) */
    I_WSJTX,            /* opérande 1 : dépile le port (sinon 2237) ; empile la liste des nouveaux décodages (2.88) */
    I_LISTE,            /* opérande : n ; dépile n valeurs, empile la liste de ces valeurs, doublons compris (§ 13.10) */
    I_RANG,             /* opérande 0 : dépile le rang, puis la liste ; 1 : le premier, 2 : le dernier ; empile l'élément (§ 13.10) */
    I_EXTRAIRE,         /* opérande : la forme de N_EXTRAIRE (0 à 10) ; dépile ses valeurs (dans l'ordre des enfants) ; empile un
                           texte, un nombre (9 : la position) ou une liste (10 : les morceaux) (§ 4.6) */
    I_TABLE,            /* opérande : n ; dépile le nom de la table, puis n paires clé, valeur ; empile la table (§ 13.11) */
    I_TABLE_LIRE,       /* dépile le nom, la table, puis la clé ; empile la valeur ; erreur si la clé n'y est pas */
    I_TABLE_ECRIRE,     /* opérande 0 (vaut : une clé nouvelle), 1 (devient : une clé présente) : dépile le nom, la valeur, la
                           clé, puis la table ; 2 (perdre) : dépile la clé, puis la table ; empile la table changée */
    I_TABLE_LISTE,      /* opérande 0 : remplace la table par la liste de ses clés ; 1 : de ses valeurs (§ 13.11) */
    I_SORTE_ECHEC,      /* empile la sorte de l'échec que le dernier essai a rattrapé, un texte (§ 18.2) */
    I_RELANCER,         /* dépile la sorte, puis le motif : l'échec repart, avec sa position d'origine (§ 18.2) */
    I_COURRIEL,         /* dépile le texte, la messagerie, l'objet, puis le destinataire ; envoie le courriel (§ 22.27) */
    I_MATH,             /* opérande : la forme de N_MATH (3 bits) | le nombre d'arguments × 8 ; dépile les arguments, empile le
                           résultat (§ 3.7) */
    I_TRIER,            /* opérande : 1 décroissant | 2 par un champ | 4 par des clés (2.97) ; dépile [le nom du champ ou la liste
                           des clés, puis] la liste ; empile la liste triée, nouvelle (§ 13.12) */
    I_PERMIS,           /* opérande : une constante texte « type␝valeurs » ; la valeur au sommet, qui reste, doit être l'une
                           d'elles (§ 16.16) */
    I_LIGNES,           /* remplace un texte ou un fichier par la liste de ses lignes (§ 15.9) */
    I_JSON,             /* remplace une valeur par son texte JSON (§ 15.9) */
    I_MINUTERIE,        /* opérande : l'intervalle en secondes de la minuterie de l'écran, pour l'ÉCRAN_ATTENDRE qui suit (3.03) */
    I_COMMANDE,         /* opérande : 1 avec une liste d'arguments ; dépile [la liste, puis] le programme ; empile sa sortie (§ 15.10) */
    I_VERIFIER,         /* opérande : une constante texte, la condition écrite ; dépile la condition : fausse, échec (§ 9.7) */
    I_GESTE,            /* opérande : une constante « forme␟ligne␟écran␟élément » ; dépile la valeur pour écrire et choisir ;
                           range le geste, que l'écran simulé jouera (3.08) */
    I_DOCUMENTS_SORTIS, /* empile la liste des titres des documents fabriqués dans le scénario (3.09) */
    I_NATIF,            /* opérande : une constante « n␟bibliothèque␟fonction » ; dépile les n arguments, empile ce que la
                           fonction C rend (3.14, § 21.10) */
    I_CONNEXION,        /* « Les utilisateurs se connectent. » : la connexion, ou le premier compte (3.17, § 16.20) */
    I_CONNECTE          /* empile l'utilisateur connecté, ou absent (3.17) */
} CodeInstruction;

#define I_DERNIER I_CONNECTE

/* Paramètres d'un descripteur de recherche (le plus grand « ?n »), ou −1 s'il est mal formé. */
long requete_parametres(const char *descripteur);

typedef enum { B_PROGRAMME = 0, B_CALCUL = 1, B_ACTION = 2 } SorteBloc;

/* L'action qui vérifie toutes les règles du programme (grammaire, § 16.14), sous un nom qu'aucun programme n'écrit :
 * la machine l'appelle quand elle valide elle-même une saisie (« Modifier » d'une fiche déduite). */
#define NOM_REGLES "\x01règles"


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
    char *fichier;        /* fichier source d'une formule déclarée dans un fichier utilisé (§ 21), sinon NULL */
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
    int hors_fiches;      /* « , hors des fiches, » : absente des fiches (grammaire, § 22.3) */
    int journal;          /* « , journalisé, » : ses changements s'inscrivent au journal (2.85) */
    char *pluriel;        /* pluriel irrégulier, ou NULL */
    char **champs;        /* champs propres */
    char **types;         /* type de chaque champ, ou NULL (classe ordinaire) */
    unsigned char *uniques;   /* bit 1 : unique ; bit 2 : facultatif (§ 16.9) ; bit 4 : disparaît avec (§ 16.12) ;
                                 bit 8 : multiple (§ 16.13) */
    char **departs;       /* valeur de départ, forme canonique (« Suisse », « -3.5 », « 2026-09-21 », « vrai »), ou NULL */
    char **anciens;       /* « , anciennement « client » » : le nom d'avant du champ, ou NULL (§ 16.7) */
    char *ancien;         /* « Un patient, conservé, anciennement « client », a : » : le nom d'avant, ou NULL */
    size_t nb_champs;
    char **abandonnes;    /* « un code d'accès (mot de passe), abandonné » : les champs dont la base efface les valeurs (2.49) */
    size_t nb_abandonnes;
} ClasseModule;

/* Un module : le programme (bloc 0), ses formules et ses classes. */
typedef struct {
    Bloc **blocs;
    size_t nb;
    ClasseModule *classes;
    size_t nb_classes;
    int base_chiffree;    /* « La base est chiffrée. » (2.37, § 16.18) : le mot de passe avant d'ouvrir la base */
    int connexion;        /* « Les utilisateurs se connectent. » (3.17, § 16.20) */
} Module;

Module *module_creer(void);
void module_ajouter(Module *m, Bloc *b);
ClasseModule *module_ajouter_classe(Module *m, const char *nom, int feminin);
void classe_ajouter_champ(ClasseModule *c, const char *champ);
void classe_ajouter_aptitude(ClasseModule *c, const char *aptitude);
void classe_typer_dernier_champ(ClasseModule *c, const char *type, int unique);
void classe_depart_dernier_champ(ClasseModule *c, const char *depart);
void classe_facultatif_dernier_champ(ClasseModule *c);
void classe_ancien_dernier_champ(ClasseModule *c, const char *ancien);
void classe_ajouter_abandonne(ClasseModule *c, const char *nom);   /* 2.49 */
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
