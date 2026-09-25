// GrymoiR : l'atelier, le schéma des données.
#include "schema.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QtMath>

#include <cstdlib>
#include <functional>

extern "C" {
#include "analyseur.h"
#include "compilateur.h"
#include "vm.h"
}

// ---------------------------------------------------------------------------------------------
// Lecture : les entités dans l'arbre (P_CLASSE conservées, § 16), y compris dans les fichiers utilisés (§ 21)
// ---------------------------------------------------------------------------------------------

QVector<EntiteSchema> lire_schema(const QString &dossier, QStringList *problemes) {
    QMap<QString, EntiteSchema> par_nom;
    QSet<QString> vues;   // « fichier:ligne » d'une déclaration déjà lue (un fichier utilisé se relit ailleurs)
    QDirIterator it(dossier, {"*.grym", "*.grymc"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList fichiers;
    while (it.hasNext()) fichiers << QFileInfo(it.next()).absoluteFilePath();
    fichiers.sort();
    for (const QString &chemin : fichiers) {
        QFile f(chemin);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray source = f.readAll(), c = chemin.toUtf8();
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
        std::function<void(Noeud *const *, size_t, const QString &)> lire;
        lire = [&](Noeud *const *phrases, size_t nb, const QString &fichier) {
            for (size_t k = 0; k < nb; k++) {
                const Noeud *n = phrases[k];
                if (n->type == P_UTILISER) {
                    if (n->entier > 0 && (size_t)n->entier <= p.nb_fichiers)
                        lire(n->enfants, n->nb_enfants,
                             QFileInfo(QString::fromUtf8(p.fichiers[n->entier - 1])).absoluteFilePath());
                    continue;
                }
                if (n->type != P_CLASSE || !(n->forme & 32)) continue;   // une entité, pas une classe ordinaire
                const QString cle = fichier + ":" + QString::number(n->ligne);
                if (vues.contains(cle)) continue;
                vues.insert(cle);
                const QString nom = QString::fromUtf8(n->texte);
                EntiteSchema &e = par_nom[nom];
                if (e.nom.isEmpty()) {   // la déclaration ; un complément (« Un membre a : ») n'ajoute que des champs
                    e.nom = nom;
                    e.fichier = fichier;
                    e.ligne = n->ligne;
                    e.feminin = (n->forme & 3) == 2;
                }
                if (n->texte2 && e.parent.isEmpty()) e.parent = QString::fromUtf8(n->texte2);
                for (size_t j = 0; j < n->nb_enfants; j++) {
                    const Noeud *ch = n->enfants[j];
                    if (ch->type == N_TEXTE) {
                        e.aptitudes << QString::fromUtf8(ch->texte);
                        continue;
                    }
                    if (ch->type != N_NOM || !ch->texte2) continue;
                    ChampSchema s;
                    s.nom = QString::fromUtf8(ch->texte);
                    s.type = QString::fromUtf8(ch->texte2);
                    s.unique = ch->op == 'U';
                    s.facultatif = ch->entier & 1;
                    s.cascade = ch->entier & 2;
                    s.multiple = ch->forme == 3;
                    s.feminin = ch->forme == 2;
                    if (ch->nb_enfants) {   // la valeur de départ, comme on l'écrit (§ 16.7)
                        const Noeud *v = ch->enfants[0];
                        const QString t = QString::fromUtf8(v->texte ? v->texte : "");
                        if (v->type == N_NOMBRE) s.depart = QString(t).replace('.', ',');
                        else if (v->type == N_DATE && t.size() == 10) s.depart = t.mid(8, 2) + "." + t.mid(5, 2) + "." + t.left(4);
                        else if (v->type == N_NEGATION && v->nb_enfants && v->enfants[0]->texte)
                            s.depart = "-" + QString::fromUtf8(v->enfants[0]->texte).replace('.', ',');
                        else s.depart = t;
                    }
                    e.champs << s;
                }
            }
        };
        lire(p.phrases, p.nb, chemin);
        programme_liberer(&p);
    }
    QVector<EntiteSchema> r;
    for (auto &e : par_nom) r << e;
    for (auto &e : r)
        for (auto &c : e.champs) c.lien = par_nom.contains(c.type);
    return r;
}

QString apercu_migration(const QString &programme, bool *refusee) {
    *refusee = false;
    QFile f(programme);
    if (!f.open(QIODevice::ReadOnly)) return QString("« %1 » ne s'ouvre pas.").arg(programme);
    const QByteArray source = f.readAll(), c = programme.toUtf8();
    Portee *portee = portee_creer();
    portee_fichier(portee, c.constData());
    Programme p = {};
    Diagnostic d = {};
    const bool compacte = programme.endsWith(".grymc", Qt::CaseInsensitive);
    const int ok = compacte ? analyser_compact(source.constData(), (size_t)source.size(), portee, &p, &d)
                            : analyser(source.constData(), (size_t)source.size(), portee, 0, &p, &d);
    portee_detruire(portee);
    if (!ok) {
        const QString m = QString::fromUtf8(d.message ? d.message : "erreur");
        diagnostic_liberer(&d);
        return "Le programme contient une erreur : " + m;
    }
    Module *module = compiler(&p, &d);
    programme_liberer(&p);
    if (!module) {
        const QString m = QString::fromUtf8(d.message ? d.message : "erreur");
        diagnostic_liberer(&d);
        return m;
    }
    // la base à côté du programme, comme grym lancer (grammaire, § 16.5)
    const QFileInfo i(programme);
    const QByteArray dossier = i.absolutePath().toUtf8();
    const QByteArray base = (i.absolutePath() + "/" + i.completeBaseName() + ".grymd").toUtf8();
    Machine *m = machine_creer();
    machine_dossier(m, dossier.constData());
    machine_base(m, base.constData());
    Chaine rapport = {}, sortie = {};
    machine_essai_migration(m, &rapport);
    const int reussi = machine_executer(m, module, &sortie, &d);
    machine_detruire(m);
    module_detruire(module);
    std::free(sortie.d);
    QString r = QString::fromUtf8(rapport.d ? rapport.d : "").trimmed();
    std::free(rapport.d);
    if (!reussi) {
        *refusee = true;
        r = QString::fromUtf8(d.message ? d.message : "migration refusée");
        diagnostic_liberer(&d);
    }
    return r;
}

// ---------------------------------------------------------------------------------------------
// Dessin
// ---------------------------------------------------------------------------------------------

namespace {

const int LARGEUR_MIN = 180, MARGE = 8;

class Boite : public QGraphicsItem {
public:
    Boite(const EntiteSchema &e, std::function<void()> bouge, std::function<void()> ouvre)
        : entite(e), sur_mouvement(std::move(bouge)), sur_double_clic(std::move(ouvre)) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setCursor(Qt::OpenHandCursor);
        QFont gras;
        gras.setBold(true);
        const QFontMetrics fg(gras), fm((QFont()));
        int l = fg.horizontalAdvance(titre()) + 2 * MARGE;
        for (const QString &x : lignes()) l = qMax(l, fm.horizontalAdvance(x) + 2 * MARGE);
        largeur = qMax(LARGEUR_MIN, l);
        hauteur_titre = fg.height() + MARGE;
        hauteur = hauteur_titre + (int)lignes().size() * (fm.height() + 2) + MARGE;
        QString bulle = QString("%1\n%2, ligne %3\nDouble-clic : la déclaration dans le code")
                            .arg(entite.nom, QFileInfo(entite.fichier).fileName()).arg(entite.ligne);
        setToolTip(bulle);
    }

    QString titre() const { return entite.parent.isEmpty() ? entite.nom : entite.nom + "  ⟶ " + entite.parent; }

    QStringList lignes() const {
        QStringList r;
        if (!entite.aptitudes.isEmpty()) r << "adopte : " + entite.aptitudes.join(", ");
        for (const auto &c : entite.champs) {
            QString x = c.nom + " : " + c.type;
            QStringList m;
            if (c.multiple) m << "plusieurs";
            if (c.unique) m << "unique";
            if (c.facultatif) m << "facultatif";
            if (c.cascade) m << "disparaît avec";
            if (!m.isEmpty()) x += "  (" + m.join(", ") + ")";
            r << x;
        }
        if (entite.champs.isEmpty() && entite.aptitudes.isEmpty()) r << "aucun champ propre";
        return r;
    }

    QRectF boundingRect() const override { return QRectF(0, 0, largeur, hauteur); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        const QRectF r = boundingRect();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(isSelected() ? QColor(0x1f, 0x5f, 0xa8) : QColor(0x88, 0x88, 0x88), isSelected() ? 2 : 1));
        p->setBrush(QColor(0xfc, 0xfc, 0xfa));
        p->drawRoundedRect(r, 6, 6);
        p->setBrush(QColor(0xe4, 0xec, 0xf6));
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(QRectF(1, 1, r.width() - 2, hauteur_titre), 5, 5);
        QFont gras;
        gras.setBold(true);
        p->setFont(gras);
        p->setPen(Qt::black);
        p->drawText(QRectF(MARGE, 0, r.width() - 2 * MARGE, hauteur_titre), Qt::AlignVCenter, titre());
        p->setFont(QFont());
        const QFontMetrics fm((QFont()));
        int y = hauteur_titre + 4;
        const QStringList ls = lignes();
        for (int k = 0; k < ls.size(); k++) {
            const bool lien = !entite.aptitudes.isEmpty() ? (k > 0 && entite.champs[k - 1].lien)
                                                          : (k < entite.champs.size() && entite.champs[k].lien);
            p->setPen(lien ? QColor(0x1f, 0x5f, 0xa8) : QColor(0x33, 0x33, 0x33));
            p->drawText(QRectF(MARGE, y, r.width() - 2 * MARGE, fm.height()), Qt::AlignVCenter, ls[k]);
            y += fm.height() + 2;
        }
    }

    EntiteSchema entite;

protected:
    QVariant itemChange(GraphicsItemChange c, const QVariant &v) override {
        if (c == ItemPositionHasChanged && sur_mouvement) sur_mouvement();
        return QGraphicsItem::itemChange(c, v);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override {
        QGraphicsItem::mouseReleaseEvent(e);
        relache = true;
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *) override {
        if (sur_double_clic) sur_double_clic();
    }

public:
    bool relache = false;

private:
    int largeur = 0, hauteur = 0, hauteur_titre = 0;
    std::function<void()> sur_mouvement, sur_double_clic;
};

// Point du bord de la boîte, sur la droite qui joint son centre à `vers`.
QPointF bord(const QRectF &r, const QPointF &vers) {
    const QPointF c = r.center(), d = vers - c;
    if (qFuzzyIsNull(d.x()) && qFuzzyIsNull(d.y())) return c;
    const double sx = qFuzzyIsNull(d.x()) ? 1e9 : (r.width() / 2) / qAbs(d.x());
    const double sy = qFuzzyIsNull(d.y()) ? 1e9 : (r.height() / 2) / qAbs(d.y());
    return c + d * qMin(sx, sy);
}

class Fleche : public QGraphicsItem {
public:
    enum Sorte { Lien, Multiple, Heritage };
    Fleche(QGraphicsItem *de, QGraphicsItem *vers, Sorte s, const QString &nom) : a(de), b(vers), sorte(s), champ(nom) {
        setZValue(-1);
        setToolTip(s == Heritage ? nom : nom + "\nDouble-clic : modifier ce lien ; clic droit : le supprimer");
    }
    // une flèche se saisit près de son trait, pas dans tout le rectangle qui l'entoure
    QPainterPath shape() const override {
        QPainterPath p(p1);
        p.lineTo(p2);
        QPainterPathStroker s;
        s.setWidth(10);
        return s.createStroke(p);
    }
    QRectF boundingRect() const override { return QRectF(p1, p2).normalized().adjusted(-12, -12, 12, 12); }
    void mettre_a_jour() {
        prepareGeometryChange();
        const QRectF ra = a->sceneBoundingRect(), rb = b->sceneBoundingRect();
        if (a == b) {   // un lien vers soi : une boucle sur le coin
            p1 = ra.topRight() + QPointF(-30, 0);
            p2 = ra.topRight() + QPointF(0, 30);
            return;
        }
        p1 = bord(ra, rb.center());
        p2 = bord(rb, ra.center());
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        QPen stylo(sorte == Heritage ? QColor(0x66, 0x66, 0x66) : QColor(0x1f, 0x5f, 0xa8), 1.4);
        if (sorte == Heritage) stylo.setStyle(Qt::DashLine);
        p->setPen(stylo);
        if (a == b) {
            p->setBrush(Qt::NoBrush);
            p->drawArc(QRectF(p1.x(), p1.y() - 30, 60, 60), 0, 270 * 16);
            return;
        }
        p->drawLine(p1, p2);
        // la pointe vers l'objet désigné ; double pour « plusieurs » ; triangle creux pour l'héritage
        const double angle = qAtan2(p1.y() - p2.y(), p1.x() - p2.x());
        auto pointe = [&](QPointF bout) {
            const QPointF g = bout + QPointF(qCos(angle + 0.4), qSin(angle + 0.4)) * 11;
            const QPointF d = bout + QPointF(qCos(angle - 0.4), qSin(angle - 0.4)) * 11;
            p->setPen(QPen(stylo.color(), 1.4));
            if (sorte == Heritage) {
                p->setBrush(Qt::white);
                p->drawPolygon(QPolygonF({bout, g, d}));
            } else {
                p->drawLine(bout, g);
                p->drawLine(bout, d);
            }
        };
        pointe(p2);
        if (sorte == Multiple) pointe(p2 + QPointF(qCos(angle), qSin(angle)) * 8);
    }
    QGraphicsItem *a, *b;
    Sorte sorte;
    QString champ;
    QPointF p1, p2;
};

Boite *boite_sous(QGraphicsScene *s, const QPointF &p) {
    for (QGraphicsItem *i : s->items(p))
        if (auto *b = dynamic_cast<Boite *>(i)) return b;
    return nullptr;
}

Fleche *fleche_sous(QGraphicsScene *s, const QPointF &p) {
    for (QGraphicsItem *i : s->items(p))
        if (auto *f = dynamic_cast<Fleche *>(i)) return f->sorte == Fleche::Heritage ? nullptr : f;
    return nullptr;
}

}  // namespace

VueSchema::VueSchema(QWidget *parent) : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(QColor(0xf3, 0xf3, 0xf0));
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        if (en_construction) return;
        const auto choix = scene_->selectedItems();
        const auto *b = choix.isEmpty() ? nullptr : dynamic_cast<Boite *>(choix.first());
        emit choisie(b ? b->entite.nom : QString());
    });
}

void VueSchema::choisir(const QString &entite) {
    en_construction = true;   // choisir par le programme n'émet rien
    for (QGraphicsItem *i : scene_->items())
        if (auto *b = dynamic_cast<Boite *>(i)) b->setSelected(b->entite.nom == entite);
    en_construction = false;
}

void VueSchema::montrer(const QVector<EntiteSchema> &e, const QMap<QString, QPointF> &positions) {
    en_construction = true;
    scene_->clear();
    entites = e;
    // Une entité sans place reçoit la sienne par étages : celles que rien ne désigne en haut, puis chaque
    // entité un étage sous les entités qu'elle désigne. Les flèches montent ainsi, et se croisent peu.
    QMap<QString, int> etage;
    for (const auto &x : entites) etage[x.nom] = 0;
    for (int tour = 0; tour < entites.size(); tour++) {   // au plus autant de tours que d'entités : un cercle s'arrête
        bool change = false;
        for (const auto &x : entites)
            for (const auto &c : x.champs)
                if (c.lien && c.type != x.nom && etage.contains(c.type) && etage[x.nom] < etage[c.type] + 1) {
                    etage[x.nom] = etage[c.type] + 1;
                    change = true;
                }
        if (!change) break;
    }
    QVector<QRectF> occupees;
    for (const auto &n : positions) occupees << QRectF(n, QSizeF(220, 170));
    QMap<int, int> colonne;   // prochaine colonne essayée, par étage
    QMap<int, int> par_etage; // entités à placer, par étage : chaque étage se centre sur le plus large
    int plus_large = 1;
    for (const auto &x : entites)
        if (!positions.contains(x.nom)) plus_large = qMax(plus_large, ++par_etage[etage.value(x.nom)]);
    for (const auto &x : entites) {
        auto *b = new Boite(x, [this] { if (en_construction) return; relier(); emit deplacee(); },
                            [this, x] { emit ouvrir(x.fichier, x.ligne); });
        scene_->addItem(b);
        if (positions.contains(x.nom)) {
            b->setPos(positions.value(x.nom));
            continue;
        }
        const int e = etage.value(x.nom);
        for (int &k = colonne[e];; k++) {
            const QPointF p((plus_large - par_etage.value(e)) * 150.0 + k * 300.0, e * 230.0);
            const QRectF r(p, b->boundingRect().size());
            bool pris = false;
            for (const QRectF &o : occupees) pris |= o.intersects(r.adjusted(-20, -20, 20, 20));
            if (!pris) {
                b->setPos(p);
                occupees << b->sceneBoundingRect();
                k++;
                break;
            }
        }
    }
    // les flèches : un lien par champ qui désigne une entité, un trait tireté pour l'héritage
    QMap<QString, QGraphicsItem *> boites;
    for (QGraphicsItem *i : scene_->items())
        if (auto *b = dynamic_cast<Boite *>(i)) boites.insert(b->entite.nom, b);
    for (QGraphicsItem *i : boites) {
        const auto *b = static_cast<Boite *>(i);
        for (const auto &c : b->entite.champs)
            if (c.lien && boites.contains(c.type))
                scene_->addItem(new Fleche(i, boites[c.type], c.multiple ? Fleche::Multiple : Fleche::Lien, c.nom));
        if (!b->entite.parent.isEmpty() && boites.contains(b->entite.parent))
            scene_->addItem(new Fleche(i, boites[b->entite.parent], Fleche::Heritage, "hérite de " + b->entite.parent));
    }
    en_construction = false;
    relier();
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-60, -60, 60, 60));
}

void VueSchema::relier() {
    for (QGraphicsItem *i : scene_->items())
        if (auto *f = dynamic_cast<Fleche *>(i)) f->mettre_a_jour();
    scene_->update();
}

QMap<QString, QPointF> VueSchema::positions() const {
    QMap<QString, QPointF> r;
    for (QGraphicsItem *i : scene_->items())
        if (auto *b = dynamic_cast<Boite *>(i)) r.insert(b->entite.nom, b->pos());
    return r;
}

int VueSchema::nombre_de_boites() const {
    int n = 0;
    for (QGraphicsItem *i : scene_->items()) n += dynamic_cast<Boite *>(i) != nullptr;
    return n;
}

int VueSchema::nombre_de_liens() const {
    int n = 0;
    for (QGraphicsItem *i : scene_->items()) n += dynamic_cast<Fleche *>(i) != nullptr;
    return n;
}

void VueSchema::contextMenuEvent(QContextMenuEvent *e) {
    const QPointF p = mapToScene(e->pos());
    QMenu menu;
    if (Boite *b = boite_sous(scene_, p)) {
        QMenu *vers = menu.addMenu(QString("Lier « %1 » à").arg(b->entite.nom));
        for (const auto &x : entites) {
            const QString de = b->entite.nom, cible = x.nom;
            vers->addAction(cible, this, [this, de, cible] { emit lier(de, cible); });
        }
    } else if (Fleche *f = fleche_sous(scene_, p)) {
        const QString de = static_cast<Boite *>(f->a)->entite.nom, champ = f->champ;
        menu.addAction(QString("Modifier le lien « %1 »…").arg(champ), this, [this, de, champ] { emit lien_choisi(de, champ, false); });
        menu.addAction(QString("Supprimer le lien « %1 »").arg(champ), this, [this, de, champ] { emit lien_choisi(de, champ, true); });
    } else {
        return;
    }
    menu.exec(e->globalPos());
}

void VueSchema::mousePressEvent(QMouseEvent *e) {
    // Maj+glisser depuis une boîte : un lien se tire vers une autre boîte (ou vers elle-même)
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)) {
        if (Boite *b = boite_sous(scene_, mapToScene(e->pos()))) {
            trait_de = b->entite.nom;
            const QPointF c = b->sceneBoundingRect().center();
            trait = scene_->addLine(QLineF(c, c), QPen(QColor(0x1f, 0x5f, 0xa8), 2, Qt::DashLine));
            trait->setZValue(10);
            return;
        }
    }
    QGraphicsView::mousePressEvent(e);
}

void VueSchema::mouseMoveEvent(QMouseEvent *e) {
    if (trait) {
        QLineF l = trait->line();
        l.setP2(mapToScene(e->pos()));
        trait->setLine(l);
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void VueSchema::mouseReleaseEvent(QMouseEvent *e) {
    if (trait) {
        Boite *b = boite_sous(scene_, mapToScene(e->pos()));
        delete trait;
        trait = nullptr;
        if (b) emit lier(trait_de, b->entite.nom);
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void VueSchema::mouseDoubleClickEvent(QMouseEvent *e) {
    const QPointF p = mapToScene(e->pos());
    if (!boite_sous(scene_, p))
        if (Fleche *f = fleche_sous(scene_, p)) {
            emit lien_choisi(static_cast<Boite *>(f->a)->entite.nom, f->champ, false);
            return;
        }
    QGraphicsView::mouseDoubleClickEvent(e);
}
