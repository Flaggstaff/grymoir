/* GrymoiR : compilateur de l'arbre vers le bytecode, v0.2 */
#include "compilateur.h"
#include "imprimeur.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Boucle en cours : où va « Passer au tour suivant », quels sauts « Sortir » doit corriger. */
static int mode_exemples = 0;   /* grym exemples (grammaire, § 9.6) */

typedef struct {
    size_t suivant;          /* cible de « Passer », si elle est déjà connue */
    int suivant_connu;
    size_t *sorties, nb_sorties;
    size_t *suivants, nb_suivants;   /* « Passer » en attente de leur cible */
    int essais;              /* essais ouverts à l'entrée de la boucle */
} Boucle;

typedef struct {
    Bloc *b;           /* bloc en cours de compilation */
    Module *module;
    Diagnostic *diag;
    int echec;
    Boucle *boucles;
    size_t nb_boucles;
    int essais;        /* blocs « Essayer » ouverts autour de la phrase en cours (§ 18) */
    const Programme *programme;
    const char *fichier;   /* fichier utilisé dont on compile les déclarations (§ 21), NULL pour le principal */
    const Noeud **regles;  /* les règles du projet, fichiers utilisés compris (§ 16.14), dans l'ordre de lecture */
    char **cles;           /* la clé de chaque règle : son arbre décrit, avant que son calcul ne soit renommé */
    size_t nb_regles;
} Compilation;

static void emettre(Compilation *c, CodeInstruction code, long op, int ligne, int colonne);

/* Le nom réservé du calcul de la k-ième règle : le \x01 initial ne s'écrit dans aucune source. */
static char *nom_regle(size_t k) { return grym_formater("\x01règle %lu", (unsigned long)k); }

/* Avant chaque validation (§ 16.14) : si la transaction a écrit, chaque règle parcourt ses objets et refuse au
 * premier qui la viole ; le refus est une erreur comme une autre, qu'un événement rattrape. Une règle dont rien de
 * ce qu'elle a lu la dernière fois n'a changé ne se revérifie pas (docs/vm.md, § 3.3) : RÈGLE_UTILE le dit. */
static void verifier_regles(Compilation *c, int ligne, int colonne) {
    if (!c->nb_regles) return;
    emettre(c, I_BASE_MODIFIEE, 0, ligne, colonne);
    size_t vers_fin = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, ligne, colonne);
    for (size_t k = 0; k < c->nb_regles; k++) {
        char *nom = nom_regle(k);
        long i = bloc_nom(c->b, nom);
        free(nom);
        long cle = bloc_constante(c->b, C_TEXTE, c->cles[k]);
        if (i < 0 || cle < 0) { c->echec = 1; return; }
        emettre(c, I_REGLE_UTILE, cle, ligne, colonne);
        size_t sauter = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, ligne, colonne);
        bloc_emettre_appel(c->b, (uint16_t)i, 0, 1, ligne, colonne);
        size_t s = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, ligne, colonne);   /* rend vrai : on jette la valeur */
        bloc_corriger_saut(c->b, s, c->b->taille_code);
        emettre(c, I_REGLE_FAITE, cle, ligne, colonne);
        bloc_corriger_saut(c->b, sauter, c->b->taille_code);
    }
    bloc_corriger_saut(c->b, vers_fin, c->b->taille_code);
}

static void recenser_regles(Compilation *c, Noeud *const *ph, size_t nb) {
    for (size_t k = 0; k < nb; k++) {
        if (ph[k]->type == P_UTILISER) recenser_regles(c, ph[k]->enfants, ph[k]->nb_enfants);
        else if (ph[k]->type == P_REGLE) {
            const Noeud **t = grym_allouer((c->nb_regles + 1) * sizeof *t);
            char **cl = grym_allouer((c->nb_regles + 1) * sizeof *cl);
            if (c->nb_regles) memcpy(t, c->regles, c->nb_regles * sizeof *t);
            if (c->nb_regles) memcpy(cl, c->cles, c->nb_regles * sizeof *cl);
            free(c->regles);
            free(c->cles);
            c->regles = t;
            c->cles = cl;
            c->cles[c->nb_regles] = noeud_decrire(ph[k]);
            c->regles[c->nb_regles++] = ph[k];
        }
    }
}

static void trop_grand(Compilation *c, const Noeud *n) {
    if (c->echec) return;
    c->echec = 1;
    c->diag->message = grym_dupliquer(
        "Programme trop grand : un bloc accepte au plus 65'536 constantes et 65'536 noms.");
    c->diag->ligne = n->ligne;
    c->diag->colonne = n->colonne;
    c->diag->fichier = c->fichier ? grym_dupliquer(c->fichier) : NULL;
}

static void emettre(Compilation *c, CodeInstruction code, long operande, int ligne, int colonne) {
    bloc_emettre(c->b, code, (uint16_t)(operande < 0 ? 0 : operande), ligne, colonne);
}

static void constante(Compilation *c, TypeConstante type, const char *texte, const Noeud *n, int ligne, int colonne) {
    long k = bloc_constante(c->b, type, texte);
    if (k < 0) { trop_grand(c, n); return; }
    emettre(c, I_CONSTANTE, k, ligne, colonne);
}

static void expression(Compilation *c, const Noeud *n);
static void document(Compilation *c, const Noeud *n, int montrer);
static uint16_t unite_duree(const Noeud *d);
static void appel(Compilation *c, const Noeud *n, int rend);

/* Condition : après son code, la pile porte vrai ou faux. SAUTER_SI_FAUX vérifie
 * au passage que chaque membre de « et » et « ou » est bien un booléen. */
static void logique(Compilation *c, const Noeud *n) {
    int l = n->op_ligne, col = n->op_colonne;
    expression(c, n->enfants[0]);
    if (n->op == 'e') {
        /* g ; si faux → faux ; d ; si faux → faux ; vrai */
        size_t s1 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        expression(c, n->enfants[1]);
        size_t s2 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        constante(c, C_BOOLEEN, "vrai", n, l, col);
        size_t s3 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, s1, c->b->taille_code);
        bloc_corriger_saut(c->b, s2, c->b->taille_code);
        constante(c, C_BOOLEEN, "faux", n, l, col);
        bloc_corriger_saut(c->b, s3, c->b->taille_code);
    } else {
        /* g ; si faux → examiner d ; vrai ; … d ; si faux → faux ; vrai */
        size_t s1 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        constante(c, C_BOOLEEN, "vrai", n, l, col);
        size_t s2 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, s1, c->b->taille_code);
        expression(c, n->enfants[1]);
        size_t s3 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        constante(c, C_BOOLEEN, "vrai", n, l, col);
        size_t s4 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, s3, c->b->taille_code);
        constante(c, C_BOOLEEN, "faux", n, l, col);
        bloc_corriger_saut(c->b, s2, c->b->taille_code);
        bloc_corriger_saut(c->b, s4, c->b->taille_code);
    }
}

static void comparaison(Compilation *c, const Noeud *n) {
    int l = n->op_ligne, col = n->op_colonne;
    expression(c, n->enfants[0]);
    CodeInstruction code;
    if (n->op == 'm') {   /* « m est parmi les élus » (§ 13.9) : la valeur, l'ensemble, puis PARMI */
        expression(c, n->enfants[1]);
        emettre(c, I_PARMI, 0, l, col);
        if (n->negation) emettre(c, I_NON, 0, l, col);
        return;
    }
    if (n->op == 'k') {   /* « le code de l'écran correspond au code du membre » : la clair, l'empreinte (§ 16.1) */
        expression(c, n->enfants[1]);   /* lu juste avant CORRESPOND : la seule lecture permise d'un mot de passe */
        emettre(c, I_CORRESPOND, 0, l, col);
        return;
    }
    if (n->op == 'c' || n->op == 'd' || n->op == 'f') {   /* contient, commence par, finit par (§ 4.5) */
        expression(c, n->enfants[1]);
        emettre(c, I_CONTIENT, n->op == 'c' ? 0 : n->op == 'd' ? 1 : 2, l, col);
        if (n->negation) emettre(c, I_NON, 0, l, col);
        return;
    }
    if (n->op == 'A' || n->op == 'R') {   /* est absent, est présent (§ 16.9) */
        emettre(c, I_EST_ABSENT, 0, l, col);
        if ((n->op == 'R') != (n->negation != 0)) emettre(c, I_NON, 0, l, col);
        return;
    }
    switch (n->op) {
    case '=': code = I_EGAL; break;
    case '!': code = I_DIFFERENT; break;
    case '<': code = I_INFERIEUR; break;
    case '>': code = I_SUPERIEUR; break;
    case 'l': code = I_INFERIEUR_OU_EGAL; break;
    case 'g': code = I_SUPERIEUR_OU_EGAL; break;
    case 'P': code = I_SUPERIEUR; break;   /* positif : > 0 (sens courant, § 5.1) */
    case 'N': code = I_INFERIEUR; break;   /* négatif : < 0 */
    case '0': code = I_EGAL; break;        /* nul : = 0 */
    default:  code = I_EGAL; break;        /* vrai, faux */
    }
    if (n->nb_enfants > 1)                        expression(c, n->enfants[1]);
    else if (n->op == 'V' || n->op == 'F')        constante(c, C_BOOLEEN, n->op == 'V' ? "vrai" : "faux", n, l, col);
    else                                          constante(c, C_NOMBRE, "0", n, l, col);
    emettre(c, code, 0, l, col);
    if (n->negation) emettre(c, I_NON, 0, l, col);
}

/* Arguments empilés dans l'ordre, puis APPELER (docs/vm.md, § 3). */
static void appel(Compilation *c, const Noeud *n, int rend) {
    if (n->nb_enfants > 255) { trop_grand(c, n); return; }
    for (size_t k = 0; k < n->nb_enfants; k++) expression(c, n->enfants[k]);
    /* « comme une personne » (§ 13.8) : « saluer␞personne », la version cherchée depuis cette classe */
    char *x = n->texte2 ? grym_formater("%s\x1e%s", n->texte, n->texte2) : grym_dupliquer(n->texte);
    long nom = bloc_nom(c->b, x);
    free(x);
    if (nom < 0) { trop_grand(c, n); return; }
    bloc_emettre_appel(c->b, (uint16_t)nom, (uint8_t)n->nb_enfants, rend, n->ligne, n->colonne);
}

/* ---------------------------------------------------------------- */
/* Recherche dans la base (grammaire, § 16.4)                       */
/* ---------------------------------------------------------------- */

static void expression(Compilation *c, const Noeud *n);

/* Condition « dont » en descripteur ; chaque valeur comparée est calculée ici et devient « ?n ». */
static void condition_dont(Compilation *c, const Noeud *n, Chaine *d, int *parametres) {
    if (n->type == N_GROUPE) { condition_dont(c, n->enfants[0], d, parametres); return; }
    if (n->type == N_LOGIQUE) {
        chaine_ajouter(d, n->op == 'e' ? "(e" : "(o");
        condition_dont(c, n->enfants[0], d, parametres);
        condition_dont(c, n->enfants[1], d, parametres);
        chaine_ajouter(d, ")");
        return;
    }
    /* N_COMPARAISON, champ à gauche (vérifié à l'analyse) */
    if (n->negation) chaine_ajouter(d, "(n");
    char op[4] = { '(', n->op, '[', 0 };
    chaine_ajouter(d, op);
    if (n->enfants[0]->texte2) {   /* à travers des liens : « [écriture␜comptabilisation] » (§ 16.4) */
        chaine_ajouter(d, n->enfants[0]->texte2);
        chaine_ajouter(d, "\x1c");
    }
    chaine_ajouter(d, n->enfants[0]->texte);
    chaine_ajouter(d, "]");
    if (n->nb_enfants == 2) {
        expression(c, n->enfants[1]);
        char t[16];
        snprintf(t, sizeof t, "?%d", ++*parametres);
        chaine_ajouter(d, t);
    }
    chaine_ajouter(d, ")");
    if (n->negation) chaine_ajouter(d, ")");
}

static void chercher(Compilation *c, const Noeud *n) {
    Chaine d = {0};
    chaine_ajouter(&d, n->texte);
    char t[8];
    /* modes 0 à 2 ; 3 à 5 : les mêmes, dans la corbeille (§ 16.12) ; 6 : somme du champ texte2, 7 : dans la corbeille ;
     * 8, 9 : moyenne ; a, b : plus grand ; c, d : plus petit ; e, f : l'objet au plus grand (ou petit) champ texte2 */
    static const char AGREGATS[] = "68ace";
    const char mode = n->forme >= 3 ? (char)(AGREGATS[n->forme - 3] + (n->negation ? 1 : 0))
                                    : (char)('0' + n->forme + (n->negation ? 3 : 0));
    snprintf(t, sizeof t, "\x1f%c\x1f", mode);
    chaine_ajouter(&d, t);
    if (n->texte2) chaine_ajouter(&d, n->texte2);
    chaine_ajouter(&d, (n->entier & 1) ? "\x1f" "1\x1f" : "\x1f" "0\x1f");
    int parametres = 0;
    /* « les œuvres de bach » (§ 16.10), « les interprètes de o » (§ 16.13) : l'objet en dernier enfant */
    int inverse = n->op == 'I' || n->op == 'M';
    int condition = n->nb_enfants > (size_t)inverse;
    if (inverse) {
        if (condition) chaine_ajouter(&d, "(e");
        expression(c, n->enfants[n->nb_enfants - 1]);
        char t[16];
        snprintf(t, sizeof t, "(%c?%d", n->op, ++parametres);
        chaine_ajouter(&d, t);
        if (n->op == 'M') { chaine_ajouter(&d, "["); chaine_ajouter(&d, n->texte3); chaine_ajouter(&d, "]"); }
        chaine_ajouter(&d, ")");
    }
    if (condition) condition_dont(c, n->enfants[0], &d, &parametres);
    if (inverse && condition) chaine_ajouter(&d, ")");
    char *texte = chaine_rendre(&d);
    long k = bloc_constante(c->b, C_RECHERCHE, texte);
    free(texte);
    if (k < 0) { trop_grand(c, n); return; }
    emettre(c, I_CHERCHER, k, n->ligne, n->colonne);
}

static void expression(Compilation *c, const Noeud *n) {
    if (c->echec) return;
    switch (n->type) {
    case N_NOMBRE:
    case N_TEXTE: {
        long k = bloc_constante(c->b, n->type == N_NOMBRE ? C_NOMBRE : C_TEXTE, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_CONSTANTE, k, n->ligne, n->colonne);
        return;
    }
    case N_SUJET:
        emettre(c, I_LIRE_LOCAL, n->local, n->ligne, n->colonne);
        return;
    case N_NOM: {
        if (n->local >= 0) {
            emettre(c, I_LIRE_LOCAL, n->local, n->ligne, n->colonne);
            return;
        }
        long k = bloc_nom(c->b, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_LIRE, k, n->ligne, n->colonne);
        return;
    }
    case N_APPEL:
        appel(c, n, 1);
        return;
    case N_NOUVEAU: {
        long k = bloc_nom(c->b, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_NOUVEAU, k, n->ligne, n->colonne);
        for (size_t q = 0; q < n->nb_enfants; q++) {
            const Noeud *init = n->enfants[q];
            expression(c, init->enfants[0]);
            long ch = bloc_nom(c->b, init->texte);
            if (ch < 0) { trop_grand(c, n); return; }
            emettre(c, I_INITIALISER_CHAMP, ch, init->ligne, init->colonne);
        }
        if (n->op == 'S') {   /* « saisi » : les champs du bloc ne sont pas demandés (§ 19) */
            Chaine deja = {0};
            for (size_t q = 0; q < n->nb_enfants; q++) {
                if (q) chaine_ajouter(&deja, ", ");
                chaine_ajouter(&deja, n->enfants[q]->texte);
            }
            char *t = chaine_rendre(&deja);
            long k2 = bloc_constante(c->b, C_TEXTE, t);
            free(t);
            if (k2 < 0) { trop_grand(c, n); return; }
            verifier_regles(c, n->ligne, n->colonne);   /* un formulaire valide ce qui précède */
            emettre(c, I_SAISIR, k2, n->ligne, n->colonne);
        }
        return;
    }
    case N_DATE: {
        long k = bloc_constante(c->b, C_DATE, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_CONSTANTE, k, n->ligne, n->colonne);
        return;
    }
    case N_AUJOURDHUI:
        emettre(c, I_AUJOURDHUI, 0, n->ligne, n->colonne);
        return;
    case N_HEURE:   /* « 14:30 » : HEURE avec les minutes (§ 14.7) */
        emettre(c, I_HEURE, n->entier, n->ligne, n->colonne);
        return;
    case N_MAINTENANT:
        emettre(c, I_MAINTENANT, 0, n->ligne, n->colonne);
        return;
    case N_ABSENT:
        emettre(c, I_ABSENT, 0, n->ligne, n->colonne);
        return;
    case N_CHERCHER:
        chercher(c, n);
        return;
    case N_CODE_QR:   /* « le code QR suisse de t » : le texte, puis son image PNG (2.27) */
        expression(c, n->enfants[0]);
        emettre(c, I_CODE_QR, n->forme, n->ligne, n->colonne);
        return;
    case N_DOCUMENT:   /* « le document de quittance » : son PDF, un fichier (2.25) */
        document(c, n, 0);
        return;
    case N_ECRAN:   /* « l'écran » : l'objet de l'écran ouvert (§ 22.2) */
        emettre(c, I_ECRAN_OBJET, 0, n->ligne, n->colonne);
        return;
    case N_CADRE:   /* la valeur, la largeur, puis CADRER avec le sens (§ 4.2) */
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        emettre(c, I_CADRER, (long)n->forme, n->op_ligne, n->op_colonne);
        return;
    case N_COLLAGE:   /* « a suivi de b » : COLLER (§ 4.4) */
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        emettre(c, I_COLLER, 0, n->op_ligne, n->op_colonne);
        return;
    case N_ELISION:   /* « de x » : ÉLIDER 0 ; « que x » : ÉLIDER 1 (§ 4.4) */
        expression(c, n->enfants[0]);
        emettre(c, I_ELIDER, n->op == 'q', n->ligne, n->colonne);
        return;
    case N_MOTIF:   /* « le motif de l'échec » : la case où l'échec l'a rangé (§ 18) */
        emettre(c, I_LIRE_LOCAL, n->local, n->ligne, n->colonne);
        return;
    case N_REPONSE: {   /* la question, puis DEMANDER avec le type (§ 17) */
        expression(c, n->enfants[0]);
        long t = bloc_nom(c->b, n->texte2);
        if (t < 0) { trop_grand(c, n); return; }
        verifier_regles(c, n->ligne, n->colonne);   /* une question valide ce qui précède */
        emettre(c, I_DEMANDER, t, n->ligne, n->colonne);
        return;
    }
    case N_FICHIER:
        expression(c, n->enfants[0]);
        emettre(c, I_LIRE_FICHIER, 0, n->ligne, n->colonne);
        return;
    case N_CHAMP: {
        expression(c, n->enfants[0]);
        long ch = bloc_nom(c->b, n->texte);
        if (ch < 0) { trop_grand(c, n); return; }
        emettre(c, I_LIRE_CHAMP, ch, n->op_ligne, n->op_colonne);
        return;
    }
    case N_ECART:   /* d1, d2, puis ÉCART avec l'unité (§ 14.6) */
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        emettre(c, I_ECART, n->op == 'j' ? 0 : n->op == 's' ? 1 : n->op == 'm' ? 2 : n->op == 'n' ? 4 : n->op == 'h' ? 5
                            : n->op == 'o' ? 6 : 3,
                n->ligne, n->colonne);
        return;
    case N_HASARD:   /* les bornes, ou l'ensemble, puis HASARD (§ 3.6) */
        for (size_t k = 0; k < n->nb_enfants; k++) expression(c, n->enfants[k]);
        emettre(c, I_HASARD, n->forme, n->ligne, n->colonne);
        return;
    case N_ENSEMBLE:   /* les éléments, puis ENSEMBLE n (§ 13.9) */
        if (n->nb_enfants > 65535) { trop_grand(c, n); return; }
        for (size_t k = 0; k < n->nb_enfants; k++) expression(c, n->enfants[k]);
        emettre(c, I_ENSEMBLE, (long)n->nb_enfants, n->ligne, n->colonne);
        return;
    case N_AJOUT:
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        emettre(c, I_AJOUTER, n->op == 'p', n->ligne, n->colonne);
        return;
    case N_TAILLE:
        expression(c, n->enfants[0]);
        emettre(c, I_TAILLE_ENSEMBLE, 0, n->ligne, n->colonne);
        return;
    case N_JOINDRE:
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        emettre(c, I_JOINDRE, 0, n->ligne, n->colonne);
        return;
    case N_COLONNE:   /* la ligne, le titre, puis COLONNE (§ 15.5) */
        expression(c, n->enfants[1]);
        expression(c, n->enfants[0]);
        emettre(c, I_COLONNE, 0, n->ligne, n->colonne);
        return;
    case N_RESTE:   /* le dividende, le diviseur, puis RESTE (§ 3.5) */
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        emettre(c, I_RESTE, 0, n->ligne, n->colonne);
        return;
    case N_CONVERSION:   /* la valeur, puis CONVERTIR vers la cible (§ 4.5) */
        expression(c, n->enfants[0]);
        emettre(c, I_CONVERTIR, (long)n->entier, n->op_ligne, n->op_colonne);
        return;
    case N_ARRONDI:   /* la valeur, le pas, puis ARRONDIR avec le sens (§ 3.4) */
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        emettre(c, I_ARRONDIR, n->op == 'b' ? 1 : n->op == 'h' ? 2 : n->op == 'e' ? 3 : 0, n->ligne, n->colonne);
        return;
    case N_GROUPE:
        expression(c, n->enfants[0]);
        return;
    case N_BOOLEEN:
        constante(c, C_BOOLEEN, n->texte, n, n->ligne, n->colonne);
        return;
    case N_COMPARAISON:
        comparaison(c, n);
        return;
    case N_LOGIQUE:
        logique(c, n);
        return;
    case N_NEGATION:
        expression(c, n->enfants[0]);
        emettre(c, I_NEGATION, 0, n->ligne, n->colonne);
        return;
    case N_OPERATION: {
        expression(c, n->enfants[0]);
        if (n->enfants[1]->type == N_DUREE) {   /* « d + 3 mois », « d − 1 an » (§ 14.2) */
            const Noeud *d = n->enfants[1];
            expression(c, d->enfants[0]);
            if (n->op == '-') emettre(c, I_NEGATION, 0, n->op_ligne, n->op_colonne);
            emettre(c, I_DECALER, unite_duree(d), n->op_ligne, n->op_colonne);
            return;
        }
        expression(c, n->enfants[1]);
        CodeInstruction code =
            n->op == '+' ? I_ADDITION :
            n->op == '-' ? I_SOUSTRACTION :
            n->op == '*' ? I_MULTIPLICATION :
            n->op == '/' ? I_DIVISION : I_PUISSANCE;
        emettre(c, code, 0, n->op_ligne, n->op_colonne);   /* l'erreur pointera l'opérateur */
        return;
    }
    default:
        return;
    }
}

/* L'opérande de DÉCALER : 0 jours, 1 semaines, 2 mois, 3 années. */
static uint16_t unite_duree(const Noeud *d) {
    return d->op == 'j' ? 0 : d->op == 's' ? 1 : d->op == 'm' ? 2 : d->op == 'n' ? 4 : d->op == 'h' ? 5 : 3;
}

static void ajouter_saut(size_t **liste, size_t *nb, size_t pos) {
    size_t *t = grym_allouer((*nb + 1) * sizeof *t);
    if (*nb) memcpy(t, *liste, *nb * sizeof *t);
    free(*liste);
    *liste = t;
    (*liste)[(*nb)++] = pos;
}

static void entrer_boucle(Compilation *c, size_t suivant, int connu) {
    Boucle *t = grym_allouer((c->nb_boucles + 1) * sizeof *t);
    if (c->nb_boucles) memcpy(t, c->boucles, c->nb_boucles * sizeof *t);
    free(c->boucles);
    c->boucles = t;
    Boucle *b = &c->boucles[c->nb_boucles++];
    b->suivant = suivant;
    b->suivant_connu = connu;
    b->sorties = b->suivants = NULL;
    b->nb_sorties = b->nb_suivants = 0;
    b->essais = c->essais;
}

/* Quitter n essais sans échec : « Sortir de la boucle » ou « Rendre » depuis un bloc « Essayer » (§ 18). */
static void quitter_essais(Compilation *c, int n, const Noeud *ph) {
    for (int k = 0; k < n; k++) emettre(c, I_FIN_ESSAI, 0, ph->ligne, ph->colonne);
}

/* Fixe la cible des « Passer » en attente (tour suivant). */
static void cible_suivant(Compilation *c, size_t cible) {
    Boucle *b = &c->boucles[c->nb_boucles - 1];
    for (size_t k = 0; k < b->nb_suivants; k++) bloc_corriger_saut(c->b, b->suivants[k], cible);
    b->nb_suivants = 0;
    b->suivant = cible;
    b->suivant_connu = 1;
}

/* Termine la boucle : les « Sortir » sautent ici. */
static void sortir_boucle(Compilation *c) {
    Boucle *b = &c->boucles[--c->nb_boucles];
    for (size_t k = 0; k < b->nb_sorties; k++) bloc_corriger_saut(c->b, b->sorties[k], c->b->taille_code);
    free(b->sorties);
    free(b->suivants);
}

static void echouer_si(Compilation *c, const char *message, const Noeud *n) {
    long k = bloc_constante(c->b, C_TEXTE, message);
    if (k < 0) { trop_grand(c, n); return; }
    emettre(c, I_ECHOUER, k, n->ligne, n->colonne);
}

/* Selon : le sujet est rangé une fois ; chaque cas teste ses conditions dans l'ordre. */
static void condition_de_cas(Compilation *c, const Noeud *e, int case_sujet) {
    if (e->type == N_INTERVALLE) {
        /* Entre deux bornes, dans un ordre quelconque : (s ≥ a et s ≤ b) ou (s ≤ a et s ≥ b).
         * Les bornes peuvent être calculées deux fois : ce sont des expressions sans effet. */
        int l = e->ligne, col = e->colonne;
        emettre(c, I_LIRE_LOCAL, case_sujet, l, col);
        expression(c, e->enfants[0]);
        emettre(c, I_SUPERIEUR_OU_EGAL, 0, l, col);
        size_t autre1 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, case_sujet, l, col);
        expression(c, e->enfants[1]);
        emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
        size_t autre2 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        constante(c, C_BOOLEEN, "vrai", e, l, col);
        size_t fin1 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, autre1, c->b->taille_code);
        bloc_corriger_saut(c->b, autre2, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, case_sujet, l, col);
        expression(c, e->enfants[0]);
        emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
        size_t faux = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, case_sujet, l, col);
        expression(c, e->enfants[1]);
        emettre(c, I_SUPERIEUR_OU_EGAL, 0, l, col);
        size_t fin2 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, faux, c->b->taille_code);
        constante(c, C_BOOLEEN, "faux", e, l, col);
        bloc_corriger_saut(c->b, fin1, c->b->taille_code);
        bloc_corriger_saut(c->b, fin2, c->b->taille_code);
        return;
    }
    expression(c, e);   /* N_COMPARAISON dont le sujet est un N_SUJET */
}

static void phrase(Compilation *c, const Noeud *ph);

static void selon(Compilation *c, const Noeud *ph) {
    int cs = ph->entier;
    expression(c, ph->enfants[0]);
    emettre(c, I_ECRIRE_LOCAL, cs, ph->ligne, ph->colonne);
    size_t *vers_fin = NULL, nb_fin = 0;
    for (size_t k = 1; k < ph->nb_enfants && !c->echec; k++) {
        const Noeud *cas = ph->enfants[k];
        if (cas->type != N_CAS) continue;   /* remarques */
        const Noeud *corps = cas->enfants[cas->nb_enfants - 1];
        if (cas->forme == 1) {               /* Autrement */
            phrase(c, corps);
            continue;
        }
        size_t *vers_corps = NULL, nb_corps = 0;
        size_t vers_suivant = 0;
        size_t nb_cond = cas->nb_enfants - 1;
        for (size_t q = 0; q < nb_cond; q++) {
            condition_de_cas(c, cas->enfants[q], cs);
            size_t faux = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, cas->ligne, cas->colonne);
            if (q + 1 < nb_cond) {
                ajouter_saut(&vers_corps, &nb_corps, bloc_emettre_saut(c->b, I_SAUTER, cas->ligne, cas->colonne));
                bloc_corriger_saut(c->b, faux, c->b->taille_code);
            } else {
                vers_suivant = faux;
            }
        }
        for (size_t q = 0; q < nb_corps; q++) bloc_corriger_saut(c->b, vers_corps[q], c->b->taille_code);
        free(vers_corps);
        phrase(c, corps);
        ajouter_saut(&vers_fin, &nb_fin, bloc_emettre_saut(c->b, I_SAUTER, cas->ligne, cas->colonne));
        bloc_corriger_saut(c->b, vers_suivant, c->b->taille_code);
    }
    for (size_t q = 0; q < nb_fin; q++) bloc_corriger_saut(c->b, vers_fin[q], c->b->taille_code);
    free(vers_fin);
}

static void phrases(Compilation *c, Noeud *const *liste, size_t nb) {
    for (size_t i = 0; i < nb && !c->echec; i++) phrase(c, liste[i]);
}

static void phrase(Compilation *c, const Noeud *ph);

/* Un événement appelé hors de l'attente (ouverture, fermeture) : ESSAYER ; APPELER ; FIN_ESSAI ; → fin ;
 * raté : ÉCRAN_ERREUR ; fin. */
static void evenement_seul(Compilation *c, const Noeud *ph, const Noeud *q) {
    size_t vers_rate = bloc_emettre_saut(c->b, I_ESSAYER, ph->ligne, ph->colonne);
    long nom = bloc_nom(c->b, q->texte);
    if (nom < 0) { trop_grand(c, ph); return; }
    bloc_emettre_appel(c->b, (uint16_t)nom, 0, 0, ph->ligne, ph->colonne);
    verifier_regles(c, ph->ligne, ph->colonne);   /* dans l'essai : une règle violée n'annule que l'événement */
    emettre(c, I_FIN_ESSAI, 0, ph->ligne, ph->colonne);
    size_t vers_fin = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
    bloc_corriger_saut(c->b, vers_rate, c->b->taille_code);
    emettre(c, I_ECRAN_ERREUR, 0, ph->ligne, ph->colonne);
    bloc_corriger_saut(c->b, vers_fin, c->b->taille_code);
}

/* L'écran de ce nom, et l'événement d'un bouton ou d'une liste, dans tout le programme (§ 22). */
static const Noeud *trouver_ecran(Noeud *const *ph, size_t nb, const char *nom) {
    for (size_t k = 0; k < nb; k++) {
        if (ph[k]->type == P_UTILISER) {
            const Noeud *r = trouver_ecran(ph[k]->enfants, ph[k]->nb_enfants, nom);
            if (r) return r;
        }
        if (ph[k]->type == P_ECRAN && strcmp(ph[k]->texte, nom) == 0) return ph[k];
    }
    return NULL;
}

static const Noeud *trouver_quand(Noeud *const *ph, size_t nb, const char *ecran, int forme, const char *objet) {
    for (size_t k = 0; k < nb; k++) {
        if (ph[k]->type == P_UTILISER) {
            const Noeud *r = trouver_quand(ph[k]->enfants, ph[k]->nb_enfants, ecran, forme, objet);
            if (r) return r;
        }
        if (ph[k]->type == P_QUAND && ph[k]->forme == forme && strcmp(ph[k]->texte3, ecran) == 0
            && strcmp(ph[k]->enfants[2]->texte, objet) == 0) return ph[k];
    }
    return NULL;
}

/* Les zones de l'écran qu'un corps lit (« le nom de l'écran »), chacune une fois, séparées par U+001E (§ 22.6).
 * testees : 1 pour ne garder que celles dont il teste la présence (« le nom de l'écran est présent »). */
static void zones_lues_ou_testees(const Noeud *n, Chaine *c, int testees) {
    if (!n) return;
    const Noeud *z = NULL;   /* la zone lue ici */
    if (!testees && n->type == N_CHAMP && n->texte && n->nb_enfants == 1 && n->enfants[0] && n->enfants[0]->type == N_ECRAN)
        z = n;
    if (testees && n->type == N_COMPARAISON && (n->op == 'A' || n->op == 'R') && n->nb_enfants >= 1 && n->enfants[0]
        && n->enfants[0]->type == N_CHAMP && n->enfants[0]->texte && n->enfants[0]->nb_enfants == 1
        && n->enfants[0]->enfants[0] && n->enfants[0]->enfants[0]->type == N_ECRAN)
        z = n->enfants[0];
    if (z) {
        int deja = 0;
        for (const char *p = c->d ? c->d : ""; *p && !deja;) {
            const char *f = strchr(p, '\x1e');
            const size_t l = f ? (size_t)(f - p) : strlen(p);
            deja = l == strlen(z->texte) && strncmp(p, z->texte, l) == 0;
            p = f ? f + 1 : p + l;
        }
        if (!deja) {
            if (c->n) chaine_ajouter(c, "\x1e");
            chaine_ajouter(c, z->texte);
        }
    }
    for (size_t k = 0; k < n->nb_enfants; k++) zones_lues_ou_testees(n->enfants[k], c, testees);
}

/* Les zones qu'un événement exige d'emblée (§ 22.6) : celles qu'il lit, sauf celles dont il teste lui-même la présence ;
 * celles-là, il s'en charge, et les lire vides plus loin l'arrête encore, au moment de la lecture. */
static void zones_lues(const Noeud *n, Chaine *c) {
    Chaine lues = {0}, testees = {0};
    zones_lues_ou_testees(n, &lues, 0);
    zones_lues_ou_testees(n, &testees, 1);
    for (const char *p = lues.d ? lues.d : ""; *p;) {
        const char *f = strchr(p, '\x1e');
        const size_t l = f ? (size_t)(f - p) : strlen(p);
        int teste = 0;
        for (const char *q = testees.d ? testees.d : ""; *q && !teste;) {
            const char *g = strchr(q, '\x1e');
            const size_t m = g ? (size_t)(g - q) : strlen(q);
            teste = m == l && strncmp(p, q, l) == 0;
            q = g ? g + 1 : q + m;
        }
        if (!teste) {
            if (c->n) chaine_ajouter(c, "\x1e");
            char *zone = grym_formater("%.*s", (int)l, p);
            chaine_ajouter(c, zone);
            free(zone);
        }
        p = f ? f + 1 : p + l;
    }
    free(lues.d);
    free(testees.d);
}

/* La description d'un écran (ou d'un document, 2.25), constante de ÉCRAN_OUVRIR ; -1 si le programme est trop grand. */
static long description_ecran(Compilation *c, const Noeud *e, const Noeud *ph) {
    (void)ph;
    Chaine d = {0};
    chaine_ajouter(&d, e->texte);
    chaine_ajouter(&d, "\x1f");
    if (e->texte2) chaine_ajouter(&d, e->texte2);
    if (e->taille_titre) chaine_ajouter(&d, e->taille_titre > 0 ? "\x1dg" : "\x1dp");   /* la taille du titre (§ 22.5) */
    for (size_t k = 0; k < e->nb_enfants; k++) {
        const Noeud *el = e->enfants[k];
        if (el->type == N_DISPOSITION) {   /* « côte à côte » : H, « l'un sous l'autre » : V, fin du bloc : F */
            if (el->place_dite) {   /* un bloc placé dans un bloc libre : sa place devant sa lettre (§ 22.9) */
                char *pl = grym_formater("\x1f\x1d%d,%d,%d,%d\x1d", el->place[0], el->place[1],
                                         (el->place_dite & 2) ? el->place[2] : 0, (el->place_dite & 4) ? el->place[3] : 0);
                chaine_ajouter(&d, pl);
                free(pl);
                chaine_ajouter(&d, el->forme == 1 ? "H" : el->forme == 2 ? "V" : el->forme == 3 ? "G" : el->forme == 5 ? "O" : "P");
                if (el->forme == 3) chaine_ajouter(&d, el->texte);
                if (el->forme == 3 && (el->entier & 1)) chaine_ajouter(&d, "\x1e" "i");   /* 2.18 */
                continue;
            }
            chaine_ajouter(&d, el->forme == 1 ? "\x1f" "H" : el->forme == 2 ? "\x1f" "V" : el->forme == 3 ? "\x1f" "G"
                               : el->forme == 4 ? "\x1f" "P" : el->forme == 5 ? "\x1f" "O" : el->forme == 6 ? "\x1f" "Q"
                               : el->forme == 7 ? "\x1f" "M" : "\x1f" "F");   /* M : un menu (§ 22.12) */   /* P : en position libre (§ 22.9) ; O, Q : les onglets, un onglet (§ 22.10) */
            if (el->forme == 3 || el->forme == 6 || el->forme == 7) chaine_ajouter(&d, el->texte);   /* « Gadresse » : le titre (§ 22.7) */
            if (el->forme == 3 && (el->entier & 1)) chaine_ajouter(&d, "\x1e" "i");   /* « Gprix ␞ i » : en couleur inversée (2.18) */
            continue;
        }
        chaine_ajouter(&d, "\x1f");
        if (el->place_dite) {   /* « ␝96,16,240,0␝ » devant l'élément : sa place et sa taille, 0 si non dite (§ 22.9) */
            char *pl = grym_formater("\x1d%d,%d,%d,%d\x1d", el->place[0], el->place[1],
                                     (el->place_dite & 2) ? el->place[2] : 0, (el->place_dite & 4) ? el->place[3] : 0);
            chaine_ajouter(&d, pl);
            free(pl);
        }
        if (el->type == N_CHERCHER && el->graphique) {   /* « Rb+ » : un graphique, en barres ou en secteurs, compté ou additionné (§ 22.11) */
            /* « R », la forme et la période en une lettre (b s : sans ; c t : par mois ; d u : par semaine ; e v : par
             * année ; barres puis secteurs), « + » somme ou « n » nombre (§ 22.11) ; deux lettres seulement, pour
             * que l'ancienne lecture ne change pas */
            const int pe = (el->graphique & 8) ? 1 : (el->graphique & 16) ? 2 : (el->graphique & 32) ? 3 : 0;
            const char forme[2] = {"bcdestuv"[(el->graphique & 2 ? 4 : 0) + pe], 0};
            chaine_ajouter(&d, "R");
            chaine_ajouter(&d, forme);
            chaine_ajouter(&d, (el->graphique & 4) ? "+" : "n");
        } else
        chaine_ajouter(&d, el->type == N_CHERCHER ? "L" : el->type == N_BOUTON ? "B"
                         : el->type == N_NOM ? "Z" : el->type == N_IMAGE_ECRAN ? (el->texte ? "I" : "J") : "T");
        if (el->texte) chaine_ajouter(&d, el->texte);   /* « J » : une image calculée, sans fichier écrit (§ 22.8) */
        if (el->type == N_TEXTE_ECRAN && (el->entier & 2048)) chaine_ajouter(&d, "\x1e" "s");   /* en petites capitales (2.20) */
        if (el->type == N_TEXTE_ECRAN && (el->entier & 4096)) chaine_ajouter(&d, "\x1e" "q");   /* la section paiement (2.28) */
        if (el->type == N_BOUTON && (el->entier & (256 | 512 | 1024))) {   /* « Bclients ␞ pte ␝ description ␚ image » (2.14 à 2.17) */
            chaine_ajouter(&d, "\x1e");
            if (el->entier & 256) chaine_ajouter(&d, "p");
            if (el->entier & 512) chaine_ajouter(&d, "t");
            if (el->entier & 1024) chaine_ajouter(&d, "e");   /* à côté du titre, dans l'en-tête */
            if (el->indication) { chaine_ajouter(&d, "\x1d"); chaine_ajouter(&d, el->indication); }
            if (el->texte2) { chaine_ajouter(&d, "\x1a"); chaine_ajouter(&d, el->texte2); }
        }
        if (el->type == N_CHERCHER && el->texte3) {   /* colonnes choisies : « ␛ écrit ␝ a ␜ b ␞ … » */
            chaine_ajouter(&d, "\x1b");
            char *cols = grym_dupliquer(el->texte3);
            for (char *q = cols; *q; q++) if (*q == '\x1f') *q = '\x1c';
            chaine_ajouter(&d, cols);
            free(cols);
        }
        if (el->type == N_CHERCHER && !el->graphique && (el->entier & 2)) chaine_ajouter(&d, "\x15" "c");   /* en cartes (2.19) */
        if (el->type == N_NOM) {   /* « Zpays ␞ texte ␞ f » : le type, et « f » si facultative */
            chaine_ajouter(&d, "\x1e");
            chaine_ajouter(&d, el->texte2);
            chaine_ajouter(&d, (el->entier & 1) ? "\x1e" "f" : "\x1e" "-");
            if (el->entier & 2) chaine_ajouter(&d, "m");   /* sur plusieurs lignes (§ 22.6) */
            if (el->entier & 4) chaine_ajouter(&d, "s");   /* en lecture seule */
            if (el->entier & 8) chaine_ajouter(&d, "i");   /* en interrupteur */
            if (el->entier & 16) chaine_ajouter(&d, "p");  /* des choix multiples */
            if (el->entier & 32) chaine_ajouter(&d, "r");  /* avec recherche */
            if (el->entier & 64) chaine_ajouter(&d, "c");  /* masqué */
            if (el->borne_min || el->borne_max) {   /* « ␜ 1 ␛ 99 » : les bornes, l'une ou l'autre vide (§ 22.6) */
                chaine_ajouter(&d, "\x1c");
                if (el->borne_min) chaine_ajouter(&d, el->borne_min);
                chaine_ajouter(&d, "\x1b");
                if (el->borne_max) chaine_ajouter(&d, el->borne_max);
            }
            if (el->unite) {   /* « ␚ < CHF » : l'unité, « < » devant, « > » derrière (§ 22.6) */
                chaine_ajouter(&d, (el->entier & 128) ? "\x1a<" : "\x1a>");
                chaine_ajouter(&d, el->unite);
            }
            if (el->indication) {   /* « ␝ 4 chiffres » : après les lettres des options, qu'elle ne doit pas brouiller */
                chaine_ajouter(&d, "\x1d");
                chaine_ajouter(&d, el->indication);
            }
        }
    }
    char *desc = chaine_rendre(&d);
    long kd = bloc_constante(c->b, C_TEXTE, desc);
    free(desc);
    return kd;
}

/* Ce qu'un écran relit avant chaque attente, et qu'un document lit une fois (2.25) : listes, états, images, textes. */
static void relire_ecran(Compilation *c, const Noeud *e, const Noeud *ph) {
    (void)ph;
    for (size_t k = 0; k < e->nb_enfants; k++) {
        if (e->enfants[k]->type != N_CHERCHER) continue;
        expression(c, e->enfants[k]);
        emettre(c, I_ECRAN_LISTE, (long)k, ph->ligne, ph->colonne);
    }
    for (size_t k = 0; k < e->nb_enfants; k++)   /* « actif si », « visible si » : relus à chaque tour (§ 22.6) */
        for (size_t q = 0; q < e->enfants[k]->nb_enfants; q++) {
            const Noeud *si = e->enfants[k]->enfants[q];
            if (si->type != N_SI_ECRAN) continue;
            /* l'image calculée (§ 22.8) : ÉCRAN_IMAGE k × 2 + 1 ouvre la source (un objet absent la rend absente),
             * k × 2 la montre */
            if (si->forme == 4) {   /* le texte calculé (2.24) : sa valeur, relue à chaque tour */
                expression(c, si->enfants[0]);
                emettre(c, I_ECRAN_TEXTE, (long)k, si->ligne, si->colonne);
                continue;
            }
            if (si->forme == 3) emettre(c, I_ECRAN_IMAGE, (long)(k * 2 + 1), si->ligne, si->colonne);
            expression(c, si->enfants[0]);
            if (si->forme == 3) emettre(c, I_ECRAN_IMAGE, (long)(k * 2), si->ligne, si->colonne);
            else emettre(c, I_ECRAN_ETAT, (long)(k * 2 + (si->forme == 2)), si->ligne, si->colonne);
        }
}

/* Un document (2.25) : l'écran qu'il déclare, ouvert sans interface, lu une fois, puis rendu en PDF : empilé
 * (montrer = 0), ou montré dans un aperçu d'où l'on imprime (montrer = 1). */
static void document(Compilation *c, const Noeud *n, int montrer) {
    const Noeud *e = trouver_ecran(c->programme->phrases, c->programme->nb, n->texte);
    if (!e) { trop_grand(c, n); return; }
    const long kd = description_ecran(c, e, n);
    if (kd < 0) return;
    emettre(c, I_ECRAN_OUVRIR, kd, n->ligne, n->colonne);
    relire_ecran(c, e, n);
    emettre(c, I_DOCUMENT, montrer, n->ligne, n->colonne);
}

static void phrase(Compilation *c, const Noeud *ph) {
    switch (ph->type) {
    case P_CREATION:
    case P_MODIFICATION: {
        expression(c, ph->enfants[0]);
        if (ph->local >= 0) {
            emettre(c, I_ECRIRE_LOCAL, ph->local, ph->ligne, ph->colonne);
            return;
        }
        long k = bloc_nom(c->b, ph->texte);
        if (k < 0) { trop_grand(c, ph); return; }
        emettre(c, I_ECRIRE, k, ph->ligne, ph->colonne);
        return;
    }
    case P_FICHE:   /* l'objet, puis FICHE (§ 20) */
        expression(c, ph->enfants[0]);
        emettre(c, I_FICHE, 0, ph->ligne, ph->colonne);
        return;
    case P_RESAISIR:   /* l'objet, puis RESAISIR (§ 19) */
        expression(c, ph->enfants[0]);
        emettre(c, I_RESAISIR, 0, ph->ligne, ph->colonne);
        return;
    case P_ESSAYER: {
        /* ESSAYER → échec ; corps ; FIN_ESSAI ; → fin ; échec : motif → case ; bloc « En cas d'échec » ; fin (§ 18) */
        size_t vers_echec = bloc_emettre_saut(c->b, I_ESSAYER, ph->ligne, ph->colonne);
        c->essais++;
        phrase(c, ph->enfants[0]);
        c->essais--;
        emettre(c, I_FIN_ESSAI, 0, ph->ligne, ph->colonne);
        size_t vers_fin = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        bloc_corriger_saut(c->b, vers_echec, c->b->taille_code);
        emettre(c, I_ECRIRE_LOCAL, ph->entier, ph->ligne, ph->colonne);
        phrase(c, ph->enfants[1]);
        bloc_corriger_saut(c->b, vers_fin, c->b->taille_code);
        return;
    }
    case P_EXEMPLE: {   /* § 9.6 : ESSAYER → raté ; obtenu ; attendu ; EXEMPLE ; FIN_ESSAI ; → fin ; raté : EXEMPLE_RATÉ */
        if (!mode_exemples) return;   /* un exemple ne s'exécute qu'avec « grym exemples » */
        char *t = imprimer_phrase(c->programme, ph, 0);
        size_t l = strlen(t);
        while (l && (t[l - 1] == '\n' || t[l - 1] == '.')) t[--l] = '\0';
        const char *corps = strncmp(t, "Exemple : ", 10) == 0 ? t + 10 : t;
        char *texte = grym_formater("ligne %d : %s", ph->ligne, corps);
        free(t);
        long k = bloc_constante(c->b, C_TEXTE, texte);
        free(texte);
        if (k < 0) { trop_grand(c, ph); return; }
        size_t vers_rate = bloc_emettre_saut(c->b, I_ESSAYER, ph->ligne, ph->colonne);
        c->essais++;
        expression(c, ph->enfants[0]);
        expression(c, ph->enfants[1]);
        emettre(c, I_EXEMPLE, k, ph->ligne, ph->colonne);
        c->essais--;
        emettre(c, I_FIN_ESSAI, 0, ph->ligne, ph->colonne);
        size_t vers_fin = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        bloc_corriger_saut(c->b, vers_rate, c->b->taille_code);
        emettre(c, I_EXEMPLE_RATE, k, ph->ligne, ph->colonne);
        bloc_corriger_saut(c->b, vers_fin, c->b->taille_code);
        return;
    }
    case P_EFFACER:   /* « Effacer l'écran. » (§ 4.3) */
        emettre(c, I_EFFACER, 0, ph->ligne, ph->colonne);
        return;
    case P_STYLE:   /* « Les nombres s'affichent à la française. » (§ 4.1) */
        emettre(c, I_STYLE, ph->entier, ph->ligne, ph->colonne);
        return;
    case P_REFUSER:   /* « Refuser « … » puis x. » (§ 18.1) */
        if (ph->nb_enfants > 0xFFFF) { trop_grand(c, ph); return; }
        for (size_t e = 0; e < ph->nb_enfants; e++) expression(c, ph->enfants[e]);
        emettre(c, I_REFUSER, (long)ph->nb_enfants, ph->ligne, ph->colonne);
        return;
    case P_APPARENCE:   /* « Les écrans ont la couleur verte et le logo « … ». » (§ 22.5) : le logo lu, ou absent */
        if (ph->texte) {
            long k = bloc_constante(c->b, C_TEXTE, ph->texte);
            if (k < 0) { trop_grand(c, ph); return; }
            emettre(c, I_CONSTANTE, k, ph->ligne, ph->colonne);
            emettre(c, I_LIRE_FICHIER, 0, ph->ligne, ph->colonne);
        } else {
            emettre(c, I_ABSENT, 0, ph->ligne, ph->colonne);
        }
        if (ph->texte2 || ph->texte3 || ph->nb_enfants) {   /* 2.13 : la police, la couleur libre, le fond, ou absents ;
                                                                 2.16 : puis la signature, si elle est écrite (0x200) */
            const char *signature = apparence_reglage(ph, 1);
            const char *suite[4] = { apparence_reglage(ph, 0), ph->texte2, ph->texte3, signature };
            for (int r = 0; r < (signature ? 4 : 3); r++) {
                if (!suite[r]) { emettre(c, I_ABSENT, 0, ph->ligne, ph->colonne); continue; }
                long k = bloc_constante(c->b, C_TEXTE, suite[r]);
                if (k < 0) { trop_grand(c, ph); return; }
                emettre(c, I_CONSTANTE, k, ph->ligne, ph->colonne);
                if (r == 0 || r == 3) emettre(c, I_LIRE_FICHIER, 0, ph->ligne, ph->colonne);
            }
            emettre(c, I_APPARENCE, (ph->entier + 1) | 0x100 | (signature ? 0x200 : 0), ph->ligne, ph->colonne);
            return;
        }
        emettre(c, I_APPARENCE, ph->entier + 1, ph->ligne, ph->colonne);
        return;
    case P_AFFICHAGE:
        if (ph->nb_enfants > 0xFFFF) { trop_grand(c, ph); return; }
        for (size_t e = 0; e < ph->nb_enfants; e++) expression(c, ph->enfants[e]);
        emettre(c, ph->forme ? I_AFFICHER_SANS_LIGNE : I_AFFICHER, (long)ph->nb_enfants, ph->ligne, ph->colonne);
        return;
    case P_EXPRESSION:
        expression(c, ph->enfants[0]);
        emettre(c, I_AFFICHER, 1, ph->ligne, ph->colonne);
        return;
    case P_SI: {
        /* condition ; si faux → sinon ; alors ; → fin ; sinon ; fin */
        const Noeud *cond = ph->enfants[0];
        expression(c, cond);
        size_t vers_sinon = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, cond->ligne, cond->colonne);
        phrase(c, ph->enfants[1]);
        if (ph->nb_enfants > 2) {
            size_t vers_fin = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
            bloc_corriger_saut(c->b, vers_sinon, c->b->taille_code);
            phrase(c, ph->enfants[2]);
            bloc_corriger_saut(c->b, vers_fin, c->b->taille_code);
        } else {
            bloc_corriger_saut(c->b, vers_sinon, c->b->taille_code);
        }
        return;
    }
    case N_BLOC:
        phrases(c, ph->enfants, ph->nb_enfants);
        return;
    case P_RENDRE:
        expression(c, ph->enfants[0]);
        quitter_essais(c, c->essais, ph);
        emettre(c, I_RENDRE, 0, ph->ligne, ph->colonne);
        return;
    case P_APPEL:
        appel(c, ph, 0);
        return;
    case P_TANT_QUE: {
        /* test ; si faux → sortie ; corps ; → test */
        size_t test = c->b->taille_code;
        const Noeud *cond = ph->enfants[0];
        expression(c, cond);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, cond->ligne, cond->colonne);
        entrer_boucle(c, test, 1);
        phrase(c, ph->enfants[1]);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_REPETER: {
        /* reste ← n (entier ≥ 0) ; test : reste > 0 ? ; reste ← reste − 1 ; corps ; → test */
        int r = ph->entier, l = ph->ligne, col = ph->colonne;
        expression(c, ph->enfants[0]);
        emettre(c, I_EXIGER_ENTIER_NATUREL, 0, ph->enfants[0]->ligne, ph->enfants[0]->colonne);
        emettre(c, I_ECRIRE_LOCAL, r, l, col);
        size_t test = c->b->taille_code;
        emettre(c, I_LIRE_LOCAL, r, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_SUPERIEUR, 0, l, col);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, r, l, col);
        constante(c, C_NOMBRE, "1", ph, l, col);
        emettre(c, I_SOUSTRACTION, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, r, l, col);
        entrer_boucle(c, test, 1);
        phrase(c, ph->enfants[1]);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_POUR_CHAQUE: {
        int i = ph->local, f = ph->entier, p = ph->entier + 1, l = ph->ligne, col = ph->colonne;
        const Noeud *corps = ph->enfants[ph->nb_enfants - 1];
        if ((ph->forme & 1) && ph->enfants[2]->type == N_DUREE) {
            /* « par pas de 1 mois » (§ 14.2) : le compteur vaut départ + k unités, recalculé à chaque tour depuis le
             * départ ; sinon le 28 février d'un départ au 31 janvier se répéterait tous les mois suivants.
             *   départ ← début ; fin ← fin ; pas ← nombre ; compteur ← départ + pas unités (vérifie la date et le pas) ;
             *   pas = 0 ? ÉCHOUER ; compteur ← départ + 0 unité ; k ← 0 ;
             *   T: (pas > 0 ? compteur ≤ fin : compteur ≥ fin) sinon S ; corps ;
             *   P: k ← k + pas ; compteur ← départ + k unités ; SAUTER T ; S: */
            const Noeud *duree = ph->enfants[2];
            int s = ph->entier + 2, k = ph->entier + 3;
            uint16_t u = unite_duree(duree);
            expression(c, ph->enfants[0]);
            emettre(c, I_ECRIRE_LOCAL, s, l, col);
            expression(c, ph->enfants[1]);
            emettre(c, I_ECRIRE_LOCAL, f, l, col);
            expression(c, duree->enfants[0]);
            emettre(c, I_ECRIRE_LOCAL, p, l, col);
            emettre(c, I_LIRE_LOCAL, s, l, col);
            emettre(c, I_LIRE_LOCAL, p, l, col);
            emettre(c, I_DECALER, u, duree->op_ligne, duree->op_colonne);
            emettre(c, I_ECRIRE_LOCAL, i, l, col);
            emettre(c, I_LIRE_LOCAL, p, l, col);
            constante(c, C_NOMBRE, "0", ph, l, col);
            emettre(c, I_EGAL, 0, l, col);
            size_t non_nul = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
            echouer_si(c, "Pas nul : la boucle ne finirait jamais.", ph);
            bloc_corriger_saut(c->b, non_nul, c->b->taille_code);
            emettre(c, I_LIRE_LOCAL, s, l, col);
            constante(c, C_NOMBRE, "0", ph, l, col);
            emettre(c, I_DECALER, u, duree->op_ligne, duree->op_colonne);
            emettre(c, I_ECRIRE_LOCAL, i, l, col);
            constante(c, C_NOMBRE, "0", ph, l, col);
            emettre(c, I_ECRIRE_LOCAL, k, l, col);
            size_t test = c->b->taille_code;
            emettre(c, I_LIRE_LOCAL, p, l, col);
            constante(c, C_NOMBRE, "0", ph, l, col);
            emettre(c, I_SUPERIEUR, 0, l, col);
            size_t descendant = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
            emettre(c, I_LIRE_LOCAL, i, l, col);
            emettre(c, I_LIRE_LOCAL, f, l, col);
            emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
            size_t verdict = bloc_emettre_saut(c->b, I_SAUTER, l, col);
            bloc_corriger_saut(c->b, descendant, c->b->taille_code);
            emettre(c, I_LIRE_LOCAL, i, l, col);
            emettre(c, I_LIRE_LOCAL, f, l, col);
            emettre(c, I_SUPERIEUR_OU_EGAL, 0, l, col);
            bloc_corriger_saut(c->b, verdict, c->b->taille_code);
            size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
            entrer_boucle(c, 0, 0);
            phrase(c, corps);
            cible_suivant(c, c->b->taille_code);
            emettre(c, I_LIRE_LOCAL, k, l, col);
            emettre(c, I_LIRE_LOCAL, p, l, col);
            emettre(c, I_ADDITION, 0, l, col);
            emettre(c, I_ECRIRE_LOCAL, k, l, col);
            emettre(c, I_LIRE_LOCAL, s, l, col);
            emettre(c, I_LIRE_LOCAL, k, l, col);
            emettre(c, I_DECALER, u, duree->op_ligne, duree->op_colonne);
            emettre(c, I_ECRIRE_LOCAL, i, l, col);
            size_t retour = bloc_emettre_saut(c->b, I_SAUTER, l, col);
            bloc_corriger_saut(c->b, retour, test);
            bloc_corriger_saut(c->b, sortie, c->b->taille_code);
            sortir_boucle(c);
            return;
        }
        expression(c, ph->enfants[0]);
        emettre(c, I_ECRIRE_LOCAL, i, l, col);
        expression(c, ph->enfants[1]);
        emettre(c, I_ECRIRE_LOCAL, f, l, col);
        if (ph->forme & 1) {
            expression(c, ph->enfants[2]);
        } else {
            /* sens automatique : +1 si début ≤ fin, sinon −1 */
            emettre(c, I_LIRE_LOCAL, i, l, col);
            emettre(c, I_LIRE_LOCAL, f, l, col);
            emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
            size_t desc = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
            constante(c, C_NOMBRE, "1", ph, l, col);
            size_t apres = bloc_emettre_saut(c->b, I_SAUTER, l, col);
            bloc_corriger_saut(c->b, desc, c->b->taille_code);
            constante(c, C_NOMBRE, "-1", ph, l, col);
            bloc_corriger_saut(c->b, apres, c->b->taille_code);
        }
        emettre(c, I_ECRIRE_LOCAL, p, l, col);
        /* un pas nul ne finirait jamais */
        emettre(c, I_LIRE_LOCAL, p, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_EGAL, 0, l, col);
        size_t non_nul = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        echouer_si(c, "Pas nul : la boucle ne finirait jamais.", ph);
        bloc_corriger_saut(c->b, non_nul, c->b->taille_code);
        /* test : compteur ≤ fin (pas positif) ou compteur ≥ fin (pas négatif) */
        size_t test = c->b->taille_code;
        emettre(c, I_LIRE_LOCAL, p, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_SUPERIEUR, 0, l, col);
        size_t descendant = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, i, l, col);
        emettre(c, I_LIRE_LOCAL, f, l, col);
        emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
        size_t verdict = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, descendant, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, i, l, col);
        emettre(c, I_LIRE_LOCAL, f, l, col);
        emettre(c, I_SUPERIEUR_OU_EGAL, 0, l, col);
        bloc_corriger_saut(c->b, verdict, c->b->taille_code);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        entrer_boucle(c, 0, 0);
        phrase(c, corps);
        cible_suivant(c, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, i, l, col);
        emettre(c, I_LIRE_LOCAL, p, l, col);
        emettre(c, I_ADDITION, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, i, l, col);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_SORTIR:
    case P_PASSER: {
        Boucle *b = &c->boucles[c->nb_boucles - 1];
        quitter_essais(c, c->essais - b->essais, ph);
        size_t saut = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        if (ph->type == P_SORTIR) ajouter_saut(&b->sorties, &b->nb_sorties, saut);
        else if (b->suivant_connu) bloc_corriger_saut(c->b, saut, b->suivant);
        else ajouter_saut(&b->suivants, &b->nb_suivants, saut);
        return;
    }
    case P_SELON:
        selon(c, ph);
        return;
    case P_CLASSE:
    case P_APTITUDE: {
        ClasseModule *cm = module_ajouter_classe(c->module, ph->texte, ph->type == P_APTITUDE || (ph->forme & 3) == 2);
        cm->aptitude = ph->type == P_APTITUDE;
        cm->conserve = ph->type == P_CLASSE && (ph->forme & 32);
        cm->hors_fiches = ph->type == P_CLASSE && (ph->forme & 64);
        if (ph->type == P_CLASSE && ph->texte3) cm->pluriel = grym_dupliquer(ph->texte3);
        if (ph->type == P_CLASSE && ph->texte2) cm->parent = grym_dupliquer(ph->texte2);
        if (ph->type == P_CLASSE && ph->ancien) cm->ancien = grym_dupliquer(ph->ancien);   /* § 16.7 */
        for (size_t q = 0; q < ph->nb_enfants; q++) {
            const Noeud *ch = ph->enfants[q];
            if (ch->type == N_TEXTE) classe_ajouter_aptitude(cm, ch->texte);
            else {
                classe_ajouter_champ(cm, ch->texte);
                if (ch->texte3 && ch->forme != 3 && ch->texte2) {   /* valeurs permises : « texte␞à faire␞fini » (§ 16.15) */
                    /* « texte␞payée␞en retard␝vr » : les couleurs des étiquettes, une lettre par valeur (§ 16.15) */
                    char *t = grym_formater("%s\x1e%s%s%s", ch->texte2, ch->texte3, ch->couleurs ? "\x1d" : "", ch->couleurs ? ch->couleurs : "");
                    classe_typer_dernier_champ(cm, t, ch->op == 'U');
                    free(t);
                } else if (ch->transcrit_de) {   /* « texte␜mémo » : le champ (son) qu'il transcrit (§ 15.8) */
                    char *t = grym_formater("%s\x1c%s", ch->texte2, ch->transcrit_de);
                    classe_typer_dernier_champ(cm, t, ch->op == 'U');
                    free(t);
                } else classe_typer_dernier_champ(cm, ch->texte2, ch->op == 'U');
                if (ch->entier & 1) classe_facultatif_dernier_champ(cm);
                if (ch->entier & 2) classe_cascade_dernier_champ(cm);
                if (ch->forme == 3) classe_multiple_dernier_champ(cm);   /* champ multiple (§ 16.13) */
                if (ch->ancien) classe_ancien_dernier_champ(cm, ch->ancien);   /* « , anciennement « client » » (§ 16.7) */
                if (ch->nb_enfants) {   /* valeur de départ, sous forme canonique */
                    const Noeud *v = ch->enfants[0];
                    char *t = v->type == N_NEGATION ? grym_formater("-%s", v->enfants[0]->texte) : grym_dupliquer(v->texte);
                    classe_depart_dernier_champ(cm, t);
                    free(t);
                }
            }
        }
        return;
    }
    case P_POUR_CONSERVE: {
        /* liste figée au début de la boucle, parcourue par rang (§ 16.4) */
        int v = ph->local, liste = ph->entier, rang = ph->entier + 1, l = ph->ligne, col = ph->colonne;
        chercher(c, ph->enfants[0]);
        emettre(c, I_ECRIRE_LOCAL, liste, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_ECRIRE_LOCAL, rang, l, col);
        size_t test = c->b->taille_code;
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        emettre(c, I_LIRE_LOCAL, liste, l, col);
        emettre(c, I_TAILLE_LISTE, 0, l, col);
        emettre(c, I_INFERIEUR, 0, l, col);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, liste, l, col);
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        emettre(c, I_ELEMENT, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, v, l, col);
        entrer_boucle(c, 0, 0);
        phrase(c, ph->enfants[1]);
        cible_suivant(c, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        constante(c, C_NOMBRE, "1", ph, l, col);
        emettre(c, I_ADDITION, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, rang, l, col);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_POUR_TABLEAU: {   /* le tableau lu une fois, au début, puis parcouru par rang (§ 15.5) */
        int v = ph->local, tableau = ph->entier, rang = ph->entier + 1, l = ph->ligne, col = ph->colonne;
        expression(c, ph->enfants[0]);
        if (ph->op != 'p') {   /* « parmi les couleurs » (§ 13.9) : l'ensemble lui-même, figé par sa copie */
            emettre(c, I_LIRE_FICHIER, 0, l, col);
            emettre(c, I_TABLEAU, 0, l, col);
        }
        emettre(c, I_ECRIRE_LOCAL, tableau, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_ECRIRE_LOCAL, rang, l, col);
        size_t test = c->b->taille_code;
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        emettre(c, I_LIRE_LOCAL, tableau, l, col);
        emettre(c, I_TAILLE_LISTE, 0, l, col);
        emettre(c, I_INFERIEUR, 0, l, col);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, tableau, l, col);
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        emettre(c, I_ELEMENT, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, v, l, col);
        entrer_boucle(c, 0, 0);
        phrase(c, ph->enfants[1]);
        cible_suivant(c, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        constante(c, C_NOMBRE, "1", ph, l, col);
        emettre(c, I_ADDITION, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, rang, l, col);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_CONSERVER:
    case P_SUPPRIMER:
        expression(c, ph->enfants[0]);
        emettre(c, ph->type == P_CONSERVER ? I_CONSERVER : ph->entier == 2 ? I_RETABLIR
                   : ph->entier == 1 ? I_SUPPRIMER_DEFINITIVEMENT : I_SUPPRIMER, 0, ph->ligne, ph->colonne);
        return;
    case P_EXPORTER: {   /* la liste, le chemin, puis EXPORTER avec « entité ␛ colonnes » (§ 15.4) */
        const Noeud *el = ph->enfants[0];
        chercher(c, el);
        expression(c, ph->enfants[1]);
        /* « entité ␛ colonnes [␚ titre] [␙ totaux] » : le titre et les totaux d'un rapport (§ 15.6) */
        char *d = grym_formater("%s\x1b%s%s%s%s%s", el->texte, el->texte3 ? el->texte3 : "", ph->texte ? "\x1a" : "",
                                ph->texte ? ph->texte : "", ph->texte2 ? "\x19" : "", ph->texte2 ? ph->texte2 : "");
        long k = bloc_constante(c->b, C_TEXTE, d);
        free(d);
        if (k < 0) { trop_grand(c, ph); return; }
        emettre(c, I_EXPORTER, k, ph->ligne, ph->colonne);
        return;
    }
    case P_ENREGISTRER:
        expression(c, ph->enfants[0]);
        expression(c, ph->enfants[1]);
        emettre(c, I_ENREGISTRER, 0, ph->ligne, ph->colonne);
        return;
    case P_GAGNER: {   /* « Les genres de o gagnent baroque. » (§ 16.13) */
        expression(c, ph->enfants[0]);
        expression(c, ph->enfants[1]);
        long ch = bloc_nom(c->b, ph->texte);
        if (ch < 0) { trop_grand(c, ph); return; }
        emettre(c, ph->forme ? I_PERDRE : I_GAGNER, ch, ph->op_ligne, ph->op_colonne);
        return;
    }
    case P_MODIF_CHAMP: {
        expression(c, ph->enfants[0]);
        expression(c, ph->enfants[1]);
        long ch = bloc_nom(c->b, ph->texte);
        if (ch < 0) { trop_grand(c, ph); return; }
        emettre(c, I_ECRIRE_CHAMP, ch, ph->ligne, ph->colonne);
        return;
    }
    case P_CALCUL:
    case P_QUAND:   /* un événement d'écran se compile comme une action (grammaire, § 22.2) */
    case P_ACTION: {
        Chaine exige = {0};   /* un événement : les zones de l'écran que son corps lit, vérifiées d'un coup en tête (§ 22.6) */
        if (ph->type == P_QUAND) zones_lues(ph->enfants[1], &exige);
        /* Une formule se compile dans son propre bloc ; le programme n'en garde aucune trace. */
        Bloc *prec = c->b;
        Bloc *f = bloc_creer();
        f->nom = grym_dupliquer(ph->texte);
        f->classe = ph->texte2 ? grym_dupliquer(ph->texte2) : NULL;
        f->fichier = c->fichier ? grym_dupliquer(c->fichier) : NULL;
        f->sorte = ph->type == P_CALCUL ? B_CALCUL : B_ACTION;
        f->nb_parametres = (int)ph->enfants[0]->nb_enfants;
        f->nb_locaux = ph->entier;
        module_ajouter(c->module, f);
        c->b = f;
        Boucle *boucles = c->boucles;
        size_t nb_boucles = c->nb_boucles;
        int essais = c->essais;
        c->boucles = NULL;
        c->nb_boucles = 0;
        c->essais = 0;
        const Noeud *corps = ph->enfants[1];
        if (exige.n) {
            char *z = chaine_rendre(&exige);
            long kz = bloc_constante(c->b, C_TEXTE, z);
            free(z);
            emettre(c, I_ECRAN_EXIGER, kz, ph->ligne, ph->colonne);
        } else free(chaine_rendre(&exige));
        if (ph->type == P_CALCUL && ph->forme == 0) {
            expression(c, corps);
            emettre(c, I_RENDRE, 0, corps->ligne, corps->colonne);
        } else {
            phrase(c, corps);
            if (ph->type != P_CALCUL) emettre(c, I_RETOUR, 0, ph->ligne, 0);
        }
        free(c->boucles);
        c->boucles = boucles;
        c->nb_boucles = nb_boucles;
        c->essais = essais;
        c->b = prec;
        return;
    }
    case P_ECRAN: {   /* la classe de l'écran : ses zones, la ligne choisie de chaque liste (§ 22.1) ; « Ouvrir » la retrouve */
        char *nom = grym_formater("écran %s", ph->texte);
        ClasseModule *cm = module_ajouter_classe(c->module, nom, 0);
        free(nom);
        for (size_t k = 0; k < ph->nb_enfants; k++) {
            const Noeud *el = ph->enfants[k];
            if (el->type == N_NOM) {
                classe_ajouter_champ(cm, el->texte);
                if (el->texte3) {   /* valeurs permises d'une zone : « texte␞actif␞retiré » (§ 22.7) */
                    char *t = grym_formater("%s\x1e%s", el->texte2, el->texte3);
                    classe_typer_dernier_champ(cm, t, 0);
                    free(t);
                } else if (el->transcrit_de) {   /* « texte␜mémo » : transcrite d'une zone (son) (§ 15.8) */
                    char *t = grym_formater("%s\x1c%s", el->texte2, el->transcrit_de);
                    classe_typer_dernier_champ(cm, t, 0);
                    free(t);
                } else
                classe_typer_dernier_champ(cm, el->texte2, 0);
                if (el->entier & 1) classe_facultatif_dernier_champ(cm);
                if (el->entier & 16) classe_multiple_dernier_champ(cm);   /* « des genres (genre) » (§ 22.1) */
            } else if (el->type == N_CHERCHER && !el->graphique) {   /* un graphique ne se choisit pas (§ 22.11) */
                int fem = 0;   /* « le compositeur choisi », « la partition choisie » : le genre de l'entité */
                for (size_t q = 0; q < c->module->nb_classes; q++)
                    if (strcmp(c->module->classes[q].nom, el->texte) == 0) fem = c->module->classes[q].feminin;
                char *choisi = grym_formater("%s %s", el->texte, fem ? "choisie" : "choisi");
                classe_ajouter_champ(cm, choisi);
                classe_typer_dernier_champ(cm, el->texte, 0);
                classe_facultatif_dernier_champ(cm);
                free(choisi);
            }
        }
        return;
    }
    case P_FERMER:
        emettre(c, I_FERMER_ECRAN, 0, ph->ligne, ph->colonne);
        return;
    case P_OUVRIR: {
        if (ph->forme == 3) {   /* « Ouvrir le justificatif du frais. » (2.26) : le fichier, montré par le système */
            expression(c, ph->enfants[0]);
            emettre(c, I_DOCUMENT, 2, ph->ligne, ph->colonne);
            return;
        }
        if (ph->forme == 2) {   /* « Ouvrir le document de quittance. » (2.25) : l'aperçu, d'où l'on imprime */
            document(c, ph, 1);
            return;
        }
        if (ph->forme == 1) {   /* « Ouvrir la fiche de c. » (§ 22.3) */
            expression(c, ph->enfants[0]);
            verifier_regles(c, ph->ligne, ph->colonne);
            emettre(c, I_FICHE_ECRAN, 0, ph->ligne, ph->colonne);
            return;
        }
        /* ÉCRAN_OUVRIR ; T : listes (CHERCHER ; ÉCRAN_LISTE i) ; ÉCRAN_ATTENDRE → objet, code ;
         * code = 0 → S ; ESSAYER → raté ; aiguillage des événements (APPELER) ; FIN_ESSAI ; → T ;
         * raté : ÉCRAN_ERREUR ; → T ; S : ÉCRAN_FERMER. Chaque attente valide ce qui précède : un événement
         * est une transaction, et un événement raté n'écrit rien (grammaire, § 22.3 ; charte, art. 7). */
        const Noeud *e = trouver_ecran(c->programme->phrases, c->programme->nb, ph->texte);
        if (!e) { trop_grand(c, ph); return; }
        const long kd = description_ecran(c, e, ph);
        if (kd < 0) return;
        const int ev = ph->local, objet = ph->local + 1;
        verifier_regles(c, ph->ligne, ph->colonne);   /* « Ouvrir » valide ce qui précède */
        emettre(c, I_ECRAN_OUVRIR, kd, ph->ligne, ph->colonne);
        for (size_t k = 0; k < e->nb_enfants; k++) {   /* les valeurs de départ des zones */
            const Noeud *el = e->enfants[k];
            const int depart = el->type == N_NOM && el->nb_enfants && el->enfants[0]->type != N_SI_ECRAN;
            /* une case à cocher obligatoire montre « non » : elle vaut faux tant qu'on ne l'a pas cochée (§ 22.6) */
            const int case_vide = el->type == N_NOM && !depart && !(el->entier & 1) && strcmp(el->texte2, "vrai ou faux") == 0;
            if (!depart && !case_vide) continue;
            emettre(c, I_ECRAN_OBJET, 0, ph->ligne, ph->colonne);
            if (depart) expression(c, el->enfants[0]);
            else emettre(c, I_CONSTANTE, bloc_constante(c->b, C_BOOLEEN, "faux"), ph->ligne, ph->colonne);
            long nz = bloc_nom(c->b, el->texte);
            if (nz < 0) { trop_grand(c, ph); return; }
            emettre(c, I_ECRIRE_CHAMP, nz, ph->ligne, ph->colonne);
        }
        /* « Quand on ouvre » : une fois, avant la première attente, avec sa propre reprise */
        const Noeud *qo = trouver_quand(c->programme->phrases, c->programme->nb, e->texte, 4, "ouverture");
        if (qo) evenement_seul(c, ph, qo);
        size_t tour = c->b->taille_code;
        relire_ecran(c, e, ph);
        emettre(c, I_ECRAN_ATTENDRE, 0, ph->ligne, ph->colonne);
        emettre(c, I_ECRIRE_LOCAL, ev, ph->ligne, ph->colonne);
        emettre(c, I_ECRIRE_LOCAL, objet, ph->ligne, ph->colonne);
        emettre(c, I_LIRE_LOCAL, ev, ph->ligne, ph->colonne);
        emettre(c, I_CONSTANTE, bloc_constante(c->b, C_NOMBRE, "0"), ph->ligne, ph->colonne);
        emettre(c, I_EGAL, 0, ph->ligne, ph->colonne);
        size_t vers_suite = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, ph->ligne, ph->colonne);
        size_t vers_sortie = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        bloc_corriger_saut(c->b, vers_suite, c->b->taille_code);
        size_t vers_rate = bloc_emettre_saut(c->b, I_ESSAYER, ph->ligne, ph->colonne);
        size_t *vers_apres = grym_allouer((e->nb_enfants ? e->nb_enfants : 1) * sizeof *vers_apres);
        size_t nb_apres = 0;
        for (size_t k = 0; k < e->nb_enfants; k++) {
            const Noeud *el = e->enfants[k];
            if (el->type == N_TEXTE_ECRAN || el->type == N_DISPOSITION || el->type == N_IMAGE_ECRAN) continue;   /* sans événement */
            const Noeud *q = trouver_quand(c->programme->phrases, c->programme->nb, e->texte,
                                           el->type == N_BOUTON ? 1 : el->type == N_NOM ? 3 : 2, el->texte);
            if (!q) continue;   /* une liste sans « Quand on choisit » : le choix ne fait rien */
            char num[24];
            snprintf(num, sizeof num, "%lu", (unsigned long)(k + 1));
            emettre(c, I_LIRE_LOCAL, ev, ph->ligne, ph->colonne);
            emettre(c, I_CONSTANTE, bloc_constante(c->b, C_NOMBRE, num), ph->ligne, ph->colonne);
            emettre(c, I_EGAL, 0, ph->ligne, ph->colonne);
            size_t suivant = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, ph->ligne, ph->colonne);
            if (q->forme == 2) emettre(c, I_LIRE_LOCAL, objet, ph->ligne, ph->colonne);
            long nom = bloc_nom(c->b, q->texte);
            if (nom < 0) { free(vers_apres); trop_grand(c, ph); return; }
            bloc_emettre_appel(c->b, (uint16_t)nom, (uint8_t)(q->forme == 2 ? 1 : 0), 0, ph->ligne, ph->colonne);
            vers_apres[nb_apres++] = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
            bloc_corriger_saut(c->b, suivant, c->b->taille_code);
        }
        for (size_t k = 0; k < nb_apres; k++) bloc_corriger_saut(c->b, vers_apres[k], c->b->taille_code);
        free(vers_apres);
        verifier_regles(c, ph->ligne, ph->colonne);   /* dans l'essai : une règle violée n'annule que l'événement */
        emettre(c, I_FIN_ESSAI, 0, ph->ligne, ph->colonne);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        bloc_corriger_saut(c->b, retour, tour);
        bloc_corriger_saut(c->b, vers_rate, c->b->taille_code);
        emettre(c, I_ECRAN_ERREUR, 0, ph->ligne, ph->colonne);
        size_t retour2 = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        bloc_corriger_saut(c->b, retour2, tour);
        bloc_corriger_saut(c->b, vers_sortie, c->b->taille_code);
        /* « Quand on ferme » : à la fermeture, qui ne se refuse pas ; un échec se montre, et l'écran se ferme quand même */
        const Noeud *qf = trouver_quand(c->programme->phrases, c->programme->nb, e->texte, 5, "fermeture");
        if (qf) evenement_seul(c, ph, qf);
        emettre(c, I_ECRAN_FERMER, 0, ph->ligne, ph->colonne);
        return;
    }
    case P_REGLE: {   /* son calcul, sous le nom réservé qui correspond à son rang (§ 16.14) */
        size_t k = 0;
        while (k < c->nb_regles && c->regles[k] != ph) k++;
        Noeud *calcul = ph->enfants[0];
        free(calcul->texte);
        calcul->texte = nom_regle(k);
        phrase(c, calcul);
        return;
    }
    case P_UTILISER: {
        /* Les déclarations d'un fichier utilisé se compilent ici, une seule fois par projet (§ 21). */
        const char *prec = c->fichier;
        if (ph->entier > 0 && (size_t)ph->entier <= c->programme->nb_fichiers)
            c->fichier = c->programme->fichiers[ph->entier - 1];
        phrases(c, ph->enfants, ph->nb_enfants);
        c->fichier = prec;
        return;
    }
    default:   /* P_REMARQUE : rien à exécuter */
        return;
    }
}

void compilateur_mode_exemples(int actif) { mode_exemples = actif; }

Module *compiler(const Programme *p, Diagnostic *diag) {
    Module *m = module_creer();
    Compilation c = { bloc_creer(), m, diag, 0, NULL, 0, 0, p, NULL, NULL, NULL, 0 };
    recenser_regles(&c, p->phrases, p->nb);
    c.b->nb_locaux = p->nb_locaux;
    module_ajouter(m, c.b);
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;
    diag->fichier = NULL;
    diag->origine_ligne = diag->origine_colonne = 0;
    {   /* les sigles des noms (§ 2.4), avant toute phrase : les libellés les écriront en capitales */
        char *s = texte_sigles();
        if (*s) emettre(&c, I_SIGLES, bloc_constante(c.b, C_TEXTE, s), 0, 0);
        free(s);
    }
    if (mode_exemples) {   /* les déclarations et les exemples seulement : rien d'autre ne s'exécute (§ 9.6) */
        Noeud **garde = grym_allouer((p->nb ? p->nb : 1) * sizeof *garde);
        size_t n = 0;
        for (size_t k = 0; k < p->nb; k++) {
            TypeNoeud t = p->phrases[k]->type;
            if (t == P_CLASSE || t == P_APTITUDE || t == P_CALCUL || t == P_ACTION || t == P_REGLE || t == P_UTILISER
                || t == P_STYLE || t == P_EXEMPLE)
                garde[n++] = p->phrases[k];
        }
        phrases(&c, garde, n);
        free(garde);
    } else {
        phrases(&c, p->phrases, p->nb);
    }
    int ligne = p->nb ? p->phrases[p->nb - 1]->ligne : 0;
    c.b = m->blocs[0];
    if (!mode_exemples) verifier_regles(&c, ligne, 0);   /* la fin du programme valide tout (§ 16.14) */
    if (!mode_exemples && c.nb_regles && !c.echec) {
        /* les mêmes vérifications, en action sous un nom réservé : la machine l'appelle là où elle valide elle-même
         * une saisie, hors du code compilé (« Modifier » d'une fiche déduite, § 22.3 ; docs/vm.md, § 3.3) */
        Bloc *r = bloc_creer();
        r->nom = grym_dupliquer(NOM_REGLES);
        r->sorte = B_ACTION;
        module_ajouter(m, r);
        Bloc *garde = c.b;
        c.b = r;
        verifier_regles(&c, ligne, 0);
        emettre(&c, I_RETOUR, 0, ligne, 0);
        c.b = garde;
    }
    free(c.boucles);
    free(c.regles);
    for (size_t k = 0; k < c.nb_regles; k++) free(c.cles[k]);
    free(c.cles);
    if (c.echec) {
        module_detruire(m);
        return NULL;
    }
    bloc_emettre(m->blocs[0], I_RETOUR, 0, ligne, 0);
    return m;
}
