/* Tests du lexeur GrymoiR v0.1.
 * Les caractères invisibles (espaces insécables, accents combinants, BOM)
 * sont écrits en \x, et la chaîne est coupée après chaque échappement
 * pour qu'un chiffre suivant ne soit pas avalé par l'échappement.
 */
#include "lexeur.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int total = 0, echecs = 0;

/* Résumé d'une source : « MOT(le) NOMBRE(3.5) POINT ».
 * S'arrête à la première erreur ; « ENCODAGE » si l'UTF-8 est invalide. */
static const char *resumer(const char *src) {
    static char buf[4096];
    size_t k = 0;
    buf[0] = '\0';
    char *err = NULL;
    Lexeur *lx = lexeur_creer(src, strlen(src), &err);
    if (!lx) {
        free(err);
        snprintf(buf, sizeof buf, "ENCODAGE");
        return buf;
    }
    for (;;) {
        Jeton j = lexeur_suivant(lx);
        if (j.type == J_FIN) break;
        int w;
        if (j.valeur && j.type != J_ERREUR)
            w = snprintf(buf + k, sizeof buf - k, "%s%s(%s)", k ? " " : "",
                         type_jeton_nom(j.type), j.valeur);
        else
            w = snprintf(buf + k, sizeof buf - k, "%s%s", k ? " " : "",
                         type_jeton_nom(j.type));
        if (w > 0) k += (size_t)w;
        if (k >= sizeof buf) k = sizeof buf - 1;
        TypeJeton t = j.type;
        jeton_liberer(&j);
        if (t == J_ERREUR) break;
    }
    lexeur_detruire(lx);
    return buf;
}

static void verifier(int l, const char *src, const char *attendu) {
    total++;
    const char *obtenu = resumer(src);
    if (strcmp(obtenu, attendu) != 0) {
        echecs++;
        printf("ÉCHEC (test ligne %d)\n  attendu : %s\n  obtenu  : %s\n", l, attendu, obtenu);
    }
}

/* Vérifie que le message de la première erreur contient un fragment. */
static void verifier_message(int l, const char *src, const char *fragment) {
    total++;
    char *err = NULL;
    Lexeur *lx = lexeur_creer(src, strlen(src), &err);
    int ok = 0;
    char message[1024] = "(aucune erreur)";
    if (!lx) {
        snprintf(message, sizeof message, "%s", err);
        ok = strstr(err, fragment) != NULL;
        free(err);
    } else {
        for (;;) {
            Jeton j = lexeur_suivant(lx);
            if (j.type == J_FIN) break;
            if (j.type == J_ERREUR) {
                snprintf(message, sizeof message, "%s", j.valeur);
                ok = strstr(j.valeur, fragment) != NULL;
                jeton_liberer(&j);
                break;
            }
            jeton_liberer(&j);
        }
        lexeur_detruire(lx);
    }
    if (!ok) {
        echecs++;
        printf("ÉCHEC (test ligne %d)\n  fragment attendu : %s\n  message obtenu   : %s\n",
               l, fragment, message);
    }
}

/* Vérifie la ligne et la colonne du jeton numéro `index` (à partir de 0). */
static void verifier_position(int l, const char *src, int index, int ligne, int colonne) {
    total++;
    char *err = NULL;
    Lexeur *lx = lexeur_creer(src, strlen(src), &err);
    int ok = 0, jl = -1, jc = -1;
    if (lx) {
        for (int i = 0;; i++) {
            Jeton j = lexeur_suivant(lx);
            TypeJeton t = j.type;
            if (i == index) { jl = j.ligne; jc = j.colonne; ok = (jl == ligne && jc == colonne); }
            jeton_liberer(&j);
            if (i == index || t == J_FIN || t == J_ERREUR) break;
        }
        lexeur_detruire(lx);
    } else {
        free(err);
    }
    if (!ok) {
        echecs++;
        printf("ÉCHEC (test ligne %d)\n  attendu : %d:%d\n  obtenu  : %d:%d\n",
               l, ligne, colonne, jl, jc);
    }
}

#define V(src, att)        verifier(__LINE__, src, att)
#define VM(src, frag)      verifier_message(__LINE__, src, frag)
#define VP(src, i, li, co) verifier_position(__LINE__, src, i, li, co)

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* --- Phrases de la spécification --- */
    V("Le total vaut 3,5.", "MOT(le) MOT(total) MOT(vaut) NOMBRE(3.5) POINT");
    V("Le prix unitaire vaut 12,50.", "MOT(le) MOT(prix) MOT(unitaire) MOT(vaut) NOMBRE(12.50) POINT");
    V("Le total vaut prix unitaire × quantité.",
      "MOT(le) MOT(total) MOT(vaut) MOT(prix) MOT(unitaire) FOIS MOT(quantité) POINT");
    V("Le total devient total + 5.", "MOT(le) MOT(total) MOT(devient) MOT(total) PLUS NOMBRE(5) POINT");
    V("Afficher « Total à payer : » puis le total.",
      "MOT(afficher) TEXTE(Total à payer :) MOT(puis) MOT(le) MOT(total) POINT");
    V("", "");
    V("  \n\t ", "");

    /* --- Nombres (§ 1.2) --- */
    V("3, 5", "NOMBRE(3) VIRGULE NOMBRE(5)");
    V("1'000", "NOMBRE(1000)");
    V("1’234,50", "NOMBRE(1234.50)");
    V("1\xC2\xA0" "000", "NOMBRE(1000)");
    V("1\xE2\x80\xAF" "000", "NOMBRE(1000)");
    V("12'345'678", "NOMBRE(12345678)");
    V("1 000", "NOMBRE(1) NOMBRE(000)");
    V("Le total vaut 3.\n", "MOT(le) MOT(total) MOT(vaut) NOMBRE(3) POINT");
    V("1'00", "ERREUR");
    V("1234'567", "ERREUR");
    V("3.5", "ERREUR");
    V("1,2,3", "ERREUR");
    V("3kg", "ERREUR");
    VM("1'00", "milliers mal placé");
    VM("Le total vaut 3.5.", "Écrivez « 3,5 »");
    VM("1,2,3", "qu'une virgule");
    VM("3kg", "collé à un mot");

    /* --- Mots, casse, élision --- */
    V("SOLDE Solde solde", "MOT(solde) MOT(solde) MOT(solde)");
    V("ÉTÉ Œuvre", "MOT(été) MOT(œuvre)");
    V("taux2", "MOT(taux2)");
    V("l'addition", "ÉLISION(l) MOT(addition)");
    V("L’" "Addition", "ÉLISION(l) MOT(addition)");
    V("' x", "ERREUR");

    /* --- Normalisation NFC (é décomposé = é composé) --- */
    V("e\xCC\x81" "te\xCC\x81", "MOT(été)");
    V("c\xCC\xA7" "a", "MOT(ça)");

    /* --- Opérateurs (§ 1.4) --- */
    V("2 × 3 * 4 ÷ 5 / 6 − 7 - 8 ^ 9 (1)",
      "NOMBRE(2) FOIS NOMBRE(3) FOIS NOMBRE(4) DIVISE NOMBRE(5) DIVISE NOMBRE(6) "
      "MOINS NOMBRE(7) MOINS NOMBRE(8) PUISSANCE NOMBRE(9) PAR_OUV NOMBRE(1) PAR_FERM");
    V("3 – 2", "NOMBRE(3) MOINS NOMBRE(2)");
    V("3 — 2", "NOMBRE(3) ERREUR");
    VM("3 — 2", "Tiret cadratin");

    /* --- Texte (§ 1.5) --- */
    V("« Bonjour »", "TEXTE(Bonjour)");
    V("«\xC2\xA0" "Total :\xC2\xA0" "»", "TEXTE(Total :)");
    V("\" a b \"", "TEXTE( a b )");
    V("« »", "TEXTE()");
    V("« ouvert", "ERREUR");
    V("« a\nb »", "ERREUR");
    V("“salut”", "TEXTE(salut)");
    V("“ a ”", "TEXTE( a )");
    V("“ouvert", "ERREUR");
    VM("“ouvert", "guillemet fermant ”");
    V("” x", "ERREUR");
    V("„a“", "ERREUR");
    VM("„a“", "non reconnu");
    V("fin »", "MOT(fin) ERREUR");

    /* --- Remarques (§ 1.7) --- */
    V("Remarque : un test  \nLe x vaut 1.",
      "REMARQUE(un test) MOT(le) MOT(x) MOT(vaut) NOMBRE(1) POINT");
    V("remarque\xC2\xA0" ": insécable", "REMARQUE(insécable)");
    V("   Remarque : indentée", "REMARQUE(indentée)");
    V("Remarque :", "REMARQUE()");
    V("Le x vaut 1. Remarque : pas en début",
      "MOT(le) MOT(x) MOT(vaut) NOMBRE(1) POINT MOT(remarque) DEUX_POINTS MOT(pas) MOT(en) MOT(début)");

    /* --- Comparaisons et crochets (grammaire, § 1.6 et § 2.2) --- */
    V("= ≠ <> < > ≤ <= ≥ >=", "ÉGAL DIFFÉRENT DIFFÉRENT INFÉRIEUR SUPÉRIEUR INFÉRIEUR_OU_ÉGAL "
                            "INFÉRIEUR_OU_ÉGAL SUPÉRIEUR_OU_ÉGAL SUPÉRIEUR_OU_ÉGAL");
    V("a<b", "MOT(a) INFÉRIEUR MOT(b)");
    V("[frais de port et d'emballage]", "CROCHETS(frais de port et d'emballage)");
    V("[ Frais   DE port ]", "CROCHETS(frais de port)");
    V("[prix de l’article] × 2", "CROCHETS(prix de l'article) FOIS NOMBRE(2)");
    V("[]", "ERREUR");
    V("[a b", "ERREUR");
    V("[a 3]", "ERREUR");
    V("[a l']", "ERREUR");
    V("a ]", "MOT(a) ERREUR");
    VM("[a 3]", "« 3 » ne peut pas faire partie");

    /* --- Indentation : pas de tabulation en début de ligne (§ 1.6) --- */
    V("Si x :\n\tLe y vaut 1.", "MOT(si) MOT(x) DEUX_POINTS ERREUR");
    VM("Si x :\n  \tLe y vaut 1.", "Tabulation en début de ligne");
    VP("Si x :\n  \tLe", 3, 2, 3);
    V("a\tb", "MOT(a) MOT(b)");

    /* --- Encodage --- */
    V("\xEF\xBB\xBF" "Afficher 1.", "MOT(afficher) NOMBRE(1) POINT");
    V("\xFF", "ENCODAGE");
    V("\xC3", "ENCODAGE");
    V("\xC0\xAF", "ENCODAGE");
    V("\xED\xA0\x80", "ENCODAGE");
    VM("ab\n\xFF", "ligne 2, colonne 1");

    /* --- Positions : lignes et colonnes en points de code --- */
    VP("Le total\n  vaut 3.", 2, 2, 3);
    VP("Le total\r\n  vaut 3.", 2, 2, 3);
    VP("Le total\r  vaut 3.", 2, 2, 3);
    VP("été x", 1, 1, 5);
    VP("e\xCC\x81" "te\xCC\x81" " x", 1, 1, 5);
    VP("Remarque : a\nLe x", 1, 2, 1);
    VP("x 3.5", 1, 1, 3);

    /* --- Dates (§ 14.1) --- */
    V("Le jour vaut 21.09.2026.", "MOT(le) MOT(jour) MOT(vaut) DATE(2026-09-21) POINT");
    V("1.3.2026", "DATE(2026-03-01)");
    V("29.02.2024 31.12.9999 01.01.0001", "DATE(2024-02-29) DATE(9999-12-31) DATE(0001-01-01)");
    V("21.09.2026+1", "DATE(2026-09-21) PLUS NOMBRE(1)");
    VM("31.02.2026", "Le 31 février 2026 n'existe pas.");
    VM("29.02.2025", "Le 29 février 2025 n'existe pas.");
    VM("29.02.1900", "Le 29 février 1900 n'existe pas.");
    V("29.02.2000", "DATE(2000-02-29)");
    VM("21.13.2026", "Mois 13 impossible");
    VM("00.01.2026", "Jour 0 impossible");
    VM("01.01.0000", "Année 0 hors du calendrier");
    VM("21.9.26", "Date mal formée « 21.9.26 »");
    VM("021.09.2026", "Date mal formée");
    VM("21.09.20266", "Date mal formée");
    VM("21.09", "ajoutez l'année : 21.09.2026");
    VM("3.5", "Écrivez « 3,5 », ou, pour une date");
    VM("1234.5", "Écrivez « 1234,5 ».");
    VP("Le x vaut 21.09.2026.", 3, 1, 11);
    VP("Le x vaut 21.09.2026.", 4, 1, 21);

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
