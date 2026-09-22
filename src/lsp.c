/* GrymoiR : serveur d'aide à la saisie, protocole LSP 3.17
 * Spécification : docs/lsp.md.
 *
 * Messages : en-tête « Content-Length: n », ligne vide, puis n octets de JSON-RPC 2.0.
 * Le client envoie le document entier à chaque modification (synchronisation complète).
 * Positions LSP : ligne et caractère comptés à partir de 0, le caractère en unités UTF-16 ;
 * positions GrymoiR : ligne et colonne à partir de 1, la colonne en caractères.
 */
#include "lsp.h"
#include "analyseur.h"
#include "imprimeur.h"
#include "json.h"
#include "texte.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *uri;
    char *texte;
} Document;

typedef struct {
    FILE *sortie;
    Document *docs;
    size_t nb;
    int arret_demande;   /* « shutdown » reçu */
} Serveur;

/* ---------------------------------------------------------------- */
/* Messages                                                         */
/* ---------------------------------------------------------------- */

static void envoyer(Serveur *s, const char *json) {
    fprintf(s->sortie, "Content-Length: %lu\r\n\r\n%s", (unsigned long)strlen(json), json);
    fflush(s->sortie);
}

static void repondre(Serveur *s, const Json *id, const char *resultat) {
    Chaine c = {0};
    chaine_ajouter(&c, "{\"jsonrpc\":\"2.0\",\"id\":");
    json_ecrire(&c, id);
    chaine_ajouter(&c, ",\"result\":");
    chaine_ajouter(&c, resultat);
    chaine_ajouter(&c, "}");
    char *m = chaine_rendre(&c);
    envoyer(s, m);
    free(m);
}

static void repondre_erreur(Serveur *s, const Json *id, int code, const char *message) {
    Chaine c = {0};
    chaine_ajouter(&c, "{\"jsonrpc\":\"2.0\",\"id\":");
    json_ecrire(&c, id);
    char t[64];
    snprintf(t, sizeof t, ",\"error\":{\"code\":%d,\"message\":", code);
    chaine_ajouter(&c, t);
    json_ecrire_texte(&c, message);
    chaine_ajouter(&c, "}}");
    char *m = chaine_rendre(&c);
    envoyer(s, m);
    free(m);
}

/* Lit un message ; NULL à la fin de l'entrée. */
static char *lire_message(FILE *e, size_t *taille) {
    char ligne[1024];
    long longueur = -1;
    for (;;) {
        if (!fgets(ligne, sizeof ligne, e)) return NULL;
        if (strcmp(ligne, "\r\n") == 0 || strcmp(ligne, "\n") == 0) {
            if (longueur >= 0) break;
            continue;
        }
        if (strncmp(ligne, "Content-Length:", 15) == 0) longueur = strtol(ligne + 15, NULL, 10);
    }
    if (longueur < 0 || longueur > 64L * 1024 * 1024) return NULL;
    char *m = malloc((size_t)longueur + 1);
    if (!m) return NULL;
    if (fread(m, 1, (size_t)longueur, e) != (size_t)longueur) { free(m); return NULL; }
    m[longueur] = '\0';
    *taille = (size_t)longueur;
    return m;
}

/* ---------------------------------------------------------------- */
/* Documents et positions                                           */
/* ---------------------------------------------------------------- */

static Document *document(Serveur *s, const char *uri) {
    for (size_t k = 0; k < s->nb; k++) if (strcmp(s->docs[k].uri, uri) == 0) return &s->docs[k];
    return NULL;
}

static int est_compact(const char *uri) {
    size_t l = strlen(uri);
    return l > 6 && strcmp(uri + l - 6, ".grymc") == 0;
}

/* Longueur d'un caractère UTF-8 à partir de son premier octet. */
static size_t octets_du_caractere(unsigned char c) {
    return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
}

/* Position LSP (ligne, unités UTF-16) → décalage en octets dans le texte. */
static size_t decalage(const char *t, long ligne, long car) {
    size_t i = 0;
    for (long l = 0; l < ligne && t[i]; i++) if (t[i] == '\n') l++;
    long u = 0;
    while (t[i] && t[i] != '\n' && u < car) {
        size_t n = octets_du_caractere((unsigned char)t[i]);
        u += n == 4 ? 2 : 1;   /* hors du plan de base : deux unités UTF-16 */
        i += n;
    }
    return i;
}

/* Colonne GrymoiR (caractères, à partir de 1) sur une ligne (à partir de 1) → unités UTF-16. */
static long colonne_utf16(const char *t, int ligne, int colonne, long *fin_mot) {
    size_t i = 0;
    for (int l = 1; l < ligne && t[i]; i++) if (t[i] == '\n') l++;
    long u = 0;
    for (int c = 1; c < colonne && t[i] && t[i] != '\n'; c++) {
        size_t n = octets_du_caractere((unsigned char)t[i]);
        u += n == 4 ? 2 : 1;
        i += n;
    }
    /* souligner jusqu'à la fin du mot, ou au moins un caractère */
    long f = u;
    while (t[i] && t[i] != '\n' && t[i] != ' ' && t[i] != '.' && t[i] != ',' && t[i] != ':') {
        size_t n = octets_du_caractere((unsigned char)t[i]);
        f += n == 4 ? 2 : 1;
        i += n;
    }
    *fin_mot = f > u ? f : u + 1;
    return u;
}

/* ---------------------------------------------------------------- */
/* Diagnostics, suites, formatage                                   */
/* ---------------------------------------------------------------- */

/* Analyse le document ; *diag reçoit la première erreur. Renvoie 1 si le programme est correct. */
static int analyser_document(const Document *d, Programme *p, Diagnostic *diag) {
    Portee *portee = portee_creer();
    int ok = est_compact(d->uri) ? analyser_compact(d->texte, strlen(d->texte), portee, p, diag)
                                 : analyser(d->texte, strlen(d->texte), portee, 0, p, diag);
    portee_detruire(portee);
    return ok;
}

static void publier_diagnostics(Serveur *s, const Document *d) {
    Programme p;
    Diagnostic diag;
    Chaine c = {0};
    chaine_ajouter(&c, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
    json_ecrire_texte(&c, d->uri);
    chaine_ajouter(&c, ",\"diagnostics\":[");
    if (analyser_document(d, &p, &diag)) {
        programme_liberer(&p);
    } else {
        int ligne = diag.ligne > 0 ? diag.ligne : 1;
        long fin;
        long debut = colonne_utf16(d->texte, ligne, diag.colonne > 0 ? diag.colonne : 1, &fin);
        char t[256];
        snprintf(t, sizeof t, "{\"range\":{\"start\":{\"line\":%d,\"character\":%ld},\"end\":{\"line\":%d,\"character\":%ld}},"
                              "\"severity\":1,\"source\":\"grym\",\"message\":", ligne - 1, debut, ligne - 1, fin);
        chaine_ajouter(&c, t);
        json_ecrire_texte(&c, diag.message ? diag.message : "Erreur.");
        chaine_ajouter(&c, "}");
        diagnostic_liberer(&diag);
    }
    chaine_ajouter(&c, "]}}");
    char *m = chaine_rendre(&c);
    envoyer(s, m);
    free(m);
}

static void suites(Serveur *s, const Json *id, const Json *params) {
    const Document *d = document(s, json_texte(json_chemin(params, "textDocument.uri")) ? json_texte(json_chemin(params, "textDocument.uri")) : "");
    if (!d || est_compact(d->uri)) { repondre(s, id, "[]"); return; }   /* suites : forme littéraire (§ 8) */
    size_t pos = decalage(d->texte, json_entier(json_chemin(params, "position.line"), 0),
                          json_entier(json_chemin(params, "position.character"), 0));
    Suggestions g = suites_valides(d->texte, pos);
    Chaine c = {0};
    chaine_ajouter(&c, "[");
    int premier = 1;
    for (size_t k = 0; k < g.nb; k++) {
        const char *x = g.items[k];
        if (x[0] == '(' && x[1] != '\0') continue;           /* « (nombre) » : une catégorie, pas un mot */
        if (strstr(x, "…")) continue;                         /* « « … » » : un gabarit */
        if (!premier) chaine_ajouter(&c, ",");
        premier = 0;
        chaine_ajouter(&c, "{\"label\":");
        json_ecrire_texte(&c, x);
        chaine_ajouter(&c, "}");
    }
    chaine_ajouter(&c, "]");
    suggestions_liberer(&g);
    char *r = chaine_rendre(&c);
    repondre(s, id, r);
    free(r);
}

static void formater(Serveur *s, const Json *id, const Json *params) {
    const char *uri = json_texte(json_chemin(params, "textDocument.uri"));
    const Document *d = uri ? document(s, uri) : NULL;
    Programme p;
    Diagnostic diag;
    if (!d || !analyser_document(d, &p, &diag)) {   /* un programme incorrect n'est pas mis en forme */
        if (d) diagnostic_liberer(&diag);
        repondre(s, id, "[]");
        return;
    }
    char *t = est_compact(d->uri) ? imprimer_compact(&p) : imprimer_litteraire(&p);
    programme_liberer(&p);
    long lignes = 0;
    for (const char *q = d->texte; *q; q++) lignes += *q == '\n';
    Chaine c = {0};
    char e[192];
    snprintf(e, sizeof e, "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":%ld,\"character\":0}},"
                          "\"newText\":", lignes + 1);
    chaine_ajouter(&c, e);
    json_ecrire_texte(&c, t);
    chaine_ajouter(&c, "}]");
    free(t);
    char *r = chaine_rendre(&c);
    repondre(s, id, r);
    free(r);
}

/* ---------------------------------------------------------------- */
/* Boucle                                                           */
/* ---------------------------------------------------------------- */

static void ouvrir_ou_changer(Serveur *s, const char *uri, const char *texte) {
    Document *d = document(s, uri);
    if (!d) {
        Document *t = grym_allouer((s->nb + 1) * sizeof *t);
        if (s->nb) memcpy(t, s->docs, s->nb * sizeof *t);
        free(s->docs);
        s->docs = t;
        d = &s->docs[s->nb++];
        d->uri = grym_dupliquer(uri);
        d->texte = NULL;
    }
    free(d->texte);
    d->texte = grym_dupliquer(texte);
    publier_diagnostics(s, d);
}

static void fermer(Serveur *s, const char *uri) {
    for (size_t k = 0; k < s->nb; k++)
        if (strcmp(s->docs[k].uri, uri) == 0) {
            Chaine c = {0};
            chaine_ajouter(&c, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
            json_ecrire_texte(&c, uri);
            chaine_ajouter(&c, ",\"diagnostics\":[]}}");
            char *m = chaine_rendre(&c);
            envoyer(s, m);
            free(m);
            free(s->docs[k].uri);
            free(s->docs[k].texte);
            s->docs[k] = s->docs[--s->nb];
            return;
        }
}

int lsp_servir(FILE *entree, FILE *sortie) {
    Serveur s = { sortie, NULL, 0, 0 };
    int code = 1;
    for (;;) {
        size_t n;
        char *brut = lire_message(entree, &n);
        if (!brut) break;
        char *erreur = NULL;
        Json *m = json_lire(brut, n, &erreur);
        free(brut);
        if (!m) {
            repondre_erreur(&s, NULL, -32700, erreur);
            free(erreur);
            continue;
        }
        const char *methode = json_texte(json_champ(m, "method"));
        const Json *id = json_champ(m, "id");
        const Json *params = json_champ(m, "params");
        if (!methode) {
            /* réponse du client à une requête du serveur : le serveur n'en envoie pas */
        } else if (strcmp(methode, "initialize") == 0) {
            repondre(&s, id, "{\"capabilities\":{\"textDocumentSync\":1,"
                             "\"completionProvider\":{\"triggerCharacters\":[\" \",\"'\"]},"
                             "\"documentFormattingProvider\":true},"
                             "\"serverInfo\":{\"name\":\"grym\",\"version\":\"0.4\"}}");
        } else if (strcmp(methode, "shutdown") == 0) {
            s.arret_demande = 1;
            repondre(&s, id, "null");
        } else if (strcmp(methode, "exit") == 0) {
            code = s.arret_demande ? 0 : 1;
            json_liberer(m);
            break;
        } else if (strcmp(methode, "textDocument/didOpen") == 0) {
            const char *uri = json_texte(json_chemin(params, "textDocument.uri"));
            const char *texte = json_texte(json_chemin(params, "textDocument.text"));
            if (uri && texte) ouvrir_ou_changer(&s, uri, texte);
        } else if (strcmp(methode, "textDocument/didChange") == 0) {
            const char *uri = json_texte(json_chemin(params, "textDocument.uri"));
            const Json *ch = json_champ(params, "contentChanges");
            const char *texte = ch && ch->type == JSON_TABLEAU && ch->nb ? json_texte(json_champ(ch->elements[ch->nb - 1], "text")) : NULL;
            if (uri && texte) ouvrir_ou_changer(&s, uri, texte);
        } else if (strcmp(methode, "textDocument/didClose") == 0) {
            const char *uri = json_texte(json_chemin(params, "textDocument.uri"));
            if (uri) fermer(&s, uri);
        } else if (strcmp(methode, "textDocument/completion") == 0) {
            suites(&s, id, params);
        } else if (strcmp(methode, "textDocument/formatting") == 0) {
            formater(&s, id, params);
        } else if (id) {
            repondre_erreur(&s, id, -32601, "Méthode non prise en charge.");
        }
        json_liberer(m);
    }
    for (size_t k = 0; k < s.nb; k++) { free(s.docs[k].uri); free(s.docs[k].texte); }
    free(s.docs);
    return code;
}
