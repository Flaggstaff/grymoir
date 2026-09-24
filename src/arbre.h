/* GrymoiR : arbre syntaxique, v0.1
 * Spécification : docs/grammaire.md (révision 1.34), § 6.
 * Chaque nœud garde sa position dans la source (debut, fin), pour les
 * messages d'erreur et, en v0.2, pour la traduction sans perte.
 */
#ifndef GRYM_ARBRE_H
#define GRYM_ARBRE_H

#include <stddef.h>

typedef enum {
    /* expressions */
    N_NOMBRE,        /* texte : valeur canonique « 12.50 » ; forme 1 : écrit avec un séparateur de milliers */
    N_NOM,           /* texte : clé du nom « prix unitaire » ; article : voir ci-dessous */
    N_NEGATION,      /* enfants[0] */
    N_OPERATION,     /* op, enfants[0], enfants[1] */
    N_GROUPE,        /* parenthèses conservées pour la traduction : enfants[0] */
    N_TEXTE,         /* texte : contenu, seulement dans Afficher */
    N_BOOLEEN,       /* texte : « vrai » ou « faux » */
    N_COMPARAISON,   /* op : voir ci-dessous ; enfants[0] : sujet ; enfants[1] : terme comparé (absent pour
                        positif, négatif, nul, vrai, faux) ; negation : « n'est pas » ;
                        forme : 1 si écrite avec un symbole (<, ≤…), 0 avec des mots, 2 cas de Selon
                        réduit à une valeur, 3 « est valeur » dans une condition « dont » (§ 16.4) */
    N_LOGIQUE,       /* op : 'e' (et) ou 'o' (ou) ; enfants[0], enfants[1] */
    N_BLOC,          /* enfants : phrases d'un bloc indenté ou d'une forme courte */
    N_APPEL,         /* appel d'un calcul : texte : nom ; enfants : arguments */
    N_SUJET,         /* le sujet d'un Selon, rangé dans la case locale `local` */
    N_INTERVALLE,    /* cas « de a à b » : enfants[0], enfants[1] ; bornes dans un ordre quelconque */
    N_CAS,           /* enfants : conditions, puis le N_BLOC ; forme 1 : « Autrement » (sans condition) */
    N_DATE,          /* « 21.09.2026 » : texte : forme ISO « 2026-09-21 » (§ 14) */
    N_ABSENT,        /* « absent », « absente » : forme 2 si féminin (§ 16.9) */
    N_AUJOURDHUI,    /* « aujourd'hui » (§ 14.3) */
    N_CADRE,         /* « le nom sur 20 à droite » (§ 4.2) : enfants[0] la valeur, enfants[1] la largeur ;
                        forme 0 : sens par défaut, 1 : à gauche, 2 : à droite */
    N_REPONSE,       /* « la réponse en nombre à « Âge ? » » (§ 17) : texte2 : type demandé ;
                        enfants[0] : la question */
    N_FICHIER,       /* « le fichier « photos/ana.jpg » » : enfants[0] : chemin (§ 15.2) */
    N_MOTIF,         /* « le motif de l'échec » (§ 18) : local : case du motif */
    N_COLLAGE,       /* « a suivi de b » (§ 4.4) : enfants[0], enfants[1] ; texte collé, sans espace */
    N_ELISION,       /* « de x », « que x » dans un assemblage (§ 4.4) : op 'd' ou 'q' ; enfants[0] */
    N_CHAMP_DONT,    /* champ de l'objet examiné, dans une condition « dont » : texte : champ (§ 16.4) */
    N_CHERCHER,      /* objets conservés : texte : entité ; forme : 0 liste (boucle), 1 un seul, 2 nombre ;
                        enfants[0] : condition « dont » (facultative) ; texte2 : champ du tri ; entier : 1 si décroissant ;
                        op 'I' : relation inverse, l'objet en dernier enfant (§ 16.10) ; op 'M' : les éléments du
                        champ multiple texte3 de cet objet (§ 16.13) */
    N_CHAMP,         /* « le solde du client » : texte : champ ; enfants[0] : objet ; article : devant le champ */
    N_NOUVEAU,       /* « un nouveau client » : texte : classe ; enfants : N_INIT ; forme 1 : bloc d'initialisation */
    N_INIT,          /* « Le nom vaut … » dans le bloc d'un nouvel objet : texte : champ ; enfants[0] : valeur */
    /* phrases */
    P_CREATION,      /* texte : nom ; enfants[0] : expression */
    P_MODIFICATION,  /* texte : nom ; enfants[0] : expression */
    P_AFFICHAGE,     /* enfants : éléments */
    P_REMARQUE,      /* texte : contenu */
    P_EXPRESSION,    /* boucle interactive : enfants[0] */
    P_SI,            /* enfants[0] : condition ; enfants[1] : N_BLOC alors ; enfants[2] : N_BLOC sinon (facultatif) ;
                        forme : bit 0 = bloc indenté (sinon forme courte), bit 1 = introduit par « Sinon si »,
                        bit 2 = « Sinon » en bloc */
    P_CALCUL,        /* texte : nom ; enfants[0] : N_BLOC des paramètres (N_NOM) ; enfants[1] : corps,
                        une valeur (forme 0, « vaut ») ou un N_BLOC (forme 1) ; entier : nombre de locaux */
    P_ACTION,        /* texte : nom ; enfants[0] : N_BLOC des paramètres ; enfants[1] : N_BLOC ; entier : locaux */
    P_RENDRE,        /* enfants[0] : valeur rendue par un calcul */
    P_APPEL,         /* appel d'une action : texte : nom ; enfants : arguments */
    P_TANT_QUE,      /* enfants[0] : condition ; enfants[1] : N_BLOC */
    P_REPETER,       /* enfants[0] : nombre de tours ; enfants[1] : N_BLOC ; entier : case du compte à rebours */
    P_POUR_CHAQUE,   /* texte : compteur (case `local`) ; enfants : début, fin, [pas], N_BLOC ;
                        forme 1 : pas écrit ; forme 2 et 4 : « du » et « au » écrits devant une valeur
                        sans article ; entier : case de la fin, entier + 1 : case du pas */
    P_SORTIR,        /* « Sortir de la boucle. » */
    P_PASSER,        /* « Passer au tour suivant. » */
    P_SELON,         /* enfants[0] : sujet ; puis les N_CAS ; entier : case du sujet */
    P_CLASSE,        /* « Un client a : » : texte : classe ; forme : 1 masculin, 2 féminin ; enfants : champs propres
                        (N_NOM, forme = genre) et aptitudes adoptées (N_TEXTE, forme féminine) ;
                        texte2 : classe parente (« Un membre est une personne. »), ou NULL ;
                        forme : bit 16 = déclarée par « est », bit 32 = entité (« conservé ») ;
                        un champ d'entité porte son type en texte2, op = 'U' s'il est unique, sa
                        valeur de départ en enfants[0] (§ 16.7), et entier = 1 s'il est facultatif (§ 16.9) ;
                        un champ multiple (§ 16.13) a forme 3, son type en texte2 et son singulier irrégulier
                        déclaré en texte3 (NULL s'il se déduit) */
    P_MODIF_CHAMP,   /* « Le solde du client devient … » : texte : champ ; enfants[0] : objet ; enfants[1] : valeur */
    P_POUR_CONSERVE, /* « Pour chaque client conservé dont … : » : texte : entité (nom du compteur, case `local`) ;
                        enfants[0] : N_CHERCHER ; enfants[1] : N_BLOC ; entier : case de la liste, entier + 1 : rang ;
                        forme : comme P_POUR_CHAQUE (bloc ou forme courte) */
    P_CONSERVER,     /* « Conserver le client. » : enfants[0] : l'objet (§ 16.3) */
    P_SUPPRIMER,     /* « Supprimer le client. » : enfants[0] : l'objet (§ 16.3) */
    P_ENREGISTRER,   /* « Enregistrer … dans « copie.jpg ». » : enfants[0] : fichier ; enfants[1] : chemin (§ 15.2) */
    P_APTITUDE,      /* « Une chose horodatée a : » : texte : forme féminine ; texte2 : forme masculine déclarée
                        entre parenthèses, ou NULL si elle se déduit ; enfants : champs (N_NOM) */
    P_EFFACER,       /* « Effacer l'écran. » (§ 4.3) */
    P_ESSAYER,       /* « Essayer : … En cas d'échec : … » (§ 18) : enfants[0] : N_BLOC essayé ; enfants[1] : N_BLOC
                        de l'échec ; entier : case du motif ; forme 1 : « En cas d'échec » en bloc (sinon forme courte) */
    P_STYLE,         /* « Les nombres s'affichent à la française. » (§ 4.1) : entier 0 suisse, 1 française,
                        2 sans séparateur */
    P_GAGNER         /* « Les genres de o gagnent baroque. » : texte : champ multiple ; enfants[0] : objet ;
                        enfants[1] : valeur ; forme 1 : « perdent » (§ 16.13) */
} TypeNoeud;

typedef enum { ART_AUCUN, ART_LE, ART_LA, ART_L, ART_IMPLICITE } Article;

typedef struct Noeud {
    TypeNoeud type;
    char op;              /* N_OPERATION : '+', '-', '*', '/', '^'
                             N_COMPARAISON : '=', '!' (≠), '<', '>', 'l' (≤), 'g' (≥),
                                             'P' (positif), 'N' (négatif), '0' (nul), 'V' (vrai), 'F' (faux),
                                             'A' (absent), 'R' (présent), 'p' (parmi, § 16.13 : enfants[0] le
                                             champ multiple, enfants[1] la valeur cherchée) */
    int negation;         /* N_COMPARAISON : « n'est pas » */
    int forme;            /* variante d'écriture, conservée pour la traduction sans perte */
    int crochets;         /* N_NOM, P_CREATION, P_MODIFICATION : nom écrit entre crochets */
    int local;            /* N_NOM, P_CREATION, P_MODIFICATION : case locale d'une formule, −1 si nom global */
    int entier;           /* P_CALCUL, P_ACTION : nombre de cases locales (paramètres compris) */
    int ligne_fin;        /* phrases : dernière ligne occupée (lignes vides conservées à l'impression) */
    Article article;      /* article écrit devant le nom (N_NOM, P_CREATION, P_MODIFICATION) */
    char *texte;
    char *texte2;         /* P_CLASSE : classe parente ; N_NOM d'un champ d'entité : son type (§ 16.1) */
    char *texte3;         /* P_CLASSE : pluriel irrégulier déclaré (« chevaux ») */
    struct Noeud **enfants;
    size_t nb_enfants;
    int ligne, colonne;   /* position du premier jeton */
    int op_ligne, op_colonne; /* position de l'opérateur (N_OPERATION), pour les erreurs d'exécution */
    size_t debut, fin;    /* en points de code dans la source normalisée */
} Noeud;

Noeud *noeud_creer(TypeNoeud type, int ligne, int colonne, size_t debut);
void noeud_ajouter(Noeud *parent, Noeud *enfant);
void noeud_liberer(Noeud *n);

/* Forme parenthésée, pour les tests et l'outil grym-arbre :
 * (créer [total] (× [prix] [quantité])) */
char *noeud_decrire(const Noeud *n);

#endif
