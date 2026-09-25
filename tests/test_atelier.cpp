// GrymoiR : essais de l'atelier (docs/atelier.md, jalon A1), sans fenêtre.
#include "coloration.h"
#include "editeur.h"
#include "execution.h"
#include "aide.h"
#include "projet.h"
#include "schema.h"
#include "reecriture.h"
#include "lien.h"
#include <QTreeWidget>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <csignal>
#include <cstdio>
#include <functional>

extern "C" {
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
    static const char lettres[] = "cmntroNa";
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
    VERIFIER(dessin("La date vaut 21.09.2026.") == "mm......mmmm.nnnnnnnnnno");
    VERIFIER(dessin("Le [frais et port] vaut 1.") == "mm.NNNNNNNNNNNNNNN.mmmm.no");
    // points de code hors du plan de base : l'emoji compte pour deux unités UTF-16
    VERIFIER(dessin("Afficher « 😀 » puis 1.") == "mmmmmmmm.tttttt.mmmm.no");
    // une erreur du lexeur marque la fin de la ligne
    VERIFIER(dessin("Le x vaut 3.5.").endsWith("aaaa"));
    // forme compacte : mots-clés à souligné, noms à soulignés sans couleur
    VERIFIER(dessin("_si prix_unitaire > 3 _alors", true) == "ccc...............o.n.cccccc");

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

    std::printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}
