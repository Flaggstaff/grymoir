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
typedef enum { ELEMENT_LISTE, ELEMENT_BOUTON, ELEMENT_TEXTE } SorteElement;
typedef struct {
    SorteElement sorte;
    const char *texte;              /* libellé du bouton, texte fixe ; pour une liste, l'entité */
    const char *const *colonnes;    /* liste : les titres des colonnes */
    size_t nb_colonnes;
} ElementEcran;

typedef enum { EVENEMENT_FERMETURE, EVENEMENT_CLIC, EVENEMENT_CHOIX } SorteEvenement;
typedef struct {
    SorteEvenement sorte;
    size_t element;   /* le bouton cliqué, la liste où l'on a choisi */
    size_t ligne;     /* choix : la ligne, à partir de 0 */
} Evenement;

typedef struct {
    const char *libelle;
    const char *texte;
    const unsigned char *octets;
    size_t taille;
    const char *format;    /* PNG, JPEG, GIF, WebP ; NULL si ce n'est pas une image */
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
    void (*ecran_lignes)(void *contexte, size_t element, const char *const *cellules, size_t nb_lignes);
    /* Attend un événement ; ISSUE_INTERROMPU si l'exécution doit s'arrêter. */
    Issue (*ecran_attendre)(void *contexte, Chaine *sortie, Evenement *e);
    void (*ecran_erreur)(void *contexte, Chaine *sortie, const char *message);
    void (*ecran_fermer)(void *contexte, Chaine *sortie);
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
