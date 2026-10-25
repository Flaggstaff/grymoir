/* GrymoiR : arbre syntaxique, v0.1
 * Spécification : docs/grammaire.md (révision 1.37), § 6.
 * Chaque nœud garde sa position dans la source (debut, fin), pour les
 * messages d'erreur et, en v0.2, pour la traduction sans perte.
 */
#ifndef GRYM_ARBRE_H
#define GRYM_ARBRE_H

#include <stddef.h>

/* N_NOM d'un champ d'entité : « , abandonné » (2.49), un bit de entier */
#define ABANDONNE (1 << 20)

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
    N_DUREE,         /* « 3 mois », seulement à droite d'un « + » ou d'un « − », ou comme pas d'une boucle (§ 14.2) :
                        enfants[0] : le nombre ; op : unité, 'j' jours, 's' semaines, 'm' mois, 'a' années ;
                        texte : le mot écrit (« jour », « ans », « années »…), gardé pour la traduction */
    N_CONVERSION,    /* « le nom en majuscules », « le code en nombre » (§ 4.5) : enfants[0] la valeur ; texte2 : la cible
                        (« majuscules », « minuscules », « nombre entier », « nombre », « date », « année »,
                        « vrai ou faux ») ; entier : son rang, opérande de CONVERTIR */
    N_COLONNE,       /* « la colonne « Nom » de la ligne » (§ 15.5) : enfants[0] le titre, enfants[1] la ligne */
    N_ENSEMBLE,      /* « « rouge », « vert » et « bleu » », « un ensemble vide » (§ 13.9) : enfants : les éléments */
    N_AJOUT,         /* « les élus gagnent m » : enfants[0] l'ensemble, enfants[1] la valeur ; op 'g' gagner, 'p' perdre */
    N_TAILLE,        /* « le nombre de couleurs » : enfants[0] l'ensemble */
    N_JOINDRE,       /* « les couleurs jointes par « , » » : enfants[0] l'ensemble, enfants[1] le séparateur ; forme 1 : joints */
    N_HEURE,         /* « 14:30 » (§ 14.7) : texte « 14:30 » ; entier : les minutes depuis minuit */
    N_MAINTENANT,    /* « maintenant » : l'heure, lue une fois au début de l'exécution (§ 14.7) */
    N_ECART,         /* « le nombre de mois entre d1 et d2 » (§ 14.6) : enfants[0] d1, enfants[1] d2 ;
                        op : 'j' jours, 's' semaines, 'm' mois, 'a' années ; texte : l'unité écrite */
    N_HASARD,        /* « un nombre au hasard de 1 à 6 » : enfants[0], enfants[1] les bornes ; « un élément au hasard
                        parmi les invités » : forme 1, enfants[0] l'ensemble (§ 3.6) */
    N_RESTE,         /* « le reste de 17 par 5 » (§ 3.5) : enfants[0] le dividende, enfants[1] le diviseur */
    N_ARRONDI,       /* « l'arrondi de x à 0,05 vers le bas » (§ 3.4) : enfants[0] la valeur, enfants[1] le pas ;
                        op : 'p' au plus proche, 'b' vers le bas, 'h' vers le haut, 'e' au pair ;
                        forme 1 : article « l' » écrit */
    N_CADRE,         /* « le nom sur 20 à droite » (§ 4.2) : enfants[0] la valeur, enfants[1] la largeur ;
                        forme 0 : sens par défaut, 1 : à gauche, 2 : à droite */
    N_REPONSE,       /* « la réponse en nombre à « Âge ? » » (§ 17) : texte2 : type demandé ;
                        enfants[0] : la question */
    N_FICHIER,       /* « le fichier « photos/ana.jpg » » : enfants[0] : chemin (§ 15.2) */
    N_MOTIF,         /* « le motif de l'échec » (§ 18) : local : case du motif */
    N_COLLAGE,       /* « a suivi de b » (§ 4.4) : enfants[0], enfants[1] ; texte collé, sans espace */
    N_ELISION,       /* « de x », « que x » dans un assemblage (§ 4.4) : op 'd' ou 'q' ; enfants[0] */
    N_ECRAN,         /* « l'écran », dans un événement ou une liste d'écran : l'écran en cours (§ 22.2) ; texte : sa classe */
    N_DISPOSITION,   /* élément d'écran : forme 1 « côte à côte : », 2 « l'un sous l'autre : », 3 « le groupe « T » : »
                        (texte : le titre, § 22.7), 4 « en position libre : » (§ 22.9), 5 « les onglets : », 6 « l'onglet « T » : »
                        (texte : le titre, § 22.10), 0 fin du bloc (§ 22.1) */
    N_BOUTON,        /* élément d'écran : « un bouton « Nouveau » » : texte : libellé (§ 22.1) */
    N_TEXTE_ECRAN,   /* élément d'écran : « le texte « Bienvenue » » : texte (§ 22.1) */
    N_IMAGE_ECRAN,   /* élément d'écran : « l'image « plan.png » » : texte : le chemin, tel qu'écrit (§ 22.8) */
    N_SI_ECRAN,      /* état d'un élément d'écran, dernier de ses enfants : forme 1 « actif si », 2 « visible si » ;
                        enfants[0] : la condition (§ 22.6) */
    N_CHAMP_DONT,    /* champ de l'objet examiné, dans une condition « dont » : texte : champ (§ 16.4) */
    N_CHERCHER,      /* objets conservés : texte : entité ; forme : 0 liste (boucle), 1 un seul, 2 nombre, 3 somme
                        du champ texte2 (§ 16.4) ;
                        enfants[0] : condition « dont » (facultative) ; texte2 : champ du tri ; entier : 1 si décroissant ;
                        op 'I' : relation inverse, l'objet en dernier enfant (§ 16.10) ; op 'M' : les éléments du
                        champ multiple texte3 de cet objet (§ 16.13) ; liste d'écran (§ 22.1) : texte3, les colonnes
                        choisies (« avec … »), séparées par U+001E, chacune « texte écrit U+001D champ U+001F champ… » */
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
    P_RESAISIR,      /* « Saisir à nouveau p. » (§ 19) : enfants[0] : l'objet */
    P_FICHE,         /* « Afficher la fiche de p. » (§ 20) : enfants[0] : l'objet */
    P_ESSAYER,       /* « Essayer : … En cas d'échec : … » (§ 18) : enfants[0] : N_BLOC essayé ; enfants[1] : N_BLOC
                        de l'échec ; entier : case du motif ; forme 1 : « En cas d'échec » en bloc (sinon forme courte) */
    P_STYLE,         /* « Les nombres s'affichent à la française. » (§ 4.1) : entier 0 suisse, 1 française,
                        2 sans séparateur */
    P_ECRAN,         /* « L'écran des compositeurs montre : … » (§ 22) : texte : nom (« des compositeurs ») ; texte2 : titre
                        donné, ou NULL ; enfants : N_CHERCHER (liste), N_BOUTON, N_TEXTE_ECRAN, N_NOM (zone de saisie :
                        texte : nom, texte2 : type, forme : genre, entier 1 : facultatif, 2 : sur plusieurs lignes,
                        4 : en lecture seule ; enfants[0] : départ), dans l'ordre ; un bouton, un texte ou une zone finit
                        par ses N_SI_ECRAN (§ 22.6) */
    P_QUAND,         /* « Quand on clique sur « B » dans l'écran X : » (§ 22.2) : texte : nom interne de la formule ;
                        texte3 : écran ; forme : 1 clic, 2 choix, 3 changement, 4 ouverture, 5 fermeture ; entier : cases locales ; enfants[0] : paramètres
                        (N_BLOC), enfants[1] : corps, enfants[2] : N_TEXTE, le bouton (clic) ou l'entité (choix) */
    P_OUVRIR,        /* « Ouvrir l'écran X. » (§ 22.3) : texte : écran ; local : deux cases (événement, objet) ;
                        forme 1 : « Ouvrir la fiche de c. », enfants[0] : l'objet */
    P_FERMER,        /* « Fermer l'écran. » (§ 22.3) */
    P_UTILISER,      /* « Utiliser « données ». » (§ 21) : texte : chemin écrit, sans extension ; entier : rang du fichier
                        dans Programme.fichiers, 0 s'il était déjà lu ; enfants : ses déclarations */
    P_GAGNER,        /* « Les genres de o gagnent baroque. » : texte : champ multiple ; enfants[0] : objet ;
                        enfants[1] : valeur ; forme 1 : « perdent » (§ 16.13) */
    P_APPARENCE,     /* « Les écrans ont la couleur verte et le logo « logo.png ». » (§ 22.5) : entier : couleur, rang
                        dans la palette (0 bleue, 1 verte, 2 turquoise, 3 violette, 4 grise), −1 si non écrite ;
                        texte : chemin du logo tel qu'écrit, ou NULL */
    P_REFUSER,       /* « Refuser « Débits : » puis d. » (§ 18.1) : enfants : les éléments du message, comme
                        « Afficher » (reliés par « puis », séparés par une espace) */
    P_EXPORTER,      /* « Exporter les membres conservés …, avec le nom et la cotisation, dans « m.csv ». » (§ 15.4) :
                        enfants[0] : N_CHERCHER (liste ; texte3 : colonnes, comme une liste d'écran, ou NULL : tous les
                        champs) ; enfants[1] : le chemin */
    P_POUR_TABLEAU,  /* « Pour chaque ligne du tableau « f.csv » : » (§ 15.5) : texte : nom de la ligne ; local : sa case ;
                        entier : case du tableau (entier + 1 : rang) ; enfants[0] : chemin ; enfants[1] : corps */
    P_EXEMPLE,       /* « Exemple : le carré de 7 vaut 49. » (§ 9.6) : enfants[0] l'expression, enfants[1] le résultat attendu */
    P_REGLE,         /* « Chaque écriture conservée dont … vérifie : condition, sinon « … ». » (§ 16.14) : texte : entité ;
                        enfants[0] : P_CALCUL sans paramètre, qui parcourt les objets (P_POUR_CONSERVE) et refuse
                        au premier qui viole la condition (P_SI, dont la branche sinon est un P_REFUSER) */
    N_CODE_QR,       /* « le code QR de t », « le code QR suisse de t » (2.27) : une image PNG ; forme 1 : suisse ;
                        enfant : le texte */
    N_DOCUMENT,      /* « le document de quittance » (2.25) : le PDF qu'un document déclaré donne ; texte : son nom, tel
                        que l'écran le range (« document de quittance ») */
    P_SAUVEGARDER,   /* « Sauvegarder la base dans « copie.grymd » [avec le mot de passe m]. » (2.29, 2.30) : enfants[0] :
                        le chemin ; enfants[1] : le mot de passe, s'il y en a un (forme 1 : chiffrée, un coffre) */
    P_RESTAURER,     /* « Restaurer la base depuis f [avec le mot de passe m]. » (2.30) : enfants[0] : un chemin ou un
                        fichier ; enfants[1] : le mot de passe (forme 1) */
    P_BASE_CHIFFREE, /* « La base est chiffrée. » (2.37, § 16.18) : le module le note ; rien ne s'exécute */
    P_CHANGER_MDP,   /* « Changer le mot de passe de la base pour n, l'ancien étant a. » (2.37) : enfants[0] : le nouveau,
                        enfants[1] : l'ancien */
    N_ENVOI,         /* « l'envoi à « https://… » de : « clé » : valeur, … » (forme 0, POST JSON) ; « la lecture de
                        « https://… » avec : … » (forme 1, GET) (2.42) : enfants[0] : l'adresse, puis des paires, une
                        clé (N_TEXTE) et sa valeur */
    N_CHAMP_JSON,    /* « le champ « a.b » de r » (2.42) : texte : le chemin ; enfant : la réponse */
    N_JSON_LISTE,    /* la liste d'un champ JSON (2.60) : texte : le chemin ; enfant : la réponse ; forme 1 : son nombre
                        d'éléments ; forme 0 : la source d'un « Pour chaque … du champ « … » de … » */
    N_XML,           /* « le XML de facture » (2.46) : texte : l'action qui l'écrit (« xml de facture ») ; texte2 : la racine */
    P_XML            /* un élément d'un XML déclaré (2.46) : op 'a' attribut (texte, enfant : la valeur), 'c' contenu (enfant),
                        'e' élément (texte, enfant facultatif : son contenu), 'b' élément à enfants (texte, enfant : N_BLOC) */
} TypeNoeud;

typedef enum { ART_AUCUN, ART_LE, ART_LA, ART_L, ART_IMPLICITE, ART_LES } Article;   /* ART_LES : un ensemble (§ 13.9) */

typedef struct Noeud {
    TypeNoeud type;
    char op;              /* N_OPERATION : '+', '-', '*', '/', '^'
                             N_COMPARAISON : '=', '!' (≠), '<', '>', 'l' (≤), 'g' (≥),
                                             'P' (positif), 'N' (négatif), '0' (nul), 'V' (vrai), 'F' (faux),
                                             'A' (absent), 'R' (présent), 'p' (parmi, § 16.13 : enfants[0] le
                                             champ multiple, enfants[1] la valeur cherchée),
                                             'c' (contient), 'd' (commence par), 'f' (finit par) (§ 4.5) */
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
    char *indication;     /* zone d'écran : « , avec l'indication « … » », ou NULL (§ 22.6) */
    char *transcrit_de;   /* champ texte « , transcrite du mémo » : le champ (son) qu'il transcrit (§ 15.8), ou NULL */
    char *ancien;         /* « , anciennement « client » » : le nom d'avant d'un champ d'entité (N_NOM) ou d'une entité
                             (P_CLASSE, P_HERITAGE), qu'une migration renomme (§ 16.7) ; ou NULL */
    char *couleurs;       /* champ à valeurs permises : une lettre par valeur, sa couleur d'étiquette (§ 16.15), ou NULL */
    char *unite;          /* zone d'écran : « , avec l'unité « CHF » [devant] » (entier & 128 : devant), ou NULL */
    char *borne_min, *borne_max;   /* zone d'écran : « de 1 à 99 », « au moins », « au plus », en forme canonique ; pour
                                      un texte, en caractères (§ 22.6) ; NULL sans borne */
    struct Noeud **enfants;
    size_t nb_enfants;
    int ligne, colonne;   /* position du premier jeton */
    int op_ligne, op_colonne; /* position de l'opérateur (N_OPERATION), pour les erreurs d'exécution */
    size_t debut, fin;    /* en points de code dans la source normalisée */
    int place[4];         /* élément d'un bloc en position libre (§ 22.9) : gauche, haut, largeur, hauteur, en pixels */
    int place_dite;       /* ce qui en est écrit : 1 la place, 2 la largeur, 4 la hauteur ; 0 hors d'un bloc libre */
    int taille_titre;     /* P_ECRAN : −1 « en petit », 1 « en grand », 0 la taille du thème (§ 22.5) */
    int graphique;        /* N_CHERCHER d'un écran : 1 un graphique (§ 22.11), + 2 en secteurs, + 4 une somme (texte3 :
                             la clé, puis le champ additionné) */
    struct Noeud *titre_calcule;   /* P_ECRAN : « , (\"Bonjour \" suivi du prénom …) en grand, » (2.33) : le titre, une
                                      expression (N_GROUPE) relue avant chaque attente ; NULL pour un titre écrit */
    int genre_appel;      /* N_APPEL : le genre du calcul appelé, 1 masculin, 2 féminin, 0 inconnu : la forme compacte n'écrit
                             pas d'article, sa traduction le retrouve (« la longueur de … », « le premier jour de … ») */
    int article_a_deviner;   /* N_CHAMP, N_NOM venus de la forme compacte (`c.nom`), qui n'écrit pas d'article :
                                l'imprimeur littéral le retrouve par le genre (« le nom du c ») */
} Noeud;

Noeud *noeud_creer(TypeNoeud type, int ligne, int colonne, size_t debut);
void noeud_ajouter(Noeud *parent, Noeud *enfant);
void noeud_liberer(Noeud *n);

/* Forme parenthésée, pour les tests et l'outil grym-arbre :
 * (créer [total] (× [prix] [quantité])) */
char *noeud_decrire(const Noeud *n);

/* P_APPARENCE (§ 22.5) : un réglage porté par un enfant N_TEXTE, la police (forme 0, 2.13) ou la signature (forme 1,
 * 2.16) ; NULL s'il n'est pas écrit. */
static inline const char *apparence_reglage(const Noeud *n, int forme) {
    for (size_t k = 0; k < n->nb_enfants; k++)
        if (n->enfants[k]->type == N_TEXTE && n->enfants[k]->forme == forme) return n->enfants[k]->texte;
    return NULL;
}

/* P_APPARENCE (2.33) : le logo ('L') ou la signature ('S') donnés par une expression entre parenthèses, « le logo (le
 * logo du cabinet conservé) » : un enfant N_GROUPE dont op porte la lettre ; NULL s'il n'y en a pas. */
static inline const struct Noeud *apparence_expression(const Noeud *n, char quoi) {
    for (size_t k = 0; k < n->nb_enfants; k++)
        if (n->enfants[k]->type == N_GROUPE && n->enfants[k]->op == quoi) return n->enfants[k];
    return NULL;
}

#endif
