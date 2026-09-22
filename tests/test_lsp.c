/* Tests du lecteur JSON et du serveur d'aide à la saisie (docs/lsp.md). */
#include "json.h"
#include "lsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0, echecs = 0;

static void verifier(int ligne, int ok, const char *quoi, const char *obtenu) {
    total++;
    if (!ok) { echecs++; printf("ÉCHEC (test ligne %d) : %s\n  obtenu : %s\n", ligne, quoi, obtenu ? obtenu : "(rien)"); }
}
#define V(ok, quoi, obtenu) verifier(__LINE__, (ok), (quoi), (obtenu))

/* Relit un JSON et le réécrit : « aller-retour ». */
static char *reecrire(const char *t) {
    char *e = NULL;
    Json *j = json_lire(t, strlen(t), &e);
    if (!j) return e;
    Chaine c = {0};
    json_ecrire(&c, j);
    json_liberer(j);
    return chaine_rendre(&c);
}

static void json_ok(int ligne, const char *entree, const char *attendu) {
    char *r = reecrire(entree);
    verifier(ligne, strcmp(r, attendu) == 0, entree, r);
    free(r);
}

static void json_refuse(int ligne, const char *entree) {
    char *e = NULL;
    Json *j = json_lire(entree, strlen(entree), &e);
    verifier(ligne, j == NULL && e != NULL, entree, "(accepté)");
    json_liberer(j);
    free(e);
}

/* Session LSP : messages en entrée, sortie complète du serveur. */
static char *session(const char **messages, size_t n, int *code) {
    FILE *entree = tmpfile(), *sortie = tmpfile();
    for (size_t k = 0; k < n; k++) fprintf(entree, "Content-Length: %lu\r\n\r\n%s", (unsigned long)strlen(messages[k]), messages[k]);
    rewind(entree);
    *code = lsp_servir(entree, sortie);
    long taille = ftell(sortie);
    rewind(sortie);
    char *r = malloc((size_t)taille + 1);
    size_t lu = fread(r, 1, (size_t)taille, sortie);
    r[lu] = '\0';
    fclose(entree);
    fclose(sortie);
    return r;
}

int main(void) {
    /* --- JSON (RFC 8259) --- */
    json_ok(__LINE__, " { \"a\" : [1, -2.5e3, true, false, null], \"b\" : \"x\" } ", "{\"a\":[1,-2.5e3,true,false,null],\"b\":\"x\"}");
    json_ok(__LINE__, "\"\\u00e9\\n\\t\\\"\\\\\\/\"", "\"é\\n\\t\\\"\\\\/\"");
    json_ok(__LINE__, "\"\\ud83d\\ude42\"", "\"🙂\"");
    json_ok(__LINE__, "\"\\ud83d\"", "\"\xef\xbf\xbd\"");
    json_ok(__LINE__, "\"\\u0000\"", "\"\xef\xbf\xbd\"");
    json_ok(__LINE__, "\"é « » 🙂\"", "\"é « » 🙂\"");
    json_ok(__LINE__, "[]", "[]");
    json_ok(__LINE__, "{}", "{}");
    json_refuse(__LINE__, "");
    json_refuse(__LINE__, "{");
    json_refuse(__LINE__, "[1,]");
    json_refuse(__LINE__, "{\"a\" 1}");
    json_refuse(__LINE__, "\"non fermé");
    json_refuse(__LINE__, "\"a\\qb\"");
    json_refuse(__LINE__, "\"a\x01\"");
    json_refuse(__LINE__, "tru");
    json_refuse(__LINE__, "1 2");
    json_refuse(__LINE__, "-");
    {
        char profond[2100];
        memset(profond, '[', 1000);
        memset(profond + 1000, ']', 1000);
        profond[2000] = '\0';
        json_refuse(__LINE__, profond);   /* imbrication limitée : pas de débordement de pile */
    }
    {
        char *e = NULL;
        const char *t = "{\"a\":{\"b\":{\"c\":42}}}";
        Json *j = json_lire(t, strlen(t), &e);
        V(j && json_entier(json_chemin(j, "a.b.c"), 0) == 42, "chemin a.b.c", e);
        V(json_chemin(j, "a.x.c") == NULL, "chemin absent", NULL);
        json_liberer(j);
    }

    /* --- Session LSP --- */
    {
        const char *m[] = {
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}",
            "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}",
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file:///a.grym\","
            "\"languageId\":\"grymoir\",\"version\":1,\"text\":\"Le total vaut 1.\\nAfficher « 🙂 » puis totale.\\n\"}}}",
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":\"file:///a.grym\","
            "\"version\":2},\"contentChanges\":[{\"text\":\"Le  total vaut 1.\\nAfficher le t\"}]}}",
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"file:///a.grym\"},"
            "\"position\":{\"line\":1,\"character\":13}}}",
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":\"file:///a.grym\","
            "\"version\":3},\"contentChanges\":[{\"text\":\"Le  total vaut 1.\\nafficher  total.\"}]}}",
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/formatting\",\"params\":{\"textDocument\":{\"uri\":\"file:///a.grym\"},"
            "\"options\":{\"tabSize\":4,\"insertSpaces\":true}}}",
            "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/hover\",\"params\":{}}",
            "{pas du json",
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":\"file:///a.grym\"}}}",
            "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"shutdown\"}",
            "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}",
        };
        int code;
        char *r = session(m, sizeof m / sizeof *m, &code);
        V(code == 0, "« exit » après « shutdown » : code 0", r);
        V(strstr(r, "\"documentFormattingProvider\":true") != NULL, "capacités annoncées", r);
        V(strstr(r, "{\"range\":{\"start\":{\"line\":1,\"character\":21},\"end\":{\"line\":1,\"character\":27}},"
                    "\"severity\":1,\"source\":\"grym\",\"message\":\"« totale » inconnu, vouliez-vous « total » ?\"}") != NULL,
          "diagnostic, colonne en unités UTF-16 (l'émoji en compte deux)", r);
        V(strstr(r, "\"id\":2,\"result\":[{\"label\":\"total\"}]") != NULL, "suites : « total »", r);
        V(strstr(r, "\"id\":3,\"result\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":2,\"character\":0}},"
                    "\"newText\":\"Le total vaut 1.\\nAfficher total.\\n\"}]") != NULL, "formatage", r);
        V(strstr(r, "\"id\":4,\"error\":{\"code\":-32601") != NULL, "méthode inconnue", r);
        V(strstr(r, "\"id\":null,\"error\":{\"code\":-32700") != NULL, "JSON mal formé", r);
        V(strstr(r, "\"uri\":\"file:///a.grym\",\"diagnostics\":[]}}") != NULL, "fermeture : diagnostics effacés", r);
        V(strstr(r, "\"id\":5,\"result\":null") != NULL, "shutdown", r);
        free(r);
    }
    {
        const char *m[] = {
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file:///b.grymc\","
            "\"languageId\":\"grymoir-compact\",\"version\":1,\"text\":\"_le x << 1\\n_afficher  y\\n\"}}}",
            "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}",
        };
        int code;
        char *r = session(m, 2, &code);
        V(code == 1, "« exit » sans « shutdown » : code 1", r);
        V(strstr(r, "\"line\":1,\"character\":11") != NULL && strstr(r, "« y » inconnu.") != NULL, "diagnostic en forme compacte", r);
        free(r);
    }

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
