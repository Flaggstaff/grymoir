// GrymoiR : l'atelier, importer un tableau CSV dans une entité (docs/atelier.md, § 5 sexies). L'atelier n'écrit
// que du GrymoiR : il prépare une boucle « Pour chaque rangée du tableau … » (grammaire, § 15.5), gardée par
// « Si le nombre de … conservés = 0 », que le programme principal exécute à son prochain lancement.
#ifndef GRYM_ATELIER_IMPORTATION_H
#define GRYM_ATELIER_IMPORTATION_H

#include "reecriture.h"
#include "schema.h"

#include <QDialog>
#include <QMap>
#include <QStringList>

// Les titres de la première ligne d'un CSV, avec le séparateur que GrymoiR devinera (§ 15.5).
QStringList titres_csv(const QString &chemin, QString *erreur);

// Les champs qu'une importation peut remplir : ceux de la lignée, sans « plusieurs », fichiers ni images ;
// un lien seulement si l'entité désignée a une clé (un champ texte unique).
QVector<ChampSchema> champs_importables(const QString &entite, const QVector<EntiteSchema> &schema);

// La colonne qui correspond le mieux au champ, sans tenir compte des accents ni de la casse ; vide si aucune.
QString colonne_pour(const QString &champ, const QStringList &titres);

// Le code de l'importation ; vide et *erreur si un champ obligatoire n'a pas de colonne.
// `colonnes` : champ → titre de colonne (absent ou vide : champ non rempli). `csv` : le chemin tel qu'écrit.
QString code_importation(const QString &entite, const QVector<EntiteSchema> &schema, const QString &csv,
                         const QMap<QString, QString> &colonnes, QString *erreur);

class QComboBox;
class QPlainTextEdit;
class QTableWidget;
class QLabel;

class DialogueImportation : public QDialog {
    Q_OBJECT
public:
    // `csv_ecrit` : le chemin du CSV tel que le programme l'écrira (relatif à son dossier).
    DialogueImportation(const QString &csv, const QString &csv_ecrit, const QString &entite,
                        const QVector<EntiteSchema> &schema, QWidget *parent = nullptr);
    QString code() const { return code_; }

private:
    void remplir();
    void apercu();
    QString csv, csv_ecrit;
    QVector<EntiteSchema> schema;
    QStringList titres;
    QComboBox *entites;
    QTableWidget *table;
    QPlainTextEdit *texte;
    QLabel *etat;
    QString code_;
    class QPushButton *ecrire;
};

#endif
