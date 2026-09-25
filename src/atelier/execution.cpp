// GrymoiR : l'atelier, exécution d'un programme dans sa propre fenêtre.
#include "execution.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "analyseur.h"
#include "compilateur.h"
#include "vm.h"
}

extern volatile std::sig_atomic_t grym_fermeture_demandee;   // principal.cpp ; tests/test_atelier.cpp

static bool est_fichier(const Champ *c) {
    return c->type && (std::strcmp(c->type, "fichier") == 0 || std::strcmp(c->type, "image") == 0);
}

static bool est_oui_non(const Champ *c) { return c->type && std::strcmp(c->type, "vrai ou faux") == 0; }

// --------------------------------------------------------------------------------------------
// L'interface de la machine (src/interface.h), côté fil de la machine
// --------------------------------------------------------------------------------------------

static int iface_disponible(void *) { return 1; }

static Issue iface_formulaire(void *contexte, Chaine *sortie, Champ *champs, size_t n, size_t *arret,
                              Validation valider, void *vcontexte) {
    return static_cast<Travail *>(contexte)->formulaire(sortie, champs, n, arret, valider, vcontexte);
}

static void iface_effacer(void *contexte, Chaine *sortie) {
    auto *t = static_cast<Travail *>(contexte);
    t->vider_sortie(sortie);
    emit t->effacer();
}

static void iface_image(void *contexte, Chaine *sortie, const unsigned char *octets, size_t taille, const char *,
                        const char *description) {
    auto *t = static_cast<Travail *>(contexte);
    t->vider_sortie(sortie);
    emit t->image(QByteArray(reinterpret_cast<const char *>(octets), (qsizetype)taille),
                  QString::fromUtf8(description ? description : ""));
}

static void iface_fiche(void *contexte, Chaine *sortie, const char *titre, const LigneFiche *lignes, size_t n) {
    auto *t = static_cast<Travail *>(contexte);
    t->vider_sortie(sortie);
    // Les images de la fiche voyagent à part : le HTML les désigne par « image:K », la fenêtre les range.
    QList<QByteArray> images;
    QString h = "<p><b>" + QString::fromUtf8(titre).toHtmlEscaped() + "</b></p><table cellspacing=\"0\" cellpadding=\"3\">";
    for (size_t k = 0; k < n; k++) {
        h += "<tr><td style=\"color:#666\">" + QString::fromUtf8(lignes[k].libelle).toHtmlEscaped() + "</td><td>";
        if (lignes[k].format && lignes[k].octets) {
            h += QString("<img src=\"image:%1\"><br>").arg(images.size());
            images.append(QByteArray(reinterpret_cast<const char *>(lignes[k].octets), (qsizetype)lignes[k].taille));
        }
        h += QString::fromUtf8(lignes[k].texte ? lignes[k].texte : "").toHtmlEscaped() + "</td></tr>";
    }
    h += "</table>";
    emit t->fiche(h, images);
}

Travail::Travail(const QString &c) : chemin(c) {}

void Travail::vider_sortie(Chaine *sortie) {
    if (sortie->n) {
        emit texte(QString::fromUtf8(sortie->d, (qsizetype)sortie->n));
        sortie->n = 0;
        sortie->d[0] = '\0';
    }
}

Issue Travail::formulaire(Chaine *sortie, Champ *c, size_t nb, size_t *arret, Validation valider, void *vcontexte) {
    vider_sortie(sortie);
    champs = c;
    n = nb;
    refus = QVector<QString>((int)nb);
    acceptes = QVector<bool>((int)nb, false);
    nouvelle = true;
    for (;;) {
        emit question();
        reponse.acquire();   // la fenêtre remplit `issue` et `saisies`, puis libère
        nouvelle = false;
        *arret = 0;
        for (size_t k = 0; k < nb; k++)
            if (!acceptes[(int)k]) { *arret = k; break; }
        if (issue == 1) return ISSUE_ANNULE;
        if (issue == 2) return ISSUE_INTERROMPU;
        bool tout = true;
        for (size_t k = 0; k < nb; k++) {
            if (acceptes[(int)k]) continue;
            Champ *ch = &c[k];
            const Saisie &s = saisies[(int)k];
            refus[(int)k].clear();
            ch->vider = ch->videable && s.vider;
            if (est_fichier(ch) && s.fichier && !ch->vider) {   // le fichier choisi passe à la machine, qui le garde
                ch->fichier_recu = 1;
                ch->taille = (size_t)s.octets.size();
                ch->octets = static_cast<unsigned char *>(std::malloc(ch->taille ? ch->taille : 1));
                if (!ch->octets) std::abort();
                std::memcpy(ch->octets, s.octets.constData(), ch->taille);
                ch->nom_fichier = grym_dupliquer(s.nom_fichier.toUtf8().constData());
            }
            ch->ligne = grym_dupliquer(ch->vider || est_fichier(ch) ? "" : s.ligne.toUtf8().constData());
            char *message = nullptr;
            const int res = valider(vcontexte, k, &message);
            if (res < 0) {
                std::free(message);
                *arret = k;
                return ISSUE_ARRET;
            }
            if (res > 0) {
                refus[(int)k] = QString::fromUtf8(message ? message : "Réponse refusée.");
                tout = false;
            } else {
                acceptes[(int)k] = true;
            }
            std::free(message);
        }
        if (tout) {
            QString echo;
            for (size_t k = 0; k < nb; k++) {
                const Champ *ch = &c[k];
                const Saisie &s = saisies[(int)k];
                const QString valeur = ch->vider ? QString("(vidé)")
                                     : est_fichier(ch) ? (s.fichier ? s.nom_fichier : QString("(inchangé)"))
                                     : s.ligne;
                echo += QString::fromUtf8(ch->libelle) + (ch->question ? " " : " : ") + valeur + "\n";
            }
            emit reponses(echo);
            return ISSUE_REPONDU;
        }
    }
}

// Dossier du programme et base à côté de lui, comme grym lancer (grammaire, § 15.2, § 16.5).
static void situer(Machine *m, const QString &chemin) {
    const QFileInfo i(chemin);
    machine_dossier(m, i.absolutePath().toUtf8().constData());
    const QString base = i.absolutePath() + "/" + i.completeBaseName() + ".grymd";
    machine_base(m, base.toUtf8().constData());
}

void Travail::run() {
    QFile f(chemin);
    if (!f.open(QIODevice::ReadOnly)) {
        emit fin(false, QString("Impossible d'ouvrir « %1 ».").arg(chemin), QString());
        return;
    }
    const QByteArray source = f.readAll();
    Portee *portee = portee_creer();
    const QByteArray chemin_utf8 = chemin.toUtf8();
    portee_fichier(portee, chemin_utf8.constData());   // les fichiers utilisés se cherchent à côté (§ 21)
    Programme p = {};
    Diagnostic d = {};
    const bool compacte = chemin.endsWith(".grymc", Qt::CaseInsensitive);
    const int lu = compacte ? analyser_compact(source.constData(), (size_t)source.size(), portee, &p, &d)
                            : analyser(source.constData(), (size_t)source.size(), portee, 0, &p, &d);
    Module *module = lu ? compiler(&p, &d) : nullptr;
    programme_liberer(&p);
    portee_detruire(portee);
    auto signaler = [&](const Diagnostic &e) {
        // la même ligne que grym lancer : l'atelier la lit pour mener à l'erreur
        const QByteArray ou = e.fichier ? QByteArray(e.fichier) : chemin.toUtf8();   // un fichier utilisé, peut-être
        if (e.ligne) std::fprintf(stderr, "%s:%d:%d : erreur : %s\n", ou.constData(), e.ligne, e.colonne, e.message);
        else std::fprintf(stderr, "%s : erreur : %s\n", ou.constData(), e.message);
        std::fflush(stderr);
        const QString dans = e.fichier ? QString(" dans « %1 »").arg(QFileInfo(QString::fromUtf8(e.fichier)).fileName()) : QString();
        return e.ligne ? QString("Erreur%1, ligne %2 : %3").arg(dans).arg(e.ligne).arg(QString::fromUtf8(e.message))
                       : QString("Erreur%1 : %2").arg(dans, QString::fromUtf8(e.message));
    };
    if (!module) {
        const QString message = signaler(d);
        diagnostic_liberer(&d);
        emit fin(false, message, QString());
        return;
    }
    Machine *m = machine_creer();
    Interface i = {this, iface_disponible, iface_formulaire, iface_effacer, 1, iface_image, iface_fiche};
    machine_interface(m, &i);
    situer(m, chemin);
    Chaine sortie = {};
    grym_interruption = 0;
    const int ok = machine_executer(m, module, &sortie, &d);
    if (sortie.d) vider_sortie(&sortie);
    std::free(sortie.d);
    QString message, annulation;
    if (!ok) {
        message = signaler(d);
        diagnostic_liberer(&d);
        char *a = machine_annulation(m, 0);   // ce qui s'est affiché reste, rien de ce qui a été écrit (§ 3.3)
        if (a) annulation = QString::fromUtf8(a);
        std::free(a);
    }
    machine_detruire(m);
    module_detruire(module);
    emit fin(ok, message, annulation);
}

// --------------------------------------------------------------------------------------------
// La fenêtre, côté fil principal
// --------------------------------------------------------------------------------------------

Execution::Execution(const QString &chemin) : travail(chemin) {
    setWindowTitle(QFileInfo(chemin).fileName() + " – exécution");
    fil = new QTextBrowser;
    fil->setOpenLinks(false);
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(qMax(f.pointSize(), 13));
    fil->setFont(f);

    zone = new QWidget;
    auto *defilement = new QScrollArea;
    defilement->setWidget(zone);
    defilement->setWidgetResizable(true);

    bouton_envoyer = new QPushButton("Envoyer");
    bouton_annuler = new QPushButton("Annuler");
    bouton_arreter = new QPushButton("Arrêter");
    etat = new QLabel("En cours…");
    connect(bouton_envoyer, &QPushButton::clicked, this, [this] { envoyer(0); });
    connect(bouton_annuler, &QPushButton::clicked, this, [this] { envoyer(1); });
    connect(bouton_arreter, &QPushButton::clicked, this, &Execution::arreter);
    bouton_envoyer->setEnabled(false);
    bouton_annuler->setEnabled(false);
    bouton_envoyer->setDefault(true);

    auto *boutons = new QHBoxLayout;
    boutons->addWidget(etat, 1);
    boutons->addWidget(bouton_arreter);
    boutons->addWidget(bouton_annuler);
    boutons->addWidget(bouton_envoyer);
    auto *bas = new QWidget;
    auto *colonne = new QVBoxLayout(bas);
    colonne->addWidget(defilement, 1);
    colonne->addLayout(boutons);

    partage = new QSplitter(Qt::Vertical);
    partage->addWidget(fil);
    partage->addWidget(bas);
    partage->setStretchFactor(0, 3);
    partage->setStretchFactor(1, 1);
    setCentralWidget(partage);
    resize(820, 680);

    // Le fil de la machine émet ; ces signaux arrivent ici, dans l'ordre, par la file des événements.
    connect(&travail, &Travail::texte, this, [this](const QString &t) { ecrire(t); });
    connect(&travail, &Travail::effacer, fil, &QTextBrowser::clear);
    connect(&travail, &Travail::image, this, [this](const QByteArray &octets, const QString &description) {
        const QImage img = QImage::fromData(octets);
        if (img.isNull()) {   // format que Qt ne lit pas ici (WebP sans son greffon) : la description
            ecrire(description + "\n");
            return;
        }
        const QUrl nom(QString("image:%1").arg(images++));
        fil->document()->addResource(QTextDocument::ImageResource, nom, img);
        QTextCursor c(fil->document());
        c.movePosition(QTextCursor::End);
        QTextImageFormat format;
        format.setName(nom.toString());
        if (img.width() > 480) format.setWidth(480);
        c.insertImage(format);
        c.insertText("\n");
        fil->setTextCursor(c);
        fil->ensureCursorVisible();
    });
    connect(&travail, &Travail::fiche, this, [this](const QString &html, const QList<QByteArray> &liste) {
        QString h = html;
        for (int k = 0; k < liste.size(); k++) {
            const QImage img = QImage::fromData(liste[k]);
            const QString nom = QString("image:%1").arg(images++);
            fil->document()->addResource(QTextDocument::ImageResource, QUrl(nom),
                                         img.width() > 240 ? img.scaledToWidth(240, Qt::SmoothTransformation) : img);
            h.replace(QString("\"image:%1\"").arg(k), "\"" + nom + "\"");
        }
        QTextCursor c(fil->document());
        c.movePosition(QTextCursor::End);
        c.insertHtml(h);
        c.insertBlock();
        c.setCharFormat(QTextCharFormat());
        fil->setTextCursor(c);
        fil->ensureCursorVisible();
    });
    connect(&travail, &Travail::question, this, &Execution::poser);
    connect(&travail, &Travail::reponses, this, [this](const QString &echo) {
        QTextCursor c(fil->document());
        c.movePosition(QTextCursor::End);
        QTextCharFormat format;
        format.setFont(fil->font());
        format.setForeground(QColor(0x1f, 0x5f, 0xa8));
        c.insertText(echo, format);
        fil->setTextCursor(c);
        fil->ensureCursorVisible();
    });
    connect(&travail, &Travail::fin, this, &Execution::terminer);

    // Un signal d'arrêt venu de l'atelier (grym_interruption) débloque aussi une question en attente.
    auto *veille = new QTimer(this);
    connect(veille, &QTimer::timeout, this, [this] {
        if (grym_fermeture_demandee && !fermer_a_la_fin) {   // l'atelier a demandé l'arrêt : on ferme ensuite
            fermer_a_la_fin = true;
            if (fini) close();
        }
        if (grym_interruption && attente) envoyer(2);
    });
    veille->start(150);
}

void Execution::demarrer() { travail.start(); }

// Détruite pendant l'exécution (fin brutale du processus principal) : on arrête la machine, qui annule,
// et on l'attend ; un fil détruit en marche emporterait tout sans annulation.
Execution::~Execution() {
    if (travail.isRunning()) {
        grym_interruption = 1;
        if (attente) {
            attente = false;
            travail.issue = 2;
            travail.reponse.release();
        }
        travail.wait();
    }
}

void Execution::ecrire(const QString &t, bool erreur) {
    QTextCursor c(fil->document());
    c.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setFont(fil->font());
    if (erreur) format.setForeground(Qt::red);
    c.insertText(t, format);
    fil->setTextCursor(c);
    fil->ensureCursorVisible();
}

void Execution::poser() {
    delete zone->layout();
    qDeleteAll(zone->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
    auto *forme = new QFormLayout(zone);
    const int n = (int)travail.n;
    controles = QVector<QWidget *>(n, nullptr);
    videurs = QVector<QWidget *>(n, nullptr);
    const bool premiere = travail.nouvelle;
    if (premiere) {
        travail.saisies = QVector<Saisie>(n);
        fichiers_choisis = QVector<Saisie>(n);
    }
    QWidget *focus = nullptr;
    for (int k = 0; k < n; k++) {
        const Champ *ch = &travail.champs[k];
        const bool accepte = travail.acceptes[k];
        // la valeur à montrer : ce qui a été tapé (après un refus), sinon la valeur actuelle ou de départ
        const QString valeur = !premiere ? travail.saisies[k].ligne : QString::fromUtf8(ch->valeur ? ch->valeur : "");
        QWidget *w = nullptr;
        if (est_fichier(ch)) {
            auto *ligne = new QWidget;
            auto *h = new QHBoxLayout(ligne);
            h->setContentsMargins(0, 0, 0, 0);
            auto *choisir = new QPushButton("Choisir…");
            auto *nom = new QLabel(accepte ? "reçu"
                                   : !fichiers_choisis[k].nom_fichier.isEmpty() ? fichiers_choisis[k].nom_fichier
                                   : ch->valeur ? QString("actuel : %1").arg(QString::fromUtf8(ch->valeur))
                                                : QString("aucun"));
            choisir->setEnabled(!accepte);
            const bool image = std::strcmp(ch->type, "image") == 0;
            connect(choisir, &QPushButton::clicked, this, [this, k, nom, image] {
                const QString c = QFileDialog::getOpenFileName(this, "Choisir un fichier", QString(),
                                                               image ? "Images (*.png *.jpg *.jpeg *.gif *.webp)" : QString());
                if (c.isEmpty()) return;
                QFile f(c);
                if (!f.open(QIODevice::ReadOnly)) return;
                fichiers_choisis[k].fichier = true;
                fichiers_choisis[k].octets = f.readAll();
                fichiers_choisis[k].nom_fichier = QFileInfo(c).fileName();
                nom->setText(fichiers_choisis[k].nom_fichier);
            });
            h->addWidget(choisir);
            h->addWidget(nom, 1);
            w = ligne;
        } else if (est_oui_non(ch) || ch->suggestions_completes) {
            auto *menu = new QComboBox;
            QString v = valeur;
            if (est_oui_non(ch)) {
                if (v == "vrai") v = "oui";
                if (v == "faux") v = "non";
                if (ch->facultatif || (v != "oui" && v != "non")) menu->addItem(QString());
                menu->addItems({"oui", "non"});
            } else {   // un lien, tous les objets connus : un menu (docs/v2.md, § 10)
                menu->addItem(QString());
                QStringList cles;
                for (size_t j = 0; j < ch->nb_suggestions; j++) cles << QString::fromUtf8(ch->suggestions[j]);
                if (!v.isEmpty() && !cles.contains(v)) menu->addItem(v);   // dans la corbeille : jamais perdu
                menu->addItems(cles);
            }
            menu->setCurrentIndex(qMax(0, menu->findText(v)));
            w = menu;
        } else {
            auto *ligne = new QLineEdit(valeur);
            if (ch->nb_suggestions) {
                QStringList cles;
                for (size_t j = 0; j < ch->nb_suggestions; j++) cles << QString::fromUtf8(ch->suggestions[j]);
                auto *completion = new QCompleter(cles, ligne);
                completion->setCaseSensitivity(Qt::CaseInsensitive);
                ligne->setCompleter(completion);
            }
            connect(ligne, &QLineEdit::returnPressed, this, [this] { envoyer(0); });
            w = ligne;
        }
        w->setEnabled(!accepte);
        controles[k] = w;
        QWidget *rangee = w;
        if (ch->videable || !travail.refus[k].isEmpty()) {
            rangee = new QWidget;
            auto *v = new QVBoxLayout(rangee);
            v->setContentsMargins(0, 0, 0, 0);
            auto *h = new QHBoxLayout;
            h->addWidget(w, 1);
            if (ch->videable) {
                auto *vider = new QCheckBox("vider");
                vider->setEnabled(!accepte);
                videurs[k] = vider;
                h->addWidget(vider);
            }
            v->addLayout(h);
            if (!travail.refus[k].isEmpty()) {
                auto *r = new QLabel(travail.refus[k]);
                r->setStyleSheet("color: #c00;");
                r->setWordWrap(true);
                v->addWidget(r);
            }
        }
        forme->addRow(QString::fromUtf8(ch->libelle), rangee);
        if (!accepte && !focus) focus = w;
    }
    // la zone des champs grandit selon la question, jusqu'aux trois cinquièmes de la fenêtre
    const int total = partage->height();
    // les contrôles neufs ne sont pas encore montrés, leur taille pas encore calculée : on l'estime
    int rangees = n;
    for (int k = 0; k < n; k++) rangees += !travail.refus[k].isEmpty();
    const int voulu = qMin(bouton_envoyer->sizeHint().height() + 48 + rangees * (QLineEdit().sizeHint().height() + 10),
                           total * 3 / 5);
    partage->setSizes({total - voulu, voulu});
    attente = true;
    bouton_envoyer->setEnabled(true);
    bouton_annuler->setEnabled(true);
    etat->setText("En attente d'une réponse.");
    if (focus) focus->setFocus();
}

void Execution::envoyer(int issue) {
    if (!attente) return;
    const int n = (int)travail.n;
    for (int k = 0; k < n && issue == 0; k++) {
        if (travail.acceptes[k]) continue;
        Saisie s = fichiers_choisis[k];
        if (auto *l = qobject_cast<QLineEdit *>(controles[k])) s.ligne = l->text();
        else if (auto *m = qobject_cast<QComboBox *>(controles[k])) s.ligne = m->currentText();
        if (auto *v = qobject_cast<QCheckBox *>(videurs[k])) s.vider = v->isChecked();
        travail.saisies[k] = s;
    }
    attente = false;
    bouton_envoyer->setEnabled(false);
    bouton_annuler->setEnabled(false);
    for (QWidget *w : controles) if (w) w->setEnabled(false);
    etat->setText("En cours…");
    travail.issue = issue;
    travail.reponse.release();
}

void Execution::arreter() {
    grym_interruption = 1;   // la machine s'arrête au prochain saut arrière ou appel ; le journal annule
    if (attente) envoyer(2);
}

void Execution::terminer(bool ok, const QString &erreur, const QString &annulation) {
    fini = true;
    attente = false;
    travail.saisies.clear();
    delete zone->layout();
    qDeleteAll(zone->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
    bouton_envoyer->setEnabled(false);
    bouton_annuler->setEnabled(false);
    bouton_arreter->setEnabled(false);
    if (!ok) {
        ecrire("\n" + erreur + "\n", true);
        if (!annulation.isEmpty()) ecrire(annulation + "\n", true);
    }
    etat->setText(ok ? "Terminé." : "Terminé sur une erreur.");
    setWindowTitle(windowTitle().replace("exécution", ok ? "terminé" : "erreur"));
    if (fermer_a_la_fin) close();
}

void Execution::closeEvent(QCloseEvent *e) {
    if (fini) {
        travail.wait();
        e->accept();
        return;
    }
    fermer_a_la_fin = true;   // fermer pendant l'exécution l'arrête d'abord, proprement
    arreter();
    e->ignore();
}
