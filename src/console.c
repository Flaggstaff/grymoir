/* GrymoiR : l'interface d'entrée et de sortie en console.
 * Spécification : docs/vm.md (révision 1.31), § 13 ; grammaire, § 4.3, § 17 à 19.
 */
#include "interface.h"

#include <stdlib.h>
#include <string.h>

char *champ_invite(const Champ *c) {
    if (c->question) return grym_dupliquer(c->libelle);
    if (c->valeur) return grym_formater("%s [%s]%s ?", c->libelle, c->valeur, c->videable ? " (- pour vider)" : "");
    return grym_formater("%s ?", c->libelle);
}

/* La ligne, sans espaces ni tabulations autour, est-elle exactement s ? */
static int seule(const char *ligne, const char *s) {
    while (*ligne == ' ' || *ligne == '\t') ligne++;
    size_t n = strlen(s);
    if (strncmp(ligne, s, n) != 0) return 0;
    ligne += n;
    while (*ligne == ' ' || *ligne == '\t') ligne++;
    return *ligne == '\0';
}

static int console_disponible(void *contexte) {
    return ((Console *)contexte)->lire != NULL;
}

static Issue console_formulaire(void *contexte, Chaine *sortie, Champ *champs, size_t n, size_t *arret,
                                Validation valider, void *vcontexte) {
    Console *c = contexte;
    for (size_t k = 0; k < n; k++) {
        for (;;) {
            char *invite = champ_invite(&champs[k]);
            char *ligne = c->lire(c->contexte, sortie, invite);
            free(invite);
            if (!ligne) { *arret = k; return ISSUE_FIN; }
            if (seule(ligne, ".")) { free(ligne); *arret = k; return ISSUE_ANNULE; }   /* § 17 */
            champs[k].vider = champs[k].videable && seule(ligne, "-");                  /* § 19 */
            if (champs[k].vider) ligne[0] = '\0';
            champs[k].ligne = ligne;
            char *message = NULL;
            int r = valider(vcontexte, k, &message);
            if (r == 0) break;
            if (r < 0) { free(message); *arret = k; return ISSUE_ARRET; }
            /* la relance s'affiche avant la question suivante ; la première dit comment annuler */
            chaine_ajouter(sortie, message ? message : "");
            free(message);
            if (!c->annonce) {
                chaine_ajouter(sortie, " Tapez « . » seul pour annuler.");
                c->annonce = 1;
            }
            chaine_ajouter(sortie, "\n");
        }
    }
    return ISSUE_REPONDU;
}

static void console_effacer(void *contexte, Chaine *sortie) {
    /* hors d'un terminal, rien n'est écrit : une sortie redirigée reste propre (§ 4.3) */
    if (((Console *)contexte)->terminal) chaine_ajouter(sortie, "\033[2J\033[H");
}

Interface console_interface(Console *c) {
    Interface i = { c, console_disponible, console_formulaire, console_effacer, 0, NULL };
    return i;
}
