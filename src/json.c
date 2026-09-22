/* GrymoiR : lecture et écriture de JSON (RFC 8259), sans dépendance.
 * Spécification : docs/lsp.md.
 */
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROFONDEUR_MAX 256

typedef struct {
    const char *p, *fin;
    int profondeur;
    char *erreur;
} Lecture;

static Json *nouveau(TypeJson t) {
    Json *j = grym_allouer(sizeof *j);
    memset(j, 0, sizeof *j);
    j->type = t;
    return j;
}

static void *echec(Lecture *l, const char *m) {
    if (!l->erreur) l->erreur = grym_formater("JSON mal formé : %s.", m);
    return NULL;
}

static void blancs(Lecture *l) {
    while (l->p < l->fin && (*l->p == ' ' || *l->p == '\t' || *l->p == '\n' || *l->p == '\r')) l->p++;
}

static int hex4(Lecture *l, unsigned *v) {
    if (l->fin - l->p < 4) return 0;
    *v = 0;
    for (int k = 0; k < 4; k++) {
        char c = l->p[k];
        *v <<= 4;
        if (c >= '0' && c <= '9') *v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') *v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') *v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    l->p += 4;
    return 1;
}

static void utf8(Chaine *c, unsigned cp) {
    char t[5] = { 0 };
    if (cp < 0x80) t[0] = (char)cp;
    else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[2] = (char)(0x80 | (cp & 0x3F));
    } else {
        t[0] = (char)(0xF0 | (cp >> 18)); t[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[3] = (char)(0x80 | (cp & 0x3F));
    }
    chaine_ajouter(c, t);
}

static char *lire_texte(Lecture *l) {
    l->p++;   /* « " » */
    Chaine c = {0};
    chaine_ajouter(&c, "");
    while (l->p < l->fin && *l->p != '"') {
        unsigned char x = (unsigned char)*l->p;
        if (x < 0x20) { free(chaine_rendre(&c)); return echec(l, "caractère de contrôle dans un texte"); }
        if (x != '\\') {
            const char *d = l->p;
            while (l->p < l->fin && *l->p != '"' && *l->p != '\\' && (unsigned char)*l->p >= 0x20) l->p++;
            char *morceau = grym_formater("%.*s", (int)(l->p - d), d);
            chaine_ajouter(&c, morceau);
            free(morceau);
            continue;
        }
        if (++l->p >= l->fin) break;
        char e = *l->p++;
        switch (e) {
        case '"': chaine_ajouter(&c, "\""); break;
        case '\\': chaine_ajouter(&c, "\\"); break;
        case '/': chaine_ajouter(&c, "/"); break;
        case 'b': chaine_ajouter(&c, "\b"); break;
        case 'f': chaine_ajouter(&c, "\f"); break;
        case 'n': chaine_ajouter(&c, "\n"); break;
        case 'r': chaine_ajouter(&c, "\r"); break;
        case 't': chaine_ajouter(&c, "\t"); break;
        case 'u': {
            unsigned cp;
            if (!hex4(l, &cp)) { free(chaine_rendre(&c)); return echec(l, "échappement \\u invalide"); }
            if (cp >= 0xD800 && cp <= 0xDBFF) {   /* paire de substitution UTF-16 */
                unsigned bas;
                if (l->fin - l->p >= 6 && l->p[0] == '\\' && l->p[1] == 'u' && (l->p += 2, hex4(l, &bas))
                    && bas >= 0xDC00 && bas <= 0xDFFF)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (bas - 0xDC00);
                else cp = 0xFFFD;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;
            if (cp == 0) cp = 0xFFFD;   /* pas d'octet nul dans nos textes C */
            utf8(&c, cp);
            break;
        }
        default: free(chaine_rendre(&c)); return echec(l, "échappement inconnu");
        }
    }
    if (l->p >= l->fin) { free(chaine_rendre(&c)); return echec(l, "texte non fermé"); }
    l->p++;
    return chaine_rendre(&c);
}

static Json *valeur(Lecture *l);

static Json *conteneur(Lecture *l, int objet) {
    if (++l->profondeur > PROFONDEUR_MAX) return echec(l, "imbrication trop profonde");
    Json *j = nouveau(objet ? JSON_OBJET : JSON_TABLEAU);
    size_t cap = 0;
    l->p++;
    blancs(l);
    if (l->p < l->fin && *l->p == (objet ? '}' : ']')) { l->p++; l->profondeur--; return j; }
    for (;;) {
        char *cle = NULL;
        if (objet) {
            blancs(l);
            if (l->p >= l->fin || *l->p != '"') { json_liberer(j); return echec(l, "clé attendue"); }
            cle = lire_texte(l);
            if (!cle) { json_liberer(j); return NULL; }
            blancs(l);
            if (l->p >= l->fin || *l->p != ':') { free(cle); json_liberer(j); return echec(l, "« : » attendu"); }
            l->p++;
        }
        Json *v = valeur(l);
        if (!v) { free(cle); json_liberer(j); return NULL; }
        if (j->nb == cap) {
            cap = cap ? cap * 2 : 8;
            Json **e = grym_allouer(cap * sizeof *e);
            char **k = grym_allouer(cap * sizeof *k);
            if (j->nb) { memcpy(e, j->elements, j->nb * sizeof *e); if (j->cles) memcpy(k, j->cles, j->nb * sizeof *k); }
            free(j->elements);
            free(j->cles);
            j->elements = e;
            j->cles = k;
        }
        j->cles[j->nb] = cle;
        j->elements[j->nb++] = v;
        blancs(l);
        if (l->p < l->fin && *l->p == ',') { l->p++; continue; }
        if (l->p < l->fin && *l->p == (objet ? '}' : ']')) { l->p++; break; }
        json_liberer(j);
        return echec(l, objet ? "« , » ou « } » attendu" : "« , » ou « ] » attendu");
    }
    l->profondeur--;
    return j;
}

static Json *valeur(Lecture *l) {
    blancs(l);
    if (l->p >= l->fin) return echec(l, "valeur attendue");
    char c = *l->p;
    if (c == '{') return conteneur(l, 1);
    if (c == '[') return conteneur(l, 0);
    if (c == '"') {
        char *t = lire_texte(l);
        if (!t) return NULL;
        Json *j = nouveau(JSON_TEXTE);
        j->texte = t;
        return j;
    }
    static const struct { const char *mot; TypeJson type; int vrai; } MOTS[] = {
        { "true", JSON_BOOLEEN, 1 }, { "false", JSON_BOOLEEN, 0 }, { "null", JSON_NUL, 0 }
    };
    for (size_t k = 0; k < 3; k++) {
        size_t n = strlen(MOTS[k].mot);
        if ((size_t)(l->fin - l->p) >= n && memcmp(l->p, MOTS[k].mot, n) == 0) {
            l->p += n;
            Json *j = nouveau(MOTS[k].type);
            j->vrai = MOTS[k].vrai;
            return j;
        }
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char *d = l->p;
        if (*l->p == '-') l->p++;
        int chiffres = 0;
        while (l->p < l->fin && ((*l->p >= '0' && *l->p <= '9') || *l->p == '.' || *l->p == 'e' || *l->p == 'E'
                                 || ((*l->p == '+' || *l->p == '-') && (l->p[-1] == 'e' || l->p[-1] == 'E')))) {
            chiffres += *l->p >= '0' && *l->p <= '9';
            l->p++;
        }
        if (!chiffres) return echec(l, "nombre invalide");
        Json *j = nouveau(JSON_NOMBRE);
        j->texte = grym_formater("%.*s", (int)(l->p - d), d);
        j->nombre = strtod(j->texte, NULL);
        return j;
    }
    return echec(l, "valeur inattendue");
}

Json *json_lire(const char *texte, size_t taille, char **erreur) {
    Lecture l = { texte, texte + taille, 0, NULL };
    Json *j = valeur(&l);
    if (j) {
        blancs(&l);
        if (l.p != l.fin) { json_liberer(j); j = echec(&l, "contenu après la valeur"); }
    }
    if (!j) *erreur = l.erreur ? l.erreur : grym_dupliquer("JSON mal formé.");
    return j;
}

void json_liberer(Json *j) {
    if (!j) return;
    for (size_t k = 0; k < j->nb; k++) {
        json_liberer(j->elements[k]);
        if (j->cles) free(j->cles[k]);
    }
    free(j->elements);
    free(j->cles);
    free(j->texte);
    free(j);
}

const Json *json_champ(const Json *o, const char *cle) {
    if (!o || o->type != JSON_OBJET) return NULL;
    for (size_t k = 0; k < o->nb; k++) if (strcmp(o->cles[k], cle) == 0) return o->elements[k];
    return NULL;
}

const Json *json_chemin(const Json *j, const char *chemin) {
    char *c = grym_dupliquer(chemin);
    const Json *r = j;
    for (char *p = strtok(c, "."); p && r; p = strtok(NULL, ".")) r = json_champ(r, p);
    free(c);
    return r;
}

const char *json_texte(const Json *j) {
    return j && j->type == JSON_TEXTE ? j->texte : NULL;
}

long json_entier(const Json *j, long defaut) {
    return j && j->type == JSON_NOMBRE ? (long)j->nombre : defaut;
}

void json_ecrire_texte(Chaine *c, const char *t) {
    chaine_ajouter(c, "\"");
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        char e[8];
        if (*p == '"') chaine_ajouter(c, "\\\"");
        else if (*p == '\\') chaine_ajouter(c, "\\\\");
        else if (*p == '\n') chaine_ajouter(c, "\\n");
        else if (*p == '\r') chaine_ajouter(c, "\\r");
        else if (*p == '\t') chaine_ajouter(c, "\\t");
        else if (*p < 0x20) { snprintf(e, sizeof e, "\\u%04x", *p); chaine_ajouter(c, e); }
        else { e[0] = (char)*p; e[1] = '\0'; chaine_ajouter(c, e); }
    }
    chaine_ajouter(c, "\"");
}

void json_ecrire(Chaine *c, const Json *j) {
    if (!j) { chaine_ajouter(c, "null"); return; }
    switch (j->type) {
    case JSON_NUL: chaine_ajouter(c, "null"); return;
    case JSON_BOOLEEN: chaine_ajouter(c, j->vrai ? "true" : "false"); return;
    case JSON_NOMBRE: chaine_ajouter(c, j->texte); return;
    case JSON_TEXTE: json_ecrire_texte(c, j->texte); return;
    case JSON_TABLEAU:
    case JSON_OBJET:
        chaine_ajouter(c, j->type == JSON_OBJET ? "{" : "[");
        for (size_t k = 0; k < j->nb; k++) {
            if (k) chaine_ajouter(c, ",");
            if (j->type == JSON_OBJET) { json_ecrire_texte(c, j->cles[k]); chaine_ajouter(c, ":"); }
            json_ecrire(c, j->elements[k]);
        }
        chaine_ajouter(c, j->type == JSON_OBJET ? "}" : "]");
        return;
    }
}
