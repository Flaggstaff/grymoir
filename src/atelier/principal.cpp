// GrymoiR : l'atelier (docs/atelier.md).
//   grym-atelier [projet ou fichier]      l'atelier
//   grym-atelier --lancer fichier.grym    exécute un programme dans sa propre fenêtre (bouton « Lancer »)
#include "execution.h"
#include "fabrication.h"
#include "fenetre.h"
#include "theme.h"

#include "projet.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <csignal>
#include <cstdio>

extern "C" {
#include "vm.h"
}

// L'atelier demande l'arrêt par un signal (SIGTERM, ou SIGINT) : la machine s'arrête proprement
// au prochain saut arrière ou appel, et le journal annule l'exécution (docs/vm.md, § 6).
// Venu de l'atelier (bouton « Arrêter »), le signal ferme aussi la fenêtre une fois l'exécution annulée.
volatile std::sig_atomic_t grym_fermeture_demandee = 0;

static void sur_arret(int signal_recu) {
    grym_interruption = 1;
    grym_fermeture_demandee = 1;
    std::signal(signal_recu, sur_arret);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("GrymoiR");
    QApplication::setApplicationName("Atelier");
    const QStringList args = QApplication::arguments();
    // grym-atelier --fabriquer projet destination : le menu Programme > Fabriquer l'application…, sans fenêtre,
    // pour les scripts et l'intégration continue (docs/atelier.md, § 8). Le programme principal vient de
    // projet.grymatelier.
    if (args.size() == 4 && args.at(1) == "--fabriquer") {
        FichierProjet f;
        f.lire(QFileInfo(args.at(2)).absoluteFilePath());
        if (f.programme_principal.isEmpty()) {
            std::fprintf(stderr, "Programme principal inconnu : choisis-le dans l'atelier (projet.grymatelier).\n");
            return 1;
        }
        QString erreur, remarque;
        const QString paquet = fabriquer_application(f.dossier, f.programme_principal, args.at(3), &erreur, &remarque);
        if (paquet.isEmpty()) { std::fprintf(stderr, "%s\n", qPrintable(erreur)); return 1; }
        std::printf("%s\n", qPrintable(paquet));
        if (!remarque.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(remarque));
        return 0;
    }
    // Une application fabriquée par l'atelier (docs/atelier.md, § 8) : son programme s'ouvre directement, sans
    // éditeur, sous son nom ; sa base vit dans le dossier de données que le système donne à l'application.
    QString nom, principal;
    const QString programme = args.size() == 1 ? programme_d_application(&nom, &principal) : QString();
    if (!programme.isEmpty()) {
        QApplication::setOrganizationName(QString());
        QApplication::setApplicationName(nom);
        Theme::courant().installer(app);
        const QString donnees = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(donnees);
        const QString chemin = QDir(programme).filePath(principal);
        Execution e(chemin);
        e.setWindowTitle(nom);
        e.placer_base(QDir(donnees).filePath(QFileInfo(principal).completeBaseName() + ".grymd"));
        e.show();
        e.demarrer();
        return app.exec() == 0 ? 0 : 1;
    }
    Theme::courant().installer(app);   // l'atelier et les programmes qu'il lance : même thème
    if (args.size() == 3 && args.at(1) == "--lancer") {
        std::signal(SIGINT, sur_arret);
        std::signal(SIGTERM, sur_arret);
        Execution e(args.at(2));
        e.show();
        e.demarrer();
        return app.exec() == 0 ? 0 : 1;
    }
    Fenetre f;
    if (args.size() > 1) f.ouvrir(args.at(1));
    else {
        QString d = QSettings().value("dernier projet").toString();
        if (d.isEmpty()) {
            // Premier lancement d'une application installée (GrymoiR.app) : l'exemple qu'elle embarque
            // (Contents/Resources/Exemples) est recopié dans Documents/GrymoiR, puis ouvert. Rien n'est écrasé.
            const QDir exemples(QApplication::applicationDirPath() + "/../Resources/Exemples");
            const QStringList noms = exemples.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            if (!noms.isEmpty()) {
                const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
                QString erreur;
                d = installer_exemple(exemples.filePath(noms.first()), QDir(documents).filePath("GrymoiR"), &erreur);
                if (d.isEmpty()) QMessageBox::warning(nullptr, "Atelier", erreur);
            }
        }
        if (!d.isEmpty()) f.ouvrir(d);
    }
    f.show();
    return app.exec();
}
