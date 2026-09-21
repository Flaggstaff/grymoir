/* Tests de l'analyseur GrymoiR v0.1.
 * Chaque test compare la forme parenthésée de l'arbre, ou le message
 * et la position de la première erreur.
 */
#include "analyseur.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int total = 0, echecs = 0;

/* Arbre sous forme de texte, une phrase par ligne,
 * ou « ERREUR l:c message » si l'analyse échoue. */
static char *arbre(Portee *portee, const char *src, int interactif) {
    Programme p;
    Diagnostic d;
    Chaine c = {0};
    if (!analyser(src, strlen(src), portee, interactif, &p, &d)) {
        char *m = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return m;
    }
    for (size_t i = 0; i < p.nb; i++) {
        char *s = noeud_decrire(p.phrases[i]);
        if (i) chaine_ajouter(&c, "\n");
        chaine_ajouter(&c, s);
        free(s);
    }
    programme_liberer(&p);
    return chaine_rendre(&c);
}

static void signaler(int l, const char *src, const char *attendu, const char *obtenu) {
    echecs++;
    printf("ÉCHEC (test ligne %d)\n  source  : %s\n  attendu : %s\n  obtenu  : %s\n",
           l, src, attendu, obtenu);
}

/* Programme complet, portée neuve, mode fichier. */
static void verifier(int l, const char *src, const char *attendu) {
    total++;
    Portee *p = portee_creer();
    char *r = arbre(p, src, 0);
    if (strcmp(r, attendu) != 0) signaler(l, src, attendu, r);
    free(r);
    portee_detruire(p);
}

/* L'analyse échoue, à la position donnée, avec un message contenant le fragment. */
static void verifier_erreur(int l, const char *src, int ligne, int colonne, const char *fragment) {
    total++;
    Portee *p = portee_creer();
    char *r = arbre(p, src, 0);
    char prefixe[64];
    snprintf(prefixe, sizeof prefixe, "ERREUR %d:%d ", ligne, colonne);
    if (strncmp(r, prefixe, strlen(prefixe)) != 0 || !strstr(r, fragment)) {
        char *att = grym_formater("%s… %s …", prefixe, fragment);
        signaler(l, src, att, r);
        free(att);
    }
    free(r);
    portee_detruire(p);
}

/* Boucle interactive : plusieurs saisies qui partagent la même portée. */
static void verifier_session(int l, const char **saisies, const char **attendus, int nb) {
    total++;
    Portee *p = portee_creer();
    for (int i = 0; i < nb; i++) {
        char *r = arbre(p, saisies[i], 1);
        int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL
                                       : strcmp(r, attendus[i]) == 0;
        if (!ok) {
            signaler(l, saisies[i], attendus[i], r);
            free(r);
            break;
        }
        free(r);
    }
    portee_detruire(p);
}

/* Aide à la saisie : suggestions séparées par « | ». */
static void verifier_suites(int l, const char *src, const char *attendu) {
    total++;
    Suggestions s = suites_valides(src, strlen(src));
    Chaine c = {0};
    for (size_t k = 0; k < s.nb; k++) {
        if (k) chaine_ajouter(&c, " | ");
        chaine_ajouter(&c, s.items[k]);
    }
    char *r = chaine_rendre(&c);
    if (strcmp(r, attendu) != 0) signaler(l, src, attendu, r);
    free(r);
    suggestions_liberer(&s);
}

#define VS(src, att)           verifier_suites(__LINE__, src, att)
#define V(src, att)            verifier(__LINE__, src, att)
#define VE(src, li, co, frag)  verifier_erreur(__LINE__, src, li, co, frag)
#define SESSION(s, a)          verifier_session(__LINE__, s, a, (int)(sizeof s / sizeof *s))

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* --- Exemple de la spécification (§ 2.1, § 4) --- */
    V("Le prix unitaire vaut 12,50.\n"
      "La quantité vaut 3.\n"
      "Le total vaut prix unitaire × quantité.\n"
      "Le total devient total + 5.\n"
      "Afficher « Total à payer : » puis le total.",
      "(créer [prix unitaire] 12.50)\n"
      "(créer [quantité] 3)\n"
      "(créer [total] (× [prix unitaire] [quantité]))\n"
      "(modifier [total] (+ [total] 5))\n"
      "(afficher «Total à payer :» [total])");
    V("", "");
    V("Remarque : seulement une remarque", "(remarque «seulement une remarque»)");

    /* --- Priorités et associativité (§ 3.1) --- */
    V("Le x vaut 1 + 2 × 3.", "(créer [x] (+ 1 (× 2 3)))");
    V("Le x vaut 10 − 3 − 2.", "(créer [x] (− (− 10 3) 2))");
    V("Le x vaut 8 ÷ 4 ÷ 2.", "(créer [x] (÷ (÷ 8 4) 2))");
    V("Le x vaut 2 ^ 3 ^ 2.", "(créer [x] (^ 2 (^ 3 2)))");
    V("Le x vaut 2 × 3 ^ 2.", "(créer [x] (× 2 (^ 3 2)))");
    V("Le x vaut −2 ^ 2.", "(créer [x] (− (^ 2 2)))");
    V("Le x vaut (−2) ^ 2.", "(créer [x] (^ (groupe (− 2)) 2))");
    V("Le x vaut 2 × −3.", "(créer [x] (× 2 (− 3)))");
    V("Le x vaut 3 – 1.", "(créer [x] (− 3 1))");
    V("Le x vaut 2 ^ −1.", "(créer [x] (^ 2 (− 1)))");
    V("Le x vaut −2 ^ −2.", "(créer [x] (− (^ 2 (− 2))))");
    V("Le x vaut - - 3.", "(créer [x] (− (− 3)))");
    V("Le x vaut (1 + 2) × 3.", "(créer [x] (× (groupe (+ 1 2)) 3))");
    V("Le x vaut 2 * 3 / 4 - 1.", "(créer [x] (− (÷ (× 2 3) 4) 1))");

    /* --- Noms composés, plus longue correspondance (§ 2.2) --- */
    V("Le prix vaut 1.\nLe prix unitaire vaut 2.\n"
      "Le x vaut prix unitaire × 2.\nLe y vaut prix × 2.",
      "(créer [prix] 1)\n(créer [prix unitaire] 2)\n"
      "(créer [x] (× [prix unitaire] 2))\n(créer [y] (× [prix] 2))");
    V("Le prix de l'article vaut 5.\nLa date de la vente vaut 3.\n"
      "Afficher le prix de l'article puis la date de la vente.",
      "(créer [prix de l'article] 5)\n(créer [date de la vente] 3)\n"
      "(afficher [prix de l'article] [date de la vente])");

    /* --- Articles, élision, casse (§ 2.3, charte art. 4) --- */
    V("Le prix vaut 2.\nLa quantité vaut 3.\nLe total vaut le prix × la quantité.",
      "(créer [prix] 2)\n(créer [quantité] 3)\n(créer [total] (× [prix] [quantité]))");
    V("L'addition vaut 5.\nLe x vaut l'addition + 1.",
      "(créer [addition] 5)\n(créer [x] (+ [addition] 1))");
    V("LE TOTAL VAUT 3.\nAfficher Le Total.", "(créer [total] 3)\n(afficher [total])");
    V("L’addition vaut 5.\nLa addition devient 6.\nLa addition devient 7.",
      "(créer [addition] 5)\n(modifier [addition] 6)\n(modifier [addition] 7)");

    /* --- Afficher (§ 4) --- */
    V("Afficher « a » puis 1 + 1 puis « b ».", "(afficher «a» (+ 1 1) «b»)");
    V("Afficher 3.", "(afficher 3)");

    /* --- Remarques (§ 1.7) --- */
    V("Remarque : début\nLe x vaut 1.\nRemarque : fin",
      "(remarque «début»)\n(créer [x] 1)\n(remarque «fin»)");

    /* --- Plusieurs phrases sur une ligne, une phrase sur plusieurs lignes --- */
    V("Le a vaut 1. Le b vaut a + 1.", "(créer [a] 1)\n(créer [b] (+ [a] 1))");
    V("Le a vaut 1\n  + 2.", "(créer [a] (+ 1 2))");

    /* --- Erreurs de référence (§ 6) --- */
    VE("Le total vaut 3.\nLe total vaut 4.", 2, 4,
       "« total » existe déjà (ligne 1). Pour le modifier, écrivez : Le total devient …");
    VE("Le total devient 4.", 1, 4,
       "« total » n'existe pas. Pour le créer, écrivez : Le total vaut …");
    VE("Le total vaut 3.\nLe totl devient 4.", 2, 4, "« totl » inconnu, vouliez-vous « total » ?");
    VE("Le total vaut 3.\nLe x vaut totl + 1.", 2, 11, "« totl » inconnu, vouliez-vous « total » ?");
    VE("Le solde vaut 3.\nAfficher le soldee.", 2, 13, "vouliez-vous « solde » ?");
    VE("Le total vaut 3.\nLa total devient 4.", 2, 1, "« total » est masculin (déclaré ligne 1).");
    VE("L'addition vaut 3.\nLa addition devient 4.\nLe x vaut le addition.", 3, 11,
       "« addition » est féminin (déclaré ligne 2).");
    VE("Le x vaut 3", 1, 12, "Point final manquant (ligne 1).");
    VE("Le x vaut 3\nLe y vaut 4.", 1, 12, "Point final manquant (ligne 1).");
    VE("Le x vaut 1 / 0.\nLe x vaut 3.5.", 2, 11, "Écrivez « 3,5 »");

    /* --- Autres erreurs --- */
    VE("Le prix unitaire vaut 1.\nLe x vaut prix unitair × 2.", 2, 11,
       "« prix unitair » inconnu, vouliez-vous « prix unitaire » ?");
    VE("Le x vaut y.", 1, 11, "« y » inconnu.");
    VE("Le total vaut total + 1.", 1, 15, "« total » inconnu");
    VE("3 + 4.", 1, 1, "une phrase commence par Le, La, L', Afficher, Si, Pour ou le nom d'une action");
    VE("Le x vaut 3. Remarque : non", 1, 14, "Une remarque doit commencer une ligne");
    VE("Le x vaut 1 +\nRemarque : coupe\n2.", 2, 1, "ne peut pas couper une phrase");
    VE("Le x vaut (1 + 2.", 1, 17, "Parenthèse fermante manquante");
    VE("Le x vaut 1 +.", 1, 14, "Expression incomplète");
    VE("Le x vaut « a ».", 1, 11, "Un texte ne peut apparaître que dans une phrase Afficher.");
    VE("Afficher.", 1, 9, "Rien à afficher");
    VE("Afficher 1 puis.", 1, 16, "Élément manquant après « puis »");
    VE("Le vaut 3.", 1, 4, "Nom manquant entre « le » et « vaut ».");
    VE("Le x 3.", 1, 1, "Verbe manquant");
    VE("Le x vaut 1 2.", 1, 13, "« 2 » inattendu, attendu : un opérateur, une comparaison (est, =, <…) ou un point final.");
    VE("Le x vaut (1 2).", 1, 14, "Parenthèse fermante manquante");
    VE("Afficher 1 2.", 1, 12, "« 2 » inattendu, attendu : un opérateur, une comparaison (est, =, <…), « puis » ou un point final.");
    VE("Le x vaut 1 puis 2.", 1, 13, "« puis » inattendu, attendu : un opérateur, une comparaison (est, =, <…) ou un point final.");
    VE("Le x vaut vaut.", 1, 11, "« vaut » inattendu, attendu : un nombre, un nom, une parenthèse, « vrai » ou « faux ».");
    VE("Le prix puis vaut 3.", 1, 9, "« puis » est un mot réservé");
    VE("Le la vaut 3.", 1, 4, "Un nom ne peut pas commencer par « la ».");
    VE("Le x vaut 1.\nAfficher le x puis le.", 2, 20, "Nom attendu après « le ».");
    VE("Le x 3 vaut 2.", 1, 6, "« 3 » ne peut pas faire partie d'un nom.");
    VE("Afficher « a » « b ».", 1, 16, "Texte « b » inattendu, attendu : « puis » ou un point final.");
    VE("Afficher « a » + 1.", 1, 16, "« + » inattendu, attendu : « puis » ou un point final.");

    /* --- Boucle interactive (§ 3.3) --- */
    {
        const char *s[] = { "1,1 + 2,2" };
        const char *a[] = { "(évaluer (+ 1.1 2.2))" };
        SESSION(s, a);
    }
    {   /* les noms persistent d'une saisie à l'autre */
        const char *s[] = { "Le total vaut 3.", "le total", "Le total devient total × 2", "total + 1" };
        const char *a[] = { "(créer [total] 3)", "(évaluer [total])",
                            "(modifier [total] (× [total] 2))", "(évaluer (+ [total] 1))" };
        SESSION(s, a);
    }
    {   /* une saisie ratée ne déclare rien, même la phrase réussie qui la précède */
        const char *s[] = { "Le a vaut 1. Le b vaut c.", "Le a vaut 2.", "b" };
        const char *a[] = { "~« c » inconnu", "(créer [a] 2)", "~« b » inconnu" };
        SESSION(s, a);
    }
    {   /* un genre fixé par une saisie ratée est annulé aussi */
        const char *s[] = { "L'addition vaut 1.", "La addition devient c.", "Le addition devient 2." };
        const char *a[] = { "(créer [addition] 1)", "~« c » inconnu", "(modifier [addition] 2)" };
        SESSION(s, a);
    }

    /* --- Conditions (§ 5) --- */
    V("Le x vaut 3.\nSi x est positif, afficher x.",
      "(créer [x] 3)\n(si (positif [x]) (bloc (afficher [x])))");
    V("Le x vaut 1.\nAfficher x < 2 puis x ≥ 1 puis x <> 1 puis x = 1.",
      "(créer [x] 1)\n(afficher (< [x] 2) (≥ [x] 1) (≠ [x] 1) (= [x] 1))");
    V("Le x vaut 1.\nAfficher x est égal à 1 puis x est différent de 2 puis x est inférieur ou égal à 3 "
      "puis x n'est pas supérieur à 0.",
      "(créer [x] 1)\n(afficher (= [x] 1) (≠ [x] 2) (≤ [x] 3) (non (> [x] 0)))");
    V("La quantité vaut 3.\nAfficher la quantité est supérieure à 2 puis la quantité est nulle.",
      "(créer [quantité] 3)\n(afficher (> [quantité] 2) (nul [quantité]))");
    V("Le prix vaut 3.\nLe x vaut 1.\nAfficher x est inférieur au prix puis x est différent du prix.",
      "(créer [prix] 3)\n(créer [x] 1)\n(afficher (< [x] [prix]) (≠ [x] [prix]))");
    V("Le x vaut 1.\nAfficher (x > 0 et x < 2) ou x = 5.",
      "(créer [x] 1)\n(afficher (ou (groupe (et (> [x] 0) (< [x] 2))) (= [x] 5)))");
    V("Le x vaut 1.\nAfficher (x + 1) × 2 > 3.",
      "(créer [x] 1)\n(afficher (> (× (groupe (+ [x] 1)) 2) 3))");
    V("Le t vaut 1 < 2.\nSi le t est vrai, afficher 1.",
      "(créer [t] (< 1 2))\n(si (vrai [t]) (bloc (afficher 1)))");
    V("Si faux, afficher 1. Sinon, afficher 2.",
      "(si faux (bloc (afficher 1)) (sinon (bloc (afficher 2))))");
    V("Le x vaut 5.\nSi x > 3 :\n    Afficher « grand ».\n    Afficher x.\nSinon :\n    Afficher « petit ».\n"
      "Afficher « fin ».",
      "(créer [x] 5)\n(si (> [x] 3) (bloc (afficher «grand») (afficher [x])) (sinon (bloc (afficher «petit»))))\n"
      "(afficher «fin»)");
    V("Le x vaut 5.\nSi x > 3 :\n    Afficher 1.\nSinon si x > 1 :\n    Afficher 2.\nSinon :\n    Afficher 3.",
      "(créer [x] 5)\n(si (> [x] 3) (bloc (afficher 1)) (sinon (bloc (si (> [x] 1) (bloc (afficher 2)) "
      "(sinon (bloc (afficher 3)))))))");
    V("Le x vaut 5.\nSi x > 0 :\n  Si x > 3 :\n    Afficher 1.\n  Sinon :\n    Afficher 2.\n  Afficher 3.",
      "(créer [x] 5)\n(si (> [x] 0) (bloc (si (> [x] 3) (bloc (afficher 1)) (sinon (bloc (afficher 2)))) "
      "(afficher 3)))");
    V("Si vrai :\n    Le y vaut 2.\nLe y vaut 3.",
      "(si vrai (bloc (créer [y] 2)))\n(créer [y] 3)");
    V("Si vrai :\n    Remarque : commentaire libre\n    Afficher 1.",
      "(si vrai (bloc (remarque «commentaire libre») (afficher 1)))");
    V("  Le x vaut 1.\n  Afficher x.", "(créer [x] 1)\n(afficher [x])");

    /* --- Noms entre crochets (§ 2.2) --- */
    V("Le [frais et port] vaut 3.\nAfficher [frais et port] × 2.",
      "(créer [frais et port] 3)\n(afficher (× [frais et port] 2))");
    V("Le total vaut 1.\nAfficher [total] puis le [total].", "(créer [total] 1)\n(afficher [total] [total])");
    VE("Le frais et port vaut 3.", 1, 10,
       "« et » est un mot réservé : pour l'utiliser dans un nom, écrivez [frais et port].");
    VE("Le [a et b] vaut 1.\nLe [a et b] vaut 2.", 2, 4, "Pour le modifier, écrivez : Le [a et b] devient …");
    VE("Le [a et b] 3 vaut 2.", 1, 13, "un nom entre crochets s'écrit seul");
    VE("Afficher [inconnu].", 1, 10, "« [inconnu] » inconnu.");

    /* --- Erreurs de conditions --- */
    VE("La quantité vaut 3.\nAfficher la quantité est positif.", 2, 26,
       "« quantité » est féminin (déclaré ligne 1) : écrivez « positive ».");
    VE("L'addition vaut 3.\nAfficher l'addition est positive.\nLe x vaut le addition.", 3, 11,
       "« addition » est féminin (déclaré ligne 2).");
    VE("Le x vaut 1.\nAfficher x est inférieur à le x.", 2, 26, "« à le » s'écrit « au ».");
    VE("La quantité vaut 1.\nAfficher 3 est supérieur au quantité.", 2, 26, "« quantité » est féminin");
    VE("Le x vaut 1.\nAfficher x > 0 et x < 2 ou x = 5.", 2, 25, "mélangés sans parenthèses");
    VE("Le x vaut 1.\nAfficher x et 3.", 2, 12, "ce qui suit n'est ni vrai ni faux");
    VE("Si 3 + 4, afficher 1.", 1, 1, "Condition attendue après « Si »");
    VE("Le x vaut 1.\nAfficher x n'est égal à 1.", 2, 18, "« n'est » doit être suivi de « pas ».");
    VE("Le x vaut 1.\nSi x > 0 :\n    Le y vaut 2.\nAfficher y.", 4, 10, "« y » inconnu.");
    VE("Le x vaut 1.\n  Afficher x.", 2, 3, "Indentation inattendue");
    VE("Si vrai :\n    Afficher 1.\n  Afficher 2.", 3, 3, "Indentation inattendue");
    VE("Si vrai :\nAfficher 1.", 2, 1, "plus indentées que « Si »");
    VE("Si vrai : Afficher 1.", 1, 9, "Bloc vide");
    VE("Si vrai :\n    Afficher 1.\n  Sinon :\n    Afficher 2.", 3, 3, "doit être aligné sur son « Si »");
    VE("Sinon, afficher 1.", 1, 1, "« Sinon » sans « Si » correspondant.");
    VE("Si vrai, si vrai, afficher 1.", 1, 10, "la forme courte n'accepte qu'une phrase simple");
    VE("Si vrai afficher 1.", 1, 9, "attendu : « et », « ou », « , » ou « : »");

    /* --- Formules (§ 9) --- */
    V("Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de 7.",
      "(calcul [carré] ([nombre]) (× [nombre] [nombre]))\n(afficher (appel [carré] 7))");
    V("La moyenne d'un premier nombre et d'un second nombre vaut (premier nombre + second nombre) ÷ 2.\n"
      "Afficher la moyenne de 4 et de 6.",
      "(calcul [moyenne] ([premier nombre] [second nombre]) (÷ (groupe (+ [premier nombre] [second nombre])) 2))\n"
      "(afficher (appel [moyenne] 4 6))");
    V("La valeur absolue d'un nombre :\n    Si nombre est négatif, rendre −nombre.\n    Rendre nombre.",
      "(calcul [valeur absolue] ([nombre]) (bloc (si (négatif [nombre]) (bloc (rendre (− [nombre])))) (rendre [nombre])))");
    V("Le carré d'un nombre vaut nombre × nombre.\nLe prix vaut 3.\nAfficher le carré du prix + 1 puis carré de (2 + 1).",
      "(calcul [carré] ([nombre]) (× [nombre] [nombre]))\n(créer [prix] 3)\n"
      "(afficher (+ (appel [carré] [prix]) 1) (appel [carré] (groupe (+ 2 1))))");
    V("Le total vaut 0.\nPour relancer un client :\n    Si le client est négatif, afficher « Relance ».\n"
      "    Le total devient total + 1.\nRelancer −3.",
      "(créer [total] 0)\n(action [relancer] ([client]) (bloc (si (négatif [client]) (bloc (afficher «Relance»))) "
      "(modifier [total] (+ [total] 1))))\n(action-appel [relancer] (− 3))");
    V("Pour saluer :\n    Afficher « Bonjour ».\nSaluer.", "(action [saluer] () (bloc (afficher «Bonjour»)))\n(action-appel [saluer])");
    V("Pour payer un montant et une remise :\n    Afficher montant − remise.\nPayer 10 et 2.",
      "(action [payer] ([montant] [remise]) (bloc (afficher (− [montant] [remise]))))\n(action-appel [payer] 10 2)");
    V("La factorielle d'un nombre :\n    Si nombre ≤ 1, rendre 1.\n    Rendre nombre × la factorielle de (nombre − 1).",
      "(calcul [factorielle] ([nombre]) (bloc (si (≤ [nombre] 1) (bloc (rendre 1))) "
      "(rendre (× [nombre] (appel [factorielle] (groupe (− [nombre] 1)))))))");
    V("Le double d'une quantité vaut quantité × 2.\nAfficher la quantité est positive.",
      "ERREUR 2:13 « quantité » inconnu.");
    V("Le x vaut 1.\nLe carré d'un nombre vaut nombre × nombre.\nLe nombre vaut 2.",
      "(créer [x] 1)\n(calcul [carré] ([nombre]) (× [nombre] [nombre]))\n(créer [nombre] 2)");

    /* --- Erreurs de formules --- */
    VE("Le taux vaut 2.\nLe double d'un nombre vaut nombre × taux.", 2, 37,
       "« taux » n'est pas visible dans un calcul");
    VE("Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de 1 et de 2.", 2, 10, "« carré » attend 1 paramètre, 2 donnés.");
    VE("Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré.", 2, 18, "« carré » est un calcul");
    VE("Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de le x.", 2, 19, "« de le » s'écrit « du ».");
    VE("La valeur d'un nombre :\n    Si nombre > 0, rendre 1.", 1, 1, "doit se terminer par « Rendre … »");
    VE("Le double d'un nombre :\n    Afficher nombre.\n    Rendre nombre.", 2, 5, "Un calcul n'affiche rien");
    VE("Pour saluer :\n    Rendre 1.", 2, 5, "Une action ne rend rien");
    VE("Rendre 1.", 1, 1, "« Rendre » ne s'emploie que dans un calcul.");
    VE("Pour saluer :\n    Afficher 1.\nLe f d'un nombre :\n    Saluer.\n    Rendre 1.", 4, 5,
       "Un calcul n'appelle pas d'action");
    VE("Si vrai :\n    Pour saluer :\n        Afficher 1.", 2, 5, "au premier niveau du programme");
    VE("Pour saluer un client :\n    Afficher 1.\nSaluer.", 3, 1, "« saluer » attend 1 paramètre, 0 donné.");
    VE("Pour saluer :\n    Afficher 1.\nAfficher saluer.", 3, 10, "« saluer » est une action");
    VE("Le carré d'un nombre vaut nombre × nombre.\nLe carré devient 3.", 2, 4, "« carré » est un calcul : il ne se modifie pas.");
    VE("Le carré d'un nombre vaut nombre × nombre.\nLe carré d'un x vaut x.", 2, 4, "« carré » existe déjà.");
    VE("Le f d'un a et d'un a vaut a.", 1, 21, "Paramètre « a » déjà nommé.");
    VE("Pour saluer, afficher 1.", 1, 12, "« : » attendu");
    VE("Le carré d'un nombre devient 3.", 1, 22, "Un calcul ne se modifie pas");

    /* --- Aide à la saisie (§ 8) --- */
    VS("", "Le | La | L' | Afficher | Si | Pour | Remarque :");
    VS("Le x vaut 1.\n", "Le | La | L' | Afficher | Si | Pour | Remarque :");
    VS("Af", "Afficher");
    VS("l", "Le | La | L'");
    VS("Le total vaut 1.\nLe ", "total | (nouveau nom)");
    VS("Le total vaut 1.\nLe to", "total");
    VS("Le total vaut 1.\nLe total ", "vaut | devient");
    VS("Le total vaut 1.\nLe total d", "devient");
    VS("Le prix vaut 1.\nLe prix unitaire vaut 2.\nLe prix ", "unitaire | vaut | devient");
    VS("Le prix unitaire vaut 2.\nLe x vaut ", "prix unitaire | (nombre) | ( | − | vrai | faux");
    VS("Le prix unitaire vaut 2.\nLe x vaut pr", "prix unitaire");
    VS("Le prix vaut 1.\nLe prix unitaire vaut 2.\nLe x vaut prix ",
       "unitaire | + | − | × | ÷ | ^ | est | n'est pas | = | ≠ | < | > | ≤ | ≥ | et | ou | .");
    VS("Le prix vaut 1.\nLe prix unitaire vaut 2.\nLe x vaut prix u", "unitaire");
    VS("Le x vaut (1 + 2", "+ | − | × | ÷ | ^ | )");
    VS("Le x vaut 1.\nAfficher ", "x | (nombre) | ( | − | vrai | faux | « … »");
    VS("Le x vaut 1.\nAfficher x ", "+ | − | × | ÷ | ^ | est | n'est pas | = | ≠ | < | > | ≤ | ≥ | et | ou | puis | .");
    VS("Le x vaut 1.\nAfficher x p", "puis");
    VS("Afficher « a » ", "puis | .");
    VS("Le prix de l'article vaut 1.\nAfficher le prix de ", "l'");
    VS("Le prix de l'article vaut 1.\nAfficher le prix de l'", "article");
    VS("Le prix de l'article vaut 1.\nAfficher le prix d", "de");
    VS("Le x vaut 1 +\nLe y", "");            /* erreur plus haut : pas de suggestion */
    VS("Afficher « ouvert", "");              /* au milieu d'un texte : rien */
    VS("Le x vaut 1.\nSi ", "x | (nombre) | ( | − | vrai | faux");
    VS("Le x vaut 1.\nSi x ",
       "+ | − | × | ÷ | ^ | est | n'est pas | = | ≠ | < | > | ≤ | ≥ | et | ou | , | :");
    VS("Le x vaut 1.\nSi x est ",
       "égal à | différent de | inférieur à | inférieur ou égal à | supérieur à | supérieur ou égal à | "
       "positif | négatif | nul | vrai | faux");
    VS("La quantité vaut 1.\nSi la quantité est s", "supérieure à | supérieure ou égale à");
    VS("Le x vaut 1.\nSi x est inférieur ", "ou | à | au");
    VS("Le [a et b] vaut 1.\nAfficher ", "[a et b] | (nombre) | ( | − | vrai | faux | « … »");
    VS("Le [a et b] vaut 1.\nAfficher [a", "[a et b]");
    VS("Pour relancer :\n    Afficher 1.\nRel", "Relancer");
    VS("Le taux vaut 2.\nLe double d'un nombre vaut ", "double | nombre | (nombre) | ( | − | vrai | faux");
    VS("Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré ", "de | du");

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
