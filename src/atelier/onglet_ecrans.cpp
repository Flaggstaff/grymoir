// GrymoiR : l'atelier, l'onglet « Écrans ».
#include "onglet_ecrans.h"
#include "schema.h"

#include <QDirIterator>
#include <QEvent>
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
    struct Evenement { QString ecran, objet, fichier; int forme, ligne; };
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
                    evenements.push_back({QString::fromUtf8(n->texte3), QString::fromUtf8(n->enfants[2]->texte), fichier,
                                          n->forme, n->ligne});
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
    pd->addWidget(voir_evenement);
    pd->addWidget(voir_declaration);
    auto *partage = new QSplitter;
    partage->addWidget(liste);
    partage->addWidget(centre);
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

void OngletEcrans::montrer(const QString &dossier) {
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
        w->installEventFilter(this);   // un clic choisit l'élément, sans rien déclencher
        for (QWidget *enfant : w->findChildren<QWidget *>()) enfant->installEventFilter(this);
        if (auto *t = vue.tables[k]) {   // la structure, pas les données : deux lignes grisées d'exemple
            t->setRowCount(2);
            for (int r = 0; r < 2; r++)
                for (int c = 0; c < t->columnCount(); c++) {
                    auto *i = new QTableWidgetItem("…");
                    i->setForeground(Qt::gray);
                    t->setItem(r, c, i);
                }
        }
    }
    vue.titre->installEventFilter(this);
    centre->setWidget(vue.vue);
    choisir_element(element < e.elements.size() ? element : -1);
}

bool OngletEcrans::eventFilter(QObject *o, QEvent *ev) {
    if (ev->type() != QEvent::MouseButtonPress && ev->type() != QEvent::MouseButtonDblClick
        && ev->type() != QEvent::MouseButtonRelease && ev->type() != QEvent::KeyPress)
        return QWidget::eventFilter(o, ev);
    if (ev->type() == QEvent::MouseButtonPress) {
        int trouve = -1;
        for (int k = 0; k < vue.controles.size(); k++) {
            QWidget *w = vue.controles[k];
            if (w && (o == w || w->isAncestorOf(qobject_cast<QWidget *>(o)))) trouve = k;
        }
        choisir_element(trouve);
    }
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
        return "<tr><td style='color:#666'>" + nom.toHtmlEscaped() + "</td><td>" + valeur.toHtmlEscaped() + "</td></tr>";
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
    if (QWidget *w = vue.controles.value(k)) w->setStyleSheet("border: 2px solid #1f5fa8;");
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
    if (!x.ecrit.isEmpty()) h += "<p style='color:#666'>En GrymoiR :</p><pre>" + x.ecrit.toHtmlEscaped() + "</pre>";
    panneau->setHtml(h);
    voir_evenement->setEnabled(x.evenement_ligne > 0);
}

QString OngletEcrans::proprietes() const { return panneau->toPlainText(); }
