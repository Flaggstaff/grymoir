/* GrymoiR : arbre syntaxique, v0.1 */
#include "arbre.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>

Noeud *noeud_creer(TypeNoeud type, int ligne, int colonne, size_t debut) {
    Noeud *n = grym_allouer(sizeof *n);
    n->type = type;
    n->op = 0;
    n->negation = 0;
    n->forme = 0;
    n->crochets = 0;
    n->local = -1;
    n->entier = 0;
    n->ligne_fin = ligne;
    n->article = ART_AUCUN;
    n->texte = NULL;
    n->texte2 = NULL;
    n->texte3 = NULL;
    n->enfants = NULL;
    n->nb_enfants = 0;
    n->ligne = ligne;
    n->colonne = colonne;
    n->op_ligne = ligne;
    n->op_colonne = colonne;
    n->debut = debut;
    n->fin = debut;
    return n;
}

void noeud_ajouter(Noeud *parent, Noeud *enfant) {
    Noeud **e = realloc(parent->enfants, (parent->nb_enfants + 1) * sizeof *e);
    if (!e) {
        fputs("GrymoiR : mémoire épuisée\n", stderr);
        exit(EXIT_FAILURE);
    }
    parent->enfants = e;
    parent->enfants[parent->nb_enfants++] = enfant;
}

void noeud_liberer(Noeud *n) {
    if (!n) return;
    for (size_t i = 0; i < n->nb_enfants; i++) noeud_liberer(n->enfants[i]);
    free(n->enfants);
    free(n->texte);
    free(n->texte2);
    free(n->texte3);
    free(n);
}

static const char *symbole(char op) {
    switch (op) {
    case '+': return "+";
    case '-': return "−";
    case '*': return "×";
    case '/': return "÷";
    case '^': return "^";
    default:  return "?";
    }
}

static void decrire(const Noeud *n, Chaine *c) {
    switch (n->type) {
    case N_NOMBRE:
        chaine_ajouter(c, n->texte);
        return;
    case N_NOM:
        chaine_ajouter(c, "[");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "]");
        return;
    case N_TEXTE:
        chaine_ajouter(c, "«");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "»");
        return;
    case N_NEGATION:
        chaine_ajouter(c, "(− ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case N_OPERATION:
        chaine_ajouter(c, "(");
        chaine_ajouter(c, symbole(n->op));
        chaine_ajouter(c, " ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case N_GROUPE:
        chaine_ajouter(c, "(groupe ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_CREATION:
    case P_MODIFICATION:
        chaine_ajouter(c, n->type == P_CREATION ? "(créer [" : "(modifier [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "] ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_AFFICHAGE:
        chaine_ajouter(c, "(afficher");
        for (size_t i = 0; i < n->nb_enfants; i++) {
            chaine_ajouter(c, " ");
            decrire(n->enfants[i], c);
        }
        chaine_ajouter(c, ")");
        return;
    case P_REMARQUE:
        chaine_ajouter(c, "(remarque «");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "»)");
        return;
    case P_EXPRESSION:
        chaine_ajouter(c, "(évaluer ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case N_BOOLEEN:
        chaine_ajouter(c, n->texte);
        return;
    case N_COMPARAISON: {
        static const struct { char op; const char *nom; } R[] = {
            {'=', "="}, {'!', "≠"}, {'<', "<"}, {'>', ">"}, {'l', "≤"}, {'g', "≥"},
            {'P', "positif"}, {'N', "négatif"}, {'0', "nul"}, {'V', "vrai"}, {'F', "faux"}, {'A', "absent"}, {'R', "présent"}, {'p', "parmi"}
        };
        const char *nom = "?";
        for (size_t k = 0; k < sizeof R / sizeof *R; k++) if (R[k].op == n->op) nom = R[k].nom;
        if (n->negation) chaine_ajouter(c, "(non ");
        chaine_ajouter(c, "(");
        chaine_ajouter(c, nom);
        for (size_t k = 0; k < n->nb_enfants; k++) {
            chaine_ajouter(c, " ");
            decrire(n->enfants[k], c);
        }
        chaine_ajouter(c, ")");
        if (n->negation) chaine_ajouter(c, ")");
        return;
    }
    case N_LOGIQUE:
        chaine_ajouter(c, n->op == 'e' ? "(et " : "(ou ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case N_BLOC:
        chaine_ajouter(c, "(bloc");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            chaine_ajouter(c, " ");
            decrire(n->enfants[k], c);
        }
        chaine_ajouter(c, ")");
        return;
    case N_SUJET:
        chaine_ajouter(c, "(sujet)");
        return;
    case N_INTERVALLE:
        chaine_ajouter(c, "(entre ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case P_SORTIR:
        chaine_ajouter(c, "(sortir)");
        return;
    case P_PASSER:
        chaine_ajouter(c, "(passer)");
        return;
    case P_TANT_QUE:
    case P_REPETER:
    case P_SELON:
    case N_CAS:
        chaine_ajouter(c, n->type == P_TANT_QUE ? "(tant-que" : n->type == P_REPETER ? "(répéter"
                        : n->type == P_SELON ? "(selon" : n->forme ? "(autrement" : "(cas");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            chaine_ajouter(c, " ");
            decrire(n->enfants[k], c);
        }
        chaine_ajouter(c, ")");
        return;
    case P_POUR_CHAQUE:
        chaine_ajouter(c, "(pour-chaque [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "]");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            chaine_ajouter(c, " ");
            decrire(n->enfants[k], c);
        }
        chaine_ajouter(c, ")");
        return;
    case N_DATE:
        chaine_ajouter(c, "(date ");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, ")");
        return;
    case N_AUJOURDHUI:
        chaine_ajouter(c, "aujourd'hui");
        return;
    case N_ABSENT:
        chaine_ajouter(c, "absent");
        return;
    case N_FICHIER:
        chaine_ajouter(c, "(fichier ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_CONSERVER:
    case P_SUPPRIMER:
        chaine_ajouter(c, n->type == P_CONSERVER ? "(conserver " : n->entier == 2 ? "(rétablir "
                          : n->entier == 1 ? "(supprimer-définitivement " : "(supprimer ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_GAGNER:   /* (gagner [genres] [o] [baroque]) (§ 16.13) */
        chaine_ajouter(c, n->forme ? "(perdre [" : "(gagner [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "] ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case P_ENREGISTRER:
        chaine_ajouter(c, "(enregistrer ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case N_CHAMP_DONT:
        chaine_ajouter(c, "(champ-dont [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "])");
        return;
    case N_CHERCHER:
        chaine_ajouter(c, n->forme == 1 ? (n->negation ? "(le-supprimé [" : "(le-conservé [")
                          : n->forme == 2 ? (n->negation ? "(nombre-supprimés [" : "(nombre-conservés [")
                          : n->negation ? "(supprimés [" : "(conservés [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "]");
        if (n->op == 'M') {   /* éléments d'un champ multiple (§ 16.13) */
            chaine_ajouter(c, " (parmi [");
            chaine_ajouter(c, n->texte3);
            chaine_ajouter(c, "] ");
            decrire(n->enfants[n->nb_enfants - 1], c);
            chaine_ajouter(c, ")");
        }
        if (n->op == 'I') {   /* relation inverse : l'objet en dernier enfant (§ 16.10) */
            chaine_ajouter(c, " (de ");
            decrire(n->enfants[n->nb_enfants - 1], c);
            chaine_ajouter(c, ")");
        }
        if (n->nb_enfants > (n->op == 'I' || n->op == 'M' ? 1u : 0u)) {
            chaine_ajouter(c, " (dont ");
            decrire(n->enfants[0], c);
            chaine_ajouter(c, ")");
        }
        if (n->texte2) {
            chaine_ajouter(c, " (par [");
            chaine_ajouter(c, n->texte2);
            chaine_ajouter(c, n->entier ? "] décroissant)" : "])");
        }
        chaine_ajouter(c, ")");
        return;
    case P_POUR_CONSERVE:
        chaine_ajouter(c, "(pour-chaque-conservé [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "] ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case N_CHAMP:
        chaine_ajouter(c, "(champ [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "] ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case N_NOUVEAU:
        chaine_ajouter(c, "(nouveau [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "]");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            chaine_ajouter(c, " ");
            decrire(n->enfants[k], c);
        }
        chaine_ajouter(c, ")");
        return;
    case N_INIT:
        chaine_ajouter(c, "([");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "] ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_CLASSE:
    case P_APTITUDE:
        chaine_ajouter(c, n->type == P_APTITUDE ? "(aptitude [" : (n->forme & 32) ? "(entité [" : "(classe [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "]");
        if (n->texte3) {
            chaine_ajouter(c, " (pluriel ");
            chaine_ajouter(c, n->texte3);
            chaine_ajouter(c, ")");
        }
        if (n->type == P_CLASSE && n->texte2) {
            chaine_ajouter(c, " (est [");
            chaine_ajouter(c, n->texte2);
            chaine_ajouter(c, "])");
        }
        for (size_t k = 0; k < n->nb_enfants; k++) {
            const Noeud *ch = n->enfants[k];
            chaine_ajouter(c, " ");
            if (ch->type == N_NOM && ch->texte2) {   /* champ typé : [nom : texte], [licence : texte unique] */
                chaine_ajouter(c, "[");
                chaine_ajouter(c, ch->texte);
                chaine_ajouter(c, " : ");
                chaine_ajouter(c, ch->texte2);
                if (ch->op == 'U') chaine_ajouter(c, " unique");
                if (ch->entier & 1) chaine_ajouter(c, " facultatif");
                if (ch->entier & 2) chaine_ajouter(c, " disparaît-avec");
                if (ch->forme == 3) {   /* champ multiple : [genres : des genre] */
                    chaine_ajouter(c, " multiple");
                    if (ch->texte3) { chaine_ajouter(c, " singulier "); chaine_ajouter(c, ch->texte3); }
                }
                if (ch->nb_enfants) { chaine_ajouter(c, " départ "); decrire(ch->enfants[0], c); }
                chaine_ajouter(c, "]");
            } else {
                decrire(ch, c);
            }
        }
        chaine_ajouter(c, ")");
        return;
    case P_MODIF_CHAMP:
        chaine_ajouter(c, "(modifier-champ [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "] ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case N_APPEL:
    case P_APPEL:
        chaine_ajouter(c, n->type == N_APPEL ? "(appel [" : "(action-appel [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "]");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            chaine_ajouter(c, " ");
            decrire(n->enfants[k], c);
        }
        chaine_ajouter(c, ")");
        return;
    case P_CALCUL:
    case P_ACTION:
        chaine_ajouter(c, n->type == P_CALCUL ? "(calcul [" : "(action [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "] (");
        for (size_t k = 0; k < n->enfants[0]->nb_enfants; k++) {
            if (k) chaine_ajouter(c, " ");
            decrire(n->enfants[0]->enfants[k], c);
        }
        chaine_ajouter(c, ") ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case P_RENDRE:
        chaine_ajouter(c, "(rendre ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_SI:
        chaine_ajouter(c, "(si ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        if (n->nb_enfants > 2) {
            chaine_ajouter(c, " (sinon ");
            decrire(n->enfants[2], c);
            chaine_ajouter(c, ")");
        }
        chaine_ajouter(c, ")");
        return;
    }
}

char *noeud_decrire(const Noeud *n) {
    Chaine c = {0};
    decrire(n, &c);
    return chaine_rendre(&c);
}
