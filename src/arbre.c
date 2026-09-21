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
            {'P', "positif"}, {'N', "négatif"}, {'0', "nul"}, {'V', "vrai"}, {'F', "faux"}
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
    case N_FICHIER:
        chaine_ajouter(c, "(fichier ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_ENREGISTRER:
        chaine_ajouter(c, "(enregistrer ");
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
