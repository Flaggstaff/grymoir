// GrymoiR : l'atelier, aide : la grammaire du langage.
#include "aide.h"

#include <QFile>
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

Aide::Aide(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Le langage GrymoiR");
    lecteur = new QTextBrowser;
    lecteur->setOpenExternalLinks(true);
    QFile f(":/documentation/grammaire.md");   // embarquée par CMake (qt_add_resources)
    if (f.open(QIODevice::ReadOnly)) lecteur->setMarkdown(QString::fromUtf8(f.readAll()));
    else lecteur->setPlainText("La grammaire n'a pas été embarquée dans cet exécutable.");

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
    recherche->setPlaceholderText("Chercher dans la grammaire (Entrée : suivant, Maj+Entrée : précédent)");
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
