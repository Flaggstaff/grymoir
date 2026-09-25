// GrymoiR : l'atelier, fenêtre principale.
#include "fenetre.h"
#include "editeur.h"
#include "aide.h"
#include "schema.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QDirIterator>
#include <QFileSystemModel>
#include <QHash>
#include <QInputDialog>
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
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QTreeView>

// Les fichiers du projet (grammaire, § 21) : un programme s'écrit en gras, un fichier de déclarations
// normalement ; le survol dit lequel. La sorte se calcule par l'analyse, et se garde tant que le fichier
// ne change pas sur le disque.
class ModeleProjet : public QFileSystemModel {
public:
    using QFileSystemModel::QFileSystemModel;
    enum Sorte { Inconnue, Programme, Declarations };

    Sorte sorte(const QString &chemin) const {
        const QFileInfo i(chemin);
        const auto c = cache.constFind(chemin);
        if (c != cache.constEnd() && c->first == i.lastModified()) return c->second;
        Sorte s = Inconnue;
        QFile f(chemin);
        if (f.open(QIODevice::ReadOnly)) {
            bool declarations = false;
            const Diagnostic_atelier d = analyser_source(QString::fromUtf8(f.readAll()),
                                                         chemin.endsWith(".grymc", Qt::CaseInsensitive), chemin, &declarations);
            s = !d.message.isEmpty() ? Inconnue : declarations ? Declarations : Programme;
        }
        cache.insert(chemin, qMakePair(i.lastModified(), s));
        return s;
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if ((role == Qt::FontRole || role == Qt::ToolTipRole) && index.isValid() && !isDir(index)) {
            const Sorte s = sorte(filePath(index));
            if (role == Qt::ToolTipRole)
                return s == Programme ? QString("Programme") : s == Declarations ? QString("Fichier de déclarations")
                                                                                 : QString("Contient une erreur");
            QFont f;
            f.setBold(s == Programme);
            f.setItalic(s == Inconnue);
            return f;
        }
        return QFileSystemModel::data(index, role);
    }

private:
    mutable QHash<QString, QPair<QDateTime, Sorte>> cache;
};

Fenetre::Fenetre() {
    editeur = new Editeur;
    modele = new ModeleProjet(this);
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
        const QString autre = i->data(Qt::UserRole + 2).toString();   // l'erreur est dans un autre fichier
        if (!autre.isEmpty() && QFileInfo(autre).absoluteFilePath() != QFileInfo(fichier).absoluteFilePath()) {
            ouvrir_fichier(QFileInfo(autre).absoluteFilePath());
            if (QFileInfo(autre).absoluteFilePath() != QFileInfo(fichier).absoluteFilePath()) return;
        }
        if (ligne > 0) editeur->aller_a(ligne, i->data(Qt::UserRole + 1).toInt());
    });
    connect(erreurs, &QListWidget::itemClicked, erreurs, &QListWidget::itemActivated);

    auto *droite = new QSplitter(Qt::Vertical);
    droite->addWidget(editeur);
    droite->addWidget(erreurs);
    droite->setStretchFactor(0, 1);
    auto *centre = new QSplitter;
    centre->addWidget(arbre);
    // Deux onglets : le code, et le schéma des données (A2-a), qui se relit chaque fois qu'on l'ouvre
    schema = new VueSchema;
    schema_etat = new QLabel;
    schema_etat->setWordWrap(true);
    auto *donnees = new QWidget;
    auto *pile = new QVBoxLayout(donnees);
    pile->setContentsMargins(0, 0, 0, 0);
    pile->addWidget(schema, 1);
    pile->addWidget(schema_etat);
    onglets = new QTabWidget;
    onglets->addTab(droite, "Code");
    onglets->addTab(donnees, "Données");
    connect(onglets, &QTabWidget::currentChanged, this, [this](int k) { if (k == 1) rafraichir_schema(); });
    auto *garder = new QTimer(this);   // la disposition s'écrit quand on cesse de déplacer les boîtes
    garder->setSingleShot(true);
    garder->setInterval(600);
    connect(schema, &VueSchema::deplacee, garder, qOverload<>(&QTimer::start));
    connect(garder, &QTimer::timeout, this, [this] {
        projet_fichier.positions = schema->positions();
        if (!projet_fichier.ecrire()) statusBar()->showMessage("projet.grymatelier n'a pas pu être écrit.", 5000);
    });
    connect(schema, &VueSchema::ouvrir, this, [this](const QString &f, int ligne) {
        onglets->setCurrentIndex(0);
        ouvrir_fichier(f);
        if (QFileInfo(fichier).absoluteFilePath() == QFileInfo(f).absoluteFilePath()) editeur->aller_a(ligne, 1);
    });
    centre->addWidget(onglets);
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
    programme->addSeparator();
    programme->addAction("Choisir le programme principal…", this, &Fenetre::choisir_principal);
    QMenu *menu_aide = menuBar()->addMenu("Aide");
    QAction *langage = menu_aide->addAction("Le langage GrymoiR", this, [this] {
        if (!aide) aide = new Aide(this);
        aide->setWindowFlag(Qt::Window);
        aide->show();
        aide->raise();
        aide->activateWindow();
    });
    langage->setShortcut(QKeySequence::HelpContents);   // F1 ; Cmd+? sous macOS
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
    QSettings reglages;
    reglages.setValue("dernier projet", dossier);
    projet_fichier.lire(dossier);
    // le programme principal quittait autrefois les réglages de la machine : il déménage dans le projet
    const QString ancien = reglages.value("programme principal/" + QFileInfo(dossier).absoluteFilePath()).toString();
    if (projet_fichier.programme_principal.isEmpty() && !ancien.isEmpty() && QFileInfo::exists(ancien)) {
        projet_fichier.programme_principal = QDir(dossier).relativeFilePath(ancien);
        projet_fichier.ecrire();
    }
    if (onglets->currentIndex() == 1) rafraichir_schema();
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
    editeur->charger(texte, chemin.endsWith(".grymc", Qt::CaseInsensitive), chemin);
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
    auto ajouter = [this](const QString &texte, int ligne, int colonne, bool rouge, const QString &autre = QString()) {
        auto *i = new QListWidgetItem(texte, erreurs);
        i->setData(Qt::UserRole, ligne);
        i->setData(Qt::UserRole + 1, colonne);
        i->setData(Qt::UserRole + 2, autre);
        if (rouge) i->setForeground(Qt::red);
    };
    const auto &d = editeur->diagnostic();
    if (d.message.isEmpty()) ajouter("Aucune erreur.", 0, 0, false);
    else if (d.ligne > 0) ajouter(QString("Ligne %1, colonne %2 : %3").arg(d.ligne).arg(d.colonne).arg(d.message), d.ligne, d.colonne, true);
    else ajouter(d.message, 0, 0, true);
    if (!erreur_execution.isEmpty()) {
        const bool ici = erreur_fichier.isEmpty()
                      || QFileInfo(erreur_fichier).absoluteFilePath() == QFileInfo(fichier).absoluteFilePath();
        const QString ou = ici ? QString() : QString("« %1 », ").arg(QDir(projet).relativeFilePath(erreur_fichier));
        ajouter(erreur_ligne > 0 ? QString("Exécution, %1ligne %2, colonne %3 : %4").arg(ou).arg(erreur_ligne).arg(erreur_colonne).arg(erreur_execution)
                                 : QString("Exécution : %1").arg(erreur_execution),
                erreur_ligne, erreur_colonne, true, ici ? QString() : erreur_fichier);
    }
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
    const QString cible = programme_a_lancer();   // un fichier de déclarations lance le programme principal (§ 21)
    if (cible.isEmpty()) return;
    erreur_execution.clear();
    erreur_fichier.clear();
    erreur_ligne = erreur_colonne = 0;
    montrer_diagnostic();
    execution = new QProcess(this);
    execution->setProgram(QCoreApplication::applicationFilePath());
    execution->setArguments({"--lancer", cible});
    execution->setWorkingDirectory(QFileInfo(cible).absolutePath());
    execution->setProcessChannelMode(QProcess::SeparateChannels);
    connect(execution, &QProcess::finished, this, &Fenetre::execution_finie);
    connect(execution, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) execution_finie();
    });
    execution->start();
    action_lancer->setEnabled(false);
    action_arreter->setEnabled(true);
    statusBar()->showMessage(QString("« %1 » tourne.").arg(QFileInfo(cible).fileName()));
}

QString Fenetre::programme_a_lancer() {
    if (!editeur->declarations()) return QFileInfo(fichier).absoluteFilePath();
    auto retenu = [this] {
        return projet_fichier.programme_principal.isEmpty() ? QString()
                                                            : QDir(projet).absoluteFilePath(projet_fichier.programme_principal);
    };
    if (!retenu().isEmpty() && QFileInfo::exists(retenu()) && modele->sorte(retenu()) == ModeleProjet::Programme) return retenu();
    choisir_principal();
    return QFileInfo::exists(retenu()) ? retenu() : QString();
}

// Les programmes du projet ; un seul est retenu sans question, plusieurs font demander, une fois.
void Fenetre::choisir_principal() {
    if (projet.isEmpty()) return;
    QStringList programmes;
    QDirIterator it(projet, {"*.grym", "*.grymc"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString c = QFileInfo(it.next()).absoluteFilePath();
        if (modele->sorte(c) == ModeleProjet::Programme) programmes << QDir(projet).relativeFilePath(c);
    }
    programmes.sort(Qt::CaseInsensitive);
    if (programmes.isEmpty()) {
        statusBar()->showMessage("Aucun programme dans le projet : ses fichiers ne contiennent que des déclarations.", 6000);
        return;
    }
    QString choix = programmes.first();
    if (programmes.size() > 1) {
        bool ok = false;
        choix = QInputDialog::getItem(this, "Programme principal", "Programme à lancer :", programmes, 0, false, &ok);
        if (!ok) return;
    }
    projet_fichier.programme_principal = choix;   // dans projet.grymatelier : une information du projet
    if (!projet_fichier.ecrire()) statusBar()->showMessage("projet.grymatelier n'a pas pu être écrit.", 5000);
    statusBar()->showMessage(QString("Programme principal : « %1 ».").arg(choix), 4000);
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
        erreur_fichier = m.captured(1);
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

void Fenetre::rafraichir_schema() {
    if (projet.isEmpty()) return;
    QStringList problemes;
    const QVector<EntiteSchema> entites = lire_schema(projet, &problemes);
    schema->montrer(entites, projet_fichier.positions);
    // une entité nouvelle vient de recevoir sa place : on la garde, pour qu'elle ne bouge plus
    const auto positions = schema->positions();
    if (positions != projet_fichier.positions) {
        projet_fichier.positions = positions;
        projet_fichier.ecrire();
    }
    QString t = entites.isEmpty() ? QString("Aucune entité conservée dans ce projet.")
                                  : QString("%1 entité%2. Glissez les boîtes pour les ranger ; double-clic : la déclaration.")
                                        .arg(entites.size()).arg(entites.size() > 1 ? "s" : "");
    if (!problemes.isEmpty())
        t += "\nNon lus, car ils contiennent une erreur : " + problemes.join(", ") + ".";
    schema_etat->setText(t);
}
