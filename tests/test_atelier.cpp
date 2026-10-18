// GrymoiR : essais de l'atelier (docs/atelier.md, jalon A1), sans fenêtre.
#include "coloration.h"
#include "theme.h"
#include "vue_ecran.h"
#include "editeur.h"
#include "execution.h"
#include "aide.h"
#include "fabrication.h"
#include "projet.h"
#include "rapport.h"
#include <QTextEdit>
#include <QPlainTextEdit>
#include <cmath>
#include "transcription.h"
#include <QFontMetrics>
#include "relecture.h"
#include "son.h"
#include <QTextBrowser>
#include "schema.h"
#include "reecriture.h"
#include "lien.h"
#include "onglet_ecrans.h"
#include "contenu.h"
#include <QRadioButton>
#include "importation.h"
#include "panneau.h"
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QUrl>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QTabBar>
#include <QTabWidget>
#include <QRegularExpression>
#include <QMimeData>
#include <QDropEvent>
#include <QTreeWidget>
#include <QTableWidget>
#include <QGridLayout>

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QElapsedTimer>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QMap>
#include <QFileInfo>
#include <QPixmap>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextLayout>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <functional>

extern "C" {
#include "compilateur.h"
#include "vm.h"
}

volatile std::sig_atomic_t grym_fermeture_demandee = 0;   // défini par principal.cpp dans l'atelier

static int total = 0, echecs = 0;

static void verifier(int ligne, bool ok, const char *quoi) {
    total++;
    if (!ok) {
        echecs++;
        std::printf("ÉCHEC (test ligne %d) : %s\n", ligne, quoi);
    }
}
#define VERIFIER(c) verifier(__LINE__, (c), #c)

// Exécute `source` dans la fenêtre d'exécution, sans écran. À chaque question, `repondre(numero, fenetre)`
// remplit les champs ; il rend faux pour cliquer « Annuler », vrai pour « Envoyer ». Rend le fil affiché.
static QString executer(const QString &source, const std::function<bool(int, QWidget &)> &repondre,
                        QString *etat = nullptr, int arreter_apres_ms = -1) {
    QTemporaryDir dossier;
    const QString chemin = dossier.filePath("essai.grym");
    QFile f(chemin);
    VERIFIER(f.open(QIODevice::WriteOnly));
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
    // Un bouton principal (2.14) : le rôle que la feuille du thème peint à la couleur d'accent
    {
        QVector<ElementVue> el(2);
        el[0].sorte = ELEMENT_BOUTON;
        el[0].texte = "Enregistrer";
        el[0].principal = true;
        el[1].sorte = ELEMENT_BOUTON;
        el[1].texte = "Fermer";
        VueEcran v = dessiner_ecran("Séance", el);
        VERIFIER(v.boutons.size() == 2 && v.boutons[0]->property("role").toString() == "principal"
                 && !v.boutons[1]->property("role").isValid());
        delete v.vue;
    }
    // Des tuiles (2.15) : une grille de quatre colonnes, chaque tuile son titre, sa description, son image
    {
        QVector<ElementVue> el(3);
        for (int k = 0; k < 2; k++) {
            el[k].sorte = ELEMENT_BOUTON;
            el[k].tuile = true;
            el[k].texte = k ? "Soins" : "Clients";
        }
        el[0].indication = "Fiches et historiques";
        QFile coeur(QString(GRYM_SOURCE) + "/vendor/lucide/check.svg");
        VERIFIER(coeur.open(QIODevice::ReadOnly));
        el[0].octets = coeur.readAll();
        el[2].sorte = ELEMENT_BOUTON;
        el[2].texte = "Fermer";
        VueEcran v = dessiner_ecran("Accueil", el);
        VERIFIER(v.boutons.size() == 3 && v.boutons[0]->property("role").toString() == "tuile");
        auto *g = v.boutons[0]->parentWidget() ? qobject_cast<QGridLayout *>(v.boutons[0]->parentWidget()->layout()) : nullptr;
        VERIFIER(g && g->indexOf(v.boutons[1]) >= 0 && g->columnCount() == 4);
        QStringList textes;
        for (QLabel *l : v.boutons[0]->findChildren<QLabel *>()) if (!l->text().isEmpty()) textes << l->text();
        VERIFIER(textes.contains("Clients") && textes.contains("Fiches et historiques"));
        bool image = false;
        for (QLabel *l : v.boutons[0]->findChildren<QLabel *>()) image |= !l->pixmap().isNull();
        VERIFIER(image);
        VERIFIER(g && g->indexOf(v.boutons[2]) < 0);   // un bouton ordinaire ne rejoint pas la grille
        delete v.vue;
    }
    // Les projets du dépôt : trouvés depuis le dossier de construction, rien autour d'une application installée
    {
        bool trouve = false;
        const QStringList p = projets_du_depot(QString(GRYM_SOURCE) + "/construction", &trouve);
        VERIFIER(trouve && p.size() >= 3);
        bool cabinet = false;
        for (const QString &x : p) cabinet |= x.endsWith("/projets/Cabinet");
        VERIFIER(cabinet);
        QTemporaryDir ailleurs;
        VERIFIER(projets_du_depot(ailleurs.path(), &trouve).isEmpty() && !trouve);
    }
    // Le tri d'une cellule de liste (§ 22.1) : par la valeur, comme on la lit
    {
        auto avant = [](const char *a, const char *b) {
            return CelluleTriable(QString::fromUtf8(a)) < CelluleTriable(QString::fromUtf8(b));
        };
        VERIFIER(avant("70", "130") && !avant("130", "70"));
        VERIFIER(avant("999,95", "1'234,50") && avant("−3", "2"));
        VERIFIER(avant("15.09.2025", "01.03.2026") && avant("9:30", "14:05"));
        VERIFIER(avant("", "70") && avant("Anne", "Bruno") && avant("élodie", "Fabien"));
    }
    // « suivi » après « de » : un mot de nom, sans couleur ; « suivi de », l'opérateur (§ 4.4)
    VERIFIER(dessin("Le x vaut le tarif de suivi du s suivi de « a ».") == "mm...mmmm.mm.......mm.......mm...mmmmm.mm.ttttto");
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

    // Dessin d'un écran (T2) : marges, écarts et hauteurs viennent des jetons ; libellé au-dessus de sa zone ;
    // le message d'erreur sous le titre
    {
        Theme &t = Theme::courant();
        VERIFIER(t.mesure("marge-ecran") == 24 && t.mesure("cote-a-cote") == 16 && t.mesure("hauteur-ligne") == 32);
        VERIFIER(t.mesure("rayon-controle") == 8 && t.mesure("trait-focus") == 2);
        QVector<ElementVue> el(6);
        el[0].sorte = ELEMENT_ZONE;
        el[0].texte = "Pays";
        el[0].type = "texte";
        el[1].sorte = ELEMENT_COTE_A_COTE;
        el[2].sorte = ELEMENT_LISTE;
        el[2].colonnes = QStringList({"Nom", "Naissance"});
        el[3].sorte = ELEMENT_TEXTE;
        el[3].texte = "À droite";
        el[4].sorte = ELEMENT_FIN_DE_BLOC;
        el[5].sorte = ELEMENT_BOUTON;
        el[5].texte = "Fermer";
        VueEcran v = dessiner_ecran("Catalogue", el);
        auto *pile = qobject_cast<QVBoxLayout *>(v.vue->layout());
        VERIFIER(pile && pile->contentsMargins() == QMargins(24, 24, 24, 24) && pile->spacing() == 12);
        VERIFIER(pile->itemAt(0)->widget() == v.titre && pile->itemAt(1)->widget() == v.erreur);
        VERIFIER(v.titre->property("niveau").toString() == "ecran" && v.titre->textFormat() == Qt::PlainText);
        VERIFIER(v.erreur->property("message").toString() == "erreur" && v.erreur->isHidden());
        // la zone : un conteneur, libellé au-dessus, le contrôle dessous
        auto *rang = qobject_cast<QVBoxLayout *>(v.controles[0]->layout());
        VERIFIER(rang && rang->count() == 2);
        auto *libelle = qobject_cast<QLabel *>(rang->itemAt(0)->widget());
        VERIFIER(libelle && libelle->text() == "Pays *" && libelle->property("niveau").toString() == "etiquette");
        VERIFIER(rang->itemAt(1)->widget() == v.zones[0] && libelle->buddy() == v.zones[0]);
        // le bloc côte à côte, ses écarts ; la liste, sa hauteur de ligne et ses titres à gauche
        QHBoxLayout *cote = nullptr;
        for (int k = 0; k < pile->count(); k++)
            if (auto *h = qobject_cast<QHBoxLayout *>(pile->itemAt(k)->layout()); h && h->indexOf(v.tables[2]) >= 0) cote = h;
        VERIFIER(cote && cote->spacing() == 16);
        VERIFIER(v.tables[2]->verticalHeader()->defaultSectionSize() == 32 && !v.tables[2]->showGrid());
        VERIFIER(v.tables[2]->horizontalHeader()->defaultAlignment() == (Qt::AlignLeft | Qt::AlignVCenter));
        VERIFIER(v.boutons.size() == 1 && v.boutons[0]->text() == "Fermer");
        // une case à cocher porte son libellé, sans étiquette au-dessus
        QVector<ElementVue> el2(1);
        el2[0].sorte = ELEMENT_ZONE;
        el2[0].texte = "Actif";
        el2[0].type = "vrai ou faux";
        VueEcran v2 = dessiner_ecran("Réglages", el2);
        VERIFIER(v2.controles[0]->layout()->count() == 1 && qobject_cast<QCheckBox *>(v2.zones[0])->text() == "Actif");
        // un logo : à gauche du titre, à la hauteur demandée, ses proportions gardées
        QPixmap logo(10, 20);
        logo.fill(Qt::red);
        VueEcran v3 = dessiner_ecran("Accueil", el2, logo, 64);
        VERIFIER(v3.logo && qRound(v3.logo->pixmap().height() / v3.logo->pixmap().devicePixelRatio()) == 64);
        VERIFIER(qRound(v3.logo->pixmap().width() / v3.logo->pixmap().devicePixelRatio()) == 32);
        auto *entete = qobject_cast<QHBoxLayout *>(v3.vue->layout()->itemAt(0)->layout());
        VERIFIER(entete && entete->itemAt(0)->widget() == v3.logo && entete->itemAt(1)->widget() == v3.titre);
        VERIFIER(v3.vue->layout()->itemAt(1)->widget() == v3.erreur);
        VERIFIER(!v.logo);
        delete v.vue;
        delete v2.vue;
        delete v3.vue;
    }

    // Aide en contexte : le mot au curseur désigne sa section ; chaque section de la table existe dans la grammaire
    {
        VERIFIER(section_au_curseur("Selon le mois :", 2) == "10.5");
        VERIFIER(section_au_curseur("    _selon mois", 7) == "10.5");                   // forme compacte
        VERIFIER(section_au_curseur("Pour chaque mois de 1 à 12 :", 1) == "10.3");      // « Pour » suivi de « chaque »
        VERIFIER(section_au_curseur("Pour relancer un client :", 1) == "9.2");          // une action
        VERIFIER(section_au_curseur("Chaque stock conservé vérifie :", 2) == "16.14");  // une règle
        VERIFIER(section_au_curseur("Tant que x > 0 :", 6) == "10.1");                  // « que » après « tant »
        VERIFIER(section_au_curseur("_tant_que x > 0", 3) == "10.1");
        VERIFIER(section_au_curseur("Les écrans ont la couleur verte.", 1) == "22.5");
        VERIFIER(section_au_curseur("Les nombres s'affichent à la suisse.", 1) == "4.1");
        VERIFIER(section_au_curseur("Ouvrir l'écran d'accueil.", 10) == "22.3");
        VERIFIER(section_au_curseur("Afficher aujourd'hui.", 11) == "14.3");
        VERIFIER(section_au_curseur("Afficher d + 3 mois.", 16) == "14.2");
        VERIFIER(section_au_curseur("Le total vaut 3.", 10) == "2.1");
        VERIFIER(section_au_curseur("Le total vaut 3.", 4).isEmpty());                  // un nom du programme
        VERIFIER(section_au_curseur("", 0).isEmpty());
        Aide aide;
        for (const QString &s : sections_de_l_aide_en_contexte()) {
            if (!aide.aller_au_titre(s)) std::printf("section introuvable : %s\n", qPrintable(s));
            VERIFIER(aide.aller_au_titre(s));
        }
        VERIFIER(aide.aller_au_titre("10.5") && aide.texte()->textCursor().block().text().startsWith("10.5 Selon"));
    }

    // Nouveau fichier : « .grym » ajouté, jamais d'écrasement, pas de dossier dans le nom ; le guide embarqué
    {
        QTemporaryDir d;
        QString erreur;
        const QString c = creer_fichier(d.path(), "association", &erreur);
        VERIFIER(c == QDir(d.path()).filePath("association.grym") && QFileInfo(c).isFile() && QFileInfo(c).size() == 0);
        VERIFIER(creer_fichier(d.path(), "association.grym", &erreur).isEmpty() && erreur.contains("jamais écrasé"));
        VERIFIER(creer_fichier(d.path(), "outils.grymc", &erreur) == QDir(d.path()).filePath("outils.grymc"));
        VERIFIER(creer_fichier(d.path(), "a/b", &erreur).isEmpty() && creer_fichier(d.path(), "  ", &erreur).isEmpty());
        Aide guide(nullptr, ":/documentation/guide.md", "Premiers pas avec GrymoiR");
        VERIFIER(guide.windowTitle() == "Premiers pas avec GrymoiR");
        VERIFIER(guide.aller_au_titre("1. Premiers pas") && guide.aller_au_titre("3. Répéter"));
        VERIFIER(guide.table()->topLevelItemCount() >= 3);
    }

    // Un rapport en PDF (§ 15.6) : la mise en page seule, sur plusieurs pages ; puis un export réel, dans une exécution
    {
        QString html = "<html><body><h1>Essai</h1><table><thead><tr><th>Nom</th><th>Montant</th></tr></thead><tbody>";
        for (int i = 0; i < 150; i++) html += QString("<tr><td>Membre %1</td><td align=\"right\">%2</td></tr>").arg(i).arg(i * 10);
        html += "</tbody></table></body></html>";
        QString erreur;
        const QByteArray pdf = pdf_depuis_html(html, &erreur);
        VERIFIER(pdf.startsWith("%PDF-") && erreur.isEmpty());
        VERIFIER(pdf.count("/Type /Page\n") + pdf.count("/Type /Page ") + pdf.count("/Type /Page\r") >= 2 || pdf.count("/Page") >= 3);
    }
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Un membre, conservé, a :\n    un nom (texte),\n    une cotisation (nombre).\n"
                    "Le a vaut un nouveau membre :\n    Le nom vaut « Anne ».\n    La cotisation vaut 50.\nConserver a.\n"
                    "Exporter les membres conservés, avec le nom et la cotisation, avec le titre « Cotisations », "
                    "avec le total de la cotisation, dans « rapport.pdf ».\n");
        }
        Execution e(prog);
        e.show();
        e.demarrer();
        const QString rapport = dossier.filePath("rapport.pdf");
        // le fichier existe dès que son écriture commence : on attend un PDF complet, de « %PDF- » à « %%EOF »
        auto complet = [&] {
            QFile f(rapport);
            if (!f.open(QIODevice::ReadOnly)) return false;
            const QByteArray o = f.readAll();
            return o.startsWith("%PDF-") && o.trimmed().endsWith("%%EOF");
        };
        QElapsedTimer z;
        z.start();
        while (z.elapsed() < 60000 && !complet()) QApplication::processEvents(QEventLoop::AllEvents, 20);
        VERIFIER(complet());
    }

    // Une zone de son (§ 15.7), telle que la palette la crée : « un mémo (son) », et son aperçu choisit un fichier
    {
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    le texte « t ».\n");
        }
        Geste g;
        ElementNouveau n;
        n.sorte = ELEMENT_ZONE;
        n.type = "son";
        n.texte = QString::fromUtf8("mémo");
        VERIFIER(ecran_ajouter(dossier.path(), "x", n, &g, -1, QPoint()).isEmpty());
        QFile f(chemin);
        VERIFIER(f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains(QString::fromUtf8("un mémo (son)")));
        QVector<ElementVue> el(1);
        el[0].sorte = ELEMENT_ZONE;
        el[0].texte = QString::fromUtf8("Mémo");
        el[0].type = "son";
        VueEcran v = dessiner_ecran("Saisie", el);
        bool choisir = false;
        for (QPushButton *b2 : v.vue->findChildren<QPushButton *>()) choisir |= b2->property("fichier").toBool();
        VERIFIER(choisir);   // comme une zone de fichier : un bouton « Choisir un fichier… »
        delete v.vue;
    }

    // La transcription (§ 15.8) : le WAV lu sans greffon, le nettoyage, Whisper sur le modèle d'essai miniature, puis
    // une vraie exécution où « une note (texte), transcrite du mémo » se remplit
    {
        auto wav = [](int frequence, int canaux, int bits, double secondes) {
            QByteArray o;
            const int n = (int)(frequence * secondes), octets = bits / 8;
            auto u32 = [&](quint32 v) { for (int i = 0; i < 4; i++) o.append(char((v >> (8 * i)) & 0xFF)); };
            auto u16 = [&](quint16 v) { o.append(char(v & 0xFF)); o.append(char(v >> 8)); };
            o.append("RIFF"); u32(36 + n * canaux * octets); o.append("WAVEfmt "); u32(16); u16(1); u16((quint16)canaux);
            u32((quint32)frequence); u32((quint32)(frequence * canaux * octets)); u16((quint16)(canaux * octets)); u16((quint16)bits);
            o.append("data"); u32((quint32)(n * canaux * octets));
            for (int i = 0; i < n; i++)
                for (int c = 0; c < canaux; c++) {
                    const double x = 0.3 * std::sin(2 * 3.14159265358979 * 440 * i / frequence);
                    if (bits == 16) u16((quint16)(qint16)(x * 32767));
                    else o.append(char((int)(128 + x * 127)));
                }
            return o;
        };
        std::vector<float> pcm;
        VERIFIER(wav_en_16k(wav(44100, 2, 16, 1.0), &pcm) && pcm.size() >= 15990 && pcm.size() <= 16010);
        float crete = 0;
        for (float x : pcm) crete = std::max(crete, std::fabs(x));
        VERIFIER(crete > 0.25f && crete < 0.35f);   // l'amplitude survit à la conversion
        VERIFIER(wav_en_16k(wav(8000, 1, 8, 0.5), &pcm) && pcm.size() >= 7990 && pcm.size() <= 8010);
        VERIFIER(!wav_en_16k(QByteArray("ID3 pas un wav du tout, bien sûr"), &pcm));
        VERIFIER(nettoyer_transcription("Bonjour.\n[Propos inaudibles] [Propos inaudibles]\n[Sous-titres réalisés para la communauté d'Amara.org]\nFin.")
                 == "Bonjour.\nFin.");
        QString erreur;
        transcrire_son(wav(16000, 1, 16, 2.0), MODELE_ESSAI, &erreur);
        VERIFIER(erreur.isEmpty());   // le modèle d'essai se charge et tourne
        VERIFIER(modele_charge());
        liberer_transcription();       // ce que fait la fermeture de l'application : sur Mac, Metal l'exige
        VERIFIER(!modele_charge());
        QString sans;
        VERIFIER(transcrire_son(wav(16000, 1, 16, 1.0), QString(), &sans).isEmpty() && sans.contains("introuvable"));
        // la relecture : des marques invisibles, des nombres et des doutes surlignés, un doute qui part à la correction
        {
            const QFontMetrics fm(QApplication::font());
            VERIFIER(fm.horizontalAdvance(QString("ab")) == fm.horizontalAdvance(QString("a") + QChar(DEBUT_DOUTE) + QChar(FIN_DOUTE)
                                                                             + QChar(MARQUE_TRANSCRIT) + QString("b")));
            QPlainTextEdit z;
            installer_relecture(&z);
            const QString texte = QString(QChar(MARQUE_TRANSCRIT)) + "Pour 2 309 francs, nous vous " + QChar(DEBUT_DOUTE)
                                  + "avirons" + QChar(FIN_DOUTE) + " un verre.";
            z.setPlainText(texte);
            z.resize(600, 200);
            z.show();
            QApplication::processEvents();
            auto formats = [&] { return z.document()->firstBlock().layout()->formats(); };
            auto couvert = [&](const QString &mot, bool souligne) {
                const int i = (int)z.toPlainText().indexOf(mot);
                for (const auto &r : formats())
                    if (r.start <= i && r.start + r.length > i && r.format.background().style() != Qt::NoBrush
                        && (!souligne || r.format.underlineStyle() == QTextCharFormat::WaveUnderline))
                        return true;
                return false;
            };
            VERIFIER(couvert("2 309", false));   // le nombre, surligné
            VERIFIER(couvert("avirons", true));  // le mot douteux, souligné
            VERIFIER(!couvert("verre", false));  // le reste, intact
            QTextCursor c(z.document());          // on corrige le mot douteux : son doute s'en va
            c.setPosition((int)z.toPlainText().indexOf("avirons") + 3);
            c.insertText("X");
            QApplication::processEvents();
            VERIFIER(!z.toPlainText().contains(QChar(DEBUT_DOUTE)) && !z.toPlainText().contains(QChar(FIN_DOUTE)));
            VERIFIER(z.toPlainText().contains("aviXrons") && couvert("2 309", false));
            QPlainTextEdit ordinaire;              // un texte qui ne vient pas d'une transcription : rien n'est surligné
            installer_relecture(&ordinaire);
            ordinaire.setPlainText("Pour 2 309 francs.");
            ordinaire.show();
            QApplication::processEvents();
            bool colore = false;
            for (const auto &r : ordinaire.document()->firstBlock().layout()->formats()) colore |= r.format.background().style() != Qt::NoBrush;
            VERIFIER(!colore);
        }
        // le modèle n'entre que dans les applications qui transcrivent
        {
            QTemporaryDir p1, p2;
            QFile a(p1.filePath("a.grym")), b(p2.filePath("b.grym"));
            VERIFIER(a.open(QIODevice::WriteOnly) && b.open(QIODevice::WriteOnly));
            a.write("Une visite, conservée, a :\n    un mémo (son),\n    une note (texte), transcrite du mémo.\n");
            b.write("Un membre, conservé, a :\n    un nom (texte).\nAfficher « transcrite du mémo ».\n");
            a.close();
            b.close();
            VERIFIER(programme_transcrit(p1.path()));
            VERIFIER(!programme_transcrit(p2.path()));   // le mot dans un texte n'est pas une déclaration
        }
        // une vraie exécution
        qputenv("GRYMOIR_MODELE_TRANSCRIPTION", MODELE_ESSAI);
        QTemporaryDir dossier;
        {
            QFile f(dossier.filePath("memo.wav"));
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write(wav(16000, 1, 16, 2.0));
        }
        const QString prog = dossier.filePath("p.grym");
        {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Une visite, conservée, a :\n    un mémo (son),\n    une note (texte), facultative, transcrite du mémo.\n"
                    "Le v vaut une nouvelle visite :\n    Le mémo vaut le fichier « memo.wav ».\n"
                    "Si la note de v est présente, afficher « transcrit ».\n");
        }
        Execution e(prog);
        e.show();
        e.demarrer();
        auto sortie = [&] {
            QString t;
            for (QPlainTextEdit *x : e.findChildren<QPlainTextEdit *>()) t += x->toPlainText();
            for (QTextEdit *x : e.findChildren<QTextEdit *>()) t += x->toPlainText();
            return t;
        };
        QElapsedTimer z;
        z.start();
        while (z.elapsed() < 60000 && !sortie().contains("transcrit")) QApplication::processEvents(QEventLoop::AllEvents, 20);
        VERIFIER(sortie().contains("transcrit"));
        // dans un écran : cliquer charge un son, la zone de transcription se remplit (§ 15.8)
        {
            const QString prog2 = dossier.filePath("e.grym");
            QFile f(prog2);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    un mémo (son), facultatif,\n"
                    "    une transcription (texte), sur plusieurs lignes, facultative, transcrite du mémo,\n"
                    "    un bouton « Charger ».\n"
                    "Quand on clique sur « Charger » dans l'écran x :\n    Le mémo de l'écran devient le fichier « memo.wav ».\n"
                    "    Si la transcription de l'écran est présente, afficher « zone transcrite ».\n    Fermer l'écran.\n"
                    "Ouvrir l'écran x.\n");
            f.close();
            Execution e2(prog2);
            e2.show();
            e2.demarrer();
            auto bouton = [&]() -> QPushButton * {
                for (QPushButton *b : e2.findChildren<QPushButton *>()) if (b->text() == "Charger" && b->isVisible()) return b;
                return nullptr;
            };
            QElapsedTimer z2;
            z2.start();
            while (z2.elapsed() < 15000 && !bouton()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            VERIFIER(bouton() != nullptr);
            if (bouton()) bouton()->click();
            auto sortie2 = [&] {
                QString t;
                for (QPlainTextEdit *x : e2.findChildren<QPlainTextEdit *>()) t += x->toPlainText();
                for (QTextEdit *x : e2.findChildren<QTextEdit *>()) t += x->toPlainText();
                return t;
            };
            while (z2.elapsed() < 60000 && !sortie2().contains("zone transcrite")) QApplication::processEvents(QEventLoop::AllEvents, 20);
            VERIFIER(sortie2().contains("zone transcrite"));
        }
        // la zone de transcription, telle que la palette la pose sous un son
        {
            QTemporaryDir d3;
            QFile f(d3.filePath("p.grym"));
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    un mémo (son).\n");
            f.close();
            Geste g;
            ElementNouveau t;
            t.sorte = ELEMENT_ZONE;
            t.type = "texte";
            t.feminin = true;
            t.facultatif = true;
            t.plusieurs_lignes = true;
            t.transcrit_de = QString::fromUtf8("mémo");
            t.texte = "transcription";
            VERIFIER(ecran_ajouter(d3.path(), "x", t, &g, -1, QPoint()).isEmpty());
            QFile l(d3.filePath("p.grym"));
            VERIFIER(l.open(QIODevice::ReadOnly));
            const QString lu = QString::fromUtf8(l.readAll());
            VERIFIER(QString::fromUtf8(lu.toUtf8()).contains(QString::fromUtf8("une transcription (texte), facultative, sur plusieurs lignes, transcrite du mémo")));
        }
        qunsetenv("GRYMOIR_MODELE_TRANSCRIPTION");
    }

    // Les étiquettes (§ 16.15), dans l'exécution : la cellule d'une valeur colorée prend ses couleurs, sans le marqueur
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Une cotisation, conservée, a :\n    un membre (texte),\n    un état (« payée » en vert, « en retard » en rouge ou « annulée »).\n"
                          "Le a vaut une nouvelle cotisation :\n    Le membre vaut « Anne ».\n    L'état vaut « payée ».\nConserver a.\n"
                          "Le b vaut une nouvelle cotisation :\n    Le membre vaut « Bruno ».\n    L'état vaut « en retard ».\nConserver b.\n"
                          "Le c vaut une nouvelle cotisation :\n    Le membre vaut « Cléa ».\n    L'état vaut « annulée ».\nConserver c.\n"
                          "L'écran x montre :\n    la liste des cotisations conservées, avec le membre et l'état,\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n");
        }
        Execution e(prog);
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 15000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto table = [&]() -> QTableWidget * {
            for (QTableWidget *x : e.findChildren<QTableWidget *>()) if (x->isVisible() && x->rowCount() == 3) return x;
            return nullptr;
        };
        VERIFIER(attendre([&] { return table() != nullptr; }));
        if (QTableWidget *t = table()) {
            QTableWidgetItem *payee = nullptr, *annulee = nullptr;
            for (int r = 0; r < 3; r++) {
                if (t->item(r, 1) && t->item(r, 1)->text() == QString::fromUtf8("payée")) payee = t->item(r, 1);
                if (t->item(r, 1) && t->item(r, 1)->text() == QString::fromUtf8("annulée")) annulee = t->item(r, 1);
            }
            VERIFIER(payee && payee->background().color() == QColor("#d7f0dd") && payee->foreground().color() == QColor("#1e5b2e"));
            VERIFIER(annulee && annulee->background().style() == Qt::NoBrush);   // sans couleur : une cellule ordinaire
        }
        for (QPushButton *b : e.findChildren<QPushButton *>())   // l'application se ferme : la machine finit d'elle-même
            if (b->text() == "Fermer" && b->isVisible() && b->isEnabled()) { b->click(); break; }
        QElapsedTimer z;   // le temps que la machine finisse ; l'écran, lui, reste affiché
        z.start();
        while (z.elapsed() < 500) QApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    // Le thème libre (§ 22.5, 2.13) dans la fenêtre d'exécution : les jetons dérivés, le clair imposé par le fond, la
    // police installée, le logo SVG en icône ; puis le thème de GrymoiR rétabli pour la suite des essais
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write(QString("Les écrans ont la couleur « #46433B », le fond « #E8E2D6 », la police « %1 » et le logo « %2 ».\n"
                            "L'écran x montre :\n    un bouton « Fermer ».\n"
                            "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n")
                        .arg(QString(GRYM_SOURCE) + "/vendor/atkinson/AtkinsonHyperlegibleNext-Regular.ttf",
                             QString(GRYM_SOURCE) + "/vendor/lucide/check.svg").toUtf8());
        }
        Execution e(prog);
        e.show();
        e.demarrer();
        QElapsedTimer z;
        z.start();
        while (z.elapsed() < 15000 && Theme::courant().accent() != "libre") QApplication::processEvents(QEventLoop::AllEvents, 20);
        Theme &th = Theme::courant();
        VERIFIER(th.accent() == "libre" && th.mode() == Theme::Clair);
        VERIFIER(th.hex("fond") == "#e8e2d6" && th.hex("accent") == "#46433b" && th.hex("accent-doux") == "#cec9bd");
        VERIFIER(th.hex("sur-accent") == "#ffffff");
        VERIFIER(th.police_interface().family().contains("Atkinson"));
        VERIFIER(!e.windowIcon().isNull());
        for (QPushButton *b : e.findChildren<QPushButton *>())
            if (b->text() == "Fermer" && b->isVisible() && b->isEnabled()) { b->click(); break; }
        z.restart();
        while (z.elapsed() < 500) QApplication::processEvents(QEventLoop::AllEvents, 20);
        th.personnaliser({}, {}, false, {});
        th.appliquer(Theme::Clair, "bleue");
        VERIFIER(th.accent() == "bleue" && th.hex("fond") == "#f5efe6");
    }

    // Un écran occupe toute la fenêtre : le fil et la zone des questions se cachent, Ctrl+J les montre (docs/atelier.md,
    // § 8) ; le contenu reste en haut, dans sa largeur de lecture, sans flotter au milieu
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    un bouton « Fermer ».\n"
                    "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n");
        }
        Execution e(prog);
        e.resize(1600, 900);
        e.show();
        e.demarrer();
        QPushButton *fermer = nullptr;
        QElapsedTimer z;
        z.start();
        while (z.elapsed() < 15000 && !fermer) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            for (QPushButton *b : e.findChildren<QPushButton *>())
                if (b->text() == "Fermer" && b->isVisible() && b->isEnabled()) fermer = b;
        }
        VERIFIER(fermer != nullptr);
        QTextBrowser *fil = e.findChild<QTextBrowser *>();
        VERIFIER(fil && !fil->isVisible());
        auto ctrl_j = [&e] {
            for (QAction *a : e.actions()) if (a->shortcut() == QKeySequence("Ctrl+J")) a->trigger();
        };
        ctrl_j();
        QApplication::processEvents();
        VERIFIER(fil && fil->isVisible());
        ctrl_j();
        QApplication::processEvents();
        VERIFIER(fil && !fil->isVisible());
        if (fermer) {
            const QPoint haut = fermer->mapTo(&e, QPoint(0, 0));
            VERIFIER(haut.y() < 200);              // en haut, pas au milieu des 900 pixels
            VERIFIER(haut.x() + fermer->width() < 1600 - (1600 - 1120) / 2 + 2);   // dans la largeur de lecture, centrée
            fermer->click();
        }
        z.restart();
        while (z.elapsed() < 500) QApplication::processEvents(QEventLoop::AllEvents, 20);
        VERIFIER(fil && fil->isVisible());     // le programme fini : le fil revient, avec son dernier mot
    }

    // La fiche (§ 22.3) : le tableau des séances et sa ligne de total, en gras, jamais sélectionnable, la dernière même
    // après un tri choisi d'un clic sur un titre ; un double-clic dessus ne mène nulle part
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Un client, conservé, a :\n    un nom (texte), unique.\n"
                    "Une séance, conservée, a :\n    un client (client),\n    un prix (nombre).\n"
                    "Le c vaut un nouveau client :\n    Le nom vaut « Anne ».\nConserver c.\n"
                    "Le a vaut une nouvelle séance :\n    Le client vaut c.\n    Le prix vaut 130.\nConserver a.\n"
                    "Le b vaut une nouvelle séance :\n    Le client vaut c.\n    Le prix vaut 70.\nConserver b.\n"
                    "Ouvrir la fiche de c.\n");
        }
        Execution e(prog);
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 15000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto table = [&]() -> QTableWidget * {
            for (QTableWidget *x : e.findChildren<QTableWidget *>()) if (x->isVisible() && x->rowCount() == 3) return x;
            return nullptr;
        };
        VERIFIER(attendre([&] { return table() != nullptr; }));
        if (QTableWidget *t = table()) {
            auto derniere = [&] { return t->item(2, 0) ? t->item(2, 0)->text() : QString(); };
            VERIFIER(derniere() == "200" && t->item(2, 0)->font().bold());
            VERIFIER(!(t->item(2, 0)->flags() & Qt::ItemIsSelectable));
            for (int fois = 0; fois < 2; fois++) {   // croissant, puis décroissant : 70, 130, 200 puis 130, 70, 200
                emit t->horizontalHeader()->sectionClicked(0);
                QApplication::processEvents();
                VERIFIER(derniere() == "200");
                VERIFIER(t->item(fois ? 1 : 0, 0) && t->item(fois ? 1 : 0, 0)->text() == "70");
            }
            emit t->cellActivated(2, 0);   // la ligne de total : aucune fiche ne s'ouvre par-dessus
            QApplication::processEvents(QEventLoop::AllEvents, 300);
            VERIFIER(table() == t && t->isVisible());
        }
        for (QPushButton *b : e.findChildren<QPushButton *>())   // la fiche se ferme : la machine finit d'elle-même
            if (b->text() == "Fermer" && b->isVisible() && b->isEnabled()) { b->click(); break; }
        QElapsedTimer z;
        z.start();
        while (z.elapsed() < 500) QApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    // L'essai de migration d'un programme à écrans (A2-c) : il n'exécute rien, les écrans ne l'empêchent pas
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        QFile f(prog);
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write("Un membre, conservé, a :\n    un nom (texte).\nL'écran x montre :\n    un bouton « OK ».\n"
                "Quand on clique sur « OK » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n");
        f.close();
        bool refusee = true;
        const QString r = apercu_migration(prog, &refusee);
        VERIFIER(!refusee);
        if (refusee) std::printf("refus : %s\n", qPrintable(r));
    }

    // L'identité d'une application (docs/atelier.md, § 8) : gardée dans le projet, ses valeurs par défaut, son
    // identifiant ; l'icône Windows ; application.txt la porte
    {
        QTemporaryDir dossier;
        const QString projet = dossier.filePath(QString::fromUtf8("Élodie & Cie"));
        VERIFIER(QDir().mkpath(projet));
        {
            QFile f(projet + "/a.grym");
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Afficher 1.\n");
        }
        Identite id = identite_du_projet(projet);   // rien de dit : le dossier, 1.0, sans éditeur
        VERIFIER(id.nom == QString::fromUtf8("Élodie & Cie") && id.version == "1.0" && id.editeur.isEmpty() && id.ident == "elodie-cie");
        FichierProjet fp;
        fp.lire(projet);
        fp.programme_principal = "a.grym";
        fp.nom_application = QString::fromUtf8("Cotisations Élodie");
        fp.version = "2.3.1";
        fp.editeur = QString::fromUtf8("Alain Thiémard");
        VERIFIER(fp.ecrire());
        FichierProjet relu;
        relu.lire(projet);
        VERIFIER(relu.nom_application == QString::fromUtf8("Cotisations Élodie") && relu.version == "2.3.1"
                 && relu.editeur == QString::fromUtf8("Alain Thiémard") && relu.programme_principal == "a.grym");
        id = identite_du_projet(projet);
        VERIFIER(id.nom == QString::fromUtf8("Cotisations Élodie") && id.ident == "cotisations-elodie");
        VERIFIER(identite_refusee("A", "1.0").isEmpty() && identite_refusee("A", "2.3.1").isEmpty());
        VERIFIER(!identite_refusee("", "1.0").isEmpty() && !identite_refusee("A", "v1").isEmpty()
                 && !identite_refusee("A/B", "1.0").isEmpty() && !identite_refusee("A", "1.0.0.0.0").isEmpty());
        // l'icône Windows : un en-tête ICO, puis une image PNG de 256 pixels
        const QString ico = dossier.filePath("i.ico");
        VERIFIER(ecrire_ico(icone_du_projet(projet), ico));
        QFile fi(ico);
        VERIFIER(fi.open(QIODevice::ReadOnly));
        const QByteArray b = fi.readAll();
        VERIFIER(b.size() > 22 && b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0 && b[4] == 1);
        QImage png;
        VERIFIER(png.loadFromData(b.mid(22), "PNG") && png.width() == 256);
        // application.txt : le nom, le programme, la version, l'éditeur
        QString erreur;
        const QString vers = dossier.filePath("Programme");
        VERIFIER(preparer_programme(projet, "a.grym", id.nom, vers, &erreur));
        QFile at(vers + "/application.txt");
        VERIFIER(at.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(at.readAll());
        VERIFIER(t.contains(QString::fromUtf8("nom : Cotisations Élodie")) && t.contains("version : 2.3.1")
                 && t.contains(QString::fromUtf8("éditeur : Alain Thiémard")));
        VERIFIER(!QFileInfo::exists(vers + "/projet.grymatelier"));   // la disposition de l'atelier reste dans le projet
    }

    // Le résumé des erreurs (§ 22.6), dans l'exécution : deux zones obligatoires vides, chacune son message sous son libellé
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    un nom (texte),\n    une ville (texte),\n    un bouton « OK ».\n"
                    "Quand on clique sur « OK » dans l'écran x :\n    Afficher le nom de l'écran puis la ville de l'écran.\n"
                    "    Fermer l'écran.\nOuvrir l'écran x.\n");
        }
        Execution e(prog);
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 15000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto ok = [&]() -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == "OK" && b->isVisible() && b->isEnabled()) return b;
            return nullptr;
        };
        VERIFIER(attendre([&] { return ok() != nullptr; }));
        if (ok()) ok()->click();
        auto fautes = [&]() {
            QStringList t;
            for (QLabel *l : e.findChildren<QLabel *>("erreur-zone")) t << l->text();
            return t;
        };
        VERIFIER(attendre([&] { return fautes().size() == 2; }));
        VERIFIER(fautes().contains(QString::fromUtf8("Remplis « Nom » : cette zone est obligatoire."))
                 && fautes().contains(QString::fromUtf8("Remplis « Ville » : cette zone est obligatoire.")));

    }

    // Le guide, chapitre 11 : l'application de l'association, avec ses écrans, pilotée comme le ferait le lecteur
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("association.grym");
        VERIFIER(QFile::copy(QString(GRYM_SOURCE) + "/docs/guide/chapitre-11.grym", prog));
        Execution e(prog);
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 15000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto bouton = [&](const QString &t) -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == t && b->isVisible()) return b;
            return nullptr;
        };
        auto pret = [&](const QString &t) { return attendre([&] { return bouton(t) && bouton(t)->isEnabled(); }); };
        auto table = [&]() -> QTableWidget * {
            for (QTableWidget *x : e.findChildren<QTableWidget *>()) if (x->isVisible()) return x;
            return nullptr;
        };
        auto sortie = [&]() { return e.findChild<QTextBrowser *>()->toPlainText(); };
        VERIFIER(pret("Membres"));
        // les membres, avec la catégorie lue à travers le lien
        bouton("Membres")->click();
        VERIFIER(pret("Nouveau membre") && attendre([&] { return table() && table()->rowCount() == 3; }));
        VERIFIER(table()->item(0, 2)->text() == "Bapst" && table()->item(0, 3)->text() == "Actif");
        bouton("Fermer")->click();
        // la cotisation impayée d'Élodie, encaissée d'un clic ; la liste se vide
        VERIFIER(pret("Cotisations impayées"));
        bouton("Cotisations impayées")->click();
        VERIFIER(pret("Encaisser en caisse") && attendre([&] { return table() && table()->rowCount() == 1; }));
        VERIFIER(table()->item(0, 2)->text() == "Dupasquier" && table()->item(0, 0)->text() == "28.02.2026");
        bouton("Encaisser en caisse")->click();   // sans ligne choisie : refusé, l'écran le dit
        VERIFIER(attendre([&] {
            for (QLabel *l : e.findChildren<QLabel *>()) if (l->isVisible() && l->text().contains("Choisis d'abord")) return true;
            return false;
        }));
        table()->selectRow(0);
        bouton("Encaisser en caisse")->click();
        VERIFIER(attendre([&] { return table()->rowCount() == 0 && bouton("Fermer") && bouton("Fermer")->isEnabled(); }));
        bouton("Fermer")->click();
        // la balance et les comptes annuels, imprimés dans le panneau ; toujours équilibrés
        VERIFIER(pret("Balance"));
        bouton("Balance")->click();
        VERIFIER(attendre([&] { return sortie().contains("La balance est équilibrée."); }));
        VERIFIER(sortie().contains("Totaux                              3'612,00   3'612,00"));
        bouton("Comptes annuels")->click();
        VERIFIER(attendre([&] { return sortie().contains("Le bilan est équilibré."); }));
        VERIFIER(sortie().contains("Perte                                 412,00"));
        // noter le loyer de juillet par l'écran de saisie : zones de texte, date, nombre, et deux menus de comptes
        bouton("Noter une écriture")->click();
        VERIFIER(pret("Noter"));
        QList<QWidget *> zones;   // dans l'ordre de l'écran : pièce, date, libellé, débité, crédité, montant
        for (QWidget *w : e.findChildren<QWidget *>())
            if (w->isVisible() && (qobject_cast<QLineEdit *>(w) || qobject_cast<QComboBox *>(w))) zones << w;
        std::sort(zones.begin(), zones.end(), [](QWidget *a, QWidget *b) {
            return a->mapToGlobal(QPoint(0, 0)).y() < b->mapToGlobal(QPoint(0, 0)).y();
        });
        VERIFIER(zones.size() == 6);
        const QStringList valeurs = {"P-006", "15.07.2026", "Loyer de juillet", "Loyer du local", "Banque", "300,00"};
        for (int k = 0; k < zones.size() && k < valeurs.size(); k++) {
            if (auto *l = qobject_cast<QLineEdit *>(zones[k])) {
                l->setText(valeurs[k]);
                l->setModified(true);
                emit l->editingFinished();
            } else if (auto *m = qobject_cast<QComboBox *>(zones[k])) {
                VERIFIER(m->findText(valeurs[k]) > 0);   // le menu des comptes, par leur intitulé (champ texte unique, § 19)
                m->setCurrentIndex(m->findText(valeurs[k]));
                emit m->activated(m->currentIndex());
            }
            VERIFIER(pret("Noter"));
        }
        bouton("Noter")->click();
        if (!pret("Balance")) { e.close(); VERIFIER(false); return 1; }
        bouton("Balance")->click();
        VERIFIER(attendre([&] { return sortie().contains("Totaux                              3'912,00   3'912,00"); }));
        bouton("Fermer")->click();
        VERIFIER(attendre([&] { return !bouton("Membres"); }));
        e.close();
    }

    // Le guide, chapitre 12 : l'application en trois fichiers, verte, avec son logo
    {
        QTemporaryDir dossier;
        for (const char *f : {"association.grym", "données.grym", "opérations.grym", "tilleul.png"})
            VERIFIER(QFile::copy(QString(GRYM_SOURCE) + "/docs/guide/chapitre-12/" + QString::fromUtf8(f),
                                 dossier.filePath(QString::fromUtf8(f))));
        Execution e(dossier.filePath("association.grym"));
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 15000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto bouton = [&](const QString &t) -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == t && b->isVisible()) return b;
            return nullptr;
        };
        VERIFIER(attendre([&] { return bouton("Balance") && bouton("Balance")->isEnabled(); }));
        VERIFIER(Theme::courant().accent() == "verte" && !e.windowIcon().isNull());
        int logo = 0;
        for (QLabel *l : e.findChildren<QLabel *>())
            if (l->isVisible() && !l->pixmap().isNull()) logo = qRound(l->pixmap().height() / l->pixmap().devicePixelRatio());
        VERIFIER(logo == 64);
        bouton("Balance")->click();
        VERIFIER(attendre([&] { return e.findChild<QTextBrowser *>()->toPlainText().contains("La balance est équilibrée."); }));
        VERIFIER(e.findChild<QTextBrowser *>()->toPlainText().contains("Totaux                              3'572,00   3'572,00"));
        bouton("Fermer")->click();
        attendre([&] { return !bouton("Balance"); });
        e.close();
        Theme::courant().appliquer(Theme::Clair, "bleue");
    }

    // Contrainte d'entité (grammaire, § 16.14) dans un événement : seul l'événement est annulé, l'écran le dit
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("stock.grym");
        QFile f(prog);
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write("Un stock, conservé, a :\n    un nom (texte), unique,\n    une quantité (nombre).\n"
                "Chaque stock conservé vérifie :\n    la quantité du stock ≥ 0,\n    sinon « Stock négatif : » puis le nom du stock.\n"
                "Si le nombre de stocks conservés est nul :\n    Le s vaut un nouveau stock :\n        Le nom vaut « vis ».\n"
                "        La quantité vaut 1.\n    Conserver le s.\n"
                "L'écran de gestion montre :\n    la liste des stocks conservés,\n    un bouton « Retirer deux »,\n"
                "    un bouton « Ajouter »,\n    un bouton « Fermer ».\n"
                "Quand on clique sur « Retirer deux » dans l'écran de gestion :\n"
                "    Le s vaut le stock conservé dont le nom est « vis ».\n    La quantité du s devient la quantité du s − 2.\n"
                "Quand on clique sur « Ajouter » dans l'écran de gestion :\n"
                "    Le s vaut le stock conservé dont le nom est « vis ».\n    La quantité du s devient la quantité du s + 5.\n"
                "Quand on clique sur « Fermer » dans l'écran de gestion :\n    Fermer l'écran.\n"
                "Ouvrir l'écran de gestion.\n"
                "Afficher « Final : » puis la quantité du stock conservé dont le nom est « vis ».\n");
        f.close();
        Execution e(prog);
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 15000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto bouton = [&](const QString &t) -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == t && b->isVisible()) return b;
            return nullptr;
        };
        auto pret = [&](const QString &t) { return attendre([&] { return bouton(t) && bouton(t)->isEnabled(); }); };
        auto message = [&]() {
            for (QLabel *l : e.findChildren<QLabel *>()) if (l->isVisible() && l->text().contains("Stock négatif")) return l->text();
            return QString();
        };
        VERIFIER(pret("Retirer deux"));
        bouton("Retirer deux")->click();   // 1 − 2 : la règle refuse, l'événement est annulé, l'écran reste
        VERIFIER(attendre([&] { return message().contains("Stock négatif : vis"); }) && pret("Ajouter"));
        bouton("Ajouter")->click();        // 1 + 5 = 6 : passe
        VERIFIER(pret("Retirer deux"));
        bouton("Retirer deux")->click();   // 6 − 2 = 4 : passe
        VERIFIER(pret("Fermer"));
        bouton("Fermer")->click();
        VERIFIER(attendre([&] { return e.findChild<QTextBrowser *>()->toPlainText().contains("Final : 4"); }));
        e.close();
    }

    // Premier lancement d'une installation : l'exemple recopié, jamais écrasé
    {
        QTemporaryDir d;
        const QString source = d.filePath("Exemples/Amis du Tilleul");
        QDir().mkpath(source);
        QFile a(source + "/association.grym");
        VERIFIER(a.open(QIODevice::WriteOnly));
        a.write("Afficher 1.\n");
        a.close();
        QString erreur;
        const QString copie = installer_exemple(source, d.filePath("Documents/GrymoiR"), &erreur);
        VERIFIER(copie == d.filePath("Documents/GrymoiR/Amis du Tilleul") && QFile::exists(copie + "/association.grym"));
        QFile b(copie + "/association.grymd");   // les données du lecteur
        VERIFIER(b.open(QIODevice::WriteOnly));
        b.write("x");
        b.close();
        VERIFIER(installer_exemple(source, d.filePath("Documents/GrymoiR"), &erreur) == copie);
        VERIFIER(QFile::exists(copie + "/association.grymd"));   // rien d'écrasé
        VERIFIER(installer_exemple(d.filePath("rien"), d.filePath("Documents"), &erreur).isEmpty() && erreur.contains("introuvable"));
    }

    // Fabriquer une application : le programme recopié sans les données ni les fichiers de l'atelier
    {
        QTemporaryDir d;
        const QString p = d.filePath("Amis du Tilleul");
        QDir().mkpath(p + "/images");
        for (const char *n : {"association.grym", "données.grym", "association.grymd", "association.grymb",
                              "projet.grymatelier", ".cache", "images/logo.png"}) {
            QFile f(p + "/" + QString::fromUtf8(n));
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        QString erreur;
        const QString vers = d.filePath("app/Programme");
        VERIFIER(preparer_programme(p, "association.grym", "Amis du Tilleul", vers, &erreur));
        VERIFIER(QFile::exists(vers + "/association.grym") && QFile::exists(vers + "/données.grym")
                 && QFile::exists(vers + "/images/logo.png"));
        VERIFIER(!QFile::exists(vers + "/association.grymd") && !QFile::exists(vers + "/association.grymb")
                 && !QFile::exists(vers + "/projet.grymatelier") && !QFile::exists(vers + "/.cache"));
        QFile a(vers + "/application.txt");
        VERIFIER(a.open(QIODevice::ReadOnly));
        const QString texte = QString::fromUtf8(a.readAll());
        VERIFIER(texte.contains("nom : Amis du Tilleul\n") && texte.contains("programme : association.grym\n"));
        VERIFIER(!preparer_programme(p, "absent.grym", "X", d.filePath("autre"), &erreur) && erreur.contains("introuvable"));
    }

    // Icônes : celle de l'atelier est embarquée ; une application prend la première image PNG de son projet,
    // sinon celle de GrymoiR ; le .icns a ses dix tailles, chacune une image PNG, dans la grille de macOS
    {
        VERIFIER(QImage(":/application/icone/grymoir-atelier.png").size() == QSize(256, 256));
        QTemporaryDir d;
        VERIFIER(icone_du_projet(d.path()).size() == QSize(256, 256));                  // icône par défaut
        QImage logo(100, 50, QImage::Format_ARGB32);                                     // un logo plus large que haut
        logo.fill(QColor("#1B7340"));
        VERIFIER(logo.save(d.filePath("b-logo.png")));
        QFile faux(d.filePath("a-faux.png"));                                            // illisible : ignoré
        VERIFIER(faux.open(QIODevice::WriteOnly));
        faux.write("pas une image");
        faux.close();
        const QImage choisie = icone_du_projet(d.path());
        VERIFIER(choisie.size() == QSize(100, 50));
        const QString icns = d.filePath("x.icns");
        VERIFIER(ecrire_icns(choisie, icns));
        QFile f(icns);
        VERIFIER(f.open(QIODevice::ReadOnly));
        const QByteArray o = f.readAll();
        auto u32 = [&](int i) {
            return (quint32(quint8(o[i])) << 24) | (quint32(quint8(o[i + 1])) << 16) | (quint32(quint8(o[i + 2])) << 8)
                   | quint32(quint8(o[i + 3]));
        };
        VERIFIER(o.left(4) == "icns" && u32(4) == quint32(o.size()));
        QStringList vus;
        int i = 8;
        bool tailles = true;
        while (i + 8 <= o.size()) {
            const quint32 n = u32(i + 4);
            const QImage e = QImage::fromData(o.mid(i + 8, int(n) - 8), "PNG");
            const QString type = QString::fromLatin1(o.mid(i, 4));
            const int attendu = QMap<QString, int>{{"icp4", 16}, {"icp5", 32}, {"ic11", 32}, {"ic12", 64},
                {"ic07", 128}, {"ic13", 256}, {"ic08", 256}, {"ic14", 512}, {"ic09", 512}, {"ic10", 1024}}.value(type);
            if (e.width() != attendu || e.height() != attendu) tailles = false;
            if (type == "ic10") {
                // 824 pixels de large, centré : marges transparentes, couleur du logo au centre
                VERIFIER(qAlpha(e.pixel(99, 512)) == 0 && qAlpha(e.pixel(925, 512)) == 0);
                VERIFIER(qAlpha(e.pixel(512, 300)) == 0 && e.pixelColor(512, 512) == QColor("#1B7340"));
            }
            vus << type;
            i += int(n);
        }
        VERIFIER(i == o.size() && tailles);
        VERIFIER(vus.join(' ') == "icp4 icp5 ic11 ic12 ic07 ic13 ic08 ic14 ic09 ic10");
        VERIFIER(!ecrire_icns(QImage(), d.filePath("vide.icns")));
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

    // Aide à la saisie : les suites du cœur au curseur, le début de mot remplacé, à la frappe comme sur demande
    {
        Editeur a;
        a.resize(600, 300);
        a.show();
        auto au_bout = [&a](const QString &texte) {
            a.charger(texte, false);
            QTextCursor c = a.textCursor();
            c.movePosition(QTextCursor::End);
            a.setTextCursor(c);
        };
        auto taper = [&a](const QString &texte) {
            for (const QChar ch : texte) {
                QKeyEvent e(QEvent::KeyPress, ch == ' ' ? Qt::Key_Space : ch == '"' ? Qt::Key_QuoteDbl : Qt::Key_A,
                            Qt::NoModifier, QString(ch));
                QApplication::sendEvent(&a, &e);
                QCoreApplication::processEvents();
            }
        };
        au_bout("Le prix unitaire vaut 3.\nAfficher pr");
        VERIFIER(a.debut_de_mot() == "pr");
        VERIFIER(a.suites_au_curseur() == QStringList({"prix unitaire"}));
        a.completer("prix unitaire");
        VERIFIER(a.toPlainText().endsWith("Afficher prix unitaire"));
        // un mot qui en prolonge un autre : rien à remplacer
        au_bout("Le prix unitaire vaut 3.\nAfficher prix ");
        VERIFIER(a.debut_de_mot().isEmpty() && a.suites_au_curseur().contains("unitaire"));
        a.completer("unitaire");
        VERIFIER(a.toPlainText().endsWith("Afficher prix unitaire"));
        // après une élision, et dans un nom entre crochets
        au_bout("L'addition vaut 3.\nL'a");
        VERIFIER(a.debut_de_mot() == "a" && a.suites_au_curseur().contains("addition"));
        au_bout("Le [frais et port] vaut 3.\nAfficher [fr");
        VERIFIER(a.debut_de_mot() == "[fr");
        a.completer("[frais et port]");
        VERIFIER(a.toPlainText().endsWith("Afficher [frais et port]"));
        // la casse ne compte pas ; les catégories et les gabarits ne sont pas proposés
        au_bout("Le x vaut 3.\naff");
        VERIFIER(a.suites_au_curseur() == QStringList({"Afficher"}));
        au_bout("Le x vaut 3.\nAfficher ");
        VERIFIER(a.suites_au_curseur() == QStringList({"x", "(", "−", "vrai", "faux"}));   // sans « (nombre) » ni « « … » »
        // à la frappe : la liste s'ouvre dès deux lettres ; Entrée écrit la première suite
        au_bout("Le x vaut 3.\nSi x est ");
        taper("su");
        VERIFIER(a.completion()->popup()->isVisible());
        QKeyEvent entree(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&a, &entree);
        VERIFIER(!a.completion()->popup()->isVisible());
        VERIFIER(a.toPlainText().endsWith("Si x est supérieur à"));
        // Échap ferme la liste sans rien écrire ; une seule lettre ne l'ouvre pas
        au_bout("Le x vaut 3.\n");
        taper("A");
        VERIFIER(!a.completion()->popup()->isVisible());
        taper("f");
        VERIFIER(a.completion()->popup()->isVisible());
        QKeyEvent echap(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&a, &echap);
        VERIFIER(!a.completion()->popup()->isVisible() && a.toPlainText().endsWith("\nAf"));
        // sur demande, même sans début de mot
        au_bout("Le x vaut 3.\n");
        a.proposer(true);
        VERIFIER(a.completion()->popup()->isVisible());
        VERIFIER(a.completion()->popup()->model()->rowCount() > 5);
        a.completion()->popup()->hide();
        // par la méthode de saisie du système (le chemin de macOS), sans événement de touche
        au_bout("Le x vaut 3.\nSi x est ");
        QInputMethodEvent saisie;
        saisie.setCommitString("s");
        QApplication::sendEvent(&a, &saisie);
        QCoreApplication::processEvents();
        QInputMethodEvent saisie2;
        saisie2.setCommitString("u");
        QApplication::sendEvent(&a, &saisie2);
        QCoreApplication::processEvents();
        VERIFIER(a.completion()->popup()->isVisible());
        a.completion()->popup()->hide();
        // guillemets : « " » ouvre « «  » », curseur au milieu ; devant « » », il le franchit
        au_bout("Afficher ");
        taper("\"");
        VERIFIER(a.toPlainText() == "Afficher «  »" && a.textCursor().position() == 11);
        taper("Bonjour");
        a.completion()->popup()->hide();
        taper("\"");
        VERIFIER(a.toPlainText() == "Afficher « Bonjour »" && a.textCursor().position() == a.toPlainText().size());
        // dans un texte déjà ouvert, ou pour fermer un texte entre « " », il reste « " »
        au_bout("Afficher « a");
        taper("\"");
        VERIFIER(a.toPlainText() == "Afficher « a\"");
        au_bout("Afficher \"a");
        taper("\"");
        VERIFIER(a.toPlainText() == "Afficher \"a\"");
        // Ctrl+Z rend le « " » tapé
        au_bout("Afficher ");
        taper("\"");
        a.undo();
        VERIFIER(a.toPlainText() == "Afficher \"");
        // le même « " », arrivé par la méthode de saisie du système
        au_bout("Afficher ");
        QInputMethodEvent saisie3;
        saisie3.setCommitString("\"");
        QApplication::sendEvent(&a, &saisie3);
        QCoreApplication::processEvents();
        VERIFIER(a.toPlainText() == "Afficher «  »" && a.textCursor().position() == 11);
        // un « " » collé avec d'autres caractères n'est pas touché
        au_bout("Afficher ");
        a.insertPlainText("\"a\"");
        QCoreApplication::processEvents();
        VERIFIER(a.toPlainText() == "Afficher \"a\"");
        // dans un texte, rien ; en forme compacte, rien (le calcul ne la couvre pas encore)
        au_bout("Le x vaut 3.\nAfficher « bon");
        VERIFIER(a.suites_au_curseur().isEmpty());
        a.charger("_le x << 3\n_af", true);
        VERIFIER(a.suites_au_curseur().isEmpty());
    }

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
            VERIFIER(f.open(QIODevice::WriteOnly));
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
        VERIFIER(f.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(f.readAll());
        f.close();
        VERIFIER(t == "Remarque : disposition de l'atelier ; sans effet sur le programme.\n"
                      "programme principal : prog.grym\ncompositeur : 40, 120\nœuvre : 320, 120\n");
        VERIFIER(f.open(QIODevice::Append));
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
            VERIFIER(f.open(QIODevice::WriteOnly));
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
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write(texte.toUtf8());
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            VERIFIER(f.open(QIODevice::ReadOnly));
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
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write(texte.toUtf8());
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            VERIFIER(f.open(QIODevice::ReadOnly));
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

    // Valeurs permises (§ 16.15) et heure (§ 14.7) dans l'éditeur de données : lues, écrites, jamais perdues
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &texte) {
            QFile f(dossier.filePath("d.grym"));
            VERIFIER(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(texte.toUtf8());
        };
        auto lire = [&] {
            QFile f(dossier.filePath("d.grym"));
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        ecrire("Une tâche, conservée, a :\n    un titre (texte), unique,\n"
               "    un état (« à faire », « en cours » ou « fini »), « à faire » au départ,\n"
               "    un début (heure), facultatif.\n");
        const auto lu = lire_schema(dossier.path()).value(0);
        VERIFIER(lu.champs.value(1).valeurs == QStringList({"à faire", "en cours", "fini"}));
        VERIFIER(lu.champs.value(2).type == "heure" && lu.champs.value(2).valeurs.isEmpty());
        // le panneau montre les valeurs et l'heure, et les rend telles quelles : « Appliquer » ne retire rien
        PanneauEntite p;
        p.montrer(&lu, {"texte", "nombre", "date"});   // « heure » absent de la liste : le panneau l'ajoute
        VERIFIER(p.champ_de_la_ligne(1).valeurs == QStringList({"à faire", "en cours", "fini"}));
        VERIFIER(p.champ_de_la_ligne(2).type == "heure");
        Geste g;
        VERIFIER(modifier_champ(dossier.path(), "tâche", "état", p.champ_de_la_ligne(1), &g).isEmpty());
        VERIFIER(lire().contains("    un état (« à faire », « en cours » ou « fini »), « à faire » au départ,\n"));
        ChampVoulu etat = p.champ_de_la_ligne(1);
        etat.valeurs << "abandonné";
        VERIFIER(modifier_champ(dossier.path(), "tâche", "état", etat, &g).isEmpty());
        VERIFIER(lire().contains("un état (« à faire », « en cours », « fini » ou « abandonné »)"));
        ChampVoulu priorite;
        priorite.nom = "priorité";
        priorite.type = "texte";
        priorite.feminin = true;
        priorite.valeurs = {"basse", "haute"};
        priorite.depart = "basse";
        VERIFIER(ajouter_champ(dossier.path(), "tâche", priorite, &g).isEmpty());
        VERIFIER(lire().contains("    une priorité (« basse » ou « haute »), « basse » au départ.\n"));
        priorite.nom = "urgence";
        priorite.valeurs = {"basse"};
        VERIFIER(ajouter_champ(dossier.path(), "tâche", priorite, &g).startsWith("Au moins deux valeurs permises"));
        priorite.valeurs = {"basse", "basse"};
        VERIFIER(ajouter_champ(dossier.path(), "tâche", priorite, &g) == "« basse » est déjà dans la liste.");
        priorite.valeurs = {"basse", "haute"};
        priorite.depart = "moyenne";
        VERIFIER(ajouter_champ(dossier.path(), "tâche", priorite, &g).contains("la valeur de départ doit en être une"));
        priorite.depart.clear();
        priorite.type = "nombre";
        VERIFIER(ajouter_champ(dossier.path(), "tâche", priorite, &g).startsWith("Des valeurs permises ne vont qu'à"));
    }

    // Contenu de la base (§ 5 quinquies) : les objets, leurs valeurs écrites comme GrymoiR, liens, corbeille
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        Contenu c;
        VERIFIER(lire_contenu(base_du_programme(prog), "compositeur", {}, false, &c).startsWith("Pas encore de base « p.grymd »"));
        const QString source =
            "Un genre, conservé, a : un nom (texte), unique.\n"
            "Un compositeur, conservé, a :\n    un nom (texte), unique,\n    une naissance (date),\n"
            "    un cachet (nombre), facultatif,\n    un vivant (vrai ou faux),\n    un état (« actif » ou « retiré »),\n"
            "    des genres (genre),\n    un maître (compositeur), facultatif.\n"
            "Le g vaut un nouveau genre :\n    Le nom vaut « baroque ».\nConserver g.\n"
            "Le b vaut un nouveau compositeur :\n    Le nom vaut « Bach ».\n    La naissance vaut 31.03.1685.\n"
            "    Le cachet vaut -12,50.\n    Le vivant vaut faux.\n    L'état vaut « retiré ».\nConserver b.\n"
            "Les genres de b gagnent g.\n"
            "Le w vaut un nouveau compositeur :\n    Le nom vaut « Wagner ».\n    La naissance vaut 22.05.1813.\n"
            "    Le vivant vaut faux.\n    L'état vaut « actif ».\n    Le maître vaut b.\nConserver w.\n"
            "Supprimer w.\n";
        QFile f(prog);
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write(source.toUtf8());
        f.close();
        const QByteArray src = source.toUtf8(), base = base_du_programme(prog).toUtf8();
        Portee *po = portee_creer();
        Programme pr = {};
        Diagnostic d = {};
        VERIFIER(analyser(src.constData(), (size_t)src.size(), po, 0, &pr, &d));
        if (d.message) std::fprintf(stderr, "%s\n", d.message);
        Module *mo = compiler(&pr, &d);
        Machine *m = machine_creer();
        machine_base(m, base.constData());
        Chaine so = {};
        machine_executer(m, mo, &so, &d);
        if (d.message) std::fprintf(stderr, "%s\n", d.message);
        free(so.d);
        machine_detruire(m);
        module_detruire(mo);
        programme_liberer(&pr);
        portee_detruire(po);
        const auto schema = lire_schema(dossier.path());
        VERIFIER(lire_contenu(base_du_programme(prog), "compositeur", schema, false, &c).isEmpty());
        VERIFIER(c.colonnes == QStringList({"N°", "Nom", "Naissance", "Cachet", "Vivant", "État", "Maître", "Genres (nombre)"}));
        VERIFIER(c.lignes.size() == 1 && c.dans_la_corbeille == 1);
        VERIFIER(c.lignes.value(0).mid(1) == QStringList({"Bach", "31.03.1685", QString::fromUtf8("−12,50"), "faux", "retiré", "", "1"}));
        VERIFIER(lire_contenu(base_du_programme(prog), "compositeur", schema, true, &c).isEmpty());
        VERIFIER(c.lignes.size() == 2 && c.lignes.value(1).value(1) == "Wagner" && c.lignes.value(1).value(6) == "Bach");
        VERIFIER(c.lignes.value(1).last() == "supprimé" && c.colonnes.last() == "Corbeille");
        VERIFIER(lire_contenu(base_du_programme(prog), "inconnue", schema, false, &c).contains("n'est pas une entité"));
    }

    // Contenu de la base, format 2 (docs/vm.md, § 8) : la taille d'un fichier se lit dans grym_fichier
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym"), doc = dossier.filePath("doc.txt");
        {
            QFile f(doc);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("douze octets");
        }
        const QString source = "Une partition, conservée, a : un titre (texte), unique, un scan (fichier).\n"
                               "La p vaut une nouvelle partition :\n    Le titre vaut « Toccata ».\n"
                               "    Le scan vaut le fichier « " + doc + " ».\nConserver p.\n";
        QFile f(prog);
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write(source.toUtf8());
        f.close();
        const QByteArray src = source.toUtf8(), base = base_du_programme(prog).toUtf8();
        Portee *po = portee_creer();
        Programme pr = {};
        Diagnostic d = {};
        VERIFIER(analyser(src.constData(), (size_t)src.size(), po, 0, &pr, &d));
        Module *mo = compiler(&pr, &d);
        Machine *m = machine_creer();
        machine_base(m, base.constData());
        Chaine so = {};
        VERIFIER(machine_executer(m, mo, &so, &d));
        free(so.d);
        machine_detruire(m);
        module_detruire(mo);
        programme_liberer(&pr);
        portee_detruire(po);
        Contenu c;
        VERIFIER(lire_contenu(base_du_programme(prog), "partition", lire_schema(dossier.path()), false, &c).isEmpty());
        VERIFIER(c.lignes.size() == 1 && c.lignes.value(0).value(2) == "doc.txt (12 octets)");
    }

    // Importer un tableau, sauvegarder et restaurer la base (§ 5 sexies)
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QByteArray &octets) {
            QFile f(dossier.filePath(nom));
            VERIFIER(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(octets);
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        auto lancer = [&](const QString &nom) {   // le programme, dans la machine, avec sa base
            const QByteArray src = lire(nom).toUtf8(), base = base_du_programme(dossier.filePath(nom)).toUtf8();
            const QByteArray dir = dossier.path().toUtf8();
            Portee *po = portee_creer();
            Programme pr = {};
            Diagnostic d = {};
            QString r;
            const QString ici = QDir::currentPath();
            QDir::setCurrent(dossier.path());   // le tableau se lit depuis le dossier du programme
            if (analyser(src.constData(), (size_t)src.size(), po, 0, &pr, &d)) {
                Module *mo = compiler(&pr, &d);
                Machine *m = machine_creer();
                machine_base(m, base.constData());
                Chaine so = {};
                machine_executer(m, mo, &so, &d);
                r = QString::fromUtf8(so.d ? so.d : "");
                free(so.d);
                machine_detruire(m);
                module_detruire(mo);
            }
            if (d.message) r += QString("ERREUR %1").arg(QString::fromUtf8(d.message));
            QDir::setCurrent(ici);
            programme_liberer(&pr);
            portee_detruire(po);
            return r;
        };
        // les titres : séparateur deviné, marque d'ordre, guillemets
        QString e;
        ecrire("a.csv", "\xEF\xBB\xBFTitre;\"Ann\xC3\xA9""e; de \"\"cr\xC3\xA9""ation\"\"\";Genre\r\nx;1;y\r\n");
        VERIFIER(titres_csv(dossier.filePath("a.csv"), &e) == QStringList({"Titre", "Année; de \"création\"", "Genre"}));
        ecrire("b.csv", "Titre,Compositeur\tx\n");
        VERIFIER(titres_csv(dossier.filePath("b.csv"), &e) == QStringList({"Titre", "Compositeur\tx"}));
        ecrire("c.csv", "\n\n");
        VERIFIER(titres_csv(dossier.filePath("c.csv"), &e).isEmpty() && e.startsWith("Le tableau est vide"));
        VERIFIER(colonne_pour("année", {"Titre", "ANNEE"}) == "ANNEE" && colonne_pour("genre", {"Titre"}).isEmpty());

        ecrire("o.csv", "Titre;Compositeur;Année;Genre\nMesse;Bach;1724;sacré\nRequiem;;;profane\n");
        ecrire("p.grym", "Un compositeur, conservé, a :\n    un nom (texte), unique.\n"
                         "Une œuvre, conservée, a :\n    un titre (texte), unique,\n    un compositeur (compositeur), facultatif,\n"
                         "    une année (année), facultative,\n    un genre (« sacré » ou « profane »),\n    des interprètes (compositeur).\n"
                         "Si le nombre de compositeurs conservés = 0 :\n    Le c vaut un nouveau compositeur :\n"
                         "        Le nom vaut « Bach ».\n    Conserver c.\n"
                         "Afficher le nombre d'œuvres conservées.\n");
        const auto schema = lire_schema(dossier.path());
        QStringList noms;
        for (const auto &c : champs_importables("œuvre", schema)) noms << c.nom;
        VERIFIER(noms == QStringList({"titre", "compositeur", "année", "genre"}));   // « plusieurs » écarté
        QMap<QString, QString> col = {{"titre", "Titre"}, {"compositeur", "Compositeur"}, {"année", "Année"}};
        VERIFIER(code_importation("œuvre", schema, "o.csv", col, &e).isEmpty() && e == "« genre » est obligatoire : choisis sa colonne.");
        col["genre"] = "Genre";
        e.clear();
        const QString code = code_importation("œuvre", schema, "o.csv", col, &e);
        VERIFIER(e.isEmpty());
        VERIFIER(code == "Remarque : importé de « o.csv » par l'atelier, tant que la base n'a aucune œuvre.\n"
                         "Si le nombre d'œuvres conservées = 0 :\n"
                         "    Pour chaque rangée du tableau « o.csv » :\n"
                         "        L'importé vaut une nouvelle œuvre :\n"
                         "            Le titre vaut la colonne « Titre » de la rangée.\n"
                         "            Le genre vaut la colonne « Genre » de la rangée.\n"
                         "        Si la colonne « Compositeur » de la rangée est présente :\n"
                         "            Le compositeur de l'importé devient le compositeur conservé dont le nom est la colonne « Compositeur » de la rangée.\n"
                         "        Si la colonne « Année » de la rangée est présente :\n"
                         "            L'année de l'importé devient la colonne « Année » de la rangée en année.\n"
                         "        Conserver l'importé.\n");
        Geste g;
        VERIFIER(ajouter_importation(dossier.path(), dossier.filePath("p.grym"), code, &g).isEmpty());
        VERIFIER(lire("p.grym").endsWith("    Conserver c.\n" + code + "\nAfficher le nombre d'œuvres conservées.\n"));
        VERIFIER(lancer("p.grym") == "2\n");
        VERIFIER(lancer("p.grym") == "2\n");   // la garde : pas de doublons au second lancement
        Contenu ct;
        VERIFIER(lire_contenu(base_du_programme(dossier.filePath("p.grym")), "œuvre", schema, false, &ct).isEmpty());
        VERIFIER(ct.lignes.size() == 2 && ct.lignes[0].mid(1, 4) == QStringList({"Messe", "Bach", "1724", "sacré"}));
        VERIFIER(ct.lignes[1].mid(1, 4) == QStringList({"Requiem", "", "", "profane"}));
        annuler_geste(g);
        // avec un écran : avant le premier « Ouvrir » de premier niveau (un projet à part)
        QDir(dossier.path()).mkdir("ecran");
        ecrire("ecran/e.grym", "Une œuvre, conservée, a : un titre (texte), unique.\n"
                         "L'écran des œuvres montre :\n    la liste des œuvres conservées,\n    un bouton « Fermer ».\n"
                         "Quand on clique sur « Fermer » dans l'écran des œuvres :\n    Fermer l'écran.\n"
                         "Ouvrir l'écran des œuvres.\n");
        const QString c2 = code_importation("œuvre", lire_schema(dossier.filePath("ecran")), "o.csv", {{"titre", "Titre"}}, &e);
        VERIFIER(!c2.isEmpty() && ajouter_importation(dossier.filePath("ecran"), dossier.filePath("ecran/e.grym"), c2, &g).isEmpty());
        VERIFIER(ajouter_importation(dossier.filePath("ecran"), dossier.filePath("ecran/e.grym"), "", &g) == "Rien à importer.");
        VERIFIER(lire("ecran/e.grym").endsWith("    Fermer l'écran.\n" + c2 + "\nOuvrir l'écran des œuvres.\n"));

        // sauvegarder, changer la base, restaurer
        const QString base = base_du_programme(dossier.filePath("p.grym")), sauvegardes = dossier.filePath("Sauvegardes");
        QString copie, avant;
        VERIFIER(sauvegarder_base(base, sauvegardes, &copie).isEmpty() && QFileInfo::exists(copie));
        VERIFIER(QFileInfo(copie).fileName().startsWith("p 2") && copie.endsWith(".grymd"));
        QString copie2;
        VERIFIER(sauvegarder_base(base, sauvegardes, &copie2).isEmpty() && copie2 != copie);   // jamais d'écrasement
        ecrire("p.grym", lire("p.grym").replace("Afficher le nombre", "Le n vaut une nouvelle œuvre :\n    Le titre vaut « Messie ».\n"
                                                                      "    Le genre vaut « sacré ».\nConserver n.\nAfficher le nombre").toUtf8());
        VERIFIER(lancer("p.grym") == "3\n");
        VERIFIER(restaurer_base(copie, base, sauvegardes, &avant).isEmpty() && QFileInfo::exists(avant));
        VERIFIER(lire_contenu(base, "œuvre", schema, false, &ct).isEmpty() && ct.lignes.size() == 2);
        VERIFIER(restaurer_base(avant, base, sauvegardes, &copie2).isEmpty());   // et retour en arrière
        VERIFIER(lire_contenu(base, "œuvre", schema, false, &ct).isEmpty() && ct.lignes.size() == 3);
        VERIFIER(restaurer_base(dossier.filePath("o.csv"), base, sauvegardes, &avant) == "« o.csv » n'est pas une base GrymoiR.");
        VERIFIER(restaurer_base(base, base, sauvegardes, &avant).startsWith("C'est la base en service"));
        VERIFIER(sauvegarder_base(dossier.filePath("rien.grymd"), sauvegardes, &copie).startsWith("Pas encore de base"));
    }

    // A2-c : l'aperçu de la migration ; il ne crée ni ne change la base
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        auto ecrire = [&](const QString &t) {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
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
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write("Un compositeur, conservé, a : un nom (texte), unique, une naissance (date), facultative.\n"
                "Si le nombre de compositeurs conservés = 0 :\n"
                "    Le c vaut un nouveau compositeur :\n        Le nom vaut « Liszt ».\n    Conserver c.\n"
                "    Le d vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n        La naissance vaut 31.03.1685.\n"
                "    Conserver d.\n"
                "L'écran des compositeurs montre :\n    le texte « Choisis. »,\n"
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

    // T3 : l'apparence (grammaire, § 22.5) à l'exécution : l'accent du programme, le logo grand sur l'écran du
    // dessous, petit sur celui qui s'ouvre par-dessus, et en icône de la fenêtre
    {
        QTemporaryDir dossier;
        QImage image(20, 40, QImage::Format_ARGB32);
        image.fill(Qt::darkGreen);
        image.save(dossier.filePath("logo.png"));
        const QString prog = dossier.filePath("ap.grym");
        QFile f(prog);
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write("Les écrans ont la couleur verte et le logo « logo.png ».\n"
                "L'écran d'accueil montre :\n    un bouton « Suite »,\n    un bouton « Fermer ».\n"
                "L'écran de suite montre :\n    un bouton « Retour ».\n"
                "Quand on clique sur « Suite » dans l'écran d'accueil :\n    Ouvrir l'écran de suite.\n"
                "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                "Quand on clique sur « Retour » dans l'écran de suite :\n    Fermer l'écran.\n"
                "Ouvrir l'écran d'accueil.\n");
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
        auto bouton = [&](const QString &t) -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == t && b->isVisible()) return b;
            return nullptr;
        };
        auto hauteur_logo = [&]() {   // le logo visible, en pixels logiques
            for (QLabel *l : e.findChildren<QLabel *>())
                if (l->isVisible() && !l->pixmap().isNull()) return qRound(l->pixmap().height() / l->pixmap().devicePixelRatio());
            return 0;
        };
        VERIFIER(attendre([&] { return bouton("Suite") && bouton("Suite")->isEnabled(); }));
        VERIFIER(Theme::courant().accent() == "verte");
        VERIFIER(hauteur_logo() == 64 && !e.windowIcon().isNull());
        bouton("Suite")->click();
        VERIFIER(attendre([&] { return bouton("Retour") && bouton("Retour")->isEnabled(); }));
        VERIFIER(hauteur_logo() == 24);
        bouton("Retour")->click();
        VERIFIER(attendre([&] { return bouton("Fermer") && bouton("Fermer")->isEnabled(); }));
        bouton("Fermer")->click();
        attendre([&] { return !bouton("Fermer"); });
        e.close();
        Theme::courant().appliquer(Theme::Clair, "bleue");   // l'accent revient pour les essais suivants

        // l'aperçu lit la même apparence : couleur, logo, écran d'accueil
        const QVector<EcranLu> lus = lire_ecrans(dossier.path());
        VERIFIER(lus.size() == 2);
        for (const EcranLu &x : lus) {
            VERIFIER(x.couleur == 1 && x.logo == QFileInfo(dossier.filePath("logo.png")).absoluteFilePath());
            VERIFIER(x.accueil == (x.nom == "d'accueil"));
        }
        OngletEcrans o;
        o.montrer(dossier.path());
        o.choisir_ecran("d'accueil");
        QLabel *l = nullptr;
        for (QLabel *x : o.findChildren<QLabel *>()) if (!x->pixmap().isNull()) l = x;
        VERIFIER(l && qRound(l->pixmap().height() / l->pixmap().devicePixelRatio()) == 64);
        bool accent_de_l_apercu = false;   // l'aperçu seul prend la feuille verte ; l'atelier garde le bleu
        const QString verte = Theme::courant().feuille(Theme::courant().mode(), "verte");
        for (QWidget *w : o.findChildren<QWidget *>()) if (w->styleSheet() == verte) accent_de_l_apercu = true;
        VERIFIER(accent_de_l_apercu && Theme::courant().accent() == "bleue");
    }

    // A3-b dans la fenêtre : une zone, sa valeur de départ, sa validation, la liste qui suit, la ligne choisie
    {
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("z.grym");
        QFile f(prog);
        VERIFIER(f.open(QIODevice::WriteOnly));
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
        VERIFIER(f.open(QIODevice::WriteOnly));
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
            VERIFIER(f.open(QIODevice::WriteOnly));
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
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write(texte.toUtf8());
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            VERIFIER(f.open(QIODevice::ReadOnly));
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
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write(texte.toUtf8());
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            VERIFIER(f.open(QIODevice::ReadOnly));
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

    // États des éléments d'écran (§ 22.6) : options et conditions écrites, relues, gardées quand on modifie autre chose
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &texte) {
            QFile f(dossier.filePath("p.grym"));
            VERIFIER(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(texte.toUtf8());
        };
        auto lire = [&] {
            QFile f(dossier.filePath("p.grym"));
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        ecrire("L'écran de saisie montre :\n    une remarque (texte), facultative,\n    un bouton « OK ».\n"
               "Quand on clique sur « OK » dans l'écran de saisie :\n    Fermer l'écran.\n");
        const QString d = dossier.path(), e = "de saisie";
        Geste g;
        ElementNouveau z;
        z.sorte = ELEMENT_ZONE;
        z.texte = "remarque";
        z.type = "texte";
        z.feminin = true;
        z.facultatif = true;
        z.plusieurs_lignes = true;
        VERIFIER(ecran_modifier(d, e, 0, z, &g).isEmpty());
        VERIFIER(lire().contains("    une remarque (texte), facultative, sur plusieurs lignes,\n"));
        ElementNouveau b;
        b.sorte = ELEMENT_BOUTON;
        b.texte = "OK";
        b.actif_si = "la remarque de l'écran est présente";
        b.visible_si = "vrai";
        VERIFIER(ecran_modifier(d, e, 1, b, &g).isEmpty());
        VERIFIER(lire().contains("    un bouton « OK », actif si la remarque de l'écran est présente, visible si vrai.\n"));
        auto lus = lire_ecrans(d);
        VERIFIER(lus.size() == 1 && lus[0].elements[1].actif_si == "la remarque de l'écran est présente"
                 && lus[0].elements[1].visible_si == "vrai");
        VERIFIER(lus[0].elements[0].plusieurs_lignes && lus[0].elements[0].feminin && !lus[0].elements[0].lecture_seule);
        // modifier la zone ne touche pas aux conditions du bouton ; la zone garde son genre
        z.plusieurs_lignes = false;
        z.lecture_seule = true;
        z.visible_si = "la remarque de l'écran est absente";
        VERIFIER(ecran_modifier(d, e, 0, z, &g).isEmpty());
        VERIFIER(lire().contains("    une remarque (texte), facultative, en lecture seule, visible si la remarque de l'écran est absente,\n"
                                 "    un bouton « OK », actif si la remarque de l'écran est présente, visible si vrai.\n"));
        // refus : texte fixe actif, liste visible, condition mal écrite (la réanalyse annule tout)
        const QString stable = lire();
        ElementNouveau t;
        t.sorte = ELEMENT_TEXTE;
        t.texte = "Bonjour";
        t.actif_si = "vrai";
        VERIFIER(ecran_ajouter(d, e, t, &g) == "Seuls un bouton ou une zone sont actifs ou non.");
        b.actif_si = "3";   // un nombre n'est pas une condition
        VERIFIER(ecran_modifier(d, e, 1, b, &g).startsWith("Geste annulé"));
        VERIFIER(lire() == stable);
        z.lecture_seule = false;
        z.type = "nombre";
        z.plusieurs_lignes = true;
        VERIFIER(ecran_modifier(d, e, 0, z, &g) == "Seule une zone (texte) s'écrit sur plusieurs lignes.");
    }

    // Groupe titré, valeurs permises d'une zone, boutons radio (§ 22.7)
    {
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &texte) {
            QFile f(dossier.filePath("p.grym"));
            VERIFIER(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(texte.toUtf8());
        };
        auto lire = [&] {
            QFile f(dossier.filePath("p.grym"));
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        ecrire("L'écran de saisie montre :\n    un état (« actif », « suspendu » ou « retiré »), « actif » au départ,\n"
               "    un bouton « OK ».\nQuand on clique sur « OK » dans l'écran de saisie :\n    Fermer l'écran.\n");
        const QString d = dossier.path(), e = "de saisie";
        Geste g;
        auto lus = lire_ecrans(d);
        VERIFIER(lus.size() == 1 && lus[0].elements[0].valeurs == QStringList({"actif", "suspendu", "retiré"}));
        // « Appliquer » sur la zone garde ses valeurs ; le groupe l'entoure, son titre se change
        ElementNouveau z;
        z.sorte = ELEMENT_ZONE;
        z.texte = "état";
        z.type = "texte";
        z.depart = "actif";
        z.valeurs = lus[0].elements[0].valeurs;
        VERIFIER(ecran_modifier(d, e, 0, z, &g).isEmpty());
        VERIFIER(lire().contains("    un état (« actif », « suspendu » ou « retiré »), « actif » au départ,\n"));
        z.valeurs << "actif";
        VERIFIER(ecran_modifier(d, e, 0, z, &g) == "« actif » est déjà dans la liste.");
        z.valeurs = QStringList({"oui", "non"});
        VERIFIER(ecran_modifier(d, e, 0, z, &g).contains("la valeur de départ doit en être une"));
        VERIFIER(ecran_grouper(d, e, 0, "Statut", &g).isEmpty());
        VERIFIER(lire().startsWith("L'écran de saisie montre :\n    le groupe « Statut » :\n        un état (« actif », « suspendu » ou « retiré »), "
                                   "« actif » au départ,\n    un bouton « OK ».\n"));
        lus = lire_ecrans(d);
        VERIFIER(lus[0].elements[0].sorte == ELEMENT_GROUPE && lus[0].elements[0].texte == "Statut");
        ElementNouveau t;
        t.sorte = ELEMENT_GROUPE;
        t.texte = "Situation";
        VERIFIER(ecran_modifier(d, e, 0, t, &g).isEmpty() && lire().contains("    le groupe « Situation » :\n"));
        VERIFIER(ecran_grouper(d, e, 0, "Autre", &g) == "Choisis un élément, pas un bloc.");
        // le dessin : trois boutons radio, un « — » de plus pour une zone facultative ; cinq valeurs : un menu
        QVector<ElementVue> el(1);
        el[0].sorte = ELEMENT_ZONE;
        el[0].texte = "État";
        el[0].type = "texte";
        el[0].choix = QStringList({"actif", "suspendu", "retiré"});
        VueEcran v = dessiner_ecran("Saisie", el);
        VERIFIER(v.zones[0] && v.zones[0]->findChildren<QRadioButton *>().size() == 3);
        delete v.vue;
        el[0].facultatif = true;
        v = dessiner_ecran("Saisie", el);
        VERIFIER(v.zones[0]->findChildren<QRadioButton *>().size() == 4);
        delete v.vue;
        el[0].choix << "a" << "b";
        v = dessiner_ecran("Saisie", el);
        VERIFIER(qobject_cast<QComboBox *>(v.zones[0]) != nullptr);
        delete v.vue;
    }

    // L'image (§ 22.8) : ajoutée, relue, dessinée depuis le dossier du programme
    {
        QTemporaryDir dossier;
        VERIFIER(QFile::copy(QString(GRYM_SOURCE) + "/exemples/pixel.png", dossier.filePath("plan.png")));
        QFile f(dossier.filePath("p.grym"));
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write("L'écran d'accueil montre :\n    un bouton « OK ».\nQuand on clique sur « OK » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        f.close();
        const QString d = dossier.path(), e = "d'accueil";
        Geste g;
        ElementNouveau i;
        i.sorte = ELEMENT_IMAGE;
        i.texte = "plan.png";
        i.visible_si = "vrai";
        VERIFIER(ecran_ajouter(d, e, i, &g).isEmpty());
        QFile r(dossier.filePath("p.grym"));
        VERIFIER(r.open(QIODevice::ReadOnly));
        VERIFIER(QString::fromUtf8(r.readAll()).contains("    un bouton « OK »,\n    l'image « plan.png », visible si vrai.\n"));
        r.close();
        const auto lus = lire_ecrans(d);
        VERIFIER(lus.size() == 1 && lus[0].elements.size() == 2 && lus[0].elements[1].sorte == ELEMENT_IMAGE
                 && lus[0].elements[1].texte == "plan.png" && lus[0].elements[1].visible_si == "vrai");
        i.texte = "absente.png";   // la réanalyse refuse : rien n'est écrit
        VERIFIER(ecran_ajouter(d, e, i, &g).startsWith("Geste annulé"));
        i.texte = "plan.png";
        i.actif_si = "vrai";
        VERIFIER(ecran_ajouter(d, e, i, &g) == "Seuls un bouton ou une zone sont actifs ou non.");
        QVector<ElementVue> el(1);
        el[0].sorte = ELEMENT_IMAGE;
        el[0].texte = "plan.png";
        QFile png(dossier.filePath("plan.png"));
        VERIFIER(png.open(QIODevice::ReadOnly));
        el[0].octets = png.readAll();
        VueEcran v = dessiner_ecran("Accueil", el);
        auto *l = qobject_cast<QLabel *>(v.controles[0]);
        VERIFIER(l && !l->pixmap().isNull());
        delete v.vue;
        el[0].octets.clear();   // sans le fichier, l'aperçu montre son nom
        v = dessiner_ecran("Accueil", el);
        l = qobject_cast<QLabel *>(v.controles[0]);
        VERIFIER(l && l->text() == "plan.png");
        delete v.vue;
    }

    // Onglets (grammaire, § 22.10 ; docs/atelier.md, § 5 decies) : ajoutés, dessinés, renommés, placés, cliquables
    {
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        auto ecrire = [&](const char *t) {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(t);
        };
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        ecrire("L'écran x montre :\n    le texte « t ».\n");
        Geste g;
        QString r = ecran_ajouter_onglets(dossier.path(), "x", &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        // chaque onglet naît avec un bloc libre : ce qu'on y pose se place au pixel (§ 5 decies)
        VERIFIER(lire().contains("    les onglets :\n        l'onglet « Premier » :\n            en position libre :\n"
                                 "                le texte « Premier onglet », à 16 de la gauche et 16 du haut,\n"
                                 "        l'onglet « Second » :\n            en position libre :\n"
                                 "                le texte « Second onglet », à 16 de la gauche et 16 du haut."));
        ElementNouveau n;
        n.sorte = ELEMENT_ONGLET;
        n.texte = "Identité";
        VERIFIER(ecran_modifier(dossier.path(), "x", 2, n, &g).isEmpty() && lire().contains("l'onglet « Identité » :"));
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        o.montrer(dossier.path());
        QApplication::processEvents();
        auto *tw = qobject_cast<QTabWidget *>(o.controle(1));
        VERIFIER(tw && tw->count() == 2 && tw->tabText(0) == "Identité" && tw->currentIndex() == 0);
        if (tw) {   // un clic sur le second onglet de l'aperçu le montre
            QTabBar *barre = tw->tabBar();
            const QPoint p = barre->tabRect(1).center();
            QMouseEvent appui(QEvent::MouseButtonPress, QPointF(p), QPointF(barre->mapToGlobal(p)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(barre, &appui);
            QMouseEvent lache(QEvent::MouseButtonRelease, QPointF(p), QPointF(barre->mapToGlobal(p)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(barre, &lache);
            VERIFIER(tw->currentIndex() == 1);
            // « Ajouter » vise la page ouverte : l'élément va dans son bloc libre, sous le texte déjà posé
            const int dans = o.cible_ajout();
            VERIFIER(dans >= 0 && o.ecrans()[0].elements.value(dans).sorte == ELEMENT_ONGLET
                     && o.ecrans()[0].elements.value(dans).texte == "Second");
            ElementNouveau b;
            b.sorte = ELEMENT_TEXTE;
            b.texte = "Valider";
            r = ecran_ajouter(dossier.path(), "x", b, &g, dans);
            if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
            VERIFIER(lire().contains("                le texte « Second onglet », à 16 de la gauche et 16 du haut,\n"
                                     "                le texte « Valider », à 16 de la gauche et 64 du haut."));
            // l'aperçu redessiné garde la page ouverte, et le bouton y est
            o.montrer(dossier.path());
            QApplication::processEvents();
            auto *tw2 = qobject_cast<QTabWidget *>(o.controle(1));
            VERIFIER(tw2 && tw2->currentIndex() == 1);
            bool vu = false;
            if (tw2) for (QLabel *x : tw2->currentWidget()->findChildren<QLabel *>()) vu |= x->text() == "Valider";
            VERIFIER(vu);
        }
        // dans un écran libre : les onglets se placent, sous ce qui y est
        ecrire("L'écran x montre :\n    en position libre :\n        le texte « t », à 16 de la gauche et 16 du haut.\n");
        VERIFIER(ecran_ajouter_onglets(dossier.path(), "x", &g).isEmpty());
        VERIFIER(lire().contains("        les onglets, à 16 de la gauche et 64 du haut, large de 480, haut de 320 :\n"
                                 "            l'onglet « Premier » :\n"));
        VERIFIER(ecran_placer(dossier.path(), "x", 2, 200, 16, 320, 160, &g).isEmpty());
        VERIFIER(lire().contains("        les onglets, à 200 de la gauche et 16 du haut, large de 320, haut de 160 :\n"));
        o.montrer(dossier.path());
        QApplication::processEvents();
        QWidget *pl = nullptr;
        for (int k = 0; k < 12; k++) if (qobject_cast<QTabWidget *>(o.controle(k))) pl = o.controle(k);
        VERIFIER(pl && pl->geometry() == QRect(200, 16, 320, 160));
    }

    // Image calculée (grammaire, § 22.8 ; docs/atelier.md, § 5 nonies) : ajoutée, contractée, modifiée, montrée par sa source
    {
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Un compositeur, conservé, a :\n    un nom (texte),\n    un portrait (image), facultatif,\n    une photo (image), facultative.\n"
                    "L'écran x montre :\n    la liste des compositeurs conservés,\n    le texte « t ».\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        Geste g;
        ElementNouveau n;
        n.sorte = ELEMENT_IMAGE;
        n.source = "le portrait du compositeur choisi de l'écran";
        n.visible_si = "le compositeur choisi de l'écran est présent";
        QString r = ecran_ajouter(dossier.path(), "x", n, &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        VERIFIER(lire().contains("    l'image du portrait du compositeur choisi de l'écran, visible si le compositeur choisi de l'écran est présent."));
        n.source = "la photo du compositeur choisi de l'écran";
        n.visible_si.clear();
        VERIFIER(ecran_modifier(dossier.path(), "x", 2, n, &g).isEmpty());
        VERIFIER(lire().contains("    l'image de la photo du compositeur choisi de l'écran."));
        n.source = "du portrait";
        VERIFIER(ecran_modifier(dossier.path(), "x", 2, n, &g).startsWith("Écris la valeur sans « de »"));
        n.source = "le nom du compositeur choisi de l'écran";   // pas une image : l'analyse le dirait si le type se connaissait
        OngletEcrans o;
        o.show();
        o.montrer(dossier.path());
        VERIFIER(o.ecrans().size() == 1 && o.ecrans()[0].elements.size() == 3
                 && o.ecrans()[0].elements[2].source == "la photo du compositeur choisi de l'écran");
        auto *l = qobject_cast<QLabel *>(o.controle(2));
        VERIFIER(l && l->text() == QString::fromUtf8("Image : la photo du compositeur choisi de l'écran"));
        QLabel vide;
        montrer_image(&vide, QByteArray(), QString());   // absente : rien
        VERIFIER(vide.text().isEmpty() && vide.pixmap().isNull());
    }

    // Position libre (grammaire, § 22.9 ; docs/atelier.md, § 5 octies) : le dessin, les gestes, la souris et la grille
    {
        QVector<ElementVue> el(4);   // bloc libre : un bouton à sa place naturelle, un texte de largeur dite ; puis la fin
        el[0].sorte = ELEMENT_LIBRE;
        el[1].sorte = ELEMENT_BOUTON;
        el[1].texte = "OK";
        el[1].place = true;
        el[1].gauche = 96;
        el[1].haut = 16;
        el[2].sorte = ELEMENT_TEXTE;
        el[2].texte = "Nom";
        el[2].place = true;
        el[2].gauche = 8;
        el[2].haut = 64;
        el[2].largeur = 200;
        el[2].hauteur = 40;
        el[3].sorte = ELEMENT_FIN_DE_BLOC;
        VueEcran v = dessiner_ecran("Saisie", el);
        VERIFIER(v.controles[0] && v.controles[1] && v.controles[1]->parentWidget() == v.controles[0]);
        VERIFIER(v.controles[1]->pos() == QPoint(96, 16));
        VERIFIER(v.controles[2]->geometry() == QRect(8, 64, 200, 40));
        VERIFIER(v.controles[0]->minimumSizeHint().width() >= 208 && v.controles[0]->minimumSizeHint().height() >= 104);
        delete v.vue;
    }
    {
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        auto ecrire = [&](const char *t) {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(t);
        };
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        const char *depart = "L'écran d'accueil montre :\n    en position libre :\n"
                             "        un bouton « A », à 16 de la gauche et 16 du haut,\n"
                             "        un bouton « B », à 16 de la gauche et 64 du haut, large de 120,\n"
                             "    le texte « fin ».\n"
                             "Quand on clique sur « A » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                             "Quand on clique sur « B » dans l'écran d'accueil :\n    Fermer l'écran.\n";
        ecrire(depart);
        Geste g;
        int apres = -1;
        // B monte au-dessus de A : l'ordre écrit suit la place ; sa largeur reste
        QString r = ecran_placer(dossier.path(), "d'accueil", 2, 200, 8, -1, -1, &g, &apres);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        VERIFIER(r.isEmpty() && apres == 1);
        VERIFIER(lire().contains("    en position libre :\n        un bouton « B », à 200 de la gauche et 8 du haut, large de 120,\n"
                                 "        un bouton « A », à 16 de la gauche et 16 du haut,\n"));
        r = ecran_placer(dossier.path(), "d'accueil", 1, 200, 8, 0, 48, &g);   // largeur retirée, hauteur dite
        VERIFIER(r.isEmpty() && lire().contains("un bouton « B », à 200 de la gauche et 8 du haut, haut de 48,\n"));
        VERIFIER(!ecran_placer(dossier.path(), "d'accueil", 4, 0, 0, -1, -1, &g).isEmpty());   // le texte hors du bloc
        VERIFIER(ecran_placer(dossier.path(), "d'accueil", 1, 0, 0, 4, -1, &g) == "La taille va de 8 à 4000 pixels.");
        // modifier garde la place ; ajouter va dans le bloc, sous le plus bas ; grouper et déplacer refusés
        ElementNouveau n;
        n.sorte = ELEMENT_BOUTON;
        n.texte = "Bravo";
        VERIFIER(ecran_modifier(dossier.path(), "d'accueil", 1, n, &g).isEmpty());
        VERIFIER(lire().contains("un bouton « Bravo », à 200 de la gauche et 8 du haut, haut de 48,\n"));
        n.sorte = ELEMENT_TEXTE;
        n.texte = "Neuf";
        VERIFIER(ecran_ajouter(dossier.path(), "d'accueil", n, &g).isEmpty());
        VERIFIER(lire().contains("        le texte « Neuf », à 16 de la gauche et 64 du haut,\n    le texte « fin »."));
        VERIFIER(ecran_deplacer(dossier.path(), "d'accueil", 1, 5, DEPOT_APRES, &g).startsWith("Dans un bloc en position libre"));
        // grouper dans un bloc libre : le groupe prend la place de l'élément, qui s'y empile (1.81)
        r = ecran_grouper(dossier.path(), "d'accueil", 1, "G", &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        VERIFIER(lire().contains("        le groupe « G », à 200 de la gauche et 8 du haut, haut de 48 :\n            un bouton « Bravo »,\n"
                                 "        un bouton « A »,"));
        // le groupe se place avec son contenu, et se trie comme une seule unité : sous A, il passe après A
        VERIFIER(ecran_placer(dossier.path(), "d'accueil", 1, 16, 40, 240, 0, &g, &apres).isEmpty() && apres == 2);
        VERIFIER(lire().contains("        un bouton « A », à 16 de la gauche et 16 du haut,\n"
                                 "        le groupe « G », à 16 de la gauche et 40 du haut, large de 240 :\n            un bouton « Bravo »,\n"));
        // retirer le groupe : son élément remonte dans le bloc libre, à la place du groupe
        VERIFIER(ecran_supprimer(dossier.path(), "d'accueil", 2, &g).isEmpty());
        VERIFIER(lire().contains("        un bouton « Bravo », à 16 de la gauche et 40 du haut,\n"));
        // retirer le bloc : ses éléments restent, sans place
        VERIFIER(ecran_supprimer(dossier.path(), "d'accueil", 0, &g).isEmpty());
        VERIFIER(!lire().contains("en position libre") && !lire().contains("de la gauche") && lire().contains("un bouton « Bravo »,"));
        // tout l'écran en position libre, chaque élément à la place mesurée
        ecrire("L'écran d'accueil montre :\n    côte à côte :\n        le texte « a »,\n        le texte « b »,\n    le texte « c ».\n");
        QVector<QRect> places = {QRect(0, 0, 88, 24), QRect(0, 0, 40, 24), QRect(48, 0, 40, 24), QRect(), QRect(0, 32, 88, 24)};
        r = ecran_liberer(dossier.path(), "d'accueil", places, &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        // le bloc reste un bloc, placé d'un seul tenant, sa règle à l'intérieur (1.81)
        VERIFIER(lire().startsWith("L'écran d'accueil montre :\n    en position libre :\n"
                                   "        côte à côte, à 0 de la gauche et 0 du haut, large de 88, haut de 24 :\n"
                                   "            le texte « a »,\n            le texte « b »,\n"
                                   "        le texte « c », à 0 de la gauche et 32 du haut, large de 88.\n"));
        // un écran vide, libre dès la création
        ecrire("Afficher 1.\n");
        VERIFIER(nouvel_ecran(dossier.path(), chemin, "de saisie", &g, true).isEmpty());
        VERIFIER(lire().contains("L'écran de saisie montre :\n    en position libre :\n"
                                 "        un bouton « Fermer », à 16 de la gauche et 16 du haut.\n"));
    }
    {   // un bloc placé sur la toile : un widget à sa place, à la taille de son contenu ou à la taille dite
        QVector<ElementVue> el(6);
        el[0].sorte = ELEMENT_LIBRE;
        el[1].sorte = ELEMENT_GROUPE;
        el[1].texte = "G";
        el[1].place = true;
        el[1].gauche = 40;
        el[1].haut = 10;
        el[1].largeur = 300;
        el[2].sorte = ELEMENT_TEXTE;
        el[2].texte = "dedans";
        el[3].sorte = ELEMENT_FIN_DE_BLOC;
        el[4].sorte = ELEMENT_ZONE;
        el[4].texte = "Note";
        el[4].type = "texte";
        el[4].plusieurs_lignes = true;
        el[4].place = true;
        el[4].haut = 200;
        el[4].hauteur = 400;   // plus que huit lignes : la hauteur dite se prend entière
        el[5].sorte = ELEMENT_FIN_DE_BLOC;
        VueEcran v = dessiner_ecran("Saisie", el);
        VERIFIER(v.controles[1] && v.controles[1]->parentWidget() == v.controles[0]);
        VERIFIER(v.controles[1]->pos() == QPoint(40, 10) && v.controles[1]->width() == 300 && v.controles[1]->height() > 20);
        VERIFIER(v.controles[2] && v.controles[1]->isAncestorOf(v.controles[2]));
        VERIFIER(v.controles[4]->height() == 400 && v.zones[4]->maximumHeight() > 300);
        delete v.vue;
    }
    {   // la souris : glisser un élément montre la grille, aimante à 8 pixels, écrit la place au lâcher
        QTemporaryDir dossier;
        QFile f(dossier.filePath("p.grym"));
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write("L'écran d'accueil montre :\n    en position libre :\n        un bouton « A », à 16 de la gauche et 16 du haut.\n"
                "Quand on clique sur « A » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        f.close();
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        o.montrer(dossier.path());
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        QWidget *b = o.controle(1);
        VERIFIER(b != nullptr);
        if (b) {
            const QPointF dans(5, 5), global = b->mapToGlobal(QPoint(5, 5));
            QMouseEvent appui(QEvent::MouseButtonPress, dans, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(b, &appui);
            QMouseEvent bouge(QEvent::MouseMove, dans, global + QPointF(37, 21), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(b, &bouge);
            VERIFIER(o.grille_visible() != nullptr);
            VERIFIER(b->pos() == QPoint(56, 40));   // 16 + 37 → 53 → 56 ; 16 + 21 → 37 → 40
            QMouseEvent lache(QEvent::MouseButtonRelease, dans, global + QPointF(37, 21), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(b, &lache);
            VERIFIER(o.grille_visible() == nullptr);
            if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
            VERIFIER(resultat.isEmpty());
            QFile r(dossier.filePath("p.grym"));
            VERIFIER(r.open(QIODevice::ReadOnly));
            VERIFIER(QString::fromUtf8(r.readAll()).contains("un bouton « A », à 56 de la gauche et 40 du haut."));
            // le clavier, sur l'aperçu relu : la poignée dans le coin ; Maj + flèche, 8 pixels ; Alt + flèche, la taille
            o.montrer(dossier.path());
            o.choisir_element(1);
            VERIFIER(o.poignee_visible() != nullptr);
            VERIFIER(o.touche(Qt::Key_Right, Qt::ShiftModifier) && resultat.isEmpty());
            VERIFIER(!o.touche(Qt::Key_A));   // une autre touche ne sert pas
            o.montrer(dossier.path());
            o.choisir_element(1);
            const int h = o.controle(1) ? o.controle(1)->height() : 0;
            VERIFIER(o.touche(Qt::Key_Down, Qt::AltModifier) && resultat.isEmpty());
            QFile r2(dossier.filePath("p.grym"));
            VERIFIER(r2.open(QIODevice::ReadOnly));
            const QString t2 = QString::fromUtf8(r2.readAll());
            VERIFIER(t2.contains(QString("un bouton « A », à 64 de la gauche et 40 du haut, haut de %1.").arg(h + 1)));
            if (!t2.contains("à 64 de la gauche")) std::printf("%s\n", qPrintable(t2));
        }
        VERIFIER(OngletEcrans::aimanter(3) == 0 && OngletEcrans::aimanter(4) == 8 && OngletEcrans::aimanter(-5) == 0);
    }
    {   // les onglets dans un écran libre, les quatre défauts de la capture du 15 octobre : les déplacer par leur barre ;
        // de la place dans la page ; glisser un élément d'un onglet à l'autre ; les supprimer
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    en position libre :\n        le texte « t », à 16 de la gauche et 16 du haut.\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        Geste g;
        VERIFIER(ecran_ajouter_onglets(dossier.path(), "x", &g).isEmpty());
        OngletEcrans o;
        o.resize(1000, 800);
        o.show();
        QString resultat = "pas de geste";
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        auto onglets = [&]() -> QTabWidget * {
            for (int k = 0; k < 20; k++) if (auto *t = qobject_cast<QTabWidget *>(o.controle(k))) return t;
            return nullptr;
        };
        auto geste_souris = [&](QWidget *w, QPoint de, QPoint vers) {
            const QPoint a = w->mapToGlobal(de), b = a + vers;
            QMouseEvent appui(QEvent::MouseButtonPress, QPointF(de), QPointF(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &appui);
            QMouseEvent loin(QEvent::MouseMove, QPointF(de), QPointF(a + QPoint(0, 30)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &loin);
            QMouseEvent bouge(QEvent::MouseMove, QPointF(de), QPointF(b), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &bouge);
            QMouseEvent lache(QEvent::MouseButtonRelease, QPointF(de), QPointF(b), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(w, &lache);
        };
        o.montrer(dossier.path());
        QApplication::processEvents();
        QTabWidget *tw = onglets();
        VERIFIER(tw && tw->size() == QSize(480, 320));
        // 2. de la place : le bloc libre de la page remplit la page
        if (tw) VERIFIER(tw->currentWidget()->findChild<QWidget *>("toile") && tw->currentWidget()->findChild<QWidget *>("toile")->height() > 200);
        // 1. saisis par leur barre, les onglets remontent (64 → 24)
        if (tw) geste_souris(tw->tabBar(), tw->tabBar()->tabRect(0).center(), QPoint(0, -40));
        if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
        {   // remontés (de 64 à 24, ou au guide du texte voisin, à quelques pixels)
            QRegularExpressionMatch m = QRegularExpression("les onglets, à 16 de la gauche et (\\d+) du haut, large de 480, haut de 320 :").match(lire());
            VERIFIER(m.hasMatch() && m.captured(1).toInt() <= 30);
        }
        // le seul élément d'un onglet ne le quitte pas (l'onglet resterait vide) : on lui ajoute un voisin d'abord
        {
            o.montrer(dossier.path());
            int seul = -1, second = -1;
            for (int k = 0; k < o.ecrans()[0].elements.size(); k++) {
                if (o.ecrans()[0].elements[k].texte == "Premier onglet") seul = k;
                if (o.ecrans()[0].elements[k].sorte == ELEMENT_ONGLET && o.ecrans()[0].elements[k].texte == "Second") second = k;
            }
            VERIFIER(ecran_mettre_dans(dossier.path(), "x", seul, second, &g).startsWith("C'est le seul élément"));
            int premier = -1;
            for (int k = 0; k < o.ecrans()[0].elements.size(); k++)
                if (o.ecrans()[0].elements[k].sorte == ELEMENT_ONGLET && o.ecrans()[0].elements[k].texte == "Premier") premier = k;
            ElementNouveau v;
            v.sorte = ELEMENT_TEXTE;
            v.texte = "voisin";
            VERIFIER(ecran_ajouter(dossier.path(), "x", v, &g, premier).isEmpty());
        }
        // 3. le texte du premier onglet, lâché sur l'en-tête du second, y est rangé, sous ce qui y est
        o.montrer(dossier.path());
        QApplication::processEvents();
        tw = onglets();
        QLabel *texte = nullptr;
        if (tw) for (QLabel *l : tw->widget(0)->findChildren<QLabel *>()) if (l->text() == "Premier onglet") texte = l;
        VERIFIER(texte != nullptr);
        if (tw && texte) {
            const QPoint cible = tw->tabBar()->mapToGlobal(tw->tabBar()->tabRect(1).center());
            geste_souris(texte, QPoint(3, 3), cible - texte->mapToGlobal(QPoint(3, 3)));
        }
        if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
        VERIFIER(lire().contains("            l'onglet « Second » :\n                en position libre :\n"
                                 "                    le texte « Second onglet », à 16 de la gauche et 16 du haut,\n"
                                 "                    le texte « Premier onglet », à 16 de la gauche et 64 du haut."));
        // 4. supprimer un onglet emporte son contenu ; le dernier emporte les onglets
        o.montrer(dossier.path());
        int second = -1, premier = -1;
        for (int k = 0; k < o.ecrans()[0].elements.size(); k++) {
            if (o.ecrans()[0].elements[k].sorte == ELEMENT_ONGLET && o.ecrans()[0].elements[k].texte == "Second") second = k;
            if (o.ecrans()[0].elements[k].sorte == ELEMENT_ONGLET && o.ecrans()[0].elements[k].texte == "Premier") premier = k;
        }
        VERIFIER(second > 0 && premier > 0);
        VERIFIER(ecran_supprimer(dossier.path(), "x", second, &g).isEmpty());
        VERIFIER(lire().contains("l'onglet « Premier »") && !lire().contains("Second") && !lire().contains("le texte « Premier onglet »"));
        VERIFIER(ecran_supprimer(dossier.path(), "x", premier, &g).isEmpty());
        VERIFIER(!lire().contains("onglet") && lire().contains("le texte « t »"));
    }
    {   // un élément de l'écran, lâché dans la page ouverte d'un onglet : il y passe, là où on l'a lâché, et n'est plus que là
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    en position libre :\n        le texte « dehors », à 16 de la gauche et 16 du haut,\n"
                    "        le texte « reste », à 300 de la gauche et 16 du haut.\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        Geste g;
        VERIFIER(ecran_ajouter_onglets(dossier.path(), "x", &g).isEmpty());
        OngletEcrans o;
        o.resize(1000, 800);
        o.show();
        QString resultat = "pas de geste";
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        o.montrer(dossier.path());
        QApplication::processEvents();
        QTabWidget *tw = nullptr;
        for (int k = 0; k < 20; k++) if (auto *t = qobject_cast<QTabWidget *>(o.controle(k))) tw = t;
        QLabel *dehors = nullptr;
        for (int k = 0; k < 20; k++) if (auto *l = qobject_cast<QLabel *>(o.controle(k)); l && l->text() == "dehors") dehors = l;
        VERIFIER(tw && dehors);
        if (tw && dehors) {
            const QPoint de(3, 3), a = dehors->mapToGlobal(de);
            const QPoint dans = tw->currentWidget()->mapToGlobal(QPoint(200, 120));   // au milieu de la page « Premier »
            QMouseEvent appui(QEvent::MouseButtonPress, QPointF(de), QPointF(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(dehors, &appui);
            QMouseEvent loin(QEvent::MouseMove, QPointF(de), QPointF(a + QPoint(0, 30)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(dehors, &loin);
            QMouseEvent bouge(QEvent::MouseMove, QPointF(de), QPointF(dans), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(dehors, &bouge);
            VERIFIER(o.texte_etat().contains("seulement là"));
            QMouseEvent lache(QEvent::MouseButtonRelease, QPointF(de), QPointF(dans), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(dehors, &lache);
        }
        if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
        const QString t = lire();
        QRegularExpressionMatch m = QRegularExpression("l'onglet « Premier » :\n                en position libre :\n"
                                                       "                    le texte « Premier onglet », à 16 de la gauche et 16 du haut,\n"
                                                       "                    le texte « dehors », à (\\d+) de la gauche et (\\d+) du haut").match(t);
        VERIFIER(m.hasMatch() && qAbs(m.captured(1).toInt() - 200) <= 24 && qAbs(m.captured(2).toInt() - 120) <= 24);
        VERIFIER(t.count("le texte « dehors »") == 1 && t.contains("        le texte « reste », à 300 de la gauche et 16 du haut,\n"));
        if (!m.hasMatch()) std::printf("%s\n", qPrintable(t));
        // il n'apparaît que sur cet onglet
        o.montrer(dossier.path());
        QApplication::processEvents();
        tw = nullptr;
        for (int k = 0; k < 20; k++) if (auto *x = qobject_cast<QTabWidget *>(o.controle(k))) tw = x;
        int sur_premier = 0, sur_second = 0;
        if (tw) {
            for (QLabel *l : tw->widget(0)->findChildren<QLabel *>()) sur_premier += l->text() == "dehors";
            for (QLabel *l : tw->widget(1)->findChildren<QLabel *>()) sur_second += l->text() == "dehors";
        }
        VERIFIER(sur_premier == 1 && sur_second == 0);
    }
    {   // un écran mixte : un bloc libre, et des boutons restés en dehors, décrits ; glisser l'un d'eux le range dans le bloc libre,
        // là où on l'a lâché (avant : la conversion de l'écran était refusée, « déjà en position libre », et le bouton revenait)
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    en position libre :\n        le texte « teste », à 0 de la gauche et 0 du haut,\n"
                    "        le texte « autre », à 0 de la gauche et 300 du haut,\n"
                    "    un bouton « Fermer ».\n"
                    "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\n");
        }
        OngletEcrans o;
        o.resize(1000, 800);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        o.montrer(dossier.path());
        QApplication::processEvents();
        QPushButton *b = nullptr;
        QWidget *toile = nullptr;
        for (int k = 0; k < 20; k++) {
            if (auto *x = qobject_cast<QPushButton *>(o.controle(k)); x && x->text() == "Fermer") b = x;
            if (o.controle(k) && o.controle(k)->objectName() == "toile") toile = o.controle(k);
        }
        VERIFIER(b && toile);
        if (b && toile) {
            const QPoint m(4, 4), a = b->mapToGlobal(m);
            const QPoint vise = toile->mapToGlobal(QPoint(200, 100)) + QPoint(4, 4);   // le coin du bouton lâché à 200 × 100 du bloc
            QMouseEvent appui(QEvent::MouseButtonPress, QPointF(m), QPointF(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(b, &appui);
            QMouseEvent loin(QEvent::MouseMove, QPointF(m), QPointF(a + QPoint(0, 30)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(b, &loin);
            QMouseEvent bouge(QEvent::MouseMove, QPointF(m), QPointF(vise), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(b, &bouge);
            // la grille : visible, sans fond (le thème peint tout QWidget), et sous les éléments, qui se voient au travers
            QWidget *gr = o.grille_visible();
            VERIFIER(gr && gr->styleSheet().contains("transparent")
                     && gr->parentWidget()->children().indexOf(gr) < gr->parentWidget()->children().indexOf(toile));
            QMouseEvent lache(QEvent::MouseButtonRelease, QPointF(m), QPointF(vise), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(b, &lache);
        }
        QFile r(chemin);
        VERIFIER(r.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(r.readAll());
        QRegularExpressionMatch m = QRegularExpression("        un bouton « Fermer », à (\\d+) de la gauche et (\\d+) du haut").match(t);
        VERIFIER(resultat.isEmpty() && m.hasMatch() && qAbs(m.captured(1).toInt() - 200) <= 8 && qAbs(m.captured(2).toInt() - 100) <= 8);
        if (!m.hasMatch()) std::printf("%s\n", qPrintable(t));
    }
    {   // le dernier élément d'un bloc de rangement placé (côte à côte, l'un sous l'autre, groupe) en sort : le bloc vide disparaît ;
        // le dernier élément d'un onglet, lui, reste (une page vide n'a pas de sens)
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    en position libre :\n        le texte « test », à 0 de la gauche et 0 du haut,\n"
                    "        côte à côte, à 600 de la gauche et 40 du haut :\n            un bouton « Kopf ».\n"
                    "Quand on clique sur « Kopf » dans l'écran x :\n    Fermer l'écran.\n");
        }
        OngletEcrans o;
        o.resize(1200, 800);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        o.montrer(dossier.path());
        QApplication::processEvents();
        QPushButton *b = nullptr;
        QWidget *toile = nullptr;
        for (int k = 0; k < 20; k++) {
            if (auto *x = qobject_cast<QPushButton *>(o.controle(k)); x && x->text() == "Kopf") b = x;
            if (o.controle(k) && o.controle(k)->objectName() == "toile" && !toile) toile = o.controle(k);
        }
        VERIFIER(b && toile);
        if (b && toile) {
            const QPoint m(4, 4), a = b->mapToGlobal(m), vise = toile->mapToGlobal(QPoint(200, 160)) + QPoint(4, 4);
            QMouseEvent appui(QEvent::MouseButtonPress, QPointF(m), QPointF(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(b, &appui);
            QMouseEvent loin(QEvent::MouseMove, QPointF(m), QPointF(a + QPoint(0, 30)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(b, &loin);
            QMouseEvent bouge(QEvent::MouseMove, QPointF(m), QPointF(vise), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(b, &bouge);
            QMouseEvent lache(QEvent::MouseButtonRelease, QPointF(m), QPointF(vise), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(b, &lache);
        }
        if (!resultat.isEmpty()) std::printf("KOPF geste=[%s]\n", qPrintable(resultat));
        QFile r(chemin);
        VERIFIER(r.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(r.readAll());
        QRegularExpressionMatch m = QRegularExpression("        un bouton « Kopf », à (\\d+) de la gauche et (\\d+) du haut").match(t);
        VERIFIER(resultat.isEmpty() && m.hasMatch() && !t.contains("côte à côte"));
        if (!m.hasMatch()) std::printf("%s\n", qPrintable(t));
    }
    {   // le menu (§ 22.12) dans le dessin : une barre en tête ; l'action clique le bouton invisible, qui garde le câblage
        // d'un bouton ; l'état du bouton (actif si, visible si) se reflète sur l'action
        QVector<ElementVue> el(4);
        el[0].sorte = ELEMENT_MENU;
        el[0].texte = "Membres";
        el[1].sorte = ELEMENT_BOUTON;
        el[1].texte = "Nouveau";
        el[2].sorte = ELEMENT_FIN_DE_BLOC;
        el[3].sorte = ELEMENT_TEXTE;
        el[3].texte = "t";
        VueEcran v = dessiner_ecran("Accueil", el);
        auto *barre = v.vue->findChild<QMenuBar *>();
        VERIFIER(barre && barre->actions().size() == 1 && barre->actions()[0]->text() == "Membres" && !barre->isNativeMenuBar());
        auto *b = qobject_cast<QPushButton *>(v.controles[1]);
        QAction *action = barre && barre->actions().size() ? barre->actions()[0]->menu()->actions().value(0) : nullptr;
        VERIFIER(b && action && action->text() == "Nouveau");
        if (b && action) {
            int clics = 0;
            QObject::connect(b, &QPushButton::clicked, [&] { clics++; });
            action->trigger();
            VERIFIER(clics == 1);
            b->setEnabled(false);
            VERIFIER(!action->isEnabled());
            b->setEnabled(true);
            b->setVisible(false);
            VERIFIER(!action->isVisible());
        }
        delete v.vue;
    }
    {   // le menu dans l'atelier : la palette l'ajoute avec son événement ; un clic sur son titre le choisit, et « Ajouter » y
        // pose un bouton, avec son événement ; la conversion en position libre le laisse en tête, hors du bloc libre
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    le texte « t ».\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        Geste g;
        QString r = ecran_ajouter_menu(dossier.path(), "x", &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        VERIFIER(lire().contains("L'écran x montre :\n    le menu « Menu » :\n        un bouton « Nouvel élément »,\n    le texte « t ».")
                 && lire().contains("Quand on clique sur « Nouvel élément » dans l'écran x :\n    Remarque : à écrire."));
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        QString resultat = "pas de geste";
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        o.montrer(dossier.path());
        QApplication::processEvents();
        QMenuBar *barre = nullptr;
        for (int k = 0; k < 10 && !barre; k++) if (o.controle(k)) barre = o.controle(k)->window()->findChild<QMenuBar *>();
        VERIFIER(barre != nullptr);
        if (barre && !barre->actions().isEmpty()) {
            const QPoint c = barre->actionGeometry(barre->actions()[0]).center();
            QMouseEvent appui(QEvent::MouseButtonPress, QPointF(c), QPointF(barre->mapToGlobal(c)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(barre, &appui);
            QMouseEvent lache(QEvent::MouseButtonRelease, QPointF(c), QPointF(barre->mapToGlobal(c)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(barre, &lache);
            if (QMenu *m = barre->actions()[0]->menu()) m->hide();
        }
        VERIFIER(o.cible_ajout() == 0);   // le menu choisi : « Ajouter » y pose
        ElementNouveau n;
        n.sorte = ELEMENT_BOUTON;
        n.texte = "Liste";
        r = ecran_ajouter(dossier.path(), "x", n, &g, o.cible_ajout());
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        VERIFIER(lire().contains("        un bouton « Nouvel élément »,\n        un bouton « Liste »,\n    le texte « t ».")
                 && lire().contains("Quand on clique sur « Liste » dans l'écran x :"));
        // la conversion en position libre : le menu reste en tête, hors du bloc libre
        QVector<QRect> places(5);   // le menu, ses deux boutons, sa fin, le texte
        places[4] = QRect(0, 0, 40, 24);   // le texte, seule unité placée
        r = ecran_liberer(dossier.path(), "x", places, &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        VERIFIER(lire().contains("    le menu « Menu » :\n        un bouton « Nouvel élément »,\n        un bouton « Liste »,\n"
                                 "    en position libre :\n        le texte « t », à 0 de la gauche et 0 du haut, large de 40."));
    }
    {   // un écran décrit avec un graphique devant un texte : glisser le texte déplace le texte, pas le graphique (le graphique
        // n'est pas un bloc ; compté comme tel, il passait pour le bloc du premier niveau qui contenait le texte)
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Un état, conservé, a :\n    un nom (texte), unique.\n"
                    "L'écran x montre :\n    le graphique des états conservés, par nom,\n    le texte « bouge ».\n");
        }
        OngletEcrans o;
        o.resize(900, 700);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        o.montrer(dossier.path());
        QApplication::processEvents();
        QLabel *l = nullptr;
        for (int k = 0; k < 10; k++) if (auto *x = qobject_cast<QLabel *>(o.controle(k)); x && x->text() == "bouge") l = x;
        VERIFIER(l != nullptr);
        if (l) {
            const QPoint m(3, 3), a = l->mapToGlobal(m);
            QMouseEvent appui(QEvent::MouseButtonPress, QPointF(m), QPointF(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(l, &appui);
            QMouseEvent loin(QEvent::MouseMove, QPointF(m), QPointF(a + QPoint(0, 30)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(l, &loin);
            QMouseEvent bouge(QEvent::MouseMove, QPointF(m), QPointF(a + QPoint(300, 0)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(l, &bouge);
            QMouseEvent lache(QEvent::MouseButtonRelease, QPointF(m), QPointF(a + QPoint(300, 0)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(l, &lache);
        }
        QFile f(chemin);
        VERIFIER(f.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(f.readAll());
        QRegularExpressionMatch m = QRegularExpression("le texte « bouge », à (\\d+) de la gauche").match(t);
        QRegularExpressionMatch mg = QRegularExpression("le graphique des états conservés, par nom, à (\\d+) de la gauche").match(t);
        VERIFIER(m.hasMatch() && mg.hasMatch() && m.captured(1).toInt() >= 280 && mg.captured(1).toInt() < 40);
        if (!m.hasMatch()) std::printf("%s\n", qPrintable(t));
    }
    {   // un graphique devant des onglets ne fausse plus la page ouverte ni la cible d'« Ajouter » ; un bouton posé dans un
        // onglet arrive avec son événement
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Un état, conservé, a :\n    un nom (texte), unique.\n"
                    "L'écran x montre :\n    le graphique des états conservés, par nom,\n    le texte « t ».\n");
        }
        Geste g;
        VERIFIER(ecran_ajouter_onglets(dossier.path(), "x", &g).isEmpty());
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        o.montrer(dossier.path());
        QApplication::processEvents();
        int ko = -1;
        for (int k = 0; k < o.ecrans()[0].elements.size(); k++) if (o.ecrans()[0].elements[k].sorte == ELEMENT_ONGLETS) ko = k;
        o.choisir_element(ko);
        const int dans = o.cible_ajout();
        VERIFIER(dans > ko && o.ecrans()[0].elements.value(dans).sorte == ELEMENT_ONGLET && o.ecrans()[0].elements.value(dans).texte == "Premier");
        ElementNouveau n;
        n.sorte = ELEMENT_BOUTON;
        n.texte = "Dedans";
        const QString r = ecran_ajouter(dossier.path(), "x", n, &g, dans);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        QFile f(chemin);
        VERIFIER(f.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(f.readAll());
        VERIFIER(r.isEmpty() && t.contains("un bouton « Dedans »") && t.contains("Quand on clique sur « Dedans » dans l'écran x :"));
    }
    {   // le graphique dans le panneau (§ 22.11) : regroupé par, valeur, forme ; le « dont », la clé à deux étapes et la place
        // restent quand on change la forme et la valeur
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Une catégorie, conservée, a :\n    un nom (texte), unique.\n"
                    "Un membre, conservé, a :\n    un nom (texte),\n    une catégorie (catégorie).\n"
                    "Une cotisation, conservée, a :\n    un membre (membre),\n    un montant (nombre).\n"
                    "L'écran x montre :\n    en position libre :\n"
                    "        le graphique des cotisations conservées dont le montant est supérieur à 0, par catégorie du membre, "
                    "à 8 de la gauche et 16 du haut, large de 400.\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        OngletEcrans o;
        o.resize(1000, 700);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        o.montrer(dossier.path());
        QApplication::processEvents();
        VERIFIER(o.ecrans().value(0).elements.value(1).graphe_cle.contains(QChar(0x1d)));
        o.choisir_element(1);
        QComboBox *par = nullptr, *valeur = nullptr, *forme = nullptr;
        for (QComboBox *c : o.findChildren<QComboBox *>()) {
            if (c->findText("Secteurs") >= 0) forme = c;
            if (c->findText("Le nombre d'objets") >= 0) valeur = c;
            if (c->currentText() == "catégorie du membre") par = c;
        }
        VERIFIER(par && valeur && forme);
        QPushButton *appliquer = nullptr;
        for (QPushButton *b : o.findChildren<QPushButton *>()) if (b->text() == "Appliquer" && !b->isHidden()) appliquer = b;
        VERIFIER(appliquer != nullptr);
        if (valeur && forme && appliquer) {
            valeur->setCurrentIndex(valeur->findText("La somme de « montant »"));
            forme->setCurrentIndex(1);
            appliquer->click();
        }
        if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
        VERIFIER(lire().contains("        le graphique des cotisations conservées dont le montant est supérieur à 0, par catégorie du membre, "
                                 "avec la somme du montant, en secteurs, à 8 de la gauche et 16 du haut, large de 400."));
        if (!lire().contains("en secteurs")) std::printf("%s\n", qPrintable(lire()));
    }
    {   // la période dans le panneau (§ 22.11) : lue du fichier, changée, retirée ; seule une date en a une
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Une tâche, conservée, a :\n    un nom (texte),\n    une échéance (date),\n    un départ (date).\n"
                    "L'écran x montre :\n    le graphique des tâches conservées, par mois de l'échéance.\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        OngletEcrans o;
        o.resize(1000, 700);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        auto panneau = [&](QComboBox **par, QComboBox **periode, QPushButton **appliquer) {
            o.montrer(dossier.path());
            QApplication::processEvents();
            o.choisir_element(0);
            *par = *periode = nullptr;
            *appliquer = nullptr;
            for (QComboBox *c : o.findChildren<QComboBox *>()) {
                if (c->findText("Par mois") >= 0) *periode = c;
                if (c->findText(QString::fromUtf8("échéance")) >= 0 && c->findText("nom") >= 0) *par = c;
            }
            for (QPushButton *b : o.findChildren<QPushButton *>()) if (b->text() == "Appliquer" && !b->isHidden()) *appliquer = b;
            return *par && *periode && *appliquer;
        };
        QComboBox *par, *periode;
        QPushButton *appliquer;
        VERIFIER(panneau(&par, &periode, &appliquer));
        if (par && periode && appliquer) {
            VERIFIER(par->currentText() == QString::fromUtf8("échéance") && periode->currentText() == "Par mois" && periode->isEnabled());
            periode->setCurrentIndex(periode->findText(QString::fromUtf8("Par année")));
            appliquer->click();
            VERIFIER(resultat.isEmpty() && lire().contains(QString::fromUtf8("le graphique des tâches conservées, par année de l'échéance.")));
        }
        VERIFIER(panneau(&par, &periode, &appliquer));
        if (par && periode && appliquer) {
            VERIFIER(periode->currentText() == QString::fromUtf8("Par année"));
            par->setCurrentIndex(par->findText(QString::fromUtf8("départ")));
            VERIFIER(periode->isEnabled());
            periode->setCurrentIndex(periode->findText("Par semaine"));
            appliquer->click();
            VERIFIER(resultat.isEmpty() && lire().contains(QString::fromUtf8("le graphique des tâches conservées, par semaine du départ.")));
        }
        VERIFIER(panneau(&par, &periode, &appliquer));
        if (par && periode && appliquer) {
            par->setCurrentIndex(par->findText("nom"));   // un texte : pas de période
            VERIFIER(!periode->isEnabled() && periode->currentIndex() == 0);
            appliquer->click();
            VERIFIER(resultat.isEmpty() && lire().contains(QString::fromUtf8("le graphique des tâches conservées, par nom.")));
        }
        VERIFIER(panneau(&par, &periode, &appliquer));
        if (par && periode && appliquer) {
            par->setCurrentIndex(par->findText(QString::fromUtf8("échéance")));
            appliquer->click();   // « Chaque date » : une barre par date
            VERIFIER(resultat.isEmpty() && lire().contains(QString::fromUtf8("le graphique des tâches conservées, par échéance.")));
        }
    }
    {   // d'un bloc libre à un autre : un texte sort d'une page d'onglet vers l'écran ; un texte de l'écran entre dans le bloc
        // libre d'un groupe ; chacun là où il est lâché
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    en position libre :\n"
                    "        le texte « dehors », à 600 de la gauche et 0 du haut,\n"
                    "        le groupe « G », à 600 de la gauche et 100 du haut, large de 300, haut de 200 :\n"
                    "            en position libre :\n                le texte « fond », à 8 de la gauche et 8 du haut,\n"
                    "        les onglets, à 0 de la gauche et 0 du haut, large de 400, haut de 300 :\n"
                    "            l'onglet « Un » :\n                en position libre :\n"
                    "                    le texte « reste », à 16 de la gauche et 16 du haut,\n"
                    "                    le texte « sort », à 16 de la gauche et 64 du haut.\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        OngletEcrans o;
        o.resize(1200, 800);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        auto glisser = [&](const QString &texte, const QString &dans, QPoint vers) {   // « dans » : un texte du bloc visé
            o.montrer(dossier.path());
            QApplication::processEvents();
            QLabel *l = nullptr;
            QWidget *vers_toile = nullptr;
            for (int k = 0; k < 30; k++) {
                if (auto *x = qobject_cast<QLabel *>(o.controle(k)); x && x->text() == texte) l = x;
                if (auto *x = qobject_cast<QLabel *>(o.controle(k)); x && x->text() == dans) vers_toile = x->parentWidget();
            }
            VERIFIER(l != nullptr);
            if (!l || !vers_toile) return;
            const QPoint m(3, 3), a = l->mapToGlobal(m), cible = vers_toile->mapToGlobal(vers) + m;
            QMouseEvent appui(QEvent::MouseButtonPress, QPointF(m), QPointF(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(l, &appui);
            QMouseEvent loin(QEvent::MouseMove, QPointF(m), QPointF(a + QPoint(0, 30)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(l, &loin);
            QMouseEvent bouge(QEvent::MouseMove, QPointF(m), QPointF(cible), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(l, &bouge);
            QMouseEvent lache(QEvent::MouseButtonRelease, QPointF(m), QPointF(cible), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(l, &lache);
            if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
        };
        // 1. « sort » quitte la page de l'onglet pour l'écran, à 424 × 312 du bloc libre de l'écran (à droite des onglets,
        // sous le groupe, dans la zone visible de l'aperçu)
        glisser("sort", "dehors", QPoint(424, 312));
        QString t = lire();
        QRegularExpressionMatch m = QRegularExpression("\n        le texte « sort », à (\\d+) de la gauche et (\\d+) du haut").match(t);
        VERIFIER(m.hasMatch() && qAbs(m.captured(1).toInt() - 424) <= 8 && qAbs(m.captured(2).toInt() - 312) <= 8);
        VERIFIER(t.count("« sort »") == 1 && t.contains("le texte « reste »"));
        // 2. « dehors » entre dans le bloc libre du groupe, à 100 × 80 de ce bloc
        glisser("dehors", "fond", QPoint(100, 80));
        t = lire();
        m = QRegularExpression("en position libre :\n                le texte « fond », à 8 de la gauche et 8 du haut,\n"
                               "                le texte « dehors », à (\\d+) de la gauche et (\\d+) du haut").match(t);
        VERIFIER(m.hasMatch() && qAbs(m.captured(1).toInt() - 100) <= 8 && qAbs(m.captured(2).toInt() - 80) <= 8);
        if (!m.hasMatch()) std::printf("%s\n", qPrintable(t));
    }
    {   // l'interrupteur s'ajoute et se règle dans l'atelier ; une image glissée depuis le Finder entre dans l'écran, copiée
        QTemporaryDir dossier, ailleurs;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Un genre, conservé, a :\n    un nom (texte), unique.\n"
                    "L'écran x montre :\n    des genres (genre),\n    le texte « t ».\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        Geste g;
        ElementNouveau n;
        n.sorte = ELEMENT_ZONE;
        n.type = "vrai ou faux";
        n.texte = "actif";
        n.interrupteur = true;
        QString r = ecran_ajouter(dossier.path(), "x", n, &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        VERIFIER(lire().contains("    un actif (vrai ou faux), en interrupteur."));
        // le panneau réécrit une zone multiple sans perdre sa marque
        ElementNouveau m;
        m.sorte = ELEMENT_ZONE;
        m.type = "genre";
        m.texte = "genres";
        m.multiple = true;
        VERIFIER(ecran_modifier(dossier.path(), "x", 0, m, &g).isEmpty() && lire().contains("    des genres (genre),"));
        // une image d'ailleurs, lâchée sur l'aperçu
        const QString png = ailleurs.filePath("vacances.png");
        {
            QFile f(png);
            VERIFIER(f.open(QIODevice::WriteOnly));
            QFile source(QString(GRYM_SOURCE) + "/exemples/pixel.png");
            VERIFIER(source.open(QIODevice::ReadOnly));
            f.write(source.readAll());
        }
        // la copie dans le projet, puis l'ajout (le dépôt depuis le Finder passe par là ; un dépôt fabriqué dans l'essai
        // arrive sans ses données, il ne s'éprouve pas ici)
        QString erreur;
        ElementNouveau im;
        im.sorte = ELEMENT_IMAGE;
        im.texte = OngletEcrans::image_dans_le_projet(dossier.path(), png, &erreur);
        VERIFIER(im.texte == "vacances.png" && erreur.isEmpty());
        VERIFIER(ecran_ajouter(dossier.path(), "x", im, &g).isEmpty());
        VERIFIER(lire().contains("l'image « vacances.png »") && QFile::exists(dossier.filePath("vacances.png")));
    }
    {   // des choix multiples (§ 22.1) : une case par choix, chacune porte sa clé ; l'aperçu sans données en montre une
        QVector<ElementVue> el(2);
        el[0].sorte = ELEMENT_ZONE;
        el[0].texte = "Genres";
        el[0].type = "genre";
        el[0].multiple = true;
        el[0].choix = {"baroque", "sacré", "fugue"};
        el[1] = el[0];
        el[1].choix.clear();
        VueEcran v = dessiner_ecran("Saisie", el);
        QWidget *z = v.zones.value(0);
        VERIFIER(z && z->property("multiple").toBool() && z->findChildren<QCheckBox *>().size() == 3);
        if (z) VERIFIER(z->findChildren<QCheckBox *>().value(1)->property("cle").toString() == QString::fromUtf8("sacré"));
        VERIFIER(v.zones.value(1) && v.zones.value(1)->findChildren<QCheckBox *>().size() == 1);
        delete v.vue;
    }
    {   // l'interrupteur (§ 22.6) : une case à cocher (le câblage d'une case reste), dessinée en interrupteur
        QVector<ElementVue> el(1);
        el[0].sorte = ELEMENT_ZONE;
        el[0].texte = "Actif";
        el[0].type = "vrai ou faux";
        el[0].interrupteur = true;
        VueEcran v = dessiner_ecran("Saisie", el);
        auto *c = qobject_cast<QCheckBox *>(v.zones.value(0));
        VERIFIER(c && c->objectName() == "interrupteur" && !c->isChecked());
        if (c) {
            c->click();
            VERIFIER(c->isChecked());
            QImage img(c->sizeHint(), QImage::Format_ARGB32);
            c->resize(c->sizeHint());
            c->render(&img);
            VERIFIER(!img.isNull() && c->sizeHint().width() > 44);
        }
        delete v.vue;
    }
    {   // une zone de fichier ou d'image : un bouton « Choisir un fichier… », qui sait s'il n'accepte que des images
        QVector<ElementVue> el(2);
        el[0].sorte = ELEMENT_ZONE;
        el[0].texte = "Photo";
        el[0].type = "image";
        el[1].sorte = ELEMENT_ZONE;
        el[1].texte = "Contrat";
        el[1].type = "fichier";
        VueEcran v = dessiner_ecran("Saisie", el);
        auto *photo = qobject_cast<QPushButton *>(v.zones.value(0)), *contrat = qobject_cast<QPushButton *>(v.zones.value(1));
        VERIFIER(photo && photo->property("fichier").toBool() && photo->property("image").toBool());
        VERIFIER(contrat && contrat->property("fichier").toBool() && !contrat->property("image").toBool());
        VERIFIER(photo && photo->text() == QString::fromUtf8("Choisir un fichier…"));
        delete v.vue;
    }
    {   // le titre garde sa hauteur naturelle dans un aperçu plus haut que son contenu ; le clic droit le renomme
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran de recherche, « Recherche », montre :\n    le texte « t ».\n");
        }
        OngletEcrans o;
        o.resize(1000, 1400);   // bien plus haut que l'écran
        o.show();
        o.montrer(dossier.path());
        QApplication::processEvents();
        QLabel *titre = nullptr;
        for (QLabel *l : o.findChildren<QLabel *>()) if (l->text() == "Recherche" && l->property("niveau").toString() == "ecran") titre = l;
        VERIFIER(titre && titre->height() <= titre->sizeHint().height() + 2);
        QMenu *m = o.menu_du_titre();   // les trois tailles, la normale cochée, puis « Renommer le titre… »
        VERIFIER(m->actions().size() == 5 && m->actions()[1]->isChecked() && m->actions()[4]->text() == QString::fromUtf8("Renommer le titre…"));
        delete m;
        const int normal = titre ? titre->sizeHint().height() : 0;
        Geste gt;
        VERIFIER(ecran_taille_titre(dossier.path(), "de recherche", 1, &gt).isEmpty());
        o.montrer(dossier.path());
        QApplication::processEvents();
        titre = nullptr;
        for (QLabel *l : o.findChildren<QLabel *>()) if (l->text() == "Recherche" && l->property("niveau").toString() == "ecran") titre = l;
        VERIFIER(titre && titre->sizeHint().height() > normal);   // en grand : plus haut
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains("L'écran de recherche, « Recherche » en grand, montre :"));
        }
        Geste g;
        VERIFIER(ecran_titre(dossier.path(), "de recherche", "Chercher un membre", &g).isEmpty());
        QFile f(chemin);
        VERIFIER(f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains("L'écran de recherche, « Chercher un membre » en grand, montre :"));   // la taille reste
    }
    {   // l'unité (§ 22.6) : à côté du champ, du bon côté ; le panneau l'écrit
        QVector<ElementVue> el(2);
        el[0].sorte = el[1].sorte = ELEMENT_ZONE;
        el[0].texte = "Montant";
        el[0].type = el[1].type = "nombre";
        el[0].unite = "CHF";
        el[0].unite_devant = true;
        el[1].texte = "Remise";
        el[1].unite = "%";
        VueEcran v = dessiner_ecran("Saisie", el);
        auto unite_de = [&](int k) -> QLabel * {
            QWidget *c = v.controles.value(k);
            return c ? c->findChild<QLabel *>("unite") : nullptr;
        };
        QLabel *chf = unite_de(0), *pc = unite_de(1);
        QWidget *z0 = v.zones.value(0), *z1 = v.zones.value(1);
        v.vue->resize(500, 300);
        v.vue->show();
        QApplication::processEvents();
        VERIFIER(chf && chf->text() == "CHF" && z0 && chf->geometry().left() < z0->geometry().left());   // devant
        VERIFIER(pc && pc->text() == "%" && z1 && pc->geometry().left() > z1->geometry().left());      // derrière
        delete v.vue;
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    un montant (nombre),\n    le texte « t ».\n");
        }
        Geste g;
        ElementNouveau n;
        n.sorte = ELEMENT_ZONE;
        n.texte = "montant";
        n.type = "nombre";
        n.unite = "CHF";
        n.unite_devant = true;
        VERIFIER(ecran_modifier(dossier.path(), "x", 0, n, &g).isEmpty());
        QFile f(chemin);
        VERIFIER(f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains("    un montant (nombre), avec l'unité « CHF » devant,"));
    }
    {   // le masque (§ 22.6) : la frappe cachée
        QVector<ElementVue> el(1);
        el[0].sorte = ELEMENT_ZONE;
        el[0].texte = "Code";
        el[0].type = "texte";
        el[0].masque = true;
        VueEcran v = dessiner_ecran("Saisie", el);
        auto *le = qobject_cast<QLineEdit *>(v.zones.value(0));
        VERIFIER(le && le->echoMode() == QLineEdit::Password);
        delete v.vue;
    }
    {   // les bornes (§ 22.6) : la frappe d'un texte s'arrête à sa longueur ; le panneau les écrit
        QVector<ElementVue> el(1);
        el[0].sorte = ELEMENT_ZONE;
        el[0].texte = "Code";
        el[0].type = "texte";
        el[0].longueur_max = 4;
        VueEcran v = dessiner_ecran("Saisie", el);
        auto *le = qobject_cast<QLineEdit *>(v.zones.value(0));
        VERIFIER(le && le->maxLength() == 4);
        delete v.vue;
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    une quantité (nombre),\n    le texte « t ».\n");
        }
        Geste g;
        ElementNouveau n;
        n.sorte = ELEMENT_ZONE;
        n.texte = QString::fromUtf8("quantité");
        n.type = "nombre";
        n.feminin = true;
        n.borne_min = QString::fromUtf8("−2,5");
        n.borne_max = "99";
        const QString r = ecran_modifier(dossier.path(), "x", 0, n, &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        QFile f(chemin);
        VERIFIER(f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains(QString::fromUtf8("    une quantité (nombre), de −2,5 à 99,")));
    }
    {   // la recherche (§ 22.6) : demandée, ou au-delà de vingt objets ; les objets qui contiennent la saisie sont proposés
        QVector<ElementVue> el(3);
        for (auto &e : el) { e.sorte = ELEMENT_ZONE; e.texte = "Membre"; e.type = "membre"; }
        el[0].choix = {"Anne", "Bruno"};
        el[0].recherche = true;
        for (int i = 0; i < 25; i++) el[1].choix << QString("M%1").arg(i);
        el[2].choix = {"Anne", "Bruno"};
        VueEcran v = dessiner_ecran("Saisie", el);
        auto *demandee = qobject_cast<QComboBox *>(v.zones.value(0)), *auto_ = qobject_cast<QComboBox *>(v.zones.value(1)),
             *menu = qobject_cast<QComboBox *>(v.zones.value(2));
        VERIFIER(demandee && demandee->isEditable() && demandee->completer()->filterMode() == Qt::MatchContains);
        VERIFIER(auto_ && auto_->isEditable());
        VERIFIER(menu && !menu->isEditable());   // deux objets, rien de demandé : le menu ordinaire
        delete v.vue;
    }
    {   // l'indication (§ 22.6) : le panneau l'écrit, l'aperçu la montre sous le libellé
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    un code (texte),\n    le texte « t ».\n");
        }
        Geste g;
        ElementNouveau n;
        n.sorte = ELEMENT_ZONE;
        n.texte = "code";
        n.type = "texte";
        n.indication = "4 chiffres, sans espace";
        VERIFIER(ecran_modifier(dossier.path(), "x", 0, n, &g).isEmpty());
        QFile f(chemin);
        VERIFIER(f.open(QIODevice::ReadOnly)
                 && QString::fromUtf8(f.readAll()).contains("    un code (texte), avec l'indication « 4 chiffres, sans espace »,"));
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        o.montrer(dossier.path());
        QApplication::processEvents();
        QLabel *ind = o.findChild<QLabel *>("indication");
        VERIFIER(ind && ind->text() == "4 chiffres, sans espace");
        VERIFIER(o.ecrans().value(0).elements.value(0).indication == "4 chiffres, sans espace");
    }
    {   // le clic droit sur un élément propose d'abord de le supprimer ; sur le fond de l'écran, non
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    le texte « garde »,\n    le texte « part ».\n");
        }
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        o.montrer(dossier.path());
        QApplication::processEvents();
        QMenu *fond = o.menu_des_champs(QPoint(0, 0), -1);
        bool propose = false;
        for (QAction *x : fond->actions()) propose |= x->text().startsWith("Supprimer");
        VERIFIER(!propose);
        delete fond;
        QMenu *m = o.menu_des_champs(QPoint(0, 0), 1);
        QAction *sup = nullptr;
        for (QAction *x : m->actions()) if (x->text() == QString::fromUtf8("Supprimer « part »")) sup = x;
        VERIFIER(sup != nullptr);
        if (sup) sup->trigger();
        delete m;
        if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
        QFile f(chemin);
        VERIFIER(f.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(f.readAll());
        VERIFIER(t.contains("le texte « garde »") && !t.contains("« part »"));
    }
    {   // le clic droit : les champs des entités ; chacun devient la zone qui lui correspond, là où l'on a cliqué
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Un genre, conservé, a :\n    un nom (texte), unique.\n"
                    "Une œuvre, conservée, a :\n    un titre (texte),\n    un genre principal (genre),\n    des genres (genre).\n"
                    "L'écran x montre :\n    en position libre :\n        le texte « t », à 0 de la gauche et 0 du haut.\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        OngletEcrans o;
        o.resize(1000, 700);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        auto choisir = [&](const QString &entite, const QString &champ) {
            o.montrer(dossier.path());
            QApplication::processEvents();
            QWidget *toile = o.controle(0);
            VERIFIER(toile != nullptr);
            if (!toile) return;
            QMenu *m = o.menu_des_champs(toile->mapToGlobal(QPoint(200, 96)));
            QAction *a = nullptr;
            for (QAction *x : m->actions())
                if (x->menu() && x->text() == entite)
                    for (QAction *y : x->menu()->actions()) if (y->text() == champ) a = y;
            VERIFIER(a != nullptr);
            if (a) a->trigger();
            if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
            delete m;
        };
        {   // la palette et le clic droit proposent les mêmes éléments, dans le même ordre, les plus récents compris
            const QStringList attendus = {"Bouton", "Texte", "Zone", "Interrupteur", "Choix multiples", "Fichier", "Son", "Liste",
                                          "Graphique", "Image", "Onglets", "Menu"};
            QStringList palette;
            for (QPushButton *b : o.findChildren<QPushButton *>()) if (attendus.contains(b->text())) palette << b->text();
            VERIFIER(palette == attendus);
            o.montrer(dossier.path());
            QApplication::processEvents();
            QMenu *mc = o.menu_des_champs(o.controle(0)->mapToGlobal(QPoint(10, 10)));
            QStringList proposes;
            QAction *onglets = nullptr;
            for (QAction *x : mc->actions()) {
                if (attendus.contains(x->text())) proposes << x->text();
                if (x->text() == "Onglets") onglets = x;
            }
            VERIFIER(proposes == attendus);
            if (onglets) onglets->trigger();   // sans boîte de dialogue : l'ajout se fait d'un coup
            VERIFIER(lire().contains("les onglets"));
            delete mc;
        }
        choisir(QString::fromUtf8("œuvre"), "un titre (texte)");
        QRegularExpressionMatch m = QRegularExpression("        un titre \\(texte\\), à (\\d+) de la gauche et (\\d+) du haut").match(lire());
        VERIFIER(m.hasMatch() && qAbs(m.captured(1).toInt() - 200) <= 8 && qAbs(m.captured(2).toInt() - 96) <= 8);
        choisir(QString::fromUtf8("œuvre"), "un genre principal (genre)");
        VERIFIER(lire().contains("un genre principal (genre)"));
        choisir(QString::fromUtf8("œuvre"), "des genres (genre)");
        VERIFIER(lire().contains("des genres (genre)"));
        if (!m.hasMatch()) std::printf("%s\n", qPrintable(lire()));
    }
    {   // le graphique (§ 22.11) : fabriqué par la palette, dessiné en exemple dans l'aperçu, rempli par les lignes
        QTemporaryDir dossier;
        const QString chemin = dossier.filePath("p.grym");
        {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("Un état, conservé, a :\n    un nom (texte), unique.\n"
                    "Une tâche, conservée, a :\n    un titre (texte),\n    un état (état), facultatif,\n    une durée (nombre).\n"
                    "L'écran x montre :\n    le texte « t ».\n");
        }
        auto lire = [&]() {
            QFile f(chemin);
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        Geste g;
        ElementNouveau n;
        n.sorte = ELEMENT_GRAPHIQUE;
        n.texte = "tâche";
        n.cle = "état";
        QString r = ecran_ajouter(dossier.path(), "x", n, &g);
        if (!r.isEmpty()) std::printf("résultat : %s\n", qPrintable(r));
        VERIFIER(lire().contains("    le graphique des tâches conservées, par état."));
        n.cle = "état";
        n.somme = "durée";
        n.secteurs = true;
        VERIFIER(ecran_modifier(dossier.path(), "x", 1, n, &g).isEmpty());
        VERIFIER(lire().contains("    le graphique des tâches conservées, par état, avec la somme de la durée, en secteurs."));
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        o.montrer(dossier.path());
        QApplication::processEvents();
        VERIFIER(o.ecrans().size() == 1 && o.ecrans()[0].elements.value(1).sorte == ELEMENT_GRAPHIQUE
                 && (o.ecrans()[0].elements.value(1).graphique & 6) == 6);
        auto *gr = dynamic_cast<Graphique *>(o.controle(1));
        VERIFIER(gr && gr->libelles().size() == 3);   // l'exemple de l'aperçu
        if (gr) {
            gr->donner({"à faire", "fait"}, {nombre_affiche("1'234.5"), nombre_affiche("12,5")});
            VERIFIER(gr->valeurs() == QVector<double>({1234.5, 12.5}));
            QImage img(gr->size(), QImage::Format_ARGB32);
            gr->render(&img);   // le dessin passe sans accroc
            VERIFIER(!img.isNull());
        }
    }
    {   // les guides : glissé près du bord gauche d'un voisin (20, hors de la grille de 8), le texte s'y colle, trait montré ;
        // avec Alt, ni guide ni grille
        QTemporaryDir dossier;
        QFile f(dossier.filePath("p.grym"));
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write("L'écran d'accueil montre :\n    en position libre :\n        un bouton « A », à 20 de la gauche et 20 du haut,\n"
                "        le texte « bien plus long que le bouton », à 200 de la gauche et 120 du haut.\n"
                "Quand on clique sur « A » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        f.close();
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        auto glisser = [&](int k, QPoint de, Qt::KeyboardModifiers m, bool lacher, bool *guide) {
            QWidget *w = o.controle(k);
            if (!w) return;
            const QPoint depart = w->mapToGlobal(QPoint(w->width() / 2, w->height() / 2));
            const QPointF local = w->mapFromGlobal(depart);
            QMouseEvent appui(QEvent::MouseButtonPress, local, QPointF(depart), Qt::LeftButton, Qt::LeftButton, m);
            QApplication::sendEvent(w, &appui);
            QMouseEvent loin(QEvent::MouseMove, local, QPointF(depart + QPoint(0, 60)), Qt::NoButton, Qt::LeftButton, m);
            QApplication::sendEvent(w, &loin);   // au-delà du seuil d'un glisser, puis là où l'on veut
            QMouseEvent bouge(QEvent::MouseMove, local, QPointF(depart + de), Qt::NoButton, Qt::LeftButton, m);
            QApplication::sendEvent(w, &bouge);
            if (guide) *guide = o.guide_visible() != nullptr;
            if (!lacher) return;
            QMouseEvent lache(QEvent::MouseButtonRelease, local, QPointF(depart + de), Qt::LeftButton, Qt::NoButton, m);
            QApplication::sendEvent(w, &lache);
        };
        o.montrer(dossier.path());
        QApplication::processEvents();
        bool guide = false;
        glisser(2, QPoint(23 - 200, 80), Qt::NoModifier, true, &guide);   // gauche 23 (grille : 24), haut 200
        VERIFIER(guide);
        VERIFIER(o.guide_visible() == nullptr);   // effacés au lâcher
        QFile r(dossier.filePath("p.grym"));
        VERIFIER(r.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(r.readAll());
        VERIFIER(t.contains("le texte « bien plus long que le bouton », à 20 de la gauche et 200 du haut."));
        if (!t.contains("à 20 de la gauche et 200")) std::printf("%s\n", qPrintable(t));
        o.montrer(dossier.path());
        QApplication::processEvents();
        guide = true;
        glisser(2, QPoint(3, 0), Qt::AltModifier, true, &guide);   // Alt : 23 au pixel, pas de guide
        VERIFIER(!guide);
        QFile r2(dossier.filePath("p.grym"));
        VERIFIER(r2.open(QIODevice::ReadOnly));
        VERIFIER(QString::fromUtf8(r2.readAll()).contains("à 23 de la gauche et 200 du haut."));
    }
    {   // agrandir à la souris par le coin, l'événement livré au widget réellement sous la souris (un bouton, une zone)
        QTemporaryDir dossier;
        QFile f(dossier.filePath("p.grym"));
        VERIFIER(f.open(QIODevice::WriteOnly));
        f.write("L'écran d'accueil montre :\n    en position libre :\n        un bouton « A », à 16 de la gauche et 16 du haut,\n"
                "        une note (texte), à 16 de la gauche et 80 du haut.\n"
                "Quand on clique sur « A » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        f.close();
        OngletEcrans o;
        o.resize(900, 600);
        o.show();
        QString resultat = "pas de geste";
        Geste g;
        QObject::connect(&o, &OngletEcrans::geste, [&](const std::function<QString(Geste *)> &faire) { resultat = faire(&g); });
        for (int k : {1, 2}) {
            o.montrer(dossier.path());
            QApplication::processEvents();
            QWidget *w = o.controle(k);
            VERIFIER(w != nullptr);
            if (!w) continue;
            const QSize avant = w->size();
            const QPoint coin = w->mapToGlobal(QPoint(w->width() - 3, w->height() - 3));
            QWidget *sous = QApplication::widgetAt(coin);
            if (!sous) sous = w->childAt(w->width() - 3, w->height() - 3);
            if (!sous) sous = w;
            (void)avant;
            const QPointF local = sous->mapFromGlobal(coin);
            QMouseEvent appui(QEvent::MouseButtonPress, local, QPointF(coin), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(sous, &appui);
            QMouseEvent bouge(QEvent::MouseMove, local, QPointF(coin + QPoint(40, 30)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(sous, &bouge);
            QMouseEvent lache(QEvent::MouseButtonRelease, local, QPointF(coin + QPoint(40, 30)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(sous, &lache);
            if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
            QFile r(dossier.filePath("p.grym"));
            VERIFIER(r.open(QIODevice::ReadOnly));
            const QString t = QString::fromUtf8(r.readAll());
            VERIFIER(t.contains(k == 1 ? "un bouton « A », à 16 de la gauche et 16 du haut, large de" : "une note (texte), à 16 de la gauche et 80 du haut, large de"));
        }
        // le bord droit seul : la largeur, pas la hauteur ; au survol, le curseur l'annonce
        o.montrer(dossier.path());
        QApplication::processEvents();
        if (QWidget *w = o.controle(2)) {
            const QPoint bord = w->mapToGlobal(QPoint(w->width() - 4, w->height() / 2 - 8));
            QWidget *sous = w->childAt(w->mapFromGlobal(bord));
            if (!sous) sous = w;
            const QPointF local = sous->mapFromGlobal(bord);
            QMouseEvent survol(QEvent::MouseMove, local, QPointF(bord), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(sous, &survol);
            VERIFIER(sous->cursor().shape() == Qt::SizeHorCursor);
            const int h = w->height();
            QMouseEvent appui(QEvent::MouseButtonPress, local, QPointF(bord), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(sous, &appui);
            QMouseEvent bouge(QEvent::MouseMove, local, QPointF(bord + QPoint(48, 40)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(sous, &bouge);
            VERIFIER(w->height() == h);
            QMouseEvent lache(QEvent::MouseButtonRelease, local, QPointF(bord + QPoint(48, 40)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(sous, &lache);
            QFile r(dossier.filePath("p.grym"));
            VERIFIER(r.open(QIODevice::ReadOnly));
            VERIFIER(QString::fromUtf8(r.readAll()).contains("une note (texte), à 16 de la gauche et 80 du haut, large de 232, haute de 80."));
        }
        // un écran décrit : glisser un élément le passe en position libre, l'élément à sa nouvelle place ; un fantôme
        // et la grille pendant le geste ; les autres gardent ce qu'on voit
        {
            QFile d(dossier.filePath("p.grym"));
            VERIFIER(d.open(QIODevice::WriteOnly | QIODevice::Truncate));
            d.write("L'écran d'accueil montre :\n    le texte « t »,\n    un bouton « A ».\n"
                    "Quand on clique sur « A » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        }
        o.montrer(dossier.path());
        QApplication::processEvents();
        if (QWidget *w = o.controle(1)) {
            const QPoint milieu = w->mapToGlobal(QPoint(w->width() / 2, w->height() / 2));
            const QPointF local = w->mapFromGlobal(milieu);
            QMouseEvent appui(QEvent::MouseButtonPress, local, QPointF(milieu), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &appui);
            QMouseEvent bouge(QEvent::MouseMove, local, QPointF(milieu + QPoint(200, 40)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &bouge);
            VERIFIER(o.grille_visible() != nullptr);
            QMouseEvent lache(QEvent::MouseButtonRelease, local, QPointF(milieu + QPoint(200, 40)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(w, &lache);
            if (!resultat.isEmpty()) std::printf("résultat : %s\n", qPrintable(resultat));
            QFile r(dossier.filePath("p.grym"));
            VERIFIER(r.open(QIODevice::ReadOnly));
            const QString t = QString::fromUtf8(r.readAll());
            VERIFIER(t.contains("    en position libre :\n        le texte « t », à 0 de la gauche et 0 du haut,"));
            QRegularExpressionMatch m = QRegularExpression("un bouton « A », à (\\d+) de la gauche et (\\d+) du haut").match(t);
            VERIFIER(m.hasMatch() && m.captured(1).toInt() >= 192 && m.captured(2).toInt() >= 40);
            if (!m.hasMatch() || m.captured(1).toInt() < 232) std::printf("%s\n", qPrintable(t));
        }
        // un élément dans un groupe d'un écran décrit : c'est le groupe qui suit le geste (seul le premier niveau a une place)
        {
            QFile d(dossier.filePath("p.grym"));
            VERIFIER(d.open(QIODevice::WriteOnly | QIODevice::Truncate));
            d.write("L'écran d'accueil montre :\n    le texte « t »,\n    le groupe « G » :\n        le texte « dedans ».\n");
        }
        o.montrer(dossier.path());
        QApplication::processEvents();
        if (QWidget *w = o.controle(2)) {
            const QPoint milieu = w->mapToGlobal(QPoint(w->width() / 2, w->height() / 2));
            const QPointF local = w->mapFromGlobal(milieu);
            QMouseEvent appui(QEvent::MouseButtonPress, local, QPointF(milieu), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &appui);
            QMouseEvent bouge(QEvent::MouseMove, local, QPointF(milieu + QPoint(240, 0)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &bouge);
            QMouseEvent lache(QEvent::MouseButtonRelease, local, QPointF(milieu + QPoint(240, 0)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(w, &lache);
            QFile r(dossier.filePath("p.grym"));
            VERIFIER(r.open(QIODevice::ReadOnly));
            const QString t = QString::fromUtf8(r.readAll());
            QRegularExpressionMatch m = QRegularExpression("le groupe « G », à (\\d+) de la gauche").match(t);
            VERIFIER(m.hasMatch() && m.captured(1).toInt() >= 232);
            VERIFIER(t.contains("        le texte « dedans ».") || t.contains("            le texte « dedans »"));
            if (!m.hasMatch() || m.captured(1).toInt() < 232) std::printf("%s\n", qPrintable(t));
        }
        // Maj + flèche sur un élément d'un écran décrit, la touche livrée à l'aperçu qui a pris le focus au clic
        o.activateWindow();
        QApplication::processEvents();
        {
            QFile d(dossier.filePath("p.grym"));
            VERIFIER(d.open(QIODevice::WriteOnly | QIODevice::Truncate));
            d.write("L'écran d'accueil montre :\n    le texte « t »,\n    un bouton « A ».\n"
                    "Quand on clique sur « A » dans l'écran d'accueil :\n    Fermer l'écran.\n");
        }
        o.montrer(dossier.path());
        QApplication::processEvents();
        if (QWidget *w = o.controle(1)) {
            const QPoint milieu = w->mapToGlobal(QPoint(w->width() / 2, w->height() / 2));
            QMouseEvent appui(QEvent::MouseButtonPress, w->mapFromGlobal(milieu), QPointF(milieu), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &appui);
            QMouseEvent lache(QEvent::MouseButtonRelease, w->mapFromGlobal(milieu), QPointF(milieu), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(w, &lache);
            QWidget *f = QApplication::focusWidget();
            VERIFIER(f != nullptr);
            QKeyEvent fleche(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
            if (f) QApplication::sendEvent(f, &fleche);
            QFile r(dossier.filePath("p.grym"));
            VERIFIER(r.open(QIODevice::ReadOnly));
            VERIFIER(QString::fromUtf8(r.readAll()).contains("en position libre :"));
        }
    }

    // A4-c dans l'onglet : choisir un bouton, changer son libellé, « Appliquer » : le geste renomme aussi l'événement
    {
        QTemporaryDir dossier;
        QFile f(dossier.filePath("p.grym"));
        VERIFIER(f.open(QIODevice::WriteOnly));
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
        VERIFIER(r.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(r.readAll());
        VERIFIER(t.contains("un bouton « Terminer »") && t.contains("Quand on clique sur « Terminer » dans l'écran d'accueil"));
    }

    // A4-d : déplacer (ordre, côte à côte créé, l'un sous l'autre dans une rangée, bloc réduit à un élément retiré)
    {
        QTemporaryDir dossier;
        auto lire = [&]() {
            QFile f(dossier.filePath("p.grym"));
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        {
            QFile f(dossier.filePath("p.grym"));
            VERIFIER(f.open(QIODevice::WriteOnly));
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
            VERIFIER(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
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
        VERIFIER(f.open(QIODevice::WriteOnly));
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
        VERIFIER(r.open(QIODevice::ReadOnly));
        VERIFIER(QString::fromUtf8(r.readAll()).startsWith("L'écran d'accueil montre :\n    côte à côte :\n        le texte « A »,\n"
                                                           "        le texte « B »,\n    un bouton « OK ».\n"));
        delete mime;
    }


    // --- A5 : les projets et leurs fichiers (docs/atelier.md, § 5 quater) ---
    {   // Nouveau projet : dossier, programme, programme principal ; jamais par-dessus un dossier existant
        QTemporaryDir d;
        QString erreur;
        const QString p = creer_projet(d.filePath("Documents/GrymoiR"), "Gestion de projet", &erreur);
        VERIFIER(p == QDir(d.filePath("Documents/GrymoiR")).filePath("Gestion de projet"));
        QFile f(QDir(p).filePath("Gestion de projet.grym"));
        VERIFIER(f.open(QIODevice::ReadOnly) && f.readAll() == "Remarque : Gestion de projet, le programme principal.\n");
        FichierProjet fp;
        fp.lire(p);
        VERIFIER(fp.programme_principal == "Gestion de projet.grym");
        VERIFIER(analyser_source(QString("Remarque : Gestion de projet, le programme principal.\n"), false).message.isEmpty());
        VERIFIER(creer_projet(d.filePath("Documents/GrymoiR"), "Gestion de projet", &erreur).isEmpty()
                 && erreur.contains("jamais par-dessus"));
        VERIFIER(creer_projet(d.path(), "a/b", &erreur).isEmpty() && creer_projet(d.path(), "  ", &erreur).isEmpty());
        VERIFIER(creer_projet(d.path(), ".cache", &erreur).isEmpty() && creer_projet(d.path(), "« x »", &erreur).isEmpty());
    }
    {   // Projets récents : en tête, sans doublon, huit au plus
        QStringList l;
        for (int k = 0; k < 10; k++) l = recents_avec(l, QString("/p/%1").arg(k));
        VERIFIER(l.size() == 8 && l.first() == "/p/9" && l.last() == "/p/2");
        l = recents_avec(l, "/p/5");
        VERIFIER(l.size() == 8 && l.first() == "/p/5" && l.count("/p/5") == 1 && l.at(1) == "/p/9");
    }
    {   // Renommer : les « Utiliser » suivent (sous-dossier, forme compacte), la base suit le programme, tout s'annule
        QTemporaryDir dossier;
        auto ecrire = [&](const QString &nom, const QByteArray &texte) {
            QDir().mkpath(QFileInfo(dossier.filePath(nom)).absolutePath());
            QFile f(dossier.filePath(nom));
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write(texte);
        };
        auto lire = [&](const QString &nom) {
            QFile f(dossier.filePath(nom));
            VERIFIER(f.open(QIODevice::ReadOnly));
            return QString::fromUtf8(f.readAll());
        };
        auto existe = [&](const QString &nom) { return QFileInfo::exists(dossier.filePath(nom)); };
        ecrire("donnees.grym", "Un membre, conservé, a : un nom (texte), unique.\n");
        const QString prog = "Remarque : l'association.\nUtiliser « donnees ».\nUtiliser « ecrans/fiches ».\n"
                             "Afficher le nombre de membres conservés.\n";
        ecrire("assoc.grym", prog.toUtf8());
        ecrire("ecrans/fiches.grym", "Utiliser « ../donnees ».\nLe double d'un n vaut n × 2.\n");
        ecrire("outils.grymc", "_utiliser « donnees »\n");
        ecrire("assoc.grymd", "BASE");
        ecrire("assoc.grymd-journal", "JOURNAL");
        ecrire("projet.grymatelier", "programme principal : assoc.grym\n");
        VERIFIER(utilisateurs_de(dossier.path(), dossier.filePath("donnees.grym")).size() == 3);
        VERIFIER(utilisateurs_de(dossier.path(), dossier.filePath("assoc.grym")).isEmpty());

        Geste g;
        QString arrivee;
        VERIFIER(renommer_fichier(dossier.path(), dossier.filePath("donnees.grym"), "membres", &g, &arrivee).isEmpty());
        VERIFIER(arrivee == QDir(dossier.path()).filePath("membres.grym") && existe("membres.grym") && !existe("donnees.grym"));
        VERIFIER(lire("assoc.grym") == "Remarque : l'association.\nUtiliser « membres ».\nUtiliser « ecrans/fiches ».\n"
                                       "Afficher le nombre de membres conservés.\n");
        VERIFIER(lire("ecrans/fiches.grym").startsWith("Utiliser « ../membres ».\n"));
        VERIFIER(lire("outils.grymc") == "_utiliser « membres »\n");
        VERIFIER(g.avant.size() == 3 && g.renommes.size() == 1);
        // annuler : le nom revient d'abord, puis chaque fichier, octet pour octet
        VERIFIER(annuler_geste(g).isEmpty());
        VERIFIER(existe("donnees.grym") && !existe("membres.grym") && lire("assoc.grym") == prog);
        VERIFIER(lire("ecrans/fiches.grym").startsWith("Utiliser « ../donnees ».\n") && lire("outils.grymc") == "_utiliser « donnees »\n");

        // le programme emporte sa base, son journal, et reste le programme principal
        VERIFIER(renommer_fichier(dossier.path(), dossier.filePath("assoc.grym"), "association.grym", &g, &arrivee).isEmpty());
        VERIFIER(existe("association.grym") && existe("association.grymd") && existe("association.grymd-journal"));
        VERIFIER(!existe("assoc.grymd") && !existe("assoc.grymd-journal") && lire("association.grymd") == "BASE");
        FichierProjet fp;
        fp.lire(dossier.path());
        VERIFIER(fp.programme_principal == "association.grym");
        VERIFIER(annuler_geste(g).isEmpty());
        fp.lire(dossier.path());
        VERIFIER(existe("assoc.grymd") && existe("assoc.grymd-journal") && fp.programme_principal == "assoc.grym");

        // refus : nom pris (l'une ou l'autre forme), base prise, extension changée, dossier, rien à changer
        VERIFIER(renommer_fichier(dossier.path(), dossier.filePath("donnees.grym"), "outils", &g, &arrivee).contains("jamais écrasé"));
        ecrire("autre.grymd", "AUTRE");
        VERIFIER(renommer_fichier(dossier.path(), dossier.filePath("assoc.grym"), "autre", &g, &arrivee).contains("une base n'est jamais écrasée"));
        VERIFIER(lire("autre.grymd") == "AUTRE" && existe("assoc.grym"));
        VERIFIER(renommer_fichier(dossier.path(), dossier.filePath("donnees.grym"), "x.grymc", &g, &arrivee).contains("grym traduire"));
        VERIFIER(renommer_fichier(dossier.path(), dossier.filePath("donnees.grym"), "a/b", &g, &arrivee).contains("sans dossier"));
        VERIFIER(renommer_fichier(dossier.path(), dossier.filePath("donnees.grym"), "donnees", &g, &arrivee) == "Rien à changer.");

        // un nom qui casserait l'analyse est défait : ici, un fichier cassé ailleurs reste cassé, sans bloquer
        ecrire("casse.grym", "Afficher (.\n");
        VERIFIER(renommer_fichier(dossier.path(), dossier.filePath("donnees.grym"), "membres", &g, &arrivee).isEmpty());
        VERIFIER(annuler_geste(g).isEmpty() && existe("donnees.grym"));

        // corbeille : refusée si un autre fichier l'utilise, en le nommant ; le programme principal s'efface
        QString avis;
        const QString refus = supprimer_fichier(dossier.path(), dossier.filePath("donnees.grym"), &avis);
        VERIFIER(refus.contains("est utilisé par") && refus.contains("« assoc.grym »") && refus.contains("« ecrans/fiches.grym »"));
        VERIFIER(existe("donnees.grym"));
        // Sans ~/.local/share, Qt 6.4 a rangé la corbeille dans le dossier courant (files/, info/), constaté le
        // 10 octobre 2026 dans un conteneur Linux : on le crée d'abord.
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
        const bool corbeille = QFile::moveToTrash(dossier.filePath("casse.grym"));
        if (corbeille) {   // une corbeille existe sur ce système
            VERIFIER(supprimer_fichier(dossier.path(), dossier.filePath("assoc.grym"), &avis).isEmpty());
            VERIFIER(!existe("assoc.grym") && existe("assoc.grymd") && avis.contains("reste dans le dossier"));
            fp.lire(dossier.path());
            VERIFIER(fp.programme_principal.isEmpty());
        }
    }

    {   // Une fiche déduite (§ 22.3) : l'état en étiquette (§ 16.15), le courriel et le site en liens (§ 22.13)
        QVector<ElementVue> el(4);
        for (auto &e : el) e.sorte = ELEMENT_TEXTE;
        el[0].texte = QString::fromUtf8("État : actif");
        el[0].valeur_depuis = 7;
        el[0].etiquette = 'v';
        el[1].texte = "Courriel : anne+club@tilleul.ch";
        el[1].valeur_depuis = 11;
        el[1].lien = LIEN_COURRIEL;
        el[2].texte = "Site : https://tilleul.ch/?a=1&b=2";
        el[2].valeur_depuis = 7;
        el[2].lien = LIEN_WEB;
        el[3].texte = "Site : javascript:alert(1)";   // une adresse sans http(s) : jamais un lien
        el[3].valeur_depuis = 7;
        el[3].lien = LIEN_WEB;
        VueEcran v = dessiner_ecran("Anne", el);
        auto *etat = qobject_cast<QLabel *>(v.controles[0]);
        auto *courriel = qobject_cast<QLabel *>(v.controles[1]);
        auto *site = qobject_cast<QLabel *>(v.controles[2]);
        auto *faux = qobject_cast<QLabel *>(v.controles[3]);
        VERIFIER(etat && etat->text().startsWith(QString::fromUtf8("État : <span style=\"background-color:#d7f0dd;color:#1e5b2e")));
        VERIFIER(!etat->openExternalLinks());
        VERIFIER(courriel && courriel->text() == "Courriel : <a href=\"mailto:anne+club@tilleul.ch\">anne+club@tilleul.ch</a>");
        VERIFIER(courriel->openExternalLinks());
        VERIFIER(site && site->text() == "Site : <a href=\"https://tilleul.ch/?a=1&amp;b=2\">https://tilleul.ch/?a=1&amp;b=2</a>");
        VERIFIER(faux && !faux->text().contains("<a") && !faux->openExternalLinks());
        VERIFIER(lien_html(QString::fromUtf8("é l@x.ch"), LIEN_COURRIEL).startsWith("<a href=\"mailto:%C3%A9%20l@x.ch\">"));
        delete v.vue;
    }

    {   // Les sons (§ 15.7) : reconnus à leur signature ; le lecteur ne s'active qu'avec un son ; l'enregistreur annonce
        // le format qu'il écrirait ici (rien à vérifier sans micro : le conteneur n'en a pas)
        QByteArray wav("RIFF\x24\0\0\0WAVEfmt ", 16);
        wav += QByteArray(28, '\0');
        VERIFIER(octets_de_son(wav) && octets_de_son(QByteArray("\0\0\0\x18" "ftypM4A ", 12)) && octets_de_son("OggS....")
                 && octets_de_son("fLaC....") && !octets_de_son(QByteArray("\x89PNG\r\n\x1a\n", 8)) && !octets_de_son(""));
        LecteurSon l;
        VERIFIER(!l.bouton()->isEnabled() && !l.a_un_son());
        l.donner(wav, "memo.wav");
        VERIFIER(l.bouton()->isEnabled() && l.a_un_son() && l.nom_du_son() == "memo.wav");
        l.donner(QByteArray(), QString());
        VERIFIER(!l.bouton()->isEnabled());
        const QString ext = EnregistreurSon::extension_choisie();
        std::printf("enregistrement sur ce système : %s\n", ext.isEmpty() ? "aucun format" : qPrintable(ext));
        VERIFIER(ext.isEmpty() || QStringList({"m4a", "ogg", "flac", "wav"}).contains(ext));

        // la fiche déduite (§ 22.3) : un son devient un lecteur, une image une image, sous « Libellé : valeur »
        QVector<ElementVue> el(2);
        el[0].sorte = el[1].sorte = ELEMENT_TEXTE;
        el[0].texte = QString::fromUtf8("Mémo : memo.wav");
        el[0].valeur_depuis = 7;
        el[0].octets = wav;
        QImage img(4, 4, QImage::Format_RGB32);
        img.fill(Qt::red);
        QByteArray png;
        QBuffer tampon(&png);
        tampon.open(QIODevice::WriteOnly);
        img.save(&tampon, "PNG");
        el[1].texte = "Photo : p.png";
        el[1].valeur_depuis = 8;
        el[1].octets = png;
        VueEcran v = dessiner_ecran("Visite", el);
        auto *lf = v.controles[0] ? v.controles[0]->findChild<LecteurSon *>("lecteur-fiche") : nullptr;
        VERIFIER(lf && lf->a_un_son() && lf->bouton()->isEnabled());
        VERIFIER(v.controles[1] && !v.controles[1]->findChild<LecteurSon *>() && v.controles[1]->findChildren<QLabel *>().size() == 2);
        delete v.vue;
    }

    {   // Une zone (son) dans la fenêtre d'exécution (§ 15.7) : « Écouter » et « Enregistrer » à côté du choix de fichier ;
        // un enregistrement fait le changement de la zone, comme un fichier choisi ; le son affiché se joue depuis le fil
        QTemporaryDir dossier;
        const QString prog = dossier.filePath("p.grym");
        {
            QFile f(prog);
            VERIFIER(f.open(QIODevice::WriteOnly));
            f.write("L'écran x montre :\n    un mémo (son), facultatif,\n    un bouton « OK ».\n"
                    "Quand on clique sur « OK » dans l'écran x :\n    Afficher le format du mémo de l'écran.\n"
                    "    Afficher le mémo de l'écran.\n    Fermer l'écran.\nOuvrir l'écran x.\n");
        }
        Execution e(prog);
        e.show();
        e.demarrer();
        auto attendre = [&](const std::function<bool()> &cond) {
            QElapsedTimer z;
            z.start();
            while (z.elapsed() < 15000 && !cond()) QApplication::processEvents(QEventLoop::AllEvents, 20);
            return cond();
        };
        auto ok = [&]() -> QPushButton * {
            for (QPushButton *b : e.findChildren<QPushButton *>()) if (b->text() == "OK" && b->isVisible() && b->isEnabled()) return b;
            return nullptr;
        };
        VERIFIER(attendre([&] { return ok() != nullptr; }));
        auto *micro = e.findChild<EnregistreurSon *>();
        LecteurSon *lecteur = nullptr;
        for (LecteurSon *x : e.findChildren<LecteurSon *>()) if (x->isVisible()) lecteur = x;   // pas celui, caché, du fil
        VERIFIER(micro && micro->isVisible() && lecteur && !lecteur->a_un_son());
        if (micro && micro->enregistre && lecteur) {
            QByteArray wav("RIFF\x24\0\0\0WAVEfmt ", 16);
            wav += QByteArray(28, '\0');
            micro->enregistre(wav, "enregistrement-2026-10-18-1432.wav");   // ce que rend la fin d'un enregistrement
            VERIFIER(lecteur->a_un_son());
            auto bouton_son = [&]() -> QPushButton * {   // la machine le décrit par sa taille : 44 octets
                for (QPushButton *b : e.findChildren<QPushButton *>())
                    if (b->text().startsWith(QString::fromUtf8("un fichier de 44 octets"))) return b;
                return nullptr;
            };
            VERIFIER(attendre([&] { return bouton_son() != nullptr; }));   // la machine a reçu le son, et le montre
            VERIFIER(lecteur->a_un_son());   // sa description à elle : le son reste écoutable
            if (ok()) ok()->click();
            auto *fil = e.findChild<QTextBrowser *>();
            VERIFIER(fil && attendre([&] { return fil->toPlainText().contains(QString::fromUtf8("Écouter « ")); }));
            VERIFIER(fil && fil->toPlainText().contains("WAV") && fil->toHtml().contains("son:0"));
        }
    }

    std::printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}
