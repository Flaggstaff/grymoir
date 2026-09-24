// GrymoiR : l'atelier (docs/atelier.md). grym-atelier [projet ou fichier]
#include "fenetre.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("GrymoiR");
    QApplication::setApplicationName("Atelier");
    Fenetre f;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) f.ouvrir(args.at(1));
    else {
        const QString d = QSettings().value("dernier projet").toString();
        if (!d.isEmpty()) f.ouvrir(d);
    }
    f.show();
    return app.exec();
}
