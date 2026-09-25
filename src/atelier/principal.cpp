// GrymoiR : l'atelier (docs/atelier.md).
//   grym-atelier [projet ou fichier]      l'atelier
//   grym-atelier --lancer fichier.grym    exécute un programme dans sa propre fenêtre (bouton « Lancer »)
#include "execution.h"
#include "fenetre.h"

#include <QApplication>
#include <QSettings>
#include <csignal>

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
        const QString d = QSettings().value("dernier projet").toString();
        if (!d.isEmpty()) f.ouvrir(d);
    }
    f.show();
    return app.exec();
}
