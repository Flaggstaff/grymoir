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
    LITT("le  TOTAL   vaut 1\u00a0000.   Afficher \"x\" puis TOTAL.\n\n\n\nsi total > 3 :\n      afficher \" a \".\n"
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
    FIXE("Le prix vaut 3.\nAfficher l'arrondi du prix × 2 à 0,05 vers le bas puis l'arrondi de 2 à (1 + 1) au pair.\n");
    FIXE("Le prix vaut 3.\nAfficher le reste du prix × 2 par (1 + 3) puis la valeur absolue de −2 puis la partie entière du prix.\n");
    FIXE("Une réunion, conservée, a :\n    un début (heure).\nLe h vaut 8:05 + 2 heures.\nAfficher le nombre d'heures entre h et 19:30 puis maintenant.\n");
    FIXE("Afficher le nombre d'années entre 21.09.1990 et aujourd'hui puis le nombre de jours entre 01.01.2026 et (01.01.2026 + 3).\n"
         "Le n vaut un nombre au hasard de 1 à 6.\n");
    FIXE("Le carré d'un nombre vaut nombre × nombre.\nExemple : le carré de 7 vaut 49.\nExemple : « a » en majuscules vaut « A ».\n");
    FIXE("Les couleurs valent « a », « b » et « c ».\nLes couleurs perdent « b ».\nLes autres valent un ensemble vide.\n"
         "Les autres deviennent les couleurs.\nAfficher le nombre d'autres puis les couleurs jointes par « ; ».\n"
         "Pour chaque c parmi les couleurs, afficher c.\n");
    FIXE("Une personne a :\n    un nom.\nPour saluer une personne :\n    Afficher 1.\nLe p vaut une nouvelle personne :\n"
         "    Le nom vaut « a ».\nSaluer p comme une personne.\n");
    FIXE("Un membre, conservé, a :\n    un nom (texte).\nExporter les membres conservés, par nom décroissant, avec le nom, dans « m.csv ».\n"
         "Exporter les membres conservés dont le nom est « a » dans « n.csv ».\n");
    LITT("Le pas vaut 1.\nAfficher arrondi de 2,5 au pas.", "Le pas vaut 1.\nAfficher arrondi de 2,5 au pas.\n");
    COMP("Le pas vaut 1.\nAfficher l'arrondi de 2,5 au pas vers le haut.", "_le pas << 1\n_afficher _arrondi_haut(2,5 ; pas)\n");
    FIXE("Le nom vaut « a ».\nSi le nom ne contient pas « b » et nom finit par « a », afficher nom en majuscules.\n");
    FIXE("Afficher (« a » suivi de « b ») en minuscules puis « 3 » en nombre + 1.\n");
    FIXE("Une tâche, conservée, a :\n    un état (« à faire », « en cours » ou « fini »), « à faire » au départ.\n");
    FIXE("Une note, conservée, hors des fiches, a :\n    un texte (texte).\n");   /* § 22.3 */
    FIXE("L'écran x montre :\n    un bouton « Nouvelle séance », principal, actif si 1 = 1.\n"
         "Quand on clique sur « Nouvelle séance » dans l'écran x :\n    Fermer l'écran.\n");   /* 2.14 */
    /* « suivi » après « de » est un mot de nom ; l'opérateur « suivi de » reste (§ 4.4) */
    FIXE("Un soin, conservé, a :\n    un tarif de suivi (nombre).\nLe s vaut un nouveau soin :\n    Le tarif de suivi vaut 70.\n"
         "Afficher le tarif de suivi du s suivi de « CHF ».\n");
    COMP("Une tâche, conservée, a : un état (« a » ou « b »).",
         "_classe _une tâche _conservé\n    _un état (« a » ; « b »)\n_fin\n");
    COMP("Le nom vaut « a ».\nAfficher nom ne commence pas par « b ».", "_le nom << « a »\n_afficher _non (nom _commence « b »)\n");
    FIXE("Un membre, conservé, a : un nom (texte), une cotisation (nombre).\nAfficher la moyenne des cotisations des membres conservés puis la plus grande cotisation des membres conservés.\n"
         "Afficher nom du membre conservé à la plus petite cotisation dont le nom est « a ».\n");
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
    /* initiale accentuée d'une action : capitale en tête de phrase, minuscule après la virgule */
    LITT("Pour étiqueter :\n    Afficher 1.\nétiqueter.\nSi vrai, Étiqueter.\nPour œuvrer :\n    Afficher 2.\nŒuvrer.",
         "Pour étiqueter :\n    Afficher 1.\nÉtiqueter.\nSi vrai, étiqueter.\nPour œuvrer :\n    Afficher 2.\nŒuvrer.\n");
    /* écrans (§ 22) : forme canonique et forme compacte */
    {
        const char *ecr = "Un compositeur, conservé, a :\n    un nom (texte), unique.\n"
                          "L'écran des compositeurs, « Nos compositeurs », montre :\n    le texte « Bonjour »,\n"
                          "    la liste des compositeurs conservés dont le nom ≠ « X », par nom décroissant,\n"
                          "    un bouton « Fermer ».\n"
                          "Quand on choisit un compositeur dans l'écran des compositeurs :\n    Afficher nom du compositeur.\n"
                          "Quand on clique sur « Fermer » dans l'écran des compositeurs :\n    Fermer l'écran.\n"
                          "Ouvrir l'écran des compositeurs.\n";
        FIXE(ecr);
        COMP(ecr, "_classe _un compositeur _conservé\n    _un nom (texte) _unique\n_fin\n"
                  "_écran des_compositeurs « Nos compositeurs »\n    _texte « Bonjour »\n"
                  "    _liste compositeur _conservé _dont nom ≠ « X » _par nom _décroissant\n    _bouton « Fermer »\n_fin\n"
                  "_quand _choisit _un compositeur _dans des_compositeurs\n    _afficher compositeur.nom\n_fin\n"
                  "_quand _clique « Fermer » _dans des_compositeurs\n    _fermer\n_fin\n"
                  "_ouvrir des_compositeurs\n");
        /* A3-b : zones, « de l'écran », changement, ouverture, fermeture */
        const char *zones = "Un compositeur, conservé, a :\n    un nom (texte), unique,\n    un pays (texte).\n"
                            "L'écran de recherche montre :\n    un pays (texte), « Suisse » au départ,\n"
                            "    une ville (texte), facultative,\n    un âge (nombre), -3 au départ,\n"
                            "    la liste des compositeurs conservés dont le pays est le pays de l'écran, par nom,\n"
                            "    un bouton « Supprimer ».\n"
                            "Quand on ouvre l'écran de recherche :\n    Le pays de l'écran devient « France ».\n"
                            "Quand on change la ville dans l'écran de recherche :\n    Afficher la ville de l'écran.\n"
                            "Quand on clique sur « Supprimer » dans l'écran de recherche :\n"
                            "    Si le compositeur choisi de l'écran est présent, supprimer le compositeur choisi de l'écran.\n"
                            "Quand on ferme l'écran de recherche :\n    Afficher « fermé ».\n"
                            "Ouvrir l'écran de recherche.\n";
        FIXE(zones);
        /* A3-c : la fiche, et un écran ouvert depuis un événement */
        const char *fiche = "Un compositeur, conservé, a :\n    un nom (texte), unique.\n"
                            "L'écran d'aide montre :\n    un bouton « OK ».\n"
                            "Quand on clique sur « OK » dans l'écran d'aide :\n    Fermer l'écran.\n"
                            "L'écran des compositeurs montre :\n    la liste des compositeurs conservés,\n"
                            "    un bouton « Aide ».\n"
                            "Quand on choisit un compositeur dans l'écran des compositeurs :\n    Ouvrir la fiche du compositeur.\n"
                            "Quand on clique sur « Aide » dans l'écran des compositeurs :\n    Ouvrir l'écran d'aide.\n"
                            "Ouvrir l'écran des compositeurs.\n";
        FIXE(fiche);
        /* A3-d : blocs et colonnes choisies */
        const char *blocs = "Un compositeur, conservé, a :\n    un nom (texte), unique,\n    une date de naissance (date), facultative.\n"
                            "Une œuvre, conservée, a :\n    un titre (texte), unique,\n    un compositeur (compositeur).\n"
                            "L'écran des œuvres, « Catalogue », montre :\n    côte à côte :\n"
                            "        la liste des compositeurs conservés, par nom,\n        l'un sous l'autre :\n"
                            "            la liste des œuvres conservées, par titre, avec le titre et la date de naissance du compositeur,\n"
                            "            le texte « Double-clic : la fiche »,\n    un bouton « Fermer ».\n"
                            "Quand on clique sur « Fermer » dans l'écran des œuvres :\n    Fermer l'écran.\n"
                            "Ouvrir l'écran des œuvres.\n";
        FIXE(blocs);

        COMP("Un point a : un x.\nLe p vaut un nouveau point.\nOuvrir la fiche de p.\n",
             "_classe _un point\n    _un x\n_fin\n_le p << _nouveau point\n_ouvrir _fiche p\n");

        COMP(zones, "_classe _un compositeur _conservé\n    _un nom (texte) _unique\n    _un pays (texte)\n_fin\n"
                    "_écran de_recherche\n    _un pays (texte) _départ « Suisse »\n    _une ville (texte) _facultatif\n"
                    "    _un âge (nombre) _départ −3\n"
                    "    _liste compositeur _conservé _dont pays = _écran.pays _par nom\n    _bouton « Supprimer »\n_fin\n"
                    "_quand _ouvre de_recherche\n    _écran.pays << « France »\n_fin\n"
                    "_quand _change ville _dans de_recherche\n    _afficher _écran.ville\n_fin\n"
                    "_quand _clique « Supprimer » _dans de_recherche\n    _si _écran.compositeur_choisi _présent _alors\n"
                    "        _supprimer _écran.compositeur_choisi\n    _fin\n_fin\n"
                    "_quand _ferme de_recherche\n    _afficher « fermé »\n_fin\n_ouvrir de_recherche\n");
        FIXE("L'écran d'accueil montre :\n    un bouton « OK ».\nQuand on clique sur « OK » dans l'écran d'accueil :\n"
             "    Fermer l'écran.\nOuvrir l'écran d'accueil.\n");
    }
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

    /* --- Fichiers (§ 15) --- */
    LITT("La photo vaut le fichier « a.jpg ».\nAfficher la taille du fichier « b.png ».\nEnregistrer la photo dans « c.jpg ».",
         "La photo vaut le fichier « a.jpg ».\nAfficher la taille du fichier « b.png ».\nEnregistrer la photo dans « c.jpg ».\n");
    COMP("La photo vaut le fichier « a.jpg ».\nAfficher la taille du fichier « b.png » puis nom de fichier de photo.\n"
         "Enregistrer la photo dans « c.jpg ».",
         "_la photo << _fichier « a.jpg »\n_afficher (_fichier « b.png »).taille ; photo.nom_de_fichier\n"
         "_enregistrer photo _dans « c.jpg »\n");

    /* --- Entités (§ 16.1) --- */
    LITT("Un cheval (chevaux), conservé, a : un nom (texte), un actif (vrai ou faux), une licence (texte), unique.\n"
         "Une facture, conservée, a : un cheval (cheval).\nUn poney, conservé, est un cheval. Un poney a : une taille (nombre).",
         "Un cheval (chevaux), conservé, a :\n    un nom (texte),\n    un actif (vrai ou faux),\n"
         "    une licence (texte), unique.\nUne facture, conservée, a :\n    un cheval (cheval).\n"
         "Un poney, conservé, est un cheval.\nUn poney a :\n    une taille (nombre).\n");
    COMP("Un cheval (chevaux), conservé, a : un nom (texte), un âge (nombre entier), une licence (texte), unique.\n"
         "Un poney, conservé, est un cheval.",
         "_classe _un cheval (chevaux) _conservé\n    _un nom (texte)\n    _un âge (nombre_entier)\n"
         "    _une licence (texte) _unique\n_fin\n_classe _un poney _conservé _est _un cheval\n_fin\n");

    LITT("Un client, conservé, a : un nom (texte).\nLe c vaut un nouveau client.\nConserver le c.\nSupprimer c.",
         "Un client, conservé, a :\n    un nom (texte).\nLe c vaut un nouveau client.\nConserver le c.\nSupprimer c.\n");
    COMP("Un client, conservé, a : un nom (texte).\nLe c vaut un nouveau client.\nConserver le c.\nSupprimer c.",
         "_classe _un client _conservé\n    _un nom (texte)\n_fin\n_le c << _nouveau client\n_conserver c\n_supprimer c\n");

    /* --- Retrouver (§ 16.4) --- */
    LITT("Une facture, conservée, a : un montant (nombre), un payé (vrai ou faux).\n"
         "Pour chaque facture conservée dont le montant > 100 et le payé est faux, par montant décroissant :\n"
         "  Afficher facture.\nLa f vaut la facture conservée dont le montant est 3.\n"
         "Afficher le nombre de factures conservées dont montant n'est pas nul.",
         "Une facture, conservée, a :\n    un montant (nombre),\n    un payé (vrai ou faux).\n"
         "Pour chaque facture conservée dont le montant > 100 et le payé est faux, par montant décroissant :\n"
         "    Afficher facture.\nLa f vaut la facture conservée dont le montant est 3.\n"
         "Afficher le nombre de factures conservées dont le montant n'est pas nul.\n");
    COMP("Une facture, conservée, a : un montant (nombre).\nUn avoir (avoirs), conservé, a : un motif (texte).\n"
         "Pour chaque facture conservée dont le montant est positif, par montant, afficher facture.\n"
         "L'a vaut l'avoir conservé dont le motif est « x ».\nAfficher le nombre d'avoirs conservés.",
         "_classe _une facture _conservé\n    _un montant (nombre)\n_fin\n_classe _un avoir (avoirs) _conservé\n"
         "    _un motif (texte)\n_fin\n_pour_chaque facture _conservé _dont montant _positif _par montant\n"
         "    _afficher facture\n_fin\n_l'a << _l'avoir _conservé _dont motif = « x »\n"
         "_afficher _nombre_de avoir _conservé\n");

    COMP("Un client, conservé, a : un pays (texte), « Suisse » au départ, un code (nombre), unique, −1 au départ, "
         "un actif (vrai ou faux), vrai au départ.",
         "_classe _un client _conservé\n    _un pays (texte) _départ « Suisse »\n    _un code (nombre) _unique _départ −1\n"
         "    _un actif (vrai_ou_faux) _départ _vrai\n_fin\n");

    /* --- Champs facultatifs (§ 16.9) --- */
    LITT("Une p, conservée, a : une date (date), facultative, un nom (texte), unique, facultatif.\nLa x vaut une nouvelle p :\n"
         "    La date vaut absente.\nSi la date de x est présente, afficher 1.",
         "Une p, conservée, a :\n    une date (date), facultative,\n    un nom (texte), unique, facultatif.\n"
         "La x vaut une nouvelle p :\n    La date vaut absente.\nSi la date de x est présente, afficher 1.\n");
    /* états des éléments d'écran (§ 22.6) : ordre canonique, accord de « active », forme compacte */
    LITT("L'écran x montre :\n    une note (texte), active si vrai, sur plusieurs lignes, facultative,\n"
         "    un bouton « B », visible si faux, actif si vrai.\nQuand on clique sur « B » dans l'écran x :\n    Fermer l'écran.",
         "L'écran x montre :\n    une note (texte), facultative, sur plusieurs lignes, active si vrai,\n"
         "    un bouton « B », actif si vrai, visible si faux.\nQuand on clique sur « B » dans l'écran x :\n    Fermer l'écran.\n");
    COMP("L'écran x montre :\n    un total (nombre), 0 au départ, en lecture seule,\n    le texte « T », visible si vrai.",
         "_écran x\n    _un total (nombre) _départ 0 _lecture_seule\n    _texte « T » _visible_si _vrai\n_fin\n");
    FIXE("L'écran x montre :\n    une note (texte), sur plusieurs lignes, active si vrai,\n    le texte « T », visible si faux.");
    /* groupe titré et valeurs permises d'une zone (§ 22.7), dans les deux formes */
    COMP("L'écran x montre :\n    le groupe « Statut » :\n        un état (« a », « b » ou « c »), « a » au départ,\n    le texte « t ».",
         "_écran x\n    _groupe « Statut »\n        _un état (« a » ; « b » ; « c ») _départ « a »\n    _fin\n    _texte « t »\n_fin\n");
    FIXE("L'écran x montre :\n    le groupe « Statut » :\n        un état (« a », « b » ou « c »), « a » au départ,\n    le texte « t ».");
    /* courriel, téléphone, adresse web (§ 16.1) : dans une entité et un écran, les deux formes */
    FIXE("Un contact, conservé, a :\n    un courriel (courriel), unique,\n    un portable (téléphone),\n    un site (adresse web).\n"
         "L'écran x montre :\n    un courriel (courriel),\n    un site (adresse web).");
    COMP("L'écran x montre :\n    un site (adresse web),\n    le texte « t ».", "_écran x\n    _un site (adresse_web)\n    _texte « t »\n_fin\n");
    /* la transcription d'une zone (§ 15.8) : les deux formes */
    FIXE("L'écran x montre :\n    un mémo (son),\n    une transcription (texte), sur plusieurs lignes, facultative, transcrite du mémo.");
    COMP("L'écran x montre :\n    un mémo (son),\n    une note (texte), transcrite du mémo.",
         "_écran x\n    _un mémo (son)\n    _une note (texte) _transcrit mémo\n_fin\n");
    /* la transcription (§ 15.8) : accordée, contractée ; les deux formes */
    FIXE("Une visite, conservée, a :\n    un mémo (son),\n    une note (texte), transcrite du mémo,\n"
         "    un enregistrement (son),\n    un résumé (texte), facultatif, transcrit de l'enregistrement.");
    COMP("Une visite, conservée, a :\n    un mémo (son),\n    une note (texte), transcrite du mémo.",
         "_classe _une visite _conservé\n    _un mémo (son)\n    _une note (texte) _transcrit mémo\n_fin\n");
    /* les rapports (§ 15.6) : titre, totaux ; les deux formes */
    FIXE("Un membre, conservé, a :\n    un nom (texte),\n    une cotisation (nombre),\n    un don (nombre).\n"
         "Exporter les membres conservés, par nom, avec le nom, la cotisation et le don, avec le titre « Cotisations », "
         "avec le total de la cotisation et du don, dans « m.pdf ».");
    COMP("Un membre, conservé, a :\n    un nom (texte),\n    une cotisation (nombre).\n"
         "Exporter les membres conservés, avec le nom et la cotisation, avec le titre « C », avec le total de la cotisation, dans « m.pdf ».",
         "_classe _un membre _conservé\n    _un nom (texte)\n    _une cotisation (nombre)\n_fin\n"
         "_exporter membre _conservé _avec nom ; cotisation _titre « C » _total cotisation _dans « m.pdf »\n");
    FIXE("Un membre, conservé, a :\n    une cotisation (nombre).\nExporter les membres conservés, avec le titre « Tous », dans « m.html ».");
    /* les dates (§ 14.8, § 14.6) : parties, toutes lettres, jours ouvrables ; les deux formes */
    FIXE("Le d vaut 16.10.2026.\nAfficher le jour de la semaine de d puis le mois de d.\nAfficher d en toutes lettres.\n"
         "Afficher le nombre de jours ouvrables entre d et (d + 10).");
    COMP("Le d vaut 16.10.2026.\nAfficher le nombre de jours ouvrables entre d et d.\nAfficher d en toutes lettres.",
         "_le d << 16.10.2026\n_afficher _écart_ouvrables(d ; d)\n_afficher d _en_toutes_lettres\n");
    FIXE("Une tâche, conservée, a :\n    une échéance (date), facultative.\n"
         "L'écran x montre :\n    le graphique des tâches conservées, par mois de l'échéance.");
    /* la période se garde au formatage : un point fixe ne le voyait pas, elle disparaissait dès la première fois */
    LITT("Une tâche, conservée, a :\n    une échéance (date),\n    un départ (date),\n    une date de fin (date).\n"
         "L'écran x montre :\n    le graphique des tâches conservées, par mois de l'échéance,\n"
         "    le graphique des tâches conservées, par semaine du départ, en secteurs,\n"
         "    le graphique des tâches conservées, par année de la date de fin.",
         "Une tâche, conservée, a :\n    une échéance (date),\n    un départ (date),\n    une date de fin (date).\n"
         "L'écran x montre :\n    le graphique des tâches conservées, par mois de l'échéance,\n"
         "    le graphique des tâches conservées, par semaine du départ, en secteurs,\n"
         "    le graphique des tâches conservées, par année de la date de fin.\n");
    COMP("Une tâche, conservée, a :\n    une échéance (date).\nL'écran x montre :\n    le graphique des tâches conservées, par semaine de l'échéance.",
         "_classe _une tâche _conservé\n    _une échéance (date)\n_fin\n_écran x\n    _graphique tâche _conservé _par _semaine échéance\n_fin\n");
    /* les mots de passe (§ 16.1) : le type, « correspond au », les deux formes */
    FIXE("Un membre, conservé, a :\n    un nom (texte), unique,\n    un code (mot de passe).\n"
         "Pour chaque membre conservé :\n    Si « x » correspond au code du membre, afficher « ok ».");
    COMP("Le c vaut « x ».\nSi « x » correspond à c, afficher 1.", "_le c << « x »\n_si « x » _correspond c _alors\n    _afficher 1\n_fin\n");
    /* les étiquettes (§ 16.15) : une couleur par valeur permise, certaines sans ; les deux formes */
    FIXE("Une cotisation, conservée, a :\n    un état (« payée » en vert, « en retard » en rouge, « en attente » ou « annulée » en gris).");
    COMP("Une cotisation, conservée, a :\n    un état (« payée » en vert ou « annulée »).",
         "_classe _une cotisation _conservé\n    _un état (« payée » _vert ; « annulée »)\n_fin\n");
    /* l'unité (§ 22.6) : derrière par défaut, devant sur demande ; les deux formes */
    FIXE("L'écran x montre :\n    une remise (nombre), de 0 à 100, avec l'unité « % »,\n    un montant (nombre), avec l'unité « CHF » devant.");
    COMP("L'écran x montre :\n    un montant (nombre), avec l'unité « CHF » devant,\n    le texte « t ».",
         "_écran x\n    _un montant (nombre) _unité « CHF » _devant\n    _texte « t »\n_fin\n");
    /* le masque (§ 22.6) : accordé au genre, les deux formes */
    FIXE("L'écran x montre :\n    un code (texte), masqué,\n    une clé (texte), au moins 8 caractères, masquée.");
    COMP("L'écran x montre :\n    une clé (texte), masquée,\n    le texte « t ».",
         "_écran x\n    _une clé (texte) _masqué\n    _texte « t »\n_fin\n");
    /* les bornes (§ 22.6) : les deux formes ; « de 4 caractères » pour deux bornes égales d'un texte */
    FIXE("L'écran x montre :\n    une quantité (nombre), de 1 à 99,\n    une remise (nombre), au moins −3,5, au plus 50,\n"
         "    un code (texte), de 4 caractères,\n    un nom (texte), de 2 à 30 caractères,\n    un mot (texte), au plus 200 caractères.");
    COMP("L'écran x montre :\n    une quantité (nombre), de 1 à 99,\n    un code (texte), de 4 caractères.",
         "_écran x\n    _une quantité (nombre) _min 1 _max 99\n    _un code (texte) _min 4 _max 4\n_fin\n");
    /* la recherche (§ 22.6) : les deux formes */
    FIXE("Un membre, conservé, a :\n    un nom (texte), unique.\n" "L'écran x montre :\n    un membre (membre), avec recherche,\n    le texte « t ».");
    COMP("Un membre, conservé, a :\n    un nom (texte), unique.\n" "L'écran x montre :\n    un membre (membre), avec recherche,\n    le texte « t ».",
         "_classe _un membre _conservé\n    _un nom (texte) _unique\n_fin\n_écran x\n    _un membre (membre) _recherche\n    _texte « t »\n_fin\n");
    /* l'indication (§ 22.6) : les deux formes ; ses lettres ne se prennent pas pour des options */
    FIXE("L'écran x montre :\n    un code (texte), facultatif, avec l'indication « 4 chiffres, sans espace »,\n    le texte « t ».");
    COMP("L'écran x montre :\n    un code (texte), avec l'indication « mois »,\n    le texte « t ».",
         "_écran x\n    _un code (texte) _indication « mois »\n    _texte « t »\n_fin\n");
    /* la taille du titre (§ 22.5) : avec et sans titre donné, les deux formes */
    FIXE("L'écran de recherche, « Recherche » en grand, montre :\n    le texte « t ».");
    FIXE("L'écran de recherche, en petit, montre :\n    le texte « t ».");
    COMP("L'écran x, « Bonjour » en grand, montre :\n    le texte « t ».", "_écran x « Bonjour » _en_grand\n    _texte « t »\n_fin\n");
    /* choix multiples (§ 22.1) : les deux formes */
    FIXE("Un genre, conservé, a :\n    un nom (texte), unique.\n" "L'écran x montre :\n    des genres (genre), visible si vrai,\n    le texte « t ».");
    COMP("Un genre, conservé, a :\n    un nom (texte), unique.\n" "L'écran x montre :\n    des genres (genre),\n    le texte « t ».",
         "_classe _un genre _conservé\n    _un nom (texte) _unique\n_fin\n_écran x\n    _des genres (genre)\n    _texte « t »\n_fin\n");
    /* interrupteur (§ 22.6) : les deux formes */
    COMP("L'écran x montre :\n    un actif (vrai ou faux), en interrupteur,\n    le texte « t ».",
         "_écran x\n    _un actif (vrai_ou_faux) _interrupteur\n    _texte « t »\n_fin\n");
    FIXE("L'écran x montre :\n    un actif (vrai ou faux), faux au départ, en interrupteur, visible si vrai,\n    le texte « t ».");
    /* menu (§ 22.12) : les deux formes */
    FIXE("L'écran x montre :\n    le menu « Membres » :\n        un bouton « Nouveau membre »,\n        un bouton « Liste »,\n"
         "    le texte « t ».\nQuand on clique sur « Nouveau membre » dans l'écran x :\n    Fermer l'écran.\n"
         "Quand on clique sur « Liste » dans l'écran x :\n    Fermer l'écran.");
    COMP("L'écran x montre :\n    le menu « M » :\n        un bouton « B »,\n    le texte « t ».\n"
         "Quand on clique sur « B » dans l'écran x :\n    Fermer l'écran.",
         "_écran x\n    _menu « M »\n        _bouton « B »\n    _fin\n    _texte « t »\n_fin\n_quand _clique « B » _dans x\n    _fermer\n_fin\n");
    /* graphique (§ 22.11) : les deux formes, la clé sans article, « du montant » recontracté */
    FIXE("Un état, conservé, a :\n    un nom (texte), unique.\nUne catégorie, conservée, a :\n    un nom (texte), unique.\n"
      "Un membre, conservé, a :\n    un nom (texte),\n    une catégorie (catégorie).\n"
      "Une cotisation, conservée, a :\n    un membre (membre),\n    un montant (nombre).\n"
      "Une tâche, conservée, a :\n    un titre (texte),\n    un état (état), facultatif.\n"
         "L'écran x montre :\n    le graphique des tâches conservées, par état,\n"
         "    le graphique des cotisations conservées, par catégorie du membre, avec la somme du montant, en secteurs.");
    COMP("Une tâche, conservée, a :\n    un titre (texte),\n    un état (texte).\nL'écran x montre :\n    le graphique des tâches conservées, par état.",
         "_classe _une tâche _conservé\n    _un titre (texte)\n    _un état (texte)\n_fin\n_écran x\n    _graphique tâche _conservé _par état\n_fin\n");
    /* image calculée (§ 22.8) : la contraction « du », la forme compacte */
    FIXE("Un compositeur, conservé, a :\n    un nom (texte),\n    un portrait (image), facultatif.\n"
         "L'écran x montre :\n    la liste des compositeurs conservés,\n"
         "    l'image du portrait du compositeur choisi de l'écran, visible si le compositeur choisi de l'écran est présent.");
    COMP("L'écran x montre :\n    l'image du fichier « exemples/pixel.png »,\n    le texte « t ».",
         "_écran x\n    _image_de _fichier « exemples/pixel.png »\n    _texte « t »\n_fin\n");
    /* position libre (§ 22.9) : la place après les options, avant les conditions ; la liste après ses colonnes */
    LITT("L'écran x montre :\n    en position libre :\n        une note (texte), visible si vrai, haute de 40, à 8 de la gauche et 4 du haut,"
         " en lecture seule, large de 120,\n    le texte « t ».",
         "L'écran x montre :\n    en position libre :\n        une note (texte), en lecture seule, à 8 de la gauche et 4 du haut,"
         " large de 120, haute de 40, visible si vrai,\n    le texte « t ».\n");
    COMP("Une p, conservée, a :\n    un nom (texte).\nL'écran x montre :\n    en position libre :\n"
         "        la liste des ps conservées, par nom, à 0 de la gauche et 0 du haut, large de 300,\n"
         "        un bouton « B », à 310 de la gauche et 0 du haut,\n    le texte « t ».\n"
         "Quand on clique sur « B » dans l'écran x :\n    Fermer l'écran.",
         "_classe _une p _conservé\n    _un nom (texte)\n_fin\n_écran x\n    _position_libre\n"
         "        _liste p _conservé _par nom _place 0 0 _largeur 300\n        _bouton « B » _place 310 0\n    _fin\n"
         "    _texte « t »\n_fin\n_quand _clique « B » _dans x\n    _fermer\n_fin\n");
    FIXE("L'écran x montre :\n    en position libre :\n        l'image « exemples/pixel.png », à 0 de la gauche et 0 du haut, haute de 80,\n"
         "        un bouton « B », à 0 de la gauche et 90 du haut, haut de 30,\n    le texte « t ».\n"
         "Quand on clique sur « B » dans l'écran x :\n    Fermer l'écran.");
    COMP("L'écran x montre :\n    en position libre :\n        le groupe « G », à 8 de la gauche et 0 du haut, large de 200 :\n"
         "            le texte « a »,\n    le texte « t ».",
         "_écran x\n    _position_libre\n        _groupe « G » _place 8 0 _largeur 200\n            _texte « a »\n        _fin\n"
         "    _fin\n    _texte « t »\n_fin\n");
    COMP("L'écran x montre :\n    les onglets :\n        l'onglet « A » :\n            le texte « a »,\n"
         "        l'onglet « B » :\n            le texte « b »,\n    le texte « t ».",
         "_écran x\n    _onglets\n        _onglet « A »\n            _texte « a »\n        _fin\n        _onglet « B »\n"
         "            _texte « b »\n        _fin\n    _fin\n    _texte « t »\n_fin\n");
    FIXE("L'écran x montre :\n    en position libre :\n        les onglets, à 8 de la gauche et 0 du haut, large de 400 :\n"
         "            l'onglet « A » :\n                le texte « a »,\n    le texte « t ».");
    FIXE("L'écran x montre :\n    en position libre :\n        côte à côte, à 0 de la gauche et 90 du haut, haut de 40 :\n"
         "            le texte « c »,\n            le texte « d »,\n    le texte « t ».");
    COMP("L'écran x montre :\n    l'image « exemples/pixel.png », visible si vrai,\n    le texte « t ».",
         "_écran x\n    _image « exemples/pixel.png » _visible_si _vrai\n    _texte « t »\n_fin\n");
    FIXE("L'écran x montre :\n    l'image « exemples/pixel.png », visible si vrai,\n    le texte « t ».");
    /* une condition cite une zone écrite plus bas : l'accord suit son genre */
    LITT("L'écran x montre :\n    le texte « T », visible si la remarque de l'écran est présente,\n    une remarque (texte), facultative.",
         "L'écran x montre :\n    le texte « T », visible si la remarque de l'écran est présente,\n    une remarque (texte), facultative.\n");
    /* une colonne de tableau est féminine : « est présente » (§ 15.5) */
    LITT("Pour chaque rangée du tableau « o.csv » :\n    Si la colonne « Année » de la rangée est présente, afficher 1.",
         "Pour chaque rangée du tableau « o.csv » :\n    Si la colonne « Année » de la rangée est présente, afficher 1.\n");
    COMP("Une p, conservée, a : une date (date), facultative.\nLa x vaut une nouvelle p :\n    La date vaut absente.\n"
         "Si la date de x est présente, afficher 1.",
         "_classe _une p _conservé\n    _une date (date) _facultatif\n_fin\n_la x << _nouveau p _avec\n    date << _absent\n"
         "_fin\n_si x.date _présent _alors\n    _afficher 1\n_fin\n");

    /* --- Relations inverses (§ 16.10) --- */
    LITT("Une personne, conservée, a : un nom (texte).\nUne chanson, conservée, a : un titre (texte), "
         "un auteur (personne).\nLa p vaut la personne conservée dont le nom est « a ».\n"
         "Pour chaque chanson de p, afficher chanson.\nAfficher le nombre de chansons conservées dont p n'est pas l'auteur.",
         "Une personne, conservée, a :\n    un nom (texte).\nUne chanson, conservée, a :\n    un titre (texte),\n"
         "    un auteur (personne).\nLa p vaut la personne conservée dont le nom est « a ».\n"
         "Pour chaque chanson de p, afficher chanson.\nAfficher le nombre de chansons conservées dont p n'est pas l'auteur.\n");
    COMP("Une personne, conservée, a : un nom (texte).\nUne chanson, conservée, a : un titre (texte), "
         "un auteur (personne).\nLa p vaut la personne conservée dont le nom est « a ».\n"
         "Pour chaque chanson de p dont le titre > « a », par titre, afficher chanson.\n"
         "Afficher le nombre de chansons de p puis le nombre de chansons conservées dont p est l'auteur.",
         "_classe _une personne _conservé\n    _un nom (texte)\n_fin\n_classe _une chanson _conservé\n    _un titre (texte)\n"
         "    _un auteur (personne)\n_fin\n_la p << _la personne _conservé _dont nom = « a »\n"
         "_pour_chaque chanson _de p _dont titre > « a » _par titre\n    _afficher chanson\n_fin\n"
         "_afficher _nombre_de chanson _de p ; _nombre_de chanson _conservé _dont auteur = p\n");

    /* --- Corbeille et cascade (§ 16.12) --- */
    COMP("Une partition, conservée, a : un titre (texte).\nUn pupitre, conservé, a : une partition (partition), "
         "et disparaît avec elle.\nLa p vaut la partition supprimée dont le titre est « a ».\nRétablir p.\n"
         "Supprimer p définitivement.\nAfficher le nombre de pupitres supprimés.",
         "_classe _une partition _conservé\n    _un titre (texte)\n_fin\n_classe _un pupitre _conservé\n"
         "    _une partition (partition) _disparaît_avec\n_fin\n_la p << _la partition _supprimé _dont titre = « a »\n"
         "_rétablir p\n_supprimer p _définitivement\n_afficher _nombre_de pupitre _supprimé\n");
    LITT("Une partition, conservée, a : un titre (texte).\nUn pupitre, conservé, a : une partition (partition), "
         "et disparaît avec elle.\nPour chaque partition supprimée, afficher partition.",
         "Une partition, conservée, a :\n    un titre (texte).\nUn pupitre, conservé, a :\n"
         "    une partition (partition), et disparaît avec elle.\nPour chaque partition supprimée, afficher partition.\n");

    /* --- Plusieurs vers plusieurs (§ 16.13) --- */
#define MUI "Une personne, conservée, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), " \
            "des interprètes (personne), des travaux (travail) (personne).\nLa o vaut l'œuvre conservée dont le titre est « a ».\n"
    LITT(MUI "Les interprètes de la o gagnent o.\nLes travaux de o perdent o.\n"
         "Pour chaque interprète de o dont le nom > « a », par nom décroissant, afficher interprète.\n"
         "Afficher le nombre de travaux de o puis le nombre d'œuvres conservées dont o n'est pas parmi les interprètes.",
         "Une personne, conservée, a :\n    un nom (texte).\nUne œuvre, conservée, a :\n    un titre (texte),\n"
         "    des interprètes (personne),\n    des travaux (travail) (personne).\n"
         "La o vaut l'œuvre conservée dont le titre est « a ».\nLes interprètes de la o gagnent o.\nLes travaux d'o perdent o.\n"
         "Pour chaque interprète d'o dont le nom > « a », par nom décroissant, afficher interprète.\n"
         "Afficher le nombre de travaux d'o puis le nombre d'œuvres conservées dont o n'est pas parmi les interprètes.\n");
    COMP(MUI "Les interprètes de o gagnent o.\nLes travaux de o perdent o.\nPour chaque travail de o, afficher travail.\n"
         "Afficher le nombre de travaux de o puis le nombre d'œuvres conservées dont o est parmi les interprètes "
         "puis le nombre d'œuvres conservées dont o n'est pas parmi les travaux.",
         "_classe _une personne _conservé\n    _un nom (texte)\n_fin\n_classe _une œuvre _conservé\n    _un titre (texte)\n"
         "    _des interprètes (personne)\n    _des travaux (travail) (personne)\n_fin\n"
         "_la o << _l'œuvre _conservé _dont titre = « a »\no.interprètes _gagne o\no.travaux _perd o\n"
         "_pour_chaque travail _de o\n    _afficher travail\n_fin\n"
         "_afficher _nombre_de travail _de o ; _nombre_de œuvre _conservé _dont interprètes _contient o ; "
         "_nombre_de œuvre _conservé _dont _non (travaux _contient o)\n");

    /* --- Années (§ 14.5) --- */
    COMP("Une œuvre, conservée, a : une composition (année), facultative, une reprise (année), 1900 au départ.\n"
         "Le d vaut 01.01.2000.\nAfficher l'année de d puis l'année d'aujourd'hui.",
         "_classe _une œuvre _conservé\n    _une composition (année) _facultatif\n    _une reprise (année) _départ 1900\n_fin\n"
         "_le d << 01.01.2000\n_afficher d.année ; (_aujourd'hui).année\n");

    /* --- Séparateur de milliers : l'auteur décide de grouper, le style est normalisé (§ 12) --- */
    LITT("Le x vaut 1747.\nLe y vaut 1'000.\nLe z vaut 1\u00a0234\u00a0567,50 + 12345,5 − 0,001.",
         "Le x vaut 1747.\nLe y vaut 1'000.\nLe z vaut 1'234'567,50 + 12345,5 − 0,001.\n");
    COMP("Le x vaut 1747 + 1'000.", "_le x << 1747 + 1'000\n");
    FIXE("Le x vaut 1747.\nLe y vaut 1'000.\n");

    /* --- Contraction « du … au … » dans Pour chaque (§ 12) --- */
    FIXE("Pour chaque jour du 05.10.2026 au 02.11.2026 par pas de 7, afficher jour.\n");
    FIXE("Le début vaut 1.\nLa fin vaut 9.\nPour chaque i du début à la fin, afficher i.\n");
    LITT("Pour chaque mois de 1 à 12, afficher mois.", "Pour chaque mois de 1 à 12, afficher mois.\n");
    COMP("Pour chaque jour du 05.10.2026 au 02.11.2026, afficher jour.",
         "_pour_chaque jour _de 05.10.2026 _à 02.11.2026\n    _afficher jour\n_fin\n");

    /* --- Questions à l'utilisateur (§ 17) --- */
    FIXE("Le nom vaut la réponse à « Nom ? ».\nL'âge vaut la réponse en nombre entier à « Âge ? ».\n"
         "La q vaut « ? ».\nLe x vaut la réponse en vrai ou faux à (q).\n");
    COMP("Le nom vaut la réponse à « Nom ? ».\nL'âge vaut la réponse en nombre entier à « Âge ? ».",
         "_le nom << _réponse « Nom ? »\n_l'âge << _réponse (nombre_entier) « Âge ? »\n");

    /* --- Mise en forme (§ 4.1, § 4.2) --- */
    FIXE("Les nombres s'affichent à la française.\nLe x vaut « ab » sur 4 à droite.\n"
         "Afficher x sur 10 puis 3 sur 4 à gauche, sans passer à la ligne.\n");
    COMP("Les nombres s'affichent sans séparateur.\nAfficher « ab » sur 4 à droite, sans passer à la ligne.",
         "_style _sans_séparateur\n_afficher « ab » _sur 4 _droite _sans_ligne\n");

    /* --- Une remarque au premier niveau après un Selon reste au premier niveau, avec sa ligne vide --- */
    FIXE("Pour f un x :\n    Selon x :\n        Cas 1 :\n            Afficher 1.\n        Autrement :\n            Afficher 2.\n"
         "\nRemarque : au premier niveau.\nF 1.\n");
    FIXE("Selon 1 :\n    Cas 1, afficher 1.\n    Remarque : entre deux cas.\n    Autrement, afficher 2.\n");

    /* --- La somme (§ 16.4) ; un suffixe après « conservés » se relit en forme compacte --- */
    FIXE("Un pupitre, conservé, a :\n    un prix (nombre),\n    un nombre de parties (nombre entier).\n"
         "Afficher la somme des prix des pupitres conservés dont le prix > 0.\n"
         "Afficher la somme des nombres de parties des pupitres conservés.\n");
    COMP("Un pupitre, conservé, a :\n    un prix (nombre).\nAfficher la somme des prix des pupitres conservés.",
         "_classe _un pupitre _conservé\n    _un prix (nombre)\n_fin\n_afficher _somme_de prix _de pupitre _conservé\n");
    COMP("Un pupitre, conservé, a :\n    un prix (nombre).\nSi le nombre de pupitres conservés est nul, afficher 1.",
         "_classe _un pupitre _conservé\n    _un prix (nombre)\n_fin\n_si _nombre_de pupitre _conservé _nul _alors\n    _afficher 1\n_fin\n");

    /* --- Un chemin à travers les liens, dans dont (§ 16.4) : littéraire et compacte --- */
    FIXE("Une écriture, conservée, a :\n    une comptabilisation (date), facultative.\nUne ligne, conservée, a :\n"
         "    une écriture (écriture),\n    un débit (nombre).\n"
         "Afficher la somme des débits des lignes conservées dont la comptabilisation de l'écriture est présente.\n");
    COMP("Une écriture, conservée, a :\n    une comptabilisation (date), facultative.\nUne ligne, conservée, a :\n"
         "    une écriture (écriture).\nAfficher le nombre de lignes conservées dont la comptabilisation de l'écriture est absente.",
         "_classe _une écriture _conservé\n    _une comptabilisation (date) _facultatif\n_fin\n"
         "_classe _une ligne _conservé\n    _une écriture (écriture)\n_fin\n"
         "_afficher _nombre_de ligne _conservé _dont écriture.comptabilisation _absent\n");

    /* --- Contrainte d'entité (§ 16.14) : trois lignes en littéraire, une en compacte --- */
    FIXE("Un stock, conservé, a :\n    une quantité (nombre),\n    un nom (texte).\n"
         "Chaque stock conservé dont le nom ≠ « x » vérifie :\n    la quantité du stock ≥ 0,\n    sinon « Stock » puis le nom du stock puis « négatif. ».\n");
    COMP("Un stock, conservé, a :\n    une quantité (nombre).\nChaque stock conservé vérifie :\n    la quantité du stock ≥ 0,\n    sinon « Négatif. ».",
         "_classe _un stock _conservé\n    _une quantité (nombre)\n_fin\n_chaque stock _conservé _vérifie stock.quantité ≥ 0 _sinon « Négatif. »\n");

    /* --- Refuser (§ 18.1) : un élément, plusieurs reliés par « puis », en forme courte --- */
    FIXE("Refuser « Non. ».\n");
    FIXE("Le d vaut 3.\nSi d > 2, refuser « Trop : » puis d puis « sur 2 ».\n");
    COMP("Refuser « Débits : » puis 120 puis « crédits : » puis 100.", "_refuser « Débits : » ; 120 ; « crédits : » ; 100\n");

    /* --- Texte vide : « », une seule espace, quelle que soit l'écriture d'origine --- */
    FIXE("Afficher « ».\nLe t vaut « ».\n");
    COMP("Afficher \"\".", "_afficher « »\n");
    COMP("Le t vaut «  ».", "_le t << « »\n");

    /* --- Apparence des écrans (§ 22.5) : couleur seule, logo seul, les deux --- */
    FIXE("Les écrans ont la couleur verte et le logo « exemples/pixel.png ».\n");
    FIXE("Les écrans ont le logo « exemples/pixel.png ».\n");
    /* 2.13 : des virgules entre les réglages, « et » devant le dernier ; la forme compacte, une clé par réglage */
    FIXE("Les écrans ont la couleur « #46433B », le fond « #E8E2D6 », la police "
         "« vendor/atkinson/AtkinsonHyperlegibleNext-Regular.ttf » et le logo « vendor/lucide/check.svg ».\n");
    FIXE("Les écrans ont la couleur verte et le fond « #E8E2D6 ».\n");
    COMP("Les écrans ont la couleur « #46433B », le fond « #E8E2D6 » et le logo « vendor/lucide/check.svg ».",
         "_écrans _couleur « #46433B » _fond « #E8E2D6 » _logo « vendor/lucide/check.svg »\n");
    COMP("Les écrans ont la couleur turquoise.", "_écrans _couleur _turquoise\n");
    COMP("Les écrans ont la couleur grise et le logo « exemples/pixel.png ».",
         "_écrans _couleur _grise _logo « exemples/pixel.png »\n");

    FIXE("Effacer l'écran.\nAfficher « a ».\n");
    COMP("Effacer l'écran.", "_effacer\n");
    FIXE("L'année vaut 1.\nLe nom vaut « a ».\nAfficher « ( » suivi de année suivi de « ) » puis de nom puis que nom.\n");
    FIXE("Le nom vaut « a ».\nL'âge vaut 3.\nLa date vaut 01.01.2000.\n"
         "Le t vaut « n° » suivi du nom suivi de l'âge suivi de la date.\n");
    COMP("Le nom vaut « a ».\nAfficher « ( » suivi de 1 puis de nom.",
         "_le nom << « a »\n_afficher « ( » _suivi 1 ; _de nom\n");
    FIXE("Un client, conservé, a :\n    un nom (texte).\nPour f un client :\n    Saisir à nouveau le client.\n");
    COMP("Un client, conservé, a :\n    un nom (texte).\nPour f un client :\n    Saisir à nouveau client.\n",
         "_classe _un client _conservé\n    _un nom (texte)\n_fin\n_action f(_un client)\n    _saisir client\n_fin\n");
    FIXE("Un point a :\n    un x.\nLe p vaut un nouveau point.\nAfficher la fiche de p.\nAfficher la fiche du p.\n");
    FIXE("Essayer :\n    Afficher 1.\nEn cas d'échec, afficher le motif de l'échec.\n");
    FIXE("Essayer :\n    Afficher 1.\nEn cas d'échec :\n    Afficher « a ».\n    Afficher le motif de l'échec.\n");
    COMP("Essayer :\n    Afficher 1.\nEn cas d'échec, afficher le motif de l'échec.",
         "_essayer\n    _afficher 1\n_échec\n    _afficher _motif\n_fin\n");
    FIXE("Un client, conservé, a :\n    un nom (texte).\nLe c vaut un nouveau client saisi.\n"
         "Le d vaut un nouveau client saisi :\n    Le nom vaut « x ».\n");

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
