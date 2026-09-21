/* Tests de la forme compacte : lecture, exécution, erreurs, allers-retours (grammaire, § 11 et § 12). */
#include "analyseur.h"
#include "bytecode.h"
#include "compilateur.h"
#include "imprimeur.h"
#include "texte.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int total = 0, echecs = 0;

static void signaler(int l, const char *src, const char *attendu, const char *obtenu) {
    echecs++;
    printf("ÉCHEC (test ligne %d)\n  source  :\n%s\n  attendu :\n%s\n  obtenu  :\n%s\n", l, src, attendu, obtenu);
}

/* Lit une source (compacte ou littéraire) ; NULL et un message « ERREUR l:c … » sinon. */
static int lire(const char *src, int compact, Programme *p, char **erreur) {
    Portee *portee = portee_creer();
    Diagnostic d;
    int ok = compact ? analyser_compact(src, strlen(src), portee, p, &d)
                     : analyser(src, strlen(src), portee, 0, p, &d);
    portee_detruire(portee);
    if (!ok) {
        *erreur = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
    }
    return ok;
}

static char *executer(const char *src) {
    Programme p;
    char *err = NULL;
    if (!lire(src, 1, &p, &err)) return err;
    Diagnostic d;
    Module *m = compiler(&p, &d);
    programme_liberer(&p);
    Machine *mach = machine_creer();
    Chaine s = {0};
    int ok = m && machine_executer(mach, m, &s, &d);
    module_detruire(m);
    machine_detruire(mach);
    char *r = chaine_rendre(&s);
    if (!ok) {
        free(r);
        r = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
    }
    return r;
}

static void verifier_execution(int l, const char *src, const char *attendu) {
    total++;
    char *r = executer(src);
    if (strcmp(r, attendu) != 0) signaler(l, src, attendu, r);
    free(r);
}

static void verifier_erreur(int l, const char *src, int ligne, int colonne, const char *fragment) {
    total++;
    Programme p;
    char *err = NULL;
    if (lire(src, 1, &p, &err)) {
        programme_liberer(&p);
        signaler(l, src, fragment, "(accepté)");
        return;
    }
    char prefixe[64];
    snprintf(prefixe, sizeof prefixe, "ERREUR %d:%d ", ligne, colonne);
    if (strncmp(err, prefixe, strlen(prefixe)) != 0 || !strstr(err, fragment)) {
        char *att = grym_formater("%s… %s …", prefixe, fragment);
        signaler(l, src, att, err);
        free(att);
    }
    free(err);
}

/* Garantie 1 (§ 12) : compacte → littéraire → compacte redonne le texte compact canonique. */
static void verifier_aller_retour(int l, const char *compact, const char *litteraire_attendu) {
    total++;
    Programme p;
    char *err = NULL;
    if (!lire(compact, 1, &p, &err)) { signaler(l, compact, "lecture", err); free(err); return; }
    char *c1 = imprimer_compact(&p);
    char *lit = imprimer_litteraire(&p);
    programme_liberer(&p);
    if (strcmp(c1, compact) != 0) signaler(l, compact, compact, c1);
    else if (litteraire_attendu && strcmp(lit, litteraire_attendu) != 0) signaler(l, compact, litteraire_attendu, lit);
    else {
        Programme q;
        if (!lire(lit, 0, &q, &err)) { signaler(l, lit, "relecture littéraire", err); free(err); }
        else {
            char *c2 = imprimer_compact(&q);
            if (strcmp(c2, compact) != 0) signaler(l, lit, compact, c2);
            free(c2);
            programme_liberer(&q);
        }
    }
    free(c1);
    free(lit);
}

#define EXEC(src, att)            verifier_execution(__LINE__, src, att)
#define ERR(src, li, co, frag)    verifier_erreur(__LINE__, src, li, co, frag)
#define ALLER_RETOUR(c, l)        verifier_aller_retour(__LINE__, c, l)

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    /* --- Lecture et exécution --- */
    EXEC("_le prix_unitaire << 12,50\n_la quantité << 3\n_afficher « Total : » ; prix_unitaire × quantité\n",
         "Total : 37,50\n");
    EXEC("_le x << 3\n_tant_que x > 0\n    _afficher x\n    x << x − 1\n_fin\n", "3\n2\n1\n");
    EXEC("  _le x << 1\n_si x _positif _alors\n_afficher « oui »\n     _sinon\n _afficher « non »\n_fin\n", "oui\n");
    EXEC("_calcul _le carré(_un nombre) << nombre × nombre\n_afficher carré(3 + 1) ; carré(2) + 1\n", "16 5\n");
    EXEC("_calcul _la moyenne(_un a ; _un b) << (a + b) ÷ 2\n_afficher moyenne(4 ; 6)\n", "5\n");
    EXEC("_le total << 0\n_action ajouter(_un montant)\n    total << total + montant\n_fin\najouter(5)\najouter(2,5)\n"
         "_afficher total\n", "7,5\n");
    EXEC("_la quantité << 0\n_si _non (quantité _nul) _alors\n    _afficher 1\n_sinon\n    _afficher 2\n_fin\n", "2\n");
    EXEC("_la quantité << 2\n_afficher quantité _positif ; quantité ≥ 2 _et _vrai\n", "vrai vrai\n");
    EXEC("_pour_chaque i _de 1 _à 9 _pas 4\n    _afficher i\n_fin\n", "1\n5\n9\n");
    EXEC("_le x << 7\n_selon x\n    _cas 1 _ou 2\n        _afficher « a »\n    _cas _de 3 _à 9 _ou _négatif\n"
         "        _afficher « b »\n    _autrement\n        _afficher « c »\n_fin\n", "b\n");
    EXEC("_le x << 12\n_selon x\n    _cas > 10\n        _afficher « grand »\n_fin\n", "grand\n");
    EXEC("# une remarque\n_le frais_de_port_et_d'emballage << 7\n_afficher frais_de_port_et_d'emballage\n", "7\n");
    EXEC("_l'addition << 5\naddition << addition + 1\n_afficher addition\n", "6\n");
    EXEC("_le t << _vrai\n_si t _alors\n    _afficher t _faux\n_fin\n", "faux\n");
    EXEC("_le total << 1 +\n    2\n_afficher total\n", "ERREUR 1:16 Expression incomplète : il manque un nombre, un nom ou une parenthèse.");
    EXEC("_le total << (1 +\n    2)\n_afficher total\n", "3\n");
    EXEC("_répéter 2 _fois\n    _afficher 1\n    _sortir\n_fin\n", "1\n");
    EXEC("_le x << 5\n_afficher x ÷ 0\n", "ERREUR 2:13 Division par zéro.");

    EXEC("_classe _un client\n    _un nom\n    _un solde\n_fin\n_le c << _nouveau client _avec\n    nom << « Dupont »\n"
         "    solde << 10\n_fin\nc.solde << c.solde + 5\n_afficher c.nom ; c.solde ; c\n", "Dupont 15 un client\n");
    EXEC("_classe _un nœud\n    _un suivant\n    _une valeur\n_fin\n_le a << _nouveau nœud _avec\n    valeur << 1\n_fin\n"
         "a.suivant << _nouveau nœud _avec\n    valeur << 2\n_fin\n_afficher a.suivant.valeur\n", "2\n");

    EXEC("_classe _une personne\n    _un nom\n_fin\n_classe _un membre _est _une personne\n    _une licence\n_fin\n"
         "_classe _un invité _est _une personne\n_fin\n_le m << _nouveau membre _avec\n    nom << « Ana »\n"
         "    licence << 1\n_fin\n_afficher m.nom ; m.licence ; m\n", "Ana 1 un membre\n");
    ERR("_classe _un membre _est _une personne\n_fin\n", 1, 30, "Classe « personne » inconnue");

    EXEC("_classe _une personne\n    _un nom\n_fin\n_classe _un membre _est _une personne\n_fin\n"
         "_action saluer(_une personne)\n    _afficher 1\n_fin\n_action saluer(_un membre)\n    _afficher 2\n_fin\n"
         "_le p << _nouveau personne\n_le m << _nouveau membre\nsaluer(p)\nsaluer(m)\n", "1\n2\n");

    EXEC("_aptitude horodatée\n    _une date\n_fin\n_classe _un document _est _une chose _adopte horodatée\n_fin\n"
         "_action dater(_une chose_horodatée)\n    chose.date << 5\n_fin\n_le d << _nouveau document\ndater(d)\n"
         "_afficher d.date\n", "5\n");
    ALLER_RETOUR("_aptitude active (actif)\n    _un état\n_fin\n_classe _une personne\n    _un nom\n_fin\n"
                 "_classe _un employé _est _une personne\n_fin\n_classe _un cadre _est _un employé _adopte active\n_fin\n",
                 "Une chose active (actif) a :\n    un état.\nUne personne a :\n    un nom.\n"
                 "Un employé est une personne.\nUn cadre est un employé actif.\n");

    EXEC("_la f << 21.09.2026\n_afficher f + 30 ; _aujourd'hui = _aujourd'hui ; 01.01.2027 − f\n",
         "21.10.2026 vrai 102\n");
    ALLER_RETOUR("_le jour << 01.03.2026\n_si _aujourd'hui > jour _alors\n    _afficher jour − 01.01.2026\n_fin\n",
                 "Le jour vaut 01.03.2026.\nSi aujourd'hui > jour :\n    Afficher jour − 01.01.2026.\n");

    ALLER_RETOUR("_la photo << _fichier « a.jpg »\n_afficher (_fichier « b.png »).taille ; photo.format\n"
                 "_enregistrer photo _dans « c.jpg »\n",
                 "La photo vaut le fichier « a.jpg ».\nAfficher taille de (le fichier « b.png ») puis format de photo.\n"
                 "Enregistrer photo dans « c.jpg ».\n");
    ERR("_enregistrer photo\n", 1, 1, "Forme attendue : « _enregistrer photo _dans « copie.jpg » »");

    ALLER_RETOUR("_aptitude datée\n    _une date (date)\n_fin\n_classe _une facture (factures) _conservé\n"
                 "    _un montant (nombre)\n    _un payé (vrai_ou_faux)\n    _un numéro (nombre_entier) _unique\n_fin\n"
                 "_classe _un avoir _conservé _est _une facture _adopte datée\n    _un motif (texte)\n_fin\n",
                 "Une chose datée a :\n    une date (date).\nUne facture (factures), conservée, a :\n    un montant (nombre),\n"
                 "    un payé (vrai ou faux),\n    un numéro (nombre entier), unique.\n"
                 "Un avoir, conservé, est une facture datée.\nUn avoir a :\n    un motif (texte).\n");
    EXEC("_classe _un client _conservé\n    _un âge (nombre_entier)\n_fin\n_le c << _nouveau client _avec\n"
         "    âge << 2,5\n_fin\n", "ERREUR 5:12 Le champ « âge » attend un nombre entier, pas un nombre à virgule.");

    EXEC("_classe _un client _conservé\n    _un nom (texte) _unique\n_fin\n_le a << _nouveau client _avec\n"
         "    nom << « Ana »\n_fin\n_conserver a\n_le b << _nouveau client _avec\n    nom << « Ana »\n_fin\n_conserver b\n",
         "ERREUR 11:1 « nom » est unique : un autre client conservé a déjà « Ana ».");
    ERR("_conserver\n", 1, 1, "Forme attendue : « _conserver client »");

    EXEC("_classe _un client _conservé\n    _un nom (texte)\n    _un solde (nombre)\n_fin\n"
         "_action créer(_un nom ; _un solde)\n    _le c << _nouveau client _avec\n        nom << nom\n"
         "        solde << solde\n    _fin\n    _conserver c\n_fin\ncréer(« B » ; 10)\ncréer(« a » ; 9)\ncréer(« C » ; −1)\n"
         "_pour_chaque client _conservé _dont solde _positif _par nom _décroissant\n    _afficher client.nom\n_fin\n"
         "_afficher _nombre_de client _conservé _dont solde > 9 ; (_le client _conservé _dont nom = « a »).solde\n",
         "B\na\n1 9\n");
    ALLER_RETOUR("_classe _une facture _conservé\n    _un montant (nombre)\n_fin\n"
                 "_pour_chaque facture _conservé _dont _non (montant _nul) _par montant _décroissant\n    _afficher facture\n_fin\n"
                 "_la f << _la facture _conservé _dont montant = 3\n_afficher _nombre_de facture _conservé\n",
                 "Une facture, conservée, a :\n    un montant (nombre).\n"
                 "Pour chaque facture conservée dont le montant n'est pas nul, par montant décroissant :\n    Afficher facture.\n"
                 "La f vaut la facture conservée dont le montant = 3.\nAfficher le nombre de factures conservées.\n");

    /* --- Erreurs, aux positions du fichier compact --- */
    ERR("_le x << 1\n_afficher y\n", 2, 11, "« y » inconnu.");
    ERR("_si 1 > 0 _alors\n    _afficher 1\n", 1, 1, "« _fin » manquant : « _si », ligne 1, n'est pas fermé.");
    ERR("_afficher 1\n_fin\n", 2, 1, "« _fin » sans construction ouverte.");
    ERR("_sinon\n", 1, 1, "« _sinon » hors d'un « _si ».");
    ERR("_cas 1\n", 1, 1, "« _cas » hors d'un « _selon ».");
    ERR("_selon 1\n    _afficher 1\n_fin\n", 2, 5, "« _cas » ou « _autrement » attendu");
    ERR("_le x << 1 # note\n", 1, 12, "Une remarque commence une ligne");
    ERR("_le [x] << 1\n", 1, 5, "Pas de crochets en forme compacte");
    ERR("_si x > 0\n    _afficher 1\n_fin\n", 1, 1, "« _alors » attendu");
    ERR("_bidule\n", 1, 1, "« _bidule » ne commence pas une instruction.");
    ERR("_calcul carré(_un n) << n\n", 1, 9, "Article attendu");
    ERR("_le x << 1\nx << x _et 2\n", 2, 8, "relie deux conditions");
    ERR("_calcul _le f(_un x) << y\n", 1, 25, "« y » inconnu.");

    /* --- Allers-retours (§ 12) --- */
    ALLER_RETOUR("# exemple\n_le prix_unitaire << 12,50\n_la quantité << 3\n_l'addition << 0\n"
                 "addition << prix_unitaire × quantité\n_afficher « Total : » ; addition\n",
                 "Remarque : exemple\nLe prix unitaire vaut 12,50.\nLa quantité vaut 3.\nL'addition vaut 0.\n"
                 "L'addition devient prix unitaire × quantité.\nAfficher « Total : » puis addition.\n");
    ALLER_RETOUR("_la quantité << 3\n_si quantité _positif _et _non (quantité > 10) _alors\n    _afficher « ok »\n_fin\n",
                 "La quantité vaut 3.\nSi quantité est positive et quantité n'est pas supérieure à 10 :\n"
                 "    Afficher « ok ».\n");
    ALLER_RETOUR("_calcul _la valeur_absolue(_un nombre)\n    _si nombre _négatif _alors\n        _rendre −nombre\n"
                 "    _fin\n    _rendre nombre\n_fin\n_afficher valeur_absolue(−3)\n", NULL);
    ALLER_RETOUR("_le total << 0\n_action ajouter(_un montant ; _une remise)\n    total << total + montant − remise\n"
                 "_fin\n\najouter(5 ; 1)\n_action saluer()\n    _afficher 1\n_fin\nsaluer()\n",
                 "Le total vaut 0.\nPour ajouter un montant et une remise :\n    Le total devient total + montant − remise.\n"
                 "\nAjouter 5 et 1.\nPour saluer :\n    Afficher 1.\nSaluer.\n");
    ALLER_RETOUR("_le x << 1\n_selon x\n    _cas 1 _ou _de 2 _à 3\n        _afficher 1\n    _cas ≥ 10 _ou _négatif\n"
                 "        _afficher 2\n    _autrement\n        _afficher 3\n_fin\n", NULL);
    ALLER_RETOUR("_pour_chaque t _de 0 _à 1 _pas 0,25\n    _si t = 0,5 _alors\n        _passer\n    _fin\n"
                 "    _afficher t\n_fin\n_répéter 3 _fois\n    _sortir\n_fin\n", NULL);
    ALLER_RETOUR("_calcul _le carré(_un nombre) << nombre × nombre\n_afficher carré(3 + 1) ; carré(2) + 1 ; carré(carré(2))\n",
                 "Le carré d'un nombre vaut nombre × nombre.\n"
                 "Afficher carré de (3 + 1) puis carré de 2 + 1 puis carré de carré de 2.\n");

    ALLER_RETOUR("_classe _une facture\n    _un montant\n_fin\n_la f << _nouveau facture _avec\n    montant << 3\n_fin\n"
                 "f.montant << f.montant × 2\n_afficher f.montant\n",
                 "Une facture a :\n    un montant.\nLa f vaut une nouvelle facture :\n    Le montant vaut 3.\n"
                 "Le montant de f devient montant de f × 2.\nAfficher montant de f.\n");
    ERR("_classe _un client\n    _un nom\n", 1, 1, "« _fin » manquant");
    ALLER_RETOUR("_classe _une personne\n    _un nom\n_fin\n_classe _un membre _est _une personne\n    _une licence\n_fin\n"
                 "_classe _un invité _est _une personne\n_fin\n",
                 "Une personne a :\n    un nom.\nUn membre est une personne.\nUn membre a :\n    une licence.\n"
                 "Un invité est une personne.\n");
    ERR("_classe _un client\n_fin\n", 2, 1, "au moins un champ");

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
