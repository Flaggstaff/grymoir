/* GrymoiR : interface d'entrée et de sortie de la machine.
 * Spécification : docs/vm.md (révision 1.32), § 13.
 *
 * Tout ce qui touche l'utilisateur passe par ici : poser des questions (une seule, ou un formulaire),
 * effacer l'écran. La console l'implémente (console.c) ; le navigateur l'implémentera (docs/v2.md).
 * L'affichage s'accumule dans une Chaine, que l'interface reçoit à chaque question.
 */
#ifndef GRYM_INTERFACE_H
#define GRYM_INTERFACE_H

#include "texte.h"

#include <stddef.h>

/* Une ligne de la fiche d'un objet (grammaire, § 20) : un champ et sa valeur, écrite comme Afficher l'écrirait.
 * Une image porte aussi ses octets, pour qu'une interface riche la montre. */
/* Un élément d'écran, tel que l'interface le montre (grammaire, § 22.1). */
typedef enum { ELEMENT_LISTE, ELEMENT_BOUTON, ELEMENT_TEXTE, ELEMENT_ZONE,
               ELEMENT_COTE_A_COTE, ELEMENT_L_UN_SOUS_L_AUTRE, ELEMENT_FIN_DE_BLOC,
               ELEMENT_GROUPE,
               ELEMENT_IMAGE,
               ELEMENT_LIBRE,
               ELEMENT_ONGLETS, ELEMENT_ONGLET,
               ELEMENT_GRAPHIQUE,
               ELEMENT_MENU } SorteElement;   /* « le menu « T » : » (texte : le titre), fermé par une fin de bloc : § 22.12 */   /* blocs : § 22.1 ; groupe titré (texte : le titre) : § 22.7 ;
                                                  image (texte : son nom ; octets, taille, format) : § 22.8 ;
                                                  bloc en position libre, fermé par ELEMENT_FIN_DE_BLOC : § 22.9 ;
                                                  les onglets, un onglet (texte : son titre), fermés de même : § 22.10 */
/* Au-delà de ce nombre d'objets, une zone qui en désigne un devient un champ de recherche, sans qu'on l'écrive (§ 22.6). */
#define RECHERCHE_AU_DELA 20

typedef struct {
    SorteElement sorte;
    const char *texte;              /* libellé du bouton, texte fixe ; pour une liste, l'entité */
    const char *const *colonnes;    /* liste : les titres des colonnes */
    size_t nb_colonnes;
    const char *type;               /* zone de saisie : son type (« texte », « vrai ou faux », une entité) */
    int facultatif;
    const char *const *choix;       /* zone liée à une entité : les clés de ses objets, pour un menu */
    size_t nb_choix;
    int plusieurs_lignes;           /* zone (texte) « sur plusieurs lignes » : Entrée va à la ligne (§ 22.6) */
    int lecture_seule;              /* zone « en lecture seule » : montrée, jamais modifiée par l'utilisateur */
    const unsigned char *octets;    /* image (§ 22.8) : son contenu, lu à l'ouverture de l'écran */
    size_t taille;
    const char *format;             /* PNG, JPEG, GIF ou WebP */
    int place;                      /* élément d'un bloc en position libre (§ 22.9) : 1, et les quatre mesures */
    int gauche, haut;               /* en pixels, depuis le coin en haut à gauche du bloc */
    int largeur, hauteur;           /* en pixels ; 0 : la taille naturelle */
    int calculee;                   /* image calculée (§ 22.8) : sans octets à l'ouverture ; ecran_image les donne */
    int graphique;                  /* graphique (§ 22.11) : 2 en secteurs, 4 une somme ; ses lignes : libellé, valeur */
    int interrupteur;               /* zone (vrai ou faux) « en interrupteur » : un interrupteur plutôt qu'une case (§ 22.6) */
    const char *indication;         /* zone : « , avec l'indication « … » », ou NULL (§ 22.6) */
    const char *unite;              /* zone : « , avec l'unité « CHF » », ou NULL ; unite_devant : à gauche du champ */
    int unite_devant;
    const char *borne_min, *borne_max;   /* zone : ses bornes en forme canonique (« 1 », « -2.5 »), en caractères pour un
                                            texte ; NULL sans borne (§ 22.6) */
    int masque;                     /* zone de texte « masqué » : la frappe cachée, un mot de passe (§ 22.6) */
    int dossier;                    /* zone de texte « avec le choix d'un dossier » : le sélecteur de dossiers (2.32) */
    int largeur_caracteres;         /* « , large de 12 caractères » (2.34) ; 0 : la largeur de la colonne */
    int recherche;                  /* zone d'un objet « avec recherche » : un champ où l'on tape, avec des propositions ;
                                       les interfaces le font aussi d'elles-mêmes au-delà de RECHERCHE_AU_DELA objets */
    int multiple;                   /* « des genres (genre) » : des choix multiples parmi `choix` ; sa valeur (ecran_valeurs)
                                       et son changement : les clés cochées, séparées par U+001E (§ 22.1) */
    /* Texte d'une fiche déduite (« Libellé : valeur », § 22.3) : la valeur commence à texte + valeur_depuis (0 : texte
       ordinaire). Une interface qui l'ignore montre le texte entier, ce qui reste juste. Une image ou un son de la fiche
       (§ 15.7) porte en plus ses octets, sa taille et son format, comme ELEMENT_IMAGE ; la valeur est alors son nom. */
    size_t valeur_depuis;
    char etiquette;                 /* la valeur en étiquette, de cette couleur (b v t p g r o, § 16.15) ; 0 : non */
    int lien;                       /* la valeur en lien : LIEN_COURRIEL (écrire), LIEN_WEB (ouvrir) ; 0 : non */
    int principal;                  /* un bouton « , principal » : l'action attendue, à la couleur d'accent (2.14) */
    int a_cote_du_titre;            /* un bouton « , à côté du titre » (2.17) : sur la ligne du titre, à droite */
    int inverse;                    /* un groupe « , en couleur inversée » (2.18) : fond d'accent, texte clair */
    int cartes;                     /* une liste « , en cartes » (2.19) : une carte par ligne (voir ecran_lignes) */
    int capitales;                  /* un texte « , en petites capitales » (2.20) : surtitre, titre de section */
    int tuile;                      /* un bouton « , en tuile » (2.15) : grand, son titre, sa description (indication) et
                                       son image (octets, taille, format) ; des tuiles qui se suivent forment une grille */
} ElementEcran;

#define LIEN_COURRIEL 1   /* « mailto: » + la valeur */
#define LIEN_WEB 2        /* la valeur elle-même, http:// ou https:// (§ 22.13) */

typedef enum { EVENEMENT_FERMETURE, EVENEMENT_CLIC, EVENEMENT_CHOIX, EVENEMENT_CHANGEMENT } SorteEvenement;
typedef struct {
    SorteEvenement sorte;
    size_t element;   /* le bouton cliqué, la liste où l'on a choisi */
    size_t ligne;     /* choix : la ligne, à partir de 0 */
    const char *texte;          /* changement : le nouveau texte de la zone */
    const long *choisies;       /* pour chaque élément : la ligne sélectionnée d'une liste, ou −1 ; NULL : aucune */
    size_t nb_choisies;
    const unsigned char *octets;   /* changement d'une zone de fichier ou d'image : le fichier choisi (texte : son nom) */
    size_t taille;
} Evenement;

typedef struct {
    const char *libelle;
    const char *texte;
    const unsigned char *octets;
    size_t taille;
    const char *format;    /* PNG, JPEG, GIF, WebP ; NULL si ce n'est pas une image */
    char etiquette;        /* le texte en étiquette (§ 16.15), comme dans ElementEcran ; 0 : non */
    int lien;              /* LIEN_COURRIEL, LIEN_WEB ; 0 : non */
} LigneFiche;

/* Un champ à remplir : décrit par la machine, rempli par l'interface. */
typedef struct {
    const char *libelle;   /* nom du champ en capitale (« Date d'inscription »), ou question entière */
    int question;          /* 1 : « la réponse à » : le libellé se pose tel quel */
    const char *type;      /* texte, nombre, nombre entier, vrai ou faux, date, année, fichier, image, ou une entité */
    const char *valeur;    /* valeur montrée (actuelle, ou de départ), NULL sinon */
    int videable;          /* l'utilisateur peut demander que le champ devienne absent */
    /* rempli par l'interface avant chaque validation ; la machine prend possession de la ligne */
    char *ligne;           /* la réponse telle que tapée */
    int vider;             /* l'utilisateur demande l'absence (la ligne est alors vide) */
    /* décrits par la machine (interfaces riches, docs/v2.md, § 10) */
    int facultatif;                      /* une réponse vide laisse le champ absent */
    const char *const *suggestions;      /* lien : les clés des objets conservés, dans l'ordre du dictionnaire */
    size_t nb_suggestions;
    int suggestions_completes;           /* 1 : la liste contient tous les objets (au plus 1000) ; un menu suffit */
    /* fichier ou image reçus par l'interface, au lieu d'un chemin tapé ; la machine en prend possession */
    int fichier_recu;
    unsigned char *octets;
    size_t taille;
    char *nom_fichier;     /* nom d'origine, sans dossier */
} Champ;

typedef enum {
    ISSUE_REPONDU,   /* chaque champ a une réponse acceptée */
    ISSUE_ANNULE,    /* l'utilisateur a annulé (§ 17 : « . » en console) */
    ISSUE_FIN,       /* plus rien à lire */
    ISSUE_ARRET,     /* la validation a demandé l'arrêt ; la machine connaît le motif */
    ISSUE_INTERROMPU /* Ctrl+C pendant l'attente (docs/vm.md, § 6) */
} Issue;

/* Validation d'une réponse, appelée par l'interface : en console après chaque champ, dans un navigateur
 * après l'envoi de la page. Rend 0 (acceptée), 1 (refusée : *message, à libérer, dit pourquoi, et le
 * champ se redemande) ou -1 (arrêt). */
typedef int (*Validation)(void *contexte, size_t k, char **message);

typedef struct {
    void *contexte;
    /* Une question peut-elle se poser ? (Sans lecteur, poser une question est une erreur, § 17.) */
    int (*disponible)(void *contexte);
    /* Montre ce qui attend dans *sortie, pose les n champs, valide chaque réponse. *arret : le champ où
     * l'interface s'est arrêtée, si l'issue n'est pas ISSUE_REPONDU. */
    Issue (*formulaire)(void *contexte, Chaine *sortie, Champ *champs, size_t n, size_t *arret,
                        Validation valider, void *vcontexte);
    /* « Effacer l'écran. » (grammaire, § 4.3) */
    void (*effacer)(void *contexte, Chaine *sortie);
    /* Interface riche : la machine lui fournit les suggestions des liens. */
    int riche;
    /* Afficher une image (PNG, JPEG, GIF, WebP) à cet endroit du fil ; NULL : sa description en texte. */
    void (*afficher_image)(void *contexte, Chaine *sortie, const unsigned char *octets, size_t taille,
                           const char *format, const char *description);
    /* « Afficher la fiche de p. » (grammaire, § 20) ; NULL : la machine l'écrit en colonnes. */
    void (*afficher_fiche)(void *contexte, Chaine *sortie, const char *titre, const LigneFiche *lignes, size_t n);
    /* Écrans (grammaire, § 22). NULL : l'interface n'en montre pas, et un programme qui en ouvre est refusé
     * avant toute exécution. */
    void (*ecran_ouvrir)(void *contexte, Chaine *sortie, const char *titre, const ElementEcran *elements, size_t n);
    /* Les lignes d'une liste : nb_lignes × nb_colonnes cellules, ligne par ligne ; remplace les précédentes. */
    /* Une cellule « ␝ lettre texte » est une étiquette (§ 16.15) : la lettre dit sa couleur (b bleu, v vert, t turquoise,
     * p violet, g gris, r rouge, o orange), le texte suit. Une ligne dont la première cellule commence par « ␜ » (U+001C)
     * est une ligne de total (§ 22.3) : montrée en gras, la dernière quel que soit le tri, jamais choisie ni sélectionnée. */
    void (*ecran_lignes)(void *contexte, size_t element, const char *const *cellules, size_t nb_lignes);
    /* Attend un événement ; ISSUE_INTERROMPU si l'exécution doit s'arrêter. */
    Issue (*ecran_attendre)(void *contexte, Chaine *sortie, Evenement *e);
    void (*ecran_erreur)(void *contexte, Chaine *sortie, const char *message);
    void (*ecran_fermer)(void *contexte, Chaine *sortie);
    /* Avant chaque attente : le texte de chaque zone de saisie (NULL pour un autre élément), à montrer. */
    void (*ecran_valeurs)(void *contexte, const char *const *valeurs, size_t n);
    /* « Les écrans ont la couleur verte et le logo « … ». » (§ 22.5) : couleur, rang dans la palette (0 bleue,
     * 1 verte, 2 turquoise, 3 violette, 4 grise) ou −1 ; logo : ses octets (image), ou NULL. NULL : sans effet. */
    void (*ecran_apparence)(void *contexte, int couleur, const unsigned char *logo, size_t taille, const char *format);
    /* Avant chaque attente : l'état de chaque élément (§ 22.6), bit 1 inactif (« actif si » faux), bit 2 caché
     * (« visible si » faux). NULL : l'interface montre tout, et la machine ignore quand même un geste sur un
     * élément inactif ou caché. */
    void (*ecran_etats)(void *contexte, const unsigned char *etats, size_t n);
    /* Avant chaque attente, pour chaque image calculée (§ 22.8) : ses octets, ou NULL si elle est absente. NULL :
     * l'interface ne les montre pas. */
    void (*ecran_image)(void *contexte, size_t element, const unsigned char *octets, size_t taille, const char *format);
    /* Juste avant ecran_ouvrir : la taille du titre, −1 « en petit », 0 celle du thème, 1 « en grand » (§ 22.5).
     * NULL : la taille du thème. */
    void (*ecran_taille_titre)(void *contexte, int taille);
    /* Juste avant ecran_erreur, quand des zones sont en faute : pour chacune, son élément et son message ; le message
     * d'ecran_erreur les résume (§ 22.6). NULL : seul le résumé se voit. */
    void (*ecran_erreurs_zones)(void *contexte, const size_t *elements, const char *const *messages, size_t n);
    /* Un rapport en PDF (§ 15.6) : la page HTML d'un export, mise en page ; les octets du PDF (malloc), ou NULL avec
     * *erreur. NULL : l'interface ne sait pas en fabriquer (console, navigateur). */
    unsigned char *(*rendre_pdf)(void *contexte, const char *html, size_t *taille, char **erreur);
    /* La transcription d'un son (§ 15.8) : son texte (malloc), ou NULL avec *erreur. NULL : l'interface ne sait pas
     * transcrire (console, navigateur) ; le champ transcrit reste alors tel quel. */
    char *(*transcrire)(void *contexte, const unsigned char *octets, size_t taille, char **erreur);
    /* « Les écrans ont la couleur « #46433B », le fond « #E8E2D6 », la police « … » … » (§ 22.5, 2.13), juste après
     * ecran_apparence : les jetons dérivés (teinte.h), chacun son nom, sa valeur en clair, en sombre (NULL : celle du
     * thème) ; clair_impose : un fond est donné, les écrans restent clairs ; la police (TrueType ou OpenType), ou NULL.
     * NULL : l'interface garde son thème. */
    void (*ecran_theme)(void *contexte, const char *const *noms, const char *const *clair, const char *const *sombre,
                        size_t n, int clair_impose, const unsigned char *police, size_t taille_police);
    /* « … et la signature « signature.png ». » (§ 22.5, 2.16), juste après ecran_theme : l'image d'en-tête de chaque
     * écran, en bandeau ; le logo ne sert plus alors qu'à l'icône de la fenêtre. NULL : sans en-tête. */
    void (*ecran_signature)(void *contexte, const unsigned char *octets, size_t taille, const char *format);
    /* Avant chaque attente, pour chaque texte calculé (§ 22.1, 2.24) : sa valeur, écrite comme Afficher. NULL :
     * l'interface garde le texte de l'ouverture. */
    void (*ecran_texte)(void *contexte, size_t element, const char *texte);
    /* « Ouvrir le document de quittance. » (2.25) : le PDF, à montrer dans un aperçu d'où l'on imprime. NULL : la
     * machine refuse « Ouvrir le document » (console, navigateur). */
    void (*montrer_document)(void *contexte, const unsigned char *octets, size_t taille, const char *nom);   /* nom : « document.pdf », « ticket.jpg » (2.26) */
    /* « Ouvrir le lien OneDoc du cabinet. » (2.41) : une adresse http:// ou https://, dans le navigateur du système. NULL :
     * la machine refuse (console, navigateur). */
    void (*ouvrir_adresse)(void *contexte, const char *adresse);
    /* « l'envoi à … de : » (2.42) : lecture 0, un POST du corps JSON ; 1, un GET de l'adresse déjà complétée. Rend 1 et la
     * réponse (allouée), avec son statut HTTP ; 0 et *erreur (allouée) si rien n'est revenu. NULL : la machine refuse. */
    int (*envoyer)(void *contexte, int lecture, const char *adresse, const char *corps, const char *entetes, long *statut,
                   char **reponse, char **erreur);   /* entetes : « Nom: valeur\n » par ligne, ou « » (2.60) */
    /* La transcription d'une zone d'écran en arrière-plan (24 octobre 2026) : le son de la zone vient de changer ; la
     * zone « transcrite du … » d'indice cible recevra le texte quand il sera prêt, par un événement de changement que
     * l'interface enverra elle-même ; la machine n'attend pas. NULL : la machine transcrit elle-même, et attend. */
    void (*ecran_transcrire)(void *contexte, size_t cible, const unsigned char *octets, size_t taille);
} Interface;

/* Forme console d'un champ : « Titre [L'Offrande musicale] (- pour vider) ? », ou la question telle quelle.
 * Sert aussi aux messages de la machine (« la réponse à « … » manque »). À libérer. */
char *champ_invite(const Champ *c);

/* Console : lire(contexte, sortie, invite) écrit *sortie, la vide, affiche l'invite, et rend la ligne
 * tapée sans son saut de ligne (NULL en fin d'entrée). */
typedef struct {
    char *(*lire)(void *contexte, Chaine *sortie, const char *invite);
    void *contexte;
    int terminal;    /* la sortie est un terminal : « Effacer l'écran. » écrit sa séquence */
    int annonce;     /* « Tapez « . » seul pour annuler. » déjà dit */
} Console;

/* Interface console sur c (qui doit vivre aussi longtemps qu'elle). */
Interface console_interface(Console *c);

#endif
