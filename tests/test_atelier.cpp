// GrymoiR : essais de l'atelier (docs/atelier.md, jalon A1), sans fenêtre.
#include "coloration.h"
#include "theme.h"
#include "editeur.h"
#include "execution.h"
#include "aide.h"
#include "projet.h"
#include "schema.h"
#include "reecriture.h"
#include "lien.h"
#include "onglet_ecrans.h"
#include <QMouseEvent>
#include <QMimeData>
#include <QDropEvent>
#include <QTreeWidget>
#include <QTableWidget>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextLayout>
#include <csignal>
#include <cstdio>
#include <functional>

extern "C" {
#include "compilateur.h"
#include "vm.h"
}

volatile std::sig_atomic_t grym_fermeture_demandee = 0;   // défini par principal.cpp dans l'atelier

// Exécute `source` dans la fenêtre d'exécution, sans écran. À chaque question, `repondre(numero, fenetre)`
// remplit les champs ; il rend faux pour cliquer « Annuler », vrai pour « Envoyer ». Rend le fil affiché.
static QString executer(const QString &source, const std::function<bool(int, QWidget &)> &repondre,
                        QString *etat = nullptr, int arreter_apres_ms = -1) {
    QTemporaryDir dossier;
    const QString chemin = dossier.filePath("essai.grym");
    QFile f(chemin);
    f.open(QIODevice::WriteOnly);
    f.write(source.toUtf8());
    f.close();
    Execution e(chemin);
    e.demarrer();
    QPushButton *envoyer = nullptr, *annuler = nullptr, *arreter = nullptr;
    for (QPushButton *b : e.findChildren<QPushButton *>()) {
        if (b->text() == "Envoyer") envoyer = b;
        if (b->text() == "Annuler") annuler = b;
        if (b->text() == "Arrêter") arreter = b;
    }
    QLabel *libelle_etat = nullptr;
    for (QLabel *l : e.findChildren<QLabel *>())
        if (l->text() == "En cours…") libelle_etat = l;
    QElapsedTimer montre;
    montre.start();
    int numero = 0;
    while (montre.elapsed() < 10000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (libelle_etat->text().startsWith("Terminé")) break;
        if (arreter_apres_ms >= 0 && montre.elapsed() > arreter_apres_ms && arreter->isEnabled()) arreter->click();
        if (envoyer->isEnabled()) {
            if (repondre(numero++, e)) envoyer->click();
            else annuler->click();
        }
    }
    if (etat) *etat = libelle_etat->text();
    const QString fil = e.findChild<QTextBrowser *>()->toPlainText();
    e.close();
    return fil;
}

// Les champs de saisie actifs de la question en cours, dans l'ordre.
static QList<QLineEdit *> lignes(QWidget &w) {
    QList<QLineEdit *> r;
    for (QLineEdit *l : w.findChildren<QLineEdit *>())
        if (l->isEnabled() && !qobject_cast<QComboBox *>(l->parent())) r << l;
    return r;
}

static int total = 0, echecs = 0;

static void verifier(int ligne, bool ok, const char *quoi) {
    total++;
    if (!ok) {
        echecs++;
        std::printf("ÉCHEC (test ligne %d) : %s\n", ligne, quoi);
    }
}
#define VERIFIER(c) verifier(__LINE__, (c), #c)

// La sorte de chaque caractère de `ligne` ; « . » pour aucune couleur, sinon la première lettre de la sorte.
static QString dessin(const QString &ligne, bool compacte = false) {
    static const char lettres[] = "cmntroNad";
    QString r;
    for (int s : Coloration::sortes(ligne, compacte)) r += s < 0 ? QChar('.') : QChar(lettres[s]);
    return r;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    // Coloration : chaque mot selon le lexeur ; les noms restent sans couleur
    VERIFIER(dessin("Le total vaut 12,50.") == "mm.......mmmm.nnnnno");
    VERIFIER(dessin("Si x > 3, afficher « a ».") == "cc...o.no.mmmmmmmm.ttttto");
    VERIFIER(dessin("Remarque : bonjour") == "rrrrrrrrrrrrrrrrrr");
    VERIFIER(dessin("La date vaut 21.09.2026.") == "mm......mmmm.ddddddddddo");
    VERIFIER(dessin("Le [frais et port] vaut 1.") == "mm.NNNNNNNNNNNNNNN.mmmm.no");
    // points de code hors du plan de base : l'emoji compte pour deux unités UTF-16
    VERIFIER(dessin("Afficher « 😀 » puis 1.") == "mmmmmmmm.tttttt.mmmm.no");
    // une erreur du lexeur marque la fin de la ligne
    VERIFIER(dessin("Le x vaut 3.5.").endsWith("aaaa"));
    // forme compacte : mots-clés à souligné, noms à soulignés sans couleur
    VERIFIER(dessin("_si prix_unitaire > 3 _alors", true) == "ccc...............o.n.cccccc");

    // Thème : cinq accents, rose et orange écartés ; chaque contraste déclaré se recalcule et tient son seuil
    {
        Theme &t = Theme::courant();
        VERIFIER(t.accents() == QStringList({"bleue", "verte", "turquoise", "violette", "grise"}));
        const QString ecarts = t.verifier_contrastes();
        if (!ecarts.isEmpty()) std::printf("%s", qPrintable(ecarts));
        VERIFIER(ecarts.isEmpty());
        VERIFIER(qAbs(Theme::contraste(QColor("#ffffff"), QColor("#000000")) - 21.0) < 1e-9);
        // une feuille complète pour chaque mode et chaque accent, sans jeton restant
        for (Theme::Mode m : {Theme::Clair, Theme::Sombre})
            for (const QString &a : t.accents()) {
                const QString f = t.feuille(m, a);
                VERIFIER(!f.isEmpty() && !f.contains('@') && f.contains("QPushButton"));
            }
        VERIFIER(t.feuille(Theme::Clair, "bleue").contains("#1F5AC7"));
        VERIFIER(t.feuille(Theme::Sombre, "verte").contains("#66C991"));
        // appliquer change la palette, la coloration suit ; un accent inconnu laisse l'accent en place
        t.installer(app);
        t.appliquer(Theme::Sombre, "violette");
        VERIFIER(QApplication::palette().color(QPalette::Window) == QColor("#1B1613"));
        VERIFIER(t.couleur("accent") == QColor("#B9A5FF"));
        t.appliquer(Theme::Sombre, "orange");
        VERIFIER(t.accent() == "violette");
        VERIFIER(t.couleur("n'existe pas") == QColor(Qt::magenta));
        QTextDocument doc("Le x vaut 21.09.2026.");
        Coloration c(&doc, false);
        c.rehighlight();
        auto couleur_a = [&doc](int pos) {
            for (const auto &r : doc.firstBlock().layout()->formats())
                if (pos >= r.start && pos < r.start + r.length) return r.format.foreground().color();
            return QColor();
        };
        VERIFIER(couleur_a(12) == t.couleur("syntaxe-date"));
        t.appliquer(Theme::Clair, "bleue");
        VERIFIER(couleur_a(12) == QColor("#8F4400"));
        VERIFIER(QApplication::font().family() == "Atkinson Hyperlegible Next");
    }

    // Analyse : la première erreur, avec sa position ; rien quand tout va bien
    Diagnostic_atelier d = analyser_source("Le total vaut 1.\nLe totl devient 2.\n", false);
    VERIFIER(d.ligne == 2);
    VERIFIER(d.message.contains("totl"));
    VERIFIER(analyser_source("Le total vaut 1.\nAfficher total.\n", false).message.isEmpty());
    VERIFIER(analyser_source("_le total << 1\n_afficher total\n", true).message.isEmpty());

    // L'éditeur analyse au chargement et garde la position de l'erreur
    Editeur e;
    e.charger("La quantité vaut 3.\nSi la quantité est positif, afficher 1.\n", false);
    VERIFIER(e.diagnostic().ligne == 2);
    VERIFIER(e.diagnostic().message.contains("positive"));
    VERIFIER(!e.document()->isModified());
    e.charger("La quantité vaut 3.\n", false);
    VERIFIER(e.diagnostic().message.isEmpty());

    // Exécution : questions, refus puis nouvelle réponse, affichage
    QString etat;
    QString fil = executer("Le nom vaut la réponse à « Nom ? ».\n"
                           "Le n vaut la réponse en nombre entier à « Combien ? ».\n"
                           "Afficher « Bonjour » puis nom puis n × 2.\n",
                           [](int k, QWidget &w) {
                               auto l = lignes(w);
                               if (l.size() != 1) return true;
                               l[0]->setText(k == 0 ? "Ana" : k == 1 ? "abc" : "3");
                               return true;
                           }, &etat);
    VERIFIER(fil.contains("Bonjour Ana 6"));
    VERIFIER(etat == "Terminé.");

    // Le refus s'affiche sous le champ, et la question revient
    int poses = 0;
    bool refus_vu = false;
    executer("Le n vaut la réponse en nombre entier à « Combien ? ».\n", [&](int k, QWidget &w) {
        poses++;
        for (QLabel *l : w.findChildren<QLabel *>()) refus_vu |= l->text().contains("n'est pas un nombre");
        lignes(w)[0]->setText(k == 0 ? "abc" : "2");
        return true;
    });
    VERIFIER(poses == 2);
    VERIFIER(refus_vu);

    // Annuler une question : l'exécution s'arrête, rien n'est écrit
    fil = executer("Afficher 1.\nLe n vaut la réponse à « Nom ? ».\n", [](int, QWidget &) { return false; }, &etat);
    VERIFIER(fil.startsWith("1"));
    VERIFIER(etat == "Terminé sur une erreur.");

    // Arrêter une boucle sans fin : l'interruption, et l'annulation
    fil = executer("Le x vaut 0.\nTant que vrai :\n    Le x devient x + 1.\n", [](int, QWidget &) { return true; }, &etat, 300);
    VERIFIER(fil.contains("Interrompu (Ctrl+C)."));
    VERIFIER(etat == "Terminé sur une erreur.");

    // Un formulaire d'entité : le lien se choisit dans un menu, le vrai ou faux aussi ; la base est à côté
    fil = executer("Un compositeur, conservé, a : un nom (texte), unique.\n"
                   "Une pièce, conservée, a : un titre (texte), un compositeur (compositeur), une édition (vrai ou faux).\n"
                   "Pour préparer :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n    Conserver c.\n"
                   "Préparer.\nLa p vaut une nouvelle pièce saisie.\nConserver p.\n"
                   "Afficher titre de p puis nom du compositeur de p puis édition de p.\n",
                   [](int, QWidget &w) {
                       lignes(w)[0]->setText("Suite");
                       auto menus = w.findChildren<QComboBox *>();
                       for (QComboBox *m : menus) {
                           if (m->findText("Bach") >= 0) m->setCurrentIndex(m->findText("Bach"));
                           if (m->findText("oui") >= 0) m->setCurrentIndex(m->findText("oui"));
                       }
                       return true;
                   }, &etat);
    VERIFIER(fil.contains("Suite Bach vrai"));
    VERIFIER(etat == "Terminé.");

    // Fichiers utilisés (grammaire, § 21) : erreur posée sur « Utiliser », fichier de déclarations reconnu,
    // exécution d'un programme qui utilise un autre fichier
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QString &texte) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::WriteOnly);
            f.write(texte.toUtf8());
        };
        ecrire("donnees.grym", "Le carré d'un n vaut n × n.\n");
        ecrire("faux.grym", "Le carré d'un n vaut n × n.\nLe cube d'un n vaut n × x.\n");
        bool declarations = false;
        Diagnostic_atelier d = analyser_source("Remarque : x.\nUtiliser « faux ».\n", false, dossier.filePath("prog.grym"));
        VERIFIER(d.ligne == 2 && d.colonne == 1);
        VERIFIER(d.message.startsWith("« ") && d.message.contains("faux.grym », ligne 2 : « x » inconnu."));
        d = analyser_source("Utiliser « donnees ».\nLe double d'un n vaut n × 2.\n", false, dossier.filePath("lib.grym"), &declarations);
        VERIFIER(d.message.isEmpty() && declarations);
        d = analyser_source("Utiliser « donnees ».\nAfficher le carré de 3.\n", false, dossier.filePath("prog.grym"), &declarations);
        VERIFIER(d.message.isEmpty() && !declarations);
        ecrire("prog.grym", "Utiliser « donnees ».\nAfficher le carré de 9.\n");
        Execution e(dossier.filePath("prog.grym"));
        e.demarrer();
        QElapsedTimer montre;
        montre.start();
        QLabel *etat_prog = nullptr;
        for (QLabel *l : e.findChildren<QLabel *>())
            if (l->text() == "En cours…") etat_prog = l;
        while (montre.elapsed() < 10000 && !etat_prog->text().startsWith("Terminé"))
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        VERIFIER(e.findChild<QTextBrowser *>()->toPlainText().contains("81"));
        e.close();
    }

    // L'aide : la grammaire embarquée, son sommaire, la recherche et les titres
    {
        Aide a;
        QStringList titres;
        for (int k = 0; k < a.table()->topLevelItemCount(); k++) titres << a.table()->topLevelItem(k)->text(0);
        VERIFIER(titres.contains("21. Fichiers utilisés (atelier, A2)") || titres.join("|").contains("21. Fichiers utilisés"));
        VERIFIER(titres.size() >= 21);
        VERIFIER(a.texte()->toPlainText().contains("Utiliser « données »."));
        VERIFIER(a.chercher("Utilisation circulaire"));
        VERIFIER(a.texte()->textCursor().selectedText().compare("Utilisation circulaire", Qt::CaseInsensitive) == 0);
        VERIFIER(!a.chercher("zxqwv introuvable"));
        VERIFIER(a.aller_au_titre("16.4"));
        VERIFIER(a.texte()->textCursor().block().text().startsWith("16.4"));
    }

    // projet.grymatelier : relu tel qu'écrit, trié ; une ligne mal formée s'ignore
    {
        QTemporaryDir dossier;
        FichierProjet p;
        p.dossier = dossier.path();
        p.programme_principal = "prog.grym";
        p.positions.insert("œuvre", QPointF(320, 120));
        p.positions.insert("compositeur", QPointF(40, 120));
        VERIFIER(p.ecrire());
        QFile f(FichierProjet::chemin(dossier.path()));
        f.open(QIODevice::ReadOnly);
        const QString t = QString::fromUtf8(f.readAll());
        f.close();
        VERIFIER(t == "Remarque : disposition de l'atelier ; sans effet sur le programme.\n"
                      "programme principal : prog.grym\ncompositeur : 40, 120\nœuvre : 320, 120\n");
        f.open(QIODevice::Append);
        f.write("n'importe quoi\n");
        f.close();
        FichierProjet q;
        q.lire(dossier.path());
        VERIFIER(q.programme_principal == "prog.grym" && q.positions.size() == 2);
        VERIFIER(q.positions.value("œuvre") == QPointF(320, 120));
        FichierProjet vide;
        vide.lire(dossier.filePath("absent"));
        VERIFIER(vide.positions.isEmpty() && vide.programme_principal.isEmpty());
    }

    // Le schéma : entités de tous les fichiers (une fois chacune), héritage, complément, liens, plusieurs
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QString &texte) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::WriteOnly);
            f.write(texte.toUtf8());
        };
        ecrire("donnees.grym", "Un genre, conservé, a : un nom (texte), unique.\n"
                               "Une personne, conservée, a : un nom (texte), unique, un maître (personne), facultatif.\n"
                               "Une œuvre, conservée, a : un titre (texte), un auteur (personne), des genres (genre).\n"
                               "Un compositeur, conservé, est une personne.\nUn compositeur a : un style (texte).\n"
                               "Un point a : un x.\n");
        ecrire("prog.grym", "Utiliser « donnees ».\nAfficher 1.\n");
        ecrire("faux.grym", "Le x vaut.\n");
        QStringList problemes;
        const QVector<EntiteSchema> e = lire_schema(dossier.path(), &problemes);
        QStringList noms;
        for (const auto &x : e) noms << x.nom;
        VERIFIER(noms == QStringList({"compositeur", "genre", "personne", "œuvre"}));   // pas « point » : une classe ordinaire
        VERIFIER(problemes == QStringList({"faux.grym"}));
        const EntiteSchema &c = e[0], &o = e[3], &p = e[2];
        VERIFIER(c.parent == "personne" && c.champs.size() == 1 && c.champs[0].nom == "style");
        VERIFIER(c.fichier.endsWith("donnees.grym") && c.ligne == 4);
        VERIFIER(o.champs.size() == 3 && o.champs[1].lien && o.champs[2].multiple && o.champs[2].lien && !o.champs[0].lien);
        VERIFIER(p.champs[1].facultatif && p.champs[1].lien && p.champs[0].unique);
        VueSchema vue;
        QMap<QString, QPointF> places;
        places.insert("genre", QPointF(500, 500));
        vue.montrer(e, places);
        VERIFIER(vue.nombre_de_boites() == 4);
        VERIFIER(vue.nombre_de_liens() == 4);   // auteur, genres, maître (vers soi), héritage
        const auto pos = vue.positions();
        VERIFIER(pos.value("genre") == QPointF(500, 500) && pos.size() == 4);
        // les entités nouvelles ne se chevauchent pas
        bool chevauche = false;
        const QStringList n = pos.keys();
        for (int i = 0; i < n.size(); i++)
            for (int j = i + 1; j < n.size(); j++)
                chevauche |= QRectF(pos[n[i]], QSizeF(180, 60)).intersects(QRectF(pos[n[j]], QSizeF(180, 60)));
        VERIFIER(!chevauche);
    }

    // Réécriture chirurgicale (A2-b) : seules les phrases concernées changent ; un geste qui casse est annulé
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QString &texte) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::WriteOnly);
            f.write(texte.toUtf8());
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::ReadOnly);
            return QString::fromUtf8(f.readAll());
        };
        const QString donnees = "Remarque :   à   garder   tel quel.\n"
                                "Un genre, conservé, a : un nom (texte), unique.\n"
                                "Une œuvre, conservée, a :\n    un titre (texte),\n    des genres (genre).\n"
                                "Le double d'un n vaut n × 2.\n";
        ecrire("donnees.grym", donnees);
        ecrire("prog.grym", "Utiliser « donnees ».\nPour chaque œuvre conservée :\n    Afficher titre de l'œuvre.\n");
        Geste g;
        ChampVoulu prix;
        prix.nom = "prix";
        prix.type = "nombre";
        prix.facultatif = true;
        VERIFIER(ajouter_champ(dossier.path(), "œuvre", prix, &g).isEmpty());
        VERIFIER(lire("donnees.grym") == "Remarque :   à   garder   tel quel.\n"
                                         "Un genre, conservé, a : un nom (texte), unique.\n"
                                         "Une œuvre, conservée, a :\n    un titre (texte),\n    des genres (genre),\n"
                                         "    un prix (nombre), facultatif.\n"
                                         "Le double d'un n vaut n × 2.\n");
        VERIFIER(g.avant.size() == 1 && annuler_geste(g).isEmpty() && lire("donnees.grym") == donnees);
        // renommer un champ que le programme lit : refusé, le fichier revient
        ChampVoulu nom;
        nom.nom = "nom";
        nom.type = "texte";
        const QString refus = modifier_champ(dossier.path(), "œuvre", "titre", nom, &g);
        VERIFIER(refus.startsWith("Geste annulé : il casserait « prog.grym », ligne 3"));
        VERIFIER(lire("donnees.grym") == donnees);
        // changer un type, une unicité : la seule phrase de l'entité change
        ChampVoulu titre;
        titre.nom = "titre";
        titre.type = "texte";
        titre.unique = true;
        VERIFIER(modifier_champ(dossier.path(), "œuvre", "titre", titre, &g).isEmpty());
        VERIFIER(lire("donnees.grym").contains("    un titre (texte), unique,\n    des genres (genre).\n"));
        annuler_geste(g);
        // renommer une entité : la déclaration et les liens qui la désignent suivent
        VERIFIER(renommer_entite(dossier.path(), "genre", "style", &g).isEmpty());
        VERIFIER(lire("donnees.grym").contains("Un style, conservé, a :\n    un nom (texte), unique.\n"));
        VERIFIER(lire("donnees.grym").contains("    des genres (style).\n"));
        VERIFIER(lire("donnees.grym").startsWith("Remarque :   à   garder   tel quel.\n"));
        annuler_geste(g);
        // une entité nouvelle, après la dernière déclaration
        VERIFIER(ajouter_entite(dossier.path(), dossier.filePath("donnees.grym"), "partition", true, &g).isEmpty());
        VERIFIER(lire("donnees.grym").contains("    des genres (genre).\nUne partition, conservée, a :\n    un nom (texte), unique.\n"
                                               "Le double"));
        annuler_geste(g);
        // supprimer une entité qu'une autre désigne : refusé ; la dernière d'un champ aussi
        VERIFIER(supprimer_entite(dossier.path(), "genre", &g).startsWith("Geste annulé"));
        VERIFIER(lire("donnees.grym") == donnees);
        VERIFIER(supprimer_champ(dossier.path(), "genre", "nom", &g).contains("une entité garde au moins un champ"));
        // supprimer un champ que personne ne lit
        VERIFIER(supprimer_champ(dossier.path(), "œuvre", "genres", &g).isEmpty());
        VERIFIER(lire("donnees.grym").contains("Une œuvre, conservée, a :\n    un titre (texte).\nLe double"));
        annuler_geste(g);
        VERIFIER(lire("donnees.grym") == donnees);
        // un nom de champ mal formé
        prix.nom = "prix (TTC)";
        VERIFIER(ajouter_champ(dossier.path(), "œuvre", prix, &g).contains("ni ponctuation"));
    }

    // Liens (A2-b) : chaque sorte écrit sa tournure ; la cascade s'accorde ; la valeur de départ se relit
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QString &texte) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::WriteOnly);
            f.write(texte.toUtf8());
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::ReadOnly);
            return QString::fromUtf8(f.readAll());
        };
        ecrire("d.grym", "Une partition, conservée, a :\n    une cote (texte), unique.\n"
                         "Un pupitre, conservé, a :\n    un nom (texte), unique.\n");
        Geste g;
        ChampVoulu c;
        c.nom = "partition";
        c.type = "partition";
        c.feminin = true;
        c.cascade = true;
        VERIFIER(ajouter_champ(dossier.path(), "pupitre", c, &g).isEmpty());
        VERIFIER(lire("d.grym").contains("    une partition (partition), et disparaît avec elle.\n"));   // « elle » : partition est féminine
        annuler_geste(g);
        c.cascade = false;
        c.unique = true;
        VERIFIER(ajouter_champ(dossier.path(), "pupitre", c, &g).isEmpty());
        VERIFIER(lire("d.grym").contains("    une partition (partition), unique.\n"));
        annuler_geste(g);
        c.unique = false;
        c.plusieurs = true;
        c.nom = "partitions";
        VERIFIER(ajouter_champ(dossier.path(), "pupitre", c, &g).isEmpty());
        VERIFIER(lire("d.grym").contains("    des partitions (partition).\n"));
        annuler_geste(g);
        // valeurs de départ : texte, nombre, date ; une valeur qui n'en est pas une est refusée avant d'écrire
        ChampVoulu pays;
        pays.nom = "pays";
        pays.type = "texte";
        pays.depart = "Suisse";
        VERIFIER(ajouter_champ(dossier.path(), "pupitre", pays, &g).isEmpty());
        VERIFIER(lire("d.grym").contains("    un pays (texte), « Suisse » au départ.\n"));
        VERIFIER(lire_schema(dossier.path()).value(1).champs.value(1).depart == "Suisse");
        annuler_geste(g);
        ChampVoulu prix;
        prix.nom = "prix";
        prix.type = "nombre";
        prix.depart = "12,50";
        VERIFIER(ajouter_champ(dossier.path(), "pupitre", prix, &g).isEmpty());
        VERIFIER(lire("d.grym").contains("    un prix (nombre), 12,50 au départ.\n"));
        VERIFIER(lire_schema(dossier.path()).value(1).champs.value(1).depart == "12,50");
        annuler_geste(g);
        prix.depart = "douze";
        VERIFIER(ajouter_champ(dossier.path(), "pupitre", prix, &g).contains("n'est pas une valeur de départ"));
        ChampVoulu jour;
        jour.nom = "jour";
        jour.type = "date";
        jour.depart = "01.03.2026";
        VERIFIER(ajouter_champ(dossier.path(), "pupitre", jour, &g).isEmpty());
        VERIFIER(lire_schema(dossier.path()).value(1).champs.value(1).depart == "01.03.2026");
        annuler_geste(g);
        // les phrases des sortes, pour le dialogue
        ChampVoulu s;
        VERIFIER(sorte_de_lien("œuvre", true, "compositeur", false, s) == "chaque œuvre a un compositeur ; un compositeur a plusieurs œuvres");
        s.unique = true;
        VERIFIER(sorte_de_lien("pupitre", false, "partition", true, s) == "un pupitre pour une partition, et inversement");
        s.unique = false;
        s.plusieurs = true;
        VERIFIER(sorte_de_lien("œuvre", true, "genre", false, s) == "plusieurs œuvres pour plusieurs genres");
    }

    // A2-c : l'aperçu de la migration ; il ne crée ni ne change la base
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        auto ecrire = [&](const QString &t) {
            QFile f(prog);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(t.toUtf8());
        };
        const QString v1 = "Un compositeur, conservé, a : un nom (texte), unique.\n"
                           "Si le nombre de compositeurs conservés = 0 :\n    Le c vaut un nouveau compositeur :\n"
                           "        Le nom vaut « Bach ».\n    Conserver c.\n";
        ecrire(v1);
        bool refusee = false;
        VERIFIER(apercu_migration(prog, &refusee) == "La base n'existe pas encore : le premier lancement la créera, avec 1 table.");
        VERIFIER(!refusee && !QFileInfo::exists(dossier.filePath("p.grymd")));
        {   // un vrai lancement crée la base et y range Bach
            const QByteArray src = v1.toUtf8(), base = dossier.filePath("p.grymd").toUtf8();
            Portee *po = portee_creer();
            Programme pr = {};
            Diagnostic d = {};
            analyser(src.constData(), (size_t)src.size(), po, 0, &pr, &d);
            Module *mo = compiler(&pr, &d);
            Machine *m = machine_creer();
            machine_base(m, base.constData());
            Chaine so = {};
            machine_executer(m, mo, &so, &d);
            free(so.d);
            machine_detruire(m);
            module_detruire(mo);
            programme_liberer(&pr);
            portee_detruire(po);
        }
        VERIFIER(apercu_migration(prog, &refusee) == "La base est à jour : rien ne changera.");
        ecrire(QString(v1).replace("unique.", "unique, un pays (texte), « Suisse » au départ."));
        VERIFIER(apercu_migration(prog, &refusee) == "« compositeur » : champ « pays » ajouté ; 1 compositeur reçoit « Suisse ».");
        ecrire(QString(v1).replace("unique.", "unique, un âge (nombre)."));
        VERIFIER(apercu_migration(prog, &refusee).startsWith("« âge » est nouveau, et 1 compositeur est déjà conservé") && refusee);
        ecrire(v1);
        VERIFIER(apercu_migration(prog, &refusee) == "La base est à jour : rien ne changera." && !refusee);
    }

    // Écrans (§ 22) dans la fenêtre d'exécution : la liste, le double-clic, les boutons, la fermeture
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("e.grym");
        QFile f(prog);
        f.open(QIODevice::WriteOnly);
        f.write("Un compositeur, conservé, a : un nom (texte), unique, une naissance (date), facultative.\n"
                "Si le nombre de compositeurs conservés = 0 :\n"
                "    Le c vaut un nouveau compositeur :\n        Le nom vaut « Liszt ».\n    Conserver c.\n"
                "    Le d vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n        La naissance vaut 31.03.1685.\n"
                "    Conserver d.\n"
                "L'écran des compositeurs montre :\n    le texte « Choisissez. »,\n"
                "    la liste des compositeurs conservés, par nom,\n    un bouton « Échouer »,\n    un bouton « Fermer ».\n"
                "Quand on choisit un compositeur dans l'écran des compositeurs :\n    Afficher « choisi » puis nom du compositeur.\n"
                "Quand on clique sur « Échouer » dans l'écran des compositeurs :\n    Afficher 1 ÷ 0.\n"
                "Quand on clique sur « Fermer » dans l'écran des compositeurs :\n    Fermer l'écran.\n"
                "Ouvrir l'écran des compositeurs.\nAfficher « après ».\n");
        f.close();
        Execution e(prog);
        e.show();
        e.demarrer();
        QElapsedTimer montre;
        montre.start();
        auto attendre = [&](const std::function<bool()> &cond) {
            while (montre.elapsed() < 10000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto table = [&]() { return e.findChild<QTableWidget *>(); };
        auto bouton = [&](const QString &t) -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == t) return b;
            return nullptr;
        };
        VERIFIER(attendre([&] { return table() && table()->rowCount() == 2 && table()->isEnabled() && table()->window()->isEnabled()
                                       && bouton("Fermer") && bouton("Fermer")->isEnabled() && bouton("Fermer")->isVisible(); }));
        VERIFIER(e.windowTitle() == "Compositeurs");
        VERIFIER(table()->item(0, 0)->text() == "Bach" && table()->item(0, 1)->text() == "31.03.1685" && table()->item(1, 1)->text().isEmpty());
        emit table()->cellActivated(0, 0);   // double-clic sur Bach
        VERIFIER(attendre([&] { return e.findChild<QTextBrowser *>()->toPlainText().contains("choisi Bach") && bouton("Échouer")->isEnabled(); }));
        bouton("Échouer")->click();
        QLabel *erreur = nullptr;
        VERIFIER(attendre([&] {
            for (QLabel *l : e.findChildren<QLabel *>()) if (l->text() == "Division par zéro." && l->isVisible()) erreur = l;
            return erreur && bouton("Fermer")->isEnabled();
        }));
        bouton("Fermer")->click();
        VERIFIER(attendre([&] { return e.findChild<QTextBrowser *>()->toPlainText().contains("après") && !table(); }));
        e.close();
    }

    // A3-b dans la fenêtre : une zone, sa valeur de départ, sa validation, la liste qui suit, la ligne choisie
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("z.grym");
        QFile f(prog);
        f.open(QIODevice::WriteOnly);
        f.write("Un compositeur, conservé, a : un nom (texte), unique, un pays (texte).\n"
                "Pour remplir un nom et un pays :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut nom.\n"
                "        Le pays vaut pays.\n    Conserver c.\n"
                "Si le nombre de compositeurs conservés = 0 :\n    Remplir « Bach » et « Allemagne ».\n"
                "    Remplir « Chopin » et « Pologne ».\n    Remplir « Schumann » et « Allemagne ».\n"
                "L'écran de recherche montre :\n    un pays (texte), « Allemagne » au départ,\n"
                "    une case vivants (vrai ou faux), facultative,\n"
                "    la liste des compositeurs conservés dont le pays est le pays de l'écran, par nom,\n"
                "    un bouton « Supprimer »,\n    un bouton « Fermer ».\n"
                "Quand on clique sur « Supprimer » dans l'écran de recherche :\n"
                "    Si le compositeur choisi de l'écran est présent, supprimer le compositeur choisi de l'écran.\n"
                "Quand on clique sur « Fermer » dans l'écran de recherche :\n    Fermer l'écran.\n"
                "Ouvrir l'écran de recherche.\nAfficher le nombre de compositeurs conservés.\n");
        f.close();
        Execution e(prog);
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 10000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto table = [&]() { return e.findChild<QTableWidget *>(); };
        auto bouton = [&](const QString &t) -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == t) return b;
            return nullptr;
        };
        QLineEdit *pays = nullptr;
        VERIFIER(attendre([&] {
            for (QLineEdit *l : e.findChildren<QLineEdit *>()) if (l->text() == "Allemagne") pays = l;
            return pays && table() && table()->rowCount() == 2 && bouton("Supprimer") && bouton("Supprimer")->isEnabled();
        }));
        VERIFIER(e.findChild<QCheckBox *>() != nullptr);   // la zone (vrai ou faux) : une case à cocher
        pays->setText("Pologne");   // taper, puis quitter la zone : un changement
        pays->setModified(true);
        emit pays->editingFinished();
        VERIFIER(attendre([&] { return table()->rowCount() == 1 && table()->item(0, 0)->text() == "Chopin"
                                       && bouton("Supprimer")->isEnabled(); }));
        table()->selectRow(0);
        bouton("Supprimer")->click();   // la ligne choisie part avec l'événement
        VERIFIER(attendre([&] { return table()->rowCount() == 0 && bouton("Fermer")->isEnabled(); }));
        bouton("Fermer")->click();
        VERIFIER(attendre([&] { return e.findChild<QTextBrowser *>()->toPlainText().contains("2"); }));
        e.close();
    }

    // A3-c dans la fenêtre : la fiche d'une ligne, le lien vers une autre fiche par-dessus, puis le retour
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("f.grym");
        QFile f(prog);
        f.open(QIODevice::WriteOnly);
        f.write("Un compositeur, conservé, a : un nom (texte), unique.\n"
                "Une œuvre, conservée, a : un titre (texte), unique, un compositeur (compositeur).\n"
                "Si le nombre d'œuvres conservées = 0 :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n"
                "    Conserver c.\n    Le o vaut une nouvelle œuvre :\n        Le titre vaut « Messe ».\n"
                "        Le compositeur vaut c.\n    Conserver o.\n"
                "L'écran des œuvres montre :\n    la liste des œuvres conservées, par titre,\n    un bouton « Fermer ».\n"
                "Quand on choisit une œuvre dans l'écran des œuvres :\n    Ouvrir la fiche de l'œuvre.\n"
                "Quand on clique sur « Fermer » dans l'écran des œuvres :\n    Fermer l'écran.\n"
                "Ouvrir l'écran des œuvres.\nAfficher « fin ».\n");
        f.close();
        Execution e(prog);
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 10000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto bouton = [&](const QString &t) -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == t && b->isVisible()) return b;
            return nullptr;
        };
        auto table = [&]() -> QTableWidget * {
            for (QTableWidget *t : e.findChildren<QTableWidget *>()) if (t->isVisible()) return t;
            return nullptr;
        };
        VERIFIER(attendre([&] { return table() && table()->rowCount() == 1 && bouton("Fermer") && bouton("Fermer")->isEnabled(); }));
        emit table()->cellActivated(0, 0);
        VERIFIER(attendre([&] { return e.windowTitle() == "Messe" && bouton("Compositeur : Bach")
                                       && bouton("Compositeur : Bach")->isEnabled() && !table(); }));
        bouton("Compositeur : Bach")->click();   // la fiche du lien, par-dessus
        VERIFIER(attendre([&] { return e.windowTitle() == "Bach" && !bouton("Compositeur : Bach") && bouton("Fermer")
                                       && bouton("Fermer")->isEnabled(); }));
        bouton("Fermer")->click();   // retour à la fiche de la Messe
        VERIFIER(attendre([&] { return e.windowTitle() == "Messe" && bouton("Compositeur : Bach")
                                       && bouton("Compositeur : Bach")->isEnabled(); }));
        bouton("Fermer")->click();   // retour à la liste
        VERIFIER(attendre([&] { return e.windowTitle() == "Œuvres" && table() && bouton("Fermer") && bouton("Fermer")->isEnabled(); }));
        bouton("Fermer")->click();
        VERIFIER(attendre([&] { return e.findChild<QTextBrowser *>()->toPlainText().contains("fin"); }));
        e.close();
    }

    // A4-a : l'onglet « Écrans » : lecture, aperçu sans données, choix d'un élément, propriétés, événements
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QString &texte) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::WriteOnly);
            f.write(texte.toUtf8());
        };
        ecrire("donnees.grym", "Un compositeur, conservé, a : un nom (texte), unique, une photo (image), facultative.\n"
                               "Une œuvre, conservée, a : un titre (texte), unique, un compositeur (compositeur).\n");
        ecrire("prog.grym", "Utiliser « donnees ».\n"
                            "L'écran des œuvres, « Catalogue », montre :\n    un pays (texte), « Suisse » au départ,\n"
                            "    côte à côte :\n        la liste des compositeurs conservés, par nom,\n"
                            "        la liste des œuvres conservées, par titre décroissant, avec le titre et le nom du compositeur,\n"
                            "    un bouton « Fermer ».\n"
                            "Quand on choisit une œuvre dans l'écran des œuvres :\n    Ouvrir la fiche de l'œuvre.\n"
                            "Quand on clique sur « Fermer » dans l'écran des œuvres :\n    Fermer l'écran.\n"
                            "L'écran d'accueil montre :\n    le texte « Bonjour »,\n    un bouton « OK ».\n"
                            "Quand on clique sur « OK » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                            "Ouvrir l'écran d'accueil.\n");
        const QVector<EcranLu> e = lire_ecrans(dossier.path());
        VERIFIER(e.size() == 2 && e[0].nom == "des œuvres" && e[0].titre == "Catalogue" && e[1].titre == "Accueil");
        VERIFIER(e[0].ligne == 2 && e[0].fichier.endsWith("prog.grym"));
        const auto &el = e[0].elements;   // zone, côte à côte, liste, liste, fin, bouton
        VERIFIER(el.size() == 6 && el[0].sorte == ELEMENT_ZONE && el[1].sorte == ELEMENT_COTE_A_COTE && el[4].sorte == ELEMENT_FIN_DE_BLOC);
        VERIFIER(el[0].texte == "pays" && el[0].type == "texte" && el[0].depart == "Suisse");
        VERIFIER(el[2].colonnes == QStringList({"Nom"}));   // la photo (image) n'est pas une colonne par défaut
        VERIFIER(el[3].colonnes == QStringList({"Titre", "Nom du compositeur"}) && el[3].tri == "titre" && el[3].decroissant);
        VERIFIER(el[3].evenement_ligne == 8 && el[2].evenement_ligne == 0 && el[5].evenement_ligne == 10);
        VERIFIER(el[5].ecrit == "un bouton « Fermer »");
        OngletEcrans o;
        o.show();
        o.montrer(dossier.path());
        VERIFIER(o.proprietes().contains("Titre") && o.proprietes().contains("Catalogue"));   // l'écran lui-même
        auto *t = qobject_cast<QTableWidget *>(o.controle(3));
        VERIFIER(t && t->rowCount() == 2 && t->item(0, 0)->text() == "…");   // la structure, pas les données
        auto cliquer = [&](QWidget *w) {
            QMouseEvent p(QEvent::MouseButtonPress, QPointF(2, 2), w->mapToGlobal(QPointF(2, 2)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &p);
        };
        auto *b = qobject_cast<QPushButton *>(o.controle(5));
        int clics = 0;
        QObject::connect(b, &QPushButton::clicked, [&] { clics++; });
        cliquer(b);   // choisir le bouton dans l'aperçu : il ne s'enfonce pas
        VERIFIER(clics == 0 && o.proprietes().contains("Libellé") && o.proprietes().contains("Fermer")
                 && o.proprietes().contains("prog.grym, ligne 10"));
        QString fichier_ouvert;
        int ligne_ouverte = 0;
        QObject::connect(&o, &OngletEcrans::ouvrir, [&](const QString &f, int l) { fichier_ouvert = f; ligne_ouverte = l; });
        for (QPushButton *x : o.findChildren<QPushButton *>()) if (x->text() == "Voir l'événement") x->click();
        VERIFIER(ligne_ouverte == 10 && fichier_ouvert.endsWith("prog.grym"));
        cliquer(t->viewport());
        VERIFIER(o.proprietes().contains("Nom du compositeur") && o.proprietes().contains("titre, décroissant"));
        cliquer(o.controle(0));
        VERIFIER(o.proprietes().contains("Zone de saisie") && o.proprietes().contains("Suisse"));
        o.choisir_ecran("d'accueil");
        VERIFIER(o.proprietes().contains("Accueil"));
    }

    // A4-b : écran généré pour une entité, écran d'accueil, écran vide, phrase finale
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QString &texte) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::WriteOnly);
            f.write(texte.toUtf8());
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::ReadOnly);
            return QString::fromUtf8(f.readAll());
        };
        const QString debut = "Un compositeur, conservé, a : un nom (texte), unique.\n"
                              "Une œuvre, conservée, a : un titre (texte), unique, un compositeur (compositeur).\n"
                              "Afficher « début ».\n";
        ecrire("p.grym", debut);
        const QString p = dossier.filePath("p.grym");
        Geste g;
        VERIFIER(generer_ecran(dossier.path(), p, "œuvre", &g).isEmpty());
        const QString t = lire("p.grym");
        VERIFIER(t.contains("L'écran des œuvres montre :\n    la liste des œuvres conservées, par titre,\n"));
        VERIFIER(t.contains("Quand on choisit une œuvre dans l'écran des œuvres :\n    Ouvrir la fiche de l'œuvre.\n"));
        VERIFIER(t.contains("    Le nouveau vaut une nouvelle œuvre saisie.\n"));
        VERIFIER(t.contains("    Si l'œuvre choisie de l'écran est présente, supprimer l'œuvre choisie de l'écran.\n"));
        VERIFIER(t.contains("L'écran d'accueil montre :\n    un bouton « Œuvres »,\n    un bouton « Fermer ».\n"));
        VERIFIER(t.endsWith("Ouvrir l'écran des œuvres.\nAfficher « début ».\n"));   // avant la première phrase exécutable
        VERIFIER(analyser_source(t, false, p).message.isEmpty());
        // la deuxième : l'accueil reçoit son bouton, avant « Fermer »
        VERIFIER(generer_ecran(dossier.path(), p, "compositeur", &g).isEmpty());
        const QString t2 = lire("p.grym");
        VERIFIER(t2.contains("L'écran d'accueil montre :\n    un bouton « Œuvres »,\n    un bouton « Compositeurs »,\n"
                             "    un bouton « Fermer ».\n"));
        VERIFIER(t2.contains("Quand on choisit un compositeur dans l'écran des compositeurs :\n    Ouvrir la fiche du compositeur.\n"));
        VERIFIER(t2.contains("Quand on clique sur « Compositeurs » dans l'écran d'accueil :\n    Ouvrir l'écran des compositeurs.\n"));
        VERIFIER(analyser_source(t2, false, p).message.isEmpty());
        VERIFIER(generer_ecran(dossier.path(), p, "œuvre", &g).contains("existe déjà"));
        annuler_geste(g);   // défait la deuxième : l'accueil perd son bouton
        VERIFIER(lire("p.grym") == t);
        VERIFIER(nouvel_ecran(dossier.path(), p, "de recherche", &g).isEmpty());
        VERIFIER(lire("p.grym").contains("L'écran de recherche montre :\n    un bouton « Fermer ».\n"
                                         "Quand on clique sur « Fermer » dans l'écran de recherche :\n    Fermer l'écran.\n"));
        VERIFIER(ajouter_phrase_finale(dossier.path(), p, "Ouvrir l'écran d'accueil.", &g).isEmpty());
        VERIFIER(lire("p.grym").endsWith("Afficher « début ».\nOuvrir l'écran d'accueil.\n"));
        VERIFIER(analyser_source(lire("p.grym"), false, p).message.isEmpty());
        // lu par l'onglet
        const QVector<EcranLu> e = lire_ecrans(dossier.path());
        VERIFIER(e.size() == 3 && e[0].titre == "Œuvres" && e[1].titre == "Accueil" && e[2].titre == "Recherche");
    }

    // A4-c : modifier un écran (titre, ajouter, supprimer, renommer, zone, liste) ; ses événements suivent
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QString &texte) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::WriteOnly);
            f.write(texte.toUtf8());
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            f.open(QIODevice::ReadOnly);
            return QString::fromUtf8(f.readAll());
        };
        ecrire("p.grym", "Un compositeur, conservé, a : un nom (texte), unique, une date de naissance (date), facultative.\n"
                         "L'écran des compositeurs montre :\n    la liste des compositeurs conservés, par nom,\n"
                         "    un bouton « Fermer ».\n"
                         "Quand on choisit un compositeur dans l'écran des compositeurs :\n    Ouvrir la fiche du compositeur.\n"
                         "Quand on clique sur « Fermer » dans l'écran des compositeurs :\n    Fermer l'écran.\n"
                         "Ouvrir l'écran des compositeurs.\n");
        const QString d = dossier.path(), p = dossier.filePath("p.grym"), e = "des compositeurs";
        Geste g;
        VERIFIER(ecran_titre(d, e, "Nos compositeurs", &g).isEmpty());
        VERIFIER(lire("p.grym").contains("L'écran des compositeurs, « Nos compositeurs », montre :\n"));
        ElementNouveau b;
        b.sorte = ELEMENT_BOUTON;
        b.texte = "Imprimer";
        VERIFIER(ecran_ajouter(d, e, b, &g).isEmpty());
        VERIFIER(lire("p.grym").contains("    un bouton « Fermer »,\n    un bouton « Imprimer ».\n"));
        VERIFIER(lire("p.grym").contains("    Fermer l'écran.\nQuand on clique sur « Imprimer » dans l'écran des compositeurs :\n"
                                         "    Remarque : à écrire.\nOuvrir"));
        b.texte = "Exporter";   // renommer : l'événement suit
        VERIFIER(ecran_modifier(d, e, 2, b, &g).isEmpty());
        VERIFIER(lire("p.grym").contains("un bouton « Exporter »") && lire("p.grym").contains("Quand on clique sur « Exporter » dans"));
        VERIFIER(!lire("p.grym").contains("Imprimer"));
        ElementNouveau z;
        z.sorte = ELEMENT_ZONE;
        z.texte = "pays";
        z.type = "texte";
        z.depart = "Suisse";
        VERIFIER(ecran_ajouter(d, e, z, &g).isEmpty());
        VERIFIER(lire("p.grym").contains("    un pays (texte), « Suisse » au départ.\n"));
        ElementNouveau l;   // la liste : colonnes choisies, tri décroissant ; la condition éventuelle reste
        l.sorte = ELEMENT_LISTE;
        l.tri = "nom";
        l.decroissant = true;
        l.colonnes = QStringList({"nom", "date de naissance"});
        VERIFIER(ecran_modifier(d, e, 0, l, &g).isEmpty());
        VERIFIER(lire("p.grym").contains("la liste des compositeurs conservés, par nom décroissant, avec le nom et la date de naissance,\n"));
        const QString avant = lire("p.grym");
        VERIFIER(ecran_supprimer(d, e, 2, &g).isEmpty());   // le bouton « Exporter » et son événement
        VERIFIER(!lire("p.grym").contains("Exporter") && lire("p.grym").contains("un bouton « Fermer »,\n    un pays"));
        annuler_geste(g);
        VERIFIER(lire("p.grym") == avant);
        VERIFIER(ecran_supprimer(d, e, 0, &g).isEmpty());   // la liste, et son « Quand on choisit »
        VERIFIER(!lire("p.grym").contains("Quand on choisit"));
        VERIFIER(analyser_source(lire("p.grym"), false, p).message.isEmpty());
        z.depart = "douze";
        z.type = "nombre";
        VERIFIER(ecran_ajouter(d, e, z, &g).contains("n'est pas une valeur de départ"));
        b.texte = "Fermer";   // un doublon : la réanalyse refuse, rien n'est écrit
        const QString stable = lire("p.grym");
        VERIFIER(ecran_ajouter(d, e, b, &g).startsWith("Geste annulé"));
        VERIFIER(lire("p.grym") == stable);
    }

    // A4-c dans l'onglet : choisir un bouton, changer son libellé, « Appliquer » : le geste renomme aussi l'événement
    {
        QTemporaryDir dossier;
        QFile f(dossier.filePath("p.grym"));
        f.open(QIODevice::WriteOnly);
        f.write("L'écran d'accueil montre :\n    le texte « Bonjour »,\n    un bouton « OK ».\n"
                "Quand on clique sur « OK » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        f.close();
        OngletEcrans o;
        o.show();
        o.montrer(dossier.path());
        VERIFIER(o.ecrans().size() == 1 && o.ecrans()[0].elements[1].evenement_ecrit
                 == "Quand on clique sur « OK » dans l'écran d'accueil :\n    Fermer l'écran.");
        Geste g;
        QString resultat = "pas de geste";
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        o.choisir_element(1);
        QApplication::processEvents();
        QLineEdit *libelle = nullptr;
        for (QLineEdit *l : o.findChildren<QLineEdit *>()) if (l->text() == "OK" && l->isVisible()) libelle = l;
        VERIFIER(libelle != nullptr);
        if (libelle) libelle->setText("Terminer");
        for (QPushButton *b : o.findChildren<QPushButton *>()) if (b->text() == "Appliquer" && b->isVisible()) b->click();
        if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
        VERIFIER(resultat.isEmpty());
        QFile r(dossier.filePath("p.grym"));
        r.open(QIODevice::ReadOnly);
        const QString t = QString::fromUtf8(r.readAll());
        VERIFIER(t.contains("un bouton « Terminer »") && t.contains("Quand on clique sur « Terminer » dans l'écran d'accueil"));
    }

    // A4-d : déplacer (ordre, côte à côte créé, l'un sous l'autre dans une rangée, bloc réduit à un élément retiré)
    {
        QTemporaryDir dossier;
        auto lire = [&]() {
            QFile f(dossier.filePath("p.grym"));
            f.open(QIODevice::ReadOnly);
            return QString::fromUtf8(f.readAll());
        };
        {
            QFile f(dossier.filePath("p.grym"));
            f.open(QIODevice::WriteOnly);
            f.write("L'écran d'accueil montre :\n    le texte « A »,\n    le texte « B »,\n    le texte « C »,\n    un bouton « OK ».\n"
                    "Quand on clique sur « OK » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        }
        const QString d = dossier.path(), e = "d'accueil";
        Geste g;
        auto declaration = [&]() { const QString t = lire(); return t.left(t.indexOf("Quand on")); };
        VERIFIER(ecran_deplacer(d, e, 2, 0, DEPOT_AVANT, &g).isEmpty());   // C au-dessus de A
        VERIFIER(declaration() == "L'écran d'accueil montre :\n    le texte « C »,\n    le texte « A »,\n    le texte « B »,\n"
                                  "    un bouton « OK ».\n");
        VERIFIER(ecran_deplacer(d, e, 2, 0, DEPOT_DROITE, &g).isEmpty());   // B à droite de C : un bloc côte à côte
        VERIFIER(declaration() == "L'écran d'accueil montre :\n    côte à côte :\n        le texte « C »,\n        le texte « B »,\n"
                                  "    le texte « A »,\n    un bouton « OK ».\n");
        VERIFIER(ecran_deplacer(d, e, 4, 1, DEPOT_GAUCHE, &g).isEmpty());   // A à gauche de C, dans la même rangée
        VERIFIER(declaration() == "L'écran d'accueil montre :\n    côte à côte :\n        le texte « A »,\n        le texte « C »,\n"
                                  "        le texte « B »,\n    un bouton « OK ».\n");
        VERIFIER(ecran_deplacer(d, e, 5, 2, DEPOT_APRES, &g).isEmpty());   // OK sous C : l'un sous l'autre, dans la rangée
        VERIFIER(declaration() == "L'écran d'accueil montre :\n    côte à côte :\n        le texte « A »,\n        l'un sous l'autre :\n"
                                  "            le texte « C »,\n            un bouton « OK »,\n        le texte « B ».\n");
        VERIFIER(analyser_source(lire(), false, dossier.filePath("p.grym")).message.isEmpty());
        // sortir B de la rangée, sous le bouton : la rangée réduite à C disparaît
        {
            QFile f(dossier.filePath("p.grym"));
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write("L'écran d'accueil montre :\n    côte à côte :\n        le texte « C »,\n        le texte « B »,\n"
                    "    le texte « A »,\n    un bouton « OK ».\n"
                    "Quand on clique sur « OK » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        }
        VERIFIER(ecran_deplacer(d, e, 2, 5, DEPOT_APRES, &g).isEmpty());
        VERIFIER(declaration() == "L'écran d'accueil montre :\n    le texte « C »,\n    le texte « A »,\n    un bouton « OK »,\n"
                                  "    le texte « B ».\n");
        VERIFIER(ecran_deplacer(d, e, 0, 0, DEPOT_AVANT, &g) == "Rien à déplacer.");
    }

    // A4-d dans l'onglet : la zone de dépôt, et un dépôt qui devient le geste de déplacement
    {
        VERIFIER(OngletEcrans::cote_de_depot(QSize(200, 40), QPointF(10, 20)) == DEPOT_GAUCHE);
        VERIFIER(OngletEcrans::cote_de_depot(QSize(200, 40), QPointF(190, 20)) == DEPOT_DROITE);
        VERIFIER(OngletEcrans::cote_de_depot(QSize(200, 40), QPointF(100, 10)) == DEPOT_AVANT);
        VERIFIER(OngletEcrans::cote_de_depot(QSize(200, 40), QPointF(100, 30)) == DEPOT_APRES);
        QTemporaryDir dossier;
        QFile f(dossier.filePath("p.grym"));
        f.open(QIODevice::WriteOnly);
        f.write("L'écran d'accueil montre :\n    le texte « A »,\n    le texte « B »,\n    un bouton « OK ».\n"
                "Quand on clique sur « OK » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        f.close();
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        o.montrer(dossier.path());
        QApplication::processEvents();
        Geste g;
        QString resultat = "pas de geste";
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        QWidget *a = o.controle(0);
        auto *mime = new QMimeData;
        mime->setData("application/x-grymoir-element", "1");   // B, lâché sur le quart droit de A
        const QPoint ici(a->width() - 2, a->height() / 2);
        QDragEnterEvent entree(ici, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);   // Qt n'accepte un dépôt
        QApplication::sendEvent(a, &entree);                                                    // qu'après l'entrée
        QDragMoveEvent survol(ici, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(a, &survol);
        QDropEvent depot(QPointF(ici), Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(a, &depot);
        if (!resultat.isEmpty()) std::printf("dépôt : %s\n", qPrintable(resultat));
        VERIFIER(resultat.isEmpty());
        QFile r(dossier.filePath("p.grym"));
        r.open(QIODevice::ReadOnly);
        VERIFIER(QString::fromUtf8(r.readAll()).startsWith("L'écran d'accueil montre :\n    côte à côte :\n        le texte « A »,\n"
                                                           "        le texte « B »,\n    un bouton « OK ».\n"));
        delete mime;
    }

    std::printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}
