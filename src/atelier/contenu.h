// GrymoiR : l'atelier, le contenu de la base (docs/atelier.md, § 5 quinquies) : les objets conservés d'une
// entité, tels que le programme les a écrits. En lecture seule : une écriture passe par le langage (règles,
// valeurs permises, transaction), jamais par l'atelier.
#ifndef GRYM_ATELIER_CONTENU_H
#define GRYM_ATELIER_CONTENU_H

#include "schema.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

struct Contenu {
    QStringList colonnes;           // « N° », puis un titre par champ (« Naissance », « Genres (nombre) »)
    QVector<QStringList> lignes;    // une par objet, valeurs écrites à la manière de GrymoiR (« 12,50 », « 31.01.2026 »)
    int dans_la_corbeille = 0;      // objets supprimés (§ 16.12), comptés même s'ils ne sont pas montrés
};

// La base d'un programme : « factures.grym » → « factures.grymd » (grammaire, § 16.5).
QString base_du_programme(const QString &programme);

// Lit les objets conservés de `entite` (sous-entités comprises, comme « les clients conservés ») dans `base`.
// Rend vide si tout va bien, sinon ce qui empêche la lecture. La base est ouverte en lecture seule.
QString lire_contenu(const QString &base, const QString &entite, const QVector<EntiteSchema> &schema,
                     bool corbeille, Contenu *c);

// Sauvegarde de la base (§ 5 sexies), par l'API de sauvegarde de SQLite : une copie cohérente, même base ouverte.
// Dans `dossier` (créé au besoin), sous « nom date heure.grymd » ; *copie reçoit son chemin. Rend vide si tout va bien.
QString sauvegarder_base(const QString &base, const QString &dossier, QString *copie);

// Remet `sauvegarde` à la place de `base`, après avoir sauvegardé l'actuelle dans `dossier` (*copie_avant).
// Refuse ce qui n'est pas une base GrymoiR. Rend vide si tout va bien.
QString restaurer_base(const QString &sauvegarde, const QString &base, const QString &dossier, QString *copie_avant);

class QCheckBox;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTableView;

class FenetreContenu : public QDialog {
    Q_OBJECT
public:
    FenetreContenu(const QString &base, const QString &entite, const QVector<EntiteSchema> &schema,
                   QWidget *parent = nullptr);
    void relire();

private:
    void compter();
    int dans_la_corbeille = 0;
    QString erreur_lue;
    QString base, entite;
    QVector<EntiteSchema> schema;
    QStandardItemModel *modele;
    QSortFilterProxyModel *filtre;
    QTableView *vue;
    QLineEdit *recherche;
    QCheckBox *corbeille;
    QLabel *etat;
};

#endif
