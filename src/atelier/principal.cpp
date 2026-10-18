// GrymoiR : l'atelier (docs/atelier.md).
//   grym-atelier [projet ou fichier]      l'atelier
//   grym-atelier --lancer fichier.grym    exécute un programme dans sa propre fenêtre (bouton « Lancer »)
#include "execution.h"
#include "fabrication.h"
#include "fenetre.h"
#include "theme.h"

#include "projet.h"

#include <QScreen>
#include <memory>
#include <QPushButton>
#include <QTimer>
#include <QGuiApplication>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <csignal>
#include <cstdio>
#include <cstring>

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

// Les commandes sans fenêtre (--fabriquer, --preparer) : pour les scripts et l'intégration continue, qui n'ont pas
// d'écran ; QCoreApplication, et non QApplication, qui exigerait un affichage.
static int sans_fenetre(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    // grym-atelier --fabriquer projet destination : le menu Programme > Fabriquer l'application…, sans fenêtre,
    // pour les scripts et l'intégration continue (docs/atelier.md, § 8). Le programme principal vient de
    // projet.grymatelier.
    if (args.at(1) == "--fabriquer") {
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
    // grym-atelier --preparer projet dossier : seulement le dossier « Programme » d'une application, pour les
    // scripts qui l'empaquettent eux-mêmes (AppImage, Windows : outils/fabriquer-ci).
    if (args.at(1) == "--preparer") {
        FichierProjet f;
        f.lire(QFileInfo(args.at(2)).absoluteFilePath());
        if (f.programme_principal.isEmpty()) {
            std::fprintf(stderr, "Programme principal inconnu : choisis-le dans l'atelier (projet.grymatelier).\n");
            return 1;
        }
        QString erreur;
        if (!preparer_programme(f.dossier, f.programme_principal, identite_du_projet(f.dossier).nom, args.at(3), &erreur)) {
            std::fprintf(stderr, "%s\n", qPrintable(erreur));
            return 1;
        }
        return 0;
    }
    // grym-atelier --identite projet : « nom=…, version=…, editeur=…, ident=… », une par ligne, pour les scripts
    // (l'installateur Windows : outils/fabriquer-ci, docs/atelier.md, § 8).
    if (args.at(1) == "--identite") {
        const Identite id = identite_du_projet(QFileInfo(args.at(2)).absoluteFilePath());
        std::printf("nom=%s\nversion=%s\nediteur=%s\nident=%s\n", id.nom.toUtf8().constData(), id.version.toUtf8().constData(),
                    id.editeur.toUtf8().constData(), id.ident.toUtf8().constData());
        return 0;
    }
    // grym-atelier --transcrit projet : « oui » si le programme transcrit des sons (§ 15.8), « non » sinon ; pour les
    // scripts, qui n'ajoutent alors le modèle qu'aux applications qui s'en servent.
    if (args.at(1) == "--transcrit") {
        std::printf("%s\n", programme_transcrit(QFileInfo(args.at(2)).absoluteFilePath()) ? "oui" : "non");
        return 0;
    }
    // grym-atelier --icone-ico projet fichier.ico : l'icône de l'application pour Windows, tirée du logo du projet.
    if (args.at(1) == "--icone-ico") {
        if (!ecrire_ico(icone_du_projet(QFileInfo(args.at(2)).absoluteFilePath()), args.at(3))) {
            std::fprintf(stderr, "L'icône ne peut pas être écrite : %s\n", qPrintable(args.at(3)));
            return 1;
        }
        return 0;
    }
    return 2;
}

int main(int argc, char **argv) {
    if ((argc == 4 && (std::strcmp(argv[1], "--fabriquer") == 0 || std::strcmp(argv[1], "--preparer") == 0
                       || std::strcmp(argv[1], "--icone-ico") == 0))
        || (argc == 3 && (std::strcmp(argv[1], "--identite") == 0 || std::strcmp(argv[1], "--transcrit") == 0)))
        return sans_fenetre(argc, argv);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("GrymoiR");
    QApplication::setApplicationName("Atelier");
    const QStringList args = QApplication::arguments();
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
        // ses rapports et ses fichiers : dans Documents/<Nom>, l'application elle-même n'étant pas modifiable (§ 15.6)
        const QString documents = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(nom);
        QDir().mkpath(documents);
        e.placer_ecriture(documents);
        e.show();
        e.demarrer();
        return app.exec() == 0 ? 0 : 1;
    }
    Theme::courant().installer(app);   // l'atelier et les programmes qu'il lance : même thème
    // L'icône de l'atelier, pour la fenêtre et la barre des tâches (Linux, Windows) ; macOS prend celle du paquet.
    QApplication::setWindowIcon(QIcon(":/application/icone/grymoir-atelier.png"));
    // Capture d'un programme à écrans, pour comparer à une maquette sans écran physique (QT_QPA_PLATFORM=offscreen) :
    //   grym-atelier --capture sortie.png [--clics "Clients;Nouveau client"] [--taille 1280x860] programme.grym
    // Les clics se font dans l'ordre, chacun sur le bouton visible qui porte ce libellé, quand l'écran attend.
    if (args.size() >= 4 && args.at(1) == "--capture") {
        QString sortie = args.at(2), programme = args.last();
        QStringList clics;
        QSize taille(1280, 860);
        for (int k = 3; k + 1 < args.size(); k += 2) {
            if (args.at(k) == "--clics") clics = args.at(k + 1).split(';', Qt::SkipEmptyParts);
            else if (args.at(k) == "--taille") {
                const QStringList t = args.at(k + 1).split('x');
                if (t.size() == 2) taille = QSize(t[0].toInt(), t[1].toInt());
            }
        }
        auto *e = new Execution(programme);
        e->resize(taille);
        e->show();
        e->demarrer();
        auto *minuterie = new QTimer(e);
        auto attendu = std::make_shared<int>(0);   // les essais à vide avant le prochain geste
        QObject::connect(minuterie, &QTimer::timeout, e, [e, sortie, clics, attendu, minuterie]() mutable {
            if (++*attendu < 8) return;   // laisser l'écran se poser et se peindre
            QPushButton *pret = nullptr;
            for (QPushButton *b : e->findChildren<QPushButton *>())
                if (b->isVisible() && b->isEnabled()) { pret = b; break; }
            if (!pret) return;
            if (!clics.isEmpty()) {
                const QString l = clics.takeFirst();
                for (QPushButton *b : e->findChildren<QPushButton *>())
                    if (b->isVisible() && b->isEnabled() && (b->text() == l || b->accessibleName() == l)) { b->click(); break; }   // une tuile : son nom accessible
                *attendu = 0;
                return;
            }
            minuterie->stop();
            e->grab().save(sortie);
            std::printf("%s\n", qPrintable(sortie));
            QCoreApplication::exit(0);
        });
        minuterie->start(150);
        return app.exec();
    }
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
    // En fenêtre, la plus grande possible : l'écran disponible, sans le plein écran (zoom sous macOS)
    if (QScreen *ecran = f.screen() ? f.screen() : QGuiApplication::primaryScreen()) f.setGeometry(ecran->availableGeometry());
    f.showMaximized();
    return app.exec();
}
