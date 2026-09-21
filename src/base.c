/* GrymoiR : base de données des entités, sur SQLite embarqué
 * Spécification : docs/grammaire.md (révision 1.18), § 16 ; docs/vm.md (révision 1.12), § 8.
 */
#include "base.h"
#include "date.h"
#include "texte.h"

#include "sqlite3.h"

#include <errno.h>
#include <stdio.h>
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

int base_collations(sqlite3 *db);

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
    if (!base_collations(b->db)) {
        *erreur = grym_formater("Base « %s » : collations impossibles.", b->chemin);
        base_fermer(b);
        return NULL;
    }
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

/* ---------------------------------------------------------------- */
/* Collations : comparer comme GrymoiR, pas comme des octets         */
/* ---------------------------------------------------------------- */

/* GRYM_NOMBRE : deux textes canoniques comparés en décimal exact (« 10 » > « 9 », « 3.0 » = « 3 »). */
static int collation_nombre(void *ctx, int na, const void *a, int nb, const void *b) {
    (void)ctx;
    char *x = grym_allouer((size_t)na + 1), *y = grym_allouer((size_t)nb + 1);
    memcpy(x, a, (size_t)na);
    x[na] = '\0';
    memcpy(y, b, (size_t)nb);
    y[nb] = '\0';
    Decimal dx = dec_depuis_canonique(x), dy = dec_depuis_canonique(y);
    int c = dec_comparer(&dx, &dy);
    dec_liberer(&dx);
    dec_liberer(&dy);
    free(x);
    free(y);
    return c;
}

/* Lettre de base d'une lettre latine accentuée (UTF-8), en minuscule ; 0 si ce n'en est pas une. */
static int base_lettre(const unsigned char *p, int *longueur) {
    static const struct { const char *u; char b; } T[] = {
        { "à", 'a' }, { "â", 'a' }, { "ä", 'a' }, { "á", 'a' }, { "À", 'a' }, { "Â", 'a' }, { "Ä", 'a' },
        { "ç", 'c' }, { "Ç", 'c' }, { "é", 'e' }, { "è", 'e' }, { "ê", 'e' }, { "ë", 'e' }, { "É", 'e' },
        { "È", 'e' }, { "Ê", 'e' }, { "Ë", 'e' }, { "î", 'i' }, { "ï", 'i' }, { "í", 'i' }, { "Î", 'i' },
        { "Ï", 'i' }, { "ô", 'o' }, { "ö", 'o' }, { "ó", 'o' }, { "Ô", 'o' }, { "Ö", 'o' }, { "ù", 'u' },
        { "û", 'u' }, { "ü", 'u' }, { "ú", 'u' }, { "Ù", 'u' }, { "Û", 'u' }, { "Ü", 'u' }, { "ÿ", 'y' },
        { "ñ", 'n' }, { "Ñ", 'n' }
    };
    for (size_t k = 0; k < sizeof T / sizeof *T; k++) {
        size_t l = strlen(T[k].u);
        if (memcmp(p, T[k].u, l) == 0) { *longueur = (int)l; return T[k].b; }
    }
    *longueur = 1;
    if (*p >= 'A' && *p <= 'Z') return *p - 'A' + 'a';
    return *p;
}

/* GRYM_TEXTE : ordre du dictionnaire, sans tenir compte des accents ni de la casse, puis octets. */
static int collation_texte(void *ctx, int na, const void *a, int nb, const void *b) {
    (void)ctx;
    const unsigned char *x = a, *y = b;
    int i = 0, j = 0;
    while (i < na && j < nb) {
        int lx, ly;
        int cx = base_lettre(x + i, &lx), cy = base_lettre(y + j, &ly);
        if (cx != cy) return cx < cy ? -1 : 1;
        i += lx;
        j += ly;
    }
    if (i < na || j < nb) return i < na ? 1 : -1;
    int c = memcmp(a, b, (size_t)(na < nb ? na : nb));
    return c ? c : (na > nb) - (na < nb);
}

int base_collations(sqlite3 *db) {
    return sqlite3_create_collation(db, "GRYM_NOMBRE", SQLITE_UTF8, NULL, collation_nombre) == SQLITE_OK
        && sqlite3_create_collation(db, "GRYM_TEXTE", SQLITE_UTF8, NULL, collation_texte) == SQLITE_OK;
}

/* ---------------------------------------------------------------- */
/* Charger un objet retrouvé                                        */
/* ---------------------------------------------------------------- */

static char *classe_en_base(Base *b, long id) {
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "SELECT classe FROM grym_objet WHERE id = ?", -1, &st, NULL);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    char *r = sqlite3_step(st) == SQLITE_ROW ? grym_dupliquer((const char *)sqlite3_column_text(st, 0)) : NULL;
    sqlite3_finalize(st);
    return r;
}

int base_charger(Base *b, struct Machine *m, Objet *o, char **erreur) {
    const ClasseVM *c = o->classe;
    const ClasseVM *l[LIGNEE_MAX];
    size_t n = lignee(c, l, LIGNEE_MAX);
    for (size_t e = 0; e < n; e++) {
        Chaine sql = {0};
        int colonnes = 0;
        chaine_ajouter(&sql, "SELECT id");
        for (size_t k = 0; k < c->nb_champs; k++) {
            if (c->proprietaires[k] != l[e]) continue;
            chaine_ajouter(&sql, ", ");
            ajouter_nom(&sql, "c ", c->champs[k]);
            if (est_fichier(c->types[k])) {
                chaine_ajouter(&sql, ", ");
                ajouter_nom(&sql, "n ", c->champs[k]);
            }
            colonnes++;
        }
        chaine_ajouter(&sql, " FROM ");
        ajouter_nom(&sql, "e ", l[e]->nom);
        chaine_ajouter(&sql, " WHERE id = ?");
        char *texte = chaine_rendre(&sql);
        sqlite3_stmt *st = NULL;
        sqlite3_prepare_v2(b->db, texte, -1, &st, NULL);
        free(texte);
        sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
        if (sqlite3_step(st) != SQLITE_ROW) {
            sqlite3_finalize(st);
            char *qui = un(c);
            *erreur = grym_formater("%s a disparu de la base « %s ».", qui, b->chemin);
            free(qui);
            return 0;
        }
        int col = 1;
        for (size_t k = 0; k < c->nb_champs; k++) {
            if (c->proprietaires[k] != l[e]) continue;
            const char *t = c->types[k];
            Valeur v;
            if (strcmp(t, "texte") == 0) v = vi_texte((const char *)sqlite3_column_text(st, col));
            else if (strcmp(t, "nombre") == 0) v = vi_nombre_canonique((const char *)sqlite3_column_text(st, col));
            else if (strcmp(t, "nombre entier") == 0) {
                char tampon[32];
                snprintf(tampon, sizeof tampon, "%lld", (long long)sqlite3_column_int64(st, col));
                v = vi_nombre_canonique(tampon);
            } else if (strcmp(t, "vrai ou faux") == 0) v = vi_booleen(sqlite3_column_int(st, col));
            else if (strcmp(t, "date") == 0) {
                long jours = 0;
                date_lire_iso((const char *)sqlite3_column_text(st, col), &jours);
                v = vi_date(jours);
            } else if (est_fichier(t)) {
                v = vi_fichier(sqlite3_column_blob(st, col), (size_t)sqlite3_column_bytes(st, col),
                               (const char *)sqlite3_column_text(st, col + 1));
                col++;
            } else {
                long id = (long)sqlite3_column_int64(st, col);
                char *classe = classe_en_base(b, id);
                Objet *lie = classe ? machine_objet_en_base(m, id, classe, erreur) : NULL;
                if (!classe && !*erreur) *erreur = grym_formater("Base « %s » : lien vers un objet absent.", b->chemin);
                free(classe);
                if (!lie) { sqlite3_finalize(st); return 0; }
                v = vi_objet(lie);
            }
            o->champs[k] = v;
            o->definis[k] = 1;
            col++;
        }
        (void)colonnes;
        sqlite3_finalize(st);
    }
    return 1;
}

/* ---------------------------------------------------------------- */
/* Chercher                                                         */
/* ---------------------------------------------------------------- */

typedef struct {
    Base *b;
    const ClasseVM *e;
    const ClasseVM *l[LIGNEE_MAX];
    size_t n;                 /* taille de la lignée ; l[n − 1] est l'entité cherchée */
    const char *p;            /* position dans la condition */
    Chaine sql;
    const Valeur *params;
    size_t nb_params;
    /* paramètres liés dans l'ordre de la condition : index, type du champ, nom du champ */
    size_t liens[64];
    const char *types[64];
    const char *champs[64];
    size_t nb_liens;
    char *erreur;
} Recherche;

/* Alias SQL de la table qui porte le champ k : « t0 » pour la racine de la lignée. */
static int alias_du_champ(const Recherche *r, size_t k) {
    for (size_t i = 0; i < r->n; i++) if (r->e->proprietaires[k] == r->l[i]) return (int)i;
    return 0;
}

static void colonne(Recherche *r, size_t k) {
    char t[16];
    snprintf(t, sizeof t, "t%d.", alias_du_champ(r, k));
    chaine_ajouter(&r->sql, t);
    ajouter_nom(&r->sql, "c ", r->e->champs[k]);
}

static int lire_condition(Recherche *r) {
    if (*r->p != '(') { r->erreur = grym_dupliquer("condition illisible"); return 0; }
    char op = r->p[1];
    r->p += 2;
    if (op == 'e' || op == 'o') {
        chaine_ajouter(&r->sql, "(");
        if (!lire_condition(r)) return 0;
        chaine_ajouter(&r->sql, op == 'e' ? " AND " : " OR ");
        if (!lire_condition(r)) return 0;
        chaine_ajouter(&r->sql, ")");
    } else if (op == 'n') {
        chaine_ajouter(&r->sql, "NOT ");
        if (!lire_condition(r)) return 0;
    } else {
        if (*r->p != '[') { r->erreur = grym_dupliquer("champ attendu"); return 0; }
        const char *fin = strchr(r->p, ']');
        if (!fin) { r->erreur = grym_dupliquer("champ mal fermé"); return 0; }
        char *champ = grym_formater("%.*s", (int)(fin - r->p - 1), r->p + 1);
        r->p = fin + 1;
        long k = -1;
        for (size_t q = 0; q < r->e->nb_champs && k < 0; q++) if (strcmp(r->e->champs[q], champ) == 0) k = (long)q;
        free(champ);
        if (k < 0 || !r->e->types[k]) { r->erreur = grym_dupliquer("champ inconnu"); return 0; }
        const char *t = r->e->types[k];
        int nombre = strcmp(t, "nombre") == 0, entier = strcmp(t, "nombre entier") == 0;
        const char *sqlop = op == '=' ? " = " : op == '!' ? " <> " : op == '<' ? " < " : op == '>' ? " > "
                          : op == 'l' ? " <= " : op == 'g' ? " >= " : NULL;
        chaine_ajouter(&r->sql, "(");
        if (op == 'P' || op == 'N' || op == '0') {
            const char *o2 = op == 'P' ? " > " : op == 'N' ? " < " : " = ";
            if (entier) { colonne(r, (size_t)k); chaine_ajouter(&r->sql, o2); chaine_ajouter(&r->sql, "0"); }
            else { colonne(r, (size_t)k); chaine_ajouter(&r->sql, o2); chaine_ajouter(&r->sql, "'0' COLLATE GRYM_NOMBRE"); }
        } else if (op == 'V' || op == 'F') {
            colonne(r, (size_t)k);
            chaine_ajouter(&r->sql, op == 'V' ? " = 1" : " = 0");
        } else {
            if (*r->p != '?' || !sqlop) { r->erreur = grym_dupliquer("paramètre attendu"); return 0; }
            size_t i = (size_t)strtoul(r->p + 1, (char **)&fin, 10);
            r->p = fin;
            if (i < 1 || i > r->nb_params || r->nb_liens == 64) { r->erreur = grym_dupliquer("paramètre invalide"); return 0; }
            if (entier) { chaine_ajouter(&r->sql, "CAST("); colonne(r, (size_t)k); chaine_ajouter(&r->sql, " AS TEXT)"); }
            else colonne(r, (size_t)k);
            chaine_ajouter(&r->sql, sqlop);
            char tampon[24];
            snprintf(tampon, sizeof tampon, "?%lu", (unsigned long)(r->nb_liens + 1));
            chaine_ajouter(&r->sql, tampon);
            if (nombre || entier) chaine_ajouter(&r->sql, " COLLATE GRYM_NOMBRE");
            else if (strcmp(t, "texte") == 0 && op != '=' && op != '!') chaine_ajouter(&r->sql, " COLLATE GRYM_TEXTE");
            r->liens[r->nb_liens] = i - 1;
            r->types[r->nb_liens] = t;
            r->champs[r->nb_liens] = r->e->champs[k];
            r->nb_liens++;
        }
        chaine_ajouter(&r->sql, ")");
    }
    if (*r->p != ')') { r->erreur = grym_dupliquer("parenthèse attendue"); return 0; }
    r->p++;
    return 1;
}

/* Lie la valeur d'un paramètre, comparée au champ d'un type donné. */
static int lier_parametre(Recherche *r, sqlite3_stmt *st, int i, const char *type, const char *champ, const Valeur *v) {
    const char *attendu = NULL;
    if (strcmp(type, "texte") == 0) {
        if (v->type == V_TEXTE) { sqlite3_bind_text(st, i, v->texte, -1, SQLITE_TRANSIENT); return 1; }
        attendu = "un texte";
    } else if (strcmp(type, "nombre") == 0 || strcmp(type, "nombre entier") == 0) {
        if (v->type == V_NOMBRE) {
            char *t = dec_canonique(&v->nombre);
            sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
            free(t);
            return 1;
        }
        attendu = "un nombre";
    } else if (strcmp(type, "vrai ou faux") == 0) {
        if (v->type == V_BOOLEEN) { sqlite3_bind_int(st, i, v->vrai); return 1; }
        attendu = "vrai ou faux";
    } else if (strcmp(type, "date") == 0) {
        if (v->type == V_DATE) {
            char *t = date_iso(v->jours);
            sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
            free(t);
            return 1;
        }
        attendu = "une date";
    } else {
        if (v->type == V_OBJET) { sqlite3_bind_int64(st, i, (sqlite3_int64)v->objet->id); return 1; }
        attendu = "un objet";
    }
    r->erreur = grym_formater("Le champ « %s » se compare à %s.", champ, attendu);
    return 0;
}

int base_chercher(Base *b, struct Machine *m, const char *d, const Valeur *params, size_t nb_params,
                  Valeur *resultat, char **erreur) {
    /* entité ␟ mode ␟ tri ␟ décroissant ␟ condition */
    const char *s1 = strchr(d, '\x1f');
    const char *s2 = s1 ? strchr(s1 + 1, '\x1f') : NULL;
    const char *s3 = s2 ? strchr(s2 + 1, '\x1f') : NULL;
    const char *s4 = s3 ? strchr(s3 + 1, '\x1f') : NULL;
    if (!s4) { *erreur = grym_dupliquer("Recherche mal décrite."); return 0; }
    char *entite = grym_formater("%.*s", (int)(s1 - d), d);
    int mode = s1[1] - '0';
    char *tri = grym_formater("%.*s", (int)(s3 - s2 - 1), s2 + 1);
    int decroissant = s3[1] == '1';
    Recherche r;
    memset(&r, 0, sizeof r);
    r.b = b;
    r.e = machine_classe(m, entite);
    r.params = params;
    r.nb_params = nb_params;
    if (!r.e || !r.e->conserve) {
        *erreur = grym_formater("« %s » n'est pas une entité connue.", entite);
        free(entite);
        free(tri);
        return 0;
    }
    free(entite);
    r.n = lignee(r.e, r.l, LIGNEE_MAX);
    const ClasseVM *e = r.e;

    /* SELECT … FROM la table de l'entité, jointe à celles de sa lignée */
    chaine_ajouter(&r.sql, mode == 0 ? "SELECT g.id, g.classe" : "SELECT count(*)");
    chaine_ajouter(&r.sql, " FROM ");
    for (size_t i = r.n; i > 0; i--) {
        char t[64];
        if (i < r.n) chaine_ajouter(&r.sql, " JOIN ");
        ajouter_nom(&r.sql, "e ", r.l[i - 1]->nom);
        snprintf(t, sizeof t, " AS t%lu", (unsigned long)(i - 1));
        chaine_ajouter(&r.sql, t);
        if (i < r.n) {
            snprintf(t, sizeof t, " ON t%lu.id = t%lu.id", (unsigned long)(i - 1), (unsigned long)(r.n - 1));
            chaine_ajouter(&r.sql, t);
        }
    }
    char t[64];
    snprintf(t, sizeof t, " JOIN grym_objet AS g ON g.id = t%lu.id", (unsigned long)(r.n - 1));
    chaine_ajouter(&r.sql, t);
    int ok = 1;
    if (s4[1]) {
        chaine_ajouter(&r.sql, " WHERE ");
        r.p = s4 + 1;
        ok = lire_condition(&r);
    }
    if (ok && mode == 0) {
        chaine_ajouter(&r.sql, " ORDER BY ");
        if (*tri) {
            long k = -1;
            for (size_t q = 0; q < e->nb_champs && k < 0; q++) if (strcmp(e->champs[q], tri) == 0) k = (long)q;
            if (k < 0 || !e->types[k]) { r.erreur = grym_dupliquer("champ du tri inconnu"); ok = 0; }
            else {
                colonne(&r, (size_t)k);
                if (strcmp(e->types[k], "nombre") == 0) chaine_ajouter(&r.sql, " COLLATE GRYM_NOMBRE");
                else if (strcmp(e->types[k], "texte") == 0) chaine_ajouter(&r.sql, " COLLATE GRYM_TEXTE");
                if (decroissant) chaine_ajouter(&r.sql, " DESC");
                chaine_ajouter(&r.sql, ", ");
            }
        }
        chaine_ajouter(&r.sql, "g.id");
    }
    free(tri);
    char *texte = chaine_rendre(&r.sql);
    sqlite3_stmt *st = NULL;
    if (ok && sqlite3_prepare_v2(b->db, texte, -1, &st, NULL) != SQLITE_OK) {
        r.erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        ok = 0;
    }
    free(texte);
    for (size_t i = 0; ok && i < r.nb_liens; i++)
        ok = lier_parametre(&r, st, (int)i + 1, r.types[i], r.champs[i], &params[r.liens[i]]);
    if (!ok) {
        sqlite3_finalize(st);
        *erreur = r.erreur ? r.erreur : grym_dupliquer("Recherche impossible.");
        return 0;
    }
    if (mode == 0) {
        size_t cap = 16, n = 0;
        long *ids = grym_allouer(cap * sizeof *ids);
        char **classes = grym_allouer(cap * sizeof *classes);
        int rc;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            if (n == cap) {
                cap *= 2;
                long *i2 = grym_allouer(cap * sizeof *i2);
                char **c2 = grym_allouer(cap * sizeof *c2);
                memcpy(i2, ids, n * sizeof *ids);
                memcpy(c2, classes, n * sizeof *classes);
                free(ids);
                free(classes);
                ids = i2;
                classes = c2;
            }
            ids[n] = (long)sqlite3_column_int64(st, 0);
            classes[n] = grym_dupliquer((const char *)sqlite3_column_text(st, 1));
            n++;
        }
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) {
            for (size_t k = 0; k < n; k++) free(classes[k]);
            free(classes);
            free(ids);
            *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
            return 0;
        }
        *resultat = vi_liste(ids, classes, n);
        return 1;
    }
    long compte = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    if (mode == 2) {
        char tampon[32];
        snprintf(tampon, sizeof tampon, "%ld", compte);
        *resultat = vi_nombre_canonique(tampon);
        return 1;
    }
    /* mode 1 : exactement un objet */
    if (compte != 1) {
        char *qui = pluriel(e);
        *erreur = compte == 0
            ? grym_formater("Aucun%s %s conservé%s ne répond à cette condition.", e->feminin ? "e" : "", e->nom,
                            e->feminin ? "e" : "")
            : grym_formater("%ld %s conservé%ss répondent à cette condition : « %s %s conservé%s dont … » en "
                            "attend un seul.", compte, qui, e->feminin ? "e" : "", e->feminin ? "la" : "le", e->nom,
                            e->feminin ? "e" : "");
        free(qui);
        return 0;
    }
    /* même recherche, qui rend l'objet */
    char *d0 = grym_formater("%.*s0%s", (int)(s1 - d + 1), d, s1 + 2);
    Valeur liste;
    ok = base_chercher(b, m, d0, params, nb_params, &liste, erreur);
    free(d0);
    if (!ok) return 0;
    Objet *o = machine_objet_en_base(m, liste.liste->ids[0], liste.liste->classes[0], erreur);
    vi_liberer(&liste);
    if (!o) return 0;
    *resultat = vi_objet(o);
    return 1;
}
