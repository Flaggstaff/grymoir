/* Tests des imprimeurs : forme littéraire canonique et forme compacte (grammaire, § 11 et § 12). */
#include "analyseur.h"
#include "imprimeur.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int total = 0, echecs = 0;

static char *imprimer_source(const char *src, int compact) {
    Portee *p = portee_creer();
    Programme prog;
    Diagnostic d;
    char *r;
    if (!analyser(src, strlen(src), p, 0, &prog, &d)) {
        r = grym_formater("ERREUR %s", d.message);
        diagnostic_liberer(&d);
    } else {
        r = compact ? imprimer_compact(&prog) : imprimer_litteraire(&prog);
        programme_liberer(&prog);
    }
    portee_detruire(p);
    return r;
}

static void verifier(int l, const char *src, const char *attendu, int compact) {
    total++;
    char *r = imprimer_source(src, compact);
    if (strcmp(r, attendu) != 0) {
        echecs++;
        printf("ÉCHEC (test ligne %d)\n  source  :\n%s\n  attendu :\n%s\n  obtenu  :\n%s\n", l, src, attendu, r);
    }
    free(r);
}

/* La forme canonique est un point fixe : la formater ne change plus rien. */
static void verifier_point_fixe(int l, const char *src) {
    total++;
    char *a = imprimer_source(src, 0);
    char *b = imprimer_source(a, 0);
    if (strcmp(a, b) != 0) {
        echecs++;
        printf("ÉCHEC (test ligne %d) : forme canonique instable\n  première :\n%s\n  seconde :\n%s\n", l, a, b);
    }
    free(a);
    free(b);
}

#define LITT(src, att)  verifier(__LINE__, src, att, 0)
#define COMP(src, att)  verifier(__LINE__, src, att, 1)
#define FIXE(src)       verifier_point_fixe(__LINE__, src)

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    /* --- Forme littéraire canonique (§ 12) : présentation normalisée, programme conservé --- */
    LITT("le  TOTAL   vaut 1000.   Afficher \"x\" puis TOTAL.\n\n\n\nsi total > 3 :\n      afficher \" a \".\n"
         "SINON, afficher “b”.",
         "Le total vaut 1'000.\nAfficher « x » puis total.\n\nSi total > 3 :\n    Afficher \" a \".\n"
         "Sinon, afficher « b ».\n");
    LITT("Le prix vaut 2.\nLe x vaut 1.\nAfficher x est inférieur au prix puis x est différent du prix "
         "puis le x n'est pas nul.",
         "Le prix vaut 2.\nLe x vaut 1.\nAfficher x est inférieur au prix puis x est différent du prix "
         "puis le x n'est pas nul.\n");
    LITT("La quantité vaut 3.\nSi la quantité est supérieure ou égale à 2, afficher 1.",
         "La quantité vaut 3.\nSi la quantité est supérieure ou égale à 2, afficher 1.\n");
    LITT("Le [frais et port] vaut 3.\nAfficher [frais et port] × 2.",
         "Le [frais et port] vaut 3.\nAfficher [frais et port] × 2.\n");
    LITT("Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de 3 + 1 puis carré de (3 + 1).",
         "Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de 3 + 1 puis carré de (3 + 1).\n");
    LITT("Pour chaque i de 1 à 3, afficher i.\nPour chaque t de 0 à 1 par pas de 0,5 :\n  Afficher t.",
         "Pour chaque i de 1 à 3, afficher i.\nPour chaque t de 0 à 1 par pas de 0,5 :\n    Afficher t.\n");
    LITT("Le x vaut 1.\nSelon x :\n  Cas 1 ou de 2 à 3, afficher 1.\n  Cas positif :\n    Afficher 2.\n  Autrement, afficher 3.",
         "Le x vaut 1.\nSelon x :\n    Cas 1 ou de 2 à 3, afficher 1.\n    Cas positif :\n        Afficher 2.\n"
         "    Autrement, afficher 3.\n");
    LITT("Remarque :   note  \nLe x vaut 1.", "Remarque : note\nLe x vaut 1.\n");

    FIXE("Le total vaut 120.\nLe rabais vaut 0.\nSi le total est supérieur à 100 :\n    Le rabais devient 10.\n"
         "Sinon si le total est supérieur ou égal à 50 :\n    Le rabais devient 5.\nSinon :\n    Le rabais devient 0.");
    FIXE("La valeur absolue d'un nombre :\n    Si nombre est négatif, rendre −nombre.\n    Rendre nombre.\n"
         "Pour saluer une personne :\n    Afficher « Bonjour » puis personne.\nSaluer 3.");
    FIXE("Le x vaut 0.\nTant que vrai :\n    Le x devient x + 1.\n    Si x = 3, sortir de la boucle.\n"
         "Répéter 2 fois, passer au tour suivant.");

    /* --- Forme compacte (§ 11) --- */
    COMP("Remarque : exemple\nLe prix unitaire vaut 12,50.\nLa quantité vaut 3.\nL'addition vaut 0.\n"
         "L'addition devient prix unitaire × quantité.\nAfficher « Total : » puis l'addition.",
         "# exemple\n_le prix_unitaire << 12,50\n_la quantité << 3\n_l'addition << 0\n"
         "addition << prix_unitaire × quantité\n_afficher « Total : » ; addition\n");
    COMP("Le total vaut 120.\nSi le total est supérieur à 100 :\n    Le total devient 1.\n"
         "Sinon si total ≥ 50, le total devient 2.\nSinon :\n    Le total devient 3.",
         "_le total << 120\n_si total > 100 _alors\n    total << 1\n_sinon_si total ≥ 50 _alors\n"
         "    total << 2\n_sinon\n    total << 3\n_fin\n");
    COMP("Le x vaut 1.\nAfficher x n'est pas nul et (x est positif ou faux) puis x est différent de 2.",
         "_le x << 1\n_afficher _non (x _nul) _et (x _positif _ou _faux) ; x ≠ 2\n");
    COMP("Le [frais de port et d'emballage] vaut 12.", "_le frais_de_port_et_d'emballage << 12\n");
    COMP("La moyenne d'un premier nombre et d'une seconde valeur vaut (premier nombre + seconde valeur) ÷ 2.\n"
         "Afficher la moyenne de 4 et de (1 + 1).",
         "_calcul _la moyenne(_un premier_nombre ; _une seconde_valeur) << (premier_nombre + seconde_valeur) ÷ 2\n"
         "_afficher moyenne(4 ; 1 + 1)\n");
    COMP("Le total vaut 0.\nPour ajouter un montant :\n    Le total devient total + montant.\nAjouter 5.\n"
         "Pour saluer :\n    Afficher 1.\nSaluer.",
         "_le total << 0\n_action ajouter(_un montant)\n    total << total + montant\n_fin\najouter(5)\n"
         "_action saluer()\n    _afficher 1\n_fin\nsaluer()\n");
    COMP("La valeur absolue d'un nombre :\n    Si nombre est négatif, rendre −nombre.\n    Rendre nombre.",
         "_calcul _la valeur_absolue(_un nombre)\n    _si nombre _négatif _alors\n        _rendre −nombre\n"
         "    _fin\n    _rendre nombre\n_fin\n");
    COMP("Le x vaut 3.\nTant que x > 0, le x devient x − 1.\nRépéter 2 fois :\n    Sortir de la boucle.\n"
         "Pour chaque i de 1 à 9 par pas de 2, passer au tour suivant.",
         "_le x << 3\n_tant_que x > 0\n    x << x − 1\n_fin\n_répéter 2 _fois\n    _sortir\n_fin\n"
         "_pour_chaque i _de 1 _à 9 _pas 2\n    _passer\n_fin\n");
    COMP("Le x vaut 1.\nSelon x :\n    Cas 1 ou de 2 à 3, afficher 1.\n    Cas supérieur ou égal à 10 ou négatif :\n"
         "        Afficher 2.\n    Autrement, afficher 3.",
         "_le x << 1\n_selon x\n    _cas 1 _ou _de 2 _à 3\n        _afficher 1\n    _cas ≥ 10 _ou _négatif\n"
         "        _afficher 2\n    _autrement\n        _afficher 3\n_fin\n");

    /* --- Objets (§ 13) --- */
    LITT("Un client a : un nom, un solde.\nLe c vaut un nouveau client :\n  Le nom vaut « Dupont ».\n"
         "Le solde du c devient 3.\nAfficher le nom du c.",
         "Un client a :\n    un nom,\n    un solde.\nLe c vaut un nouveau client :\n    Le nom vaut « Dupont ».\n"
         "Le solde du c devient 3.\nAfficher le nom du c.\n");
    COMP("Une facture a : un montant, une date.\nLa f vaut une nouvelle facture :\n    Le montant vaut 3.\n"
         "Le montant de la f devient montant de la f × 2.\nAfficher la f.",
         "_classe _une facture\n    _un montant\n    _une date\n_fin\n_la f << _nouveau facture _avec\n"
         "    montant << 3\n_fin\nf.montant << f.montant × 2\n_afficher f\n");

    LITT("Une personne a : un nom.\nUn membre est une personne. Un membre a : une licence.",
         "Une personne a :\n    un nom.\nUn membre est une personne.\nUn membre a :\n    une licence.\n");
    COMP("Une personne a : un nom.\nUn membre est une personne.\nUn invité est une personne.\nUn invité a : un hôte.",
         "_classe _une personne\n    _un nom\n_fin\n_classe _un membre _est _une personne\n_fin\n"
         "_classe _un invité _est _une personne\n    _un hôte\n_fin\n");

    /* --- Dates (§ 14) --- */
    LITT("Le jour vaut 1.3.2026 + 30.\nSi aujourd'hui > jour, afficher 21.9.2026.",
         "Le jour vaut 01.03.2026 + 30.\nSi aujourd'hui > jour, afficher 21.09.2026.\n");
    COMP("Le jour vaut 1.3.2026.\nSi aujourd'hui > jour, afficher jour + 1.",
         "_le jour << 01.03.2026\n_si _aujourd'hui > jour _alors\n    _afficher jour + 1\n_fin\n");

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
