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

typedef enum { O_SI, O_BOUCLE, O_SELON, O_FORMULE, O_CLASSE, O_INIT, O_ESSAI, O_ECRAN, O_BLOC, O_ENVOI, O_XML } Ouverture;

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
    int envoi_ouvrant;     /* la phrase finit par « _avec », « _de » ou « _du_tableau » d'un envoi (2.43) : 1, 2, 3 */
    int vu_facultatif;   /* parametres() : un facultatif déjà écrit (2.98) */
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
    int p = 0, premier = 1, facultatifs = 0;
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
        if (!de && k > debut + 2 && r->e[debut].type == J_CROCHETS && r->e[debut + 1].type == J_AFFECTE) {
            /* « relancer(c ; délai << 10) » → Relancer c, avec le délai de (10). (2.98) : l'article, l'analyseur le prend
             * tel quel ; la forme littéraire reprend celui du paramètre */
            if (!facultatifs) {
                emettre(r, J_VIRGULE, NULL, &r->e[debut], 1);
                mot(r, "avec", &r->e[debut]);
            } else mot(r, "et", &r->e[debut]);
            facultatifs = 1;
            emettre(r, J_ARTICLE_IMPLICITE, NULL, &r->e[debut], 1);
            copier(r, &r->e[debut]);
            mot(r, "de", &r->e[debut + 1]);
            emettre(r, J_PAR_OUV, NULL, &r->e[debut + 2], 1);
            expression(r, debut + 2, k);
            emettre(r, J_PAR_FERM, NULL, &r->e[k < f ? k : f - 1], 1);
            premier = 0;
            debut = k + 1;
            continue;
        }
        if (facultatifs) {
            echouer(r, &r->e[debut], grym_dupliquer("Les facultatifs viennent après les arguments : « relancer(c ; délai << 10) »."));
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

/* L'index d'un « _contient » à la profondeur 0 dans [d, f), ou f : « x _contient y » se réécrit « y est parmi les x »
 * quand x n'est pas un simple nom (ce cas et celui des clés d'une table ont leurs règles plus bas). */
static size_t contient_general(const Reecriture *r, size_t d, size_t f) {
    if (d >= f || r->e[d].type == J_CROCHETS || est_cle(&r->e[d], "clés") || est_cle(&r->e[d], "valeurs")) return f;
    int p = 0;
    for (size_t k = d; k < f; k++) {
        if (r->e[k].type == J_PAR_OUV) p++;
        else if (r->e[k].type == J_PAR_FERM) p--;
        else if (p == 0 && est_cle(&r->e[k], "contient")) return k;
        else if (p == 0 && r->e[k].type == J_MOT_CLE
                 && (!strcmp(r->e[k].valeur, "et") || !strcmp(r->e[k].valeur, "ou") || !strcmp(r->e[k].valeur, "non")
                     || !strcmp(r->e[k].valeur, "dont"))) return f;   /* « _dont genres _contient x » a sa règle */
    }
    return f;
}

/* La fin de la droite d'un « _contient » : avant « _et » ou « _ou » à la profondeur 0. */
static size_t fin_droite(const Reecriture *r, size_t d, size_t f) {
    int p = 0;
    size_t e = d;
    while (e < f && !(p == 0 && r->e[e].type == J_MOT_CLE && (!strcmp(r->e[e].valeur, "et") || !strcmp(r->e[e].valeur, "ou")))) {
        if (r->e[e].type == J_PAR_OUV) p++;
        else if (r->e[e].type == J_PAR_FERM) p--;
        e++;
    }
    return e;
}

static void expression(Reecriture *r, size_t d, size_t f) {
    {   /* « _les genres _de o _contient x », « _lignes t _contient x » → « x est parmi les genres de o » (3.19) */
        const size_t c = contient_general(r, d, f);
        if (c < f) {
            const size_t e = fin_droite(r, c + 1, f);
            expression(r, c + 1, e);
            mot(r, "est", &r->e[c]);
            mot(r, "parmi", &r->e[c]);
            /* « les » : la gauche l'écrit elle-même (« _les », « _lignes », « _documents_sortis », « _trier ») */
            expression(r, d, c);
            if (e < f) expression(r, e, f);
            return;
        }
    }
    for (size_t k = d; k < f && !r->echec; k++) {
        const Jeton *t = &r->e[k];
        if (est_cle(t, "les") && k + 1 < f && r->e[k + 1].type == J_CROCHETS) {   /* _les genres _de o : « les genres de o » (3.19) */
            mot(r, "les", t);
            copier(r, &r->e[k + 1]);
            k++;
            continue;
        }
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
        if ((est_cle(t, "le") || est_cle(t, "la") || est_cle(t, "l'") || est_cle(t, "nu")) && k + 2 < f && r->e[k + 1].type == J_CROCHETS
            && r->e[k + 2].type == J_POINT && k + 3 < f && r->e[k + 3].type == J_CROCHETS) {
            /* « _le patient.nom » → « nom du patient » : l'article écrit de l'objet d'un champ (2.36) */
            const size_t b = k + 2;
            size_t q = b;
            while (q + 1 < f && r->e[q].type == J_POINT && r->e[q + 1].type == J_CROCHETS) q += 2;
            for (size_t i = q; i > b; i -= 2) {
                copier(r, &r->e[i - 1]);
                if (i - 2 != b) { mot(r, "de", &r->e[i - 2]); continue; }
                if (est_cle(t, "le")) mot(r, "du", t);
                else if (est_cle(t, "la")) { mot(r, "de", t); mot(r, "la", t); }
                else if (est_cle(t, "nu")) mot(r, "de", t);
                else { mot(r, "de", t); emettre(r, J_ELISION, "l", t, 1); }
            }
            if (est_cle(t, "nu")) emettre(r, J_CROCHETS, r->e[k + 1].valeur, &r->e[k + 1], 2);   /* sans article à deviner */
            else copier(r, &r->e[k + 1]);
            k = q - 1;
            continue;
        }
        if (est_cle(t, "nu") && k + 1 < f && r->e[k + 1].type == J_CROCHETS) {
            /* « _nu d » → « d », sans article à deviner (2.35) */
            emettre(r, J_CROCHETS, r->e[k + 1].valeur, &r->e[k + 1], 2);
            k++;
            continue;
        }
        if ((est_cle(t, "le") || est_cle(t, "la") || est_cle(t, "l'")) && k + 1 < f && r->e[k + 1].type == J_CROCHETS
            && !(k + 2 < f && (est_cle(&r->e[k + 2], "conservé") || est_cle(&r->e[k + 2], "conservée") || est_cle(&r->e[k + 2], "supprimé")))
            && !(k + 2 < f && r->e[k + 2].type == J_POINT)) {
            /* « _le relevé » → « le relevé » : l'article écrit (2.35) */
            if (!strcmp(t->valeur, "l'")) emettre(r, J_ELISION, "l", t, 1);
            else mot(r, t->valeur, t);
            copier(r, &r->e[k + 1]);
            k++;
            continue;
        }
        if (est_cle(t, "est")) {   /* « x _est v » → « x est v » (2.35) : l'égalité écrite en mots */
            mot(r, "est", t);
            continue;
        }
        if (est_cle(t, "écran") && !(k + 1 < f && r->e[k + 1].type == J_POINT)) {   /* _écran seul → l'écran (2.51) */
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "écran", t);
            continue;
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
        {
            /* § 3.7 : _racine(x) → la racine carrée de (x) ; _plus_grand(a ; b ; c) → le plus grand de (a), de (b) et de (c) ;
             * _somme(notes) → la somme des notes ; _plus_grand_élément(notes) → le plus grand élément des notes */
            static const char *const CLES[] = { "racine", "plus_grand", "plus_petit", "somme", "moyenne", "plus_grand_élément",
                                                "plus_petit_élément" };
            int qf = -1;
            for (int q = 0; q < 7; q++) if (est_cle(t, CLES[q])) qf = q;
            if (qf >= 0 && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
                size_t fin = fermante(r, k + 1, f);
                if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
                size_t bornes[64], nb = 0;
                int p = 0;
                bornes[0] = k + 1;
                for (size_t q = k + 2; q <= fin && nb < 63; q++) {
                    if (q < fin && r->e[q].type == J_PAR_OUV) p++;
                    else if (q < fin && r->e[q].type == J_PAR_FERM) p--;
                    else if (q == fin || (p == 0 && r->e[q].type == J_POINT_VIRGULE)) bornes[++nb] = q;
                }
                int vide = 0;
                for (size_t q = 0; q < nb; q++) if (bornes[q + 1] == bornes[q] + 1) vide = 1;
                const int bon = qf == 0 ? nb == 1 : qf <= 2 ? nb >= 2 : nb == 1 && bornes[1] == k + 3 && r->e[k + 2].type == J_CROCHETS;
                if (!bon || vide) {
                    static const char *const MODELE[] = { "_racine(x)", "_plus_grand(a ; b)", "_plus_petit(a ; b)", "_somme(notes)",
                                                          "_moyenne(notes)", "_plus_grand_élément(notes)", "_plus_petit_élément(notes)" };
                    echouer(r, t, grym_formater("« _%s » s'écrit ainsi : %s.", CLES[qf], MODELE[qf]));
                    return;
                }
                if (qf >= 3) {
                    mot(r, qf <= 4 ? "la" : "le", t);
                    if (qf <= 4) mot(r, qf == 3 ? "somme" : "moyenne", t);
                    else { mot(r, "plus", t); mot(r, qf == 5 ? "grand" : "petit", t); mot(r, "élément", t); }
                    mot(r, "des", t);
                    copier(r, &r->e[k + 2]);
                } else {
                    mot(r, qf == 0 ? "la" : "le", t);
                    if (qf == 0) { mot(r, "racine", t); mot(r, "carrée", t); }
                    else { mot(r, "plus", t); mot(r, qf == 1 ? "grand" : "petit", t); }
                    for (size_t q = 0; q < nb; q++) {
                        if (q) {
                            if (q + 1 == nb) mot(r, "et", &r->e[bornes[q]]);
                            else emettre(r, J_VIRGULE, NULL, &r->e[bornes[q]], 1);
                        }
                        mot(r, "de", &r->e[bornes[q]]);
                        emettre(r, J_PAR_OUV, NULL, &r->e[bornes[q] + 1], 1);
                        expression(r, bornes[q] + 1, bornes[q + 1]);
                        emettre(r, J_PAR_FERM, NULL, &r->e[bornes[q + 1]], 1);
                    }
                }
                k = fin;
                continue;
            }
        }
        if (est_cle(t, "documents_sortis")) {   /* _documents_sortis → les documents sortis (3.09) */
            if (!(k > 0 && est_cle(&r->e[k - 1], "parmi"))) mot(r, "les", t);
            mot(r, "documents", t);
            mot(r, "sortis", t);
            continue;
        }
        if (est_cle(t, "plusieurs_postes")) {   /* _plusieurs_postes → le service à plusieurs postes (3.18) */
            mot(r, "le", t);
            mot(r, "service", t);
            mot(r, "à", t);
            mot(r, "plusieurs", t);
            mot(r, "postes", t);
            continue;
        }
        if (est_cle(t, "utilisateur_connecté")) {   /* _utilisateur_connecté → l'utilisateur connecté (3.17) */
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "utilisateur", t);
            mot(r, "connecté", t);
            continue;
        }
        if (est_cle(t, "fonction_c")) {   /* _fonction_c(« f ») → la fonction C « f » (3.14, § 21.10) */
            if (!(k + 3 < f && r->e[k + 1].type == J_PAR_OUV && r->e[k + 2].type == J_TEXTE && r->e[k + 3].type == J_PAR_FERM)) {
                echouer(r, t, grym_dupliquer("« _fonction_c » s'écrit ainsi : _fonction_c(« signal_niveau »)."));
                return;
            }
            mot(r, "la", t);
            mot(r, "fonction", t);
            mot(r, "c", t);
            copier(r, &r->e[k + 2]);
            k += 3;
            continue;
        }
        if (est_cle(t, "commande") && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* _commande(« p » ; arguments) → la sortie de la commande (« p ») avec les arguments (§ 15.10) */
            size_t fin = fermante(r, k + 1, f), pv = k + 2;
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            int p = 0;
            while (pv < fin && !(p == 0 && r->e[pv].type == J_POINT_VIRGULE)) {
                if (r->e[pv].type == J_PAR_OUV) p++;
                else if (r->e[pv].type == J_PAR_FERM) p--;
                pv++;
            }
            if (pv == k + 2 || (pv < fin && (pv + 2 != fin || r->e[pv + 1].type != J_CROCHETS))) {
                echouer(r, t, grym_dupliquer("« _commande » s'écrit ainsi : _commande(« rigctl »), _commande(« rigctl » ; arguments)."));
                return;
            }
            mot(r, "la", t);
            mot(r, "sortie", t);
            mot(r, "de", t);
            mot(r, "la", t);
            mot(r, "commande", t);
            emettre(r, J_PAR_OUV, NULL, &r->e[k + 2], 1);
            expression(r, k + 2, pv);
            emettre(r, J_PAR_FERM, NULL, &r->e[pv < fin ? pv : fin], 1);
            if (pv < fin) { mot(r, "avec", &r->e[pv]); mot(r, "les", &r->e[pv]); copier(r, &r->e[pv + 1]); }
            k = fin;
            continue;
        }
        if (est_cle(t, "lignes")) {   /* _lignes x → les lignes de x (§ 15.9) ; après « _parmi », « parmi les » est écrit */
            if (!(k > 0 && est_cle(&r->e[k - 1], "parmi"))) mot(r, "les", t);
            mot(r, "lignes", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "json") && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {   /* _json(x) → le JSON de (x) (§ 15.9) */
            size_t fin = fermante(r, k + 1, f);
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            mot(r, "le", t);
            mot(r, "json", t);
            mot(r, "de", t);
            emettre(r, J_PAR_OUV, NULL, &r->e[k + 2], 1);
            expression(r, k + 2, fin);
            emettre(r, J_PAR_FERM, NULL, &r->e[fin], 1);
            k = fin;
            continue;
        }
        if (est_cle(t, "trier") && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* _trier(notes) → les notes triés ; _trier(notes ; _décroissant) → … triés par ordre décroissant ;
             * _trier(inscrits ; nom ; _décroissant) → … triés par nom décroissant (§ 13.12) */
            size_t fin = fermante(r, k + 1, f);
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            size_t pv[3], nb = 0;
            int p = 0;
            for (size_t q = k + 2; q < fin; q++) {
                if (r->e[q].type == J_PAR_OUV) p++;
                else if (r->e[q].type == J_PAR_FERM) p--;
                else if (p == 0 && r->e[q].type == J_POINT_VIRGULE && nb < 3) pv[nb++] = q;
            }
            const size_t fin0 = nb ? pv[0] : fin;
            const Jeton *champ = NULL;
            int decroissant = 0, calcul = 0, bon = fin0 > k + 2 && nb <= 2;
            for (size_t q = 0; q < nb && bon; q++) {
                const size_t d0 = pv[q] + 1, f0 = q + 1 < nb ? pv[q + 1] : fin;
                if (f0 == d0 + 2 && est_cle(&r->e[d0], "calcul") && r->e[d0 + 1].type == J_CROCHETS && !champ && !decroissant) {
                    champ = &r->e[d0 + 1];   /* « _calcul poids » (2.97) */
                    calcul = 1;
                } else if (f0 != d0 + 1) bon = 0;
                else if (est_cle(&r->e[d0], "décroissant")) decroissant = 1;
                else if (r->e[d0].type == J_CROCHETS && !champ && !decroissant) champ = &r->e[d0];
                else bon = 0;
            }
            if (!bon) {
                echouer(r, t, grym_dupliquer("« _trier » s'écrit ainsi : _trier(notes), _trier(inscrits ; nom ; _décroissant), "
                                             "_trier(mots ; _calcul poids)."));
                return;
            }
            if (fin0 == k + 3 && r->e[k + 2].type == J_CROCHETS) copier(r, &r->e[k + 2]);   /* un nom : « parmi les inscrits triés » */
            else if (k > 0 && est_cle(&r->e[k - 1], "parmi") && est_cle(&r->e[k + 2], "mots")) {
                /* _parmi _trier(_mots t ; …) : « parmi les » est écrit, reste « mots de t » (2.97) */
                mot(r, "mots", &r->e[k + 2]);
                mot(r, "de", &r->e[k + 2]);
                expression(r, k + 3, fin0);
            } else {
                emettre(r, J_PAR_OUV, NULL, &r->e[k + 2], 1);
                expression(r, k + 2, fin0);
                emettre(r, J_PAR_FERM, NULL, &r->e[fin0], 1);
            }
            mot(r, "triés", t);
            if (champ) {
                mot(r, "par", t);
                if (calcul) { mot(r, "le", t); mot(r, "calcul", t); }
                copier(r, champ);
                if (decroissant) mot(r, "décroissant", t);
            } else if (decroissant) {
                mot(r, "par", t);
                mot(r, "ordre", t);
                mot(r, "décroissant", t);
            }
            k = fin;
            continue;
        }
        if (est_cle(t, "texte_suivant") && k + 1 < f && r->e[k + 1].type == J_GABARIT_DEBUT) {
            /* _texte_suivant et son bloc → le texte suivant : et le même bloc, chaque accolade relue (§ 4.7) */
            mot(r, "le", t);
            mot(r, "texte", t);
            mot(r, "suivant", t);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            size_t q = k + 1;
            for (; q < f && r->e[q].type != J_GABARIT_FIN; q++) {
                if (r->e[q].type != J_ACC_OUV) { copier(r, &r->e[q]); continue; }
                size_t g = q + 1;
                while (g < f && r->e[g].type != J_ACC_FERM) g++;
                copier(r, &r->e[q]);
                expression(r, q + 1, g);
                if (g < f) copier(r, &r->e[g]);
                q = g;
            }
            if (q < f) copier(r, &r->e[q]);
            k = q;
            continue;
        }
        if ((est_cle(t, "clés") || est_cle(t, "valeurs")) && k + 3 < f && r->e[k + 1].type == J_PAR_OUV
            && r->e[k + 2].type == J_CROCHETS && r->e[k + 3].type == J_PAR_FERM) {
            /* _clés(tarifs) → les clés des tarifs ; _clés(tarifs) _contient x → x est parmi les clés des tarifs (§ 13.11) */
            if (k + 4 < f && est_cle(&r->e[k + 4], "contient")) {
                size_t e = k + 5;
                int p = 0;
                while (e < f && !(p == 0 && r->e[e].type == J_MOT_CLE
                                  && (!strcmp(r->e[e].valeur, "et") || !strcmp(r->e[e].valeur, "ou")))) {
                    if (r->e[e].type == J_PAR_OUV) p++;
                    else if (r->e[e].type == J_PAR_FERM) p--;
                    e++;
                }
                expression(r, k + 5, e);
                mot(r, "est", &r->e[k + 4]);
                mot(r, "parmi", &r->e[k + 4]);
                mot(r, "les", t);
                mot(r, t->valeur, t);
                mot(r, "des", t);
                copier(r, &r->e[k + 2]);
                k = e - 1;
                continue;
            }
            if (!(k > 0 && est_cle(&r->e[k - 1], "parmi"))) mot(r, "les", t);   /* « _parmi » a déjà écrit « parmi les » */
            mot(r, t->valeur, t);
            mot(r, "des", t);
            copier(r, &r->e[k + 2]);
            k += 3;
            continue;
        }
        if (est_cle(t, "valeur") && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* _valeur(k ; tarifs) → la valeur de (k) dans les tarifs (§ 13.11) */
            size_t fin = fermante(r, k + 1, f), pv = 0;
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            int p = 0, nb = 0;
            for (size_t q = k + 2; q < fin; q++) {
                if (r->e[q].type == J_PAR_OUV) p++;
                else if (r->e[q].type == J_PAR_FERM) p--;
                else if (p == 0 && r->e[q].type == J_POINT_VIRGULE) { pv = q; nb++; }
            }
            if (nb != 1 || pv == k + 2 || pv + 2 != fin || r->e[pv + 1].type != J_CROCHETS) {
                echouer(r, t, grym_dupliquer("« _valeur » s'écrit ainsi : _valeur(« clé » ; tarifs)."));
                return;
            }
            mot(r, "la", t);
            mot(r, "valeur", t);
            mot(r, "de", t);
            emettre(r, J_PAR_OUV, NULL, &r->e[k + 2], 1);
            expression(r, k + 2, pv);
            emettre(r, J_PAR_FERM, NULL, &r->e[pv], 1);
            mot(r, "dans", &r->e[pv]);
            mot(r, "les", &r->e[pv]);
            copier(r, &r->e[pv + 1]);
            k = fin;
            continue;
        }
        if (est_cle(t, "après") || est_cle(t, "avant")) {   /* r _après « FA » → r après « FA » (§ 4.6) */
            mot(r, t->valeur, t);
            continue;
        }
        if (est_cle(t, "sans")) {   /* numéro_AVS _sans « . » → le numéro AVS sans « . » (2.64) */
            mot(r, "sans", t);
            continue;
        }
        if (est_cle(t, "réponse_port")) {   /* _réponse_port p _question « FA; » _vitesse 4800 _bauds (2.82) */
            mot(r, "la", t);
            mot(r, "réponse", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "question")) {   /* … → la réponse de p à « FA; », à 4800 bauds (2.82) */
            mot(r, "à", t);
            continue;
        }
        if (est_cle(t, "vitesse")) {
            emettre(r, J_VIRGULE, NULL, t, 1);
            mot(r, "à", t);
            continue;
        }
        if (est_cle(t, "bauds")) {
            mot(r, "bauds", t);
            continue;
        }
        if (est_cle(t, "ports_série")) {   /* _pour_chaque p _parmi _ports_série → Pour chaque p parmi les ports série : (2.82) */
            mot(r, "ports", t);
            mot(r, "série", t);
            continue;
        }
        if (est_cle(t, "remplaçant")) {   /* t _remplaçant « a » _par « b » → t en remplaçant « a » par « b » (2.81) */
            mot(r, "en", t);
            mot(r, "remplaçant", t);
            continue;
        }
        if (est_cle(t, "par")) {   /* … _remplaçant « a » _par « b » (2.81) */
            mot(r, "par", t);
            continue;
        }
        if (est_cle(t, "nombre_mots")) {   /* _nombre_mots t → le nombre de mots de t (2.81) */
            mot(r, "le", t);
            mot(r, "nombre", t);
            mot(r, "de", t);
            mot(r, "mots", t);
            mot(r, "de", t);
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
        if (est_cle(t, "sorte")) {                  /* _sorte → la sorte de l'échec (§ 18.2) */
            mot(r, "la", t);
            mot(r, "sorte", t);
            mot(r, "de", t);
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "échec", t);
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
             || est_cle(t, "écart_ans")
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
                if (!strcmp(u, "années") || !strcmp(u, "ans") || !strcmp(u, "heures")) emettre(r, J_ELISION, "d", t, 1); else mot(r, "de", t);
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
        {
            /* Découper un texte (§ 4.6) : _caractère(t ; 3) → le 3e caractère de (t) ; _caractère(t ; n + 1) → le caractère de
             * rang (n + 1) de (t) ; _premier_caractère(t), _dernier_caractère(t) ; _premiers_caractères(t ; 5) → les (5) premiers
             * caractères de (t) ; _caractères(t ; 3 ; 7) → les caractères (3) à (7) de (t) ; _position(x ; t) → la position de
             * (x) dans (t) ; _morceaux(t ; « ; ») → les morceaux de (t) séparés par (« ; »). Parenthèses synthétiques. */
            static const struct { const char *cle; size_t nb; } F[] = {
                { "caractère", 2 }, { "premier_caractère", 1 }, { "dernier_caractère", 1 }, { "premiers_caractères", 2 },
                { "derniers_caractères", 2 }, { "caractères", 3 }, { "position", 2 }, { "morceaux", 2 } };
            size_t qf = sizeof F / sizeof *F;
            for (size_t q = 0; q < sizeof F / sizeof *F; q++) if (est_cle(t, F[q].cle)) qf = q;
            if (qf < sizeof F / sizeof *F && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
                size_t fin = fermante(r, k + 1, f);
                if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
                size_t bornes[4] = { k + 1, 0, 0, 0 }, nb = 0;   /* bornes[i] : le jeton qui précède l'argument i */
                int p = 0, vide = 0;
                for (size_t q = k + 2; q <= fin; q++) {
                    if (q < fin && r->e[q].type == J_PAR_OUV) p++;
                    else if (q < fin && r->e[q].type == J_PAR_FERM) p--;
                    else if (q == fin || (p == 0 && r->e[q].type == J_POINT_VIRGULE)) {
                        if (q == bornes[nb] + 1) vide = 1;
                        if (++nb < 4) bornes[nb] = q;
                        if (nb >= 4) break;
                    }
                }
                if (nb != F[qf].nb || vide) {
                    static const char *const MODELE[] = { "_caractère(t ; 3)", "_premier_caractère(t)", "_dernier_caractère(t)",
                                                          "_premiers_caractères(t ; 5)", "_derniers_caractères(t ; 3)",
                                                          "_caractères(t ; 3 ; 7)", "_position(« x » ; t)", "_morceaux(t ; « ; »)" };
                    echouer(r, t, grym_formater("« _%s » s'écrit ainsi : %s.", F[qf].cle, MODELE[qf]));
                    return;
                }
                #define ARG(i) do { emettre(r, J_PAR_OUV, NULL, &r->e[bornes[i] + 1], 1); expression(r, bornes[i] + 1, bornes[(i) + 1]); \
                                    emettre(r, J_PAR_FERM, NULL, &r->e[bornes[(i) + 1]], 1); } while (0)
                const char *c = F[qf].cle;
                const Jeton *k1 = nb >= 2 ? &r->e[bornes[1] + 1] : NULL;
                const int ordinal = !strcmp(c, "caractère") && bornes[1] + 2 == bornes[2] && k1->type == J_NOMBRE && k1->valeur
                                    && strspn(k1->valeur, "0123456789") == strlen(k1->valeur) && strtol(k1->valeur, NULL, 10) >= 1;
                if (!strcmp(c, "caractère") && ordinal) {
                    char *o = grym_formater("%s%s", k1->valeur, strcmp(k1->valeur, "1") == 0 ? "er" : "e");
                    mot(r, "le", t);
                    emettre(r, J_ORDINAL, o, k1, 1);
                    free(o);
                    mot(r, "caractère", t);
                    mot(r, "de", t);
                    ARG(0);
                } else if (!strcmp(c, "caractère")) {
                    mot(r, "le", t); mot(r, "caractère", t); mot(r, "de", t); mot(r, "rang", t);
                    ARG(1);
                    mot(r, "de", t);
                    ARG(0);
                } else if (!strcmp(c, "premier_caractère") || !strcmp(c, "dernier_caractère")) {
                    mot(r, "le", t);
                    mot(r, c[0] == 'p' ? "premier" : "dernier", t);
                    mot(r, "caractère", t);
                    mot(r, "de", t);
                    ARG(0);
                } else if (!strcmp(c, "premiers_caractères") || !strcmp(c, "derniers_caractères")) {
                    mot(r, "les", t);
                    ARG(1);
                    mot(r, c[0] == 'p' ? "premiers" : "derniers", t);
                    mot(r, "caractères", t);
                    mot(r, "de", t);
                    ARG(0);
                } else if (!strcmp(c, "caractères")) {
                    mot(r, "les", t); mot(r, "caractères", t);
                    ARG(1);
                    mot(r, "à", t);
                    ARG(2);
                    mot(r, "de", t);
                    ARG(0);
                } else if (!strcmp(c, "position")) {
                    mot(r, "la", t); mot(r, "position", t); mot(r, "de", t);
                    ARG(0);
                    mot(r, "dans", t);
                    ARG(1);
                } else {   /* morceaux */
                    mot(r, "les", t); mot(r, "morceaux", t); mot(r, "de", t);
                    ARG(0);
                    mot(r, "séparés", t); mot(r, "par", t);
                    ARG(1);
                }
                #undef ARG
                k = fin;
                continue;
            }
        }
        if ((est_cle(t, "élément") || est_cle(t, "premier") || est_cle(t, "dernier")) && k + 1 < f && r->e[k + 1].type == J_PAR_OUV) {
            /* _élément(étapes ; 3) → le 3e élément des étapes ; _élément(étapes ; n + 1) → l'élément de rang n + 1 des étapes ;
             * _premier(étapes), _dernier(étapes) → le premier élément des étapes (§ 13.10) */
            const int rang = est_cle(t, "élément");
            size_t fin = fermante(r, k + 1, f);
            if (fin == f) { echouer(r, &r->e[k + 1], grym_dupliquer("Parenthèse fermante manquante.")); return; }
            const size_t pv = k + 3;
            if (k + 2 >= fin || r->e[k + 2].type != J_CROCHETS
                || (rang ? (pv >= fin || r->e[pv].type != J_POINT_VIRGULE || pv + 1 == fin) : k + 3 != fin)) {
                echouer(r, t, grym_formater(rang ? "« _élément » prend la liste et le rang : _élément(étapes ; 3)."
                                                 : "« _%s » prend la liste seule : _%s(étapes).", t->valeur, t->valeur));
                return;
            }
            const Jeton *k1 = rang ? &r->e[pv + 1] : NULL;
            const int ordinal = rang && pv + 2 == fin && k1->type == J_NOMBRE && k1->valeur
                                && strspn(k1->valeur, "0123456789") == strlen(k1->valeur) && strtol(k1->valeur, NULL, 10) >= 1;
            if (!rang) {   /* le premier élément */
                mot(r, "le", t);
                mot(r, t->valeur, t);
                mot(r, "élément", t);
            } else if (ordinal) {   /* un rang constant revient en chiffres : le 3e élément */
                char *o = grym_formater("%s%s", k1->valeur, strcmp(k1->valeur, "1") == 0 ? "er" : "e");
                mot(r, "le", t);
                emettre(r, J_ORDINAL, o, k1, 1);
                free(o);
                mot(r, "élément", t);
            } else {   /* un rang calculé : l'élément de rang n + 1 */
                emettre(r, J_ELISION, "l", t, 1);
                mot(r, "élément", t);
                mot(r, "de", t);
                mot(r, "rang", t);
                expression(r, pv + 1, fin);
            }
            mot(r, "des", &r->e[k + 2]);
            copier(r, &r->e[k + 2]);
            k = fin;
            continue;
        }
        if (est_cle(t, "mots")) {   /* _mots t → les mots de t (§ 13.10, 2.81) */
            mot(r, "les", t);
            mot(r, "mots", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "éléments") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            /* _éléments « collection » r → les éléments du champ « collection » de r (§ 13.10, 2.60) */
            mot(r, "les", t);
            mot(r, "éléments", t);
            mot(r, "du", t);
            mot(r, "champ", t);
            copier(r, &r->e[k + 1]);
            mot(r, "de", t);
            k++;
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
                                             "vrai_ou_faux", "heure", "toutes_lettres", "format_technique", "iban",
                                             "date_locale", "heure_locale", "format_universel", "fichier" };   /* 2.60, 2.83 */
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
                mot(r, w, t);   /* « iban » : l'imprimeur l'écrit en capitales */
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
        if (est_cle(t, "saut_de_ligne")) {           /* _saut_de_ligne → un saut de ligne (2.27) */
            mot(r, "un", t);
            mot(r, "saut", t);
            mot(r, "de", t);
            mot(r, "ligne", t);
            continue;
        }
        if (est_cle(t, "xml")) {                     /* _xml facture → le XML de facture (2.46) */
            mot(r, "le", t);
            mot(r, "xml", t);
            mot(r, "de", t);
            if (k + 2 < f && r->e[k + 1].type == J_CROCHETS && r->e[k + 2].type == J_PAR_OUV) {
                /* _xml quittance(f) → le XML de quittance de (f) (2.63) : parenthèses synthétiques */
                size_t fin = fermante(r, k + 2, f);
                if (fin == f) { echouer(r, &r->e[k + 2], grym_dupliquer("« ) » attendu.")); return; }
                copier(r, &r->e[k + 1]);
                arguments(r, k + 3, fin, 1, &r->e[k + 2]);
                k = fin;
            }
            continue;
        }
        if (est_cle(t, "envoi")) {                   /* _envoi « url » → l'envoi à « url » (2.42) */
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "envoi", t);
            mot(r, "à", t);
            continue;
        }
        if (est_cle(t, "lecture")) {                 /* _lecture « url » → la lecture de « url » (2.42) */
            mot(r, "la", t);
            mot(r, "lecture", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "nombre_éléments") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            /* _nombre_éléments « a » r → le nombre d'éléments du champ « a » de r (2.60) */
            mot(r, "le", t);
            mot(r, "nombre", t);
            emettre(r, J_ELISION, "d", t, 1);
            mot(r, "éléments", t);
            mot(r, "du", t);
            mot(r, "champ", t);
            copier(r, &r->e[k + 1]);
            mot(r, "de", t);
            k++;
            continue;
        }
        if (est_cle(t, "champ") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {   /* _champ « a.b » r → le champ « a.b » de r */
            mot(r, "le", t);
            mot(r, "champ", t);
            copier(r, &r->e[k + 1]);
            mot(r, "de", t);
            k++;
            continue;
        }
        if (est_cle(t, "décodages_wsjt")) {          /* _décodages_wsjt [p] → les décodages WSJT [du port p] (2.88) */
            if (!(k > 0 && est_cle(&r->e[k - 1], "parmi"))) mot(r, "les", t);   /* « _parmi » a déjà écrit « parmi les » */
            mot(r, "décodages", t);
            mot(r, "wsjt", t);
            if (k + 1 < f && (r->e[k + 1].type == J_NOMBRE || r->e[k + 1].type == J_PAR_OUV)) {
                mot(r, "du", t);
                mot(r, "port", t);
            }
            continue;
        }
        if (est_cle(t, "langue")) {                  /* _langue t → la langue reconnue de t (2.86) */
            mot(r, "la", t);
            mot(r, "langue", t);
            mot(r, "reconnue", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "historique_base")) {         /* _historique_base → l'historique de la base (2.85) */
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "historique", t);
            mot(r, "de", t);
            mot(r, "la", t);
            mot(r, "base", t);
            continue;
        }
        if (est_cle(t, "historique")) {              /* _historique x → l'historique de x (2.85) */
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "historique", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "dernière_sauvegarde")) {     /* _dernière_sauvegarde « p » → la dernière sauvegarde de « p » (2.85) */
            mot(r, "la", t);
            mot(r, "dernière", t);
            mot(r, "sauvegarde", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "code_qr") || est_cle(t, "code_qr_suisse")) {   /* _code_qr_suisse t → le code QR suisse de t (2.27) */
            mot(r, "le", t);
            mot(r, "code", t);
            mot(r, "qr", t);
            if (est_cle(t, "code_qr_suisse")) mot(r, "suisse", t);
            mot(r, "de", t);
            continue;
        }
        if (est_cle(t, "document")) {                /* _document de_quittance → le document de quittance (2.25) */
            mot(r, "le", t);
            mot(r, "document", t);
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
                       || strcmp(t->valeur, "absent") == 0 || strcmp(t->valeur, "absente") == 0) {   /* _absente (2.36) */
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
                if (!op && k + 2 < fin) {   /* _non (_les genres _de o _contient x) → x n'est pas parmi les genres de o (3.19) */
                    const size_t c = contient_general(r, k + 2, fin);
                    if (c < fin) {
                        expression(r, c + 1, fin_droite(r, c + 1, fin));
                        emettre(r, J_ELISION, "n", &r->e[c], 1);
                        mot(r, "est", &r->e[c]);
                        mot(r, "pas", &r->e[c]);
                        mot(r, "parmi", &r->e[c]);
                        expression(r, k + 2, c);
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
                if (!op && k + 7 < fin && (est_cle(&r->e[k + 2], "clés") || est_cle(&r->e[k + 2], "valeurs"))
                    && r->e[k + 3].type == J_PAR_OUV && r->e[k + 4].type == J_CROCHETS && r->e[k + 5].type == J_PAR_FERM
                    && est_cle(&r->e[k + 6], "contient")) {
                    /* _non (_clés(tarifs) _contient x) → x n'est pas parmi les clés des tarifs (§ 13.11) */
                    expression(r, k + 7, fin);
                    emettre(r, J_ELISION, "n", &r->e[k + 6], 1);
                    mot(r, "est", &r->e[k + 6]);
                    mot(r, "pas", &r->e[k + 6]);
                    mot(r, "parmi", &r->e[k + 6]);
                    mot(r, "les", &r->e[k + 6]);
                    mot(r, r->e[k + 2].valeur, &r->e[k + 2]);
                    mot(r, "des", &r->e[k + 2]);
                    copier(r, &r->e[k + 4]);
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

static int profondeur_corps(const Reecriture *r);
static void ouvrir(Reecriture *r, Ouverture type, int profondeur, const Jeton *mot_);

static void point(Reecriture *r, size_t f) {
    if (r->envoi_ouvrant) {   /* « _envoi « … » _avec » : le bloc des valeurs suit, la phrase finira avec lui (2.43) */
        const int o = r->envoi_ouvrant;
        const Jeton *x = &r->e[f];
        r->envoi_ouvrant = 0;
        if (o == 1) mot(r, "avec", x);
        else if (o == 2) mot(r, "de", x);
        else { mot(r, "du", x); mot(r, "tableau", x); }
        emettre(r, J_DEUX_POINTS, NULL, x, 1);
        ouvrir(r, O_ENVOI, profondeur_corps(r), x);
        r->pile[r->np - 1].dans_cas = o == 3;   /* des valeurs seules, ou des paires */
        return;
    }
    emettre(r, J_POINT, NULL, &r->e[f - 1], 1);
}

/* Paramètres « _un x ; _une y » entre d et f. */
static void parametres(Reecriture *r, size_t d, size_t f, int de) {
    size_t k = d;
    int premier = 1;
    r->vu_facultatif = 0;
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
        if (k + 2 < f && r->e[k + 2].type == J_AFFECTE) {
            /* « _un délai << 30 » → , avec un délai de 30 (2.98) : la valeur va jusqu'au « ; » suivant */
            size_t v = k + 3;
            while (v < f && r->e[v].type != J_POINT_VIRGULE) v++;
            if (v == k + 3) { echouer(r, &r->e[k + 2], grym_dupliquer("Valeur de départ attendue après « << »."));  return; }
            if (!r->vu_facultatif) {
                emettre(r, J_VIRGULE, NULL, t, 1);
                mot(r, "avec", t);
            } else mot(r, "et", t);
            r->vu_facultatif = 1;
            mot(r, t->valeur, t);
            copier(r, &r->e[k + 1]);
            mot(r, "de", &r->e[k + 2]);
            expression(r, k + 3, v);
            premier = 0;
            k = v;
            if (k < f) k++;
            continue;
        }
        if (r->vu_facultatif) { echouer(r, t, grym_dupliquer("Les facultatifs viennent après les paramètres : « _un client ; _un délai << 30 »."));  return; }
        if (!premier) mot(r, "et", t);
        if (de) emettre(r, J_ELISION, "d", t, 1);
        mot(r, t->valeur, t);
        copier(r, &r->e[k + 1]);
        premier = 0;
        k += 2;
        if (k + 2 < f && r->e[k].type == J_PAR_OUV && r->e[k + 1].type == J_CROCHETS && r->e[k + 2].type == J_PAR_FERM) {
            copier(r, &r->e[k]);   /* « _un délai (nombre) » : son type (3.00) */
            copier(r, &r->e[k + 1]);
            copier(r, &r->e[k + 2]);
            k += 3;
        }
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
        || est_cle(t, "interrupteur") || est_cle(t, "choix_dossier") || est_cle(t, "largeur_caractères") || est_cle(t, "indication") || est_cle(t, "recherche") || est_cle(t, "min") || est_cle(t, "max") || est_cle(t, "masqué") || est_cle(t, "unité") || est_cle(t, "transcrit")
        || est_cle(t, "lecture_seule") || est_cle(t, "place") || est_cle(t, "largeur") || est_cle(t, "hauteur")
        || est_cle(t, "principal") || est_cle(t, "tuile") || est_cle(t, "description") || est_cle(t, "image")
        || est_cle(t, "côté_titre") || est_cle(t, "inversé") || est_cle(t, "capitales") || est_cle(t, "confirmer")
        || est_cle(t, "en_tapant") || est_cle(t, "si_modifié") || est_cle(t, "clavier") || est_cle(t, "destructif") || est_cle(t, "ferme")
        || est_cle(t, "enregistre_ferme") || est_cle(t, "non_lié");   /* 3.25 */
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
        } else if (est_cle(x, "largeur_caractères")) {   /* « _largeur_caractères 12 » → « , large de 12 caractères » (2.34) */
            if (k + 1 >= f || r->e[k + 1].type != J_NOMBRE) {
                echouer(r, x, grym_dupliquer("Nombre de caractères attendu : « _largeur_caractères 12 »."));
                return 0;
            }
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "large", x);
            mot(r, "de", x);
            copier(r, &r->e[k + 1]);
            mot(r, "caractères", x);
            k += 2;
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
        } else if (zone && est_cle(x, "non_lié")) {   /* _non_lié → , non lié (une zone, 3.25) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "non", x);
            mot(r, feminin ? "liée" : "lié", x);
            k++;
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
        } else if (zone && est_cle(x, "transcrit") && k + 1 < f && r->e[k + 1].type == J_CROCHETS) {
            /* _transcrit mémo → , transcrit de mémo (§ 15.8 ; synthétique : accord non vérifié) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, feminin ? "transcrite" : "transcrit", x);
            emettre(r, J_ELISION, "d", x, 1);
            copier(r, &r->e[k + 1]);
            k += 2;
        } else if (zone && est_cle(x, "masqué")) {   /* _masqué → , masqué / masquée (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, feminin ? "masquée" : "masqué", x);
            k++;
        } else if (zone && est_cle(x, "recherche")) {   /* _recherche → , avec recherche (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "avec", x);
            mot(r, "recherche", x);
            k++;
        } else if (zone && est_cle(x, "choix_dossier")) {   /* _choix_dossier → , avec le choix d'un dossier (2.32) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "avec", x);
            mot(r, "le", x);
            mot(r, "choix", x);
            emettre(r, J_ELISION, "d", x, 1);
            mot(r, "un", x);
            mot(r, "dossier", x);
            k++;
        } else if (zone && est_cle(x, "interrupteur")) {   /* _interrupteur → , en interrupteur (§ 22.6) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "en", x);
            mot(r, "interrupteur", x);
            k++;
        } else if (!zone && est_cle(x, "capitales")) {   /* _capitales → , en petites capitales (un texte, 2.20) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "en", x); mot(r, "petites", x); mot(r, "capitales", x);
            k++;
        } else if (!zone && est_cle(x, "inversé")) {   /* _inversé → , en couleur inversée (un groupe, 2.18) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "en", x); mot(r, "couleur", x); mot(r, "inversée", x);
            k++;
        } else if (!zone && est_cle(x, "côté_titre")) {   /* _côté_titre → , à côté du titre (un bouton, 2.17) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "à", x); mot(r, "côté", x); mot(r, "du", x); mot(r, "titre", x);
            k++;
        } else if (!zone && est_cle(x, "tuile")) {   /* _tuile → , en tuile (un bouton, 2.15) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "en", x);
            mot(r, "tuile", x);
            k++;
        } else if (!zone && est_cle(x, "description") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            emettre(r, J_VIRGULE, NULL, x, 1);   /* _description « … » → , avec la description « … » (2.15) */
            mot(r, "avec", x);
            mot(r, "la", x);
            mot(r, "description", x);
            copier(r, &r->e[k + 1]);
            k += 2;
        } else if (!zone && est_cle(x, "confirmer") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            emettre(r, J_VIRGULE, NULL, x, 1);   /* _confirmer « … » → , à confirmer par « … » (un bouton, 2.65) */
            mot(r, "à", x);
            mot(r, "confirmer", x);
            mot(r, "par", x);
            copier(r, &r->e[k + 1]);
            k += 2;
        } else if (!zone && est_cle(x, "enregistre_ferme")) {   /* _enregistre_ferme → , qui enregistre et ferme l'écran (3.25) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "qui", x);
            mot(r, "enregistre", x);
            mot(r, "et", x);
            mot(r, "ferme", x);
            emettre(r, J_ELISION, "l", x, 1);
            mot(r, "écran", x);
            k++;
        } else if (!zone && est_cle(x, "ferme")) {   /* _ferme → , qui ferme l'écran (un bouton, 3.22) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "qui", x);
            mot(r, "ferme", x);
            emettre(r, J_ELISION, "l", x, 1);
            mot(r, "écran", x);
            k++;
        } else if (!zone && est_cle(x, "destructif")) {   /* _destructif → , destructif (un bouton, 2.72) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "destructif", x);
            k++;
        } else if (!zone && est_cle(x, "clavier") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            emettre(r, J_VIRGULE, NULL, x, 1);   /* _clavier « N » → , au clavier « N » (un bouton, 2.75) */
            mot(r, "au", x);
            mot(r, "clavier", x);
            copier(r, &r->e[k + 1]);
            k += 2;
        } else if (!zone && est_cle(x, "en_tapant") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            mot(r, "en", x);   /* _en_tapant « effacer » → en tapant « effacer » (2.65), à la suite de la confirmation */
            mot(r, "tapant", x);
            copier(r, &r->e[k + 1]);
            k += 2;
        } else if (!zone && est_cle(x, "si_modifié")) {
            mot(r, "si", x);   /* _si_modifié → si l'écran est modifié (2.67), à la suite de la confirmation */
            emettre(r, J_ELISION, "l", x, 1);
            mot(r, "écran", x);
            mot(r, "est", x);
            mot(r, "modifié", x);
            k += 1;
        } else if (!zone && est_cle(x, "image") && k + 1 < f && r->e[k + 1].type == J_TEXTE) {
            emettre(r, J_VIRGULE, NULL, x, 1);   /* _image « … » → , avec l'image « … » (2.15) */
            mot(r, "avec", x);
            emettre(r, J_ELISION, "l", x, 1);
            mot(r, "image", x);
            copier(r, &r->e[k + 1]);
            k += 2;
        } else if (!zone && est_cle(x, "principal")) {   /* _principal → , principal (un bouton, 2.14) */
            emettre(r, J_VIRGULE, NULL, x, 1);
            mot(r, "principal", x);
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
        /* une remarque entre deux « _cas » d'un « _selon », alignée sur eux : au niveau des cas, pas dans le corps du
         * précédent (2.51) */
        if (haut && haut->type == O_SELON && haut->dans_cas && f < r->n && r->e[f].type == J_MOT_CLE
            && (est_cle(&r->e[f], "cas") || est_cle(&r->e[f], "autrement")) && r->e[f].retrait == t->retrait && prof > 0)
            fixer_retrait(r, premier, prof - 1);
        else fixer_retrait(r, premier, prof);
        return;
    }
    /* le XML déclaré (2.46) : « _xml facture « invoice:request » », puis « _attribut », « _contenu », « _élément » ; « _fin » */
    /* « _xml quittance(_une chose_tarifée) « quittance » » (2.63) : ses paramètres, comme ceux d'un calcul */
    const size_t xml_par = t->type == J_MOT_CLE && est_cle(t, "xml") && f >= d + 5 && r->e[d + 1].type == J_CROCHETS
                           && r->e[d + 2].type == J_PAR_OUV ? fermante(r, d + 2, f) : f;
    if (xml_par + 2 == f && r->e[f - 1].type == J_TEXTE) {
        mot(r, "le", t);
        mot(r, "xml", t);
        mot(r, "de", t);
        copier(r, &r->e[d + 1]);
        parametres(r, d + 3, xml_par, 1);
        emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
        copier(r, &r->e[f - 1]);
        emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
        mot(r, "montre", &r->e[f - 1]);
        emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_XML, prof, t);
        return;
    }
    if (t->type == J_MOT_CLE && est_cle(t, "xml") && f == d + 3 && r->e[d + 1].type == J_CROCHETS && r->e[d + 2].type == J_TEXTE) {
        mot(r, "le", t);
        mot(r, "xml", t);
        mot(r, "de", t);
        copier(r, &r->e[d + 1]);
        emettre(r, J_VIRGULE, NULL, &r->e[d + 2], 1);
        copier(r, &r->e[d + 2]);
        emettre(r, J_VIRGULE, NULL, &r->e[d + 2], 1);
        mot(r, "montre", &r->e[d + 2]);
        emettre(r, J_DEUX_POINTS, NULL, &r->e[d + 2], 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_XML, prof, t);
        return;
    }
    int dans_xml = 0;
    for (size_t q = 0; q < r->np; q++) dans_xml |= r->pile[q].type == O_XML;
    if (dans_xml && t->type == J_MOT_CLE && (est_cle(t, "attribut") || est_cle(t, "contenu") || est_cle(t, "élément"))) {
        if (est_cle(t, "contenu")) {
            mot(r, "le", t);
            mot(r, "contenu", t);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            expression(r, d + 1, f);
            emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
            fixer_retrait(r, premier, prof);
            return;
        }
        if (f < d + 2 || r->e[d + 1].type != J_TEXTE) { echouer(r, t, grym_dupliquer("Un nom XML entre guillemets suit « _attribut » ou « _élément »."));
                                                         return; }
        emettre(r, J_ELISION, "l", t, 1);
        mot(r, est_cle(t, "attribut") ? "attribut" : "élément", t);
        copier(r, &r->e[d + 1]);
        if (est_cle(t, "élément") && f == d + 2) emettre(r, J_VIRGULE, NULL, &r->e[d + 1], 1);
        else if (est_cle(t, "élément") && f == d + 3 && est_cle(&r->e[d + 2], "enfants")) {
            emettre(r, J_DEUX_POINTS, NULL, &r->e[d + 2], 1);
            fixer_retrait(r, premier, prof);
            ouvrir(r, O_XML, prof, t);   /* ses enfants : un bloc du XML, fermé par « _fin » */
            return;
        } else {
            emettre(r, J_DEUX_POINTS, NULL, &r->e[d + 1], 1);
            expression(r, d + 2, f);
            emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
        }
        fixer_retrait(r, premier, prof);
        return;
    }
    /* le bloc d'un envoi (2.43) : « « clé » : valeur » ou une valeur par ligne ; « _et_de », « _et_du_tableau » ; « _fin » */
    if (haut && haut->type == O_ENVOI) {
        if (t->type == J_MOT_CLE && !strcmp(t->valeur, "fin") && f == d + 1) {
            if (!r->ns || r->s[r->ns - 1].type != J_VIRGULE) { echouer(r, t, grym_dupliquer("Un envoi porte au moins une valeur.")); return; }
            r->s[r->ns - 1].type = J_POINT;   /* la dernière valeur finit la phrase */
            if (r->s[r->ns - 1].ligne_fin < t->ligne) r->s[r->ns - 1].ligne_fin = t->ligne;
            r->np--;
            return;
        }
        if (f == d + 1 && (est_cle(t, "et_de") || est_cle(t, "et_du_tableau"))) {
            mot(r, "et", t);
            if (est_cle(t, "et_de")) mot(r, "de", t);
            else { mot(r, "du", t); mot(r, "tableau", t); }
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            fixer_retrait(r, premier, haut->profondeur);
            haut->dans_cas = est_cle(t, "et_du_tableau");
            return;
        }
        if (!haut->dans_cas && est_cle(t, "entête") && f >= d + 4 && r->e[d + 1].type == J_TEXTE && r->e[d + 2].type == J_DEUX_POINTS) {
            /* _entête « Authorization » : v → l'en-tête « Authorization » : v (2.60) */
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "en", t);
            emettre(r, J_MOINS, NULL, t, 1);
            mot(r, "tête", t);
            copier(r, &r->e[d + 1]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[d + 2], 1);
            expression(r, d + 3, f);
            emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
            fixer_retrait(r, premier, prof);
            return;
        }
        if (!haut->dans_cas) {
            if (t->type != J_TEXTE || f < d + 3 || r->e[d + 1].type != J_DEUX_POINTS) {
                echouer(r, t, grym_dupliquer("Une valeur envoyée s'écrit « « clé » : valeur »."));
                return;
            }
            copier(r, t);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[d + 1], 1);
            expression(r, d + 2, f);
        } else expression(r, d, f);
        emettre(r, J_VIRGULE, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        return;
    }
    /* « … _envoi « … » _avec » : la phrase ouvre le bloc de ses valeurs (2.43) */
    if (f > d + 1 && r->e[f - 1].type == J_MOT_CLE
        && (est_cle(&r->e[f - 1], "avec") || est_cle(&r->e[f - 1], "de") || est_cle(&r->e[f - 1], "du_tableau"))) {
        int envoi = 0;
        for (size_t k = d; k < f - 1; k++) envoi |= est_cle(&r->e[k], "envoi") || est_cle(&r->e[k], "lecture");
        if (envoi) {
            r->envoi_ouvrant = est_cle(&r->e[f - 1], "avec") ? 1 : est_cle(&r->e[f - 1], "de") ? 2 : 3;
            f--;
        }
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
            const size_t ici = r->ns;
            expression(r, d + 1, g);
            if (r->ns > ici && r->s[ici].type == J_MOT && !strcmp(r->s[ici].valeur, "le")) {   /* « de le fichier » → « du fichier » (2.51) */
                free(r->s[ici - 1].valeur);
                r->s[ici - 1].valeur = grym_dupliquer("du");
                free(r->s[ici].valeur);
                memmove(&r->s[ici], &r->s[ici + 1], (r->ns - ici - 1) * sizeof *r->s);
                r->ns--;
            }
            if (!etats_compacts(r, g, f, 1, 0)) return;
        } else if (est_cle(t, "écoute") && f >= d + 2 && r->e[d + 1].type == J_TEXTE) {
            /* _écoute « Stryker » _silence 3 → une écoute « Stryker », coupée après 3 secondes de silence (2.69) */
            mot(r, "une", t);
            mot(r, "écoute", t);
            copier(r, &r->e[d + 1]);
            size_t g = d + 2;
            if (g + 1 < f && est_cle(&r->e[g], "silence") && r->e[g + 1].type == J_NOMBRE) {
                emettre(r, J_VIRGULE, NULL, &r->e[g], 1);
                mot(r, "coupée", &r->e[g]);
                mot(r, "après", &r->e[g]);
                copier(r, &r->e[g + 1]);
                mot(r, strcmp(r->e[g + 1].valeur, "1") == 0 ? "seconde" : "secondes", &r->e[g]);
                mot(r, "de", &r->e[g]);
                mot(r, "silence", &r->e[g]);
                g += 2;
            }
            if (!etats_compacts(r, g, f, 1, 0)) return;
        } else if (est_cle(t, "image") && f >= d + 2 && r->e[d + 1].type == J_TEXTE && (f == d + 2 || est_etat(&r->e[d + 2]))) {
            emettre(r, J_ELISION, "l", t, 1);   /* _image « plan.png » → l'image « plan.png » (§ 22.8) */
            mot(r, "image", t);
            copier(r, &r->e[d + 1]);
            if (!etats_compacts(r, d + 2, f, 1, 0)) return;
        } else if (est_cle(t, "section_paiement") && f >= d + 2) {
            /* _section_paiement charge → la section paiement de la charge (2.28) */
            size_t fin = d + 1;
            while (fin < f && !est_etat(&r->e[fin])) fin++;
            mot(r, "la", t);
            mot(r, "section", t);
            mot(r, "paiement", t);
            mot(r, "de", t);
            expression(r, d + 1, fin);
            if (!etats_compacts(r, fin, f, 0, 0)) return;
        } else if (est_cle(t, "texte_calculé") && f >= d + 2) {
            /* _texte_calculé « Ville : » _suivi écran.ville → le texte « Ville : » suivi de la ville de l'écran (2.24) */
            size_t fin = d + 1;
            while (fin < f && !est_etat(&r->e[fin])) fin++;
            mot(r, "le", t);
            mot(r, "texte", t);
            expression(r, d + 1, fin);
            if (!etats_compacts(r, fin, f, 0, 0)) return;
        } else if (est_cle(t, "partie") && f == d + 2 && r->e[d + 1].type == J_TEXTE) {
            /* _partie « en-tête » → la partie « en-tête » (2.56) : l'analyse la déplie */
            mot(r, "la", t);
            mot(r, "partie", t);
            copier(r, &r->e[d + 1]);
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
        } else if ((est_cle(t, "liste") || est_cle(t, "écoutes")) && d + 2 < f && r->e[d + 1].type == J_CROCHETS
                   && (est_cle(&r->e[d + 2], "conservé") || est_cle(&r->e[d + 2], "conservée")
                       || est_cle(&r->e[d + 2], "supprimé") || est_cle(&r->e[d + 2], "supprimée"))) {
            /* _liste compositeur _conservé [_dont …] [_par nom [_décroissant]] [_avec titre ; compositeur.nom]
             * [_place 96 16] [_largeur 240] [_hauteur 80] (§ 22.9) */
            size_t fin_places = f;
            size_t cartes = f;   /* « _cartes » (2.19) : après les colonnes, avant la place */
            const int des_ecoutes = est_cle(t, "écoutes");   /* _écoutes poste _conservé … _silence 3 (2.70) */
            for (size_t q = d + 3; q < f; q++)
                if (est_cle(&r->e[q], "place") || est_cle(&r->e[q], "largeur") || est_cle(&r->e[q], "hauteur")) { f = q; break; }
            for (size_t q = d + 3; q < f; q++)
                if (est_cle(&r->e[q], "cartes") || est_cle(&r->e[q], "silence") || est_cle(&r->e[q], "agenda")) { cartes = q; break; }
            const size_t places = f;
            if (cartes < f) f = cartes;
            size_t avec = chercher(r, d + 3, f, "avec");
            size_t fin_liste = f;
            f = avec;
            size_t par = chercher(r, d + 3, f, "par");
            mot(r, des_ecoutes ? "les" : "la", t);
            mot(r, des_ecoutes ? "écoutes" : "liste", t);
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
                size_t q = par + 2;
                if (q < f && est_cle(&r->e[q], "décroissant")) { mot(r, "décroissant", &r->e[q]); q++; }
                if (q < f && est_cle(&r->e[q], "puis")) {   /* « _par date _puis heure » (2.66) */
                    if (q + 1 >= f || r->e[q + 1].type != J_CROCHETS) {
                        echouer(r, &r->e[q], grym_dupliquer("Second tri attendu : « _par date _puis heure »."));
                        return;
                    }
                    mot(r, "puis", &r->e[q]);
                    copier(r, &r->e[q + 1]);
                    q += 2;
                    if (q < f && est_cle(&r->e[q], "décroissant")) { mot(r, "décroissant", &r->e[q]); q++; }   /* 2.73 */
                }
                if (q < f) {
                    echouer(r, &r->e[q], grym_dupliquer("Tri attendu : « _par nom », « _par date _décroissant » ou « _par date _puis heure »."));
                    return;
                }
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
            if (cartes < places && est_cle(&r->e[cartes], "silence")) {   /* « _silence 3 » → « , coupées après 3 secondes de silence » */
                if (cartes + 1 >= places || r->e[cartes + 1].type != J_NOMBRE) {
                    echouer(r, &r->e[cartes], grym_dupliquer("Durée attendue : « _silence 3 »."));
                    return;
                }
                emettre(r, J_VIRGULE, NULL, &r->e[cartes], 1);
                mot(r, "coupées", &r->e[cartes]);
                mot(r, "après", &r->e[cartes]);
                copier(r, &r->e[cartes + 1]);
                mot(r, strcmp(r->e[cartes + 1].valeur, "1") == 0 ? "seconde" : "secondes", &r->e[cartes]);
                mot(r, "de", &r->e[cartes]);
                mot(r, "silence", &r->e[cartes]);
            } else if (cartes < places && est_cle(&r->e[cartes], "agenda")) {   /* « _agenda g » → « , en agenda selon g » (2.74) */
                if (cartes + 1 >= places) { echouer(r, &r->e[cartes], grym_dupliquer("« _agenda » attend la grille : « _agenda grille »."));
                                            return; }
                emettre(r, J_VIRGULE, NULL, &r->e[cartes], 1);
                mot(r, "en", &r->e[cartes]);
                mot(r, "agenda", &r->e[cartes]);
                mot(r, "selon", &r->e[cartes]);
                expression(r, cartes + 1, places);
            } else if (cartes < places) {   /* « _cartes » → « , en cartes » (2.19) */
                emettre(r, J_VIRGULE, NULL, &r->e[cartes], 1);
                mot(r, "en", &r->e[cartes]);
                mot(r, "cartes", &r->e[cartes]);
            }
            if (!etats_compacts(r, places, fin_places, 1, 0)) return;
            f = fin_places;
        } else if ((est_cle(t, "un") || est_cle(t, "une") || est_cle(t, "des")) && d + 4 < f + 1 && r->e[d + 1].type == J_CROCHETS
                   && r->e[d + 2].type == J_PAR_OUV
                   && ((r->e[d + 3].type == J_CROCHETS && (r->e[d + 4].type == J_PAR_FERM || est_cle(&r->e[d + 4], "dont")))
                       || r->e[d + 3].type == J_TEXTE)) {
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
            } else if (est_cle(&r->e[d + 4], "dont")) {   /* « (patient _dont archivage _absent) » (2.48) */
                const size_t fv = fermante(r, d + 2, f);
                if (fv == f) { echouer(r, &r->e[d + 2], grym_dupliquer("Parenthèse fermante manquante.")); return; }
                copier(r, &r->e[d + 3]);
                mot(r, "dont", &r->e[d + 4]);
                expression(r, d + 5, fv);
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
    if (!haut && est_cle(t, "partie") && f == d + 2 && r->e[d + 1].type == J_TEXTE) {
        /* _partie « en-tête » … _fin → La partie « en-tête » montre : (2.56), ses éléments comme ceux d'un écran */
        mot(r, "la", t);
        mot(r, "partie", t);
        copier(r, &r->e[d + 1]);
        mot(r, "montre", t);
        emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_ECRAN, prof, t);
        return;
    }
    const int document = t->type == J_MOT_CLE && !strcmp(t->valeur, "document") && d + 1 < f && r->e[d + 1].type == J_CROCHETS;
    if (document || (t->type == J_MOT_CLE && !strcmp(t->valeur, "écran") && !(d + 1 < f && r->e[d + 1].type == J_POINT))) {
        /* _écran des_compositeurs [« Titre »] → L'écran des compositeurs[, « Titre »,] montre : ;
         * _document de_quittance → Le document de quittance montre : (2.25) */
        /* _pour _un salarié (3.25) : à la fin de l'en-tête → « , pour un salarié, » */
        const size_t pour_trouve = chercher(r, d + 1, f, "pour");
        size_t pour = pour_trouve;
        if (pour < f && (document || pour + 2 >= f || !(est_cle(&r->e[pour + 1], "un") || est_cle(&r->e[pour + 1], "une")))) {
            echouer(r, &r->e[pour], grym_dupliquer("Forme attendue : « _écran du_salarié _pour _un salarié »."));
            return;
        }
        const size_t f_entete = f;
        if (pour < f) f = pour;
        size_t fin_nom = f;
        const Jeton *titre = NULL, *taille = NULL;
        if (f > d + 2 && (est_cle(&r->e[f - 1], "en_grand") || est_cle(&r->e[f - 1], "en_petit"))) { taille = &r->e[f - 1]; fin_nom = f - 1; }
        /* _écran d_accueil _titre (« Bonjour » _suivi …) _en_grand (2.33) : un titre calculé */
        const size_t marque_titre = chercher(r, d + 1, fin_nom, "titre");
        const size_t fin_calcul = fin_nom;
        if (marque_titre < fin_nom) fin_nom = marque_titre;
        if (fin_nom > d + 2 && r->e[fin_nom - 1].type == J_TEXTE && marque_titre == fin_calcul) { titre = &r->e[fin_nom - 1]; fin_nom--; }
        if (fin_nom <= d + 1) { echouer(r, t, grym_dupliquer("Forme attendue : « _écran des_compositeurs »."));  return; }
        if (document) { mot(r, "le", t); mot(r, "document", t); }
        else { emettre(r, J_ELISION, "l", t, 1); mot(r, "écran", t); }
        for (size_t k = d + 1; k < fin_nom; k++) copier(r, &r->e[k]);
        if (titre || taille || marque_titre < fin_calcul) {   /* _écran x « Recherche » _en_grand → L'écran x, « Recherche » en grand, montre : */
            emettre(r, J_VIRGULE, NULL, titre ? titre : taille ? taille : &r->e[marque_titre], 1);
            if (titre) copier(r, titre);
            if (marque_titre < fin_calcul) expression(r, marque_titre + 1, fin_calcul);
            if (taille) { mot(r, "en", taille); mot(r, est_cle(taille, "en_grand") ? "grand" : "petit", taille); }
            emettre(r, J_VIRGULE, NULL, titre ? titre : taille ? taille : &r->e[marque_titre], 1);
        }
        if (pour < f_entete) {   /* « , pour un salarié, » (3.25) */
            if (!(titre || taille || marque_titre < fin_calcul)) emettre(r, J_VIRGULE, NULL, &r->e[pour], 1);
            mot(r, "pour", &r->e[pour]);
            mot(r, r->e[pour + 1].valeur, &r->e[pour + 1]);
            for (size_t k = pour + 2; k < f_entete; k++) copier(r, &r->e[k]);
            emettre(r, J_VIRGULE, NULL, &r->e[f_entete - 1], 1);
        }
        f = f_entete;
        mot(r, "montre", t);
        emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_ECRAN, prof, t);
        return;
    }
    if (t->type == J_MOT_CLE && !strcmp(t->valeur, "toutes_les")) {
        /* _toutes_les 10 _secondes _dans de_veille → Toutes les 10 secondes dans l'écran de veille : (3.03) */
        size_t k = d + 1;
        mot(r, "toutes", t);
        mot(r, "les", t);
        if (k < f && r->e[k].type == J_NOMBRE) copier(r, &r->e[k++]);
        if (k + 2 >= f || r->e[k].type != J_MOT_CLE || !est_cle(&r->e[k + 1], "dans")) {
            echouer(r, t, grym_dupliquer("Forme attendue : « _toutes_les 10 _secondes _dans de_veille »."));
            return;
        }
        mot(r, r->e[k].valeur, &r->e[k]);
        mot(r, "dans", &r->e[k + 1]);
        emettre(r, J_ELISION, "l", &r->e[k + 1], 1);
        mot(r, "écran", &r->e[k + 1]);
        for (size_t q = k + 2; q < f; q++) copier(r, &r->e[q]);
        emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
        fixer_retrait(r, premier, prof);
        ouvrir(r, O_FORMULE, prof, t);
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
        if (d + 1 < f && est_cle(&r->e[d + 1], "capte") && dans == d + 4 && (est_cle(&r->e[d + 2], "un") || est_cle(&r->e[d + 2], "une"))
            && r->e[d + 3].type == J_CROCHETS && dans + 1 < f) {
            /* _quand _capte _un poste _dans de_veille → Quand un poste capte un son dans l'écran de veille : (2.70) */
            mot(r, "quand", t);
            mot(r, r->e[d + 2].valeur, &r->e[d + 2]);
            copier(r, &r->e[d + 3]);
            mot(r, "capte", &r->e[d + 1]);
            mot(r, "un", &r->e[d + 1]);
            mot(r, "son", &r->e[d + 1]);
            mot(r, "dans", &r->e[dans]);
            emettre(r, J_ELISION, "l", &r->e[dans], 1);
            mot(r, "écran", &r->e[dans]);
            for (size_t k = dans + 1; k < f; k++) copier(r, &r->e[k]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            fixer_retrait(r, premier, prof);
            ouvrir(r, O_FORMULE, prof, t);
            return;
        }
        if (d + 1 < f && est_cle(&r->e[d + 1], "capte") && (dans == d + 2 || (dans == d + 3 && r->e[d + 2].type == J_TEXTE))
            && dans + 1 < f) {
            /* _quand _capte « S » _dans de_veille → Quand l'écoute « S » capte un son dans l'écran de veille : ;
             * sans nom → Quand une écoute capte un son … (2.69) */
            mot(r, "quand", t);
            if (dans == d + 3) {
                emettre(r, J_ELISION, "l", &r->e[d + 1], 1);
                mot(r, "écoute", &r->e[d + 1]);
                copier(r, &r->e[d + 2]);
            } else {
                mot(r, "une", &r->e[d + 1]);
                mot(r, "écoute", &r->e[d + 1]);
            }
            mot(r, "capte", &r->e[d + 1]);
            mot(r, "un", &r->e[d + 1]);
            mot(r, "son", &r->e[d + 1]);
            mot(r, "dans", &r->e[dans]);
            emettre(r, J_ELISION, "l", &r->e[dans], 1);
            mot(r, "écran", &r->e[dans]);
            for (size_t k = dans + 1; k < f; k++) copier(r, &r->e[k]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            fixer_retrait(r, premier, prof);
            ouvrir(r, O_FORMULE, prof, t);
            return;
        }
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
        const int deplace = d + 1 < f && est_cle(&r->e[d + 1], "déplace");   /* _quand _déplace _une séance _dans … (2.74) */
        int choix = d + 1 < f && (est_cle(&r->e[d + 1], "choisit") || deplace) && dans == d + 4
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
            mot(r, deplace ? "déplace" : "choisit", &r->e[d + 1]);
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
    if (est_cle(t, "ouvrir_fichier") && d + 1 < f) {   /* _ouvrir_fichier frais.justificatif → Ouvrir le justificatif du frais. (2.26) */
        mot(r, "ouvrir", t);
        expression(r, d + 1, f);
        point(r, f);
        fixer_retrait(r, premier, prof);
        return;
    }
    if (t->type == J_MOT_CLE && (!strcmp(t->valeur, "ouvrir") || !strcmp(t->valeur, "fermer"))) {
        /* _ouvrir des_compositeurs → Ouvrir l'écran des compositeurs. ; _fermer → Fermer l'écran. */
        int ouvre = !strcmp(t->valeur, "ouvrir");
        if (ouvre && d + 2 < f && est_cle(&r->e[d + 1], "document")) {   /* _ouvrir _document x → Ouvrir le document x. (2.25) */
            mot(r, "ouvrir", t);
            mot(r, "le", &r->e[d + 1]);
            mot(r, "document", &r->e[d + 1]);
            for (size_t k = d + 2; k < f; k++) copier(r, &r->e[k]);
            point(r, f);
            fixer_retrait(r, premier, prof);
            return;
        }
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
        const size_t pour = ouvre ? chercher(r, d + 1, f, "pour") : f;   /* _ouvrir du_salarié _pour s (3.25) */
        if (ouvre && (pour == d + 1 || pour + 1 == f)) {
            echouer(r, t, grym_dupliquer("Forme attendue : « _ouvrir du_salarié _pour s »."));
            return;
        }
        for (size_t k = d + 1; k < pour; k++) copier(r, &r->e[k]);
        if (pour < f) {
            mot(r, "pour", &r->e[pour]);
            expression(r, pour + 1, f);
        }
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
        size_t ancien = 0;   /* _anciennement « genres » (§ 16.7) */
        if (type && q + 4 < f && est_cle(&r->e[q + 3], "anciennement") && r->e[q + 4].type == J_TEXTE) {
            ancien = q + 4;
            f -= 2;
        }
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
        if (ancien) {
            emettre(r, J_VIRGULE, NULL, &r->e[ancien - 1], 1);
            mot(r, "anciennement", &r->e[ancien - 1]);
            copier(r, &r->e[ancien]);
            f += 2;
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
        const size_t abandon = q < f && (est_cle(&r->e[q], "abandonné") || est_cle(&r->e[q], "abandonnée")) ? q : 0;   /* 2.49 */
        if (abandon) q++;
        size_t transcrit = 0;   /* _transcrit mémo (§ 15.8) */
        if (q + 1 < f && est_cle(&r->e[q], "transcrit") && r->e[q + 1].type == J_CROCHETS) { transcrit = q + 1; q += 2; }
        size_t depart = 0, fin_depart = 0;   /* _départ « Suisse », _départ −3,5 */
        if (q < f && est_cle(&r->e[q], "départ")) {
            depart = q + 1;
            fin_depart = depart < f && r->e[depart].type == J_MOINS ? depart + 2 : depart + 1;
            q = fin_depart;
        }
        size_t ancien = 0;   /* _anciennement « client » (§ 16.7) */
        if (q + 1 < f && est_cle(&r->e[q], "anciennement") && r->e[q + 1].type == J_TEXTE) { ancien = q + 1; q += 2; }
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
        if (transcrit) {   /* _transcrit mémo → , transcrit de mémo (synthétique : accord non vérifié) */
            emettre(r, J_VIRGULE, NULL, &r->e[transcrit - 1], 1);
            mot(r, "transcrit", &r->e[transcrit - 1]);
            emettre(r, J_ELISION, "d", &r->e[transcrit - 1], 1);
            copier(r, &r->e[transcrit]);
        }
        if (abandon) {   /* _abandonné → , abandonné (2.49) */
            emettre(r, J_VIRGULE, NULL, &r->e[abandon], 1);
            mot(r, est_cle(t, "une") ? "abandonnée" : "abandonné", &r->e[abandon]);
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
        if (ancien) {
            emettre(r, J_VIRGULE, NULL, &r->e[ancien - 1], 1);
            mot(r, "anciennement", &r->e[ancien - 1]);
            copier(r, &r->e[ancien]);
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
        /* _classe _un cheval [(chevaux)] [_conservé [_hors_des_fiches]] [_est _une personne [_adopte horodatée ; active]] */
        size_t p = d + 3;
        const Jeton *pluriel = NULL, *conserve = NULL, *hors = NULL, *ancien = NULL, *journal = NULL;
        if (p + 2 < f && r->e[p].type == J_PAR_OUV && r->e[p + 1].type == J_CROCHETS
            && r->e[p + 2].type == J_PAR_FERM) {
            pluriel = &r->e[p + 1];
            p += 3;
        }
        if (p < f && (est_cle(&r->e[p], "conservé") || est_cle(&r->e[p], "conservée"))) {
            conserve = &r->e[p];
            p++;
            if (p < f && est_cle(&r->e[p], "hors_des_fiches")) {   /* § 22.3 */
                hors = &r->e[p];
                p++;
            }
            if (p < f && (est_cle(&r->e[p], "journalisé") || est_cle(&r->e[p], "journalisée"))) {   /* 2.85 */
                journal = &r->e[p];
                p++;
            }
            if (p + 1 < f && est_cle(&r->e[p], "anciennement") && r->e[p + 1].type == J_TEXTE) {   /* § 16.7 */
                ancien = &r->e[p];
                p += 2;
            }
        }
        size_t decale = p - (d + 3);   /* jetons de pluriel, de « _conservé », « _hors_des_fiches », « _anciennement » */
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
        if (hors) {
            mot(r, "hors", hors);
            mot(r, "des", hors);
            mot(r, "fiches", hors);
            emettre(r, J_VIRGULE, NULL, hors, 1);
        }
        if (journal) {
            mot(r, journal->valeur, journal);   /* synthétique : l'accord n'est pas vérifié */
            emettre(r, J_VIRGULE, NULL, journal, 1);
        }
        if (ancien) {   /* _anciennement « client » → anciennement « client », */
            mot(r, "anciennement", ancien);
            copier(r, ancien + 1);
            emettre(r, J_VIRGULE, NULL, ancien, 1);
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
    /* « _la séance.date << … » (2.36) : une affectation au champ d'un objet dont l'article est écrit, pas une création */
    const int article_champ = t->type == J_MOT_CLE && (est_cle(t, "le") || est_cle(t, "la") || est_cle(t, "l'") || est_cle(t, "nu")) && d + 3 < f
                              && r->e[d + 1].type == J_CROCHETS && r->e[d + 2].type == J_POINT;
    if (t->type == J_MOT_CLE && !article_champ) {
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
            const char *marque = NULL;   /* « _sinon _court », « _sinon_si … _alors _bloc » (2.35) */
            if (f >= d + 2 && (est_cle(&r->e[f - 1], "court") || est_cle(&r->e[f - 1], "bloc"))) {
                marque = est_cle(&r->e[f - 1], "court") ? "court" : "bloc";
                f--;
            }
            if (!strcmp(c, "sinon_si")) {
                size_t alors = chercher(r, d + 1, f, "alors");
                if (alors != f - 1) { echouer(r, t, grym_dupliquer("« _alors » attendu en fin de ligne.")); return; }
                mot(r, "si", t);
                expression(r, d + 1, alors);
            } else if (f != d + 1) {
                echouer(r, &r->e[d + 1], grym_dupliquer("« _sinon » s'écrit seul sur sa ligne."));
                return;
            }
            emettre(r, J_DEUX_POINTS, marque, &r->e[f - 1], 1);
            return;
        }
        if (!strcmp(c, "envoyer_courriel")) {
            /* _envoyer_courriel _à a _objet o _depuis m _texte t → Envoyer un courriel à a, avec l'objet o, depuis m, et le
             * texte t. ; « _texte_suivant » et son bloc à la place de « _texte t » (§ 22.27) */
            size_t pos[4] = { 0, 0, 0, 0 };
            static const char *const CLES[] = { "à", "objet", "depuis", "texte" };
            int p = 0;
            for (size_t q = d + 1; q < f; q++) {
                if (r->e[q].type == J_PAR_OUV || r->e[q].type == J_GABARIT_DEBUT) p++;
                else if (r->e[q].type == J_PAR_FERM || r->e[q].type == J_GABARIT_FIN) p--;
                else if (p == 0)
                    for (int c4 = 0; c4 < 4; c4++)
                        if (!pos[c4] && est_cle(&r->e[q], CLES[c4])) pos[c4] = q;
                if (p == 0 && !pos[3] && est_cle(&r->e[q], "texte_suivant")) pos[3] = q;
            }
            if (!pos[0] || pos[0] != d + 1 || !pos[1] || !pos[2] || !pos[3] || !(pos[0] < pos[1] && pos[1] < pos[2] && pos[2] < pos[3])) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _envoyer_courriel _à a _objet o _depuis m _texte t »."));
                return;
            }
            mot(r, "envoyer", t);
            mot(r, "un", t);
            mot(r, "courriel", t);
            mot(r, "à", t);
            expression(r, pos[0] + 1, pos[1]);
            emettre(r, J_VIRGULE, NULL, &r->e[pos[1]], 1);
            mot(r, "avec", &r->e[pos[1]]);
            emettre(r, J_ELISION, "l", &r->e[pos[1]], 1);
            mot(r, "objet", &r->e[pos[1]]);
            expression(r, pos[1] + 1, pos[2]);
            emettre(r, J_VIRGULE, NULL, &r->e[pos[2]], 1);
            mot(r, "depuis", &r->e[pos[2]]);
            expression(r, pos[2] + 1, pos[3]);
            emettre(r, J_VIRGULE, NULL, &r->e[pos[3]], 1);
            mot(r, "et", &r->e[pos[3]]);
            if (est_cle(&r->e[pos[3]], "texte_suivant")) expression(r, pos[3], f);
            else {
                mot(r, "le", &r->e[pos[3]]);
                mot(r, "texte", &r->e[pos[3]]);
                expression(r, pos[3] + 1, f);
            }
            point(r, f);
            return;
        }
        if (!strcmp(c, "échec_de")) {   /* _échec_de réseau _ou fichier → En cas d'échec de réseau ou de fichier : (§ 18.2) */
            if (!haut || haut->type != O_ESSAI) {
                echouer(r, t, grym_dupliquer("« _échec_de » hors d'un « _essayer »."));
                return;
            }
            mot(r, "en", t);
            mot(r, "cas", t);
            emettre(r, J_ELISION, "d", t, 1);
            mot(r, "échec", t);
            for (size_t q = d + 1; q < f; q++) {
                const Jeton *s = &r->e[q];
                if (q > d + 1) {
                    if (!est_cle(s, "ou") || q + 1 >= f) {
                        echouer(r, s, grym_dupliquer("Forme attendue : « _échec_de réseau _ou fichier »."));
                        return;
                    }
                    mot(r, "ou", s);
                    s = &r->e[++q];
                }
                if (s->type != J_CROCHETS) {
                    echouer(r, s, grym_dupliquer("Une sorte attendue : « _échec_de réseau »."));
                    return;
                }
                const unsigned char v = (unsigned char)s->valeur[0];
                if (v == 'a' || v == 'e' || v == 'i' || v == 'o' || v == 'u' || v == 0xC3) emettre(r, J_ELISION, "d", s, 1);
                else mot(r, "de", s);
                mot(r, s->valeur, s);
            }
            if (f == d + 1) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _échec_de réseau _ou fichier »."));
                return;
            }
            fixer_retrait(r, premier, haut->profondeur);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
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
        if ((!strcmp(c, "le") || !strcmp(c, "la") || !strcmp(c, "l'"))
            && !(d + 3 < f && r->e[d + 1].type == J_CROCHETS && r->e[d + 2].type == J_POINT)) {   /* « _la s.date << … » : plus bas (2.36) */
            /* « _le prochain (état) << « x » » : le type d'une variable (§ 16.16) */
            const int typee = d + 5 < f && r->e[d + 1].type == J_CROCHETS && r->e[d + 2].type == J_PAR_OUV
                              && r->e[d + 3].type == J_CROCHETS && r->e[d + 4].type == J_PAR_FERM && r->e[d + 5].type == J_AFFECTE;
            const size_t fleche = typee ? d + 5 : d + 2;
            if (fleche >= f || r->e[d + 1].type != J_CROCHETS || r->e[fleche].type != J_AFFECTE) {
                echouer(r, t, grym_formater("Création attendue : « _%s nom << valeur ».", c));
                return;
            }
            if (!strcmp(c, "l'")) emettre(r, J_ELISION, "l", t, 1);
            else mot(r, c, t);
            copier(r, &r->e[d + 1]);
            if (typee) { copier(r, &r->e[d + 2]); copier(r, &r->e[d + 3]); copier(r, &r->e[d + 4]); }
            mot(r, "vaut", &r->e[fleche]);
            expression(r, fleche + 1, f);
            if (avec) { emettre(r, J_DEUX_POINTS, NULL, &r->e[f], 1); fixer_retrait(r, premier, prof); ouvrir(r, O_INIT, prof, t); return; }
            point(r, f);
        } else if (!strcmp(c, "contrat")) {
            /* _contrat payable _calcul _le montant_dû(_une chose_payable) → Une chose payable doit savoir : le montant dû d'une
             * chose payable. ; _contrat payable _action encaisser(…) → … : encaisser une chose payable. (§ 13.7 bis) */
            size_t q = d + 3;
            const int calcul = d + 2 < f && est_cle(&r->e[d + 2], "calcul");
            const Jeton *art = calcul && q < f && (est_cle(&r->e[q], "le") || est_cle(&r->e[q], "la") || est_cle(&r->e[q], "l'")) ? &r->e[q++] : NULL;
            if (d + 2 >= f || r->e[d + 1].type != J_CROCHETS || (!calcul && !est_cle(&r->e[d + 2], "action")) || (calcul && !art)
                || q + 1 >= f || r->e[q].type != J_CROCHETS || r->e[q + 1].type != J_PAR_OUV || fermante(r, q + 1, f) != f - 1) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _contrat payable _calcul _le montant_dû(_une chose_payable) » ou "
                                             "« _contrat payable _action encaisser(_une chose_payable) »."));
                return;
            }
            mot(r, "une", t);
            mot(r, "chose", t);
            copier(r, &r->e[d + 1]);
            mot(r, "doit", t);
            mot(r, "savoir", t);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            if (art) {
                if (!strcmp(art->valeur, "l'")) emettre(r, J_ELISION, "l", art, 1);
                else mot(r, art->valeur, art);
            }
            copier(r, &r->e[q]);
            parametres(r, q + 2, f - 1, calcul);
            point(r, f);
        } else if (!strcmp(c, "type")) {   /* _type _un état « a » ; « b » → Un état est « a », « b ». (§ 16.16) */
            if (d + 4 >= f || (!est_cle(&r->e[d + 1], "un") && !est_cle(&r->e[d + 1], "une")) || r->e[d + 2].type != J_CROCHETS) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _type _un état « à faire » ; « fini » »."));
                return;
            }
            mot(r, r->e[d + 1].valeur, &r->e[d + 1]);
            copier(r, &r->e[d + 2]);
            mot(r, "est", &r->e[d + 2]);
            for (size_t q = d + 3; q < f; q++) {
                if (r->e[q].type == J_POINT_VIRGULE) emettre(r, J_VIRGULE, NULL, &r->e[q], 1);
                else copier(r, &r->e[q]);
            }
            point(r, f);
        } else if (!strcmp(c, "essayer")) {   /* _essayer → Essayer : (§ 18) */
            if (d + 1 != f) { echouer(r, t, grym_dupliquer("« _essayer » s'écrit seul sur sa ligne.")); return; }
            mot(r, "essayer", t);
            emettre(r, J_DEUX_POINTS, NULL, t, 1);
            ouvrir(r, O_ESSAI, prof, t);
        } else if (!strcmp(c, "utiliser") && d + 1 < f && r->e[d + 1].type == J_MOT_CLE
                   && !strcmp(r->e[d + 1].valeur, "bibliothèque")) {
            /* _utiliser _bibliothèque « X » 2 → Utiliser la bibliothèque « X », version 2. (2.61, § 21.9) */
            if (f != d + 4 || r->e[d + 2].type != J_TEXTE || r->e[d + 3].type != J_NOMBRE) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _utiliser _bibliothèque « Suisse santé » 2 »."));
                return;
            }
            mot(r, "utiliser", t);
            mot(r, "la", &r->e[d + 1]);
            mot(r, "bibliothèque", &r->e[d + 1]);
            emettre(r, J_TEXTE, r->e[d + 2].valeur, &r->e[d + 2], 0);
            emettre(r, J_VIRGULE, NULL, &r->e[d + 3], 1);
            mot(r, "version", &r->e[d + 3]);
            copier(r, &r->e[d + 3]);
            point(r, f);
        } else if (!strcmp(c, "interne")) {   /* _interne → Ce fichier est interne à sa bibliothèque. (2.62) */
            if (d + 1 != f) { echouer(r, t, grym_dupliquer("« _interne » s'écrit seul.")); return; }
            mot(r, "ce", t);
            mot(r, "fichier", t);
            mot(r, "est", t);
            mot(r, "interne", t);
            mot(r, "à", t);
            mot(r, "sa", t);
            mot(r, "bibliothèque", t);
            point(r, f);
        } else if (!strcmp(c, "bibliothèque")) {   /* _bibliothèque « X » 2 → Cette bibliothèque est « X », version 2. */
            if (f != d + 3 || r->e[d + 1].type != J_TEXTE || r->e[d + 2].type != J_NOMBRE) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _bibliothèque « Suisse santé » 2 »."));
                return;
            }
            mot(r, "cette", t);
            mot(r, "bibliothèque", t);
            mot(r, "est", t);
            emettre(r, J_TEXTE, r->e[d + 1].valeur, &r->e[d + 1], 0);
            emettre(r, J_VIRGULE, NULL, &r->e[d + 2], 1);
            mot(r, "version", &r->e[d + 2]);
            copier(r, &r->e[d + 2]);
            point(r, f);
        } else if (!strcmp(c, "éprouver")) {   /* _éprouver « Praxis » → Ces scénarios éprouvent « Praxis ». (3.10) */
            if (d + 2 != f || r->e[d + 1].type != J_TEXTE) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _éprouver « Praxis » »."));
                return;
            }
            mot(r, "ces", t);
            mot(r, "scénarios", t);
            mot(r, "éprouvent", t);
            copier(r, &r->e[d + 1]);
            point(r, f);
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
        } else if (!strcmp(c, "signer_journal")) {   /* _signer_journal t → Signer le journal avec t. (2.87) */
            if (d + 1 >= f) {
                echouer(r, t, grym_dupliquer("« _signer_journal » attend un texte : « _signer_journal « … » »."));
                return;
            }
            mot(r, "signer", t);
            mot(r, "le", t);
            mot(r, "journal", t);
            mot(r, "avec", t);
            expression(r, d + 1, f);
            point(r, f);
        } else if (!strcmp(c, "amorcer")) {   /* _amorcer t → Amorcer la transcription avec t. (2.80) */
            if (d + 1 >= f) {
                echouer(r, t, grym_dupliquer("« _amorcer » attend un texte : « _amorcer « … » »."));
                return;
            }
            const size_t toute = chercher(r, d + 1, f, "toute_langue");   /* _amorcer t _toute_langue (2.86) */
            mot(r, "amorcer", t);
            mot(r, "la", t);
            mot(r, "transcription", t);
            mot(r, "avec", t);
            expression(r, d + 1, toute);
            if (toute < f) {
                emettre(r, J_VIRGULE, NULL, &r->e[toute], 1);
                mot(r, "en", &r->e[toute]);
                mot(r, "toute", &r->e[toute]);
                mot(r, "langue", &r->e[toute]);
            }
            point(r, f);
        } else if (!strcmp(c, "refuser") || !strcmp(c, "signaler")) {   /* _refuser « … » → Refuser « … ». (§ 18.1) ; _signaler (§ 22.22) */
            if (d + 1 >= f) {
                echouer(r, t, grym_formater("« _%s » attend un message : « _%s « … » ».", c, c));
                return;
            }
            mot(r, c, t);
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
        } else if (!strcmp(c, "écrans")) {
            /* _écrans _couleur _verte|« #… » _fond « … » _police « … » _logo « … » → Les écrans ont … (§ 22.5) */
            static const char *const CLE[] = { "couleur", "fond", "police", "logo", "signature" };
            static const char *const ART[] = { "la", "le", "la", "le", "la" };
            const Jeton *val[5] = { NULL, NULL, NULL, NULL, NULL };
            size_t calc_debut[5] = { 0, 0, 0, 0, 0 }, calc_fin[5] = { 0, 0, 0, 0, 0 };   /* 2.33 : « _logo (…) » */
            size_t k = d + 1;
            int dernier = -1, nb = 0, faux = 0;
            while (k + 1 < f && !faux) {
                int q = -1;
                for (int x = 0; x < 5; x++) if (est_cle(&r->e[k], CLE[x])) q = x;
                if (q > dernier && (q == 3 || q == 4) && r->e[k + 1].type == J_PAR_OUV) {
                    int p = 0;
                    size_t z = k + 1;
                    for (; z < f; z++) {
                        if (r->e[z].type == J_PAR_OUV) p++;
                        else if (r->e[z].type == J_PAR_FERM && --p == 0) break;
                    }
                    if (z >= f) { faux = 1; break; }
                    val[q] = &r->e[k + 1];
                    calc_debut[q] = k + 1;
                    calc_fin[q] = z + 1;
                    dernier = q;
                    nb++;
                    k = z + 1;
                    continue;
                }
                const int bon = q > dernier && (r->e[k + 1].type == J_TEXTE || (q == 0 && r->e[k + 1].type == J_MOT_CLE));
                if (!bon) { faux = 1; break; }
                val[q] = &r->e[k + 1];
                dernier = q;
                nb++;
                k += 2;
            }
            if (faux || k != f || !nb) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _écrans _couleur _verte _fond « #E8E2D6 » _police « … » "
                                             "_logo « logo.png » », un réglage au moins, dans cet ordre."));
                return;
            }
            mot(r, "les", t);
            mot(r, "écrans", t);
            mot(r, "ont", t);
            int faits = 0;
            for (int q = 0; q < 5; q++) {
                if (!val[q]) continue;
                if (faits && faits + 1 == nb) mot(r, "et", val[q]);
                else if (faits) emettre(r, J_VIRGULE, NULL, val[q], 1);
                mot(r, ART[q], val[q]);
                mot(r, CLE[q], val[q]);
                if (calc_fin[q]) expression(r, calc_debut[q], calc_fin[q]);
                else if (val[q]->type == J_MOT_CLE) mot(r, val[q]->valeur, val[q]);
                else emettre(r, J_TEXTE, val[q]->valeur, val[q], 0);
                faits++;
            }
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
            /* « _alors _court », « _alors _bloc » (2.35) : la forme littéraire de la branche */
            const char *marque = NULL;
            if (!strcmp(c, "si") && alors == f - 2 && (est_cle(&r->e[f - 1], "court") || est_cle(&r->e[f - 1], "bloc"))) {
                marque = est_cle(&r->e[f - 1], "court") ? "court" : "bloc";
                f--;
            }
            if (!strcmp(c, "si") && alors != f - 1) {
                echouer(r, t, grym_dupliquer("« _alors » attendu en fin de ligne : « _si x > 0 _alors »."));
                return;
            }
            if (!strcmp(c, "si")) mot(r, "si", t);
            else { mot(r, "tant", t); mot(r, "que", t); }
            expression(r, d + 1, alors);
            emettre(r, J_DEUX_POINTS, marque, &r->e[f - 1], 1);
            ouvrir(r, !strcmp(c, "si") ? O_SI : O_BOUCLE, prof, t);
        } else if (!strcmp(c, "répéter")) {
            size_t fois = chercher(r, d + 1, f, "fois");
            if (fois != f - 1) { echouer(r, t, grym_dupliquer("« _fois » attendu : « _répéter 3 _fois »."));  return; }
            mot(r, "répéter", t);
            expression(r, d + 1, fois);
            mot(r, "fois", &r->e[fois]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
            ouvrir(r, O_BOUCLE, prof, t);
        } else if (!strcmp(c, "pour_chaque") && d + 4 < f && r->e[d + 1].type == J_CROCHETS && est_cle(&r->e[d + 2], "parmi")
                   && est_cle(&r->e[d + 3], "mots")) {
            /* _pour_chaque m _parmi _mots t → Pour chaque m parmi les mots de t : (2.81) */
            mot(r, "pour", t);
            mot(r, "chaque", t);
            copier(r, &r->e[d + 1]);
            mot(r, "parmi", &r->e[d + 2]);
            mot(r, "les", &r->e[d + 3]);
            mot(r, "mots", &r->e[d + 3]);
            mot(r, "de", &r->e[d + 3]);
            expression(r, d + 4, f);
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
        } else if (!strcmp(c, "pour_chaque") && d + 4 < f && r->e[d + 1].type == J_CROCHETS && est_cle(&r->e[d + 2], "champ")
                   && r->e[d + 3].type == J_TEXTE) {
            /* _pour_chaque rdv _champ « collection » r → Pour chaque rdv du champ « collection » de r : (2.60) */
            mot(r, "pour", t);
            mot(r, "chaque", t);
            copier(r, &r->e[d + 1]);
            mot(r, "du", &r->e[d + 2]);
            mot(r, "champ", &r->e[d + 2]);
            copier(r, &r->e[d + 3]);
            mot(r, "de", &r->e[d + 3]);
            expression(r, d + 4, f);
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
            size_t premier = d + 3;   /* le premier élément, après « _liste » (§ 13.10) */
            if (d + 3 < f && est_cle(&r->e[d + 3], "ensemble_vide") && d + 4 == f) {
                mot(r, "un", &r->e[d + 3]);
                mot(r, "ensemble", &r->e[d + 3]);
                mot(r, "vide", &r->e[d + 3]);
            } else if (d + 3 < f && est_cle(&r->e[d + 3], "table_vide") && d + 4 == f) {   /* § 13.11 */
                mot(r, "une", &r->e[d + 3]);
                mot(r, "table", &r->e[d + 3]);
                mot(r, "vide", &r->e[d + 3]);
            } else if (d + 4 < f && est_cle(&r->e[d + 3], "table") && (v->type == J_AFFECTE || est_cle(v, "deviennent"))) {
                /* _table « a » _vaut 1 ; « b » _vaut 2 → la table où « a » vaut 1 et « b » vaut 2 (§ 13.11) */
                mot(r, "la", &r->e[d + 3]);
                mot(r, "table", &r->e[d + 3]);
                mot(r, "où", &r->e[d + 3]);
                size_t debut = d + 4;
                int p = 0;
                for (size_t q = d + 4; q <= f; q++) {
                    if (q < f) {
                        if (r->e[q].type == J_PAR_OUV) p++;
                        else if (r->e[q].type == J_PAR_FERM) p--;
                        if (!(p == 0 && r->e[q].type == J_POINT_VIRGULE)) continue;
                    }
                    size_t vaut = debut;
                    while (vaut < q && !est_cle(&r->e[vaut], "vaut")) vaut++;
                    if (vaut == q || vaut == debut || vaut + 1 == q) {
                        echouer(r, &r->e[debut < f ? debut : f - 1], grym_dupliquer("Paire attendue : « « a » _vaut 1 »."));
                        return;
                    }
                    if (debut > d + 4) emettre(r, J_VIRGULE, NULL, &r->e[debut - 1], 1);
                    expression(r, debut, vaut);
                    mot(r, "vaut", &r->e[vaut]);
                    expression(r, vaut + 1, q);
                    debut = q + 1;
                }
            } else if (d + 3 < f && est_cle(&r->e[d + 3], "liste_vide") && d + 4 == f) {
                mot(r, "une", &r->e[d + 3]);
                mot(r, "liste", &r->e[d + 3]);
                mot(r, "vide", &r->e[d + 3]);
            } else if (v->type == J_AFFECTE || est_cle(v, "deviennent")) {
                if (d + 3 < f && est_cle(&r->e[d + 3], "liste")) {
                    if (d + 4 == f) { echouer(r, &r->e[d + 3], grym_dupliquer("Éléments attendus après « _liste », ou « _liste_vide »."));  return; }
                    mot(r, "la", &r->e[d + 3]);
                    mot(r, "liste", &r->e[d + 3]);
                    premier = d + 4;
                }
                size_t debut = premier;
                int p = 0;
                for (size_t q = premier; q <= f; q++) {
                    if (q < f) {
                        if (r->e[q].type == J_PAR_OUV) p++;
                        else if (r->e[q].type == J_PAR_FERM) p--;
                        if (!(p == 0 && r->e[q].type == J_POINT_VIRGULE)) continue;
                    }
                    if (debut > premier) emettre(r, J_VIRGULE, NULL, &r->e[debut - 1], 1);
                    if (q == debut) { echouer(r, &r->e[q < f ? q : f - 1], grym_dupliquer("Élément vide entre « ; »."));  return; }
                    expression(r, debut, q);
                    debut = q + 1;
                }
            } else {
                expression(r, d + 3, f);
            }
            point(r, f);
        } else if (!strcmp(c, "dans") && d + 1 < f && r->e[d + 1].type == J_CROCHETS) {
            /* _dans compteurs « CQ » _vaut 0 → Dans les compteurs, « CQ » vaut 0. (§ 13.11) */
            size_t verbe = d + 2;
            while (verbe < f && !est_cle(&r->e[verbe], "vaut") && !est_cle(&r->e[verbe], "devient")) verbe++;
            if (verbe == f || verbe == d + 2 || verbe + 1 == f) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _dans compteurs « CQ » _vaut 0 »."));
                return;
            }
            mot(r, "dans", t);
            mot(r, "les", t);
            copier(r, &r->e[d + 1]);
            emettre(r, J_VIRGULE, NULL, &r->e[d + 1], 1);
            expression(r, d + 2, verbe);
            mot(r, r->e[verbe].valeur, &r->e[verbe]);
            expression(r, verbe + 1, f);
            point(r, f);
        } else if (!strcmp(c, "scénario")) {   /* _scénario « titre » … _fin → Scénario « titre » : (§ 9.7) */
            if (d + 2 != f || r->e[d + 1].type != J_TEXTE) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _scénario « une relance se crée » », puis son bloc et « _fin »."));
                return;
            }
            mot(r, "scénario", t);
            copier(r, &r->e[d + 1]);
            emettre(r, J_DEUX_POINTS, NULL, &r->e[d + 1], 1);
            fixer_retrait(r, premier, prof);
            ouvrir(r, O_FORMULE, prof, t);
        } else if (!strcmp(c, "on_créneau") || !strcmp(c, "on_déplace")) {
            /* _on_créneau D ; H _écran E → On choisit le créneau de (D) à (H) dans l'écran E. ; _on_déplace X ; D ; H _écran E (3.12) */
            const int dep = !strcmp(c, "on_déplace");
            const size_t ke = chercher(r, d + 1, f, "écran");
            size_t pv[2], np = 0;
            int p = 0;
            for (size_t q = d + 1; q < ke && np < 2; q++) {
                if (r->e[q].type == J_PAR_OUV) p++;
                else if (r->e[q].type == J_PAR_FERM) p--;
                else if (p == 0 && r->e[q].type == J_POINT_VIRGULE) pv[np++] = q;
            }
            if (ke + 2 != f || r->e[ke + 1].type != J_CROCHETS || (int)np != (dep ? 2 : 1)) {
                echouer(r, t, grym_dupliquer(dep ? "Forme attendue : « _on_déplace séance ; 05.11.2026 ; 14:15 _écran agenda »."
                                                 : "Forme attendue : « _on_créneau 05.11.2026 ; 10:30 _écran agenda »."));
                return;
            }
            mot(r, "on", t);
            const size_t dd = dep ? pv[0] + 1 : d + 1, fd = dep ? pv[1] : pv[0];
            if (dep) {
                mot(r, "déplace", t);
                emettre(r, J_PAR_OUV, NULL, &r->e[d + 1], 1);
                expression(r, d + 1, pv[0]);
                emettre(r, J_PAR_FERM, NULL, &r->e[pv[0]], 1);
                mot(r, "à", &r->e[pv[0]]);
            } else {
                mot(r, "choisit", t);
                mot(r, "le", t);
                mot(r, "créneau", t);
                mot(r, "de", t);
            }
            emettre(r, J_PAR_OUV, NULL, &r->e[dd], 1);
            expression(r, dd, fd);
            emettre(r, J_PAR_FERM, NULL, &r->e[fd], 1);
            mot(r, "à", &r->e[fd]);
            emettre(r, J_PAR_OUV, NULL, &r->e[fd + 1], 1);
            expression(r, fd + 1, ke);
            emettre(r, J_PAR_FERM, NULL, &r->e[ke], 1);
            mot(r, "dans", &r->e[ke]);
            emettre(r, J_ELISION, "l", &r->e[ke], 1);
            mot(r, "écran", &r->e[ke]);
            copier(r, &r->e[ke + 1]);
            point(r, f);
        } else if (!strcmp(c, "on_est")) {   /* _on_est D → On est le (D). (3.21) */
            if (f <= d + 1) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _on_est 15.03.2027 »."));
                return;
            }
            mot(r, "on", t);
            mot(r, "est", t);
            mot(r, "le", t);
            emettre(r, J_PAR_OUV, NULL, &r->e[d + 1], 1);
            expression(r, d + 1, f);
            emettre(r, J_PAR_FERM, NULL, &r->e[f - 1], 1);
            point(r, f);
        } else if (!strcmp(c, "on_voit")) {   /* _on_voit « … » _écran E → On voit l'erreur « … » dans l'écran E. (3.19) */
            if (f != d + 4 || r->e[d + 1].type != J_TEXTE || !est_cle(&r->e[d + 2], "écran") || r->e[d + 3].type != J_CROCHETS) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _on_voit « Remplis « Nom » » _écran de_saisie »."));
                return;
            }
            mot(r, "on", t);
            mot(r, "voit", t);
            emettre(r, J_ELISION, "l", t, 1);
            mot(r, "erreur", t);
            copier(r, &r->e[d + 1]);
            mot(r, "dans", &r->e[d + 2]);
            emettre(r, J_ELISION, "l", &r->e[d + 2], 1);
            mot(r, "écran", &r->e[d + 2]);
            copier(r, &r->e[d + 3]);
            point(r, f);
        } else if (!strcmp(c, "on_confirme") || !strcmp(c, "on_renonce")) {   /* → On confirme. On renonce. (3.09) */
            mot(r, "on", t);
            mot(r, !strcmp(c, "on_confirme") ? "confirme" : "renonce", t);
            point(r, f);
        } else if (!strcmp(c, "on_écrit") || !strcmp(c, "on_choisit") || !strcmp(c, "on_clique") || !strcmp(c, "on_ferme")) {
            /* _on_écrit X _dans nom _écran de_saisie → On écrit X dans le nom de l'écran de saisie. (3.08) */
            const size_t ke = chercher(r, d + 1, f, "écran");
            const size_t kd = chercher(r, d + 1, f, "dans");
            if (ke + 2 != f || r->e[ke + 1].type != J_CROCHETS) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _on_clique « OK » _écran de_saisie »."));
                return;
            }
            mot(r, "on", t);
            mot(r, !strcmp(c, "on_écrit") ? "écrit" : !strcmp(c, "on_choisit") ? "choisit" : !strcmp(c, "on_clique") ? "clique" : "ferme", t);
            if (!strcmp(c, "on_clique")) {
                mot(r, "sur", t);
                if (d + 1 < ke) copier(r, &r->e[d + 1]);
                mot(r, "dans", &r->e[ke]);
            } else if (strcmp(c, "on_ferme") != 0) {
                const size_t fv = kd < ke ? kd : ke;
                emettre(r, J_PAR_OUV, NULL, &r->e[d + 1], 1);
                expression(r, d + 1, fv);
                emettre(r, J_PAR_FERM, NULL, &r->e[fv], 1);
                mot(r, "dans", &r->e[fv]);
                if (kd < ke) {   /* la zone : article implicite, la forme littéraire reprend le sien */
                    emettre(r, J_ARTICLE_IMPLICITE, NULL, &r->e[kd + 1], 1);
                    copier(r, &r->e[kd + 1]);
                    mot(r, "de", &r->e[kd + 1]);
                }
            }
            emettre(r, J_ELISION, "l", &r->e[ke], 1);
            mot(r, "écran", &r->e[ke]);
            copier(r, &r->e[ke + 1]);
            point(r, f);
        } else if (!strcmp(c, "vérifier")) {   /* _vérifier c → Vérifier que c. (§ 9.7) */
            if (d + 1 == f) { echouer(r, t, grym_dupliquer("Forme attendue : « _vérifier _nombre_de relance _conservé = 1 ».")); return; }
            mot(r, "vérifier", t);
            mot(r, "que", t);
            expression(r, d + 1, f);
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
            if (avec < fin_cols && avec < dans) {   /* sans « _avec », pas de colonnes : « _titre » n'en ouvre pas (2.51) */
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
        } else if (!strcmp(c, "comme_clic")) {   /* _comme_clic « B » → Faire comme un clic sur « B ». (2.36) */
            if (f != d + 2 || r->e[d + 1].type != J_TEXTE) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _comme_clic « Enregistrer » »."));
                return;
            }
            mot(r, "faire", t);
            mot(r, "comme", t);
            mot(r, "un", t);
            mot(r, "clic", t);
            mot(r, "sur", t);
            copier(r, &r->e[d + 1]);
            point(r, f);
        } else if (!strcmp(c, "sauvegarder") || !strcmp(c, "restaurer")) {
            /* _sauvegarder _dans « c.grymd » [_mot_de_passe m] (2.29, 2.30) ; _restaurer _depuis f [_mot_de_passe m] */
            const int sauver = !strcmp(c, "sauvegarder");
            if (f < d + 3 || !est_cle(&r->e[d + 1], sauver ? "dans" : "depuis")) {
                echouer(r, t, grym_dupliquer(sauver ? "Forme attendue : « _sauvegarder _dans « copie.grymd » [_mot_de_passe m] »."
                                                    : "Forme attendue : « _restaurer _depuis « copie.grymd » [_mot_de_passe m] »."));
                return;
            }
            size_t mdp = chercher(r, d + 2, f, "mot_de_passe");
            const size_t jour = sauver ? chercher(r, d + 2, mdp, "une_par_jour_passé") : mdp;   /* 2.53 */
            mot(r, c, t);
            mot(r, "la", t);
            mot(r, "base", t);
            mot(r, sauver ? "dans" : "depuis", &r->e[d + 1]);
            expression(r, d + 2, jour);
            if (jour < mdp) {
                emettre(r, J_VIRGULE, NULL, &r->e[jour], 1);
                mot(r, "une", &r->e[jour]); mot(r, "par", &r->e[jour]); mot(r, "jour", &r->e[jour]); mot(r, "passé", &r->e[jour]);
            }
            if (mdp < f) {
                mot(r, "avec", &r->e[mdp]);
                mot(r, "le", &r->e[mdp]);
                mot(r, "mot", &r->e[mdp]);
                mot(r, "de", &r->e[mdp]);
                mot(r, "passe", &r->e[mdp]);
                expression(r, mdp + 1, f);
            }
            point(r, f);
        } else if (!strcmp(c, "vérifier_sauvegarde")) {   /* _vérifier_sauvegarde f [_mot_de_passe m] (2.85) */
            if (f < d + 2) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _vérifier_sauvegarde « copie.coffre » [_mot_de_passe m] »."));
                return;
            }
            size_t mdp = chercher(r, d + 1, f, "mot_de_passe");
            mot(r, "vérifier", t);
            mot(r, "la", t);
            mot(r, "sauvegarde", t);
            expression(r, d + 1, mdp);
            if (mdp < f) {
                mot(r, "avec", &r->e[mdp]);
                mot(r, "le", &r->e[mdp]);
                mot(r, "mot", &r->e[mdp]);
                mot(r, "de", &r->e[mdp]);
                mot(r, "passe", &r->e[mdp]);
                expression(r, mdp + 1, f);
            }
            point(r, f);
        } else if (!strcmp(c, "connexion")) {   /* _connexion → Les utilisateurs se connectent. (3.17) */
            if (d + 1 != f) { echouer(r, t, grym_dupliquer("« _connexion » s'écrit seul.")); return; }
            mot(r, "les", t);
            mot(r, "utilisateurs", t);
            mot(r, "se", t);
            mot(r, "connectent", t);
            point(r, f);
        } else if (!strcmp(c, "base_chiffrée")) {   /* _base_chiffrée → La base est chiffrée. (2.37) */
            if (d + 1 != f) { echouer(r, t, grym_dupliquer("« _base_chiffrée » s'écrit seul.")); return; }
            mot(r, "la", t);
            mot(r, "base", t);
            mot(r, "est", t);
            mot(r, "chiffrée", t);
            point(r, f);
        } else if (!strcmp(c, "changer_mot_de_passe")) {   /* _changer_mot_de_passe n _ancien a (2.37) */
            size_t anc = chercher(r, d + 1, f, "ancien");
            if (anc == f || anc == d + 1 || anc + 1 == f) {
                echouer(r, t, grym_dupliquer("Forme attendue : « _changer_mot_de_passe nouveau _ancien ancien »."));
                return;
            }
            static const char *const M[] = { "changer", "le", "mot", "de", "passe", "de", "la", "base", "pour" };
            for (size_t k = 0; k < sizeof M / sizeof *M; k++) mot(r, M[k], t);
            expression(r, d + 1, anc);
            emettre(r, J_VIRGULE, NULL, &r->e[anc], 0);
            emettre(r, J_ELISION, "l", &r->e[anc], 1);
            mot(r, "ancien", &r->e[anc]);
            mot(r, "étant", &r->e[anc]);
            expression(r, anc + 1, f);
            point(r, f);
        } else if (!strcmp(c, "enregistrer")) {
            size_t dans = chercher(r, d + 1, f, "dans");
            if (dans == f && f > d + 1 && est_cle(&r->e[d + 1], "écran")) {   /* _enregistrer _écran.salarié → Enregistrer le salarié de l'écran. (3.25) */
                mot(r, "enregistrer", t);
                expression(r, d + 1, f);
                point(r, f);
                fixer_retrait(r, premier, prof);
                return;
            }
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
                const int ecran_d = !calcul && ferme + 2 < f && est_cle(&r->e[ferme + 1], "dans_écran");   /* 2.55 */
                if (ferme + 1 < f && !ecran_d) {
                    echouer(r, &r->e[ferme + 1], grym_dupliquer("Fin de ligne attendue : le corps suit, fermé par « _fin »."));
                    return;
                }
                if (ecran_d) {   /* _action remplir() _dans_écran du_patient → Pour remplir, dans l'écran du patient : */
                    emettre(r, J_VIRGULE, NULL, &r->e[ferme + 1], 1);
                    mot(r, "dans", &r->e[ferme + 1]);
                    emettre(r, J_ELISION, "l", &r->e[ferme + 1], 1);
                    mot(r, "écran", &r->e[ferme + 1]);
                    for (size_t q = ferme + 2; q < f; q++) {
                        if (est_cle(&r->e[q], "ou")) {   /* _ou des_soins → ou l'écran des soins (2.58) */
                            mot(r, "ou", &r->e[q]);
                            emettre(r, J_ELISION, "l", &r->e[q], 1);
                            mot(r, "écran", &r->e[q]);
                        } else copier(r, &r->e[q]);
                    }
                    emettre(r, J_DEUX_POINTS, NULL, &r->e[f - 1], 1);
                    ouvrir(r, O_FORMULE, prof, t);
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
    if ((t->type == J_CROCHETS || t->type == J_PAR_OUV) && gagne < f && gagne >= d + 3 && r->e[gagne - 2].type == J_POINT
        && r->e[gagne - 1].type == J_CROCHETS) {   /* (_l'œuvre _conservé _dont …).genres _gagne g (2.51) */
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
    /* (_le cabinet _conservé).logo << x : le champ d'un objet cherché, entre parenthèses (2.33 ; la ligne n'était pas relue) */
    const size_t ferme_objet = t->type == J_PAR_OUV ? fermante(r, d, f) : f;
    const int champ_cherche = (t->type == J_PAR_OUV && affecte < f && ferme_objet + 2 < affecte
                               && r->e[ferme_objet + 1].type == J_POINT)
        /* « _la séance.date << … » : l'objet du champ avec son article écrit (2.36) */
        || ((est_cle(t, "le") || est_cle(t, "la") || est_cle(t, "l'") || est_cle(t, "nu")) && affecte < f && d + 3 < affecte
            && r->e[d + 1].type == J_CROCHETS && r->e[d + 2].type == J_POINT)
        /* « fiche_inventée(rang).archivage << … » : le champ du résultat d'un calcul (2.51) */
        || (t->type == J_CROCHETS && d + 1 < f && r->e[d + 1].type == J_PAR_OUV && affecte < f
            && fermante(r, d + 1, f) + 2 < affecte && r->e[fermante(r, d + 1, f) + 1].type == J_POINT);
    if (((t->type == J_CROCHETS || (est_cle(t, "écran") && d + 1 < f && r->e[d + 1].type == J_POINT)) && affecte < f && affecte > d
         && (affecte == d + 1 || r->e[d + 1].type == J_POINT)) || champ_cherche) {
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
    } else if (t->type == J_CROCHETS && d + 1 < f && r->e[d + 1].type == J_PAR_OUV && f >= 3 && r->e[f - 1].type == J_CROCHETS
               && ((fermante(r, d + 1, f) == f - 3 && est_cle(&r->e[f - 2], "qui_donne"))
                   || (f >= 4 && fermante(r, d + 1, f) == f - 4 && est_cle(&r->e[f - 3], "qui_donne")
                       && (est_cle(&r->e[f - 2], "le") || est_cle(&r->e[f - 2], "la") || est_cle(&r->e[f - 2], "l'"))))) {
        /* quittancer(120) _qui_donne _la q → Quittancer 120, qui donne la q. (2.57) */
        const int avec_article = est_cle(&r->e[f - 3], "qui_donne");
        const size_t qd = avec_article ? f - 3 : f - 2;
        copier(r, t);
        arguments(r, d + 2, qd - 1, 0, t);
        emettre(r, J_VIRGULE, NULL, &r->e[qd], 1);
        mot(r, "qui", &r->e[qd]);
        mot(r, "donne", &r->e[qd]);
        if (avec_article) {
            if (est_cle(&r->e[f - 2], "l'")) emettre(r, J_ELISION, "l", &r->e[f - 2], 1);
            else mot(r, r->e[f - 2].valeur, &r->e[f - 2]);
        }
        copier(r, &r->e[f - 1]);
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
        while (k < n && e[k].type != J_FIN && (p > 0 || e[k].ligne == e[k - 1].ligne || e[k].type == J_GABARIT_DEBUT)
               && !(p == 0 && e[k - 1].type == J_GABARIT_FIN) && e[d].type != J_REMARQUE) {
            if (e[k].type == J_PAR_OUV || e[k].type == J_GABARIT_DEBUT) p++;   /* le bloc d'un texte suivant (§ 4.7) */
            else if (e[k].type == J_PAR_FERM || e[k].type == J_GABARIT_FIN) p--;
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
