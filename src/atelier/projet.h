// GrymoiR : l'atelier, le fichier projet.grymatelier (docs/atelier.md, § 6.1) : ce que l'atelier retient d'un
// projet sans que ce soit du programme. Texte lisible, une ligne par entité, triée ; recréé s'il manque.
#ifndef GRYM_ATELIER_PROJET_H
#define GRYM_ATELIER_PROJET_H

#include <QMap>
#include <QPointF>
#include <QString>
#include <QStringList>

struct FichierProjet {
    QString dossier;                  // racine du projet
    QString programme_principal;      // relatif au dossier (« partotheque.grym »), ou vide
    // L'identité de l'application fabriquée (docs/atelier.md, § 8) : vides, le nom du dossier, la version 1.0, sans
    // éditeur. Clés à virgule (« application, version »), qu'aucun nom d'entité ne peut prendre.
    QString nom_application, version, editeur;
    QMap<QString, QPointF> positions; // entité → position de sa boîte dans le schéma

    static QString chemin(const QString &dossier) { return dossier + "/projet.grymatelier"; }
    // Lit le fichier ; absent ou illisible, le projet repart vide (le perdre ne perd aucun programme).
    void lire(const QString &dossier);
    // Écrit le fichier, trié ; faux si l'écriture échoue.
    bool ecrire() const;
    QString texte() const;
};

// Crée le fichier `nom` (« association », « association.grym », « outils.grymc ») dans `dossier`, vide.
// Sans extension, « .grym ». Jamais d'écrasement. Rend son chemin, ou vide avec *erreur rempli.
QString creer_fichier(const QString &dossier, const QString &nom, QString *erreur);

// Premier lancement d'une installation (paquet macOS, docs/mainteneur.md) : recopie le dossier d'exemple `source`
// dans `parent`, sous le même nom, puis rend le chemin de la copie. Une copie déjà là n'est jamais écrasée : on la
// rend telle quelle, avec ses données. Vide, avec *erreur, si la copie échoue.
QString installer_exemple(const QString &source, const QString &parent, QString *erreur);

// A5 (docs/atelier.md, § 5 quater). Crée le projet `nom` dans `parent` : le dossier, un programme « nom.grym »
// réduit à une remarque, et projet.grymatelier qui le désigne comme programme principal. Jamais par-dessus un
// dossier existant. Rend le chemin du dossier, ou vide avec *erreur rempli.
QString creer_projet(const QString &parent, const QString &nom, QString *erreur);

// Les projets récents, du plus récent au plus ancien, au plus `max` : `dossier` passe en tête, sans doublon.
// La liste vit dans les réglages du poste, pas dans le projet.
QStringList recents_avec(QStringList recents, const QString &dossier, int max = 8);

#endif
