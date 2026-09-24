/* GrymoiR : interface d'entrée et de sortie de la machine.
 * Spécification : docs/vm.md (révision 1.29), § 13.
 *
 * Tout ce qui touche l'utilisateur passe par ici : poser des questions (une seule, ou un formulaire),
 * effacer l'écran. La console l'implémente (console.c) ; le navigateur l'implémentera (docs/v2.md).
 * L'affichage s'accumule dans une Chaine, que l'interface reçoit à chaque question.
 */
#ifndef GRYM_INTERFACE_H
#define GRYM_INTERFACE_H

#include "texte.h"

#include <stddef.h>

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
} Champ;

typedef enum {
    ISSUE_REPONDU,   /* chaque champ a une réponse acceptée */
    ISSUE_ANNULE,    /* l'utilisateur a annulé (§ 17 : « . » en console) */
    ISSUE_FIN,       /* plus rien à lire */
    ISSUE_ARRET      /* la validation a demandé l'arrêt ; la machine connaît le motif */
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
