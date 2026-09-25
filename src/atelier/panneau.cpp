// GrymoiR : l'atelier, panneau des propriétés d'une entité.
#include "panneau.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Colonne { C_NOM, C_TYPE, C_FEMININ, C_UNIQUE, C_FACULTATIF, C_PLUSIEURS, C_CASCADE, C_DEPART, NB_COLONNES };

QCheckBox *case_dans(QTableWidget *t, int ligne, int colonne) {
    QWidget *w = t->cellWidget(ligne, colonne);
    return w ? w->findChild<QCheckBox *>() : nullptr;
}

QWidget *centree(QCheckBox *c) {   // une case à cocher, centrée dans sa cellule
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setAlignment(Qt::AlignCenter);
    h->addWidget(c);
    return w;
}
}  // namespace

PanneauEntite::PanneauEntite(QWidget *parent) : QWidget(parent) {
    titre = new QLabel("Choisissez une entité dans le schéma.");
    titre->setWordWrap(true);
    nom = new QLineEdit;
    bouton_renommer = new QPushButton("Renommer");
    bouton_supprimer = new QPushButton("Supprimer l'entité");
    champs = new QTableWidget(0, NB_COLONNES);
    champs->setHorizontalHeaderLabels({"Champ", "Type", "Féminin", "Unique", "Facultatif", "Plusieurs",
                                       "Disparaît avec", "Au départ"});
    champs->horizontalHeaderItem(C_CASCADE)->setToolTip("Pour un lien : l'objet disparaît avec celui qu'il désigne (§ 16.12)");
    champs->horizontalHeaderItem(C_DEPART)->setToolTip("La valeur que reçoivent les objets déjà conservés quand le champ apparaît (§ 16.7)");
    champs->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    champs->horizontalHeader()->setSectionResizeMode(C_TYPE, QHeaderView::Stretch);
    champs->verticalHeader()->hide();
    champs->setSelectionBehavior(QAbstractItemView::SelectRows);
    champs->setSelectionMode(QAbstractItemView::SingleSelection);
    bouton_ajouter = new QPushButton("Nouveau champ");
    bouton_appliquer = new QPushButton("Appliquer");
    bouton_retirer = new QPushButton("Supprimer le champ");
    bouton_appliquer->setToolTip("Écrit dans le code la ligne choisie du tableau");

    auto *ligne_nom = new QHBoxLayout;
    ligne_nom->addWidget(nom, 1);
    ligne_nom->addWidget(bouton_renommer);
    auto *boutons = new QHBoxLayout;
    boutons->addWidget(bouton_ajouter);
    boutons->addWidget(bouton_appliquer);
    boutons->addWidget(bouton_retirer);
    auto *pile = new QVBoxLayout(this);
    pile->addWidget(titre);
    pile->addLayout(ligne_nom);
    pile->addWidget(bouton_supprimer);
    pile->addWidget(new QLabel("Champs :"));
    pile->addWidget(champs, 1);
    pile->addLayout(boutons);

    connect(bouton_renommer, &QPushButton::clicked, this, [this] {
        if (!nom_entite.isEmpty() && nom->text().trimmed() != nom_entite) emit renommer(nom_entite, nom->text().trimmed());
    });
    connect(nom, &QLineEdit::returnPressed, bouton_renommer, &QPushButton::click);
    connect(bouton_supprimer, &QPushButton::clicked, this, [this] { if (!nom_entite.isEmpty()) emit supprimer(nom_entite); });
    connect(bouton_ajouter, &QPushButton::clicked, this, [this] {   // une ligne vide, écrite par « Appliquer »
        ajouter_ligne(ChampSchema(), types_connus, true);
        champs->selectRow(champs->rowCount() - 1);
        champs->editItem(champs->item(champs->rowCount() - 1, C_NOM));
    });
    connect(bouton_appliquer, &QPushButton::clicked, this, [this] {
        const int k = champs->currentRow();
        if (k < 0 || nom_entite.isEmpty()) return;
        const QString ancien = champs->item(k, C_NOM)->data(Qt::UserRole).toString();
        if (ancien.isEmpty()) emit ajouter_champ(nom_entite, champ_de_la_ligne(k));
        else emit modifier_champ(nom_entite, ancien, champ_de_la_ligne(k));
    });
    connect(bouton_retirer, &QPushButton::clicked, this, [this] {
        const int k = champs->currentRow();
        if (k < 0 || nom_entite.isEmpty()) return;
        const QString ancien = champs->item(k, C_NOM)->data(Qt::UserRole).toString();
        if (ancien.isEmpty()) champs->removeRow(k);   // une ligne jamais écrite s'efface simplement
        else emit supprimer_champ(nom_entite, ancien);
    });
    montrer(nullptr, {});
}

void PanneauEntite::ajouter_ligne(const ChampSchema &c, const QStringList &types, bool neuve) {
    const int k = champs->rowCount();
    champs->insertRow(k);
    auto *n = new QTableWidgetItem(c.nom);
    n->setData(Qt::UserRole, neuve ? QString() : c.nom);   // le nom écrit dans le code, pour le retrouver
    champs->setItem(k, C_NOM, n);
    auto *type = new QComboBox;
    type->addItems(types);
    type->setEditable(false);
    type->setCurrentIndex(qMax(0, type->findText(neuve ? QString("texte") : c.type)));
    champs->setCellWidget(k, C_TYPE, type);
    auto cocher = [&](int col, bool v) {
        auto *b = new QCheckBox;
        b->setChecked(v);
        champs->setCellWidget(k, col, centree(b));
    };
    cocher(C_FEMININ, c.feminin);
    cocher(C_UNIQUE, c.unique);
    cocher(C_FACULTATIF, c.facultatif);
    cocher(C_PLUSIEURS, c.multiple);
    cocher(C_CASCADE, c.cascade);
    champs->setItem(k, C_DEPART, new QTableWidgetItem(c.depart));
}

ChampVoulu PanneauEntite::champ_de_la_ligne(int k) const {
    ChampVoulu c;
    c.nom = champs->item(k, C_NOM)->text().trimmed();
    if (auto *t = qobject_cast<QComboBox *>(champs->cellWidget(k, C_TYPE))) c.type = t->currentText();
    c.feminin = case_dans(champs, k, C_FEMININ)->isChecked();
    c.unique = case_dans(champs, k, C_UNIQUE)->isChecked();
    c.facultatif = case_dans(champs, k, C_FACULTATIF)->isChecked();
    c.plusieurs = case_dans(champs, k, C_PLUSIEURS)->isChecked();
    c.cascade = case_dans(champs, k, C_CASCADE)->isChecked();
    if (auto *d = champs->item(k, C_DEPART)) c.depart = d->text().trimmed();
    return c;
}

void PanneauEntite::montrer(const EntiteSchema *e, const QStringList &types) {
    types_connus = types;
    champs->setRowCount(0);
    nom_entite = e ? e->nom : QString();
    nom->setText(nom_entite);
    for (QWidget *w : {static_cast<QWidget *>(nom), static_cast<QWidget *>(bouton_renommer),
                       static_cast<QWidget *>(bouton_supprimer), static_cast<QWidget *>(champs),
                       static_cast<QWidget *>(bouton_ajouter), static_cast<QWidget *>(bouton_appliquer),
                       static_cast<QWidget *>(bouton_retirer)})
        w->setEnabled(e != nullptr);
    if (!e) {
        titre->setText("Choisissez une entité dans le schéma.");
        return;
    }
    titre->setText(QString("<b>%1</b>%2").arg(e->nom.toHtmlEscaped(),
                                              e->parent.isEmpty() ? QString() : QString(", hérite de « %1 »").arg(e->parent)));
    for (const auto &c : e->champs) ajouter_ligne(c, types, false);
}
