// GrymoiR : l'atelier, le fichier projet.grymatelier (docs/atelier.md, § 6.1) : ce que l'atelier retient d'un
// projet sans que ce soit du programme. Texte lisible, une ligne par entité, triée ; recréé s'il manque.
#ifndef GRYM_ATELIER_PROJET_H
#define GRYM_ATELIER_PROJET_H

#include <QMap>
#include <QPointF>
#include <QString>

struct FichierProjet {
    QString dossier;                  // racine du projet
    QString programme_principal;      // relatif au dossier (« partotheque.grym »), ou vide
    QMap<QString, QPointF> positions; // entité → position de sa boîte dans le schéma

    static QString chemin(const QString &dossier) { return dossier + "/projet.grymatelier"; }
    // Lit le fichier ; absent ou illisible, le projet repart vide (le perdre ne perd aucun programme).
    void lire(const QString &dossier);
    // Écrit le fichier, trié ; faux si l'écriture échoue.
    bool ecrire() const;
    QString texte() const;
};

#endif
