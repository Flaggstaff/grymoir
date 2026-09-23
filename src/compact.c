/* GrymoiR : lecture de la forme compacte, v0.2
 * Spécification : docs/grammaire.md (révision 1.32), § 11.
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

typedef enum { O_SI, O_BOUCLE, O_SELON, O_FORMULE, O_CLASSE, O_INIT, O_ESSAI } Ouverture;

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
        || (t->type == J_MOT && (strcmp(t->valeur, "vrai") == 0 || strcmp(t->valeur, "faux") == 0));
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
        if (est_cle(t, "sur")) {                    /* nom _sur 20 _droite → nom sur 20 à droite (§ 4.2) */
            mot(r, "sur", t);
            continue;
        }
        if (est_cle(t, "droite") || est_cle(t, "gauche")) {
            mot(r, "à", t);
            mot(r, est_cle(t, "droite") ? "droite" : "gauche", t);
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
        if (est_cle(t, "fichier")) {                /* _fichier « a.jpg » → le fichier « a.jpg » */
            mot(r, "le", t);
            mot(r, "fichier", t);
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
        /* _un nom [(type)] [_unique] */
        size_t q = d + 2;
        int type = q + 2 < f && r->e[q].type == J_PAR_OUV && r->e[q + 1].type == J_CROCHETS
                   && r->e[q + 2].type == J_PAR_FERM;
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
    if (t->type == J_CROCHETS && affecte < f && affecte > d
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
