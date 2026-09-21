/* GrymoiR : base de données des entités, sur SQLite embarqué
 * Spécification : docs/grammaire.md (révision 1.17), § 16 ; docs/vm.md (révision 1.11), § 8.
 */
#include "base.h"
#include "date.h"
#include "texte.h"

#include "sqlite3.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct Base {
    sqlite3 *db;
    char *chemin;     /* tel qu'affiché dans les messages */
    int transaction;
};

/* ---------------------------------------------------------------- */
/* Outils                                                           */
/* ---------------------------------------------------------------- */

/* Nom SQL entre guillemets doubles : « e client », « c date d'inscription ». */
static void ajouter_nom(Chaine *c, const char *prefixe, const char *nom) {
    chaine_ajouter(c, "\"");
    chaine_ajouter(c, prefixe);
    for (const char *p = nom; *p; p++) {
        char t[3] = { *p, 0, 0 };
        if (*p == '"') t[1] = '"';
        chaine_ajouter(c, t);
    }
    chaine_ajouter(c, "\"");
}

static int executer(Base *b, const char *sql, char **erreur) {
    char *msg = NULL;
    if (sqlite3_exec(b->db, sql, NULL, NULL, &msg) == SQLITE_OK) return 1;
    *erreur = grym_formater("Base « %s » : %s.", b->chemin, msg ? msg : sqlite3_errmsg(b->db));
    sqlite3_free(msg);
    return 0;
}

static int est_fichier(const char *type) {
    return strcmp(type, "fichier") == 0 || strcmp(type, "image") == 0;
}

static int est_lien(const char *type) {
    static const char *const BASE[] = { "texte", "nombre", "nombre entier", "vrai ou faux", "date", "fichier", "image" };
    for (size_t k = 0; k < sizeof BASE / sizeof *BASE; k++) if (strcmp(type, BASE[k]) == 0) return 0;
    return 1;
}

static int commence_par_voyelle(const char *s) {
    return strchr("aeiouyAEIOUY", s[0]) != NULL || strncmp(s, "é", strlen("é")) == 0 || strncmp(s, "h", 1) == 0;
}

static char *pluriel(const ClasseVM *c) {
    return c->pluriel ? grym_dupliquer(c->pluriel) : grym_formater("%ss", c->nom);
}

/* « un client », « une facture » */
static char *un(const ClasseVM *c) {
    return grym_formater("%s %s", c->feminin ? "une" : "un", c->nom);
}

/* Lignée d'une entité, de la racine à la classe elle-même. */
static size_t lignee(const ClasseVM *c, const ClasseVM **l, size_t max) {
    size_t n = 0;
    for (const ClasseVM *x = c; x && n < max; x = x->parent) n++;
    size_t k = n;
    for (const ClasseVM *x = c; x && k > 0; x = x->parent) l[--k] = x;
    return n;
}

#define LIGNEE_MAX 256

/* ---------------------------------------------------------------- */
/* Ouverture et transaction                                         */
/* ---------------------------------------------------------------- */

Base *base_ouvrir(const char *chemin, char **erreur) {
    Base *b = grym_allouer(sizeof *b);
    b->db = NULL;
    b->transaction = 0;
    b->chemin = grym_dupliquer(chemin ? chemin : "en mémoire");
    if (sqlite3_open_v2(chemin ? chemin : ":memory:", &b->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)
        != SQLITE_OK) {
        *erreur = grym_formater("Base « %s » : ouverture impossible (%s).", b->chemin,
                                b->db ? sqlite3_errmsg(b->db) : "mémoire épuisée");
        base_fermer(b);
        return NULL;
    }
    sqlite3_busy_timeout(b->db, 2000);
    if (!executer(b, "PRAGMA foreign_keys = ON;"
                     "CREATE TABLE IF NOT EXISTS grym_objet (id INTEGER PRIMARY KEY AUTOINCREMENT, classe TEXT NOT NULL);"
                     "CREATE TABLE IF NOT EXISTS grym_schema (entite TEXT PRIMARY KEY, definition TEXT NOT NULL);",
                  erreur)) {
        base_fermer(b);
        return NULL;
    }
    return b;
}

void base_fermer(Base *b) {
    if (!b) return;
    if (b->transaction) base_annuler(b);
    sqlite3_close(b->db);
    free(b->chemin);
    free(b);
}

int base_commencer(Base *b, char **erreur) {
    if (b->transaction) return 1;
    /* IMMEDIATE : la base est réservée en écriture dès le début de l'exécution (§ 16.6). */
    int rc = sqlite3_exec(b->db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
    if (rc == SQLITE_BUSY) {
        *erreur = grym_formater("La base « %s » est utilisée par un autre programme.", b->chemin);
        return 0;
    }
    if (rc != SQLITE_OK) {
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        return 0;
    }
    b->transaction = 1;
    return 1;
}

int base_valider(Base *b, char **erreur) {
    if (!b->transaction) return 1;
    b->transaction = 0;
    if (sqlite3_exec(b->db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK) return 1;
    *erreur = grym_formater("Base « %s » : validation impossible (%s).", b->chemin, sqlite3_errmsg(b->db));
    sqlite3_exec(b->db, "ROLLBACK;", NULL, NULL, NULL);
    return 0;
}

void base_annuler(Base *b) {
    if (!b->transaction) return;
    b->transaction = 0;
    sqlite3_exec(b->db, "ROLLBACK;", NULL, NULL, NULL);
}

/* ---------------------------------------------------------------- */
/* Schéma                                                           */
/* ---------------------------------------------------------------- */

/* Définition d'une entité : parent, puis chaque champ de sa table, « nom:type:unique ». */
static char *definition(const ClasseVM *c) {
    Chaine d = {0};
    chaine_ajouter(&d, "parent=");
    chaine_ajouter(&d, c->parent ? c->parent->nom : "");
    for (size_t k = 0; k < c->nb_champs; k++) {
        if (c->proprietaires[k] != c) continue;
        chaine_ajouter(&d, ";");
        chaine_ajouter(&d, c->champs[k]);
        chaine_ajouter(&d, ":");
        chaine_ajouter(&d, c->types[k] ? c->types[k] : "");
        chaine_ajouter(&d, c->uniques[k] ? ":unique" : "");
    }
    return chaine_rendre(&d);
}

int base_preparer(Base *b, const ClasseVM *c, char **erreur) {
    if (!c->conserve) return 1;
    char *def = definition(c);
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "SELECT definition FROM grym_schema WHERE entite = ?", -1, &st, NULL);
    sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        int pareil = strcmp((const char *)sqlite3_column_text(st, 0), def) == 0;
        sqlite3_finalize(st);
        free(def);
        if (pareil) return 1;
        *erreur = grym_formater("La base « %s » connaît « %s » avec une autre définition : les migrations de schéma "
                                "ne sont pas encore prises en charge.", b->chemin, c->nom);
        return 0;
    }
    sqlite3_finalize(st);

    Chaine sql = {0};
    chaine_ajouter(&sql, "CREATE TABLE ");
    ajouter_nom(&sql, "e ", c->nom);
    chaine_ajouter(&sql, " (id INTEGER PRIMARY KEY REFERENCES ");
    if (c->parent) ajouter_nom(&sql, "e ", c->parent->nom);
    else chaine_ajouter(&sql, "grym_objet");
    chaine_ajouter(&sql, "(id) ON DELETE CASCADE");
    for (size_t k = 0; k < c->nb_champs; k++) {
        if (c->proprietaires[k] != c) continue;
        const char *t = c->types[k];
        chaine_ajouter(&sql, ", ");
        ajouter_nom(&sql, "c ", c->champs[k]);
        if (strcmp(t, "nombre entier") == 0) chaine_ajouter(&sql, " INTEGER NOT NULL");
        else if (strcmp(t, "vrai ou faux") == 0) {
            chaine_ajouter(&sql, " INTEGER NOT NULL CHECK (");
            ajouter_nom(&sql, "c ", c->champs[k]);
            chaine_ajouter(&sql, " IN (0, 1))");
        } else if (est_fichier(t)) chaine_ajouter(&sql, " BLOB NOT NULL");
        else if (est_lien(t)) {
            chaine_ajouter(&sql, " INTEGER NOT NULL REFERENCES ");
            ajouter_nom(&sql, "e ", t);
            chaine_ajouter(&sql, "(id)");
        } else chaine_ajouter(&sql, " TEXT NOT NULL");
        if (c->uniques[k]) chaine_ajouter(&sql, " UNIQUE");
        if (est_fichier(t)) {
            chaine_ajouter(&sql, ", ");
            ajouter_nom(&sql, "n ", c->champs[k]);
            chaine_ajouter(&sql, " TEXT NOT NULL");
        }
    }
    chaine_ajouter(&sql, ");");
    char *texte = chaine_rendre(&sql);
    int ok = executer(b, texte, erreur);
    free(texte);
    if (ok) {
        sqlite3_prepare_v2(b->db, "INSERT INTO grym_schema (entite, definition) VALUES (?, ?)", -1, &st, NULL);
        sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, def, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
        if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        sqlite3_finalize(st);
    }
    free(def);
    return ok;
}

/* ---------------------------------------------------------------- */
/* Valeurs                                                          */
/* ---------------------------------------------------------------- */

/* Lie la valeur d'un champ à partir du paramètre i ; renvoie le paramètre suivant, 0 en cas d'erreur. */
/* soi, id_soi : l'objet en train d'être conservé, qui peut se désigner lui-même (« un parrain (client) »). */
static int lier(Base *b, sqlite3_stmt *st, int i, const char *type, const char *champ, const Valeur *v,
                const Objet *soi, long id_soi, char **erreur) {
    if (strcmp(type, "texte") == 0) {
        sqlite3_bind_text(st, i, v->texte, -1, SQLITE_TRANSIENT);
    } else if (strcmp(type, "nombre") == 0) {
        char *t = dec_canonique(&v->nombre);
        sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
        free(t);
    } else if (strcmp(type, "nombre entier") == 0) {
        char *t = dec_canonique(&v->nombre), *fin = NULL;
        errno = 0;
        long long n = strtoll(t, &fin, 10);
        int ok = errno == 0 && fin != t && (*fin == '\0' || (*fin == '.' && strspn(fin + 1, "0") == strlen(fin + 1)));
        free(t);
        if (!ok) {
            *erreur = grym_formater("Le champ « %s » est trop grand pour la base : un nombre entier y tient entre "
                                    "−9'223'372'036'854'775'808 et 9'223'372'036'854'775'807.", champ);
            return 0;
        }
        sqlite3_bind_int64(st, i, (sqlite3_int64)n);
    } else if (strcmp(type, "vrai ou faux") == 0) {
        sqlite3_bind_int(st, i, v->vrai ? 1 : 0);
    } else if (strcmp(type, "date") == 0) {
        char *t = date_iso(v->jours);
        sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
        free(t);
    } else if (est_fichier(type)) {
        sqlite3_bind_blob64(st, i, v->fichier->octets, (sqlite3_uint64)v->fichier->taille, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, i + 1, v->fichier->nom, -1, SQLITE_TRANSIENT);
        return i + 2;
    } else if (v->objet == soi) {
        sqlite3_bind_int64(st, i, (sqlite3_int64)id_soi);
    } else {
        if (!v->objet->id) {
            char *qui = un(v->objet->classe);
            *erreur = grym_formater("Le champ « %s » désigne %s qui n'est pas conservé%s : conservez-%s d'abord.", champ,
                                    qui, v->objet->classe->feminin ? "e" : "", v->objet->classe->feminin ? "la" : "le");
            free(qui);
            return 0;
        }
        sqlite3_bind_int64(st, i, (sqlite3_int64)v->objet->id);
    }
    (void)b;
    return i + 1;
}

/* Message d'une contrainte refusée par SQLite. */
static char *message_contrainte(Base *b, const Objet *o) {
    int code = sqlite3_extended_errcode(b->db);
    const char *m = sqlite3_errmsg(b->db);
    if (code == SQLITE_CONSTRAINT_UNIQUE) {
        const char *p = strstr(m, ".c ");   /* « UNIQUE constraint failed: e client.c licence » */
        if (!p) return grym_formater("Base « %s » : %s.", b->chemin, m);
        const ClasseVM *c = o->classe;
        char *valeur = NULL;
        for (size_t k = 0; k < c->nb_champs && !valeur; k++) {
            if (strcmp(c->champs[k], p + 3) != 0 || !o->definis[k]) continue;
            const Valeur *v = &o->champs[k];
            valeur = v->type == V_TEXTE ? grym_formater("« %s »", v->texte)
                   : v->type == V_NOMBRE ? dec_formater(&v->nombre)
                   : v->type == V_DATE ? date_suisse(v->jours) : grym_dupliquer("cette valeur");
        }
        char *r = grym_formater("« %s » est unique : %s %s conservé%s a déjà %s.", p + 3,
                                c->feminin ? "une autre" : "un autre", c->nom, c->feminin ? "e" : "",
                                valeur ? valeur : "cette valeur");
        free(valeur);
        return r;
    }
    if (code == SQLITE_CONSTRAINT_FOREIGNKEY)
        return grym_dupliquer("Un lien désigne un objet qui n'est plus conservé.");
    return grym_formater("Base « %s » : %s.", b->chemin, m);
}

/* ---------------------------------------------------------------- */
/* Conserver, écrire, supprimer                                     */
/* ---------------------------------------------------------------- */

int base_conserver(Base *b, const Objet *o, long *id, char **erreur) {
    const ClasseVM *c = o->classe;
    for (size_t k = 0; k < c->nb_champs; k++)
        if (!o->definis[k]) {
            char *qui = un(c);
            *erreur = grym_formater("Le champ « %s » n'a pas de valeur : %s incomplet%s ne se conserve pas.",
                                    c->champs[k], qui, c->feminin ? "e" : "");
            free(qui);
            return 0;
        }
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "INSERT INTO grym_objet (classe) VALUES (?)", -1, &st, NULL);
    sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) { *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db)); return 0; }
    long nouvel = (long)sqlite3_last_insert_rowid(b->db);

    const ClasseVM *l[LIGNEE_MAX];
    size_t n = lignee(c, l, LIGNEE_MAX);
    for (size_t e = 0; e < n; e++) {
        Chaine sql = {0}, valeurs = {0};
        chaine_ajouter(&sql, "INSERT INTO ");
        ajouter_nom(&sql, "e ", l[e]->nom);
        chaine_ajouter(&sql, " (id");
        chaine_ajouter(&valeurs, "?");
        for (size_t k = 0; k < c->nb_champs; k++) {
            if (c->proprietaires[k] != l[e]) continue;
            chaine_ajouter(&sql, ", ");
            ajouter_nom(&sql, "c ", c->champs[k]);
            chaine_ajouter(&valeurs, ", ?");
            if (est_fichier(c->types[k])) {
                chaine_ajouter(&sql, ", ");
                ajouter_nom(&sql, "n ", c->champs[k]);
                chaine_ajouter(&valeurs, ", ?");
            }
        }
        chaine_ajouter(&sql, ") VALUES (");
        char *v = chaine_rendre(&valeurs);
        chaine_ajouter(&sql, v);
        free(v);
        chaine_ajouter(&sql, ")");
        char *texte = chaine_rendre(&sql);
        sqlite3_prepare_v2(b->db, texte, -1, &st, NULL);
        free(texte);
        sqlite3_bind_int64(st, 1, (sqlite3_int64)nouvel);
        int i = 2;
        for (size_t k = 0; k < c->nb_champs && i; k++)
            if (c->proprietaires[k] == l[e])
                i = lier(b, st, i, c->types[k], c->champs[k], &o->champs[k], o, nouvel, erreur);
        ok = i && sqlite3_step(st) == SQLITE_DONE;
        if (i && !ok) *erreur = message_contrainte(b, o);
        sqlite3_finalize(st);
        if (!ok) return 0;   /* la transaction de l'exécution annulera les lignes déjà écrites */
    }
    *id = nouvel;
    return 1;
}

int base_ecrire_champ(Base *b, const Objet *o, size_t k, char **erreur) {
    const ClasseVM *c = o->classe;
    const ClasseVM *table = c->proprietaires[k];
    if (!table || !c->types[k]) return 1;
    Chaine sql = {0};
    chaine_ajouter(&sql, "UPDATE ");
    ajouter_nom(&sql, "e ", table->nom);
    chaine_ajouter(&sql, " SET ");
    ajouter_nom(&sql, "c ", c->champs[k]);
    chaine_ajouter(&sql, " = ?");
    if (est_fichier(c->types[k])) {
        chaine_ajouter(&sql, ", ");
        ajouter_nom(&sql, "n ", c->champs[k]);
        chaine_ajouter(&sql, " = ?");
    }
    chaine_ajouter(&sql, " WHERE id = ?");
    char *texte = chaine_rendre(&sql);
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, texte, -1, &st, NULL);
    free(texte);
    int i = lier(b, st, 1, c->types[k], c->champs[k], &o->champs[k], o, o->id, erreur);
    int ok = 0;
    if (i) {
        sqlite3_bind_int64(st, i, (sqlite3_int64)o->id);
        ok = sqlite3_step(st) == SQLITE_DONE;
        if (!ok) *erreur = message_contrainte(b, o);
    }
    sqlite3_finalize(st);
    return ok;
}

int base_supprimer(Base *b, const Objet *o, ClasseVM *const *classes, size_t nb_classes, char **erreur) {
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "DELETE FROM grym_objet WHERE id = ?", -1, &st, NULL);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return 1;
    if (sqlite3_extended_errcode(b->db) != SQLITE_CONSTRAINT_FOREIGNKEY) {
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        return 0;
    }
    /* Qui le désigne encore ? Un champ de lien vers sa classe ou l'une de ses classes parentes. */
    const ClasseVM *oc = o->classe;
    for (size_t i = 0; i < nb_classes; i++) {
        const ClasseVM *x = classes[i];
        if (!x->conserve) continue;
        for (size_t k = 0; k < x->nb_champs; k++) {
            if (x->proprietaires[k] != x || !x->types[k] || !est_lien(x->types[k])) continue;
            int vise = 0;
            for (const ClasseVM *p = oc; p && !vise; p = p->parent) vise = strcmp(p->nom, x->types[k]) == 0;
            if (!vise) continue;
            Chaine sql = {0};
            chaine_ajouter(&sql, "SELECT count(*) FROM ");
            ajouter_nom(&sql, "e ", x->nom);
            chaine_ajouter(&sql, " WHERE ");
            ajouter_nom(&sql, "c ", x->champs[k]);
            chaine_ajouter(&sql, " = ?1 AND id <> ?1");   /* un objet qui se désigne lui-même ne compte pas */
            char *texte = chaine_rendre(&sql);
            sqlite3_prepare_v2(b->db, texte, -1, &st, NULL);
            free(texte);
            sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
            long n = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : 0;
            sqlite3_finalize(st);
            if (n == 0) continue;
            const char *ce = oc->feminin ? "Cette" : commence_par_voyelle(oc->nom) ? "Cet" : "Ce";
            char *qui = n > 1 ? pluriel(x) : un(x);
            *erreur = n > 1 ? grym_formater("%s %s est encore désigné%s par le champ « %s » de %ld %s.", ce, oc->nom,
                                            oc->feminin ? "e" : "", x->champs[k], n, qui)
                            : grym_formater("%s %s est encore désigné%s par le champ « %s » d'%s.", ce, oc->nom,
                                            oc->feminin ? "e" : "", x->champs[k], qui);
            free(qui);
            return 0;
        }
    }
    *erreur = grym_dupliquer("Un lien désigne encore cet objet.");
    return 0;
}
