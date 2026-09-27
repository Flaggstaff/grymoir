// GrymoiR : l'atelier, aide : la grammaire du langage.
#include "aide.h"

#include <QFile>
#include <QHash>
#include <QVector>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

Aide::Aide(QWidget *parent, const QString &ressource, const QString &titre) : QMainWindow(parent) {
    setWindowTitle(titre);
    lecteur = new QTextBrowser;
    lecteur->setOpenExternalLinks(true);
    QFile f(ressource);   // embarquée par CMake (qt_add_resources)
    if (f.open(QIODevice::ReadOnly)) lecteur->setMarkdown(QString::fromUtf8(f.readAll()));
    else lecteur->setPlainText(QString("« %1 » n'a pas été embarqué dans cet exécutable.").arg(titre));

    // La table des matières se lit dans le document rendu : ses titres de niveaux 2 et 3.
    sommaire = new QTreeWidget;
    sommaire->header()->hide();
    QTreeWidgetItem *section = nullptr;
    for (QTextBlock b = lecteur->document()->begin(); b.isValid(); b = b.next()) {
        const int niveau = b.blockFormat().headingLevel();
        if (niveau != 2 && niveau != 3) continue;
        QTreeWidgetItem *i = niveau == 2 || !section ? new QTreeWidgetItem(sommaire) : new QTreeWidgetItem(section);
        i->setText(0, b.text());
        i->setData(0, Qt::UserRole, b.position());
        if (niveau == 2) section = i;
    }
    connect(sommaire, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *i) {
        aller_au_bloc(i->data(0, Qt::UserRole).toInt());
    });

    recherche = new QLineEdit;
    recherche->setPlaceholderText("Chercher (Entrée : suivant, Maj+Entrée : précédent)");
    recherche->setClearButtonEnabled(true);
    etat = new QLabel;
    connect(recherche, &QLineEdit::returnPressed, this, [this] { chercher(recherche->text()); });
    auto *precedent = new QShortcut(QKeySequence("Shift+Return"), recherche);
    connect(precedent, &QShortcut::activated, this, [this] { chercher(recherche->text(), true); });
    auto *trouver = new QShortcut(QKeySequence::Find, this);
    connect(trouver, &QShortcut::activated, this, [this] { recherche->setFocus(); recherche->selectAll(); });

    auto *haut = new QHBoxLayout;
    haut->addWidget(recherche, 1);
    haut->addWidget(etat);
    auto *droite = new QWidget;
    auto *colonne = new QVBoxLayout(droite);
    colonne->setContentsMargins(0, 0, 0, 0);
    colonne->addLayout(haut);
    colonne->addWidget(lecteur, 1);
    auto *partage = new QSplitter;
    partage->addWidget(sommaire);
    partage->addWidget(droite);
    partage->setStretchFactor(1, 1);
    partage->setSizes({280, 820});
    setCentralWidget(partage);
    resize(1100, 760);
}

void Aide::aller_au_bloc(int position) {
    QTextCursor c(lecteur->document());
    c.setPosition(position);
    lecteur->setTextCursor(c);
    // le titre en haut de la vue, pas seulement quelque part à l'écran
    lecteur->verticalScrollBar()->setValue(lecteur->verticalScrollBar()->value() + lecteur->cursorRect().top());
}

bool Aide::aller_au_titre(const QString &debut) {
    for (QTextBlock b = lecteur->document()->begin(); b.isValid(); b = b.next())
        if (b.blockFormat().headingLevel() > 0 && b.text().startsWith(debut)) {
            aller_au_bloc(b.position());
            return true;
        }
    return false;
}

bool Aide::chercher(const QString &texte, bool arriere) {
    if (texte.isEmpty()) return false;
    const QTextDocument::FindFlags options = arriere ? QTextDocument::FindBackward : QTextDocument::FindFlags();
    bool trouve = lecteur->find(texte, options);
    if (!trouve) {   // on reprend de l'autre bout
        QTextCursor c(lecteur->document());
        c.movePosition(arriere ? QTextCursor::End : QTextCursor::Start);
        lecteur->setTextCursor(c);
        trouve = lecteur->find(texte, options);
    }
    etat->setText(trouve ? QString() : QString("Introuvable."));
    return trouve;
}

// ------------------------------------------------------------------------------------------------
// Aide en contexte
// ------------------------------------------------------------------------------------------------

// Mot → section de la grammaire. Les mots s'écrivent en minuscules ; la forme compacte perd son souligné.
// La table suit les titres de docs/grammaire.md : un essai de l'atelier vérifie que chaque section existe.
static const QHash<QString, QString> &table_des_mots() {
    static const QHash<QString, QString> t = {
        {"remarque", "1.7"},
        {"vaut", "2.1"}, {"devient", "2.1"},
        {"afficher", "4. "}, {"puis", "4. "}, {"sur", "4.2"}, {"gauche", "4.2"}, {"droite", "4.2"}, {"effacer", "4.3"},
        {"suivi", "4.4"},
        {"est", "5.1"}, {"égal", "5.1"}, {"égale", "5.1"}, {"différent", "5.1"}, {"différente", "5.1"},
        {"inférieur", "5.1"}, {"inférieure", "5.1"}, {"supérieur", "5.1"}, {"supérieure", "5.1"},
        {"positif", "5.1"}, {"positive", "5.1"}, {"négatif", "5.1"}, {"négative", "5.1"}, {"nul", "5.1"}, {"nulle", "5.1"},
        {"et", "5.3"}, {"ou", "5.3"}, {"si", "5.4"}, {"sinon", "5.4"}, {"alors", "5.4"}, {"vrai", "5.5"}, {"faux", "5.5"},
        {"calcul", "9.1"}, {"rendre", "9.1"}, {"pour", "9.2"}, {"action", "9.2"},
        {"tant", "10.1"}, {"répéter", "10.2"}, {"fois", "10.2"}, {"chaque", "10.3"}, {"pas", "10.3"},
        {"sortir", "10.4"}, {"passer", "10.4"}, {"selon", "10.5"}, {"cas", "10.5"}, {"autrement", "10.5"},
        {"classe", "13.1"}, {"nouveau", "13.2"}, {"nouvel", "13.2"}, {"nouvelle", "13.2"}, {"chose", "13.7"},
        {"aptitude", "13.7"}, {"adopte", "13.7"}, {"aujourd", "14.3"},
        {"fichier", "15.2"}, {"enregistrer", "15.2"}, {"dans", "15.2"},
        {"conservé", "16.1"}, {"conservée", "16.1"}, {"conservés", "16.1"}, {"conservées", "16.1"}, {"unique", "16.1"},
        {"conserver", "16.3"}, {"supprimer", "16.3"}, {"dont", "16.4"}, {"décroissant", "16.4"}, {"nombre_de", "16.4"},
        {"départ", "16.7"}, {"facultatif", "16.9"}, {"facultative", "16.9"}, {"absent", "16.9"}, {"absente", "16.9"},
        {"présent", "16.9"}, {"présente", "16.9"}, {"supprimé", "16.12"}, {"supprimée", "16.12"}, {"rétablir", "16.12"},
        {"définitivement", "16.12"}, {"disparaît", "16.12"}, {"disparaît_avec", "16.12"},
        {"plusieurs", "16.13"}, {"gagnent", "16.13"}, {"perdent", "16.13"}, {"gagne", "16.13"}, {"perd", "16.13"},
        {"parmi", "16.13"}, {"réponse", "17. "}, {"essayer", "18. "}, {"refuser", "18.1"}, {"échec", "18. "}, {"saisi", "19. "},
        {"saisie", "19. "}, {"saisir", "19. "}, {"fiche", "20.1"}, {"utiliser", "21.1"},
        {"écran", "22.1"}, {"montre", "22.1"}, {"bouton", "22.1"}, {"liste", "22.1"}, {"zone", "22.1"}, {"côte", "22.1"},
        {"quand", "22.2"}, {"clique", "22.2"}, {"choisit", "22.2"}, {"change", "22.2"}, {"choisi", "22.2"},
        {"ouvrir", "22.3"}, {"fermer", "22.3"},
        {"écrans", "22.5"}, {"couleur", "22.5"}, {"logo", "22.5"},
    };
    return t;
}

QStringList sections_de_l_aide_en_contexte() {
    QStringList r;
    for (const QString &v : table_des_mots()) if (!r.contains(v)) r << v;
    r << "4.1" << "10.1" << "10.3" << "16.13" << "22.3" << "22.5";   // celles que les mots voisins choisissent
    return r;
}

QString section_au_curseur(const QString &ligne, int colonne) {
    // Les mots de la ligne, avec leur position ; un mot : lettres, chiffres, souligné (forme compacte).
    struct Mot { QString texte; int debut, fin; };
    QVector<Mot> mots;
    for (int i = 0; i < ligne.size();) {
        if (!(ligne.at(i).isLetterOrNumber() || ligne.at(i) == '_')) { i++; continue; }
        int j = i;
        while (j < ligne.size() && (ligne.at(j).isLetterOrNumber() || ligne.at(j) == '_' || ligne.at(j).isMark())) j++;
        QString m = ligne.mid(i, j - i).toLower();
        while (m.startsWith('_')) m.remove(0, 1);   // « _selon » → « selon »
        mots.push_back({m, i, j});
        i = j;
    }
    int k = -1;
    for (int q = 0; q < mots.size(); q++)
        if (colonne >= mots[q].debut && colonne <= mots[q].fin) { k = q; break; }
    if (k < 0) return QString();
    const QString m = mots[k].texte;
    int p = k - 1;   // le mot d'avant, sans l'élision (« Ouvrir l'écran » : « ouvrir », pas « l »)
    while (p >= 0 && mots[p].texte.size() == 1) p--;
    const QString avant = p >= 0 ? mots[p].texte : QString(), apres = k + 1 < mots.size() ? mots[k + 1].texte : QString();
    // Les mots voisins départagent.
    if (m == "tant_que" || (m == "que" && avant == "tant")) return "10.1";
    if (m == "pour_chaque" || (m == "pour" && apres == "chaque")) return "10.3";
    if (m == "sinon_si") return "5.4";
    if (m == "les" || m == "nombres" || m == "affichent" || m == "style") {
        const QString suite = m == "les" ? apres : m;
        if (suite == "nombres" || suite == "affichent" || suite == "style") return "4.1";
        if (suite == "écrans") return "22.5";
        return m == "les" ? "16.13" : QString();
    }
    if (m == "écran" && (avant == "ouvrir" || avant == "fermer")) return "22.3";
    if (m == "écran" && avant == "effacer") return "4.3";
    return table_des_mots().value(m);
}
