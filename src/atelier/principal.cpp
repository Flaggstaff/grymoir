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
#include <QTableWidget>
#include <QPainter>
#include <QAction>
#include <QTimer>
#include <QGuiApplication>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QMouseEvent>
#include "agenda.h"
#include <QStandardPaths>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <QDateTime>
#include <QFile>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h>
#define GRYM_PILE 1
#endif
#endif

extern "C" {
#include "vm.h"
}

// L'atelier demande l'arrêt par un signal (SIGTERM, ou SIGINT) : la machine s'arrête proprement
// au prochain saut arrière ou appel, et le journal annule l'exécution (docs/vm.md, § 6).
// Venu de l'atelier (bouton « Arrêter »), le signal ferme aussi la fenêtre une fois l'exécution annulée.
volatile std::sig_atomic_t grym_fermeture_demandee = 0;

// En fenêtre, la plus grande possible : l'écran disponible, sans le plein écran exclusif (zoom sous macOS), comme
// l'atelier ; c'est ainsi qu'on travaille dans une application de gestion.
static void sur_arret(int signal_recu) {
    grym_interruption = 1;
    grym_fermeture_demandee = 1;
    std::signal(signal_recu, sur_arret);
}

// Le journal des plantages (6 novembre 2026) : une application fabriquée qui s'arrête brusquement (accès mémoire interdit,
// abandon, erreur de calcul, instruction illégale ; sous Windows, toute exception non rattrapée) écrit, avant de mourir,
// dans Documents/<Nom>/Plantages.txt : le moment, le signal, l'application et sa version, la pile d'appels (macOS, Linux).
// Au lancement suivant, l'application le dit, pour que le fichier parte chez qui la maintient. Le gestionnaire n'utilise
// que des appels permis dans un signal (open, write, close, backtrace_symbols_fd) ; tout le reste est préparé d'avance.
static char plantage_chemin[4096];
static char plantage_entete[1024];

static void plantage_ecrire(int fd, const char *t) {
    const size_t n = std::strlen(t);
#ifdef _WIN32
    _write(fd, t, (unsigned)n);
#else
    if (write(fd, t, n) < 0) {}
#endif
}

static void plantage_nombre(int fd, long long v) {
    char t[32], *p = t + sizeof t - 1;
    *p = 0;
    const bool negatif = v < 0;
    unsigned long long u = negatif ? (unsigned long long)(-v) : (unsigned long long)v;
    do { *--p = (char)('0' + u % 10); u /= 10; } while (u);
    if (negatif) *--p = '-';
    plantage_ecrire(fd, p);
}

static int plantage_ouvrir() {
#ifdef _WIN32
    return _open(plantage_chemin, _O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY, 0644);
#else
    return open(plantage_chemin, O_WRONLY | O_APPEND | O_CREAT, 0644);
#endif
}

static void plantage_fermer(int fd) {
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
}

static void sur_plantage(int signal_recu) {
    const int fd = plantage_ouvrir();
    if (fd >= 0) {
        plantage_ecrire(fd, "\n--- Plantage, ");
        plantage_nombre(fd, (long long)std::time(nullptr));
        plantage_ecrire(fd, " (secondes depuis 1970, UTC) ---\n");
        plantage_ecrire(fd, plantage_entete);
        plantage_ecrire(fd, "Signal : ");
        plantage_ecrire(fd, signal_recu == SIGSEGV ? "SIGSEGV (accès mémoire interdit)"
                            : signal_recu == SIGABRT ? "SIGABRT (abandon)"
                            : signal_recu == SIGFPE ? "SIGFPE (erreur de calcul)"
                            : signal_recu == SIGILL ? "SIGILL (instruction illégale)"
#ifdef SIGBUS
                            : signal_recu == SIGBUS ? "SIGBUS (erreur de bus)"
#endif
                            : "autre");
        plantage_ecrire(fd, "\n");
#ifdef GRYM_PILE
        void *pile[64];
        const int n = backtrace(pile, 64);
        plantage_ecrire(fd, "Pile d'appels :\n");
        backtrace_symbols_fd(pile, n, fd);
#endif
        plantage_fermer(fd);
    }
    std::signal(signal_recu, SIG_DFL);
    std::raise(signal_recu);
}

#ifdef _WIN32
static LONG WINAPI sur_exception(EXCEPTION_POINTERS *e) {
    const int fd = plantage_ouvrir();
    if (fd >= 0) {
        plantage_ecrire(fd, "\n--- Plantage, ");
        plantage_nombre(fd, (long long)std::time(nullptr));
        plantage_ecrire(fd, " (secondes depuis 1970, UTC) ---\n");
        plantage_ecrire(fd, plantage_entete);
        plantage_ecrire(fd, "Exception Windows : ");
        plantage_nombre(fd, (long long)e->ExceptionRecord->ExceptionCode);
        plantage_ecrire(fd, ", à l'adresse ");
        plantage_nombre(fd, (long long)(uintptr_t)e->ExceptionRecord->ExceptionAddress);
        plantage_ecrire(fd, "\n");
        plantage_fermer(fd);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Les messages fatals de Qt (qFatal) : écrits au journal, puis l'abandon habituel, que sur_plantage note aussi.
static void sur_message_qt(QtMsgType sorte, const QMessageLogContext &, const QString &message) {
    std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    if (sorte != QtFatalMsg) return;
    const int fd = plantage_ouvrir();
    if (fd >= 0) {
        plantage_ecrire(fd, "\nMessage fatal de Qt : ");
        plantage_ecrire(fd, message.toUtf8().constData());
        plantage_ecrire(fd, "\n");
        plantage_fermer(fd);
    }
}

// Prépare le journal dans le dossier donné, pour l'application nommée ; rend le chemin du fichier.
static QString installer_journal_plantages(const QString &dossier, const QString &nom, const QString &version) {
    const QString chemin = QDir(dossier).filePath("Plantages.txt");
    std::snprintf(plantage_chemin, sizeof plantage_chemin, "%s", QFile::encodeName(chemin).constData());
    std::snprintf(plantage_entete, sizeof plantage_entete, "Application : %s, version %s ; GrymoiR, atelier de %s ; %s\n",
                  nom.toUtf8().constData(), version.isEmpty() ? "inconnue" : version.toUtf8().constData(), __DATE__,
                  QSysInfo::prettyProductName().toUtf8().constData());
    for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL
#ifdef SIGBUS
                  , SIGBUS
#endif
         })
        std::signal(s, sur_plantage);
#ifdef _WIN32
    SetUnhandledExceptionFilter(sur_exception);
#endif
    qInstallMessageHandler(sur_message_qt);
    return chemin;
}

// Au lancement : un plantage noté depuis le lancement d'avant se dit, une fois.
static void annoncer_plantage(const QString &chemin, const QString &nom, QWidget *parent) {
    const qint64 taille = QFileInfo(chemin).size(), vue = QSettings().value("plantages-vus", (qint64)0).toLongLong();
    QSettings().setValue("plantages-vus", taille);
    if (taille <= vue) return;
    QTimer::singleShot(0, parent, [chemin, nom, parent] {
        QMessageBox::information(parent, nom,
                                 QString::fromUtf8("%1 s'est arrêté brusquement la dernière fois. Ce qui s'est passé est noté dans :\n%2\n\n"
                                                   "Envoie ce fichier à qui maintient l'application : il aide à trouver la cause. "
                                                   "Tes données n'y sont pas.").arg(nom, chemin));
    });
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
        || (argc == 3 && std::strcmp(argv[1], "--identite") == 0))
        return sans_fenetre(argc, argv);
    // grym-atelier --essai-plantage dossier : installe le journal des plantages dans le dossier, puis plante (les essais)
    if (argc == 3 && std::strcmp(argv[1], "--essai-plantage") == 0) {
        QCoreApplication app(argc, argv);
        installer_journal_plantages(QString::fromLocal8Bit(argv[2]), QString::fromUtf8("Essai"), QString::fromUtf8("1.0"));
        volatile int *rien = nullptr;
        return *rien;   // un accès mémoire interdit, voulu
    }
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
#ifdef Q_OS_WIN
        // Sous Windows, QSettings range les réglages dans le registre et exige une organisation : sans elle, il ne lit
        // ni n'écrit rien (AccessError, documentation de QSettings) ; les réglages d'une écoute, par exemple, se
        // perdaient. L'organisation prend le nom de l'application, après le calcul du dossier des données, qui reste
        // donc le même (8 novembre 2026, 3.0.1). Sous macOS et Linux, rien ne change.
        QApplication::setOrganizationName(nom);
#endif
        const QString chemin = QDir(programme).filePath(principal);
        Execution e(chemin);
        e.setWindowTitle(nom);
        e.placer_base(QDir(donnees).filePath(QFileInfo(principal).completeBaseName() + ".grymd"));
        // ses rapports et ses fichiers : dans Documents/<Nom>, l'application elle-même n'étant pas modifiable (§ 15.6)
        const QString documents = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(nom);
        QDir().mkpath(documents);
        e.placer_ecriture(documents);
        // le journal des plantages, dans le même dossier (6 novembre 2026)
        const QString plantages = installer_journal_plantages(documents, nom, QCoreApplication::applicationVersion());   // Info.plist, VERSIONINFO
        annoncer_plantage(plantages, nom, &e);
        // la fenêtre prend la taille de l'accueil (sans vide autour), centrée ; un écran plus grand défile
        e.ajuster_au_premier_ecran();
        e.show();
        e.demarrer();
        return app.exec() == 0 ? 0 : 1;
    }
    Theme::courant().installer(app);   // l'atelier et les programmes qu'il lance : même thème
    // L'icône de l'atelier, pour la fenêtre et la barre des tâches (Linux, Windows) ; macOS prend celle du paquet.
    QApplication::setWindowIcon(QIcon(":/application/icone/grymoir-atelier.png"));
    // Capture d'un programme à écrans, pour comparer à une maquette sans écran physique (QT_QPA_PLATFORM=offscreen) :
    //   grym-atelier --capture sortie.png [--clics "Clients;Nouveau client;ligne:Anne Rochat;calendrier"] [--taille 1280x860] programme.grym
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
                if (l == QString::fromUtf8("calendrier")) {   // ouvrir le calendrier de la première zone (date)
                    for (QAction *a : e->findChildren<QAction *>("calendrier")) { a->trigger(); break; }
                } else if (l.startsWith(QString::fromUtf8("créneau:"))) {   // « créneau:02.11.2026 10:30 » : un clic dans l'agenda (2.75)
                    const QStringList jh = l.mid(8).split(' ');
                    const QDate j = QDate::fromString(jh.value(0), "dd.MM.yyyy");
                    const QTime h = QTime::fromString(jh.value(1), "HH:mm");
                    if (Agenda *ag = e->findChild<Agenda *>(); ag && j.isValid() && h.isValid()) {
                        const QPointF p = ag->point_de(j, h) + QPoint(0, 2);
                        QMouseEvent a(QEvent::MouseButtonPress, p, ag->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(ag, &a);
                        QMouseEvent b(QEvent::MouseButtonRelease, p, ag->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                        QApplication::sendEvent(ag, &b);
                    }
                } else if (l.startsWith(QString::fromUtf8("ligne:"))) {   // « ligne:Anne Rochat » : choisir la ligne qui porte ce texte
                    const QString cherche = l.mid(6);
                    for (QTableWidget *t : e->findChildren<QTableWidget *>()) {
                        const QList<QTableWidgetItem *> vus = t->isVisible() ? t->findItems(cherche, Qt::MatchExactly) : QList<QTableWidgetItem *>();
                        if (vus.isEmpty()) continue;
                        t->setCurrentItem(vus.first());
                        emit t->cellActivated(vus.first()->row(), vus.first()->column());
                        break;
                    }
                } else
                for (QPushButton *b : e->findChildren<QPushButton *>())
                    if (b->isVisible() && b->isEnabled() && (b->text() == l || b->accessibleName() == l)) { b->click(); break; }   // une tuile : son nom accessible
                *attendu = 0;
                return;
            }
            minuterie->stop();
            QPixmap image = e->grab();
            if (QWidget *surgi = QApplication::activePopupWidget()) {   // un calendrier ouvert : peint par-dessus, à sa place
                QPainter p(&image);
                p.drawPixmap(e->mapFromGlobal(surgi->mapToGlobal(QPoint(0, 0))), surgi->grab());
            }
            image.save(sortie);
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
        e.ajuster_au_premier_ecran();   // comme l'application fabriquée : ce qu'on essaie est ce que l'utilisateur verra
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
    if (f.reprendre_disposition()) f.show();   // la taille et les panneaux du dernier lancement
    else f.showMaximized();
    return app.exec();
}
