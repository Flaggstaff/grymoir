/* GrymoiR : lecture de la forme compacte, v0.2
 * Spécification : docs/grammaire.md (révision 1.37), § 11.
 *
 * Chaque instruction compacte est réécrite en la phrase littéraire équivalente,
 * jeton par jeton, en gardant les positions du fichier compact. L'analyseur
 * littéraire fait ensuite le reste : les deux formes partagent ainsi toutes
 * les vérifications (genre, pureté, portée) et produisent le même arbre.
 * Les blocs, fermés par « _fin », deviennent un retrait calculé (§ 5.4).
 */
#include "compact.h"
#include "texte.h"

#include <stdlib.h>
#include <string.h>

typedef enum { O_SI, O_BOUCLE, O_SELON, O_FORMULE, O_CLASSE, O_INIT, O_ESSAI, O_ECRAN, O_BLOC } Ouverture;

typedef struct {
    Ouverture type;
    int profondeur;        /* profondeur de la ligne qui ouvre */
    const Jeton *mot;      /* pour les messages */
    int dans_cas;          /* Selon : un « _cas » ou « _autrement » a déjà ouvert un corps */
    const Jeton *classe;   /* classe héritière : son nom et son article, pour la phrase des champs */
    const Jeton *article;
    int champs;            /* classe : champs déjà écrits */
} Niveau;

typedef struct {
    const Jeton *e;        /* jetons compacts, terminés par J_FIN */
    size_t n;
    Jeton *s;              /* jetons littéraires produits */
    size_t ns, cap;
    Niveau *pile;
    size_t np, capp;
    Diagnostic *diag;
    int echec;
} Reecriture;

/* ---------------------------------------------------------------- */
/* Production                                                       */
/* ---------------------------------------------------------------- */

static void emettre(Reecriture *r, TypeJeton type, const char *valeur, const Jeton *origine, int synthetique) {
    if (r->ns == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 128;
        Jeton *t = grym_allouer(r->cap * sizeof *t);
        if (r->ns) memcpy(t, r->s, r->ns * sizeof *t);
        free(r->s);
        r->s = t;
    }
    Jeton *j = &r->s[r->ns++];
    j->type = type;
    j->debut = origine->debut;
    j->longueur = origine->longueur;
    j->ligne = origine->ligne;
    j->colonne = origine->colonne;
    j->valeur = valeur ? grym_dupliquer(valeur) : NULL;
    j->retrait = origine->colonne;
    j->synthetique = synthetique;
    j->ligne_fin = origine->ligne;
    j->groupe = origine->groupe;
}

static void mot(Reecriture *r, const char *m, const Jeton *o) { emettre(r, J_MOT, m, o, 1); }

static void copier(Reecriture *r, const Jeton *t) {
    emettre(r, t->type, t->valeur, t, t->synthetique);
}

static void echouer(Reecriture *r, const Jeton *t, char *message) {
    if (r->echec) { free(message); return; }
    r->echec = 1;
    r->diag->message = message;
    r->diag->ligne = t->ligne;
    r->diag->colonne = t->colonne;
}

static int est_cle(const Jeton *t, const char *m) {
    return t->type == J_MOT_CLE && strcmp(t->valeur, m) == 0;
}

/* ---------------------------------------------------------------- */
/* Expressions                                                      */
/* ---------------------------------------------------------------- */

static void expression(Reecriture *r, size_t d, size_t f);

/* Jeton qui termine une valeur : un suffixe « _positif » s'y rapporte. */
static int finit_valeur(const Jeton *t) {
    return t->type == J_CROCHETS || t->type == J_NOMBRE || t->type == J_TEXTE || t->type == J_PAR_FERM
        || (t->type == J_MOT && (strcmp(t->valeur, "vrai") == 0 || strcmp(t->valeur, "faux") == 0 || strcmp(t->valeur, "écran") == 0))
        /* « _nombre_de client _conservé _nul » : réécrit, « … clients conservés », que le suffixe suit (§ 16.4) */
        || (t->type == J_MOT && (strcmp(t->valeur, "conservés") == 0 || strcmp(t->valeur, "supprimés") == 0));
}

static const char *adjectif(const Jeton *t) {
    static const char *const A[] = { "positif", "négatif", "nul", "vrai", "faux", "absent", "présent" };
    if (t->type != J_MOT_CLE) return NULL;
    for (size_t k = 0; k < 7; k++) if (strcmp(t->valeur, A[k]) == 0) return A[k];
    return NULL;
}

/* Tournure littéraire d'un comparateur : « > » → « supérieur à ». */
static void tournure(Reecriture *r, const Jeton *t) {
    switch (t->type) {
    case J_EGAL:      mot(r, "égal", t); mot(r, "à", t); break;
    case J_DIFFERENT: mot(r, "différent", t); mot(r, "de", t); break;
    case J_INFERIEUR: mot(r, "inférieur", t); mot(r, "à", t); break;
    case J_SUPERIEUR: mot(r, "supérieur", t); mot(r, "à", t); break;
    case J_INF_EGAL:  mot(r, "inférieur", t); mot(r, "ou", t); mot(r, "égal", t); mot(r, "à", t); break;
    case J_SUP_EGAL:  mot(r, "supérieur", t); mot(r, "ou", t); mot(r, "égal", t); mot(r, "à", t); break;
    default: break;
    }
}

static int est_comparateur(const Jeton *t) {
    return t->type >= J_EGAL && t->type <= J_SUP_EGAL;
}

/* Fin de la parenthèse ouverte en d (index de la fermante), ou f si absente. */
static size_t fermante(const Reecriture *r, size_t d, size_t f) {
    int p = 0;
    for (size_t k = d; k < f; k++) {
        if (r->e[k].type == J_PAR_OUV) p++;
        else if (r->e[k].type == J_PAR_FERM && --p == 0) return k;
    }
    return f;
}

/* Arguments « a ; b » entre d et f : chaque argument, séparé par « et » et précédé de « de ». */
static void arguments(Reecriture *r, size_t d, size_t f, int de, const Jeton *o) {
    size_t debut = d;
    int p = 0, premier = 1;
    for (size_t k = d; k <= f; k++) {
        if (k < f) {
            if (r->e[k].type == J_PAR_OUV) p++;
            else if (r->e[k].type == J_PAR_FERM) p--;
            if (!(p == 0 && r->e[k].type == J_POINT_VIRGULE)) continue;
        }
        if (k == debut) {
            if (k < f || !premier) echouer(r, k < f ? &r->e[k] : o, grym_dupliquer("Argument vide entre « ; »."));
            return;
        }
        if (!premier) mot(r, "et", &r->e[debut]);
        if (de) {
            /* « de (arg) » : parenthèses synthétiques, sans nœud de groupe dans l'arbre */
            mot(r, "de", &r->e[debut]);
            emettre(r, J_PAR_OUV, NULL, &r->e[debut], 1);
            expression(r, debut, k);
            emettre(r, J_PAR_FERM, NULL, &r->e[k < f ? k : f - 1], 1);
        } else {
            expression(r, debut, k);
        }
        premier = 0;
        debut = k + 1;
    }
}

/* Fin d'une valeur simple commençant en k : nom, appel f(…) ou parenthèse. */
static size_t fin_simple(const Reecriture *r, size_t k, size_t f) {
    if (r->e[k].type == J_PAR_OUV) {
        size_t fin = fermante(r, k, f);
        return fin == f ? f : fin + 1;
    }
    if (r->e[k].type == J_CROCHETS && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
        size_t fin = fermante(r, k + 1, f);
        return fin == f ? f : fin + 1;
    }
    return k + 1;
}

static void expression(Reecriture *r, size_t d, size_t f) {
    for (size_t k = d; k < f && !r->echec; k++) {
        const Jeton *t = &r->e[k];
        if (t->type == J_CROCHETS || t->type == J_PAR_OUV) {
            /* facture.client.nom → nom de client de facture (§ 13.3) */
            size_t b = fin_simple(r, k, f), q = b, nb = 0;
            while (q + 1 < f && r->e[q].type == J_POINT && r->e[q + 1].type == J_CROCHETS) { q += 2; nb++; }
            if (nb) {
                for (size_t i = q; i > b; i -= 2) {
                    copier(r, &r->e[i - 1]);
                    mot(r, "de", &r->e[i - 2]);
                }
                expression(r, k, b);
                k = q - 1;
                continue;
            }
        }
        if (est_cle(t, "écran") && k + 2 < f && r->e[k + 1].type == J_POINT && r->e[k + 2].type == J_CROCHETS) {
            /* _écran.pays → pays de l'écran (§ 22.2) */
            size_t q = k + 1;
            while (q + 1 < f && r->e[q].type == J_POINT && r->e[q + 1].type == J_CROCHETS) q += 2;
            for (size_t i = q; i > k + 1; i -= 2) {
                copier(r, &r->e[i - 1]);
                mot(r, "de", &r->e[i - 2]);
            }
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "écran", t);
            k = q - 1;
            continue;
        }
        if ((est_cle(t, "le") || est_cle(t, "la") || est_cle(t, "l'")) && k + 2 < f
            && r->e[k + 1].type == J_CROCHETS && (est_cle(&r->e[k + 2], "conservé") || est_cle(&r->e[k + 2], "conservée") || est_cle(&r->e[k + 2], "supprimé"))) {
            /* _le client _conservé → le client conservé (§ 16.4) */
            if (!strcmp(t->valeur, "l'")) emettre(r, J_ELISION, "l", t, 1);
            else mot(r, t->valeur, t);
            copier(r, &r->e[k + 1]);
            mot(r, est_cle(&r->e[k + 2], "supprimé") ? "supprimé" : "conservé", &r->e[k + 2]);
            k += 2;
            continue;
        }
        if ((est_cle(t, "somme_de") || est_cle(t, "moyenne_de") || est_cle(t, "plus_grand_de") || est_cle(t, "plus_petit_de"))
            && k + 4 < f && r->e[k + 1].type == J_CROCHETS && est_cle(&r->e[k + 2], "de")
            && r->e[k + 3].type == J_CROCHETS
            && (est_cle(&r->e[k + 4], "de") || est_cle(&r->e[k + 4], "conservé") || est_cle(&r->e[k + 4], "conservée")
                || est_cle(&r->e[k + 4], "supprimé"))) {
            /* _somme_de montant _de cotisation _conservé → la somme des montant des cotisation conservés (§ 16.4) ;
             * _moyenne_de, _plus_grand_de, _plus_petit_de : la moyenne des, le plus grand, le plus petit */
            if (est_cle(t, "somme_de") || est_cle(t, "moyenne_de")) {
                mot(r, "la", t);
                mot(r, est_cle(t, "somme_de") ? "somme" : "moyenne", t);
                mot(r, "des", t);
            } else {
                mot(r, "le", t);
                mot(r, "plus", t);
                mot(r, est_cle(t, "plus_grand_de") ? "grand" : "petit", t);
            }
            copier(r, &r->e[k + 1]);
            mot(r, "des", &r->e[k + 2]);
            copier(r, &r->e[k + 3]);
            if (est_cle(&r->e[k + 4], "de")) mot(r, "de", &r->e[k + 4]);
            else mot(r, est_cle(&r->e[k + 4], "supprimé") ? "supprimés" : "conservés", &r->e[k + 4]);
            k += 4;
            continue;
        }
        if (est_cle(t, "nombre_de") && k + 2 < f && r->e[k + 1].type == J_CROCHETS && est_cle(&r->e[k + 2], "de")) {
            /* _nombre_de œuvre _de bach → le nombre de œuvre de bach (§ 16.10) */
            mot(r, "le", t);
            mot(r, "nombre", t);
            mot(r, "de", t);
            copier(r, &r->e[k + 1]);
            mot(r, "de", &r->e[k + 2]);
            k += 2;
            continue;
        }
        if (est_cle(t, "nombre_de") && k + 2 < f && r->e[k + 1].type == J_CROCHETS
            && (est_cle(&r->e[k + 2], "conservé") || est_cle(&r->e[k + 2], "conservée") || est_cle(&r->e[k + 2], "supprimé"))) {
            /* _nombre_de client _conservé → le nombre de client conservés */
            mot(r, "le", t);
            mot(r, "nombre", t);
            mot(r, "de", t);
            copier(r, &r->e[k + 1]);
            mot(r, est_cle(&r->e[k + 2], "supprimé") ? "supprimés" : "conservés", &r->e[k + 2]);
            k += 2;
            continue;
        }
        if (est_cle(t, "au_plus_grand") || est_cle(t, "au_plus_petit")) {   /* _au_plus_grand solde → au plus grand solde */
            mot(r, "au", t);
            mot(r, "plus", t);
            mot(r, est_cle(t, "au_plus_grand") ? "grand" : "petit", t);
            continue;
        }
        if (est_cle(t, "comme") && k + 1 < f && r->e[k + 1].type == J_CROCHETS) {   /* _comme personne → comme un personne */
            mot(r, "comme", t);
            mot(r, "un", t);   /* synthétique : l'accord n'est pas vérifié */
            copier(r, &r->e[k + 1]);
            k++;
            continue;
        }
        if (est_cle(t, "nombre_de") && k + 1 < f && r->e[k + 1].type == J_CROCHETS
            && !(k + 2 < f && (est_cle(&r->e[k + 2], "de") || est_cle(&r->e[k + 2], "conservé")
                               || est_cle(&r->e[k + 2], "conservée") || est_cle(&r->e[k + 2], "supprimé")))) {
            mot(r, "le", t);   /* _nombre_de couleurs → le nombre de couleurs (§ 13.9) */
            mot(r, "nombre", t);
            mot(r, "de", t);
            copier(r, &r->e[k + 1]);
            k++;
            continue;
        }
        if (t->type == J_CROCHETS && k + 2 < f && est_cle(&r->e[k + 1], "jointes")) {
            mot(r, "les", t);   /* couleurs _jointes « , » → les couleurs jointes par « , » */
            copier(r, t);
            mot(r, "jointes", &r->e[k + 1]);
            mot(r, "par", &r->e[k + 1]);
            k++;
            continue;
        }
        if (est_cle(t, "dont")) {
            mot(r, "dont", t);
            continue;
        }
        if (t->type == J_CROCHETS && k + 1 < f && est_cle(&r->e[k + 1], "contient")) {
            /* genres _contient baroque → baroque est parmi les genres (§ 16.13) */
            size_t e = k + 2;
            int p = 0;
            while (e < f && !(p == 0 && r->e[e].type == J_MOT_CLE
                              && (!strcmp(r->e[e].valeur, "et") || !strcmp(r->e[e].valeur, "ou")))) {
                if (r->e[e].type == J_PAR_OUV) p++;
                else if (r->e[e].type == J_PAR_FERM) p--;
                e++;
            }
            expression(r, k + 2, e);
            mot(r, "est", &r->e[k + 1]);
            mot(r, "parmi", &r->e[k + 1]);
            mot(r, "les", &r->e[k + 1]);
            copier(r, t);
            k = e - 1;
            continue;
        }
        {                                            /* d + 3 _mois → d + 3 mois (§ 14.2) */
            static const char *const U[] = { "jour", "jours", "semaine", "semaines", "mois", "an", "ans", "année", "années",
                                             "minute", "minutes", "heure", "heures" };
            int unite = 0;
            for (size_t u = 0; u < sizeof U / sizeof *U; u++) if (est_cle(t, U[u])) unite = 1;
            if (unite) { mot(r, t->valeur, t); continue; }
        }
        if (est_cle(t, "sur")) {                    /* nom _sur 20 _droite → nom sur 20 à droite (§ 4.2) */
            mot(r, "sur", t);
            continue;
        }
        if (est_cle(t, "droite") || est_cle(t, "gauche")) {
            mot(r, "à", t);
            mot(r, est_cle(t, "droite") ? "droite" : "gauche", t);
            continue;
        }
        if (est_cle(t, "suivi")) {                  /* _suivi → suivi de (§ 4.4) */
            mot(r, "suivi", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "de") || est_cle(t, "que")) {   /* _de x, _que x : élision selon la valeur (§ 4.4) */
            mot(r, t->valeur, t);
            continue;
        }
        if (est_cle(t, "motif")) {                  /* _motif → le motif de l'échec (§ 18) */
            mot(r, "le", t);
            mot(r, "motif", t);
            mot(r, "de", t);
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "échec", t);
            continue;
        }
        if (est_cle(t, "réponse")) {                /* _réponse (nombre) « Âge ? » → la réponse en nombre à « Âge ? » */
            mot(r, "la", t);
            mot(r, "réponse", t);
            if (k + 2 < f && r->e[k + 1].type == J_PAR_OUV && r->e[k + 2].type == J_CROCHETS
                && k + 3 < f && r->e[k + 3].type == J_PAR_FERM) {
                mot(r, "en", t);
                for (const char *p = r->e[k + 2].valeur; p && *p; ) {
                    const char *e = strchr(p, ' ');
                    char *w = e ? grym_formater("%.*s", (int)(e - p), p) : grym_dupliquer(p);
                    mot(r, w, &r->e[k + 2]);
                    free(w);
                    p = e ? e + 1 : p + strlen(p);
                }
                k += 3;
            }
            mot(r, "à", t);
            continue;
        }
        if (est_cle(t, "colonne") && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* _colonne(« Nom » ; ligne) → la colonne (« Nom ») de (ligne) (§ 15.5) */
            size_t fin = fermante(r, k + 1, f);
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            size_t pv = 0;
            int p = 0, nb = 0;
            for (size_t q = k + 2; q < fin; q++) {
                if (r->e[q].type == J_PAR_OUV) p++;
                else if (r->e[q].type == J_PAR_FERM) p--;
                else if (p == 0 && r->e[q].type == J_POINT_VIRGULE) { pv = q; nb++; }
            }
            if (nb != 1 || pv == k + 2 || pv + 1 == fin) {
                echouer(r, t, grym_dupliquer("« _colonne » prend deux arguments, le titre et la ligne : _colonne(« Nom » ; ligne)."));
                return;
            }
            mot(r, "la", t);
            mot(r, "colonne", t);
            emettre(r, J_PAR_OUV, NULL, &r->e[k + 2], 1);
            expression(r, k + 2, pv);
            emettre(r, J_PAR_FERM, NULL, &r->e[pv], 1);
            mot(r, "de", &r->e[pv]);
            emettre(r, J_PAR_OUV, NULL, &r->e[pv + 1], 1);
            expression(r, pv + 1, fin);
            emettre(r, J_PAR_FERM, NULL, &r->e[fin], 1);
            k = fin;
            continue;
        }
        if ((est_cle(t, "écart_jours") || est_cle(t, "écart_semaines") || est_cle(t, "écart_mois") || est_cle(t, "écart_années")
             || est_cle(t, "écart_minutes") || est_cle(t, "écart_heures") || est_cle(t, "écart_ouvrables")
             || est_cle(t, "hasard") || est_cle(t, "hasard_parmi")) && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* _écart_mois(d1 ; d2) → le nombre de mois entre (d1) et (d2) (§ 14.6) ; _hasard(1 ; 6) → un nombre au hasard
             * de (1) à (6) ; _hasard_parmi(faces) → un élément au hasard parmi les faces (§ 3.6) */
            size_t fin = fermante(r, k + 1, f);
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            int parmi = est_cle(t, "hasard_parmi");
            size_t pv = 0;
            int p = 0, nb = 0;
            for (size_t q = k + 2; q < fin; q++) {
                if (r->e[q].type == J_PAR_OUV) p++;
                else if (r->e[q].type == J_PAR_FERM) p--;
                else if (p == 0 && r->e[q].type == J_POINT_VIRGULE) { pv = q; nb++; }
            }
            if (parmi ? (nb != 0 || fin != k + 3 || r->e[k + 2].type != J_CROCHETS) : (nb != 1 || pv == k + 2 || pv + 1 == fin)) {
                echouer(r, t, grym_formater(parmi ? "« _%s » prend un ensemble : _hasard_parmi(faces)."
                                                  : "« _%s » prend deux arguments séparés par « ; ».", t->valeur));
                return;
            }
            if (parmi) {
                mot(r, "un", t); mot(r, "élément", t); mot(r, "au", t); mot(r, "hasard", t); mot(r, "parmi", t); mot(r, "les", t);
                copier(r, &r->e[k + 2]);
                k = fin;
                continue;
            }
            if (est_cle(t, "hasard")) { mot(r, "un", t); mot(r, "nombre", t); mot(r, "au", t); mot(r, "hasard", t); mot(r, "de", t); }
            else {
                const char *u = t->valeur + strlen("écart_");
                mot(r, "le", t); mot(r, "nombre", t);
                if (!strcmp(u, "années") || !strcmp(u, "heures")) emettre(r, J_ELISION, "d", t, 1); else mot(r, "de", t);
                if (!strcmp(u, "ouvrables")) { mot(r, "jours", t); mot(r, "ouvrables", t); }   /* § 14.6 */
                else mot(r, u, t);
                mot(r, "entre", t);
            }
            emettre(r, J_PAR_OUV, NULL, &r->e[k + 2], 1);
            expression(r, k + 2, pv);
            emettre(r, J_PAR_FERM, NULL, &r->e[pv], 1);
            mot(r, est_cle(t, "hasard") ? "à" : "et", &r->e[pv]);
            emettre(r, J_PAR_OUV, NULL, &r->e[pv + 1], 1);
            expression(r, pv + 1, fin);
            emettre(r, J_PAR_FERM, NULL, &r->e[fin], 1);
            k = fin;
            continue;
        }
        if (est_cle(t, "reste") && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* _reste(17 ; 5) → le reste de (17) par (5), parenthèses synthétiques (§ 3.5) */
            size_t fin = fermante(r, k + 1, f);
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            size_t pv = 0;
            int p = 0, nb = 0;
            for (size_t q = k + 2; q < fin; q++) {
                if (r->e[q].type == J_PAR_OUV) p++;
                else if (r->e[q].type == J_PAR_FERM) p--;
                else if (p == 0 && r->e[q].type == J_POINT_VIRGULE) { pv = q; nb++; }
            }
            if (nb != 1 || pv == k + 2 || pv + 1 == fin) {
                echouer(r, t, grym_dupliquer("« _reste » prend deux arguments, le dividende et le diviseur : _reste(17 ; 5)."));
                return;
            }
            mot(r, "le", t);
            mot(r, "reste", t);
            mot(r, "de", t);
            emettre(r, J_PAR_OUV, NULL, &r->e[k + 2], 1);
            expression(r, k + 2, pv);
            emettre(r, J_PAR_FERM, NULL, &r->e[pv], 1);
            mot(r, "par", &r->e[pv]);
            emettre(r, J_PAR_OUV, NULL, &r->e[pv + 1], 1);
            expression(r, pv + 1, fin);
            emettre(r, J_PAR_FERM, NULL, &r->e[fin], 1);
            k = fin;
            continue;
        }
        if ((est_cle(t, "arrondi") || est_cle(t, "arrondi_bas") || est_cle(t, "arrondi_haut") || est_cle(t, "arrondi_pair"))
            && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* _arrondi_bas(x ; 0,05) → l'arrondi de (x) à (0,05) vers le bas, parenthèses synthétiques (§ 3.4) */
            size_t fin = fermante(r, k + 1, f);
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            size_t pv = 0;
            int p = 0, nb = 0;
            for (size_t q = k + 2; q < fin; q++) {
                if (r->e[q].type == J_PAR_OUV) p++;
                else if (r->e[q].type == J_PAR_FERM) p--;
                else if (p == 0 && r->e[q].type == J_POINT_VIRGULE) { pv = q; nb++; }
            }
            if (nb != 1 || pv == k + 2 || pv + 1 == fin) {
                echouer(r, t, grym_formater("« _%s » prend deux arguments, la valeur et le pas : _%s(x ; 0,05).",
                                            t->valeur, t->valeur));
                return;
            }
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "arrondi", t);
            mot(r, "de", t);
            emettre(r, J_PAR_OUV, NULL, &r->e[k + 2], 1);
            expression(r, k + 2, pv);
            emettre(r, J_PAR_FERM, NULL, &r->e[pv], 1);
            mot(r, "à", &r->e[pv]);
            emettre(r, J_PAR_OUV, NULL, &r->e[pv + 1], 1);
            expression(r, pv + 1, fin);
            emettre(r, J_PAR_FERM, NULL, &r->e[fin], 1);
            if (est_cle(t, "arrondi_bas") || est_cle(t, "arrondi_haut")) {
                mot(r, "vers", t);
                mot(r, "le", t);
                mot(r, est_cle(t, "arrondi_bas") ? "bas" : "haut", t);
            } else if (est_cle(t, "arrondi_pair")) {
                mot(r, "au", t);
                mot(r, "pair", t);
            }
            k = fin;
            continue;
        }
        if (t->type == J_MOT_CLE && strncmp(t->valeur, "en_", 3) == 0) {
            /* nom _en_majuscules → nom en majuscules ; code _en_nombre_entier → code en nombre entier (§ 4.5) */
            static const char *const C[] = { "majuscules", "minuscules", "nombre_entier", "nombre", "date", "année",
                                             "vrai_ou_faux", "heure", "toutes_lettres" };
            size_t q = 0;
            while (q < sizeof C / sizeof *C && strcmp(t->valeur + 3, C[q]) != 0) q++;
            if (q == sizeof C / sizeof *C) {
                echouer(r, t, grym_formater("« _%s » inconnu : _en_majuscules, _en_minuscules, _en_nombre, "
                                            "_en_nombre_entier, _en_date, _en_année ou _en_vrai_ou_faux.", t->valeur));
                return;
            }
            mot(r, "en", t);
            for (const char *p = C[q]; *p; ) {
                const char *e = strchr(p, '_');
                char *w = e ? grym_formater("%.*s", (int)(e - p), p) : grym_dupliquer(p);
                mot(r, w, t);
                free(w);
                p = e ? e + 1 : p + strlen(p);
            }
            continue;
        }
        if (est_cle(t, "correspond")) {   /* x _correspond y → x correspond à y (§ 16.1) */
            mot(r, "correspond", t);
            mot(r, "à", t);
            continue;
        }
        if (est_cle(t, "inclut") || est_cle(t, "commence") || est_cle(t, "finit")) {
            /* x _inclut y → x contient y ; _commence, _finit → commence par, finit par (§ 4.5) */
            mot(r, est_cle(t, "inclut") ? "contient" : t->valeur, t);
            if (!est_cle(t, "inclut")) mot(r, "par", t);
            continue;
        }
        if (est_cle(t, "fichier")) {                /* _fichier « a.jpg » → le fichier « a.jpg » */
            mot(r, "le", t);
            mot(r, "fichier", t);
            continue;
        }
        if (est_cle(t, "maintenant")) {              /* _maintenant → maintenant (§ 14.7) */
            mot(r, "maintenant", t);
            continue;
        }
        if (est_cle(t, "aujourd'hui")) {            /* _aujourd'hui → aujourd'hui */
            emettre(r, J_ELISION, "aujourd", t, 1);
            mot(r, "hui", t);
            continue;
        }
        if (est_cle(t, "nouveau")) {
            if (k + 1 >= f || r->e[k + 1].type != J_CROCHETS) {
                echouer(r, t, grym_dupliquer("Nom de classe attendu après « _nouveau »."));
                return;
            }
            mot(r, "un", t);
            mot(r, "nouveau", t);
            copier(r, &r->e[k + 1]);
            k++;
            if (k + 1 < f && est_cle(&r->e[k + 1], "saisi")) {   /* _saisi → saisi (accord non vérifié, § 19) */
                mot(r, "saisi", &r->e[k + 1]);
                k++;
            }
            continue;
        }
        if (t->type == J_CROCHETS && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* appel de calcul : carré(7) → carré de (7) */
            size_t fin = fermante(r, k + 1, f);
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            copier(r, t);
            arguments(r, k + 2, fin, 1, t);
            k = fin;
            continue;
        }
        if (t->type == J_MOT_CLE) {
            const char *adj = adjectif(t);
            if (strcmp(t->valeur, "et") == 0 || strcmp(t->valeur, "ou") == 0) {
                mot(r, t->valeur, t);
            } else if (adj && r->ns && finit_valeur(&r->s[r->ns - 1]) && k > d) {
                mot(r, "est", t);          /* x _positif → x est positif */
                mot(r, adj, t);
            } else if (strcmp(t->valeur, "vrai") == 0 || strcmp(t->valeur, "faux") == 0
                       || strcmp(t->valeur, "absent") == 0) {
                mot(r, t->valeur, t);      /* valeur booléenne, ou absente */
            } else if (strcmp(t->valeur, "non") == 0) {
                /* _non (x > 0) → x n'est pas supérieur à 0 */
                if (k + 1 >= f || r->e[k + 1].type != J_PAR_OUV) {
                    echouer(r, t, grym_dupliquer("« _non » s'applique à une comparaison entre parenthèses : "
                                                 "_non (x > 0)."));
                    return;
                }
                size_t fin = fermante(r, k + 1, f);
                size_t op = 0;
                int p = 0;
                for (size_t q = k + 2; q < fin; q++) {
                    if (r->e[q].type == J_PAR_OUV) p++;
                    else if (r->e[q].type == J_PAR_FERM) p--;
                    else if (p == 0 && (est_comparateur(&r->e[q]) || adjectif(&r->e[q]))) { op = q; break; }
                }
                if (!op) {   /* _non (nom _inclut « x ») → nom ne contient pas « x » (§ 4.5) */
                    size_t v = 0;
                    p = 0;
                    for (size_t q = k + 2; q < fin && !v; q++) {
                        if (r->e[q].type == J_PAR_OUV) p++;
                        else if (r->e[q].type == J_PAR_FERM) p--;
                        else if (p == 0 && (est_cle(&r->e[q], "inclut") || est_cle(&r->e[q], "commence")
                                            || est_cle(&r->e[q], "finit"))) v = q;
                    }
                    if (v) {
                        expression(r, k + 2, v);
                        mot(r, "ne", &r->e[v]);
                        mot(r, est_cle(&r->e[v], "inclut") ? "contient" : r->e[v].valeur, &r->e[v]);
                        mot(r, "pas", &r->e[v]);
                        if (!est_cle(&r->e[v], "inclut")) mot(r, "par", &r->e[v]);
                        expression(r, v + 1, fin);
                        k = fin;
                        continue;
                    }
                }
                if (!op && k + 3 < fin && r->e[k + 2].type == J_CROCHETS && est_cle(&r->e[k + 3], "contient")) {
                    /* _non (genres _contient baroque) → baroque n'est pas parmi les genres (§ 16.13) */
                    expression(r, k + 4, fin);
                    emettre(r, J_ELISION, "n", &r->e[k + 3], 1);
                    mot(r, "est", &r->e[k + 3]);
                    mot(r, "pas", &r->e[k + 3]);
                    mot(r, "parmi", &r->e[k + 3]);
                    mot(r, "les", &r->e[k + 3]);
                    copier(r, &r->e[k + 2]);
                    k = fin;
                    continue;
                }
                if (!op || fin == f) {
                    echouer(r, t, grym_dupliquer("« _non » s'applique à une comparaison : _non (x > 0)."));
                    return;
                }
                expression(r, k + 2, op);
                emettre(r, J_ELISION, "n", &r->e[op], 1);
                mot(r, "est", &r->e[op]);
                mot(r, "pas", &r->e[op]);
                if (adjectif(&r->e[op])) mot(r, adjectif(&r->e[op]), &r->e[op]);
                else { tournure(r, &r->e[op]); expression(r, op + 1, fin); }
                k = fin;
            } else {
                echouer(r, t, grym_formater("« _%s » inattendu dans une expression.", t->valeur));
                return;
            }
            continue;
        }
        if (t->type == J_AFFECTE || t->type == J_POINT_VIRGULE) {
            echouer(r, t, grym_formater("« %s » inattendu dans une expression.",
                                        t->type == J_AFFECTE ? "<<" : ";"));
            return;
        }
        copier(r, t);
    }
}

/* ---------------------------------------------------------------- */
/* Instructions                                                     */
/* ---------------------------------------------------------------- */

static void ouvrir(Reecriture *r, Ouverture type, int profondeur, const Jeton *mot_) {
    if (r->np == r->capp) {
        r->capp = r->capp ? r->capp * 2 : 16;
        Niveau *t = grym_allouer(r->capp * sizeof *t);
        if (r->np) memcpy(t, r->pile, r->np * sizeof *t);
        free(r->pile);
        r->pile = t;
    }
    r->pile[r->np].type = type;
    r->pile[r->np].profondeur = profondeur;
    r->pile[r->np].mot = mot_;
    r->pile[r->np].dans_cas = 0;
    r->pile[r->np].classe = NULL;
    r->pile[r->np].article = NULL;
    r->pile[r->np].champs = 0;
    r->np++;
}

/* Profondeur des phrases du bloc courant. */
static int profondeur_corps(const Reecriture *r) {
    if (!r->np) return 0;
    const Niveau *h = &r->pile[r->np - 1];
    return h->profondeur + (h->type == O_SELON ? 2 : 1);
}

/* Premier jeton émis pour une ligne : son retrait porte la structure des blocs. */
static void fixer_retrait(Reecriture *r, size_t premier, int profondeur) {
    if (premier < r->ns) r->s[premier].retrait = 1 + 4 * profondeur;
}

/* Index du premier mot-clé m entre d et f, au premier niveau de parenthèses, ou f. */
static size_t chercher(const Reecriture *r, size_t d, size_t f, const char *m) {
    int p = 0;
    for (size_t k = d; k < f; k++) {
        if (r->e[k].type == J_PAR_OUV) p++;
        else if (r->e[k].type == J_PAR_FERM) p--;
        else if (p == 0 && est_cle(&r->e[k], m)) return k;
    }
    return f;
}

static void point(Reecriture *r, size_t f) {
    emettre(r, J_POINT, NULL, &r->e[f - 1], 1);
}

/* Paramètres « _un x ; _une y » entre d et f. */
static void parametres(Reecriture *r, size_t d, size_t f, int de) {
    size_t k = d;
    int premier = 1;
    while (k < f && !r->echec) {
        const Jeton *t = &r->e[k];
        if (!est_cle(t, "un") && !est_cle(t, "une")) {
            echouer(r, t, grym_dupliquer("Paramètre attendu : « _un nombre » ou « _une remise »."));
            return;
        }
        if (k + 1 >= f || r->e[k + 1].type != J_CROCHETS) {
            echouer(r, t, grym_dupliquer("Nom de paramètre attendu après « _un »."));
            return;
        }
        if (!premier) mot(r, "et", t);
        if (de) emettre(r, J_ELISION, "d", t, 1);
        mot(r, t->valeur, t);
        copier(r, &r->e[k + 1]);
        premier = 0;
        k += 2;
        if (k < f) {
            if (r->e[k].type != J_POINT_VIRGULE) {
                echouer(r, &r->e[k], grym_dupliquer("« ; » attendu entre deux paramètres."));
                return;
            }
            k++;
        }
    }
}

/* Une condition de cas : « 1 », « _de 2 _à 3 », « > 10 », « _négatif ». */
static void condition_de_cas(Reecriture *r, size_t d, size_t f) {
    if (d >= f) { echouer(r, &r->e[d > 0 ? d - 1 : 0], grym_dupliquer("Condition de cas vide.")); return; }
    const Jeton *t = &r->e[d];
    if (est_cle(t, "de")) {
        size_t a = chercher(r, d + 1, f, "à");
        if (a == f) { echouer(r, t, grym_dupliquer("« _à » attendu : « _cas _de 2 _à 3 »."));  return; }
        mot(r, "de", t);
        expression(r, d + 1, a);
        mot(r, "à", &r->e[a]);
        expression(r, a + 1, f);
    } else if (est_comparateur(t)) {
        tournure(r, t);
        expression(r, d + 1, f);
    } else if (adjectif(t) && d + 1 == f) {
        mot(r, adjectif(t), t);
    } else {
        expression(r, d, f);
    }
}

/* Une instruction compacte, jetons d à f (exclus), à la profondeur donnée. */

static int est_etat(const Jeton *t) {
    return est_cle(t, "actif_si") || est_cle(t, "active_si") || est_cle(t, "visible_si") || est_cle(t, "plusieurs_lignes")
        || est_cle(t, "interrupteur") || est_cle(t, "indication") || est_cle(t, "recherche") || est_cle(t, "min") || est_cle(t, "max") || est_cle(t, "masqué") || est_cle(t, "unité")
        || est_cle(t, "lecture_seule") || est_cle(t, "place") || est_cle(t, "largeur") || est_cle(t, "hauteur");
}

/* Les états d'un élément d'écran, de k à f (§ 22.6) : « _plusieurs_lignes », « _lecture_seule » (zones),
 * « _actif_si cond », « _visible_si cond ». Rend 0 si l'élément est mal écrit (l'erreur est posée). */
/* La zone dont on lit les états est un texte : ses bornes se disent en caractères (§ 22.6). Posé par la zone. */
static int zone_de_texte = 0;

static int etats_compacts(Reecriture *r, size_t k, size_t f, int feminin, int zone) {
    while (k < f) {
        const Jeton *x = &r->e[k];
        if (est_cle(x, "place")) {   /* « _place 96 16 » → « , à 96 de la gauche et 16 du haut » (§ 22.9) */
            if (k + 2 >= f + 0 || r->e[k + 1].type != J_NOMBRE || r->e[k + 2].type != J_NOMBRE) {
                echouer(r, x, grym_dupliquer("Deux nombres attendus : « _place 96 16 »."));
                return 0;
            }
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "à", x);
            copier(r, &r->e[k + 1]);
            mot(r, "de", x); mot(r, "la", x); mot(r, "gauche", x); mot(r, "et", x);
            copier(r, &r->e[k + 2]);
            mot(r, "du", x); mot(r, "haut", x);
            k += 3;
        } else if (est_cle(x, "largeur") || est_cle(x, "hauteur")) {   /* « _largeur 240 » → « , large de 240 » */
            if (k + 1 >= f || r->e[k + 1].type != J_NOMBRE) {
                echouer(r, x, grym_formater("Nombre de pixels attendu : « _%s 80 ».", x->valeur));
                return 0;
            }
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, est_cle(x, "largeur") ? "large" : feminin ? "haute" : "haut", x);
            mot(r, "de", x);
            copier(r, &r->e[k + 1]);
            k += 2;
        } else if (zone && est_cle(x, "plusieurs_lignes")) {
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "sur", x); mot(r, "plusieurs", x); mot(r, "lignes", x);
            k++;
        } else if (est_cle(x, "unité") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            /* _unité « CHF » [_devant] → , avec l'unité « CHF » [devant] (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "avec", x);
            emettre(r, J_ELISION, "l", x, 1);
            mot(r, "unité", x);
            copier(r, &r->e[k + 1]);
            k += 2;
            if (k < f && est_cle(&r->e[k], "devant")) { mot(r, "devant", &r->e[k]); k++; }
        } else if (est_cle(x, "indication") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            /* _indication « … » → , avec l'indication « … » (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "avec", x);
            emettre(r, J_ELISION, "l", x, 1);
            mot(r, "indication", x);
            copier(r, &r->e[k + 1]);
            k += 2;
        } else if (zone && (est_cle(x, "min") || est_cle(x, "max")) && k + 1 < f) {
            /* _min 1 → , au moins 1 ; _max 30 → , au plus 30 ; « caractères » pour un texte (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "au", x);
            mot(r, est_cle(x, "min") ? "moins" : "plus", x);
            size_t q = k + 1;
            if (r->e[q].type == J_MOINS && q + 1 < f) copier(r, &r->e[q++]);
            copier(r, &r->e[q]);
            if (zone_de_texte) mot(r, "caractères", x);
            k = q + 1;
        } else if (zone && est_cle(x, "masqué")) {   /* _masqué → , masqué / masquée (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, feminin ? "masquée" : "masqué", x);
            k++;
        } else if (zone && est_cle(x, "recherche")) {   /* _recherche → , avec recherche (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "avec", x);
            mot(r, "recherche", x);
            k++;
        } else if (zone && est_cle(x, "interrupteur")) {   /* _interrupteur → , en interrupteur (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "en", x);
            mot(r, "interrupteur", x);
            k++;
        } else if (zone && est_cle(x, "lecture_seule")) {
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "en", x); mot(r, "lecture", x); mot(r, "seule", x);
            k++;
        } else if (est_cle(x, "actif_si") || est_cle(x, "active_si") || est_cle(x, "visible_si")) {
            size_t g = k + 1;
            while (g < f && !est_etat(&r->e[g])) g++;
            if (g == k + 1) {
                echouer(r, x, grym_dupliquer("Condition attendue après « _actif_si » ou « _visible_si »."));
                return 0;
            }
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, est_cle(x, "visible_si") ? "visible" : feminin ? "active" : "actif", x);
            mot(r, "si", x);
            expression(r, k + 1, g);
            k = g;
        } else {
            echouer(r, x, grym_dupliquer(zone ? "« _départ », « _facultatif », « _plusieurs_lignes », « _lecture_seule », "
                                                "« _place », « _largeur », « _hauteur », « _actif_si » ou « _visible_si » "
                                                "attendu après une zone."
                                              : "« _place », « _largeur », « _hauteur », « _actif_si » ou « _visible_si » "
                                                "attendu après un élément."));
            return 0;
        }
    }
    return 1;
}

static void instruction(Reecriture *r, size_t d, size_t f) {
    const Jeton *t = &r->e[d];
    size_t premier = r->ns;
    int prof = profondeur_corps(r);
    Niveau *haut = r->np ? &r->pile[r->np - 1] : NULL;

    if (t->type == J_REMARQUE) {
        copier(r, t);
        fixer_retrait(r, premier, prof);
        return;
    }
    if (t->type == J_MOT_CLE && !strcmp(t->valeur, "fin") && haut && haut->type == O_CLASSE && f == d + 1) {
        if (haut->classe && !haut->champs) {   /* héritière sans champ propre */
            if (r->ns && r->s[r->ns - 1].ligne_fin < t->ligne) r->s[r->ns - 1].ligne_fin = t->ligne;
            r->np--;
            return;
        }
        /* dernier champ : la virgule devient le point final */
        if (!r->ns || r->s[r->ns - 1].type != J_VIRGULE) {
            echouer(r, t, grym_dupliquer("Une classe déclare au moins un champ : « _un nom »."));
            return;
        }
        r->s[r->ns - 1].type = J_POINT;
        if (r->s[r->ns - 1].ligne_fin < t->ligne) r->s[r->ns - 1].ligne_fin = t->ligne;
        r->np--;
        return;
    }
    /* Écrans (grammaire, § 22 ; docs/ecrans.md, § 3.7) */
    if (t->type == J_MOT_CLE && !strcmp(t->valeur, "fin") && haut && haut->type == O_ECRAN && f == d + 1) {
        if (!r->ns || r->s[r->ns - 1].type != J_VIRGULE) {
            echouer(r, t, grym_dupliquer("Un écran montre au moins un élément : « _bouton « OK » »."));
            return;
        }
        r->s[r->ns - 1].type = J_POINT;   /* le dernier élément finit la phrase */
        if (r->s[r->ns - 1].ligne_fin < t->ligne) r->s[r->ns - 1].ligne_fin = t->ligne;
        r->np--;
        return;
    }
    if (t->type == J_MOT_CLE && !strcmp(t->valeur, "fin") && haut && haut->type == O_BLOC && f == d + 1) {
        r->np--;   /* la fin d'un bloc : l'indentation suffit en forme littéraire */
        return;
    }
    /* l'en-tête d'un bloc, puis sa place éventuelle (§ 22.9) : « _groupe « Adresse » _place 16 8 _largeur 320 » */
    const size_t long_en_tete = (est_cle(t, "l'") || est_cle(t, "groupe") || est_cle(t, "onglet") || est_cle(t, "menu")) ? 2 : 1;
    const size_t fin_en_tete = d + long_en_tete;
    const int place_bloc = f > fin_en_tete && fin_en_tete < f && est_etat(&r->e[fin_en_tete]);
    const size_t fb = place_bloc ? fin_en_tete : f;
    const int l_un = est_cle(t, "l'") && fb == d + 2 && r->e[d + 1].type == J_CROCHETS
                     && strcmp(r->e[d + 1].valeur, "un sous l'autre") == 0;   /* « _l'un_sous_l'autre » : « _l' » puis un nom */
    const int groupe = est_cle(t, "groupe") && fb == d + 2 && r->e[d + 1].type == J_TEXTE;   /* « _groupe « Adresse » » */
    const int menu = est_cle(t, "menu") && fb == d + 2 && r->e[d + 1].type == J_TEXTE;      /* « _menu « Membres » » (§ 22.12) */
    if (haut && (haut->type == O_ECRAN || haut->type == O_BLOC)
        && ((est_cle(t, "côte_à_côte") && fb == d + 1) || l_un || groupe || menu || (est_cle(t, "position_libre") && fb == d + 1)
            || (est_cle(t, "onglets") && fb == d + 1) || (est_cle(t, "onglet") && fb == d + 2 && r->e[d + 1].type == J_TEXTE))) {
        /* _côte_à_côte → côte à côte : ; _l'un_sous_l'autre → l'un sous l'autre : (§ 22.1) ;
         * _groupe « Adresse » → le groupe « Adresse » : (§ 22.7) */
        if (groupe || menu) {
            mot(r, "le", t);
            mot(r, menu ? "menu" : "groupe", t);
            copier(r, &r->e[d + 1]);
        } else if (est_cle(t, "onglets")) {   /* _onglets → les onglets : (§ 22.10) */
            mot(r, "les", t);
            mot(r, "onglets", t);
        } else if (est_cle(t, "onglet")) {   /* _onglet « Identité » → l'onglet « Identité » : */
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "onglet", t);
            copier(r, &r->e[d + 1]);
        } else if (est_cle(t, "position_libre")) {   /* _position_libre → en position libre : (§ 22.9) */
            mot(r, "en", t);
            mot(r, "position", t);
            mot(r, "libre", t);
        } else if (est_cle(t, "côte_à_côte")) {
            mot(r, "côte", t);
            mot(r, "à", t);
            mot(r, "côte", t);
        } else {
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "un", t);
            mot(r, "sous", t);
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "autre", t);
        }
        if (place_bloc && !etats_compacts(r, fb, f, 0, 0)) return;
        emettre(r, J_DEUX_POINTS, NULL, t, 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_BLOC, prof, t);
        return;
    }
    if (haut && (haut->type == O_ECRAN || haut->type == O_BLOC)) {
        if (est_cle(t, "image_de") && f >= d + 2) {   /* _image_de x → l'image de x (§ 22.8) */
            size_t g = d + 1;
            while (g < f && !est_etat(&r->e[g])) g++;
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "image", t);
            mot(r, "de", t);
            expression(r, d + 1, g);
            if (!etats_compacts(r, g, f, 1, 0)) return;
        } else if (est_cle(t, "image") && f >= d + 2 && r->e[d + 1].type == J_TEXTE && (f == d + 2 || est_etat(&r->e[d + 2]))) {
            emettre(r, J_ELISION, "l", t, 1);   /* _image « plan.png » → l'image « plan.png » (§ 22.8) */
            mot(r, "image", t);
            copier(r, &r->e[d + 1]);
            if (!etats_compacts(r, d + 2, f, 1, 0)) return;
        } else if ((est_cle(t, "bouton") || est_cle(t, "texte")) && f >= d + 2 && r->e[d + 1].type == J_TEXTE
            && (f == d + 2 || est_etat(&r->e[d + 2]))) {
            mot(r, est_cle(t, "bouton") ? "un" : "le", t);
            mot(r, est_cle(t, "bouton") ? "bouton" : "texte", t);
            copier(r, &r->e[d + 1]);
            if (!etats_compacts(r, d + 2, f, 0, 0)) return;
        } else if (est_cle(t, "graphique") && d + 2 < f && r->e[d + 1].type == J_CROCHETS
                   && (est_cle(&r->e[d + 2], "conservé") || est_cle(&r->e[d + 2], "conservée"))) {
            /* _graphique tâche _conservé [_dont …] _par état [_somme montant] [_secteurs] [_place …] (§ 22.11) */
            size_t fin_places = f;
            for (size_t q = d + 3; q < f; q++)
                if (est_cle(&r->e[q], "place") || est_cle(&r->e[q], "largeur") || est_cle(&r->e[q], "hauteur")) { f = q; break; }
            const size_t places = f;
            const size_t secteurs = chercher(r, d + 3, f, "secteurs");
            const size_t somme = chercher(r, d + 3, secteurs, "somme");
            const size_t par = chercher(r, d + 3, somme, "par");
            mot(r, "le", t);
            mot(r, "graphique", t);
            mot(r, "des", t);
            copier(r, &r->e[d + 1]);
            mot(r, "conservés", &r->e[d + 2]);
            if (d + 3 < par) {
                if (!est_cle(&r->e[d + 3], "dont")) {
                    echouer(r, &r->e[d + 3], grym_dupliquer("« _dont » ou « _par » attendu après le graphique."));
                    return;
                }
                mot(r, "dont", &r->e[d + 3]);
                expression(r, d + 4, par);
            }
            if (par >= somme || par + 1 >= somme) {
                echouer(r, par < f ? &r->e[par] : t, grym_dupliquer("Regroupement attendu : « _par état »."));
                return;
            }
            emettre(r, J_VIRGULE, NULL, &r->e[par], 1);
            mot(r, "par", &r->e[par]);
            size_t debut_cle = par + 1;
            if (debut_cle + 1 < somme && (est_cle(&r->e[debut_cle], "mois") || est_cle(&r->e[debut_cle], "semaine")
                                          || est_cle(&r->e[debut_cle], "année"))) {
                /* _par _mois échéance → par mois de échéance (§ 22.11) */
                mot(r, r->e[debut_cle].valeur, &r->e[debut_cle]);
                mot(r, "de", &r->e[debut_cle]);
                debut_cle++;
            }
            expression(r, debut_cle, somme);
            if (somme < secteurs) {
                if (somme + 1 >= secteurs) { echouer(r, &r->e[somme], grym_dupliquer("Champ attendu : « _somme montant ».")); return; }
                emettre(r, J_VIRGULE, NULL, &r->e[somme], 1);
                mot(r, "avec", &r->e[somme]);
                mot(r, "la", &r->e[somme]);
                mot(r, "somme", &r->e[somme]);
                mot(r, "de", &r->e[somme]);
                expression(r, somme + 1, secteurs);
            }
            if (secteurs < f) {
                emettre(r, J_VIRGULE, NULL, &r->e[secteurs], 1);
                mot(r, "en", &r->e[secteurs]);
                mot(r, "secteurs", &r->e[secteurs]);
            }
            if (!etats_compacts(r, places, fin_places, 1, 0)) return;
            f = fin_places;
        } else if (est_cle(t, "liste") && d + 2 < f && r->e[d + 1].type == J_CROCHETS
                   && (est_cle(&r->e[d + 2], "conservé") || est_cle(&r->e[d + 2], "conservée")
                       || est_cle(&r->e[d + 2], "supprimé") || est_cle(&r->e[d + 2], "supprimée"))) {
            /* _liste compositeur _conservé [_dont …] [_par nom [_décroissant]] [_avec titre ; compositeur.nom]
             * [_place 96 16] [_largeur 240] [_hauteur 80] (§ 22.9) */
            size_t fin_places = f;
            for (size_t q = d + 3; q < f; q++)
                if (est_cle(&r->e[q], "place") || est_cle(&r->e[q], "largeur") || est_cle(&r->e[q], "hauteur")) { f = q; break; }
            const size_t places = f;
            size_t avec = chercher(r, d + 3, f, "avec");
            size_t fin_liste = f;
            f = avec;
            size_t par = chercher(r, d + 3, f, "par");
            mot(r, "la", t);
            mot(r, "liste", t);
            mot(r, "des", t);
            copier(r, &r->e[d + 1]);
            mot(r, est_cle(&r->e[d + 2], "supprimé") || est_cle(&r->e[d + 2], "supprimée") ? "supprimés" : "conservés",
                &r->e[d + 2]);
            if (d + 3 < par) {
                if (!est_cle(&r->e[d + 3], "dont")) {
                    echouer(r, &r->e[d + 3], grym_dupliquer("« _dont » ou « _par » attendu après la liste."));
                    return;
                }
                mot(r, "dont", &r->e[d + 3]);
                expression(r, d + 4, par);
            }
            if (par < f) {
                if (par + 1 >= f || r->e[par + 1].type != J_CROCHETS) {
                    echouer(r, &r->e[par], grym_dupliquer("Champ du tri attendu : « _par nom »."));
                    return;
                }
                emettre(r, J_VIRGULE, NULL, &r->e[par], 1);
                mot(r, "par", &r->e[par]);
                copier(r, &r->e[par + 1]);
                if (par + 2 < f) mot(r, "décroissant", &r->e[par + 2]);
            }
            if (avec < fin_liste) {   /* « _avec a ; b.c » → « , avec a, c de b » ; « et » devant la dernière */
                emettre(r, J_VIRGULE, NULL, &r->e[avec], 1);
                mot(r, "avec", &r->e[avec]);
                size_t nb = 1;
                for (size_t k = avec + 1; k < fin_liste; k++) nb += r->e[k].type == J_POINT_VIRGULE;
                size_t debut = avec + 1, i = 0;
                for (size_t k = avec + 1; k <= fin_liste; k++) {
                    if (k < fin_liste && r->e[k].type != J_POINT_VIRGULE) continue;
                    if (i) {
                        if (i + 1 == nb) mot(r, "et", &r->e[k - 1]);
                        else emettre(r, J_VIRGULE, NULL, &r->e[k - 1], 1);
                    }
                    expression(r, debut, k);   /* compositeur.nom → nom de compositeur */
                    debut = k + 1;
                    i++;
                }
            }
            if (!etats_compacts(r, places, fin_places, 1, 0)) return;
            f = fin_places;
        } else if ((est_cle(t, "un") || est_cle(t, "une") || est_cle(t, "des")) && d + 4 < f + 1 && r->e[d + 1].type == J_CROCHETS
                   && r->e[d + 2].type == J_PAR_OUV
                   && ((r->e[d + 3].type == J_CROCHETS && r->e[d + 4].type == J_PAR_FERM) || r->e[d + 3].type == J_TEXTE)) {
            /* _un pays (texte) [_départ « Suisse »] [_facultatif] → un pays (texte), « Suisse » au départ, facultatif ;
             * _un état (« actif » ; « retiré ») → un état (« actif », « retiré ») (§ 22.7) */
            mot(r, t->valeur, t);
            copier(r, &r->e[d + 1]);
            emettre(r, J_PAR_OUV, NULL, &r->e[d + 2], 1);
            size_t k = d + 5;
            if (r->e[d + 3].type == J_TEXTE) {
                const size_t fv = fermante(r, d + 2, f);
                if (fv == f) { echouer(r, &r->e[d + 2], grym_dupliquer("Parenthèse fermante manquante.")); return; }
                for (size_t v = d + 3; v < fv; v++) {
                    if (r->e[v].type == J_POINT_VIRGULE) emettre(r, J_VIRGULE, NULL, &r->e[v], 1);
                    else copier(r, &r->e[v]);
                }
                emettre(r, J_PAR_FERM, NULL, &r->e[fv], 1);
                k = fv + 1;
            } else {
                copier(r, &r->e[d + 3]);
                emettre(r, J_PAR_FERM, NULL, &r->e[d + 4], 1);
            }
            while (k < f) {
                if (est_cle(&r->e[k], "départ") && k + 1 < f) {
                    size_t v = k + 1, fv = v + 1;
                    if (r->e[v].type == J_MOINS) fv++;
                    emettre(r, J_VIRGULE, NULL, &r->e[k], 1);
                    for (size_t x = v; x < fv && x < f; x++) {
                        if (r->e[x].type == J_MOT_CLE) mot(r, r->e[x].valeur, &r->e[x]);
                        else copier(r, &r->e[x]);
                    }
                    mot(r, "au", &r->e[k]);
                    mot(r, "départ", &r->e[k]);
                    k = fv;
                } else if (est_cle(&r->e[k], "facultatif") || est_cle(&r->e[k], "facultative")) {
                    emettre(r, J_VIRGULE, NULL, &r->e[k], 1);
                    mot(r, est_cle(t, "une") ? "facultative" : "facultatif", &r->e[k]);
                    k++;
                } else {
                    zone_de_texte = r->e[d + 3].type == J_TEXTE
                                    || (r->e[d + 3].type == J_CROCHETS && strcmp(r->e[d + 3].valeur, "texte") == 0);
                    if (!etats_compacts(r, k, f, est_cle(t, "une"), 1)) return;
                    zone_de_texte = 0;
                    k = f;
                }
            }
        } else {
            echouer(r, t, grym_dupliquer("Élément d'écran attendu : « _liste compositeur _conservé », "
                                         "« _bouton « OK » », « _texte « … » », « _un pays (texte) » ou « _côte_à_côte »."));
            return;
        }
        emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        return;
    }
    if (t->type == J_MOT_CLE && !strcmp(t->valeur, "écran") && !(d + 1 < f && r->e[d + 1].type == J_POINT)) {
        /* _écran des_compositeurs [« Titre »] → L'écran des compositeurs[, « Titre »,] montre : */
        size_t fin_nom = f;
        const Jeton *titre = NULL, *taille = NULL;
        if (f > d + 2 && (est_cle(&r->e[f - 1], "en_grand") || est_cle(&r->e[f - 1], "en_petit"))) { taille = &r->e[f - 1]; fin_nom = f - 1; }
        if (fin_nom > d + 2 && r->e[fin_nom - 1].type == J_TEXTE) { titre = &r->e[fin_nom - 1]; fin_nom--; }
        if (fin_nom <= d + 1) { echouer(r, t, grym_dupliquer("Forme attendue : « _écran des_compositeurs »."));  return; }
        emettre(r, J_ELISION, "l", t, 1);
        mot(r, "écran", t);
        for (size_t k = d + 1; k < fin_nom; k++) copier(r, &r->e[k]);
        if (titre || taille) {   /* _écran x « Recherche » _en_grand → L'écran x, « Recherche » en grand, montre : */
            emettre(r, J_VIRGULE, NULL, titre ? titre : taille, 1);
            if (titre) copier(r, titre);
            if (taille) { mot(r, "en", taille); mot(r, est_cle(taille, "en_grand") ? "grand" : "petit", taille); }
            emettre(r, J_VIRGULE, NULL, titre ? titre : taille, 1);
        }
        mot(r, "montre", t);
        emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_ECRAN, prof, t);
        return;
    }
    if (t->type == J_MOT_CLE && !strcmp(t->valeur, "quand")) {
        /* _quand _clique « B » _dans X ; _quand _choisit _un c _dans X → Quand on … dans l'écran X : */
        if (d + 2 < f && (est_cle(&r->e[d + 1], "ouvre") || est_cle(&r->e[d + 1], "ferme"))) {
            /* _quand _ouvre de_recherche → Quand on ouvre l'écran de recherche : */
            mot(r, "quand", t);
            mot(r, "on", t);
            mot(r, r->e[d + 1].valeur, &r->e[d + 1]);
            emettre(r, J_ELISION, "l", &r->e[d + 1], 1);
            mot(r, "écran", &r->e[d + 1]);
            for (size_t k = d + 2; k < f; k++) copier(r, &r->e[k]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            fixer_retrait(r, premier, prof);
            ouvrir(r, O_FORMULE, prof, t);
            return;
        }
        size_t dans = chercher(r, d + 1, f, "dans");
        if (d + 1 < f && est_cle(&r->e[d + 1], "change") && dans == d + 3 && r->e[d + 2].type == J_CROCHETS && dans + 1 < f) {
            /* _quand _change pays _dans de_recherche → Quand on change pays dans l'écran de recherche : */
            mot(r, "quand", t);
            mot(r, "on", t);
            mot(r, "change", &r->e[d + 1]);
            copier(r, &r->e[d + 2]);
            mot(r, "dans", &r->e[dans]);
            emettre(r, J_ELISION, "l", &r->e[dans], 1);
            mot(r, "écran", &r->e[dans]);
            for (size_t k = dans + 1; k < f; k++) copier(r, &r->e[k]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            fixer_retrait(r, premier, prof);
            ouvrir(r, O_FORMULE, prof, t);
            return;
        }
        int clic = d + 1 < f && est_cle(&r->e[d + 1], "clique") && dans == d + 3 && r->e[d + 2].type == J_TEXTE;
        int choix = d + 1 < f && est_cle(&r->e[d + 1], "choisit") && dans == d + 4
                    && (est_cle(&r->e[d + 2], "un") || est_cle(&r->e[d + 2], "une")) && r->e[d + 3].type == J_CROCHETS;
        if ((!clic && !choix) || dans + 1 >= f) {
            echouer(r, t, grym_dupliquer("Forme attendue : « _quand _clique « OK » _dans d'accueil » ou "
                                         "« _quand _choisit _un compositeur _dans des_compositeurs »."));
            return;
        }
        mot(r, "quand", t);
        mot(r, "on", t);
        if (clic) {
            mot(r, "clique", &r->e[d + 1]);
            mot(r, "sur", &r->e[d + 1]);
            copier(r, &r->e[d + 2]);
        } else {
            mot(r, "choisit", &r->e[d + 1]);
            mot(r, r->e[d + 2].valeur, &r->e[d + 2]);
            copier(r, &r->e[d + 3]);
        }
        mot(r, "dans", &r->e[dans]);
        emettre(r, J_ELISION, "l", &r->e[dans], 1);
        mot(r, "écran", &r->e[dans]);
        for (size_t k = dans + 1; k < f; k++) copier(r, &r->e[k]);
        emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_FORMULE, prof, t);
        return;
    }
    if (t->type == J_MOT_CLE && (!strcmp(t->valeur, "ouvrir") || !strcmp(t->valeur, "fermer"))) {
        /* _ouvrir des_compositeurs → Ouvrir l'écran des compositeurs. ; _fermer → Fermer l'écran. */
        int ouvre = !strcmp(t->valeur, "ouvrir");
        if (ouvre && d + 2 < f && est_cle(&r->e[d + 1], "fiche")) {   /* _ouvrir _fiche c → Ouvrir la fiche de c. */
            mot(r, "ouvrir", t);
            mot(r, "la", &r->e[d + 1]);
            mot(r, "fiche", &r->e[d + 1]);
            mot(r, "de", &r->e[d + 1]);
            expression(r, d + 2, f);
            point(r, f);
            fixer_retrait(r, premier, prof);
            return;
        }
        if (ouvre ? f <= d + 1 : f != d + 1) {
            echouer(r, t, grym_dupliquer(ouvre ? "Forme attendue : « _ouvrir des_compositeurs »." : "« _fermer » s'écrit seul."));
            return;
        }
        mot(r, t->valeur, t);
        emettre(r, J_ELISION, "l", t, 1);
        mot(r, "écran", t);
        for (size_t k = d + 1; k < f; k++) copier(r, &r->e[k]);
        point(r, f);
        fixer_retrait(r, premier, prof);
        return;
    }
    if (haut && haut->type == O_CLASSE && est_cle(t, "des")) {
        /* _des interprètes [(singulier)] (type) : champ multiple (§ 16.13) */
        size_t q = d + 2;
        int singulier = q + 5 < f + 1 && r->e[q].type == J_PAR_OUV && r->e[q + 1].type == J_CROCHETS
                        && r->e[q + 2].type == J_PAR_FERM && q + 3 < f && r->e[q + 3].type == J_PAR_OUV;
        if (singulier) q += 3;
        int type = q + 2 < f && r->e[q].type == J_PAR_OUV && r->e[q + 1].type == J_CROCHETS && r->e[q + 2].type == J_PAR_FERM;
        if (d + 1 >= f || r->e[d + 1].type != J_CROCHETS || !type || q + 3 != f) {
            echouer(r, t, grym_dupliquer("Champ multiple attendu : « _des genres (genre) », ou « _des travaux (travail) (tâche) »."));
            return;
        }
        if (haut->classe && !haut->champs) {
            mot(r, haut->article->valeur, t);
            copier(r, haut->classe);
            mot(r, "a", t);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            fixer_retrait(r, premier, haut->profondeur);
        }
        haut->champs++;
        mot(r, "des", t);
        copier(r, &r->e[d + 1]);
        for (size_t v = d + 2; v < f; v++) {
            if (r->e[v].type == J_CROCHETS) copier(r, &r->e[v]);
            else emettre(r, r->e[v].type, NULL, &r->e[v], 1);
        }
        emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
        return;
    }
    if (haut && haut->type == O_CLASSE) {
        /* _un nom [(type)] [_unique] ; _un état (« a » ; « b ») : valeurs permises (§ 16.15) */
        size_t q = d + 2;
        int type = q + 2 < f && r->e[q].type == J_PAR_OUV && r->e[q + 1].type == J_CROCHETS
                   && r->e[q + 2].type == J_PAR_FERM;
        size_t valeurs = 0, fin_valeurs = 0;
        if (!type && q + 1 < f && r->e[q].type == J_PAR_OUV && r->e[q + 1].type == J_TEXTE) {
            fin_valeurs = fermante(r, q, f);
            if (fin_valeurs == f) { echouer(r, &r->e[q], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            valeurs = q;
            q = fin_valeurs + 1;
        }
        if (type) q += 3;
        int unique = q < f && est_cle(&r->e[q], "unique");
        if (unique) q++;
        int facultatif = q < f && est_cle(&r->e[q], "facultatif");
        if (facultatif) q++;
        int cascade = q < f && est_cle(&r->e[q], "disparaît_avec");
        if (cascade) q++;
        size_t depart = 0, fin_depart = 0;   /* _départ « Suisse », _départ −3,5 */
        if (q < f && est_cle(&r->e[q], "départ")) {
            depart = q + 1;
            fin_depart = depart < f && r->e[depart].type == J_MOINS ? depart + 2 : depart + 1;
            q = fin_depart;
        }
        if (!(est_cle(t, "un") || est_cle(t, "une")) || d + 1 >= f || r->e[d + 1].type != J_CROCHETS || q != f) {
            echouer(r, t, grym_dupliquer("Champ attendu : « _un nom », « _un nom (texte) » ou « _une licence (texte) _unique »."));
            return;
        }
        if (haut->classe && !haut->champs) {
            /* « Un membre a : » : seconde phrase, après « Un membre est une personne. » */
            mot(r, haut->article->valeur, t);
            copier(r, haut->classe);
            mot(r, "a", t);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            fixer_retrait(r, premier, haut->profondeur);   /* la seconde phrase s'aligne sur la première */
        }
        haut->champs++;
        mot(r, t->valeur, t);
        copier(r, &r->e[d + 1]);
        if (type) {
            emettre(r, J_PAR_OUV, NULL, &r->e[d + 2], 1);
            copier(r, &r->e[d + 3]);
            emettre(r, J_PAR_FERM, NULL, &r->e[d + 4], 1);
        }
        if (valeurs) {
            emettre(r, J_PAR_OUV, NULL, &r->e[valeurs], 1);
            static const char *const COULEURS[] = {"bleu", "vert", "turquoise", "violet", "gris", "rouge", "orange"};
            for (size_t v = valeurs + 1; v < fin_valeurs; v++) {
                int couleur = -1;   /* « « payée » _vert » → « « payée » en vert » (§ 16.15) */
                for (int c = 0; c < 7 && couleur < 0; c++) if (est_cle(&r->e[v], COULEURS[c])) couleur = c;
                if (couleur >= 0) { mot(r, "en", &r->e[v]); mot(r, COULEURS[couleur], &r->e[v]); }
                else if (r->e[v].type == J_POINT_VIRGULE) emettre(r, J_VIRGULE, NULL, &r->e[v], 1);
                else copier(r, &r->e[v]);
            }
            emettre(r, J_PAR_FERM, NULL, &r->e[fin_valeurs], 1);
        }
        if (unique) {
            emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
            mot(r, "unique", &r->e[f - 1]);
        }
        if (facultatif) {
            emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
            mot(r, "facultatif", &r->e[f - 1]);   /* synthétique : l'accord n'est pas vérifié */
        }
        if (cascade) {   /* _disparaît_avec → , et disparaît avec lui (accord non vérifié) */
            emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
            mot(r, "et", &r->e[f - 1]);
            mot(r, "disparaît", &r->e[f - 1]);
            mot(r, "avec", &r->e[f - 1]);
            mot(r, "lui", &r->e[f - 1]);
        }
        if (depart) {
            emettre(r, J_VIRGULE, NULL, &r->e[depart - 1], 1);
            for (size_t v = depart; v < fin_depart; v++) {
                const Jeton *x = &r->e[v];
                if (x->type == J_MOT_CLE && (!strcmp(x->valeur, "vrai") || !strcmp(x->valeur, "faux"))) mot(r, x->valeur, x);
                else copier(r, x);
            }
            mot(r, "au", &r->e[fin_depart - 1]);
            mot(r, "départ", &r->e[fin_depart - 1]);
        }
        emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
        return;
    }
    if (haut && haut->type == O_INIT && !(t->type == J_MOT_CLE && !strcmp(t->valeur, "fin"))) {
        if (t->type != J_CROCHETS || f < d + 3 || r->e[d + 1].type != J_AFFECTE) {
            echouer(r, t, grym_dupliquer("Initialisation attendue : « nom << valeur »."));
            return;
        }
        emettre(r, J_ARTICLE_IMPLICITE, NULL, t, 1);
        copier(r, t);
        mot(r, "vaut", &r->e[d + 1]);
        expression(r, d + 2, f);
        point(r, f);
        fixer_retrait(r, premier, prof);
        return;
    }
    /* « … << _nouveau client _avec » : le bloc qui suit initialise l'objet (§ 13.2) */
    int avec = r->e[f - 1].type == J_MOT_CLE && f >= d + 3 && est_cle(&r->e[f - 1], "avec")
               && ((r->e[f - 2].type == J_CROCHETS && est_cle(&r->e[f - 3], "nouveau"))
                   || (f >= d + 4 && est_cle(&r->e[f - 2], "saisi") && r->e[f - 3].type == J_CROCHETS
                       && est_cle(&r->e[f - 4], "nouveau")));
    if (avec) f--;
    if (t->type == J_MOT_CLE && !strcmp(t->valeur, "aptitude")) {
        /* _aptitude horodatée [(horodaté)] → Une chose horodatée a : */
        int irregulier = f == d + 5 && r->e[d + 2].type == J_PAR_OUV && r->e[d + 3].type == J_CROCHETS
                         && r->e[d + 4].type == J_PAR_FERM;
        if ((f != d + 2 && !irregulier) || r->e[d + 1].type != J_CROCHETS) {
            echouer(r, t, grym_dupliquer("Forme attendue : « _aptitude horodatée » ou « _aptitude active (actif) »."));
            return;
        }
        mot(r, "une", t);
        mot(r, "chose", t);
        copier(r, &r->e[d + 1]);
        if (irregulier) {
            emettre(r, J_PAR_OUV, NULL, &r->e[d + 2], 1);
            copier(r, &r->e[d + 3]);
            emettre(r, J_PAR_FERM, NULL, &r->e[d + 4], 1);
        }
        mot(r, "a", t);
        emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_CLASSE, prof, t);
        return;
    }
    if (t->type == J_MOT_CLE && !strcmp(t->valeur, "classe")) {
        /* _classe _un cheval [(chevaux)] [_conservé] [_est _une personne [_adopte horodatée ; active]] */
        size_t p = d + 3;
        const Jeton *pluriel = NULL, *conserve = NULL;
        if (p + 2 < f && r->e[p].type == J_PAR_OUV && r->e[p + 1].type == J_CROCHETS
            && r->e[p + 2].type == J_PAR_FERM) {
            pluriel = &r->e[p + 1];
            p += 3;
        }
        if (p < f && (est_cle(&r->e[p], "conservé") || est_cle(&r->e[p], "conservée"))) {
            conserve = &r->e[p];
            p++;
        }
        size_t decale = p - (d + 3);   /* jetons de pluriel et de « _conservé » avant « _est » */
        size_t adopte = chercher(r, d + 1, f, "adopte");
        int herite = adopte == d + 6 + decale && est_cle(&r->e[p], "est")
                     && (est_cle(&r->e[p + 1], "un") || est_cle(&r->e[p + 1], "une")) && r->e[p + 2].type == J_CROCHETS;
        if (herite && adopte < f) {
            for (size_t q = adopte + 1; q < f; q += 2)
                if (r->e[q].type != J_CROCHETS || (q + 1 < f && r->e[q + 1].type != J_POINT_VIRGULE)) herite = 0;
            if (adopte + 1 >= f) herite = 0;
        }
        if (adopte < f && !herite) {
            echouer(r, t, grym_dupliquer("Forme attendue : « _classe _un membre _est _une personne _adopte horodatée »."));
            return;
        }
        if ((f != p && !herite) || !(est_cle(&r->e[d + 1], "un") || est_cle(&r->e[d + 1], "une"))
            || r->e[d + 2].type != J_CROCHETS) {
            echouer(r, t, grym_dupliquer("Forme attendue : « _classe _un client [_conservé] » ou "
                                         "« _classe _un membre [_conservé] _est _une personne »."));
            return;
        }
        mot(r, r->e[d + 1].valeur, t);
        copier(r, &r->e[d + 2]);
        if (pluriel) {
            emettre(r, J_PAR_OUV, NULL, pluriel, 1);
            copier(r, pluriel);
            emettre(r, J_PAR_FERM, NULL, pluriel, 1);
        }
        if (conserve) {
            emettre(r, J_VIRGULE, NULL, conserve, 1);
            mot(r, conserve->valeur, conserve);   /* synthétique : l'accord n'est pas vérifié */
            emettre(r, J_VIRGULE, NULL, conserve, 1);
        }
        if (herite) {
            mot(r, "est", &r->e[p]);
            mot(r, r->e[p + 1].valeur, &r->e[p + 1]);
            if (!strcmp(r->e[p + 2].valeur, "chose")) mot(r, "chose", &r->e[p + 2]);   /* aptitudes seules */
            else copier(r, &r->e[p + 2]);
            for (size_t q = adopte + 1; q < f; q += 2) {
                if (q > adopte + 1) mot(r, "et", &r->e[q - 1]);
                copier(r, &r->e[q]);                       /* adjectif : l'accord n'est pas vérifié */
            }
            point(r, f);
        } else {
            mot(r, "a", t);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[d + 2], 1);
        }
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_CLASSE, prof, t);
        if (herite) {
            r->pile[r->np - 1].classe = &r->e[d + 2];
            r->pile[r->np - 1].article = &r->e[d + 1];
        }
        return;
    }
    if (est_cle(t, "écran") && d + 2 < f && r->e[d + 1].type == J_POINT) {
        /* _écran.pays << « France » → Le pays de l'écran devient « France ». (§ 22.2) */
        size_t affecte = d;
        while (affecte < f && r->e[affecte].type != J_AFFECTE) affecte++;
        if (affecte >= f) { echouer(r, t, grym_dupliquer("« _écran.zone << valeur » attendu.")); return; }
        emettre(r, J_ARTICLE_IMPLICITE, NULL, t, 1);
        expression(r, d, affecte);
        mot(r, "devient", &r->e[affecte]);
        expression(r, affecte + 1, f);
        point(r, f);
        fixer_retrait(r, premier, prof);
        return;
    }
    if (t->type == J_MOT_CLE) {
        const char *c = t->valeur;
        if (!strcmp(c, "fin")) {
            if (f != d + 1) { echouer(r, &r->e[d + 1], grym_dupliquer("« _fin » s'écrit seul sur sa ligne.")); return; }
            if (!r->np) { echouer(r, t, grym_dupliquer("« _fin » sans construction ouverte.")); return; }
            r->np--;
            /* la construction occupe aussi la ligne de son « _fin » (lignes vides, § 12) */
            if (r->ns && r->s[r->ns - 1].ligne_fin < t->ligne) r->s[r->ns - 1].ligne_fin = t->ligne;
            return;
        }
        if (!strcmp(c, "sinon") || !strcmp(c, "sinon_si")) {
            if (!haut || haut->type != O_SI) {
                echouer(r, t, grym_formater("« _%s » hors d'un « _si ».", c));
                return;
            }
            mot(r, "sinon", t);
            fixer_retrait(r, premier, haut->profondeur);
            if (!strcmp(c, "sinon_si")) {
                size_t alors = chercher(r, d + 1, f, "alors");
                if (alors != f - 1) { echouer(r, t, grym_dupliquer("« _alors » attendu en fin de ligne.")); return; }
                mot(r, "si", t);
                expression(r, d + 1, alors);
            } else if (f != d + 1) {
                echouer(r, &r->e[d + 1], grym_dupliquer("« _sinon » s'écrit seul sur sa ligne."));
                return;
            }
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            return;
        }
        if (!strcmp(c, "échec")) {   /* _échec → En cas d'échec : (§ 18) */
            if (!haut || haut->type != O_ESSAI) {
                echouer(r, t, grym_dupliquer("« _échec » hors d'un « _essayer »."));
                return;
            }
            if (f != d + 1) {
                echouer(r, &r->e[d + 1], grym_dupliquer("« _échec » s'écrit seul sur sa ligne."));
                return;
            }
            mot(r, "en", t);
            mot(r, "cas", t);
            emettre(r, J_ELISION, "d", t, 1);
            mot(r, "échec", t);
            fixer_retrait(r, premier, haut->profondeur);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            return;
        }
        if (!strcmp(c, "cas") || !strcmp(c, "autrement")) {
            if (!haut || haut->type != O_SELON) {
                echouer(r, t, grym_formater("« _%s » hors d'un « _selon ».", c));
                return;
            }
            mot(r, c, t);
            haut->dans_cas = 1;
            fixer_retrait(r, premier, haut->profondeur + 1);
            if (!strcmp(c, "cas")) {
                size_t debut = d + 1;
                for (;;) {
                    size_t ou = chercher(r, debut, f, "ou");
                    condition_de_cas(r, debut, ou);
                    if (ou == f || r->echec) break;
                    mot(r, "ou", &r->e[ou]);
                    debut = ou + 1;
                }
            } else if (f != d + 1) {
                echouer(r, &r->e[d + 1], grym_dupliquer("« _autrement » s'écrit seul sur sa ligne."));
                return;
            }
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            return;
        }
        if (haut && haut->type == O_SELON && !haut->dans_cas) {
            echouer(r, t, grym_dupliquer("« _cas » ou « _autrement » attendu dans un « _selon »."));
            return;
        }
        if (!strcmp(c, "le") || !strcmp(c, "la") || !strcmp(c, "l'")) {
            if (d + 2 >= f || r->e[d + 1].type != J_CROCHETS || r->e[d + 2].type != J_AFFECTE) {
                echouer(r, t, grym_formater("Création attendue : « _%s nom << valeur ».", c));
                return;
            }
            if (!strcmp(c, "l'")) emettre(r, J_ELISION, "l", t, 1);
            else mot(r, c, t);
            copier(r, &r->e[d + 1]);
            mot(r, "vaut", &r->e[d + 2]);
            expression(r, d + 3, f);
            if (avec) { emettre(r, J_DEUX_POINTS, NULL, &r->e[f], 1); fixer_retrait(r, premier, prof); ouvrir(r, O_INIT, prof, t); return; }
            point(r, f);
        } else if (!strcmp(c, "essayer")) {   /* _essayer → Essayer : (§ 18) */
            if (d + 1 != f) { echouer(r, t, grym_dupliquer("« _essayer » s'écrit seul sur sa ligne.")); return; }
            mot(r, "essayer", t);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            ouvrir(r, O_ESSAI, prof, t);
        } else if (!strcmp(c, "utiliser")) {   /* _utiliser « données » → Utiliser « données ». (§ 21) */
            if (f != d + 2 || r->e[d + 1].type != J_TEXTE) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _utiliser « données » »."));
                return;
            }
            mot(r, "utiliser", t);
            emettre(r, J_TEXTE, r->e[d + 1].valeur, &r->e[d + 1], 0);
            point(r, f);
        } else if (!strcmp(c, "effacer")) {   /* _effacer → Effacer l'écran. */
            if (d + 1 != f) { echouer(r, t, grym_dupliquer("« _effacer » s'écrit seul.")); return; }
            mot(r, "effacer", t);
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "écran", t);
            point(r, f);
        } else if (!strcmp(c, "style")) {   /* _style _française → Les nombres s'affichent à la française. */
            const char *st = d + 1 < f && r->e[d + 1].type == J_MOT_CLE ? r->e[d + 1].valeur : NULL;
            if (!st || (strcmp(st, "suisse") && strcmp(st, "française") && strcmp(st, "sans_séparateur")) || d + 2 != f) {
                echouer(r, t, grym_dupliquer("Style attendu : « _style _suisse », « _style _française » "
                                             "ou « _style _sans_séparateur »."));
                return;
            }
            mot(r, "les", t);
            mot(r, "nombres", t);
            emettre(r, J_ELISION, "s", t, 1);
            mot(r, "affichent", t);
            if (strcmp(st, "sans_séparateur") == 0) { mot(r, "sans", t); mot(r, "séparateur", t); }
            else { mot(r, "à", t); mot(r, "la", t); mot(r, st, &r->e[d + 1]); }
            point(r, f);
        } else if (!strcmp(c, "chaque") && d + 2 < f && r->e[d + 1].type == J_CROCHETS
                   && (est_cle(&r->e[d + 2], "conservé") || est_cle(&r->e[d + 2], "conservée"))) {
            /* _chaque écriture _conservé [_dont …] _vérifie condition _sinon « … » ; x (§ 16.14) */
            size_t v = chercher(r, d + 3, f, "vérifie"), s = chercher(r, v, f, "sinon");
            if (v >= f || s >= f || s == v + 1 || s + 1 >= f) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _chaque écriture _conservé [_dont …] _vérifie condition "
                                             "_sinon « message » »."));
                return;
            }
            mot(r, "chaque", t);
            copier(r, &r->e[d + 1]);
            mot(r, "conservé", &r->e[d + 2]);
            if (d + 3 < v) {
                if (!est_cle(&r->e[d + 3], "dont")) {
                    echouer(r, &r->e[d + 3], grym_dupliquer("« _dont » ou « _vérifie » attendu après « _conservé »."));
                    return;
                }
                mot(r, "dont", &r->e[d + 3]);
                expression(r, d + 4, v);
            }
            mot(r, "vérifie", &r->e[v]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[v], 1);
            expression(r, v + 1, s);
            emettre(r, J_VIRGULE, NULL, &r->e[s], 1);
            mot(r, "sinon", &r->e[s]);
            size_t debut = s + 1;   /* les éléments du message, séparés par « ; » hors parenthèses */
            int p = 0;
            for (size_t k = s + 1; k <= f; k++) {
                if (k < f) {
                    if (r->e[k].type == J_PAR_OUV) p++;
                    else if (r->e[k].type == J_PAR_FERM) p--;
                    if (!(p == 0 && r->e[k].type == J_POINT_VIRGULE)) continue;
                }
                if (debut > s + 1) mot(r, "puis", &r->e[debut - 1]);
                expression(r, debut, k);
                debut = k + 1;
            }
            point(r, f);
        } else if (!strcmp(c, "refuser")) {   /* _refuser « … » → Refuser « … ». (§ 18.1) */
            if (d + 1 >= f) { echouer(r, t, grym_dupliquer("« _refuser » attend un message : « _refuser « … » ».")); return; }
            mot(r, "refuser", t);
            size_t debut = d + 1;   /* les éléments, séparés par « ; » hors parenthèses, comme pour _afficher */
            int p = 0;
            for (size_t k = d + 1; k <= f; k++) {
                if (k < f) {
                    if (r->e[k].type == J_PAR_OUV) p++;
                    else if (r->e[k].type == J_PAR_FERM) p--;
                    if (!(p == 0 && r->e[k].type == J_POINT_VIRGULE)) continue;
                }
                if (debut > d + 1) mot(r, "puis", &r->e[debut - 1]);
                expression(r, debut, k);
                debut = k + 1;
            }
            point(r, f);
        } else if (!strcmp(c, "écrans")) {   /* _écrans _couleur _verte _logo « … » → Les écrans ont … (§ 22.5) */
            size_t k = d + 1;
            const Jeton *coul = NULL, *logo = NULL;
            if (k + 1 < f && est_cle(&r->e[k], "couleur") && r->e[k + 1].type == J_MOT_CLE) { coul = &r->e[k + 1]; k += 2; }
            if (k + 1 < f && est_cle(&r->e[k], "logo") && r->e[k + 1].type == J_TEXTE) { logo = &r->e[k + 1]; k += 2; }
            if (k != f || (!coul && !logo)) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _écrans _couleur _verte _logo « logo.png » », "
                                             "l'un des deux réglages au moins, la couleur d'abord."));
                return;
            }
            mot(r, "les", t);
            mot(r, "écrans", t);
            mot(r, "ont", t);
            if (coul) { mot(r, "la", coul); mot(r, "couleur", coul); mot(r, coul->valeur, coul); }
            if (coul && logo) mot(r, "et", logo);
            if (logo) { mot(r, "le", logo); mot(r, "logo", logo); emettre(r, J_TEXTE, logo->valeur, logo, 0); }
            point(r, f);
        } else if (!strcmp(c, "afficher")) {
            mot(r, "afficher", t);
            size_t sans = f;
            if (f > d + 1 && est_cle(&r->e[f - 1], "sans_ligne")) sans = f - 1;   /* § 4.2 */
            size_t fin_elements = sans;
            size_t debut = d + 1;
            int p = 0;
            for (size_t k = d + 1; k <= fin_elements; k++) {
                if (k < fin_elements) {
                    if (r->e[k].type == J_PAR_OUV) p++;
                    else if (r->e[k].type == J_PAR_FERM) p--;
                    if (!(p == 0 && r->e[k].type == J_POINT_VIRGULE)) continue;
                }
                if (debut > d + 1) mot(r, "puis", &r->e[debut - 1]);
                expression(r, debut, k);
                debut = k + 1;
            }
            if (sans != f) {
                emettre(r, J_VIRGULE, NULL, &r->e[sans], 1);
                mot(r, "sans", &r->e[sans]);
                mot(r, "passer", &r->e[sans]);
                mot(r, "à", &r->e[sans]);
                mot(r, "la", &r->e[sans]);
                mot(r, "ligne", &r->e[sans]);
            }
            point(r, f);
        } else if (!strcmp(c, "si") || !strcmp(c, "tant_que")) {
            size_t alors = !strcmp(c, "si") ? chercher(r, d + 1, f, "alors") : f;
            if (!strcmp(c, "si") && alors != f - 1) {
                echouer(r, t, grym_dupliquer("« _alors » attendu en fin de ligne : « _si x > 0 _alors »."));
                return;
            }
            if (!strcmp(c, "si")) mot(r, "si", t);
            else { mot(r, "tant", t); mot(r, "que", t); }
            expression(r, d + 1, alors);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, !strcmp(c, "si") ? O_SI : O_BOUCLE, prof, t);
        } else if (!strcmp(c, "répéter")) {
            size_t fois = chercher(r, d + 1, f, "fois");
            if (fois != f - 1) { echouer(r, t, grym_dupliquer("« _fois » attendu : « _répéter 3 _fois »."));  return; }
            mot(r, "répéter", t);
            expression(r, d + 1, fois);
            mot(r, "fois", &r->e[fois]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, O_BOUCLE, prof, t);
        } else if (!strcmp(c, "pour_chaque") && d + 3 < f && r->e[d + 1].type == J_CROCHETS && est_cle(&r->e[d + 2], "parmi")) {
            /* _pour_chaque couleur _parmi couleurs → Pour chaque couleur parmi les couleurs : (§ 13.9) */
            mot(r, "pour", t);
            mot(r, "chaque", t);
            copier(r, &r->e[d + 1]);
            mot(r, "parmi", &r->e[d + 2]);
            mot(r, "les", &r->e[d + 2]);
            expression(r, d + 3, f);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, O_BOUCLE, prof, t);
        } else if (!strcmp(c, "pour_chaque") && d + 3 < f && r->e[d + 1].type == J_CROCHETS && est_cle(&r->e[d + 2], "tableau")) {
            /* _pour_chaque ligne _tableau « f.csv » → Pour chaque ligne du tableau « f.csv » : (§ 15.5) */
            mot(r, "pour", t);
            mot(r, "chaque", t);
            copier(r, &r->e[d + 1]);
            mot(r, "du", &r->e[d + 2]);
            mot(r, "tableau", &r->e[d + 2]);
            expression(r, d + 3, f);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, O_BOUCLE, prof, t);
        } else if (!strcmp(c, "pour_chaque") && d + 2 < f && r->e[d + 1].type == J_CROCHETS
                   && (est_cle(&r->e[d + 2], "conservé") || est_cle(&r->e[d + 2], "conservée") || est_cle(&r->e[d + 2], "supprimé"))) {
            /* _pour_chaque client _conservé [_dont …] [_par champ [_décroissant]] */
            mot(r, "pour", t);
            mot(r, "chaque", t);
            copier(r, &r->e[d + 1]);
            mot(r, est_cle(&r->e[d + 2], "supprimé") ? "supprimé" : "conservé", &r->e[d + 2]);
            size_t par = chercher(r, d + 3, f, "par");
            if (d + 3 < par) {
                if (!est_cle(&r->e[d + 3], "dont")) {
                    echouer(r, &r->e[d + 3], grym_dupliquer("« _dont » ou « _par » attendu après « _conservé »."));
                    return;
                }
                mot(r, "dont", &r->e[d + 3]);
                expression(r, d + 4, par);
            }
            if (par < f) {
                size_t fin_tri = f;
                if (est_cle(&r->e[f - 1], "décroissant")) fin_tri = f - 1;
                if (fin_tri != par + 2 || r->e[par + 1].type != J_CROCHETS) {
                    echouer(r, &r->e[par], grym_dupliquer("Tri attendu : « _par nom » ou « _par solde _décroissant »."));
                    return;
                }
                emettre(r, J_VIRGULE, NULL, &r->e[par], 1);
                mot(r, "par", &r->e[par]);
                copier(r, &r->e[par + 1]);
                if (fin_tri < f) mot(r, "décroissant", &r->e[f - 1]);
            }
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, O_BOUCLE, prof, t);
        } else if (!strcmp(c, "pour_chaque") && d + 2 < f && r->e[d + 1].type == J_CROCHETS && est_cle(&r->e[d + 2], "de")
                   && chercher(r, d + 1, chercher(r, d + 1, f, "dont"), "à") == chercher(r, d + 1, f, "dont")) {
            /* _pour_chaque œuvre _de bach [_dont …] [_par champ [_décroissant]] (§ 16.10) */
            size_t dont = chercher(r, d + 3, f, "dont"), par = chercher(r, d + 3, f, "par");
            size_t fin_objet = dont < par ? dont : par;
            mot(r, "pour", t);
            mot(r, "chaque", t);
            copier(r, &r->e[d + 1]);
            mot(r, "de", &r->e[d + 2]);
            expression(r, d + 3, fin_objet);
            if (dont < par) {
                mot(r, "dont", &r->e[dont]);
                expression(r, dont + 1, par);
            }
            if (par < f) {
                size_t fin_tri = est_cle(&r->e[f - 1], "décroissant") ? f - 1 : f;
                if (fin_tri != par + 2 || r->e[par + 1].type != J_CROCHETS) {
                    echouer(r, &r->e[par], grym_dupliquer("Tri attendu : « _par nom » ou « _par solde _décroissant »."));
                    return;
                }
                emettre(r, J_VIRGULE, NULL, &r->e[par], 1);
                mot(r, "par", &r->e[par]);
                copier(r, &r->e[par + 1]);
                if (fin_tri < f) mot(r, "décroissant", &r->e[f - 1]);
            }
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, O_BOUCLE, prof, t);
        } else if (!strcmp(c, "pour_chaque")) {
            size_t de = chercher(r, d + 1, f, "de"), a = chercher(r, d + 1, f, "à"), pas = chercher(r, d + 1, f, "pas");
            if (de != d + 2 || r->e[d + 1].type != J_CROCHETS || a == f || a < de) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _pour_chaque i _de 1 _à 9 [_pas 2] »."));
                return;
            }
            mot(r, "pour", t);
            mot(r, "chaque", t);
            copier(r, &r->e[d + 1]);
            mot(r, "de", &r->e[de]);
            expression(r, de + 1, a);
            mot(r, "à", &r->e[a]);
            expression(r, a + 1, pas);
            if (pas < f) {
                mot(r, "par", &r->e[pas]);
                mot(r, "pas", &r->e[pas]);
                mot(r, "de", &r->e[pas]);
                expression(r, pas + 1, f);
            }
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, O_BOUCLE, prof, t);
        } else if (!strcmp(c, "sortir") || !strcmp(c, "passer")) {
            if (f != d + 1) { echouer(r, &r->e[d + 1], grym_formater("« _%s » s'écrit seul.", c)); return; }
            if (!strcmp(c, "sortir")) { mot(r, "sortir", t); mot(r, "de", t); mot(r, "la", t); mot(r, "boucle", t); }
            else { mot(r, "passer", t); mot(r, "au", t); mot(r, "tour", t); mot(r, "suivant", t); }
            point(r, f);
        } else if (!strcmp(c, "selon")) {
            mot(r, "selon", t);
            expression(r, d + 1, f);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, O_SELON, prof, t);
        } else if (!strcmp(c, "fiche")) {   /* _fiche p → Afficher la fiche de p. (§ 20) */
            if (f == d + 1) { echouer(r, t, grym_dupliquer("Forme attendue : « _fiche client ».")); return; }
            mot(r, "afficher", t);
            mot(r, "la", t);
            mot(r, "fiche", t);
            mot(r, "de", t);
            expression(r, d + 1, f);
            point(r, f);
        } else if (!strcmp(c, "saisir")) {   /* _saisir p → Saisir à nouveau p. (§ 19) */
            if (f == d + 1) { echouer(r, t, grym_dupliquer("Forme attendue : « _saisir client ».")); return; }
            mot(r, "saisir", t);
            mot(r, "à", t);
            mot(r, "nouveau", t);
            expression(r, d + 1, f);
            point(r, f);
        } else if (!strcmp(c, "conserver") || !strcmp(c, "supprimer") || !strcmp(c, "rétablir")) {
            if (f == d + 1) {
                echouer(r, t, grym_formater("Forme attendue : « _%s client ».", c));
                return;
            }
            int definitif = !strcmp(c, "supprimer") && f > d + 2 && est_cle(&r->e[f - 1], "définitivement");
            mot(r, c, t);
            expression(r, d + 1, definitif ? f - 1 : f);
            if (definitif) mot(r, "définitivement", &r->e[f - 1]);   /* _supprimer c _définitivement (§ 16.12) */
            point(r, f);
        } else if (!strcmp(c, "les") && d + 2 < f && r->e[d + 1].type == J_CROCHETS
                   && (r->e[d + 2].type == J_AFFECTE || est_cle(&r->e[d + 2], "deviennent") || est_cle(&r->e[d + 2], "gagnent")
                       || est_cle(&r->e[d + 2], "perdent"))) {
            /* _les couleurs << « a » ; « b » → Les couleurs valent « a » et « b ». (§ 13.9) */
            mot(r, "les", t);
            copier(r, &r->e[d + 1]);
            const Jeton *v = &r->e[d + 2];
            mot(r, v->type == J_AFFECTE ? "valent" : v->valeur, v);
            if (d + 3 < f && est_cle(&r->e[d + 3], "ensemble_vide") && d + 4 == f) {
                mot(r, "un", &r->e[d + 3]);
                mot(r, "ensemble", &r->e[d + 3]);
                mot(r, "vide", &r->e[d + 3]);
            } else if (v->type == J_AFFECTE || est_cle(v, "deviennent")) {
                size_t debut = d + 3;
                int p = 0;
                for (size_t q = d + 3; q <= f; q++) {
                    if (q < f) {
                        if (r->e[q].type == J_PAR_OUV) p++;
                        else if (r->e[q].type == J_PAR_FERM) p--;
                        if (!(p == 0 && r->e[q].type == J_POINT_VIRGULE)) continue;
                    }
                    if (debut > d + 3) emettre(r, J_VIRGULE, NULL, &r->e[debut - 1], 1);
                    if (q == debut) { echouer(r, &r->e[q < f ? q : f - 1], grym_dupliquer("Élément vide entre « ; »."));  return; }
                    expression(r, debut, q);
                    debut = q + 1;
                }
            } else {
                expression(r, d + 3, f);
            }
            point(r, f);
        } else if (!strcmp(c, "exemple")) {   /* _exemple carré(7) << 49 → Exemple : le carré de 7 vaut 49. (§ 9.6) */
            size_t aff = d + 1;
            while (aff < f && r->e[aff].type != J_AFFECTE) aff++;
            if (aff == f || aff == d + 1 || aff + 1 == f) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _exemple carré(7) << 49 »."));
                return;
            }
            mot(r, "exemple", t);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            expression(r, d + 1, aff);
            mot(r, "vaut", &r->e[aff]);
            expression(r, aff + 1, f);
            point(r, f);
        } else if (!strcmp(c, "exporter")) {
            /* _exporter œuvre _conservé [_dont …] [_par titre [_décroissant]] [_avec titre ; compositeur.nom] _dans « m.csv »
             * → Exporter les œuvre conservés …, avec titre, nom de compositeur, dans « m.csv ». (§ 15.4) */
            size_t dans = chercher(r, d + 1, f, "dans");
            if (d + 2 >= f || r->e[d + 1].type != J_CROCHETS || dans == f
                || !(est_cle(&r->e[d + 2], "conservé") || est_cle(&r->e[d + 2], "conservée")
                     || est_cle(&r->e[d + 2], "supprimé") || est_cle(&r->e[d + 2], "supprimée"))) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _exporter membre _conservé _dans « membres.csv » »."));
                return;
            }
            /* _titre « … », _total cotisation ; montant : un rapport (§ 15.6), après les colonnes */
            const size_t titre = chercher(r, d + 3, dans, "titre"), total = chercher(r, d + 3, dans, "total");
            const size_t fin_cols = titre < total ? titre : total;
            size_t avec = chercher(r, d + 3, fin_cols, "avec");
            size_t par = chercher(r, d + 3, avec, "par");
            mot(r, "exporter", t);
            mot(r, "les", t);
            copier(r, &r->e[d + 1]);
            mot(r, est_cle(&r->e[d + 2], "supprimé") || est_cle(&r->e[d + 2], "supprimée") ? "supprimés" : "conservés",
                &r->e[d + 2]);
            if (d + 3 < par) {
                if (!est_cle(&r->e[d + 3], "dont")) {
                    echouer(r, &r->e[d + 3], grym_dupliquer("« _dont », « _par », « _avec » ou « _dans » attendu."));
                    return;
                }
                mot(r, "dont", &r->e[d + 3]);
                expression(r, d + 4, par);
            }
            if (par < avec) {
                if (par + 1 >= avec || r->e[par + 1].type != J_CROCHETS) {
                    echouer(r, &r->e[par], grym_dupliquer("Champ du tri attendu : « _par nom »."));
                    return;
                }
                emettre(r, J_VIRGULE, NULL, &r->e[par], 1);
                mot(r, "par", &r->e[par]);
                copier(r, &r->e[par + 1]);
                if (par + 2 < avec) mot(r, "décroissant", &r->e[par + 2]);
            }
            if (avec < dans) {
                emettre(r, J_VIRGULE, NULL, &r->e[avec], 1);
                mot(r, "avec", &r->e[avec]);
                size_t debut = avec + 1, i = 0;
                const size_t bout = fin_cols < dans ? fin_cols : dans;
                for (size_t k = avec + 1; k <= bout; k++) {
                    if (k < bout && r->e[k].type != J_POINT_VIRGULE) continue;
                    if (i) emettre(r, J_VIRGULE, NULL, &r->e[k - 1], 1);
                    expression(r, debut, k);
                    debut = k + 1;
                    i++;
                }
                emettre(r, J_VIRGULE, NULL, &r->e[bout], 1);
            }
            if (titre < dans) {
                if (titre + 1 >= dans || r->e[titre + 1].type != J_TEXTE) { echouer(r, &r->e[titre], grym_dupliquer("Titre attendu : « _titre « … » ».")); return; }
                if (avec >= fin_cols) emettre(r, J_VIRGULE, NULL, &r->e[titre], 1);
                mot(r, "avec", &r->e[titre]); mot(r, "le", &r->e[titre]); mot(r, "titre", &r->e[titre]);
                copier(r, &r->e[titre + 1]);
                emettre(r, J_VIRGULE, NULL, &r->e[titre], 1);
            }
            if (total < dans) {
                if (avec >= fin_cols && titre > total) emettre(r, J_VIRGULE, NULL, &r->e[total], 1);
                mot(r, "avec", &r->e[total]); mot(r, "le", &r->e[total]); mot(r, "total", &r->e[total]);
                const size_t bout_t = titre > total && titre < dans ? titre : dans;
                size_t debut = total + 1;
                for (size_t k = total + 1; k <= bout_t; k++) {
                    if (k < bout_t && r->e[k].type != J_POINT_VIRGULE) continue;
                    if (debut > total + 1) mot(r, "et", &r->e[k - 1]);
                    mot(r, "de", &r->e[debut]);
                    expression(r, debut, k);
                    debut = k + 1;
                }
                emettre(r, J_VIRGULE, NULL, &r->e[bout_t < f ? bout_t : dans], 1);
            }
            mot(r, "dans", &r->e[dans]);
            expression(r, dans + 1, f);
            point(r, f);
        } else if (!strcmp(c, "enregistrer")) {
            size_t dans = chercher(r, d + 1, f, "dans");
            if (dans == f || dans == d + 1) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _enregistrer photo _dans « copie.jpg » »."));
                return;
            }
            mot(r, "enregistrer", t);
            expression(r, d + 1, dans);
            mot(r, "dans", &r->e[dans]);
            expression(r, dans + 1, f);
            point(r, f);
        } else if (!strcmp(c, "rendre")) {
            mot(r, "rendre", t);
            expression(r, d + 1, f);
            point(r, f);
        } else if (!strcmp(c, "calcul") || !strcmp(c, "action")) {
            int calcul = !strcmp(c, "calcul");
            size_t k = d + 1;
            const Jeton *art = NULL;
            if (calcul) {
                art = &r->e[k];
                if (!est_cle(art, "le") && !est_cle(art, "la") && !est_cle(art, "l'")) {
                    echouer(r, art, grym_dupliquer("Article attendu : « _calcul _le carré(_un nombre) … »."));
                    return;
                }
                k++;
            }
            if (k + 1 >= f || r->e[k].type != J_CROCHETS || r->e[k + 1].type != J_PAR_OUV) {
                echouer(r, t, grym_formater("Forme attendue : « _%s nom(_un paramètre) ».", c));
                return;
            }
            size_t ferme = fermante(r, k + 1, f);
            if (ferme == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            if (calcul) {
                if (!strcmp(art->valeur, "l'")) emettre(r, J_ELISION, "l", art, 1);
                else mot(r, art->valeur, art);
            } else {
                mot(r, "pour", t);
            }
            copier(r, &r->e[k]);
            parametres(r, k + 2, ferme, calcul);
            if (calcul && ferme + 1 < f) {
                if (r->e[ferme + 1].type != J_AFFECTE) {
                    echouer(r, &r->e[ferme + 1], grym_dupliquer("« << » ou fin de ligne attendu après les paramètres."));
                    return;
                }
                mot(r, "vaut", &r->e[ferme + 1]);
                expression(r, ferme + 2, f);
                point(r, f);
            } else {
                if (ferme + 1 < f) {
                    echouer(r, &r->e[ferme + 1], grym_dupliquer("Fin de ligne attendue : le corps suit, fermé par « _fin »."));
                    return;
                }
                emettre(r, J_DEUX_POINTS, NULL, &r->e[ferme], 1);
                ouvrir(r, O_FORMULE, prof, t);
            }
        } else {
            echouer(r, t, grym_formater("« _%s » ne commence pas une instruction.", c));
            return;
        }
        fixer_retrait(r, premier, prof);
        return;
    }
    if (haut && haut->type == O_SELON && !haut->dans_cas) {
        echouer(r, t, grym_dupliquer("« _cas » ou « _autrement » attendu dans un « _selon »."));
        return;
    }
    size_t gagne = d;   /* o.genres _gagne baroque → Les genres de o gagnent baroque. (§ 16.13) */
    while (gagne < f && !est_cle(&r->e[gagne], "gagne") && !est_cle(&r->e[gagne], "perd")) gagne++;
    if (t->type == J_CROCHETS && gagne < f && gagne >= d + 3 && r->e[gagne - 2].type == J_POINT
        && r->e[gagne - 1].type == J_CROCHETS) {
        mot(r, "les", t);
        copier(r, &r->e[gagne - 1]);
        mot(r, "de", &r->e[gagne - 2]);
        expression(r, d, gagne - 2);
        mot(r, est_cle(&r->e[gagne], "gagne") ? "gagnent" : "perdent", &r->e[gagne]);
        expression(r, gagne + 1, f);
        point(r, f);
        fixer_retrait(r, premier, prof);
        return;
    }
    size_t affecte = d;
    while (affecte < f && r->e[affecte].type != J_AFFECTE) affecte++;
    if ((t->type == J_CROCHETS || (est_cle(t, "écran") && d + 1 < f && r->e[d + 1].type == J_POINT)) && affecte < f && affecte > d
        && (affecte == d + 1 || r->e[d + 1].type == J_POINT)) {
        emettre(r, J_ARTICLE_IMPLICITE, NULL, t, 1);   /* total << … → Le total devient … */
        expression(r, d, affecte);                     /* client.solde → solde de client */
        mot(r, "devient", &r->e[affecte]);
        expression(r, affecte + 1, f);
        if (avec) { emettre(r, J_DEUX_POINTS, NULL, &r->e[f], 1); fixer_retrait(r, premier, prof); ouvrir(r, O_INIT, prof, t); return; }
        point(r, f);
    } else if (t->type == J_CROCHETS && d + 1 < f && r->e[d + 1].type == J_PAR_OUV && fermante(r, d + 1, f) == f - 1) {
        copier(r, t);                                 /* relancer(client) → Relancer client. */
        arguments(r, d + 2, f - 1, 0, t);
        point(r, f);
    } else if (t->type == J_CROCHETS && d + 1 < f && r->e[d + 1].type == J_PAR_OUV && f >= 3 && fermante(r, d + 1, f) == f - 3
               && est_cle(&r->e[f - 2], "comme") && r->e[f - 1].type == J_CROCHETS) {
        copier(r, t);                                 /* saluer(m) _comme personne → Saluer m comme un personne. (§ 13.8) */
        arguments(r, d + 2, f - 3, 0, t);
        mot(r, "comme", &r->e[f - 2]);
        mot(r, "un", &r->e[f - 2]);
        copier(r, &r->e[f - 1]);
        point(r, f);
    } else {
        expression(r, d, f);
        point(r, f);
    }
    fixer_retrait(r, premier, prof);
}

int compact_vers_litteraire(Jeton *e, size_t n, Jeton **sortie, size_t *nb_sortie, Diagnostic *diag) {
    Reecriture r;
    memset(&r, 0, sizeof r);
    r.e = e;
    r.n = n;
    r.diag = diag;
    size_t k = 0;
    while (k < n && e[k].type != J_FIN && !r.echec) {
        /* une instruction : jusqu'au changement de ligne, hors parenthèses */
        size_t d = k;
        int p = 0;
        k++;
        if (e[d].type == J_PAR_OUV) p++;
        while (k < n && e[k].type != J_FIN && (p > 0 || e[k].ligne == e[k - 1].ligne) && e[d].type != J_REMARQUE) {
            if (e[k].type == J_PAR_OUV) p++;
            else if (e[k].type == J_PAR_FERM) p--;
            k++;
        }
        instruction(&r, d, k);
    }
    if (!r.echec && r.np) {
        const Niveau *h = &r.pile[r.np - 1];
        echouer(&r, h->mot, grym_formater("« _fin » manquant : « _%s », ligne %d, n'est pas fermé.",
                                          h->mot->valeur, h->mot->ligne));
    }
    const Jeton *fin = &e[n - 1];
    emettre(&r, J_FIN, NULL, fin, 1);
    free(r.pile);
    if (r.echec) {
        for (size_t q = 0; q < r.ns; q++) free(r.s[q].valeur);
        free(r.s);
        return 0;
    }
    *sortie = r.s;
    *nb_sortie = r.ns;
    return 1;
}
