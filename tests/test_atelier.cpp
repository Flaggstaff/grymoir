// GrymoiR : essais de l'atelier (docs/atelier.md, jalon A1), sans fenêtre.
#include "coloration.h"
#include "editeur.h"
#include "execution.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <csignal>
#include <cstdio>
#include <functional>

extern "C" {
#include "vm.h"
}

volatile std::sig_atomic_t grym_fermeture_demandee = 0;   // défini par principal.cpp dans l'atelier

// Exécute `source` dans la fenêtre d'exécution, sans écran. À chaque question, `repondre(numero, fenetre)`
// remplit les champs ; il rend faux pour cliquer « Annuler », vrai pour « Envoyer ». Rend le fil affiché.
static QString executer(const QString &source, const std::function<bool(int, QWidget &)> &repondre,
                        QString *etat = nullptr, int arreter_apres_ms = -1) {
    QTemporaryDir dossier;
    const QString chemin = dossier.filePath("essai.grym");
    QFile f(chemin);
    f.open(QIODevice::WriteOnly);
    f.write(source.toUtf8());
    f.close();
    Execution e(chemin);
    e.demarrer();
    QPushButton *envoyer = nullptr, *annuler = nullptr, *arreter = nullptr;
    for (QPushButton *b : e.findChildren<QPushButton *>()) {
        if (b->text() == "Envoyer") envoyer = b;
        if (b->text() == "Annuler") annuler = b;
        if (b->text() == "Arrêter") arreter = b;
    }
    QLabel *libelle_etat = nullptr;
    for (QLabel *l : e.findChildren<QLabel *>())
        if (l->text() == "En cours…") libelle_etat = l;
    QElapsedTimer montre;
    montre.start();
    int numero = 0;
    while (montre.elapsed() < 10000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (libelle_etat->text().startsWith("Terminé")) break;
        if (arreter_apres_ms >= 0 && montre.elapsed() > arreter_apres_ms && arreter->isEnabled()) arreter->click();
        if (envoyer->isEnabled()) {
            if (repondre(numero++, e)) envoyer->click();
            else annuler->click();
        }
    }
    if (etat) *etat = libelle_etat->text();
    const QString fil = e.findChild<QTextBrowser *>()->toPlainText();
    e.close();
    return fil;
}

// Les champs de saisie actifs de la question en cours, dans l'ordre.
static QList<QLineEdit *> lignes(QWidget &w) {
    QList<QLineEdit *> r;
    for (QLineEdit *l : w.findChildren<QLineEdit *>())
        if (l->isEnabled() && !qobject_cast<QComboBox *>(l->parent())) r << l;
    return r;
}

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

    // Exécution : questions, refus puis nouvelle réponse, affichage
    QString etat;
    QString fil = executer("Le nom vaut la réponse à « Nom ? ».\n"
                           "Le n vaut la réponse en nombre entier à « Combien ? ».\n"
                           "Afficher « Bonjour » puis nom puis n × 2.\n",
                           [](int k, QWidget &w) {
                               auto l = lignes(w);
                               if (l.size() != 1) return true;
                               l[0]->setText(k == 0 ? "Ana" : k == 1 ? "abc" : "3");
                               return true;
                           }, &etat);
    VERIFIER(fil.contains("Bonjour Ana 6"));
    VERIFIER(etat == "Terminé.");

    // Le refus s'affiche sous le champ, et la question revient
    int poses = 0;
    bool refus_vu = false;
    executer("Le n vaut la réponse en nombre entier à « Combien ? ».\n", [&](int k, QWidget &w) {
        poses++;
        for (QLabel *l : w.findChildren<QLabel *>()) refus_vu |= l->text().contains("n'est pas un nombre");
        lignes(w)[0]->setText(k == 0 ? "abc" : "2");
        return true;
    });
    VERIFIER(poses == 2);
    VERIFIER(refus_vu);

    // Annuler une question : l'exécution s'arrête, rien n'est écrit
    fil = executer("Afficher 1.\nLe n vaut la réponse à « Nom ? ».\n", [](int, QWidget &) { return false; }, &etat);
    VERIFIER(fil.startsWith("1"));
    VERIFIER(etat == "Terminé sur une erreur.");

    // Arrêter une boucle sans fin : l'interruption, et l'annulation
    fil = executer("Le x vaut 0.\nTant que vrai :\n    Le x devient x + 1.\n", [](int, QWidget &) { return true; }, &etat, 300);
    VERIFIER(fil.contains("Interrompu (Ctrl+C)."));
    VERIFIER(etat == "Terminé sur une erreur.");

    // Un formulaire d'entité : le lien se choisit dans un menu, le vrai ou faux aussi ; la base est à côté
    fil = executer("Un compositeur, conservé, a : un nom (texte), unique.\n"
                   "Une pièce, conservée, a : un titre (texte), un compositeur (compositeur), une édition (vrai ou faux).\n"
                   "Pour préparer :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n    Conserver c.\n"
                   "Préparer.\nLa p vaut une nouvelle pièce saisie.\nConserver p.\n"
                   "Afficher titre de p puis nom du compositeur de p puis édition de p.\n",
                   [](int, QWidget &w) {
                       lignes(w)[0]->setText("Suite");
                       auto menus = w.findChildren<QComboBox *>();
                       for (QComboBox *m : menus) {
                           if (m->findText("Bach") >= 0) m->setCurrentIndex(m->findText("Bach"));
                           if (m->findText("oui") >= 0) m->setCurrentIndex(m->findText("oui"));
                       }
                       return true;
                   }, &etat);
    VERIFIER(fil.contains("Suite Bach vrai"));
    VERIFIER(etat == "Terminé.");

    // Fichiers utilisés (grammaire, § 21) : erreur posée sur « Utiliser », fichier de déclarations reconnu,
    // exécution d'un programme qui utilise un autre fichier
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QString &texte) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::WriteOnly);
            f.write(texte.toUtf8());
        };
        ecrire("donnees.grym", "Le carré d'un n vaut n × n.\n");
        ecrire("faux.grym", "Le carré d'un n vaut n × n.\nLe cube d'un n vaut n × x.\n");
        bool declarations = false;
        Diagnostic_atelier d = analyser_source("Remarque : x.\nUtiliser « faux ».\n", false, dossier.filePath("prog.grym"));
        VERIFIER(d.ligne == 2 && d.colonne == 1);
        VERIFIER(d.message.startsWith("« ") && d.message.contains("faux.grym », ligne 2 : « x » inconnu."));
        d = analyser_source("Utiliser « donnees ».\nLe double d'un n vaut n × 2.\n", false, dossier.filePath("lib.grym"), &declarations);
        VERIFIER(d.message.isEmpty() && declarations);
        d = analyser_source("Utiliser « donnees ».\nAfficher le carré de 3.\n", false, dossier.filePath("prog.grym"), &declarations);
        VERIFIER(d.message.isEmpty() && !declarations);
        ecrire("prog.grym", "Utiliser « donnees ».\nAfficher le carré de 9.\n");
        Execution e(dossier.filePath("prog.grym"));
        e.demarrer();
        QElapsedTimer montre;
        montre.start();
        QLabel *etat_prog = nullptr;
        for (QLabel *l : e.findChildren<QLabel *>())
            if (l->text() == "En cours…") etat_prog = l;
        while (montre.elapsed() < 10000 && !etat_prog->text().startsWith("Terminé"))
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        VERIFIER(e.findChild<QTextBrowser *>()->toPlainText().contains("81"));
        e.close();
    }

    std::printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}
