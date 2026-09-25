// GrymoiR : l'atelier, réécriture chirurgicale (docs/atelier.md, § 3 ; A2-b) : chaque geste de l'éditeur de
// données change les seules phrases concernées, imprimées dans la forme canonique, et laisse le reste des
// fichiers octet pour octet. Après chaque geste, tout le projet est réanalysé : un fichier qui s'analysait
// et ne s'analyse plus fait annuler le geste, et l'erreur est rendue.
#ifndef GRYM_ATELIER_REECRITURE_H
#define GRYM_ATELIER_REECRITURE_H

#include <QMap>
#include <QString>

// Ce qu'un geste a changé : le contenu d'avant de chaque fichier touché (vide : le fichier n'existait pas).
struct Geste {
    QMap<QString, QByteArray> avant;
    QString description;   // « Ajouter le champ « prix » à « œuvre » »
};

struct ChampVoulu {
    QString nom, type;
    bool feminin = false, unique = false, facultatif = false, plusieurs = false;
};

// Chaque fonction rend un message d'erreur, vide si le geste a réussi ; *geste reçoit de quoi l'annuler.
QString ajouter_entite(const QString &dossier, const QString &fichier, const QString &nom, bool feminin, Geste *geste);
QString renommer_entite(const QString &dossier, const QString &ancien, const QString &nouveau, Geste *geste);
QString supprimer_entite(const QString &dossier, const QString &nom, Geste *geste);
QString ajouter_champ(const QString &dossier, const QString &entite, const ChampVoulu &champ, Geste *geste);
QString modifier_champ(const QString &dossier, const QString &entite, const QString &ancien, const ChampVoulu &champ,
                       Geste *geste);
QString supprimer_champ(const QString &dossier, const QString &entite, const QString &nom, Geste *geste);

// Remet chaque fichier touché dans son état d'avant ; message d'erreur, vide si tout est revenu.
QString annuler_geste(const Geste &geste);

#endif
