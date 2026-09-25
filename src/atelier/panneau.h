// GrymoiR : l'atelier, panneau des propriétés d'une entité (A2-b) : son nom, ses champs. Il ne réécrit rien
// lui-même : il émet des demandes, que la fenêtre confie à la réécriture chirurgicale.
#ifndef GRYM_ATELIER_PANNEAU_H
#define GRYM_ATELIER_PANNEAU_H

#include "reecriture.h"
#include "schema.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class PanneauEntite : public QWidget {
    Q_OBJECT
public:
    explicit PanneauEntite(QWidget *parent = nullptr);
    // Montre une entité (nullptr : rien de choisi). `types` : les types de base, puis les entités du projet.
    void montrer(const EntiteSchema *e, const QStringList &types);
    QString entite() const { return nom_entite; }
    // Le champ de la ligne k, tel que le tableau le décrit.
    ChampVoulu champ_de_la_ligne(int k) const;
    QTableWidget *tableau() const { return champs; }

signals:
    void renommer(const QString &ancien, const QString &nouveau);
    void supprimer(const QString &entite);
    void ajouter_champ(const QString &entite, const ChampVoulu &champ);
    void modifier_champ(const QString &entite, const QString &ancien, const ChampVoulu &champ);
    void supprimer_champ(const QString &entite, const QString &nom);

private:
    void ajouter_ligne(const ChampSchema &c, const QStringList &types, bool neuve);
    QString nom_entite;
    QStringList types_connus;
    QLabel *titre;
    QLineEdit *nom;
    QPushButton *bouton_renommer, *bouton_supprimer, *bouton_ajouter, *bouton_appliquer, *bouton_retirer;
    QTableWidget *champs;
};

#endif
