/* Test de l'intégration de SQLite (vendor/sqlite) : version, réglages de compilation,
 * transactions, clés étrangères, texte UTF-8 et nombres décimaux gardés exacts. */
#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0, echecs = 0;

static void verifier(int ligne, int ok, const char *quoi) {
    total++;
    if (!ok) { echecs++; printf("ÉCHEC (test ligne %d) : %s\n", ligne, quoi); }
}
#define V(ok, quoi) verifier(__LINE__, (ok), (quoi))

static int executer(sqlite3 *b, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(b, sql, NULL, NULL, &err);
    sqlite3_free(err);
    return rc;
}

/* Première colonne de la première ligne, en texte (copie à libérer), ou NULL. */
static char *lire(sqlite3 *b, const char *sql) {
    sqlite3_stmt *st = NULL;
    char *r = NULL;
    if (sqlite3_prepare_v2(b, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *t = sqlite3_column_text(st, 0);
        if (t) {
            r = malloc(strlen((const char *)t) + 1);
            strcpy(r, (const char *)t);
        }
    }
    sqlite3_finalize(st);
    return r;
}

static int egal(char *obtenu, const char *attendu) {
    int ok = obtenu && strcmp(obtenu, attendu) == 0;
    if (!ok) printf("  attendu « %s », obtenu « %s »\n", attendu, obtenu ? obtenu : "(rien)");
    free(obtenu);
    return ok;
}

int main(void) {
    V(strcmp(sqlite3_libversion(), "3.53.4") == 0, "version 3.53.4");
    V(strstr(sqlite3_sourceid(), "bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc") != NULL,
      "identifiant de la version officielle");
    V(sqlite3_threadsafe() == 0, "compilé sans fils d'exécution (SQLITE_THREADSAFE=0)");
    V(sqlite3_compileoption_used("OMIT_LOAD_EXTENSION"), "chargement d'extensions retiré");

    sqlite3 *b = NULL;
    V(sqlite3_open(":memory:", &b) == SQLITE_OK, "ouverture d'une base en mémoire");
    V(egal(lire(b, "PRAGMA foreign_keys"), "1"), "clés étrangères actives par défaut");

    V(executer(b, "CREATE TABLE client (id INTEGER PRIMARY KEY, nom TEXT NOT NULL, solde TEXT NOT NULL);"
                  "CREATE TABLE facture (id INTEGER PRIMARY KEY, client INTEGER NOT NULL REFERENCES client(id));")
      == SQLITE_OK, "création des tables");
    /* Un décimal se range en texte : aucune perte, contrairement à un REAL binaire. */
    V(executer(b, "INSERT INTO client VALUES (1, 'Thérèse Müller', '0.1000000000000000000000000001');") == SQLITE_OK,
      "insertion");
    V(egal(lire(b, "SELECT solde FROM client WHERE id = 1"), "0.1000000000000000000000000001"), "décimal exact");
    V(egal(lire(b, "SELECT nom FROM client WHERE id = 1"), "Thérèse Müller"), "texte UTF-8");
    V(egal(lire(b, "SELECT length(nom) FROM client WHERE id = 1"), "14"), "longueur en caractères");

    /* Clé étrangère refusée. */
    V(executer(b, "INSERT INTO facture VALUES (1, 99);") == SQLITE_CONSTRAINT, "lien vers un client inexistant refusé");

    /* Transaction annulée : rien ne reste. */
    V(executer(b, "BEGIN; INSERT INTO client VALUES (2, 'Bo', '5'); ROLLBACK;") == SQLITE_OK, "transaction annulée");
    V(egal(lire(b, "SELECT count(*) FROM client"), "1"), "l'annulation efface l'insertion");

    /* Guillemets doubles pour les chaînes refusés (SQLITE_DQS=0). */
    V(executer(b, "SELECT \"pas une colonne\" FROM client;") != SQLITE_OK, "guillemets doubles réservés aux noms");

    V(sqlite3_close(b) == SQLITE_OK, "fermeture");
    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
