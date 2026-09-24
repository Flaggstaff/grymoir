// GrymoiR : l'atelier, fenêtre principale.
#include "fenetre.h"
#include "editeur.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeView>

Fenetre::Fenetre() {
    editeur = new Editeur;
    modele = new QFileSystemModel(this);
    modele->setNameFilters({"*.grym", "*.grymc"});
    modele->setNameFilterDisables(false);   // les autres fichiers disparaissent, au lieu d'être grisés
    arbre = new QTreeView;
    arbre->setModel(modele);
    for (int k = 1; k < 4; k++) arbre->hideColumn(k);
    arbre->header()->hide();
    connect(arbre, &QTreeView::activated, this, [this](const QModelIndex &i) {
        if (!modele->isDir(i)) ouvrir_fichier(modele->filePath(i));
    });
    connect(arbre, &QTreeView::clicked, this, [this](const QModelIndex &i) {
        if (!modele->isDir(i)) ouvrir_fichier(modele->filePath(i));
    });

    erreurs = new QListWidget;
    erreurs->setMaximumHeight(90);
    connect(erreurs, &QListWidget::itemActivated, this, [this] {
        const auto &d = editeur->diagnostic();
        if (d.ligne > 0) editeur->aller_a(d.ligne, d.colonne);
    });
    connect(erreurs, &QListWidget::itemClicked, erreurs, &QListWidget::itemActivated);

    auto *droite = new QSplitter(Qt::Vertical);
    droite->addWidget(editeur);
    droite->addWidget(erreurs);
    droite->setStretchFactor(0, 1);
    auto *centre = new QSplitter;
    centre->addWidget(arbre);
    centre->addWidget(droite);
    centre->setStretchFactor(1, 1);
    centre->setSizes({220, 900});
    setCentralWidget(centre);

    QMenu *menu = menuBar()->addMenu("Fichier");
    QAction *a = menu->addAction("Ouvrir un projet…", this, &Fenetre::choisir_projet);
    a->setShortcut(QKeySequence::Open);
    a = menu->addAction("Enregistrer", this, [this] { enregistrer(); });
    a->setShortcut(QKeySequence::Save);
    menu->addSeparator();
    a = menu->addAction("Quitter", this, &QWidget::close);
    a->setShortcut(QKeySequence::Quit);
    a->setMenuRole(QAction::QuitRole);

    connect(editeur, &Editeur::diagnostic_change, this, &Fenetre::montrer_diagnostic);
    connect(editeur->document(), &QTextDocument::modificationChanged, this, &Fenetre::mettre_a_jour_titre);
    editeur->setEnabled(false);
    resize(1200, 780);
    mettre_a_jour_titre();
}

void Fenetre::ouvrir(const QString &chemin) {
    const QFileInfo i(chemin);
    if (i.isDir()) {
        ouvrir_projet(i.absoluteFilePath());
    } else if (i.isFile()) {
        ouvrir_projet(i.absolutePath());
        ouvrir_fichier(i.absoluteFilePath());
    } else {
        statusBar()->showMessage(QString("« %1 » introuvable.").arg(chemin), 5000);
    }
}

void Fenetre::choisir_projet() {
    QSettings reglages;
    const QString d = QFileDialog::getExistingDirectory(this, "Ouvrir un projet",
                                                        reglages.value("dernier projet").toString());
    if (!d.isEmpty()) ouvrir_projet(d);
}

void Fenetre::ouvrir_projet(const QString &dossier) {
    if (!quitter_fichier()) return;
    projet = dossier;
    fichier.clear();
    editeur->charger(QString(), false);
    editeur->setEnabled(false);
    erreurs->clear();
    arbre->setRootIndex(modele->setRootPath(dossier));
    QSettings().setValue("dernier projet", dossier);
    mettre_a_jour_titre();
}

void Fenetre::ouvrir_fichier(const QString &chemin) {
    if (chemin == fichier || !quitter_fichier()) return;
    QFile f(chemin);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Atelier", QString("« %1 » ne s'ouvre pas.").arg(chemin));
        return;
    }
    const QByteArray octets = f.readAll();
    QString texte = QString::fromUtf8(octets);
    if (texte.toUtf8() != octets) {   // le cœur refuserait aussi : les sources sont en UTF-8 (charte, art. 3)
        QMessageBox::warning(this, "Atelier", QString("« %1 » n'est pas en UTF-8.").arg(QFileInfo(chemin).fileName()));
        return;
    }
    fichier = chemin;
    editeur->setEnabled(true);
    editeur->charger(texte, chemin.endsWith(".grymc", Qt::CaseInsensitive));
    arbre->setCurrentIndex(modele->index(chemin));
    mettre_a_jour_titre();
}

bool Fenetre::enregistrer() {
    if (fichier.isEmpty()) return true;
    QSaveFile f(fichier);   // écrit à côté, puis remplace : une panne ne laisse jamais un fichier à moitié écrit
    if (!f.open(QIODevice::WriteOnly) || f.write(editeur->toPlainText().toUtf8()) < 0 || !f.commit()) {
        QMessageBox::warning(this, "Atelier", QString("« %1 » n'a pas pu être enregistré.").arg(fichier));
        return false;
    }
    editeur->document()->setModified(false);
    statusBar()->showMessage("Enregistré.", 2000);
    return true;
}

bool Fenetre::quitter_fichier() {
    if (fichier.isEmpty() || !editeur->document()->isModified()) return true;
    const auto r = QMessageBox::question(this, "Atelier",
                                         QString("« %1 » a changé. L'enregistrer ?").arg(QFileInfo(fichier).fileName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save) return enregistrer();
    return r == QMessageBox::Discard;
}

void Fenetre::closeEvent(QCloseEvent *e) {
    if (quitter_fichier()) e->accept();
    else e->ignore();
}

void Fenetre::mettre_a_jour_titre() {
    QString t = "GrymoiR";
    if (!projet.isEmpty()) t = QFileInfo(projet).fileName() + " – " + t;
    if (!fichier.isEmpty()) t = QFileInfo(fichier).fileName() + (editeur->document()->isModified() ? " •" : "") + " – " + t;
    setWindowTitle(t);
}

void Fenetre::montrer_diagnostic() {
    erreurs->clear();
    if (fichier.isEmpty()) return;
    const auto &d = editeur->diagnostic();
    if (d.message.isEmpty()) {
        erreurs->addItem("Aucune erreur.");
    } else if (d.ligne > 0) {
        erreurs->addItem(QString("Ligne %1, colonne %2 : %3").arg(d.ligne).arg(d.colonne).arg(d.message));
        erreurs->item(0)->setForeground(Qt::red);
    } else {
        erreurs->addItem(d.message);
        erreurs->item(0)->setForeground(Qt::red);
    }
}
