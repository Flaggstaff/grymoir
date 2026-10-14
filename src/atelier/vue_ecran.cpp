// GrymoiR : l'atelier, le dessin d'un écran.
#include "vue_ecran.h"
#include "theme.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>

extern "C" {
#include "interface.h"
}

QSize taille_placee(QWidget *w, int largeur, int hauteur) {
    QSize t = w->sizeHint().expandedTo(w->minimumSizeHint());
    if (largeur > 0) t.setWidth(largeur);
    if (hauteur > 0) t.setHeight(hauteur);
    return t;
}

// « en position libre : » (§ 22.9) : une toile, dont chaque enfant a sa géométrie ; elle mesure ce qu'ils occupent.
namespace {
class Toile : public QWidget {
public:
    Toile() { setObjectName("toile"); }
    QSize sizeHint() const override { return minimumSizeHint(); }
    QSize minimumSizeHint() const override {
        QRect r;
        for (QObject *o : children())
            if (auto *w = qobject_cast<QWidget *>(o); w && !w->isHidden() && w->property("place").toBool()) r |= w->geometry();
        return QSize(r.right() + 1, r.bottom() + 1).expandedTo(QSize(1, 1));
    }
};
}  // namespace

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
    QWidget *toile = nullptr;               // le bloc en position libre ouvert (§ 22.9), qui ne contient pas de bloc
    for (int k = 0; k < n; k++) {
        const ElementVue &e = elements[k];
        if (e.sorte != ELEMENT_BOUTON || toile) rangee = nullptr;
        if (e.sorte == ELEMENT_LIBRE) {
            toile = new Toile;
            blocs.last()->addWidget(toile, 0, Qt::AlignLeft | Qt::AlignTop);
            r.controles[k] = toile;
            continue;
        }
        if (e.sorte == ELEMENT_COTE_A_COTE || e.sorte == ELEMENT_L_UN_SOUS_L_AUTRE) {
            QBoxLayout *b = e.sorte == ELEMENT_COTE_A_COTE ? static_cast<QBoxLayout *>(new QHBoxLayout)
                                                           : static_cast<QBoxLayout *>(new QVBoxLayout);
            b->setSpacing(t.mesure(e.sorte == ELEMENT_COTE_A_COTE ? "cote-a-cote" : "l-un-sous-l-autre"));
            blocs.last()->addLayout(b, 1);
            blocs.push_back(b);
            continue;
        }
        if (e.sorte == ELEMENT_GROUPE) {   // « le groupe « Adresse » : » : un cadre titré, ses éléments l'un sous l'autre (§ 22.7)
            auto *cadre = new QGroupBox(e.texte);
            auto *b = new QVBoxLayout(cadre);
            b->setSpacing(t.mesure("l-un-sous-l-autre"));
            blocs.last()->addWidget(cadre);
            blocs.push_back(b);
            r.controles[k] = cadre;
            continue;
        }
        if (e.sorte == ELEMENT_FIN_DE_BLOC) {
            if (toile) { toile->updateGeometry(); toile = nullptr; continue; }
            if (blocs.size() > 1) blocs.pop_back();
            continue;
        }
        QBoxLayout *ici = blocs.last();
        // Dans un bloc en position libre, l'élément va sur la toile, à sa place ; ailleurs, dans la boîte du bloc.
        auto poser = [&](QWidget *w, int etirement = 0) {
            if (!toile) { ici->addWidget(w, etirement); return; }
            w->setParent(toile);
            w->setProperty("place", true);
            w->setGeometry(QRect(QPoint(e.gauche, e.haut), taille_placee(w, e.largeur, e.hauteur)));
        };
        if (e.sorte == ELEMENT_IMAGE) {   // « l'image « plan.png » » (§ 22.8) : sa taille, sans dépasser 480 × 320
            auto *l = new QLabel;
            QPixmap p;
            if (!e.octets.isEmpty() && p.loadFromData(e.octets)) {
                const qreal densite = l->devicePixelRatioF();
                if (p.width() > 480 || p.height() > 320)
                    p = p.scaled(qRound(480 * densite), qRound(320 * densite), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                else
                    p = p.scaled(qRound(p.width() * densite), qRound(p.height() * densite), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.setDevicePixelRatio(densite);
                l->setPixmap(p);
            } else {
                l->setText(e.texte);   // l'aperçu sans le fichier : son nom
            }
            l->setToolTip(e.texte);
            poser(l);
            r.controles[k] = l;
        } else if (e.sorte == ELEMENT_TEXTE) {
            auto *l = new QLabel(e.texte);
            l->setWordWrap(true);
            poser(l);
            r.controles[k] = l;
        } else if (e.sorte == ELEMENT_ZONE) {   // les contrôles des formulaires (docs/v2.md, § 10)
            QWidget *w;
            if (e.type == "vrai ou faux") w = new QCheckBox(e.texte);   // la case porte son libellé : un clic dessus la coche
            else if (en_boutons_radio(e)) {   // « (« actif », « suspendu » ou « retiré ») » : tous visibles d'un coup (§ 22.7)
                w = new QWidget;
                w->setProperty("radios", true);
                auto *rang = new QHBoxLayout(w);
                rang->setContentsMargins(0, 0, 0, 0);
                rang->setSpacing(t.mesure("espace-4"));
                auto *groupe = new QButtonGroup(w);
                groupe->setExclusive(true);
                QStringList valeurs = e.choix;
                if (e.facultatif) valeurs.prepend(QString());   // « — » : la zone reste absente
                for (const QString &v : valeurs) {
                    auto *b = new QRadioButton(v.isEmpty() ? QString::fromUtf8("—") : v);
                    b->setProperty("valeur", v);
                    if (v.isEmpty()) b->setToolTip("Aucune valeur");
                    groupe->addButton(b);
                    rang->addWidget(b);
                }
                rang->addStretch(1);
            } else if (!e.choix.isEmpty()) {
                auto *m = new QComboBox;
                m->addItem(QString());
                m->addItems(e.choix);
                w = m;
            } else if (e.plusieurs_lignes) {   // « sur plusieurs lignes » (§ 22.6) : Tab passe à la suite, Entrée va à la ligne
                auto *p = new QPlainTextEdit;
                p->setTabChangesFocus(true);
                p->setMinimumHeight(p->fontMetrics().lineSpacing() * 4 + 2 * p->frameWidth() + 8);
                p->setMaximumHeight(p->fontMetrics().lineSpacing() * 8 + 2 * p->frameWidth() + 8);
                w = p;
            } else w = new QLineEdit;
            if (e.lecture_seule) {   // « en lecture seule » : montrée, sélectionnable, jamais modifiée
                if (auto *l = qobject_cast<QLineEdit *>(w)) l->setReadOnly(true);
                else if (auto *p = qobject_cast<QPlainTextEdit *>(w)) p->setReadOnly(true);
                else w->setEnabled(false);
                w->setProperty("lecture-seule", true);
            }
            r.zones[k] = w;
            auto *conteneur = new QWidget;   // le libellé et le contrôle, un seul élément à choisir dans l'aperçu
            auto *rang = new QVBoxLayout(conteneur);   // le libellé au-dessus du contrôle (maquette, § 5 et 8)
            rang->setContentsMargins(0, 0, 0, 0);
            rang->setSpacing(t.mesure("espace-1"));
            if (!qobject_cast<QCheckBox *>(w)) {
                // une zone à remplir avant d'agir porte un astérisque (§ 22.6) ; une case a toujours une valeur
                const bool requise = !e.facultatif && !e.lecture_seule;
                auto *libelle = new QLabel(requise ? e.texte + QString::fromUtf8(" *") : e.texte);
                if (requise) libelle->setToolTip("Obligatoire");
                libelle->setProperty("niveau", "etiquette");
                libelle->setBuddy(w);
                rang->addWidget(libelle);
            }
            rang->addWidget(w);
            poser(conteneur);
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
            poser(tab, 1);
        } else {
            if (!rangee && !toile) {
                rangee = new QHBoxLayout;
                rangee->setSpacing(t.mesure("espace-2"));
                rangee->addStretch(1);
                ici->addLayout(rangee);
            }
            auto *b = new QPushButton(e.texte);
            r.boutons << b;
            r.controles[k] = b;
            if (toile) poser(b); else rangee->addWidget(b);   // en position libre : chaque bouton à sa place (§ 22.9)
        }
    }
    r.erreur = new QLabel;
    r.erreur->setProperty("message", "erreur");   // le cadre du message d'erreur, selon la feuille du thème
    r.erreur->setWordWrap(true);
    r.erreur->hide();
    pile->insertWidget(1, r.erreur);   // sous le titre : l'utilisateur le voit avant de chercher ce qui a raté
    return r;
}
