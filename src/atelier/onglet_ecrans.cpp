// GrymoiR : l'atelier, l'onglet « Écrans ».
#include "onglet_ecrans.h"
#include "theme.h"

#include <QPixmap>
#include "schema.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDirIterator>
#include <QFormLayout>
#include <QInputDialog>
#include <QDir>
#include <QFileDialog>
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
#include <QContextMenuEvent>
#include <QMenu>
#include <QUrl>
#include <QStandardPaths>
#include <QMenuBar>
#include <QHash>
#include <QTabWidget>
#include <QTabBar>
#include <QKeyEvent>
#include <climits>
#include <QSpinBox>
#include <QPainter>
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

// Un bloc, qui s'ouvre et se ferme par une fin de bloc : pas un élément. Le graphique et le menu, venus après les blocs
// dans la numérotation, font qu'un test « sorte ≥ côte à côte » ne suffit plus.
// « Interrupteur » dans la palette : une zone (vrai ou faux) en interrupteur, pas une sorte d'élément (§ 22.6)
static const int INTERRUPTEUR_PALETTE = 100;
static const int CHOIX_MULTIPLES_PALETTE = 101;   // « des genres (genre) » (§ 22.1)
static const int FICHIER_PALETTE = 102;           // une zone de fichier ou d'image (§ 22.1)
// Ce que la palette et le clic droit proposent, dans cet ordre.
static const std::initializer_list<int> SORTES_PALETTE = {ELEMENT_BOUTON, ELEMENT_TEXTE, ELEMENT_ZONE, INTERRUPTEUR_PALETTE,
                                                          CHOIX_MULTIPLES_PALETTE, FICHIER_PALETTE, ELEMENT_LISTE,
                                                          ELEMENT_GRAPHIQUE, ELEMENT_IMAGE, ELEMENT_ONGLETS, ELEMENT_MENU};
static QString nom_palette(int sorte) {
    return sorte == ELEMENT_BOUTON ? "Bouton" : sorte == ELEMENT_TEXTE ? "Texte" : sorte == ELEMENT_ZONE ? "Zone"
         : sorte == INTERRUPTEUR_PALETTE ? "Interrupteur" : sorte == CHOIX_MULTIPLES_PALETTE ? "Choix multiples"
         : sorte == FICHIER_PALETTE ? "Fichier" : sorte == ELEMENT_LISTE ? "Liste" : sorte == ELEMENT_GRAPHIQUE ? "Graphique"
         : sorte == ELEMENT_IMAGE ? "Image" : sorte == ELEMENT_ONGLETS ? "Onglets" : "Menu";
}

static bool est_bloc(int s) {
    return s == ELEMENT_COTE_A_COTE || s == ELEMENT_L_UN_SOUS_L_AUTRE || s == ELEMENT_GROUPE || s == ELEMENT_LIBRE
           || s == ELEMENT_ONGLETS || s == ELEMENT_ONGLET || s == ELEMENT_MENU;
}
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

// Un champ pour « actif si » ou « visible si » (§ 22.6) : la condition en GrymoiR, vide pour toujours.
static QLineEdit *condition_ecran(const QString &ecrit, const QString &exemple) {
    auto *l = new QLineEdit(ecrit);
    l->setPlaceholderText("toujours ; ou une condition : " + exemple);
    l->setToolTip("Une condition GrymoiR, relue après chaque événement ; elle cite les zones et les listes "
                  "écrites avant l'élément, par « de l'écran ».");
    return l;
}

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
                e.taille_titre = n->taille_titre;
                e.fichier = fichier;
                e.ligne = n->ligne;
                for (size_t q = 0; q < n->nb_enfants; q++) {
                    const Noeud *el = n->enfants[q];
                    ElementLu x;
                    if (ici) x.ecrit = extrait(el);   // un fichier utilisé se lit ailleurs, avec ses propres positions
                    for (size_t s2 = 0; ici && s2 < el->nb_enfants; s2++) {   // « actif si », « visible si » (§ 22.6)
                        const Noeud *si = el->enfants[s2];
                        if (si->type != N_SI_ECRAN) continue;
                        if (si->forme == 3) { x.source = extrait(si->enfants[0]); continue; }   // image calculée (§ 22.8)
                        (si->forme == 1 ? x.actif_si : x.visible_si) = extrait(si->enfants[0]);
                    }
                    x.place = el->place_dite & 1;   // § 22.9
                    x.gauche = el->place[0];
                    x.haut = el->place[1];
                    x.largeur = (el->place_dite & 2) ? el->place[2] : 0;
                    x.hauteur = (el->place_dite & 4) ? el->place[3] : 0;
                    if (el->type == N_DISPOSITION) {
                        x.sorte = el->forme == 1 ? ELEMENT_COTE_A_COTE : el->forme == 2 ? ELEMENT_L_UN_SOUS_L_AUTRE
                                : el->forme == 3 ? ELEMENT_GROUPE : el->forme == 4 ? ELEMENT_LIBRE : el->forme == 5 ? ELEMENT_ONGLETS
                                : el->forme == 6 ? ELEMENT_ONGLET : el->forme == 7 ? ELEMENT_MENU : ELEMENT_FIN_DE_BLOC;
                        if (el->forme == 3 || el->forme == 6 || el->forme == 7) x.texte = QString::fromUtf8(el->texte);   // le titre (§ 22.7, § 22.10, § 22.12)
                        x.ecrit.clear();
                    } else if (el->type == N_BOUTON) {
                        x.sorte = ELEMENT_BOUTON;
                        x.texte = QString::fromUtf8(el->texte);
                    } else if (el->type == N_IMAGE_ECRAN) {   // « l'image « plan.png » » : le chemin écrit (§ 22.8)
                        x.sorte = ELEMENT_IMAGE;
                        x.texte = el->texte ? QString::fromUtf8(el->texte) : QString();
                    } else if (el->type == N_TEXTE_ECRAN) {
                        x.sorte = ELEMENT_TEXTE;
                        x.texte = QString::fromUtf8(el->texte);
                    } else if (el->type == N_NOM) {
                        x.sorte = ELEMENT_ZONE;
                        x.multiple = el->entier & 16;   // des choix multiples (§ 22.1)
                        x.texte = QString::fromUtf8(el->texte);
                        x.type = QString::fromUtf8(el->texte2);
                        x.facultatif = el->entier & 1;
                        x.feminin = el->forme == 2;
                        x.plusieurs_lignes = el->entier & 2;
                        if (el->texte3) x.valeurs = QString::fromUtf8(el->texte3).split(QChar(0x1e));
                        x.lecture_seule = el->entier & 4;
                        x.interrupteur = el->entier & 8;
                        x.recherche = el->entier & 32;
                        x.masque = el->entier & 64;
                        auto ecrite = [](const char *c) {   // « -2.5 » → « −2,5 »
                            QString t = QString::fromUtf8(c ? c : "");
                            t.replace('.', ',');
                            if (t.startsWith('-')) t = QString::fromUtf8("−") + t.mid(1);
                            return t;
                        };
                        x.borne_min = ecrite(el->borne_min);
                        x.borne_max = ecrite(el->borne_max);
                        x.indication = el->indication ? QString::fromUtf8(el->indication) : QString();
                        if (el->nb_enfants && el->enfants[0]->type != N_SI_ECRAN) {
                            const Noeud *v = el->enfants[0];
                            x.depart = QString::fromUtf8(v->texte ? v->texte
                                                         : v->nb_enfants && v->enfants[0]->texte ? v->enfants[0]->texte : "");
                            if (v->type == N_NEGATION) x.depart = "−" + x.depart;
                        }
                    } else if (el->type == N_CHERCHER) {
                        x.sorte = el->graphique ? ELEMENT_GRAPHIQUE : ELEMENT_LISTE;   // § 22.11
                        x.graphique = el->graphique;
                        x.texte = QString::fromUtf8(el->texte);
                        x.tri = el->texte2 ? QString::fromUtf8(el->texte2) : QString();
                        x.decroissant = el->entier == 1;
                        x.colonnes_choisies = el->texte3 != nullptr;
                        if (el->graphique && el->texte3) {   // la clé, puis la somme (§ 22.11)
                            const QStringList parts = QString::fromUtf8(el->texte3).split(QChar(0x1e));
                            x.graphe_cle = parts.value(0);
                            x.graphe_somme = (el->graphique & 4) ? parts.value(1) : QString();
                        }
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
        int couleur = -1;
        QString logo, accueil;
        for (size_t k = 0; k < p.nb; k++) {   // l'apparence, et le premier écran ouvert au premier niveau (§ 22.5)
            const Noeud *n = p.phrases[k];
            if (n->type == P_APPARENCE) {
                couleur = n->entier;
                if (n->texte) logo = QFileInfo(chemin).dir().absoluteFilePath(QString::fromUtf8(n->texte));
            } else if (n->type == P_OUVRIR && n->forme == 0 && accueil.isEmpty() && n->texte) {
                accueil = QString::fromUtf8(n->texte);
            }
        }
        const int avant = r.size();
        lire(p.phrases, p.nb, chemin, true);
        for (int k = avant; k < r.size(); k++) {
            r[k].couleur = couleur;
            r[k].logo = logo;
            r[k].accueil = r[k].nom == accueil;
        }
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
    for (int sorte : SORTES_PALETTE) {   // la même liste que le clic droit
        auto *b = new QPushButton(nom_palette(sorte));
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
                              : QString("%1 écran%2. Clique un élément de l'aperçu pour voir ses propriétés.")
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
        el[k].plusieurs_lignes = e.elements[k].plusieurs_lignes;
        if (e.elements[k].sorte == ELEMENT_IMAGE && !e.elements[k].source.isEmpty())   // calculée : sa source, faute de données
            el[k].texte = QString::fromUtf8("Image : ") + e.elements[k].source;
        else if (e.elements[k].sorte == ELEMENT_IMAGE) {   // l'aperçu lit l'image, depuis le dossier du fichier qui déclare l'écran
            const QString t = e.elements[k].texte;
            QFile f(QFileInfo(t).isAbsolute() ? t : QFileInfo(e.fichier).absoluteDir().filePath(t));
            if (f.open(QIODevice::ReadOnly)) el[k].octets = f.readAll();
        }
        el[k].facultatif = e.elements[k].facultatif;
        if (!e.elements[k].valeurs.isEmpty()) el[k].choix = e.elements[k].valeurs;   // l'aperçu montre le menu ou les boutons
        el[k].lecture_seule = e.elements[k].lecture_seule;
        el[k].place = e.elements[k].place;
        el[k].graphique = e.elements[k].graphique;
        el[k].multiple = e.elements[k].multiple;
        el[k].interrupteur = e.elements[k].interrupteur;
        el[k].indication = e.elements[k].indication;
        el[k].recherche = e.elements[k].recherche;
        el[k].masque = e.elements[k].masque;
        if (e.elements[k].type == "texte") el[k].longueur_max = e.elements[k].borne_max.toInt();
        el[k].gauche = e.elements[k].gauche;
        el[k].haut = e.elements[k].haut;
        el[k].largeur = e.elements[k].largeur;
        el[k].hauteur = e.elements[k].hauteur;
    }
    // L'apparence du programme (§ 22.5) : son accent sur l'aperçu seul, pas sur l'atelier ; son logo, grand
    // sur l'écran d'accueil, petit sur les autres, comme à l'exécution.
    const Theme &th = Theme::courant();
    const QPixmap logo = e.logo.isEmpty() ? QPixmap() : QPixmap(e.logo);
    vue = dessiner_ecran(e.titre, el, logo, th.mesure(e.accueil ? "logo-accueil" : "logo-titre"));   // le même dessin que l'exécution
    taille_du_titre(vue.titre, e.taille_titre);
    const QStringList accents = th.accents();
    if (e.couleur >= 0 && e.couleur < accents.size() && accents[e.couleur] != th.accent())
        vue.vue->setStyleSheet(th.feuille(th.mode(), accents[e.couleur]));
    vue.vue->setAcceptDrops(true);   // une image glissée depuis le Finder sur le fond de l'écran
    vue.vue->installEventFilter(this);
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
    if (vue.logo) vue.logo->installEventFilter(this);   // le clic droit sur le logo vaut celui sur le titre
    for (int k = 0; k < vue.controles.size(); k++)   // le survol des éléments placés : le curseur change sur leurs bords
        if (QWidget *w = vue.controles[k]) {
            w->setMouseTracking(true);
            for (QWidget *x : w->findChildren<QWidget *>()) x->setMouseTracking(true);
        }
    grille = nullptr;
    for (int k = 0; k < vue.controles.size(); k++)   // la page ouverte de chaque bloc d'onglets, gardée d'un aperçu à l'autre
        if (auto *tw = qobject_cast<QTabWidget *>(vue.controles[k])) {
            const QString cle = e.nom + "#" + QString::number(k);
            tw->setCurrentIndex(pages_ouvertes.value(cle, 0));
            connect(tw, &QTabWidget::currentChanged, this, [this, cle](int i) { pages_ouvertes[cle] = i; });
        }
    for (QMenuBar *barre : vue.vue->findChildren<QMenuBar *>()) barre->installEventFilter(this);   // § 22.12
    for (int k = 0; k < vue.controles.size(); k++)   // un élément de menu, cliqué dans le menu ouvert de l'aperçu : choisi
        if (auto *b = qobject_cast<QPushButton *>(vue.controles[k]); b && b->property("element-de-menu").toBool())
            connect(b, &QPushButton::clicked, this, [this, k] { choisir_element(k); });
    poignee = nullptr;   // l'ancienne est partie avec l'ancien aperçu
    fantome = nullptr;
    guide_v = guide_h = nullptr;   // partis avec l'ancien aperçu
    centre->installEventFilter(this);
    for (int k = 0; k < e.elements.size(); k++)   // le bloc en position libre : son contour, pour savoir où l'on pose
        if (e.elements[k].sorte == ELEMENT_LIBRE && vue.controles.value(k)) {
            QWidget *t = vue.controles[k];
            t->setAttribute(Qt::WA_StyledBackground);
            t->setStyleSheet("#toile{border:1px dashed " + Theme::courant().hex("texte-2") + ";}");
        }
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

// La grille d'alignement (§ 5 octies) : un trait tous les 8 pixels, appuyé tous les 32, sur la toile du bloc libre,
// le temps de placer un élément. Elle ne reçoit pas la souris.
namespace {
class Grille : public QWidget {
public:
    explicit Grille(QWidget *toile) : QWidget(toile) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName("grille");
        setStyleSheet("background: transparent;");   // le thème peint tout QWidget : sans cela, une plaque qui cache tout
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        QColor fin = Theme::courant().couleur("accent"), fort = fin;
        fin.setAlpha(28);   // discrète : les éléments déjà posés se voient au travers
        fort.setAlpha(64);
        for (int x = 0; x < width(); x += 8) { p.setPen(x % 32 ? fin : fort); p.drawLine(x, 0, x, height()); }
        for (int y = 0; y < height(); y += 8) { p.setPen(y % 32 ? fin : fort); p.drawLine(0, y, width(), y); }
    }
};
}  // namespace

QWidget *OngletEcrans::grille_visible() const { return grille && grille->isVisible() ? grille : nullptr; }
// Le bord droit, le bord bas, ou leur coin : 12 pixels de prise, assez pour un trackpad et pour le retrait du dessin
// d'un bouton macOS dans son cadre.
int OngletEcrans::bord_sous(QWidget *w, QPoint global) {
    const QPoint p = w->mapFromGlobal(global);
    if (p.x() < 0 || p.y() < 0 || p.x() > w->width() || p.y() > w->height()) return 0;
    return (p.x() >= w->width() - 12 ? 1 : 0) | (p.y() >= w->height() - 12 ? 2 : 0);
}

QString OngletEcrans::texte_etat() const { return etat->text(); }

// Le bloc libre visible le plus intérieur sous la souris, hors de l'élément saisi et de ce qu'il contient ; −1 sinon.
// Une page d'onglet cachée n'en est pas : son bloc libre est au même endroit, mais invisible.
int OngletEcrans::bloc_libre_sous(QPoint global, QWidget *saisi, QWidget **toile) const {
    int bloc = -1;
    if (toile) *toile = nullptr;
    for (int q = 0; courant >= 0 && q < vue.controles.size(); q++) {
        QWidget *x = vue.controles[q];
        if (!x || lus[courant].elements.value(q).sorte != ELEMENT_LIBRE || x == saisi || (saisi && saisi->isAncestorOf(x))
            || !x->isVisible())
            continue;
        // la surface visible : le bloc, coupé par chacun de ses parents (un bloc qui grandit pendant le geste déborde de sa
        // page d'onglet, mais la page le coupe)
        bool dedans = true;
        for (QWidget *p = x; p && dedans; p = p == vue.vue ? nullptr : p->parentWidget())
            dedans = p->rect().contains(p->mapFromGlobal(global));
        if (!dedans) continue;
        bloc = q;   // le dernier trouvé est le plus intérieur : les blocs suivent l'ordre écrit
        if (toile) *toile = x;
    }
    return bloc;
}

int OngletEcrans::onglet_ouvert(int k) const {
    if (courant < 0 || k < 0 || k >= lus[courant].elements.size() || lus[courant].elements[k].sorte != ELEMENT_ONGLETS) return -1;
    auto *tw = qobject_cast<QTabWidget *>(vue.controles.value(k));
    const int voulu = tw ? tw->currentIndex() : 0;
    int rang = 0, prof = 0;
    for (int q = k + 1; q < lus[courant].elements.size(); q++) {
        const int s = lus[courant].elements[q].sorte;
        if (s == ELEMENT_FIN_DE_BLOC) { if (prof-- == 0) break; continue; }
        if (est_bloc(s)) {
            if (prof == 0 && s == ELEMENT_ONGLET && rang++ == voulu) return q;
            prof++;
        }
    }
    return -1;
}

int OngletEcrans::onglets_de(int k) const {
    if (courant < 0 || k < 0 || k >= lus[courant].elements.size()) return -1;
    const auto &els = lus[courant].elements;
    QVector<int> pile;
    for (int q = 0; q < k; q++) {
        if (els[q].sorte == ELEMENT_FIN_DE_BLOC) { if (!pile.isEmpty()) pile.pop_back(); }
        else if (est_bloc(els[q].sorte)) pile << q;
    }
    for (int i = pile.size(); i-- > 0;) if (els[pile[i]].sorte == ELEMENT_ONGLETS) return pile[i];
    return -1;
}

int OngletEcrans::onglet_sous(QPoint global, int sauf) const {
    if (courant < 0 || !vue.vue) return -1;
    for (int k = 0; k < vue.controles.size(); k++) {
        auto *tw = qobject_cast<QTabWidget *>(vue.controles[k]);
        if (!tw || k == sauf || !tw->isVisible()) continue;
        QTabBar *barre = tw->tabBar();
        const int i = barre->tabAt(barre->mapFromGlobal(global));
        if (i < 0 || !barre->rect().contains(barre->mapFromGlobal(global))) continue;
        int rang = 0, prof = 0;   // l'onglet de rang i de ces onglets
        for (int q = k + 1; q < lus[courant].elements.size(); q++) {
            const int s = lus[courant].elements[q].sorte;
            if (s == ELEMENT_FIN_DE_BLOC) { if (prof-- == 0) break; continue; }
            if (est_bloc(s)) {
                if (prof == 0 && s == ELEMENT_ONGLET && rang++ == i) return q;
                prof++;
            }
        }
    }
    return -1;
}

int OngletEcrans::page_sous(QPoint global, int k, QWidget **toile) const {
    if (toile) *toile = nullptr;
    if (courant < 0) return -1;
    QWidget *glisse_w = vue.controles.value(k);
    for (int q = 0; q < vue.controles.size(); q++) {
        auto *tw = qobject_cast<QTabWidget *>(vue.controles[q]);
        if (!tw || q == k || !tw->isVisible() || (glisse_w && glisse_w->isAncestorOf(tw))) continue;
        QWidget *page = tw->currentWidget();
        if (!page || !page->rect().contains(page->mapFromGlobal(global))) continue;
        const int onglet = onglet_ouvert(q);
        if (onglet < 0) continue;
        for (int p = onglets_de(k); p >= 0; p = onglets_de(p))   // déjà dans cet onglet : un déplacement ordinaire
            if (p == q && glisse_w && page->isAncestorOf(glisse_w)) return -1;
        if (toile) *toile = page->findChild<QWidget *>("toile");
        return onglet;
    }
    return -1;
}

QString OngletEcrans::image_dans_le_projet(const QString &dossier, const QString &fichier, QString *erreur) {
    const QDir projet(dossier);
    const QString relatif = projet.relativeFilePath(fichier);
    if (!relatif.startsWith("..") && !QDir::isAbsolutePath(relatif)) return relatif;   // déjà dans le projet
    QFile source(fichier);
    if (!source.open(QIODevice::ReadOnly)) { *erreur = QString("« %1 » ne s'ouvre pas.").arg(fichier); return QString(); }
    const QByteArray octets = source.readAll();
    const QFileInfo info(fichier);
    QString nom = info.fileName();
    for (int n = 2;; n++) {   // un nom libre, ou le même fichier déjà copié
        QFile deja(projet.filePath(nom));
        if (!deja.exists()) break;
        if (deja.open(QIODevice::ReadOnly) && deja.readAll() == octets) return nom;
        nom = QString("%1-%2%3").arg(info.completeBaseName()).arg(n).arg(info.suffix().isEmpty() ? QString() : "." + info.suffix());
    }
    QFile copie(projet.filePath(nom));
    if (!copie.open(QIODevice::WriteOnly) || copie.write(octets) != octets.size()) {
        *erreur = QString("L'image n'a pas pu être copiée dans le projet (« %1 »).").arg(nom);
        return QString();
    }
    return nom;
}

QMenu *OngletEcrans::menu_du_titre() {
    auto *menu = new QMenu(this);
    if (courant < 0 || courant >= lus.size()) return menu;
    const QString ecran = lus[courant].nom, actuel = vue.titre ? vue.titre->text() : QString();
    const int taille = lus[courant].taille_titre;
    for (int t : {-1, 0, 1}) {   // la taille du titre (§ 22.5)
        QAction *x = menu->addAction(t < 0 ? "Titre en petit" : t > 0 ? "Titre en grand" : "Titre de taille normale");
        x->setCheckable(true);
        x->setChecked(t == taille);
        connect(x, &QAction::triggered, this, [=] {
            if (t != taille) emit geste([=](Geste *g) { return ecran_taille_titre(dossier, ecran, t, g); });
        });
    }
    menu->addSeparator();
    QAction *a = menu->addAction(QString::fromUtf8("Renommer le titre…"));
    connect(a, &QAction::triggered, this, [=] {
        bool ok = false;
        const QString t = QInputDialog::getText(this, "Titre de l'écran", "Titre :", QLineEdit::Normal, actuel, &ok);
        if (!ok || t.trimmed().isEmpty() || t == actuel) return;
        emit geste([=](Geste *g) { return ecran_titre(dossier, ecran, t.trimmed(), g); });
    });
    return menu;
}

QMenu *OngletEcrans::menu_des_champs(QPoint global, int sur) {
    auto *menu = new QMenu(this);
    if (courant < 0 || courant >= lus.size()) return menu;
    if (sur >= 0 && sur < lus[courant].elements.size()) {   // l'élément cliqué : le supprimer, d'abord
        const ElementLu &x = lus[courant].elements[sur];
        const QString nom = x.texte.isEmpty() ? QString("cet élément") : QString::fromUtf8("« %1 »").arg(x.texte);
        menu->addSection(nom);
        QAction *s = menu->addAction(QString("Supprimer %1").arg(nom));
        connect(s, &QAction::triggered, this, [this, sur] {
            choisir_element(sur);
            supprimer();   // la même suppression que le panneau : un événement qui partirait avec est montré d'abord
        });
    }
    const QString ecran = lus[courant].nom;
    QWidget *toile = nullptr;
    int dans = bloc_libre_sous(global, nullptr, &toile);   // le bloc libre sous le clic, sinon le bloc choisi
    if (dans < 0)   // ailleurs dans l'aperçu : le bloc libre de l'écran, à cet endroit (comme au glisser)
        for (int q = 0; q < vue.controles.size() && dans < 0; q++)
            if (lus[courant].elements.value(q).sorte == ELEMENT_LIBRE && vue.controles[q]
                && vue.controles[q]->objectName() == "toile" && !vue.controles[q]->property("place").toBool()) {
                dans = q;
                toile = vue.controles[q];
            }
    QPoint ou(-1, -1);
    if (dans >= 0 && toile) {
        const QPoint l = toile->mapFromGlobal(global);
        ou = QPoint(aimanter(qMax(0, l.x())), aimanter(qMax(0, l.y())));
    } else
        dans = cible_ajout();
    menu->addSection("Ajouter un élément");   // la palette, posée ici
    for (int sorte : SORTES_PALETTE) {
        QAction *a = menu->addAction(nom_palette(sorte));
        connect(a, &QAction::triggered, this, [this, sorte, dans, ou] {
            dans_clic = dans;
            ou_clic = ou;
            ajouter(sorte);
        });
    }
    menu->addSection("Ajouter un champ");
    const auto schema = lire_schema(dossier);
    if (schema.isEmpty()) menu->addAction("Aucune entité dans le projet")->setEnabled(false);
    for (const auto &en : schema) {
        QMenu *sous = menu->addMenu(en.nom);
        for (const auto &c : en.champs) {
            const QString vu = c.multiple ? QString("des %1 (%2)").arg(c.nom, c.type)
                                          : QString("%1 %2 (%3)").arg(c.feminin ? "une" : "un", c.nom, c.type);
            QAction *a = sous->addAction(vu);
            connect(a, &QAction::triggered, this, [=] {
                ElementNouveau n;   // la zone qui correspond au champ : son type, son genre, ses valeurs permises
                n.sorte = ELEMENT_ZONE;
                n.texte = c.nom;
                n.type = c.type;
                n.feminin = c.feminin;
                n.valeurs = c.valeurs;
                n.facultatif = c.facultatif;
                n.multiple = c.multiple;
                emit geste([=](Geste *g) { return ecran_ajouter(dossier, ecran, n, g, dans, ou); });
            });
        }
        if (en.champs.isEmpty()) sous->addAction("(aucun champ)")->setEnabled(false);
    }
    return menu;
}

int OngletEcrans::cible_ajout() const {
    if (courant < 0 || element < 0 || element >= lus[courant].elements.size()) return -1;
    const auto &els = lus[courant].elements;
    auto conteneur = [](int s) { return est_bloc(s); };
    if (els[element].sorte == ELEMENT_ONGLETS) return onglet_ouvert(element);
    if (conteneur(els[element].sorte)) return element;
    QVector<int> pile;   // le bloc le plus intérieur qui contient l'élément choisi
    for (int q = 0; q < element; q++) {
        if (els[q].sorte == ELEMENT_FIN_DE_BLOC) { if (!pile.isEmpty()) pile.pop_back(); }
        else if (conteneur(els[q].sorte)) pile << q;
    }
    if (pile.isEmpty()) return -1;
    return els[pile.last()].sorte == ELEMENT_ONGLETS ? onglet_ouvert(pile.last()) : pile.last();
}

QWidget *OngletEcrans::guide_visible() const {
    if (guide_v && !guide_v->isHidden()) return guide_v;
    return guide_h && !guide_h->isHidden() ? guide_h : nullptr;
}

bool OngletEcrans::aligner(QRect &r, const QRect &brut, const QVector<QRect> &voisins, QWidget *sur) {
    // les bords et le centre de l'élément, sans aimant ; ceux des voisins ; le plus proche à moins de 5 pixels gagne
    auto meilleur = [](const QVector<int> &miens, const QVector<int> &leurs, int *ecart, int *trait) {
        int mieux = 6;
        for (int m : miens)
            for (int l : leurs)
                if (qAbs(l - m) < mieux) { mieux = qAbs(l - m); *ecart = l - m; *trait = l; }
        return mieux < 6;
    };
    QVector<int> xs, ys;
    for (const QRect &v : voisins) {
        xs << v.left() << v.left() + v.width() << v.left() + v.width() / 2;
        ys << v.top() << v.top() + v.height() << v.top() + v.height() / 2;
    }
    int ex = 0, tx = 0, ey = 0, ty = 0;
    bool bx, by;
    if (libre_taille) {   // agrandir : le bord qu'on tire
        bx = (libre_taille & 1) && meilleur({brut.left() + brut.width()}, xs, &ex, &tx);
        by = (libre_taille & 2) && meilleur({brut.top() + brut.height()}, ys, &ey, &ty);
        if (bx) r.setWidth(qMax(8, brut.width() + ex));
        if (by) r.setHeight(qMax(8, brut.height() + ey));
    } else {
        bx = meilleur({brut.left(), brut.left() + brut.width(), brut.left() + brut.width() / 2}, xs, &ex, &tx);
        by = meilleur({brut.top(), brut.top() + brut.height(), brut.top() + brut.height() / 2}, ys, &ey, &ty);
        if (bx) r.moveLeft(qMax(0, brut.left() + ex));
        if (by) r.moveTop(qMax(0, brut.top() + ey));
    }
    auto trait = [&](QFrame *&f, bool montre, QRect g) {
        if (f && f->parentWidget() != sur) { delete f; f = nullptr; }
        if (!montre) { if (f) f->hide(); return; }
        if (!f) {
            f = new QFrame(sur);
            f->setAttribute(Qt::WA_TransparentForMouseEvents);
            f->setStyleSheet("background: " + Theme::courant().hex("accent") + ";");
        }
        f->setGeometry(g);
        f->show();
        f->raise();
    };
    trait(guide_v, bx, QRect(tx, 0, 1, sur->height()));
    trait(guide_h, by, QRect(0, ty, sur->width(), 1));
    return bx || by;
}

QWidget *OngletEcrans::poignee_visible() const { return poignee && !poignee->isHidden() ? poignee : nullptr; }

// Les flèches déplacent l'élément placé choisi d'un pixel (Maj : de 8, sur la grille) ; avec Alt, elles changent sa
// taille. Chaque appui est un geste, qui s'annule. Rend vrai si la touche a servi.
bool OngletEcrans::clavier(int t, Qt::KeyboardModifiers m) {
    if (courant < 0 || element < 0 || element >= lus[courant].elements.size()) return false;
    if (t != Qt::Key_Left && t != Qt::Key_Right && t != Qt::Key_Up && t != Qt::Key_Down) return false;
    const ElementLu &x = lus[courant].elements[element];
    const int pas = (m & Qt::ShiftModifier) ? 8 : 1;
    const int dx = t == Qt::Key_Left ? -pas : t == Qt::Key_Right ? pas : 0, dy = t == Qt::Key_Up ? -pas : t == Qt::Key_Down ? pas : 0;
    if (!x.place) {   // un écran décrit : le premier geste le passe en position libre (§ 5 octies)
        QRect r = rect_vu(element);
        if (r.isNull()) return false;
        for (int q = 0; q < lus[courant].elements.size(); q++)   // un écran mixte : l'élément entre dans son bloc libre
            if (lus[courant].elements[q].sorte == ELEMENT_LIBRE) {
                const QString ecran = lus[courant].nom;
                const int k = element;
                element = -1;
                emit geste([=](Geste *g) { return ecran_mettre_dans(dossier, ecran, k, q, g); });
                return true;
            }
        if (m & Qt::AltModifier) r.setSize(QSize(qMax(8, r.width() + dx), qMax(8, r.height() + dy)));
        else r.translate(dx, dy);
        liberer(element, r);
        return true;
    }
    QWidget *w = vue.controles.value(element);
    if (m & Qt::AltModifier) {   // la taille : partie de la taille dite, ou de celle qu'on voit
        const int l = qBound(8, (x.largeur ? x.largeur : (w ? w->width() : 80)) + dx, 4000);
        const int h = qBound(8, (x.hauteur ? x.hauteur : (w ? w->height() : 24)) + dy, 4000);
        placer(element, x.gauche, x.haut, dx ? l : -1, dy ? h : -1);
    } else
        placer(element, qBound(0, x.gauche + dx, 4000), qBound(0, x.haut + dy, 4000), -1, -1);
    return true;
}

void OngletEcrans::placer(int k, int gauche, int haut, int largeur, int hauteur) {
    if (courant < 0 || k < 0 || k >= lus[courant].elements.size()) return;
    const QString ecran = lus[courant].nom;
    emit geste([=](Geste *g) {
        int apres = k;
        const QString r = ecran_placer(dossier, ecran, k, gauche, haut, largeur, hauteur, g, &apres);
        if (r.isEmpty()) element = apres;   // l'élément reste choisi, à son nouveau rang
        return r;
    });
}

QRect OngletEcrans::rect_vu(int k) const {
    if (courant < 0 || k < 0 || k >= lus[courant].elements.size()) return QRect();
    const auto &els = lus[courant].elements;
    if (els[k].sorte == ELEMENT_FIN_DE_BLOC) return QRect();
    if (QWidget *w = vue.controles.value(k)) return QRect(w->mapTo(vue.vue, QPoint(0, 0)), w->size());
    if (!est_bloc(els[k].sorte)) return QRect();
    // un bloc sans widget (côte à côte, l'un sous l'autre, dans une boîte) : ce que ses éléments occupent
    QRect r;
    int prof = 0;
    for (int q = k; q < els.size(); q++) {
        const int s = els[q].sorte;
        if (s == ELEMENT_COTE_A_COTE || s == ELEMENT_L_UN_SOUS_L_AUTRE || s == ELEMENT_GROUPE || s == ELEMENT_LIBRE
            || s == ELEMENT_ONGLETS || s == ELEMENT_ONGLET || s == ELEMENT_MENU) prof++;
        else if (s == ELEMENT_FIN_DE_BLOC) { if (--prof == 0) break; }
        else if (QWidget *w = vue.controles.value(q)) r |= QRect(w->mapTo(vue.vue, QPoint(0, 0)), w->size());
    }
    return r;
}

void OngletEcrans::liberer(int k, QRect nouveau) {
    if (courant < 0 || courant >= lus.size()) return;
    const QString ecran = lus[courant].nom;
    QVector<QRect> places(lus[courant].elements.size());
    for (int q = 0; q < places.size(); q++) places[q] = rect_vu(q);
    if (k >= 0 && k < places.size()) {
        // Seules les unités du premier niveau prennent une place : un élément dans un bloc (groupe, onglet…) fait suivre
        // son geste au bloc du premier niveau qui le contient, du même déplacement et du même agrandissement.
        int haut = k, prof = 0;
        for (int q = 0; q < k; q++) {
            const int s = lus[courant].elements[q].sorte;
            if (s == ELEMENT_FIN_DE_BLOC) { if (prof) prof--; }
            else if (est_bloc(s)) { if (prof++ == 0) haut = q; }
        }
        if (prof == 0) haut = k;
        const QRect avant = rect_vu(k);
        if (haut == k || avant.isNull()) places[k] = nouveau;
        else {
            const QRect b = places[haut];
            places[haut] = QRect(b.topLeft() + (nouveau.topLeft() - avant.topLeft()), b.size() + (nouveau.size() - avant.size()));
        }
    }
    QPoint coin(INT_MAX, INT_MAX);   // ce qu'on voit, relatif à l'élément le plus en haut à gauche
    for (const QRect &r : places)
        if (!r.isNull()) { coin.setX(qMin(coin.x(), r.x())); coin.setY(qMin(coin.y(), r.y())); }
    for (QRect &r : places)
        if (!r.isNull()) r = QRect(aimanter(r.x() - coin.x()), aimanter(r.y() - coin.y()), qMax(8, aimanter(r.width())), qMax(8, aimanter(r.height())));
    element = -1;
    emit geste([=](Geste *g) { return ecran_liberer(dossier, ecran, places, g); });
}

bool OngletEcrans::eventFilter(QObject *o, QEvent *ev) {
    if (ev->type() == QEvent::ContextMenu && courant >= 0) {   // le clic droit : les champs disponibles, à ajouter ici
        const bool titre = o == vue.titre || (vue.logo && o == vue.logo);   // sur le titre : le renommer
        QMenu *m = titre ? menu_du_titre()
                         : menu_des_champs(static_cast<QContextMenuEvent *>(ev)->globalPos(), o == vue.vue ? -1 : element_sous(o));
        m->exec(static_cast<QContextMenuEvent *>(ev)->globalPos());
        m->deleteLater();
        return true;
    }
    // La barre de menus de l'aperçu (§ 22.12) : un clic choisit le menu et l'ouvre ; un élément du menu se choisit en
    // le cliquant dans le menu ouvert (son bouton invisible est relié dans dessiner()).
    if (auto *barre = qobject_cast<QMenuBar *>(o)) {
        if (ev->type() == QEvent::MouseButtonPress && courant >= 0)
            if (QAction *a = barre->actionAt(static_cast<QMouseEvent *>(ev)->position().toPoint()))
                for (int k = 0; k < lus[courant].elements.size(); k++)
                    if (lus[courant].elements[k].sorte == ELEMENT_MENU && lus[courant].elements[k].texte == a->text()) choisir_element(k);
        return false;
    }
    // La barre d'un widget d'onglets : ses clics ouvrent la page, comme dans un navigateur, et choisissent l'onglet (§ 5 decies)
    if (auto *barre = qobject_cast<QTabBar *>(o); barre && libre_k < 0) {
        if (ev->type() == QEvent::MouseButtonPress && courant >= 0) {
            const int k = element_sous(o);   // les onglets dont c'est la barre
            const int i = barre->tabAt(static_cast<QMouseEvent *>(ev)->position().toPoint());
            if (i >= 0 && k >= 0) {
                barre->setCurrentIndex(i);
                const int page = onglet_ouvert(k);
                if (page >= 0) choisir_element(page);
            }
            // saisie par sa barre, comme une fenêtre par son titre : glisser déplace tout le bloc d'onglets
            if (QWidget *tw = vue.controles.value(k)) {
                depart = static_cast<QMouseEvent *>(ev)->globalPosition().toPoint();
                libre_k = k;
                libre_bouge = false;
                libre_taille = 0;
                libre_decrit = !lus[courant].elements.value(k).place;
                libre_depart = libre_decrit ? rect_vu(k) : tw->geometry();
            }
            return true;
        }
        return ev->type() == QEvent::MouseButtonRelease || ev->type() == QEvent::MouseButtonDblClick
               || (ev->type() == QEvent::MouseMove && (static_cast<QMouseEvent *>(ev)->buttons() & Qt::LeftButton));
    }
    if (ev->type() == QEvent::KeyPress) {   // les flèches sur un élément placé (§ 5 octies)
        auto *k = static_cast<QKeyEvent *>(ev);
        if (clavier(k->key(), k->modifiers())) return true;
        if (o == centre) return QWidget::eventFilter(o, ev);
    }
    // Au survol d'un élément placé, le curseur dit ce que la souris saisira : le bord droit, le bord bas, le coin.
    if (ev->type() == QEvent::MouseMove && !(static_cast<QMouseEvent *>(ev)->buttons() & Qt::LeftButton) && courant >= 0) {
        const int k = element_sous(o);
        QWidget *w = vue.controles.value(k);
        if (auto *cible = qobject_cast<QWidget *>(o); cible && w) {
            const int b = bord_sous(w, static_cast<QMouseEvent *>(ev)->globalPosition().toPoint());   // tout se place (§ 5 octies)
            if (b == 3) cible->setCursor(Qt::SizeFDiagCursor);
            else if (b == 1) cible->setCursor(Qt::SizeHorCursor);
            else if (b == 2) cible->setCursor(Qt::SizeVerCursor);
            else cible->unsetCursor();
        }
    }
    // Position libre (§ 5 octies) : l'élément suit la souris, aimanté à la grille (Alt : au pixel) ; par son coin en
    // bas à droite, il s'agrandit. Rien n'est écrit avant de lâcher.
    if (libre_k >= 0 && (ev->type() == QEvent::MouseMove || ev->type() == QEvent::MouseButtonRelease)) {
        auto *m = static_cast<QMouseEvent *>(ev);
        QWidget *w = vue.controles.value(libre_k);
        if (!w) { libre_k = -1; return true; }
        const QPoint d = m->globalPosition().toPoint() - depart;
        if (ev->type() == QEvent::MouseMove && (m->buttons() & Qt::LeftButton)) {
            if (!libre_bouge && d.manhattanLength() < QApplication::startDragDistance()) return true;
            libre_bouge = true;
            if (const int vise = onglet_sous(m->globalPosition().toPoint(), libre_k); vise >= 0) {   // survoler un onglet l'ouvre
                const int ko = onglets_de(vise);
                if (auto *tw = qobject_cast<QTabWidget *>(vue.controles.value(ko)))
                    tw->setCurrentWidget(vue.controles.value(vise));
                etat->setText("Lâche ici pour mettre l'élément dans cet onglet.");
            }
            const bool vers_page = lus[courant].elements.value(libre_k).sorte != ELEMENT_ONGLETS
                                   && page_sous(m->globalPosition().toPoint(), libre_k, nullptr) >= 0;
            const struct Fin {   // à la sortie du geste : la barre d'état annonce l'onglet visé, par-dessus « Place : … »
                QLabel *etat; bool vers_page;
                ~Fin() { if (vers_page) etat->setText("Lâche ici : l'élément ira dans cet onglet, et seulement là."); }
            } fin{etat, vers_page};
            const bool pixel = m->modifiers() & Qt::AltModifier;
            auto cale = [pixel](int v) { return pixel ? qMax(0, v) : aimanter(v); };
            QRect r = libre_depart;
            if (libre_taille) r.setSize(QSize((libre_taille & 1) ? qMax(8, cale(r.width() + d.x())) : r.width(),
                                              (libre_taille & 2) ? qMax(8, cale(r.height() + d.y())) : r.height()));
            else r.moveTopLeft(QPoint(cale(r.x() + d.x()), cale(r.y() + d.y())));
            {   // les guides : un bord ou un centre près de celui d'un voisin s'y colle, plus fort que la grille (§ 5 octies)
                QRect brut = libre_depart;
                if (libre_taille) brut.setSize(QSize(brut.width() + ((libre_taille & 1) ? d.x() : 0), brut.height() + ((libre_taille & 2) ? d.y() : 0)));
                else brut.translate(d);
                QVector<QRect> voisins;
                QWidget *sur = libre_decrit ? vue.vue : w->parentWidget();
                for (int q = 0; q < vue.controles.size(); q++) {
                    QWidget *x = vue.controles[q];
                    if (!x || q == libre_k || x == w || x->isAncestorOf(w) || w->isAncestorOf(x) || x->objectName() == "toile") continue;
                    if (libre_decrit) voisins << QRect(x->mapTo(vue.vue, QPoint(0, 0)), x->size());
                    else if (x->parentWidget() == sur) voisins << x->geometry();
                }
                if (!pixel) aligner(r, brut, voisins, sur);
                else aligner(r, QRect(-9999, -9999, 1, 1), {}, sur);   // Alt : ni grille ni guides ; les traits s'effacent
            }
            if (libre_decrit) {   // le fantôme, la grille sur tout l'aperçu
                if (!fantome) {
                    fantome = new QFrame(vue.vue);
                    fantome->setAttribute(Qt::WA_TransparentForMouseEvents);
                    fantome->setStyleSheet("border: 2px dashed " + Theme::courant().hex("accent") + "; background: transparent;");
                }
                if (!grille || grille->parentWidget() != vue.vue) { delete grille; grille = new Grille(vue.vue); }
                grille->setGeometry(vue.vue->rect());
                grille->show();
                grille->lower();   // sous les éléments, qui restent visibles
                fantome->setGeometry(r);
                libre_fin = r;
                fantome->show();
                fantome->raise();
                etat->setText(libre_taille ? QString("Taille : %1 × %2 pixels").arg(r.width()).arg(r.height())
                                           : QString("Place : glisse où tu veux%1").arg(pixel ? "" : " (grille de 8 ; Alt : au pixel)"));
                return true;
            }
            w->setGeometry(r);
            QWidget *toile = w->parentWidget();
            if (!grille || grille->parentWidget() != toile) { delete grille; grille = new Grille(toile); }
            grille->setGeometry(0, 0, qMax(toile->width(), r.right() + 64), qMax(toile->height(), r.bottom() + 64));
            toile->setMinimumSize(grille->size());
            grille->show();
            grille->lower();
            w->raise();
            etat->setText(libre_taille ? QString("Taille : %1 × %2 pixels").arg(r.width()).arg(r.height())
                                       : QString("Place : %1 de la gauche, %2 du haut%3").arg(r.x()).arg(r.y())
                                             .arg(pixel ? "" : " (grille de 8 ; Alt : au pixel)"));
            return true;
        }
        if (ev->type() == QEvent::MouseButtonRelease) {
            const int k = libre_k;
            libre_k = -1;
            if (grille) grille->hide();
            if (guide_v) guide_v->hide();
            if (guide_h) guide_h->hide();
            if (fantome) { fantome->hide(); fantome->deleteLater(); fantome = nullptr; }
            if (!libre_bouge) return true;
            if (const int vise = onglet_sous(m->globalPosition().toPoint(), k);
                vise >= 0 && lus[courant].elements.value(k).sorte != ELEMENT_ONGLETS) {   // lâché sur un onglet : rangé dedans
                const QString ecran = lus[courant].nom;
                element = -1;
                emit geste([=](Geste *g) { return ecran_mettre_dans(dossier, ecran, k, vise, g); });
                return true;
            }
            {   // lâché dans la page ouverte d'un onglet : il y est rangé, là où on l'a lâché, et n'est plus que sur cet onglet
                QWidget *toile = nullptr;
                const int vise = lus[courant].elements.value(k).sorte == ELEMENT_ONGLETS ? -1
                                 : page_sous(m->globalPosition().toPoint(), k, &toile);
                if (vise >= 0) {
                    const QPoint coin = libre_decrit ? vue.vue->mapToGlobal(libre_fin.topLeft())
                                                     : w->parentWidget()->mapToGlobal(w->geometry().topLeft());
                    QPoint ou(-1, -1);
                    if (toile) {
                        const QPoint l = toile->mapFromGlobal(coin);
                        ou = QPoint(aimanter(qMax(0, l.x())), aimanter(qMax(0, l.y())));
                    }
                    const QString ecran = lus[courant].nom;
                    element = -1;
                    emit geste([=](Geste *g) { return ecran_mettre_dans(dossier, ecran, k, vise, g, ou); });
                    return true;
                }
            }
            if (libre_decrit) {
                // un écran qui a déjà un bloc libre : l'élément décrit y entre, là où on l'a lâché (le bloc libre sous la
                // souris, sinon le premier) ; sans bloc libre, tout l'écran passe en position libre
                QWidget *toile = nullptr;
                int bloc = bloc_libre_sous(m->globalPosition().toPoint(), w, &toile);
                if (bloc < 0)   // aucun sous la souris : le premier bloc libre de l'écran, s'il y en a un
                    for (int q = 0; q < vue.controles.size() && bloc < 0; q++)
                        if (lus[courant].elements.value(q).sorte == ELEMENT_LIBRE && vue.controles[q]) { bloc = q; toile = vue.controles[q]; }
                if (bloc < 0) { liberer(k, libre_fin); return true; }
                const QPoint l = toile->mapFromGlobal(vue.vue->mapToGlobal(libre_fin.topLeft()));
                const QPoint ou(aimanter(qMax(0, l.x())), aimanter(qMax(0, l.y())));
                const QString ecran = lus[courant].nom;
                element = -1;
                emit geste([=](Geste *g) { return ecran_mettre_dans(dossier, ecran, k, bloc, g, ou); });
                return true;
            }
            const QRect r = w->geometry();
            if (!libre_taille) {   // lâché dans un autre bloc libre (l'écran, le bloc d'un groupe…) : il y passe, à cet endroit
                QWidget *toile = nullptr;
                int bloc = bloc_libre_sous(m->globalPosition().toPoint(), w, &toile);
                if (bloc < 0 && centre->rect().contains(centre->mapFromGlobal(m->globalPosition().toPoint())))
                    for (int q = 0; q < vue.controles.size() && bloc < 0; q++)   // ailleurs sur l'écran : son bloc libre de premier niveau
                        if (lus[courant].elements.value(q).sorte == ELEMENT_LIBRE && vue.controles[q]
                            && vue.controles[q]->objectName() == "toile" && !vue.controles[q]->property("place").toBool()) {
                            bloc = q;
                            toile = vue.controles[q];
                        }
                if (bloc >= 0 && toile != w->parentWidget()) {
                    const QPoint l = toile->mapFromGlobal(w->parentWidget()->mapToGlobal(r.topLeft()));
                    const QPoint ou(aimanter(qMax(0, l.x())), aimanter(qMax(0, l.y())));
                    const QString ecran = lus[courant].nom;
                    element = -1;
                    emit geste([=](Geste *g) { return ecran_mettre_dans(dossier, ecran, k, bloc, g, ou); });
                    return true;
                }
            }
            if (libre_taille) placer(k, libre_depart.x(), libre_depart.y(), (libre_taille & 1) ? r.width() : -1,
                                     (libre_taille & 2) ? r.height() : -1);
            else placer(k, r.x(), r.y(), -1, -1);
            return true;
        }
    }
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
    if ((ev->type() == QEvent::DragEnter || ev->type() == QEvent::DragMove || ev->type() == QEvent::Drop)
        && !static_cast<QDropEvent *>(ev)->mimeData()->urls().isEmpty() && courant >= 0) {
        // une image glissée depuis le Finder : copiée dans le projet, ajoutée à l'écran, dans le bloc sous la souris
        auto *d = static_cast<QDropEvent *>(ev);
        QStringList images;
        for (const QUrl &u : d->mimeData()->urls()) {
            const QString suffixe = QFileInfo(u.toLocalFile()).suffix().toLower();
            if (u.isLocalFile() && QStringList({"png", "jpg", "jpeg", "gif", "webp"}).contains(suffixe)) images << u.toLocalFile();
        }
        if (images.isEmpty()) { ev->ignore(); return true; }
        d->acceptProposedAction();
        if (ev->type() != QEvent::Drop) return true;
        QWidget *toile = nullptr;
        const QPoint global = qobject_cast<QWidget *>(o)->mapToGlobal(d->position().toPoint());
        int dans = bloc_libre_sous(global, nullptr, &toile);
        if (dans < 0) dans = cible_ajout();
        const QString ecran = lus[courant].nom;
        for (const QString &f : images) {
            QString erreur;
            ElementNouveau n;
            n.sorte = ELEMENT_IMAGE;
            n.texte = image_dans_le_projet(dossier, f, &erreur);
            if (n.texte.isEmpty()) { QMessageBox::warning(this, "Nouvelle image", erreur); continue; }
            emit geste([=](Geste *g) { return ecran_ajouter(dossier, ecran, n, g, dans); });
        }
        etat->setText(images.size() == 1 ? "Image ajoutée, copiée dans le projet." : "Images ajoutées, copiées dans le projet.");
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
    if (ev->type() == QEvent::MouseButtonPress && qobject_cast<QTabBar *>(o)) return false;   // changer d'onglet dans l'aperçu
    if (ev->type() == QEvent::MouseButtonPress) {
        int trouve = element_sous(o);
        glisse = trouve;   // peut-être le début d'un glisser
        depart = static_cast<QMouseEvent *>(ev)->globalPosition().toPoint();
        choisir_element(trouve);
        centre->setFocus();   // les flèches vont à l'aperçu
        // le vide d'une page : la prise est le bloc d'onglets entier (déplacer, ou agrandir par ses bords)
        if (courant >= 0 && lus[courant].elements.value(trouve).sorte == ELEMENT_ONGLET) {
            const int ko = onglets_de(trouve);
            if (ko >= 0) trouve = ko;
        }
        QWidget *w = vue.controles.value(trouve);
        if (w && courant >= 0 && lus[courant].elements.value(trouve).place) {   // un élément en position libre (§ 5 octies)
            glisse = -1;
            libre_k = trouve;
            libre_bouge = false;
            libre_decrit = false;
            libre_depart = w->geometry();
            libre_taille = bord_sous(w, depart);   // un bord ou le coin : la taille ; ailleurs : la place
            etat->setText(libre_taille ? "Glisse pour changer la taille (Alt : au pixel)." : "Glisse pour placer (Alt : au pixel).");
        } else if (w && !(static_cast<QMouseEvent *>(ev)->modifiers() & Qt::ControlModifier) && courant >= 0
                   && lus[courant].elements.value(trouve).sorte != ELEMENT_LIBRE) {
            // un écran décrit : tout se place quand même ; un fantôme suit la souris, le lâcher passe l'écran en position
            // libre en gardant ce qu'on voit, cet élément à sa nouvelle place (§ 5 octies). Cmd (Ctrl) : remettre dans l'ordre.
            glisse = -1;
            libre_k = trouve;
            libre_bouge = false;
            libre_decrit = true;
            libre_depart = rect_vu(trouve);
            libre_taille = bord_sous(w, depart);
            etat->setText(libre_taille ? "Glisse pour changer la taille : l'écran passera en position libre."
                                       : "Glisse pour placer : l'écran passera en position libre (Cmd : remettre dans l'ordre).");
        }
    }
    if (ev->type() == QEvent::MouseButtonRelease) glisse = -1;
    return true;   // l'aperçu ne s'utilise pas : ni clic, ni saisie, ni double-clic
}

void OngletEcrans::choisir_element(int k) {
    for (QWidget *w : vue.controles)
        if (w) w->setStyleSheet(QString());
    if (poignee) { poignee->hide(); poignee->deleteLater(); poignee = nullptr; }
    for (QWidget *w : vue.controles)   // le contour pointillé des blocs libres, que la ligne précédente a effacé
        if (w && w->objectName() == "toile") w->setStyleSheet("#toile{border:1px dashed " + Theme::courant().hex("texte-2") + ";}");
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
    if (QWidget *w = vue.controles.value(k); w && x.place && w->parentWidget()) {   // la poignée de taille, dans le coin
        poignee = new QFrame(w->parentWidget());
        poignee->setAttribute(Qt::WA_TransparentForMouseEvents);   // le geste reste celui de l'élément (son coin)
        poignee->setStyleSheet("background: " + Theme::courant().hex("accent") + ";");
        poignee->setGeometry(w->geometry().right() - 7, w->geometry().bottom() - 7, 8, 8);
        poignee->setToolTip("Glisser le coin : la taille ; flèches : la place ; Alt + flèches : la taille");
        poignee->show();
        poignee->raise();
    }
    static const char *const noms[] = {"Liste", "Bouton", "Texte", "Zone de saisie", "Côte à côte", "L'un sous l'autre",
                                       "Fin de bloc", "Groupe", "Image", "En position libre", "Onglets", "Onglet", "Graphique", "Menu"};
    h = QString("<p><b>%1</b></p><table>").arg(noms[qBound(0, x.sorte, 13)]);
    if (x.place) h += ligne("Place", QString("%1 de la gauche, %2 du haut").arg(x.gauche).arg(x.haut))
                    + ligne("Taille", QString("%1 × %2").arg(x.largeur ? QString::number(x.largeur) : QString("naturelle"),
                                                             x.hauteur ? QString::number(x.hauteur) : QString("naturelle")));
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
        bool deja = false;
        for (const auto &x : e.elements) deja |= x.sorte == ELEMENT_LIBRE;
        if (!deja) {   // « Mettre en position libre » : chaque élément là où on le voit (§ 5 octies)
            auto *liberer_b = new QPushButton("Mettre en position libre");
            liberer_b->setToolTip("Chaque élément garde la place et la largeur qu'il a dans l'aperçu ; ensuite, il se glisse au pixel.");
            f->addRow("", liberer_b);
            connect(liberer_b, &QPushButton::clicked, this, [this] { liberer(); });
        }
        connect(appliquer, &QPushButton::clicked, this, [this, ecran, titre] {
            const QString t = titre->text();
            emit geste([=](Geste *g) { return ecran_titre(dossier, ecran, t, g); });
        });
    } else {
        const ElementLu &x = e.elements[element];
        const int k = element;
        if (x.sorte == ELEMENT_ONGLET) {   // tout le bloc d'onglets, d'un clic (§ 5 decies)
            auto *tous = new QPushButton("Supprimer les onglets");
            tous->setToolTip("Tous les onglets et leur contenu ; Cmd+Z les rend.");
            f->addRow("", tous);
            const int ko = onglets_de(k);
            connect(tous, &QPushButton::clicked, this, [this, ecran, ko] {
                element = -1;
                emit geste([=](Geste *g) { return ecran_supprimer(dossier, ecran, ko, g); });
            });
        }
        if (const int ko = onglets_de(k); ko >= 0 && x.sorte != ELEMENT_ONGLET && x.sorte != ELEMENT_FIN_DE_BLOC) {
            // un élément d'un onglet : le ranger dans un autre onglet des mêmes onglets
            auto *vers = new QComboBox;
            int prof = 0;
            for (int q = ko + 1; q < e.elements.size(); q++) {
                const int s = e.elements[q].sorte;
                if (s == ELEMENT_FIN_DE_BLOC) { if (prof-- == 0) break; continue; }
                if (est_bloc(s)) {
                    if (prof == 0 && s == ELEMENT_ONGLET) vers->addItem(e.elements[q].texte, q);
                    prof++;
                }
            }
            auto *ranger = new QPushButton("Mettre dans l'onglet");
            auto *ligne = new QHBoxLayout;
            ligne->addWidget(vers, 1);
            ligne->addWidget(ranger);
            f->addRow("Onglet", ligne);
            connect(ranger, &QPushButton::clicked, this, [this, ecran, k, vers] {
                const int vise = vers->currentData().toInt();
                element = -1;
                emit geste([=](Geste *g) { return ecran_mettre_dans(dossier, ecran, k, vise, g); });
            });
        }
        if (x.sorte == ELEMENT_GROUPE || x.sorte == ELEMENT_ONGLET || x.sorte == ELEMENT_MENU) {   // son titre (§ 22.7, § 22.10, § 22.12)
            auto *titre = new QLineEdit(x.texte);
            f->addRow(x.sorte == ELEMENT_GROUPE ? "Titre du groupe" : "Titre de l'onglet", titre);
            const int sorte = x.sorte;
            connect(appliquer, &QPushButton::clicked, this, [this, ecran, k, titre, sorte] {
                ElementNouveau n;
                n.sorte = sorte;
                n.texte = titre->text();
                emit geste([=](Geste *g) { return ecran_modifier(dossier, ecran, k, n, g); });
            });
        } else if (x.sorte == ELEMENT_IMAGE && !x.source.isEmpty()) {   // « l'image de … » : la valeur, « visible si » (§ 22.8)
            auto *source = new QLineEdit(x.source);
            source->setToolTip("Une valeur image, sans « de » : « le portrait du compositeur choisi de l'écran ».");
            auto *visible = condition_ecran(x.visible_si, "le compositeur choisi de l'écran est présent");
            f->addRow("Image de", source);
            f->addRow("Visible si", visible);
            connect(appliquer, &QPushButton::clicked, this, [this, ecran, k, source, visible] {
                ElementNouveau n;
                n.sorte = ELEMENT_IMAGE;
                n.source = source->text();
                n.visible_si = visible->text();
                emit geste([=](Geste *g) { return ecran_modifier(dossier, ecran, k, n, g); });
            });
        } else if (x.sorte == ELEMENT_IMAGE) {   // « l'image « … » » : le fichier, « visible si » (§ 22.8)
            auto *chemin = new QLineEdit(x.texte);
            chemin->setToolTip("Relatif au dossier du programme ; PNG, JPEG, GIF ou WebP.");
            auto *visible = condition_ecran(x.visible_si, "le compositeur choisi de l'écran est présent");
            auto *choisir = new QPushButton(QString::fromUtf8("Choisir…"));   // une autre image, d'où que ce soit
            choisir->setToolTip("Une image prise ailleurs que dans le projet y est copiée.");
            connect(choisir, &QPushButton::clicked, this, [this, chemin] {
                const QString fi = QFileDialog::getOpenFileName(this, "Image", dossier, "Images (*.png *.jpg *.jpeg *.gif *.webp)");
                if (fi.isEmpty()) return;
                QString erreur;
                const QString r = image_dans_le_projet(dossier, fi, &erreur);
                if (r.isEmpty()) { QMessageBox::warning(this, "Image", erreur); return; }
                chemin->setText(r);
            });
            auto *ligne = new QHBoxLayout;
            ligne->addWidget(chemin, 1);
            ligne->addWidget(choisir);
            f->addRow("Fichier", ligne);
            f->addRow("Visible si", visible);
            connect(appliquer, &QPushButton::clicked, this, [this, ecran, k, chemin, visible] {
                ElementNouveau n;
                n.sorte = ELEMENT_IMAGE;
                n.texte = chemin->text();
                n.visible_si = visible->text();
                emit geste([=](Geste *g) { return ecran_modifier(dossier, ecran, k, n, g); });
            });
        } else if (x.sorte == ELEMENT_BOUTON || x.sorte == ELEMENT_TEXTE) {
            auto *texte = new QLineEdit(x.texte);
            f->addRow(x.sorte == ELEMENT_BOUTON ? "Libellé" : "Texte", texte);
            QLineEdit *actif = nullptr;
            if (x.sorte == ELEMENT_BOUTON) {
                actif = condition_ecran(x.actif_si, "le nom de l'écran est présent");
                f->addRow("Actif si", actif);
            }
            auto *visible = condition_ecran(x.visible_si, "le compositeur choisi de l'écran est présent");
            f->addRow("Visible si", visible);
            const int sorte = x.sorte;
            connect(appliquer, &QPushButton::clicked, this, [this, ecran, k, texte, sorte, actif, visible] {
                ElementNouveau n;
                n.sorte = sorte;
                n.texte = texte->text();
                if (actif) n.actif_si = actif->text();
                n.visible_si = visible->text();
                emit geste([=](Geste *g) { return ecran_modifier(dossier, ecran, k, n, g); });
            });
        } else if (x.sorte == ELEMENT_ZONE) {
            auto *nom = new QLineEdit(x.texte);
            auto *type = new QComboBox;
            type->addItems({"texte", "nombre", "nombre entier", "vrai ou faux", "date", "année", "heure"});
            for (const auto &en : lire_schema(dossier)) type->addItem(en.nom);
            type->setCurrentIndex(qMax(0, type->findText(x.type)));
            auto *feminin = new QCheckBox("féminin (une …)");
            feminin->setChecked(x.feminin);   // sans quoi « Appliquer » rendait masculine une zone féminine
            auto *facultative = new QCheckBox("facultative");
            facultative->setChecked(x.facultatif);
            auto *depart = new QLineEdit(x.depart);
            auto *valeurs = new QLineEdit(x.valeurs.join(" ; "));
            valeurs->setPlaceholderText("aucune ; ou « actif ; suspendu ; retiré » pour une zone (texte)");
            valeurs->setToolTip("Les seules valeurs acceptées, séparées par « ; ». Jusqu'à quatre, l'écran montre des "
                                "boutons radio ; au-delà, un menu (§ 22.7).");
            auto *lignes = new QCheckBox("sur plusieurs lignes (texte)");
            lignes->setChecked(x.plusieurs_lignes);
            auto *seule = new QCheckBox("en lecture seule");
            seule->setChecked(x.lecture_seule);
            auto *inter = new QCheckBox("en interrupteur (vrai ou faux)");   // § 22.6
            inter->setChecked(x.interrupteur);
            auto *cherche = new QCheckBox("avec recherche (un objet parmi beaucoup)");   // § 22.6
            cherche->setChecked(x.recherche);
            auto *masq = new QCheckBox("masqué (un mot de passe)");   // § 22.6
            masq->setChecked(x.masque);
            auto *bmin = new QLineEdit(x.borne_min), *bmax = new QLineEdit(x.borne_max);   // les bornes (§ 22.6)
            bmin->setPlaceholderText("aucune");
            bmax->setPlaceholderText(x.type == "texte" ? "aucune (en caractères)" : "aucune");
            auto *indic = new QLineEdit(x.indication);   // « , avec l'indication « … » » (§ 22.6)
            indic->setPlaceholderText("une phrase sous le libellé (facultative)");
            auto *actif = condition_ecran(x.actif_si, "le compositeur choisi de l'écran est présent");
            auto *visible = condition_ecran(x.visible_si, "le pays de l'écran est « Suisse »");
            f->addRow("Nom", nom);
            f->addRow("Type", type);
            f->addRow("", feminin);
            f->addRow("", facultative);
            f->addRow("Au départ", depart);
            f->addRow("Valeurs permises", valeurs);
            f->addRow("", lignes);
            f->addRow("", seule);
            f->addRow("", inter);
            f->addRow("", cherche);
            f->addRow("", masq);
            f->addRow("Indication", indic);
            f->addRow("Au moins", bmin);
            f->addRow("Au plus", bmax);
            f->addRow("Active si", actif);
            f->addRow("Visible si", visible);
            connect(appliquer, &QPushButton::clicked, this, [=] {
                ElementNouveau n;
                n.sorte = ELEMENT_ZONE;
                n.texte = nom->text();
                n.type = type->currentText();
                n.feminin = feminin->isChecked();
                n.facultatif = facultative->isChecked();
                n.depart = depart->text();
                n.plusieurs_lignes = lignes->isChecked();
                for (const QString &v : valeurs->text().split(';')) if (!v.trimmed().isEmpty()) n.valeurs << v.trimmed();
                n.lecture_seule = seule->isChecked();
                n.interrupteur = inter->isChecked();
                n.recherche = cherche->isChecked();
                n.masque = masq->isChecked();
                n.indication = indic->text().trimmed();
                n.borne_min = bmin->text().trimmed();
                n.borne_max = bmax->text().trimmed();
                n.multiple = x.multiple;   // une zone multiple le reste
                n.actif_si = actif->text();
                n.visible_si = visible->text();
                emit geste([=](Geste *g) { return ecran_modifier(dossier, ecran, k, n, g); });
            });
        } else if (x.sorte == ELEMENT_GRAPHIQUE) {   // le regroupement, compter ou additionner, barres ou secteurs (§ 22.11)
            EntiteSchema en;
            for (const auto &y : lire_schema(dossier)) if (y.nom == x.texte) en = y;
            auto sans_article = [](QString s) {
                for (const char *a : {"le ", "la ", "l'"}) if (s.startsWith(QString::fromUtf8(a))) return s.mid(QString::fromUtf8(a).size());
                return s;
            };
            auto *par = new QComboBox;    // donnée : « écrit ␝ chemin » tel qu'écrit, ou le nom du champ
            auto *valeur = new QComboBox;
            valeur->addItem("Le nombre d'objets", QString());
            for (const auto &c : en.champs) {
                if (c.multiple || c.type == "fichier" || c.type == "image") continue;
                par->addItem(c.nom, c.nom);
                if (c.type.startsWith("nombre")) valeur->addItem("La somme de « " + c.nom + " »", c.nom);
            }
            const QString cle = sans_article(x.graphe_cle.section(QChar(0x1d), 0, 0));
            int i = par->findText(cle);
            if (i < 0 && !cle.isEmpty()) { par->addItem(cle, x.graphe_cle); i = par->count() - 1; }   // un chemin : « catégorie du membre »
            par->setCurrentIndex(qMax(0, i));
            const QString somme = sans_article(x.graphe_somme.section(QChar(0x1d), 0, 0));
            int j = somme.isEmpty() ? 0 : valeur->findData(somme);
            if (j < 0) { valeur->addItem("La somme de « " + somme + " »", x.graphe_somme); j = valeur->count() - 1; }
            valeur->setCurrentIndex(j);
            auto *forme = new QComboBox;
            forme->addItems({"Barres", "Secteurs"});
            forme->setCurrentIndex((x.graphique & 2) ? 1 : 0);
            f->addRow("Regroupé par", par);
            f->addRow("Valeur", valeur);
            f->addRow("Forme", forme);
            connect(appliquer, &QPushButton::clicked, this, [=] {
                ElementNouveau n;
                n.sorte = ELEMENT_GRAPHIQUE;
                n.cle = par->currentData().toString();
                n.somme = valeur->currentData().toString();
                n.secteurs = forme->currentIndex() == 1;
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
        if (x.place) {   // la place et la taille au pixel (§ 22.9) ; 0 : la taille naturelle
            auto mesure = [](int v, int min) {
                auto *s = new QSpinBox;
                s->setRange(min, 4000);
                s->setValue(v);
                s->setSuffix(" px");
                if (min == 0) s->setSpecialValueText("naturelle");
                return s;
            };
            auto *g = mesure(x.gauche, 0), *h = mesure(x.haut, 0), *l = mesure(x.largeur, 0), *ht = mesure(x.hauteur, 0);
            g->setSpecialValueText(QString());
            h->setSpecialValueText(QString());
            f->addRow("Gauche", g);
            f->addRow("Haut", h);
            f->addRow("Largeur", l);
            f->addRow("Hauteur", ht);
            auto *placer_b = new QPushButton("Placer");
            f->addRow("", placer_b);
            connect(placer_b, &QPushButton::clicked, this, [this, k, g, h, l, ht] {
                if ((l->value() && l->value() < 8) || (ht->value() && ht->value() < 8)) { etat->setText("La taille va de 8 à 4000 pixels."); return; }
                placer(k, g->value(), h->value(), l->value(), ht->value());
            });
        }
        if ((x.sorte == ELEMENT_BOUTON || x.sorte == ELEMENT_TEXTE || x.sorte == ELEMENT_ZONE || x.sorte == ELEMENT_LISTE
            || x.sorte == ELEMENT_IMAGE) && !x.place) {
            auto *grouper = new QPushButton("Mettre dans un groupe…");   // « le groupe « … » : » (§ 22.7)
            f->addRow("", grouper);
            connect(grouper, &QPushButton::clicked, this, [this, ecran, k] {
                bool ok = false;
                const QString t = QInputDialog::getText(this, "Grouper", "Titre du groupe (« Adresse ») :", QLineEdit::Normal, "", &ok);
                if (ok) emit geste([=](Geste *g) { return ecran_grouper(dossier, ecran, k, t, g); });
            });
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
    // le bloc et la place d'un clic droit (§ 5 terdecies) ; sinon la page ouverte, le groupe choisi… (§ 5 decies)
    const int dans = dans_clic >= -1 ? dans_clic : cible_ajout();
    const QPoint ou = dans_clic >= -1 ? ou_clic : QPoint(-1, -1);
    dans_clic = -2;
    if (sorte == ELEMENT_MENU) {   // un menu en tête de l'écran, avec un élément et son événement (§ 22.12)
        emit geste([=](Geste *g) { return ecran_ajouter_menu(dossier, ecran, g); });
        return;
    }
    if (sorte == ELEMENT_ONGLETS) {   // deux onglets à renommer, un texte dans chacun (§ 22.10)
        emit geste([=](Geste *g) { return ecran_ajouter_onglets(dossier, ecran, g); });
        return;
    }
    ElementNouveau n;
    n.sorte = sorte;
    bool ok = false;
    if (sorte == CHOIX_MULTIPLES_PALETTE) {   // « des genres (genre) » : l'entité, puis le nom au pluriel (§ 22.1)
        QStringList entites;
        const auto schema = lire_schema(dossier);
        for (const auto &en : schema) entites << en.nom;
        if (entites.isEmpty()) { etat->setText("Des choix multiples se font parmi les objets d'une entité : déclare-en une d'abord."); return; }
        const QString en = QInputDialog::getItem(this, "Choix multiples", "Parmi les objets de l'entité :", entites, 0, false, &ok);
        if (!ok) return;
        QString pluriel = en + "s";
        for (const auto &x : schema) if (x.nom == en && !x.pluriel.isEmpty()) pluriel = x.pluriel;
        n.sorte = ELEMENT_ZONE;
        n.type = en;
        n.multiple = true;
        n.texte = QInputDialog::getText(this, "Choix multiples", "Nom, au pluriel :", QLineEdit::Normal, pluriel, &ok);
        if (!ok || n.texte.trimmed().isEmpty()) return;
        emit geste([=](Geste *g) { return ecran_ajouter(dossier, ecran, n, g, dans, ou); });
        return;
    }
    if (sorte == FICHIER_PALETTE) {   // une zone de fichier ou d'image (§ 22.1)
        const QStringList sortes = {"Un fichier (un contrat, un document…)", "Une image (une photo…)"};
        const QString quoi = QInputDialog::getItem(this, "Nouvelle zone de fichier", "On y choisit :", sortes, 0, false, &ok);
        if (!ok) return;
        n.sorte = ELEMENT_ZONE;
        n.type = quoi == sortes[1] ? "image" : "fichier";
        n.feminin = n.type == "image";
        n.texte = QInputDialog::getText(this, "Nouvelle zone de fichier", "Nom (« contrat », « photo ») :", QLineEdit::Normal,
                                        n.type == "image" ? "photo" : "contrat", &ok);
        if (!ok || n.texte.trimmed().isEmpty()) return;
        emit geste([=](Geste *g) { return ecran_ajouter(dossier, ecran, n, g, dans, ou); });
        return;
    }
    if (sorte == INTERRUPTEUR_PALETTE) {   // une zone (vrai ou faux), montrée en interrupteur (§ 22.6)
        n.sorte = ELEMENT_ZONE;
        n.type = "vrai ou faux";
        n.interrupteur = true;
        n.texte = QInputDialog::getText(this, "Nouvel interrupteur", "Nom (« actif », « payée ») :", QLineEdit::Normal, "", &ok);
        if (!ok || n.texte.trimmed().isEmpty()) return;
        emit geste([=](Geste *g) { return ecran_ajouter(dossier, ecran, n, g, dans, ou); });
        return;
    }
    if (sorte == ELEMENT_IMAGE) {   // un fichier du projet, ou une valeur (§ 22.8)
        const QStringList sortes = {"Une image de l'ordinateur (copiée dans le projet)", "Une image calculée (la photo de la ligne choisie…)"};
        const QString quoi = QInputDialog::getItem(this, "Nouvelle image", "L'image montre :", sortes, 0, false, &ok);
        if (!ok) return;
        if (quoi == sortes[1]) {
            n.source = QInputDialog::getText(this, "Nouvelle image", "Image de (« le portrait du compositeur choisi de l'écran ») :",
                                             QLineEdit::Normal, "", &ok);
            if (!ok) return;
            emit geste([=](Geste *g) { return ecran_ajouter(dossier, ecran, n, g, dans, ou); });
            return;
        }
    }
    if (sorte == ELEMENT_IMAGE) {   // une image du projet, écrite relative au dossier du programme (§ 22.8)
        // n'importe où sur l'ordinateur : une image d'ailleurs est copiée dans le projet, pour partir avec l'application
        const QString images = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);   // tes images d'abord
        const QString f = QFileDialog::getOpenFileName(this, "Nouvelle image", images.isEmpty() ? dossier : images,
                                                       "Images (*.png *.jpg *.jpeg *.gif *.webp)");
        ok = !f.isEmpty();
        if (!ok) return;
        QString erreur;
        n.texte = image_dans_le_projet(dossier, f, &erreur);
        if (n.texte.isEmpty()) { QMessageBox::warning(this, "Nouvelle image", erreur); return; }
        if (QDir(dossier).relativeFilePath(f) != n.texte) etat->setText(QString("Image copiée dans le projet : « %1 ».").arg(n.texte));
    } else if (sorte == ELEMENT_BOUTON) n.texte = QInputDialog::getText(this, "Nouveau bouton", "Libellé :", QLineEdit::Normal, "", &ok);
    else if (sorte == ELEMENT_TEXTE) n.texte = QInputDialog::getText(this, "Nouveau texte", "Texte :", QLineEdit::Normal, "", &ok);
    else if (sorte == ELEMENT_ZONE) {
        n.texte = QInputDialog::getText(this, "Nouvelle zone", "Nom (« pays », « date de début ») :", QLineEdit::Normal, "", &ok);
        if (ok) {
            QStringList types = {"texte", "nombre", "nombre entier", "vrai ou faux", "date", "année", "heure", "fichier", "image"};
            for (const auto &en : lire_schema(dossier)) types << en.nom;
            n.type = QInputDialog::getItem(this, "Nouvelle zone", "Type :", types, 0, false, &ok);
        }
    } else if (sorte == ELEMENT_GRAPHIQUE) {   // l'entité, puis le champ qui regroupe (§ 22.11)
        const auto schema = lire_schema(dossier);
        QStringList entites;
        for (const auto &en : schema) entites << en.nom;
        if (entites.isEmpty()) { etat->setText("Un graphique compte des objets conservés : déclare d'abord une entité."); return; }
        n.texte = QInputDialog::getItem(this, "Nouveau graphique", "Compter les objets de l'entité :", entites, 0, false, &ok);
        if (!ok) return;
        QStringList champs;
        for (const auto &en : schema)
            if (en.nom == n.texte)
                for (const auto &c : en.champs)
                    if (c.type != "fichier" && c.type != "image") champs << c.nom;
        if (champs.isEmpty()) return;
        n.cle = QInputDialog::getItem(this, "Nouveau graphique", "Regroupés par :", champs, 0, false, &ok);
    } else {
        QStringList entites;
        for (const auto &en : lire_schema(dossier)) entites << en.nom;
        if (entites.isEmpty()) return;
        n.texte = QInputDialog::getItem(this, "Nouvelle liste", "Entité :", entites, 0, false, &ok);
    }
    if (!ok) return;
    emit geste([=](Geste *g) { return ecran_ajouter(dossier, ecran, n, g, dans, ou); });
}

QString OngletEcrans::proprietes() const { return panneau->toPlainText(); }
