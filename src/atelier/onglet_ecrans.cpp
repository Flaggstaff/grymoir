// GrymoiR : l'atelier, l'onglet « Écrans ».
#include "onglet_ecrans.h"
#include "theme.h"
#include "schema.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDirIterator>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QShortcut>
#include <QApplication>
#include <QDrag>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFrame>
#include <QMimeData>
#include <QMouseEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <functional>

extern "C" {
#include "analyseur.h"
#include "interface.h"
}

namespace {

QString capitale(const QString &s) { return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1); }

// Le titre qu'un écran prend de son nom, comme à l'exécution : « des compositeurs » → « Compositeurs ».
QString titre_du_nom(const QString &nom) {
    for (const char *d : {"des ", "du ", "de la ", "de l'", "de ", "d'"}) {
        const QString x = QString::fromUtf8(d);
        if (nom.startsWith(x) && nom.size() > x.size()) return capitale(nom.mid(x.size()));
    }
    return capitale(nom);
}

}  // namespace

QVector<EcranLu> lire_ecrans(const QString &dossier, QStringList *problemes) {
    QMap<QString, EntiteSchema> entites;   // pour les colonnes par défaut d'une liste
    for (const auto &e : lire_schema(dossier)) entites.insert(e.nom, e);
    QVector<EcranLu> r;
    QSet<QString> vus;
    struct Evenement { QString ecran, objet, fichier, ecrit; int forme, ligne; };
    QVector<Evenement> evenements;
    QDirIterator it(dossier, {"*.grym", "*.grymc"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList fichiers;
    while (it.hasNext()) fichiers << QFileInfo(it.next()).absoluteFilePath();
    fichiers.sort();
    for (const QString &chemin : fichiers) {
        QFile f(chemin);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray source = f.readAll(), c = chemin.toUtf8();
        const QString texte = QString::fromUtf8(source);
        QVector<int> utf16;   // point de code k → position dans `texte`
        for (int i = 0; i < texte.size(); i++) {
            utf16 << i;
            if (texte.at(i).isHighSurrogate() && i + 1 < texte.size()) i++;
        }
        utf16 << texte.size();
        Portee *portee = portee_creer();
        portee_fichier(portee, c.constData());
        Programme p = {};
        Diagnostic d = {};
        const bool compacte = chemin.endsWith(".grymc", Qt::CaseInsensitive);
        const int ok = compacte ? analyser_compact(source.constData(), (size_t)source.size(), portee, &p, &d)
                                : analyser(source.constData(), (size_t)source.size(), portee, 0, &p, &d);
        portee_detruire(portee);
        if (!ok) {
            if (problemes) *problemes << QDir(dossier).relativeFilePath(chemin);
            diagnostic_liberer(&d);
            continue;
        }
        auto extrait = [&](const Noeud *n) {   // le texte de l'élément dans son propre fichier
            const int a = utf16.value((int)qMin(n->debut, (size_t)utf16.size() - 1));
            const int b = utf16.value((int)qMin(n->fin, (size_t)utf16.size() - 1));
            return b > a ? texte.mid(a, b - a).simplified() : QString();
        };
        std::function<void(Noeud *const *, size_t, const QString &, bool)> lire;
        lire = [&](Noeud *const *phrases, size_t nb, const QString &fichier, bool ici) {
            for (size_t k = 0; k < nb; k++) {
                const Noeud *n = phrases[k];
                if (n->type == P_UTILISER) {
                    if (n->entier > 0 && (size_t)n->entier <= p.nb_fichiers)
                        lire(n->enfants, n->nb_enfants, QFileInfo(QString::fromUtf8(p.fichiers[n->entier - 1])).absoluteFilePath(), false);
                    continue;
                }
                const QString cle = fichier + ":" + QString::number(n->ligne);
                if (n->type == P_QUAND) {
                    if (vus.contains(cle)) continue;
                    vus.insert(cle);
                    const int a = utf16.value((int)qMin(n->debut, (size_t)utf16.size() - 1));
                    const int b = k + 1 < nb ? utf16.value((int)qMin(phrases[k + 1]->debut, (size_t)utf16.size() - 1)) : texte.size();
                    evenements.push_back({QString::fromUtf8(n->texte3), QString::fromUtf8(n->enfants[2]->texte), fichier,
                                          ici && b > a ? texte.mid(a, b - a).trimmed() : QString(), n->forme, n->ligne});
                    continue;
                }
                if (n->type != P_ECRAN || vus.contains(cle)) continue;
                vus.insert(cle);
                EcranLu e;
                e.nom = QString::fromUtf8(n->texte);
                e.titre = n->texte2 ? QString::fromUtf8(n->texte2) : titre_du_nom(e.nom);
                e.fichier = fichier;
                e.ligne = n->ligne;
                for (size_t q = 0; q < n->nb_enfants; q++) {
                    const Noeud *el = n->enfants[q];
                    ElementLu x;
                    if (ici) x.ecrit = extrait(el);   // un fichier utilisé se lit ailleurs, avec ses propres positions
                    if (el->type == N_DISPOSITION) {
                        x.sorte = el->forme == 1 ? ELEMENT_COTE_A_COTE : el->forme == 2 ? ELEMENT_L_UN_SOUS_L_AUTRE : ELEMENT_FIN_DE_BLOC;
                        x.ecrit.clear();
                    } else if (el->type == N_BOUTON) {
                        x.sorte = ELEMENT_BOUTON;
                        x.texte = QString::fromUtf8(el->texte);
                    } else if (el->type == N_TEXTE_ECRAN) {
                        x.sorte = ELEMENT_TEXTE;
                        x.texte = QString::fromUtf8(el->texte);
                    } else if (el->type == N_NOM) {
                        x.sorte = ELEMENT_ZONE;
                        x.texte = QString::fromUtf8(el->texte);
                        x.type = QString::fromUtf8(el->texte2);
                        x.facultatif = el->entier & 1;
                        if (el->nb_enfants) {
                            const Noeud *v = el->enfants[0];
                            x.depart = QString::fromUtf8(v->texte ? v->texte
                                                         : v->nb_enfants && v->enfants[0]->texte ? v->enfants[0]->texte : "");
                            if (v->type == N_NEGATION) x.depart = "−" + x.depart;
                        }
                    } else if (el->type == N_CHERCHER) {
                        x.sorte = ELEMENT_LISTE;
                        x.texte = QString::fromUtf8(el->texte);
                        x.tri = el->texte2 ? QString::fromUtf8(el->texte2) : QString();
                        x.decroissant = el->entier == 1;
                        x.colonnes_choisies = el->texte3 != nullptr;
                        if (el->texte3) {   // les colonnes choisies : « écrit ␝ chemin », séparées par ␞
                            for (const QString &col : QString::fromUtf8(el->texte3).split(QChar(0x1e))) {
                                QString ecrit = col.section(QChar(0x1d), 0, 0);
                                for (const char *a : {"le ", "la ", "l'"})
                                    if (ecrit.startsWith(QString::fromUtf8(a))) { ecrit = ecrit.mid(QString::fromUtf8(a).size()); break; }
                                x.colonnes << capitale(ecrit);
                            }
                        } else {   // par défaut : les champs simples, sans fichiers, images ni « plusieurs »
                            for (const auto &ch : entites.value(x.texte).champs)
                                if (!ch.multiple && ch.type != "fichier" && ch.type != "image") x.colonnes << capitale(ch.nom);
                        }
                    }
                    e.elements << x;
                }
                r << e;
            }
        };
        lire(p.phrases, p.nb, chemin, true);
        programme_liberer(&p);
    }
    for (auto &e : r)   // l'événement de chaque élément
        for (auto &x : e.elements)
            for (const auto &v : evenements) {
                const int forme = x.sorte == ELEMENT_BOUTON ? 1 : x.sorte == ELEMENT_LISTE ? 2 : x.sorte == ELEMENT_ZONE ? 3 : 0;
                if (forme && v.forme == forme && v.ecran == e.nom && v.objet == x.texte) {
                    x.evenement_fichier = v.fichier;
                    x.evenement_ligne = v.ligne;
                    x.evenement_ecrit = v.ecrit;
                }
            }
    return r;
}

// --------------------------------------------------------------------------------------------

OngletEcrans::OngletEcrans(QWidget *parent) : QWidget(parent) {
    liste = new QListWidget;
    centre = new QScrollArea;
    centre->setWidgetResizable(true);
    panneau = new QTextBrowser;
    voir_evenement = new QPushButton("Voir l'événement");
    voir_declaration = new QPushButton("Voir la déclaration");
    etat = new QLabel;
    etat->setWordWrap(true);
    auto *droite = new QWidget;
    auto *pd = new QVBoxLayout(droite);
    pd->setContentsMargins(0, 0, 0, 0);
    pd->addWidget(panneau, 1);
    edition = new QWidget;   // les propriétés modifiables de l'élément choisi (A4-c)
    new QVBoxLayout(edition);
    edition->layout()->setContentsMargins(0, 0, 0, 0);
    pd->addWidget(edition);
    pd->addWidget(voir_evenement);
    pd->addWidget(voir_declaration);
    auto *gauche = new QWidget;   // les écrans, et de quoi en créer (A4-b)
    auto *pg = new QVBoxLayout(gauche);
    pg->setContentsMargins(0, 0, 0, 0);
    auto *b_generer = new QPushButton("Écran pour une entité…");
    auto *b_vide = new QPushButton("Nouvel écran vide");
    connect(b_generer, &QPushButton::clicked, this, &OngletEcrans::generer);
    connect(b_vide, &QPushButton::clicked, this, &OngletEcrans::ecran_vide);
    pg->addWidget(b_generer);
    pg->addWidget(b_vide);
    pg->addWidget(liste, 1);
    auto *palette = new QWidget;   // ajouter un élément à la fin de l'écran (A4-c)
    auto *pp = new QHBoxLayout(palette);
    pp->setContentsMargins(0, 0, 0, 0);
    pp->addWidget(new QLabel("Ajouter :"));
    for (int sorte : {ELEMENT_BOUTON, ELEMENT_TEXTE, ELEMENT_ZONE, ELEMENT_LISTE}) {
        auto *b = new QPushButton(sorte == ELEMENT_BOUTON ? "Bouton" : sorte == ELEMENT_TEXTE ? "Texte"
                                  : sorte == ELEMENT_ZONE ? "Zone" : "Liste");
        connect(b, &QPushButton::clicked, this, [this, sorte] { ajouter(sorte); });
        pp->addWidget(b);
    }
    pp->addStretch(1);
    auto *milieu = new QWidget;
    auto *pm = new QVBoxLayout(milieu);
    pm->setContentsMargins(0, 0, 0, 0);
    pm->addWidget(palette);
    pm->addWidget(centre, 1);
    auto *suppr = new QShortcut(QKeySequence::Delete, this);
    connect(suppr, &QShortcut::activated, this, &OngletEcrans::supprimer);
    auto *partage = new QSplitter;
    partage->addWidget(gauche);
    partage->addWidget(milieu);
    partage->addWidget(droite);
    partage->setStretchFactor(1, 1);
    partage->setSizes({180, 620, 300});
    auto *pile = new QVBoxLayout(this);
    pile->setContentsMargins(0, 0, 0, 0);
    pile->addWidget(partage, 1);
    pile->addWidget(etat);
    connect(liste, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r < 0 || r >= lus.size() || r == courant) return;
        courant = r;
        element = -1;
        dessiner();
    });
    connect(voir_evenement, &QPushButton::clicked, this, [this] {
        if (courant < 0 || element < 0) return;
        const ElementLu &x = lus[courant].elements[element];
        if (x.evenement_ligne) emit ouvrir(x.evenement_fichier, x.evenement_ligne);
    });
    connect(voir_declaration, &QPushButton::clicked, this, [this] {
        if (courant >= 0) emit ouvrir(lus[courant].fichier, lus[courant].ligne);
    });
    dessiner();
}

void OngletEcrans::montrer(const QString &d) {
    dossier = d;
    const QString avant = courant >= 0 && courant < lus.size() ? lus[courant].nom : QString();
    const int element_avant = element;
    QStringList problemes;
    lus = lire_ecrans(dossier, &problemes);
    liste->blockSignals(true);
    liste->clear();
    courant = lus.isEmpty() ? -1 : 0;
    for (int k = 0; k < lus.size(); k++) {
        liste->addItem(lus[k].titre);
        liste->item(k)->setToolTip(QString("L'écran %1\n%2, ligne %3").arg(lus[k].nom, QFileInfo(lus[k].fichier).fileName())
                                       .arg(lus[k].ligne));
        if (lus[k].nom == avant) courant = k;
    }
    if (courant >= 0) liste->setCurrentRow(courant);
    liste->blockSignals(false);
    element = lus.value(courant).nom == avant ? element_avant : -1;
    QString t = lus.isEmpty() ? QString("Aucun écran dans ce projet.")
                              : QString("%1 écran%2. Cliquez un élément de l'aperçu pour voir ses propriétés.")
                                    .arg(lus.size()).arg(lus.size() > 1 ? "s" : "");
    if (!problemes.isEmpty()) t += "\nNon lus, car ils contiennent une erreur : " + problemes.join(", ") + ".";
    etat->setText(t);
    dessiner();
}

void OngletEcrans::choisir_ecran(const QString &nom) {
    for (int k = 0; k < lus.size(); k++)
        if (lus[k].nom == nom) liste->setCurrentRow(k);
}

void OngletEcrans::dessiner() {
    if (courant < 0 || courant >= lus.size()) {
        centre->setWidget(new QLabel("Aucun écran à montrer."));
        vue = VueEcran();
        choisir_element(-1);
        return;
    }
    const EcranLu &e = lus[courant];
    QVector<ElementVue> el(e.elements.size());
    for (int k = 0; k < e.elements.size(); k++) {
        el[k].sorte = e.elements[k].sorte;
        el[k].texte = e.elements[k].sorte == ELEMENT_ZONE ? capitale(e.elements[k].texte) : e.elements[k].texte;
        el[k].colonnes = e.elements[k].colonnes;
        el[k].type = e.elements[k].type;
    }
    vue = dessiner_ecran(e.titre, el);   // le même dessin que l'exécution
    for (int k = 0; k < vue.controles.size(); k++) {
        QWidget *w = vue.controles[k];
        if (!w) continue;
        w->installEventFilter(this);   // un clic choisit l'élément, sans rien déclencher ; glisser le déplace (A4-d)
        w->setAcceptDrops(true);
        for (QWidget *enfant : w->findChildren<QWidget *>()) {
            enfant->installEventFilter(this);
            enfant->setAcceptDrops(true);
        }
        if (auto *t = vue.tables[k]) {   // la structure, pas les données : deux lignes grisées d'exemple
            t->setRowCount(2);
            for (int r = 0; r < 2; r++)
                for (int c = 0; c < t->columnCount(); c++) {
                    auto *i = new QTableWidgetItem("…");
                    i->setForeground(Theme::courant().couleur("texte-2"));
                    t->setItem(r, c, i);
                }
        }
    }
    vue.titre->installEventFilter(this);
    repere = new QFrame(vue.vue);
    repere->setStyleSheet("background: " + Theme::courant().hex("accent") + ";");
    repere->hide();
    centre->setWidget(vue.vue);
    choisir_element(element < e.elements.size() ? element : -1);
}

int OngletEcrans::element_sous(QObject *o) const {
    int trouve = -1;
    for (int k = 0; k < vue.controles.size(); k++) {
        QWidget *w = vue.controles[k];
        if (w && (o == w || w->isAncestorOf(qobject_cast<QWidget *>(o)))) trouve = k;
    }
    return trouve;
}

int OngletEcrans::cote_de_depot(const QSize &t, const QPointF &p) {
    if (p.x() < t.width() * 0.25) return DEPOT_GAUCHE;
    if (p.x() > t.width() * 0.75) return DEPOT_DROITE;
    return p.y() < t.height() / 2.0 ? DEPOT_AVANT : DEPOT_APRES;
}

void OngletEcrans::deposer(int source, int cible, int cote) {
    if (courant < 0 || source < 0 || cible < 0 || source == cible) return;
    const QString ecran = lus[courant].nom;
    element = -1;
    emit geste([=](Geste *g) { return ecran_deplacer(dossier, ecran, source, cible, cote, g); });
}

bool OngletEcrans::eventFilter(QObject *o, QEvent *ev) {
    // A4-d : glisser un élément de l'aperçu ; un trait montre où il atterrira
    if (ev->type() == QEvent::MouseMove) {
        auto *m = static_cast<QMouseEvent *>(ev);
        if (glisse >= 0 && (m->buttons() & Qt::LeftButton)
            && (m->globalPosition().toPoint() - depart).manhattanLength() >= QApplication::startDragDistance()) {
            auto *d = new QDrag(this);
            auto *mime = new QMimeData;
            mime->setData("application/x-grymoir-element", QByteArray::number(glisse));
            d->setMimeData(mime);
            if (QWidget *w = vue.controles.value(glisse)) d->setPixmap(w->grab().scaledToWidth(qMin(240, w->width())));
            glisse = -1;
            d->exec(Qt::MoveAction);
            if (repere) repere->hide();
        }
        return true;
    }
    if (ev->type() == QEvent::DragEnter || ev->type() == QEvent::DragMove) {
        auto *d = static_cast<QDropEvent *>(ev);
        const int k = element_sous(o);
        QWidget *w = vue.controles.value(k);
        if (!d->mimeData()->hasFormat("application/x-grymoir-element") || !w) { ev->ignore(); return true; }
        const QPointF p = w->mapFrom(qobject_cast<QWidget *>(o), d->position().toPoint());
        depot_cible = k;
        depot_cote = cote_de_depot(w->size(), p);
        const QRect r(w->mapTo(vue.vue, QPoint(0, 0)), w->size());
        repere->setGeometry(depot_cote == DEPOT_GAUCHE ? QRect(r.left() - 2, r.top(), 4, r.height())
                            : depot_cote == DEPOT_DROITE ? QRect(r.right() - 1, r.top(), 4, r.height())
                            : depot_cote == DEPOT_AVANT ? QRect(r.left(), r.top() - 2, r.width(), 4)
                                                        : QRect(r.left(), r.bottom() - 1, r.width(), 4));
        repere->show();
        repere->raise();
        d->acceptProposedAction();
        return true;
    }
    if (ev->type() == QEvent::DragLeave) {
        if (repere) repere->hide();
        return true;
    }
    if (ev->type() == QEvent::Drop) {
        auto *d = static_cast<QDropEvent *>(ev);
        if (repere) repere->hide();
        const int k = element_sous(o);
        if (!d->mimeData()->hasFormat("application/x-grymoir-element") || k < 0) return true;
        QWidget *w = vue.controles.value(k);
        const int cote = cote_de_depot(w->size(), w->mapFrom(qobject_cast<QWidget *>(o), d->position().toPoint()));
        d->acceptProposedAction();
        deposer(d->mimeData()->data("application/x-grymoir-element").toInt(), k, cote);
        return true;
    }
    if (ev->type() != QEvent::MouseButtonPress && ev->type() != QEvent::MouseButtonDblClick
        && ev->type() != QEvent::MouseButtonRelease && ev->type() != QEvent::KeyPress)
        return QWidget::eventFilter(o, ev);
    if (ev->type() == QEvent::MouseButtonPress) {
        const int trouve = element_sous(o);
        glisse = trouve;   // peut-être le début d'un glisser
        depart = static_cast<QMouseEvent *>(ev)->globalPosition().toPoint();
        choisir_element(trouve);
    }
    if (ev->type() == QEvent::MouseButtonRelease) glisse = -1;
    return true;   // l'aperçu ne s'utilise pas : ni clic, ni saisie, ni double-clic
}

void OngletEcrans::choisir_element(int k) {
    for (QWidget *w : vue.controles)
        if (w) w->setStyleSheet(QString());
    element = k;
    if (courant < 0 || courant >= lus.size()) {
        panneau->setHtml("<p>Aucun écran.</p>");
        voir_evenement->setEnabled(false);
        voir_declaration->setEnabled(false);
        return;
    }
    const EcranLu &e = lus[courant];
    voir_declaration->setEnabled(true);
    auto ligne = [](const QString &nom, const QString &valeur) {
        return "<tr><td style='color:" + Theme::courant().hex("texte-2") + "'>" + nom.toHtmlEscaped() + "</td><td>" + valeur.toHtmlEscaped() + "</td></tr>";
    };
    QString h;
    if (k < 0 || k >= e.elements.size()) {
        h = "<p><b>Écran</b></p><table>" + ligne("Nom", "l'écran " + e.nom) + ligne("Titre", e.titre)
          + ligne("Fichier", QFileInfo(e.fichier).fileName() + ", ligne " + QString::number(e.ligne))
          + ligne("Éléments", QString::number(e.elements.size())) + "</table>";
        voir_evenement->setEnabled(false);
        panneau->setHtml(h);
        return;
    }
    const ElementLu &x = e.elements[k];
    if (QWidget *w = vue.controles.value(k)) w->setStyleSheet("border: 2px solid " + Theme::courant().hex("accent") + ";");
    static const char *const noms[] = {"Liste", "Bouton", "Texte", "Zone de saisie", "Côte à côte", "L'un sous l'autre", "Fin de bloc"};
    h = QString("<p><b>%1</b></p><table>").arg(noms[qBound(0, x.sorte, 6)]);
    if (x.sorte == ELEMENT_BOUTON) h += ligne("Libellé", x.texte);
    else if (x.sorte == ELEMENT_TEXTE) h += ligne("Texte", x.texte);
    else if (x.sorte == ELEMENT_ZONE) {
        h += ligne("Nom", x.texte) + ligne("Type", x.type) + ligne("Facultative", x.facultatif ? "oui" : "non");
        if (!x.depart.isEmpty()) h += ligne("Au départ", x.depart);
    } else if (x.sorte == ELEMENT_LISTE) {
        h += ligne("Entité", x.texte) + ligne("Colonnes", x.colonnes.join(", "));
        h += ligne("Tri", x.tri.isEmpty() ? QString("ordre de conservation") : x.tri + (x.decroissant ? ", décroissant" : ""));
    }
    h += ligne("Événement", x.evenement_ligne ? QFileInfo(x.evenement_fichier).fileName() + ", ligne "
                                                 + QString::number(x.evenement_ligne)
                                               : QString(x.sorte == ELEMENT_BOUTON || x.sorte == ELEMENT_LISTE
                                                         || x.sorte == ELEMENT_ZONE ? "aucun" : "sans objet"));
    h += "</table>";
    if (!x.ecrit.isEmpty()) h += "<p style='color:" + Theme::courant().hex("texte-2") + "'>En GrymoiR :</p><pre>" + x.ecrit.toHtmlEscaped() + "</pre>";
    panneau->setHtml(h);
    voir_evenement->setEnabled(x.evenement_ligne > 0);
    editer();
}

// --- A4-c : modifier ---

void OngletEcrans::editer() {
    QLayout *pile = edition->layout();
    while (QLayoutItem *i = pile->takeAt(0)) { delete i->widget(); delete i; }
    if (courant < 0 || courant >= lus.size()) return;
    const EcranLu &e = lus[courant];
    const QString ecran = e.nom;
    auto *cadre = new QWidget;
    auto *f = new QFormLayout(cadre);
    f->setContentsMargins(0, 0, 0, 0);
    auto *appliquer = new QPushButton("Appliquer");
    appliquer->setProperty("role", "principal");
    auto *retirer = new QPushButton(element >= 0 ? "Supprimer" : "");
    retirer->setProperty("role", "danger");
    retirer->setVisible(element >= 0);
    connect(retirer, &QPushButton::clicked, this, &OngletEcrans::supprimer);
    if (element < 0 || element >= e.elements.size()) {   // l'écran lui-même : son titre
        auto *titre = new QLineEdit(e.titre);
        f->addRow("Titre", titre);
        connect(appliquer, &QPushButton::clicked, this, [this, ecran, titre] {
            const QString t = titre->text();
            emit geste([=](Geste *g) { return ecran_titre(dossier, ecran, t, g); });
        });
    } else {
        const ElementLu &x = e.elements[element];
        const int k = element;
        if (x.sorte == ELEMENT_BOUTON || x.sorte == ELEMENT_TEXTE) {
            auto *texte = new QLineEdit(x.texte);
            f->addRow(x.sorte == ELEMENT_BOUTON ? "Libellé" : "Texte", texte);
            const int sorte = x.sorte;
            connect(appliquer, &QPushButton::clicked, this, [this, ecran, k, texte, sorte] {
                ElementNouveau n;
                n.sorte = sorte;
                n.texte = texte->text();
                emit geste([=](Geste *g) { return ecran_modifier(dossier, ecran, k, n, g); });
            });
        } else if (x.sorte == ELEMENT_ZONE) {
            auto *nom = new QLineEdit(x.texte);
            auto *type = new QComboBox;
            type->addItems({"texte", "nombre", "nombre entier", "vrai ou faux", "date", "année"});
            for (const auto &en : lire_schema(dossier)) type->addItem(en.nom);
            type->setCurrentIndex(qMax(0, type->findText(x.type)));
            auto *feminin = new QCheckBox("féminin (une …)");
            auto *facultative = new QCheckBox("facultative");
            facultative->setChecked(x.facultatif);
            auto *depart = new QLineEdit(x.depart);
            f->addRow("Nom", nom);
            f->addRow("Type", type);
            f->addRow("", feminin);
            f->addRow("", facultative);
            f->addRow("Au départ", depart);
            connect(appliquer, &QPushButton::clicked, this, [=] {
                ElementNouveau n;
                n.sorte = ELEMENT_ZONE;
                n.texte = nom->text();
                n.type = type->currentText();
                n.feminin = feminin->isChecked();
                n.facultatif = facultative->isChecked();
                n.depart = depart->text();
                emit geste([=](Geste *g) { return ecran_modifier(dossier, ecran, k, n, g); });
            });
        } else if (x.sorte == ELEMENT_LISTE) {
            EntiteSchema en;
            for (const auto &y : lire_schema(dossier)) if (y.nom == x.texte) en = y;
            auto *tri = new QComboBox;
            tri->addItem("(ordre de conservation)");
            auto *decroissant = new QCheckBox("décroissant");
            decroissant->setChecked(x.decroissant);
            auto *colonnes = new QListWidget;   // cochées : les colonnes choisies ; aucune : les colonnes par défaut
            for (const auto &c : en.champs) {
                if (c.multiple || c.type == "fichier" || c.type == "image") continue;
                tri->addItem(c.nom);
                auto *i = new QListWidgetItem(c.nom, colonnes);
                const QString titre = c.nom.left(1).toUpper() + c.nom.mid(1);
                i->setCheckState(x.colonnes_choisies && x.colonnes.contains(titre) ? Qt::Checked : Qt::Unchecked);
            }
            tri->setCurrentIndex(qMax(0, tri->findText(x.tri)));
            colonnes->setMaximumHeight(110);
            f->addRow("Tri", tri);
            f->addRow("", decroissant);
            f->addRow("Colonnes", colonnes);
            connect(appliquer, &QPushButton::clicked, this, [=] {
                ElementNouveau n;
                n.sorte = ELEMENT_LISTE;
                n.tri = tri->currentIndex() > 0 ? tri->currentText() : QString();
                n.decroissant = decroissant->isChecked();
                for (int r = 0; r < colonnes->count(); r++)
                    if (colonnes->item(r)->checkState() == Qt::Checked) n.colonnes << colonnes->item(r)->text();
                emit geste([=](Geste *g) { return ecran_modifier(dossier, ecran, k, n, g); });
            });
        } else {   // un bloc : seulement le retirer
            appliquer->hide();
            retirer->setText("Retirer le bloc (ses éléments restent)");
        }
    }
    pile->addWidget(cadre);
    auto *boutons = new QWidget;
    auto *h = new QHBoxLayout(boutons);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(retirer);
    h->addStretch(1);
    h->addWidget(appliquer);
    pile->addWidget(boutons);
}

void OngletEcrans::supprimer() {
    if (courant < 0 || element < 0 || element >= lus[courant].elements.size()) return;
    const ElementLu &x = lus[courant].elements[element];
    const QString ecran = lus[courant].nom;
    const int k = element;
    // un événement part avec son élément : on montre d'abord le code qui disparaîtra (docs/atelier.md, § 5 bis)
    if (!x.evenement_ecrit.isEmpty()
        && QMessageBox::question(this, "Supprimer", "Cet élément part avec son événement :\n\n" + x.evenement_ecrit
                                                        + "\n\nLe supprimer ?") != QMessageBox::Yes)
        return;
    element = -1;
    emit geste([=](Geste *g) { return ecran_supprimer(dossier, ecran, k, g); });
}

void OngletEcrans::ajouter(int sorte) {
    if (courant < 0 || courant >= lus.size()) return;
    const QString ecran = lus[courant].nom;
    ElementNouveau n;
    n.sorte = sorte;
    bool ok = false;
    if (sorte == ELEMENT_BOUTON) n.texte = QInputDialog::getText(this, "Nouveau bouton", "Libellé :", QLineEdit::Normal, "", &ok);
    else if (sorte == ELEMENT_TEXTE) n.texte = QInputDialog::getText(this, "Nouveau texte", "Texte :", QLineEdit::Normal, "", &ok);
    else if (sorte == ELEMENT_ZONE) {
        n.texte = QInputDialog::getText(this, "Nouvelle zone", "Nom (« pays », « date de début ») :", QLineEdit::Normal, "", &ok);
        if (ok) {
            QStringList types = {"texte", "nombre", "nombre entier", "vrai ou faux", "date", "année"};
            for (const auto &en : lire_schema(dossier)) types << en.nom;
            n.type = QInputDialog::getItem(this, "Nouvelle zone", "Type :", types, 0, false, &ok);
        }
    } else {
        QStringList entites;
        for (const auto &en : lire_schema(dossier)) entites << en.nom;
        if (entites.isEmpty()) return;
        n.texte = QInputDialog::getItem(this, "Nouvelle liste", "Entité :", entites, 0, false, &ok);
    }
    if (!ok) return;
    emit geste([=](Geste *g) { return ecran_ajouter(dossier, ecran, n, g); });
}

QString OngletEcrans::proprietes() const { return panneau->toPlainText(); }
