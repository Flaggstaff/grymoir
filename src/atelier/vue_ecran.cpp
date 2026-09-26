// GrymoiR : l'atelier, le dessin d'un écran.
#include "vue_ecran.h"
#include "theme.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

extern "C" {
#include "interface.h"
}

VueEcran dessiner_ecran(const QString &titre, const QVector<ElementVue> &elements) {
    VueEcran r;
    const int n = elements.size();
    r.controles = QVector<QWidget *>(n, nullptr);
    r.tables = QVector<QTableWidget *>(n, nullptr);
    r.zones = QVector<QWidget *>(n, nullptr);
    r.vue = new QWidget;
    auto *pile = new QVBoxLayout(r.vue);
    r.titre = new QLabel(titre);
    r.titre->setTextFormat(Qt::PlainText);
    r.titre->setProperty("niveau", "ecran");   // la taille et la graisse viennent de la feuille du thème
    pile->addWidget(r.titre);
    QHBoxLayout *rangee = nullptr;   // des boutons qui se suivent : une ligne, en bas à droite (§ 3.3)
    QVector<QBoxLayout *> blocs = {pile};   // « côte à côte », « l'un sous l'autre » : des boîtes emboîtées (§ 22.1)
    for (int k = 0; k < n; k++) {
        const ElementVue &e = elements[k];
        if (e.sorte != ELEMENT_BOUTON) rangee = nullptr;
        if (e.sorte == ELEMENT_COTE_A_COTE || e.sorte == ELEMENT_L_UN_SOUS_L_AUTRE) {
            QBoxLayout *b = e.sorte == ELEMENT_COTE_A_COTE ? static_cast<QBoxLayout *>(new QHBoxLayout)
                                                           : static_cast<QBoxLayout *>(new QVBoxLayout);
            blocs.last()->addLayout(b, 1);
            blocs.push_back(b);
            continue;
        }
        if (e.sorte == ELEMENT_FIN_DE_BLOC) {
            if (blocs.size() > 1) blocs.pop_back();
            continue;
        }
        QBoxLayout *ici = blocs.last();
        if (e.sorte == ELEMENT_TEXTE) {
            auto *l = new QLabel(e.texte);
            l->setWordWrap(true);
            ici->addWidget(l);
            r.controles[k] = l;
        } else if (e.sorte == ELEMENT_ZONE) {   // les contrôles des formulaires (docs/v2.md, § 10)
            QWidget *w;
            if (e.type == "vrai ou faux") w = new QCheckBox;
            else if (!e.choix.isEmpty()) {
                auto *m = new QComboBox;
                m->addItem(QString());
                m->addItems(e.choix);
                w = m;
            } else w = new QLineEdit;
            r.zones[k] = w;
            auto *conteneur = new QWidget;   // le libellé et le contrôle, un seul élément à choisir dans l'aperçu
            auto *rang = new QFormLayout(conteneur);
            rang->setContentsMargins(0, 0, 0, 0);
            rang->addRow(e.texte, w);
            ici->addWidget(conteneur);
            r.controles[k] = conteneur;
        } else if (e.sorte == ELEMENT_LISTE) {
            auto *tab = new QTableWidget(0, e.colonnes.size());
            tab->setHorizontalHeaderLabels(e.colonnes);
            tab->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);   // les titres entiers
            tab->horizontalHeader()->setStretchLastSection(true);
            tab->verticalHeader()->hide();
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            QFont chiffres = tab->font();   // chiffres de largeur fixe : les colonnes de nombres s'alignent
            chiffres.setFeature(QFont::Tag("tnum"), 1);
            tab->setFont(chiffres);
#endif
            tab->setSelectionBehavior(QAbstractItemView::SelectRows);
            tab->setSelectionMode(QAbstractItemView::SingleSelection);
            tab->setEditTriggers(QAbstractItemView::NoEditTriggers);
            // Un clic sur un titre trie à l'écran, sans toucher au programme (§ 22.1) ; un second clic inverse.
            QObject::connect(tab->horizontalHeader(), &QHeaderView::sectionClicked, tab, [tab](int c) {
                const bool meme = tab->property("tri").isValid() && tab->property("tri").toInt() == c;
                const Qt::SortOrder o = meme && tab->property("ordre").toInt() == Qt::AscendingOrder ? Qt::DescendingOrder
                                                                                                    : Qt::AscendingOrder;
                tab->setProperty("tri", c);
                tab->setProperty("ordre", (int)o);
                tab->horizontalHeader()->setSortIndicatorShown(true);
                tab->horizontalHeader()->setSortIndicator(c, o);
                tab->sortItems(c, o);
            });
            r.tables[k] = tab;
            r.controles[k] = tab;
            ici->addWidget(tab, 1);
        } else {
            if (!rangee) {
                rangee = new QHBoxLayout;
                rangee->addStretch(1);
                ici->addLayout(rangee);
            }
            auto *b = new QPushButton(e.texte);
            r.boutons << b;
            r.controles[k] = b;
            rangee->addWidget(b);
        }
    }
    r.erreur = new QLabel;
    r.erreur->setProperty("message", "erreur");   // le cadre du message d'erreur, selon la feuille du thème
    r.erreur->setWordWrap(true);
    r.erreur->hide();
    pile->addWidget(r.erreur);
    return r;
}
