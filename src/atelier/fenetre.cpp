// GrymoiR : l'atelier, fenêtre principale.
#include "fenetre.h"
#include "editeur.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QToolBar>
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
    connect(erreurs, &QListWidget::itemActivated, this, [this](QListWidgetItem *i) {
        const int ligne = i->data(Qt::UserRole).toInt();
        if (ligne > 0) editeur->aller_a(ligne, i->data(Qt::UserRole + 1).toInt());
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

    QMenu *programme = menuBar()->addMenu("Programme");
    action_lancer = programme->addAction("Lancer", this, &Fenetre::lancer);
    action_lancer->setShortcut(QKeySequence("Ctrl+R"));
    action_arreter = programme->addAction("Arrêter", this, &Fenetre::arreter);
    action_arreter->setShortcut(QKeySequence("Ctrl+."));
    action_arreter->setEnabled(false);
    QToolBar *barre = addToolBar("Programme");
    barre->setMovable(false);
    barre->addAction(action_lancer);
    barre->addAction(action_arreter);

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
    if (!quitter_fichier()) {
        e->ignore();
        return;
    }
    if (execution) {   // l'atelier ne laisse pas un programme orphelin : il l'arrête, qui annule
        disconnect(execution, nullptr, this, nullptr);
        execution->terminate();
        if (!execution->waitForFinished(5000)) execution->kill();
    }
    e->accept();
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
    auto ajouter = [this](const QString &texte, int ligne, int colonne, bool rouge) {
        auto *i = new QListWidgetItem(texte, erreurs);
        i->setData(Qt::UserRole, ligne);
        i->setData(Qt::UserRole + 1, colonne);
        if (rouge) i->setForeground(Qt::red);
    };
    const auto &d = editeur->diagnostic();
    if (d.message.isEmpty()) ajouter("Aucune erreur.", 0, 0, false);
    else if (d.ligne > 0) ajouter(QString("Ligne %1, colonne %2 : %3").arg(d.ligne).arg(d.colonne).arg(d.message), d.ligne, d.colonne, true);
    else ajouter(d.message, 0, 0, true);
    if (!erreur_execution.isEmpty())
        ajouter(erreur_ligne > 0 ? QString("Exécution, ligne %1, colonne %2 : %3").arg(erreur_ligne).arg(erreur_colonne).arg(erreur_execution)
                                 : QString("Exécution : %1").arg(erreur_execution),
                erreur_ligne, erreur_colonne, true);
}

void Fenetre::lancer() {
    if (fichier.isEmpty() || execution) return;
    if (editeur->document()->isModified() && !enregistrer()) return;   // on lance ce qui est sur le disque
    const auto &d = editeur->diagnostic();
    if (!d.message.isEmpty()) {   // inutile de lancer : l'erreur est déjà connue
        if (d.ligne > 0) editeur->aller_a(d.ligne, d.colonne);
        statusBar()->showMessage("Corrigez d'abord l'erreur signalée.", 4000);
        return;
    }
    erreur_execution.clear();
    erreur_ligne = erreur_colonne = 0;
    montrer_diagnostic();
    execution = new QProcess(this);
    execution->setProgram(QCoreApplication::applicationFilePath());
    execution->setArguments({"--lancer", fichier});
    execution->setWorkingDirectory(QFileInfo(fichier).absolutePath());
    execution->setProcessChannelMode(QProcess::SeparateChannels);
    connect(execution, &QProcess::finished, this, &Fenetre::execution_finie);
    connect(execution, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) execution_finie();
    });
    execution->start();
    action_lancer->setEnabled(false);
    action_arreter->setEnabled(true);
    statusBar()->showMessage(QString("« %1 » tourne.").arg(QFileInfo(fichier).fileName()));
}

void Fenetre::arreter() {
    if (!execution) return;
    // Demande polie : la machine s'arrête et annule (SIGTERM ; sous Windows, la fenêtre reçoit la fermeture).
    execution->terminate();
    QProcess *p = execution;
    QTimer::singleShot(5000, p, [p] {   // un programme qui ne répond plus du tout : SQLite annulera à la réouverture
        if (p->state() != QProcess::NotRunning) p->kill();
    });
}

void Fenetre::execution_finie() {
    if (!execution) return;
    // La ligne d'erreur de la machine, au format de grym lancer : « fichier:ligne:colonne : erreur : message ».
    const QString sortie = QString::fromUtf8(execution->readAllStandardError());
    static const QRegularExpression forme("^(.*):(\\d+):(\\d+) : erreur : (.*)$", QRegularExpression::MultilineOption);
    static const QRegularExpression sans_position("^.* : erreur : (.*)$", QRegularExpression::MultilineOption);
    const auto m = forme.match(sortie);
    if (m.hasMatch()) {
        erreur_ligne = m.captured(2).toInt();
        erreur_colonne = m.captured(3).toInt();
        erreur_execution = m.captured(4);
    } else if (const auto m2 = sans_position.match(sortie); m2.hasMatch()) {
        erreur_execution = m2.captured(1);
    } else if (execution->error() == QProcess::FailedToStart) {
        erreur_execution = "le programme n'a pas pu démarrer.";
    }
    execution->deleteLater();
    execution = nullptr;
    action_lancer->setEnabled(true);
    action_arreter->setEnabled(false);
    statusBar()->showMessage(erreur_execution.isEmpty() ? "Exécution terminée." : "Exécution terminée sur une erreur.", 5000);
    montrer_diagnostic();
}
