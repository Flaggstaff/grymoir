// GrymoiR : l'atelier, le dessin d'un écran.
#include "vue_ecran.h"
#include <QFontInfo>
#include <QAction>
#include <QMenu>
#include <QMenuBar>

#include <QLocale>

#include <QPainter>
#include "theme.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QTabWidget>
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

void montrer_image(QLabel *l, const QByteArray &octets, const QString &repli, int largeur, int hauteur) {
    QPixmap p;
    if (octets.isEmpty() || !p.loadFromData(octets)) {
        l->setPixmap(QPixmap());
        l->setText(repli);
        return;
    }
    const qreal densite = l->devicePixelRatioF();
    const int lmax = largeur > 0 ? largeur : 480, hmax = hauteur > 0 ? hauteur : 320;
    if (p.width() > lmax || p.height() > hmax)
        p = p.scaled(qRound(lmax * densite), qRound(hmax * densite), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    else
        p = p.scaled(qRound(p.width() * densite), qRound(p.height() * densite), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.setDevicePixelRatio(densite);
    l->setText(QString());
    l->setPixmap(p);
}

// « en interrupteur » (§ 22.6) : une case à cocher dessinée en interrupteur, un rail et un bouton ; le câblage d'une case
// (toggled, setChecked) reste le même.
namespace {
class Interrupteur : public QCheckBox {
public:
    explicit Interrupteur(const QString &texte) : QCheckBox(texte) { setObjectName("interrupteur"); }
    QSize sizeHint() const override {
        const QSize t = fontMetrics().size(Qt::TextSingleLine, text());
        return QSize(44 + 8 + t.width(), qMax(24, t.height() + 4));
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF rail(1, (height() - 22) / 2.0, 42, 22);
        const QColor accent = palette().color(QPalette::Highlight);
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? accent : palette().color(QPalette::Mid));
        if (!isEnabled()) p.setOpacity(0.5);
        p.drawRoundedRect(rail, 11, 11);
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(isChecked() ? rail.right() - 20 : rail.left() + 2, rail.top() + 2, 18, 18));
        p.setOpacity(1);
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QRect(52, 0, width() - 52, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
        if (hasFocus()) {
            p.setPen(QPen(accent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rail.adjusted(-2, -2, 2, 2), 13, 13);
        }
    }
    bool hitButton(const QPoint &pos) const override { return rect().contains(pos); }
};
}  // namespace

void taille_du_titre(QLabel *titre, int taille) {
    if (!titre) return;
    titre->setStyleSheet(QString());
    if (!taille) return;
    const int px = QFontInfo(titre->font()).pixelSize();
    titre->setStyleSheet(QString("font-size: %1px;").arg(qRound(px * (taille > 0 ? 1.6 : 0.75))));
}

double nombre_affiche(const QString &t) {
    QString b;
    for (QChar c : t) {
        if (c.isDigit() || c == '-' || c == '.') b += c;
        else if (c == ',') b += '.';
    }
    return b.toDouble();
}

Graphique::Graphique(bool s, QWidget *parent) : QWidget(parent), secteurs(s) {
    setObjectName("graphique");
    setMinimumSize(160, 100);
    noms = {QString::fromUtf8("…"), QString::fromUtf8("…"), QString::fromUtf8("…")};   // l'aperçu, sans données
    vals = {3, 2, 1};
}

void Graphique::donner(const QStringList &l, const QVector<double> &v) {
    exemple = false;
    noms = l;
    vals = v;
    updateGeometry();
    update();
}

QSize Graphique::sizeHint() const { return QSize(420, secteurs ? 220 : qMax(100, (int)noms.size() * 30 + 16)); }

void Graphique::paintEvent(QPaintEvent *) {
    static const char *const teintes[] = {"#2f6f8f", "#c0633a", "#5a8f3c", "#8f5aa8", "#c49a2c", "#3c8f86", "#a84a6a", "#6a6a6a"};
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (exemple) p.setOpacity(0.35);   // l'aperçu : un exemple, pas des données
    const QColor texte = palette().color(QPalette::WindowText);
    if (noms.isEmpty()) { p.setPen(texte); p.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("Aucune donnée.")); return; }
    double max = 0, total = 0;
    for (double v : vals) { max = qMax(max, v); if (v > 0) total += v; }
    if (secteurs) {
        const int d = qMin(height() - 16, width() / 2 - 16);
        const QRectF disque(8, 8, d, d);
        double fait = 0;
        for (int i = 0; i < vals.size() && total > 0; i++) {
            if (vals[i] <= 0) continue;
            p.setBrush(QColor(teintes[i % 8]));
            p.setPen(Qt::NoPen);
            // Qt compte en seizièmes de degré, à partir de trois heures, dans le sens inverse : de midi, dans le sens horaire
            p.drawPie(disque, qRound((90 - fait * 360) * 16), -qRound(vals[i] / total * 360 * 16));
            fait += vals[i] / total;
        }
        for (int i = 0; i < noms.size(); i++) {
            const int y = 8 + i * 24;
            p.setBrush(QColor(teintes[i % 8]));
            p.setPen(Qt::NoPen);
            p.drawRect(d + 24, y, 14, 14);
            p.setPen(texte);
            p.drawText(d + 46, y + 12, noms[i] + " : " + QLocale().toString(vals.value(i)));
        }
        return;
    }
    const int gauche = qMin(140, width() / 3), large = width() - gauche - 64;
    for (int i = 0; i < noms.size(); i++) {
        const int y = 8 + i * 30;
        p.setPen(texte);
        p.drawText(QRect(0, y, gauche - 8, 20), Qt::AlignRight | Qt::AlignVCenter, noms[i]);
        const double l = max > 0 && vals.value(i) > 0 ? large * vals[i] / max : 0;
        p.fillRect(QRectF(gauche, y, l, 20), QColor(teintes[0]));
        p.drawText(QRectF(gauche + l + 6, y, 60, 20), Qt::AlignLeft | Qt::AlignVCenter, QLocale().toString(vals.value(i)));
    }
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

// Un élément de menu (§ 22.12) : l'action suit l'état du bouton invisible qui porte son événement.
namespace {
class MiroirDAction : public QObject {
public:
    MiroirDAction(QAction *a, QObject *parent) : QObject(parent), action(a) {}
    bool eventFilter(QObject *o, QEvent *ev) override {
        auto *w = static_cast<QWidget *>(o);
        if (ev->type() == QEvent::EnabledChange) action->setEnabled(w->isEnabled());
        else if (ev->type() == QEvent::ShowToParent || ev->type() == QEvent::HideToParent) action->setVisible(!w->isHidden());
        return false;
    }
private:
    QAction *action;
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
    // sa hauteur naturelle, jamais plus : extensible, il prenait toute la hauteur libre de l'aperçu et s'y centrait
    r.titre->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    r.titre->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
        r.logo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        entete->addWidget(r.logo);
        entete->addWidget(r.titre, 1);
        pile->addLayout(entete);
    }
    QHBoxLayout *rangee = nullptr;   // des boutons qui se suivent : une ligne, en bas à droite (§ 3.3)
    // Les conteneurs ouverts : une boîte (« côte à côte », « l'un sous l'autre », un groupe, l'écran) ou une toile
    // (« en position libre : »). Un bloc posé sur une toile y est un widget placé, que sa fin remet à sa taille (§ 22.9).
    struct Niveau { QBoxLayout *boite; QWidget *toile; QWidget *place; int largeur, hauteur; QTabWidget *onglets = nullptr; };
    QVector<Niveau> niveaux = {{pile, nullptr, nullptr, 0, 0}};
    // Pose w dans le conteneur ouvert : sur une toile, à la place de e ; dans une boîte, à la suite.
    auto poser = [&](QWidget *w, const ElementVue &e, int etirement = 0) {
        const Niveau &ici = niveaux.last();
        if (!ici.toile) {
            QWidget *hote = ici.boite->parentWidget();
            if (e.sorte == ELEMENT_LIBRE && hote && ici.boite != pile) {   // dans un conteneur (page, groupe…) : il le remplit
                w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                ici.boite->addWidget(w, 1);
            } else if (e.sorte == ELEMENT_LIBRE) ici.boite->addWidget(w, 0, Qt::AlignLeft | Qt::AlignTop);
            else ici.boite->addWidget(w, etirement);
            return;
        }
        w->setParent(ici.toile);
        w->setProperty("place", true);
        w->setGeometry(QRect(QPoint(e.gauche, e.haut), taille_placee(w, e.largeur, e.hauteur)));
    };
    auto ouvrir = [&](QWidget *w, QBoxLayout *b, QWidget *toile, const ElementVue &e) {
        const bool sur_toile = niveaux.last().toile != nullptr;
        if (w) poser(w, e, 1);
        else niveaux.last().boite->addLayout(b, 1);
        niveaux.push_back({b, toile, sur_toile ? w : nullptr, e.largeur, e.hauteur});
    };
    QMenuBar *barre = nullptr;    // les menus de l'écran (§ 22.12), en tête, dans la vue et non dans la barre système
    QMenu *menu_ouvert = nullptr;
    for (int k = 0; k < n; k++) {
        const ElementVue &e = elements[k];
        if (e.sorte != ELEMENT_BOUTON || niveaux.last().toile) rangee = nullptr;
        if (e.sorte == ELEMENT_MENU) {   // « le menu « Membres » : » : un menu de la barre, en tête de l'écran (§ 22.12)
            if (!barre) {
                barre = new QMenuBar;
                barre->setNativeMenuBar(false);   // l'écran vit dans la fenêtre de l'atelier : sa barre reste à lui
                pile->insertWidget(0, barre);
            }
            menu_ouvert = barre->addMenu(e.texte);
            continue;
        }
        if (e.sorte == ELEMENT_FIN_DE_BLOC && menu_ouvert) { menu_ouvert = nullptr; continue; }
        if (e.sorte == ELEMENT_BOUTON && menu_ouvert) {
            // un élément de menu : une action, et un bouton invisible qu'elle clique. Le bouton garde le câblage d'un
            // bouton (son événement) et reçoit les états (actif si, visible si), que l'action reflète.
            QAction *action = menu_ouvert->addAction(e.texte);
            auto *b = new QPushButton(e.texte, barre);
            b->setFixedSize(0, 0);
            b->setProperty("element-de-menu", true);
            QObject::connect(action, &QAction::triggered, b, &QPushButton::click);
            b->installEventFilter(new MiroirDAction(action, b));
            r.boutons << b;
            r.controles[k] = b;
            continue;
        }
        if (e.sorte == ELEMENT_LIBRE) {
            auto *toile = new Toile;
            ouvrir(toile, nullptr, toile, e);
            r.controles[k] = toile;
            continue;
        }
        if (e.sorte == ELEMENT_COTE_A_COTE || e.sorte == ELEMENT_L_UN_SOUS_L_AUTRE) {
            QBoxLayout *b = e.sorte == ELEMENT_COTE_A_COTE ? static_cast<QBoxLayout *>(new QHBoxLayout)
                                                           : static_cast<QBoxLayout *>(new QVBoxLayout);
            b->setSpacing(t.mesure(e.sorte == ELEMENT_COTE_A_COTE ? "cote-a-cote" : "l-un-sous-l-autre"));
            QWidget *w = nullptr;
            if (niveaux.last().toile) {   // posé sur une toile : un widget qui porte la boîte
                w = new QWidget;
                w->setObjectName("bloc");
                b->setContentsMargins(0, 0, 0, 0);
                w->setLayout(b);
                r.controles[k] = w;
            }
            ouvrir(w, b, nullptr, e);
            continue;
        }
        if (e.sorte == ELEMENT_ONGLETS) {   // « les onglets : » : un widget d'onglets, placé comme un bloc (§ 22.10)
            auto *tw = new QTabWidget;
            tw->setDocumentMode(true);
            r.controles[k] = tw;
            const bool sur_toile = niveaux.last().toile != nullptr;
            poser(tw, e, 1);
            niveaux.push_back({nullptr, nullptr, sur_toile ? tw : nullptr, e.largeur, e.hauteur, tw});
            continue;
        }
        if (e.sorte == ELEMENT_ONGLET) {   // « l'onglet « Identité » : » : une page, ses éléments l'un sous l'autre
            auto *page = new QWidget;
            page->setProperty("page", true);
            auto *b = new QVBoxLayout(page);
            b->setSpacing(t.mesure("l-un-sous-l-autre"));
            b->setAlignment(Qt::AlignTop);
            if (QTabWidget *tw = niveaux.last().onglets) tw->addTab(page, e.texte);
            r.controles[k] = page;
            niveaux.push_back({b, nullptr, nullptr, 0, 0});
            continue;
        }
        if (e.sorte == ELEMENT_GROUPE) {   // « le groupe « Adresse » : » : un cadre titré, ses éléments l'un sous l'autre (§ 22.7)
            auto *cadre = new QGroupBox(e.texte);
            auto *b = new QVBoxLayout(cadre);
            b->setSpacing(t.mesure("l-un-sous-l-autre"));
            r.controles[k] = cadre;
            if (niveaux.last().toile) ouvrir(cadre, b, nullptr, e);
            else { niveaux.last().boite->addWidget(cadre); niveaux.push_back({b, nullptr, nullptr, 0, 0}); }
            continue;
        }
        if (e.sorte == ELEMENT_FIN_DE_BLOC) {
            if (niveaux.size() <= 1) continue;
            const Niveau fin = niveaux.takeLast();
            if (fin.toile) fin.toile->updateGeometry();
            if (fin.place) {   // rempli : sa taille naturelle est connue maintenant
                if (fin.boite) fin.boite->activate();
                if (fin.onglets) fin.onglets->adjustSize();
                fin.place->resize(taille_placee(fin.place, fin.largeur, fin.hauteur));
            }
            if (niveaux.last().toile) niveaux.last().toile->updateGeometry();
            continue;
        }
        QBoxLayout *ici = niveaux.last().boite;
        QWidget *const toile = niveaux.last().toile;
        auto poser_ici = [&](QWidget *w, int etirement = 0) { poser(w, e, etirement); };
#define poser poser_ici
        if (e.sorte == ELEMENT_IMAGE) {   // « l'image « plan.png » » (§ 22.8) : sa taille, sans dépasser 480 × 320
            auto *l = new QLabel;
            montrer_image(l, e.octets, e.texte, e.largeur, e.hauteur);   // sans le fichier : son nom
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
            if (e.multiple) {   // des choix multiples : une case par objet (§ 22.1) ; l'aperçu, sans données, en montre une
                w = new QWidget;
                w->setProperty("multiple", true);
                auto *f = new QHBoxLayout(w);
                f->setContentsMargins(0, 0, 0, 0);
                f->setSpacing(t.mesure("espace-4"));
                const QStringList choix = e.choix.isEmpty() ? QStringList{QString::fromUtf8("…")} : e.choix;
                for (const QString &c : choix) {
                    auto *b = new QCheckBox(c);
                    b->setProperty("cle", c);
                    f->addWidget(b);
                }
                f->addStretch(1);
            }
            else if (e.type == "vrai ou faux" && e.interrupteur) w = new Interrupteur(e.texte);   // § 22.6
            else if (e.type == "vrai ou faux") w = new QCheckBox(e.texte);   // la case porte son libellé : un clic dessus la coche
            else if (e.type == "fichier" || e.type == "image") {   // une zone de fichier ou d'image : choisir un fichier
                auto *b = new QPushButton(QString::fromUtf8("Choisir un fichier…"));
                b->setProperty("fichier", true);
                b->setProperty("image", e.type == "image");
                w = b;
            }
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
            } else if (!e.choix.isEmpty() || e.recherche) {
                auto *m = new QComboBox;
                m->addItem(QString());
                m->addItems(e.choix);
                if (e.recherche || e.choix.size() > RECHERCHE_AU_DELA) {
                    // « avec recherche », ou beaucoup d'objets : on tape, les objets qui contiennent la saisie sont proposés
                    m->setEditable(true);
                    m->setInsertPolicy(QComboBox::NoInsert);
                    m->completer()->setFilterMode(Qt::MatchContains);
                    m->completer()->setCompletionMode(QCompleter::PopupCompletion);
                    m->completer()->setCaseSensitivity(Qt::CaseInsensitive);
                    m->lineEdit()->setPlaceholderText(QString::fromUtf8("Tape pour chercher…"));
                    m->setProperty("recherche", true);
                }
                w = m;
            } else if (e.plusieurs_lignes) {   // « sur plusieurs lignes » (§ 22.6) : Tab passe à la suite, Entrée va à la ligne
                auto *p = new QPlainTextEdit;
                p->setTabChangesFocus(true);
                p->setMinimumHeight(p->fontMetrics().lineSpacing() * 4 + 2 * p->frameWidth() + 8);
                if (e.hauteur <= 0)   // une hauteur dite (§ 22.9) se prend entière ; sinon, huit lignes au plus
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
            if (auto *le = qobject_cast<QLineEdit *>(w)) {   // courriel, téléphone, adresse web : le bon clavier (§ 16.1)
                if (e.type == "courriel") le->setInputMethodHints(Qt::ImhEmailCharactersOnly);
                else if (e.type == QString::fromUtf8("téléphone")) le->setInputMethodHints(Qt::ImhDialableCharactersOnly);
                else if (e.type == "adresse web") le->setInputMethodHints(Qt::ImhUrlCharactersOnly);
            }
            if (e.masque)   // « masqué » (§ 22.6) : la frappe cachée
                if (auto *le = qobject_cast<QLineEdit *>(w)) {
                    le->setEchoMode(QLineEdit::Password);
                    le->setProperty("masque", true);
                }
            if (e.longueur_max > 0) {   // « au plus N caractères » (§ 22.6) : la frappe s'arrête là
                if (auto *le = qobject_cast<QLineEdit *>(w)) le->setMaxLength(e.longueur_max);
            }
            if (!e.indication.isEmpty()) {   // l'indication (§ 22.6) : plus petite, plus pâle, sous le libellé
                auto *ind = new QLabel(e.indication);
                ind->setObjectName("indication");
                ind->setWordWrap(true);
                QFont f = ind->font();
                f.setPointSizeF(f.pointSizeF() * 0.9);
                ind->setFont(f);
                QPalette pal = ind->palette();
                QColor c = pal.color(QPalette::WindowText);
                c.setAlphaF(0.7);
                pal.setColor(QPalette::WindowText, c);
                ind->setPalette(pal);
                ind->setStyleSheet(QString("font-size: %1px;").arg(qRound(QFontInfo(f).pixelSize() * 0.9)));
                rang->addWidget(ind);
            }
            rang->addWidget(w);
            poser(conteneur);
            r.controles[k] = conteneur;
        } else if (e.sorte == ELEMENT_GRAPHIQUE) {   // « le graphique des tâches conservées, par état » (§ 22.11)
            auto *gr = new Graphique(e.graphique & 2);
            r.controles[k] = gr;
            poser(gr, 1);
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
#undef poser
    r.erreur = new QLabel;
    r.erreur->setProperty("message", "erreur");   // le cadre du message d'erreur, selon la feuille du thème
    r.erreur->setWordWrap(true);
    r.erreur->hide();
    pile->insertWidget(1, r.erreur);   // sous le titre : l'utilisateur le voit avant de chercher ce qui a raté
    return r;
}
