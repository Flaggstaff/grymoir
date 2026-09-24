// GrymoiR : essais de l'atelier (docs/atelier.md, jalon A1), sans fenêtre.
#include "coloration.h"
#include "editeur.h"

#include <QApplication>
#include <cstdio>

static int total = 0, echecs = 0;

static void verifier(int ligne, bool ok, const char *quoi) {
    total++;
    if (!ok) {
        echecs++;
        std::printf("ÉCHEC (test ligne %d) : %s\n", ligne, quoi);
    }
}
#define VERIFIER(c) verifier(__LINE__, (c), #c)

// La sorte de chaque caractère de `ligne` ; « . » pour aucune couleur, sinon la première lettre de la sorte.
static QString dessin(const QString &ligne, bool compacte = false) {
    static const char lettres[] = "cmntroNa";
    QString r;
    for (int s : Coloration::sortes(ligne, compacte)) r += s < 0 ? QChar('.') : QChar(lettres[s]);
    return r;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    // Coloration : chaque mot selon le lexeur ; les noms restent sans couleur
    VERIFIER(dessin("Le total vaut 12,50.") == "mm.......mmmm.nnnnno");
    VERIFIER(dessin("Si x > 3, afficher « a ».") == "cc...o.no.mmmmmmmm.ttttto");
    VERIFIER(dessin("Remarque : bonjour") == "rrrrrrrrrrrrrrrrrr");
    VERIFIER(dessin("La date vaut 21.09.2026.") == "mm......mmmm.nnnnnnnnnno");
    VERIFIER(dessin("Le [frais et port] vaut 1.") == "mm.NNNNNNNNNNNNNNN.mmmm.no");
    // points de code hors du plan de base : l'emoji compte pour deux unités UTF-16
    VERIFIER(dessin("Afficher « 😀 » puis 1.") == "mmmmmmmm.tttttt.mmmm.no");
    // une erreur du lexeur marque la fin de la ligne
    VERIFIER(dessin("Le x vaut 3.5.").endsWith("aaaa"));
    // forme compacte : mots-clés à souligné, noms à soulignés sans couleur
    VERIFIER(dessin("_si prix_unitaire > 3 _alors", true) == "ccc...............o.n.cccccc");

    // Analyse : la première erreur, avec sa position ; rien quand tout va bien
    Diagnostic_atelier d = analyser_source("Le total vaut 1.\nLe totl devient 2.\n", false);
    VERIFIER(d.ligne == 2);
    VERIFIER(d.message.contains("totl"));
    VERIFIER(analyser_source("Le total vaut 1.\nAfficher total.\n", false).message.isEmpty());
    VERIFIER(analyser_source("_le total << 1\n_afficher total\n", true).message.isEmpty());

    // L'éditeur analyse au chargement et garde la position de l'erreur
    Editeur e;
    e.charger("La quantité vaut 3.\nSi la quantité est positif, afficher 1.\n", false);
    VERIFIER(e.diagnostic().ligne == 2);
    VERIFIER(e.diagnostic().message.contains("positive"));
    VERIFIER(!e.document()->isModified());
    e.charger("La quantité vaut 3.\n", false);
    VERIFIER(e.diagnostic().message.isEmpty());

    std::printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}
