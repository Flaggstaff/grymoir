// GrymoiR : l'atelier, le dessin d'un écran.
#include "vue_ecran.h"
#include "theme.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>

extern "C" {
#include "interface.h"
}

VueEcran dessiner_ecran(const QString &titre, const QVector<ElementVue> &elements) {
    return dessiner_ecran(titre, elements, QPixmap(), 0);
}

VueEcran dessiner_ecran(const QString &titre, const QVector<ElementVue> &elements, const QPixmap &logo,
                        int taille_logo) {
    VueEcran r;
    const int n = elements.size();
    r.controles = QVector<QWidget *>(n, nullptr);
    r.tables = QVector<QTableWidget *>(n, nullptr);
    r.zones = QVector<QWidget *>(n, nullptr);
    // Mesures du thème (src/atelier/theme/grymoir-jetons.json) : marges, écarts des blocs, hauteur des lignes.
    const Theme &t = Theme::courant();
    r.vue = new QWidget;
    auto *pile = new QVBoxLayout(r.vue);
    const int marge = t.mesure("marge-ecran");
    pile->setContentsMargins(marge, marge, marge, marge);
    pile->setSpacing(t.mesure("l-un-sous-l-autre"));
    r.titre = new QLabel(titre);
    r.titre->setTextFormat(Qt::PlainText);
    r.titre->setProperty("niveau", "ecran");   // la taille et la graisse viennent de la feuille du thème
    if (logo.isNull() || taille_logo <= 0) {
        pile->addWidget(r.titre);
    } else {   // le logo à gauche du titre, sa hauteur fixée, ses proportions gardées (§ 22.5)
        auto *entete = new QHBoxLayout;
        entete->setSpacing(t.mesure("espace-4"));
        r.logo = new QLabel;
        const qreal densite = r.vue->devicePixelRatioF();
        QPixmap p = logo.scaledToHeight(qRound(taille_logo * densite), Qt::SmoothTransformation);
        p.setDevicePixelRatio(densite);
        r.logo->setPixmap(p);
        entete->addWidget(r.logo);
        entete->addWidget(r.titre, 1);
        pile->addLayout(entete);
    }
    QHBoxLayout *rangee = nullptr;   // des boutons qui se suivent : une ligne, en bas à droite (§ 3.3)
    QVector<QBoxLayout *> blocs = {pile};   // « côte à côte », « l'un sous l'autre » : des boîtes emboîtées (§ 22.1)
    for (int k = 0; k < n; k++) {
        const ElementVue &e = elements[k];
        if (e.sorte != ELEMENT_BOUTON) rangee = nullptr;
        if (e.sorte == ELEMENT_COTE_A_COTE || e.sorte == ELEMENT_L_UN_SOUS_L_AUTRE) {
            QBoxLayout *b = e.sorte == ELEMENT_COTE_A_COTE ? static_cast<QBoxLayout *>(new QHBoxLayout)
                                                           : static_cast<QBoxLayout *>(new QVBoxLayout);
            b->setSpacing(t.mesure(e.sorte == ELEMENT_COTE_A_COTE ? "cote-a-cote" : "l-un-sous-l-autre"));
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
            if (e.type == "vrai ou faux") w = new QCheckBox(e.texte);   // la case porte son libellé : un clic dessus la coche
            else if (!e.choix.isEmpty()) {
                auto *m = new QComboBox;
                m->addItem(QString());
                m->addItems(e.choix);
                w = m;
            } else w = new QLineEdit;
            r.zones[k] = w;
            auto *conteneur = new QWidget;   // le libellé et le contrôle, un seul élément à choisir dans l'aperçu
            auto *rang = new QVBoxLayout(conteneur);   // le libellé au-dessus du contrôle (maquette, § 5 et 8)
            rang->setContentsMargins(0, 0, 0, 0);
            rang->setSpacing(t.mesure("espace-1"));
            if (!qobject_cast<QCheckBox *>(w)) {
                auto *libelle = new QLabel(e.texte);
                libelle->setProperty("niveau", "etiquette");
                libelle->setBuddy(w);
                rang->addWidget(libelle);
            }
            rang->addWidget(w);
            ici->addWidget(conteneur);
            r.controles[k] = conteneur;
        } else if (e.sorte == ELEMENT_LISTE) {
            auto *tab = new QTableWidget(0, e.colonnes.size());
            tab->setHorizontalHeaderLabels(e.colonnes);
            tab->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);   // les titres entiers
            tab->horizontalHeader()->setStretchLastSection(true);
            tab->verticalHeader()->hide();
            tab->verticalHeader()->setDefaultSectionSize(t.mesure("hauteur-ligne"));
            tab->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            tab->setShowGrid(false);   // les lignes se séparent par un trait (feuille du thème), pas par une grille
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
                rangee->setSpacing(t.mesure("espace-2"));
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
    pile->insertWidget(1, r.erreur);   // sous le titre : l'utilisateur le voit avant de chercher ce qui a raté
    return r;
}
