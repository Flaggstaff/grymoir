/* GrymoiR : machine virtuelle à pile, v0.2
 * Spécification : docs/vm.md (révision 1.0).
 */
#include "vm.h"
#include "decimal.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- */
/* Valeurs (docs/vm.md, § 2)                                        */
/* ---------------------------------------------------------------- */

typedef enum { V_NOMBRE, V_TEXTE, V_BOOLEEN } TypeValeur;

typedef struct {
    TypeValeur type;
    Decimal nombre;
    char *texte;
    int vrai;
} Valeur;

static const char *nom_type(TypeValeur t) {
    return t == V_NOMBRE ? "un nombre" : t == V_TEXTE ? "un texte" : "un booléen";
}

static Valeur valeur_copier(const Valeur *v) {
    Valeur r;
    r.type = v->type;
    r.nombre = v->type == V_NOMBRE ? dec_copier(&v->nombre) : dec_zero();
    r.texte = v->type == V_TEXTE ? grym_dupliquer(v->texte) : NULL;
    r.vrai = v->vrai;
    return r;
}

static void valeur_liberer(Valeur *v) {
    dec_liberer(&v->nombre);
    free(v->texte);
    v->texte = NULL;
}

static Valeur valeur_nombre(Decimal d) {
    Valeur v;
    v.type = V_NOMBRE;
    v.nombre = d;
    v.texte = NULL;
    v.vrai = 0;
    return v;
}

static Valeur valeur_booleen(int vrai) {
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_BOOLEEN;
    v.vrai = vrai != 0;
    return v;
}

/* ---------------------------------------------------------------- */
/* Machine : table globale et journal d'annulation                  */
/* ---------------------------------------------------------------- */

typedef struct {
    char *nom;
    int definie;
    Valeur valeur;
} Case;

typedef struct {
    size_t c;          /* case modifiée */
    int etait_definie;
    Valeur ancienne;
} Ecriture;

struct Machine {
    Case *cases;
    size_t nb_cases, cap_cases;
    Ecriture *journal;
    size_t nb_journal, cap_journal;
};

Machine *machine_creer(void) {
    Machine *m = grym_allouer(sizeof *m);
    memset(m, 0, sizeof *m);
    return m;
}

void machine_detruire(Machine *m) {
    if (!m) return;
    for (size_t i = 0; i < m->nb_cases; i++) {
        free(m->cases[i].nom);
        if (m->cases[i].definie) valeur_liberer(&m->cases[i].valeur);
    }
    free(m->cases);
    free(m->journal);
    free(m);
}

/* Liaison (docs/vm.md, § 5) : case de la table globale pour un nom, créée au besoin. */
static size_t case_de(Machine *m, const char *nom) {
    for (size_t i = 0; i < m->nb_cases; i++)
        if (strcmp(m->cases[i].nom, nom) == 0) return i;
    if (m->nb_cases == m->cap_cases) {
        m->cap_cases = m->cap_cases ? m->cap_cases * 2 : 16;
        Case *c = grym_allouer(m->cap_cases * sizeof *c);
        if (m->nb_cases) memcpy(c, m->cases, m->nb_cases * sizeof *c);
        free(m->cases);
        m->cases = c;
    }
    Case *c = &m->cases[m->nb_cases];
    c->nom = grym_dupliquer(nom);
    c->definie = 0;
    return m->nb_cases++;
}

static void ecrire(Machine *m, size_t c, Valeur v) {
    if (m->nb_journal == m->cap_journal) {
        m->cap_journal = m->cap_journal ? m->cap_journal * 2 : 16;
        Ecriture *j = grym_allouer(m->cap_journal * sizeof *j);
        if (m->nb_journal) memcpy(j, m->journal, m->nb_journal * sizeof *j);
        free(m->journal);
        m->journal = j;
    }
    Ecriture *e = &m->journal[m->nb_journal++];
    e->c = c;
    e->etait_definie = m->cases[c].definie;
    if (e->etait_definie) e->ancienne = m->cases[c].valeur;   /* la valeur passe au journal */
    m->cases[c].valeur = v;
    m->cases[c].definie = 1;
}

/* Rejoue le journal du plus récent au plus ancien. */
static void annuler(Machine *m) {
    while (m->nb_journal) {
        Ecriture *e = &m->journal[--m->nb_journal];
        Case *c = &m->cases[e->c];
        valeur_liberer(&c->valeur);
        c->definie = e->etait_definie;
        if (e->etait_definie) c->valeur = e->ancienne;
    }
}

static void valider(Machine *m) {
    for (size_t i = 0; i < m->nb_journal; i++)
        if (m->journal[i].etait_definie) valeur_liberer(&m->journal[i].ancienne);
    m->nb_journal = 0;
}

/* ---------------------------------------------------------------- */
/* Exécution                                                        */
/* ---------------------------------------------------------------- */

typedef struct {
    Valeur *v;
    size_t n, cap;
} Pile;

static void empiler(Pile *p, Valeur v) {
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        Valeur *nv = grym_allouer(p->cap * sizeof *nv);
        if (p->n) memcpy(nv, p->v, p->n * sizeof *nv);
        free(p->v);
        p->v = nv;
    }
    p->v[p->n++] = v;
}

static Valeur depiler(Pile *p) { return p->v[--p->n]; }

static void vider(Pile *p) {
    while (p->n) valeur_liberer(&p->v[--p->n]);
    free(p->v);
    p->v = NULL;
    p->cap = 0;
}

static int echouer(Diagnostic *d, const Bloc *b, size_t decalage, char *message) {
    d->message = message;
    bloc_position(b, decalage, &d->ligne, &d->colonne);
    return 0;
}

static char *message_statut(StatutDecimal st) {
    switch (st) {
    case DEC_DIVISION_PAR_ZERO:
        return grym_dupliquer("Division par zéro.");
    case DEC_TROP_GRAND:
        return grym_formater("Nombre trop grand : un résultat est limité à %d chiffres.", DEC_CHIFFRES_MAX);
    case DEC_EXPOSANT_NON_ENTIER:
        return grym_dupliquer("Exposant non entier : en v0.1, la puissance n'accepte qu'un exposant "
                              "entier (2 ^ 3, 2 ^ −1).");
    default:
        return grym_dupliquer("Erreur interne de calcul.");
    }
}

int machine_executer(Machine *m, const Bloc *b, Chaine *sortie, Diagnostic *diag) {
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;

    char *erreur = NULL;
    if (!bloc_verifier(b, &erreur)) {
        diag->message = grym_formater("Bytecode invalide : %s", erreur);
        free(erreur);
        return 0;
    }

    /* Liaison des noms du bloc aux cases de la machine. */
    size_t *liaison = grym_allouer((b->nb_noms ? b->nb_noms : 1) * sizeof *liaison);
    for (size_t i = 0; i < b->nb_noms; i++) liaison[i] = case_de(m, b->noms[i]);

    Pile pile = { NULL, 0, 0 };
    int ok = 1;
    size_t ip = 0;
    for (;;) {
        size_t debut = ip;
        CodeInstruction code = (CodeInstruction)b->code[ip];
        unsigned op = 0;
        size_t cible = 0;
        if (instruction_a_operande(code))
            op = (unsigned)b->code[ip + 1] | ((unsigned)b->code[ip + 2] << 8);
        if (code == I_SAUTER || code == I_SAUTER_SI_FAUX)
            cible = (size_t)b->code[ip + 1] | ((size_t)b->code[ip + 2] << 8)
                  | ((size_t)b->code[ip + 3] << 16) | ((size_t)b->code[ip + 4] << 24);
        ip += instruction_taille(code);
        if (code == I_RETOUR) break;

        switch (code) {
        case I_CONSTANTE: {
            const Constante *k = &b->constantes[op];
            Valeur v;
            if (k->type == C_NOMBRE) {
                v = valeur_nombre(dec_depuis_canonique(k->texte));
            } else if (k->type == C_BOOLEEN) {
                v = valeur_booleen(strcmp(k->texte, "vrai") == 0);
            } else {
                v = valeur_nombre(dec_zero());
                v.type = V_TEXTE;
                v.texte = grym_dupliquer(k->texte);
            }
            empiler(&pile, v);
            break;
        }
        case I_LIRE: {
            Case *c = &m->cases[liaison[op]];
            if (!c->definie) {
                ok = echouer(diag, b, debut, grym_formater("« %s » n'a pas de valeur.", c->nom));
                break;
            }
            empiler(&pile, valeur_copier(&c->valeur));
            break;
        }
        case I_ECRIRE:
            ecrire(m, liaison[op], depiler(&pile));
            break;
        case I_NEGATION: {
            Valeur *x = &pile.v[pile.n - 1];
            if (x->type != V_NOMBRE) {
                ok = echouer(diag, b, debut, grym_formater("Opposé impossible : la valeur est %s.",
                                                           nom_type(x->type)));
                break;
            }
            Decimal d = dec_negation(&x->nombre);
            dec_liberer(&x->nombre);
            x->nombre = d;
            break;
        }
        case I_ADDITION: case I_SOUSTRACTION: case I_MULTIPLICATION:
        case I_DIVISION: case I_PUISSANCE: {
            Valeur vb = depiler(&pile), va = depiler(&pile);
            if (va.type != V_NOMBRE || vb.type != V_NOMBRE) {
                valeur_liberer(&va);
                valeur_liberer(&vb);
                ok = echouer(diag, b, debut, grym_formater(
                    "%s impossible : un des opérandes est %s.", instruction_nom(code),
                    nom_type(va.type != V_NOMBRE ? va.type : vb.type)));
                break;
            }
            Decimal r;
            StatutDecimal st =
                code == I_ADDITION       ? dec_addition(&va.nombre, &vb.nombre, &r) :
                code == I_SOUSTRACTION   ? dec_soustraction(&va.nombre, &vb.nombre, &r) :
                code == I_MULTIPLICATION ? dec_multiplication(&va.nombre, &vb.nombre, &r) :
                code == I_DIVISION       ? dec_division(&va.nombre, &vb.nombre, &r) :
                                           dec_puissance(&va.nombre, &vb.nombre, &r);
            valeur_liberer(&va);
            valeur_liberer(&vb);
            if (st != DEC_OK) {
                dec_liberer(&r);
                ok = echouer(diag, b, debut, message_statut(st));
                break;
            }
            empiler(&pile, valeur_nombre(r));
            break;
        }
        case I_AFFICHER: {
            /* Les éléments sont dans la pile, du premier au dernier. */
            size_t base = pile.n - op;
            for (size_t k = 0; k < op; k++) {
                Valeur *v = &pile.v[base + k];
                if (k) chaine_ajouter(sortie, " ");
                if (v->type == V_TEXTE) {
                    chaine_ajouter(sortie, v->texte);
                } else if (v->type == V_BOOLEEN) {
                    chaine_ajouter(sortie, v->vrai ? "vrai" : "faux");
                } else {
                    char *s = dec_formater(&v->nombre);
                    chaine_ajouter(sortie, s);
                    free(s);
                }
            }
            chaine_ajouter(sortie, "\n");
            while (pile.n > base) valeur_liberer(&pile.v[--pile.n]);
            break;
        }
        case I_EGAL: case I_DIFFERENT: case I_INFERIEUR: case I_SUPERIEUR:
        case I_INFERIEUR_OU_EGAL: case I_SUPERIEUR_OU_EGAL: {
            Valeur vb = depiler(&pile), va = depiler(&pile);
            int egalite = code == I_EGAL || code == I_DIFFERENT;
            int resultat = 0;
            if (va.type != vb.type || (!egalite && va.type != V_NOMBRE)) {
                ok = echouer(diag, b, debut, va.type != vb.type
                    ? grym_formater("Comparaison impossible entre %s et %s.",
                                    nom_type(va.type), nom_type(vb.type))
                    : grym_formater("Seuls deux nombres se comparent par ordre : la valeur est %s.",
                                    nom_type(va.type)));
            } else if (va.type == V_NOMBRE) {
                int c = dec_comparer(&va.nombre, &vb.nombre);
                resultat = code == I_EGAL ? c == 0 : code == I_DIFFERENT ? c != 0
                         : code == I_INFERIEUR ? c < 0 : code == I_SUPERIEUR ? c > 0
                         : code == I_INFERIEUR_OU_EGAL ? c <= 0 : c >= 0;
            } else {
                int egaux = va.type == V_BOOLEEN ? va.vrai == vb.vrai : strcmp(va.texte, vb.texte) == 0;
                resultat = code == I_EGAL ? egaux : !egaux;
            }
            valeur_liberer(&va);
            valeur_liberer(&vb);
            if (ok) empiler(&pile, valeur_booleen(resultat));
            break;
        }
        case I_NON: {
            Valeur *x = &pile.v[pile.n - 1];
            if (x->type != V_BOOLEEN) {
                ok = echouer(diag, b, debut, grym_formater("Négation impossible : la valeur est %s, "
                                                           "ni vraie ni fausse.", nom_type(x->type)));
                break;
            }
            x->vrai = !x->vrai;
            break;
        }
        case I_SAUTER:
            ip = cible;
            break;
        case I_SAUTER_SI_FAUX: {
            Valeur v = depiler(&pile);
            if (v.type != V_BOOLEEN) {
                ok = echouer(diag, b, debut, grym_formater(
                    "Condition ni vraie ni fausse : la valeur est %s.", nom_type(v.type)));
            } else if (!v.vrai) {
                ip = cible;
            }
            valeur_liberer(&v);
            break;
        }
        default:
            ok = echouer(diag, b, debut, grym_dupliquer("Instruction inconnue."));
            break;
        }
        if (!ok) break;
    }

    vider(&pile);
    free(liaison);
    if (ok) valider(m);
    else annuler(m);
    return ok;
}
