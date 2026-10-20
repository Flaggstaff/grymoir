// GrymoiR : l'atelier, exécution d'un programme dans sa propre fenêtre.
#include "execution.h"
#include "rapport.h"
#include "transcription.h"
#include "theme.h"
#include "vue_ecran.h"
#include <QUrl>
// L'autorisation du micro (Qt 6.5 et suivants), comme dans son.cpp ; Qt 6.4 n'en a pas besoin.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#  if QT_CONFIG(permissions)
#    include <QPermission>
#    define GRYM_AUTORISATION_MICRO 1
#  endif
#endif
#include <QMessageBox>
#include "son.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QHash>
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
#include <QAbstractButton>
#include <QButtonGroup>
#include <functional>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QGuiApplication>
#include <QScreen>
#include <QSplitter>
#include <QFrame>
#include <QTabWidget>
#include <QAction>
#include <QTableWidget>
#include <QSvgRenderer>
#include <QPainter>
#include <QHeaderView>
#include <QTextBrowser>
#include <QDesktopServices>
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
    return c->type && (std::strcmp(c->type, "fichier") == 0 || std::strcmp(c->type, "image") == 0 || std::strcmp(c->type, "son") == 0);
}

static bool est_oui_non(const Champ *c) { return c->type && std::strcmp(c->type, "vrai ou faux") == 0; }

// Les formats de son que la machine reconnaît (§ 15.7, format_son de src/vm.c) ; les autres sont des images.
static bool format_de_son(const char *f) {
    if (!f) return false;
    for (const char *s : {"WAV", "MP3", "AAC", "M4A", "Ogg", "WebM", "FLAC"}) if (std::strcmp(f, s) == 0) return true;
    return false;
}

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

static void iface_image(void *contexte, Chaine *sortie, const unsigned char *octets, size_t taille, const char *format,
                        const char *description) {
    auto *t = static_cast<Travail *>(contexte);
    t->vider_sortie(sortie);
    const QByteArray o(reinterpret_cast<const char *>(octets), (qsizetype)taille);
    if (format_de_son(format)) emit t->son(o, QString::fromUtf8(description ? description : ""));   // § 15.7
    else emit t->image(o, QString::fromUtf8(description ? description : ""));
}

static void iface_fiche(void *contexte, Chaine *sortie, const char *titre, const LigneFiche *lignes, size_t n) {
    auto *t = static_cast<Travail *>(contexte);
    t->vider_sortie(sortie);
    // Les images de la fiche voyagent à part : le HTML les désigne par « image:K », la fenêtre les range.
    QList<QByteArray> images, sons;
    QString h = "<p><b>" + QString::fromUtf8(titre).toHtmlEscaped() + "</b></p><table cellspacing=\"0\" cellpadding=\"3\">";
    for (size_t k = 0; k < n; k++) {
        h += "<tr><td style=\"color:" + Theme::courant().hex("texte-2") + "\">" + QString::fromUtf8(lignes[k].libelle).toHtmlEscaped() + "</td><td>";
        if (lignes[k].format && lignes[k].octets && format_de_son(lignes[k].format)) {   // un son : un lien qui le joue
            h += QString::fromUtf8("<a href=\"son-fiche:%1\">▶ Écouter</a> ").arg(sons.size() / 2);   // rang définitif : la fenêtre
            sons.append(QByteArray(lignes[k].texte ? lignes[k].texte : ""));
            sons.append(QByteArray(reinterpret_cast<const char *>(lignes[k].octets), (qsizetype)lignes[k].taille));
        } else if (lignes[k].format && lignes[k].octets) {
            h += QString("<img src=\"image:%1\"><br>").arg(images.size());
            images.append(QByteArray(reinterpret_cast<const char *>(lignes[k].octets), (qsizetype)lignes[k].taille));
        }
        const QString valeur = QString::fromUtf8(lignes[k].texte ? lignes[k].texte : "");
        const auto p = teinte_etiquette(lignes[k].etiquette ? QChar(lignes[k].etiquette) : QChar());   // § 16.15
        const QString lien = lien_html(valeur, lignes[k].lien);   // § 22.13
        if (!p.first.isEmpty())
            h += "<span style=\"background-color:" + p.first + ";color:" + p.second + ";font-weight:600\">&#8201;"
                 + valeur.toHtmlEscaped() + "&#8201;</span>";
        else if (!lien.isEmpty()) h += lien;
        else h += valeur.toHtmlEscaped();
        h += "</td></tr>";
    }
    h += "</table>";
    emit t->fiche(h, images, sons);
}

// --- Écrans (grammaire, § 22) : la machine décrit, la fenêtre montre ---

// Une zone sur plusieurs lignes valide quand on la quitte : Entrée y va à la ligne (§ 22.6).
class SortieDeZone : public QObject {
public:
    SortieDeZone(QObject *parent, std::function<void()> f) : QObject(parent), quand(std::move(f)) {}
protected:
    bool eventFilter(QObject *, QEvent *e) override {
        if (e->type() == QEvent::FocusOut) quand();
        return false;
    }
private:
    std::function<void()> quand;
};

static void iface_ecran_ouvrir(void *contexte, Chaine *sortie, const char *titre, const ElementEcran *el, size_t n) {
    auto *t = static_cast<Travail *>(contexte);
    t->vider_sortie(sortie);
    QVector<int> sortes;
    QStringList textes;
    QVector<QStringList> colonnes;
    for (size_t k = 0; k < n; k++) {
        sortes << (int)el[k].sorte;
        textes << QString::fromUtf8(el[k].texte);
        QStringList c;
        for (size_t q = 0; q < el[k].nb_colonnes; q++) c << QString::fromUtf8(el[k].colonnes[q]);
        colonnes << c;
    }
    if (!t->nb_colonnes.isEmpty() || !t->types_zones.isEmpty())   // un écran est déjà ouvert : il passe dessous
        t->dessous.push_back({t->nb_colonnes, t->types_zones, t->facultatives, t->choix_zones, t->options_zones, t->images, t->places,
                          t->graphiques});
    t->nb_colonnes.clear();
    for (const auto &c : colonnes) t->nb_colonnes << c.size();
    t->types_zones.clear();
    t->facultatives.clear();
    t->choix_zones.clear();
    t->options_zones.clear();
    t->images.clear();
    t->places.clear();
    t->graphiques.clear();
    t->indications.clear();
    t->longueurs.clear();
    t->unites.clear();
    t->valeurs_fiche.clear();
    for (size_t k = 0; k < n; k++) {
        // le rang de la valeur, en caractères Qt : la machine le donne en octets UTF-8
        t->valeurs_fiche << (el[k].sorte == ELEMENT_TEXTE && el[k].valeur_depuis && el[k].texte
                                 && el[k].valeur_depuis <= strlen(el[k].texte)
                             ? QVector<int>{(int)QString::fromUtf8(el[k].texte, (qsizetype)el[k].valeur_depuis).size(),
                                            (int)(unsigned char)el[k].etiquette, el[k].lien}
                             : QVector<int>());
        t->places << (el[k].place ? QVector<int>{el[k].gauche, el[k].haut, el[k].largeur, el[k].hauteur} : QVector<int>());
        t->graphiques << (el[k].sorte == ELEMENT_GRAPHIQUE ? el[k].graphique : 0);
        t->images << (el[k].octets ? QByteArray(reinterpret_cast<const char *>(el[k].octets), (qsizetype)el[k].taille) : QByteArray());
        t->types_zones << (el[k].sorte == ELEMENT_ZONE ? QString::fromUtf8(el[k].type) : QString());
        t->options_zones << ((el[k].plusieurs_lignes ? 1 : 0) | (el[k].lecture_seule ? 2 : 0) | (el[k].interrupteur ? 4 : 0)
                             | (el[k].multiple ? 8 : 0) | (el[k].recherche ? 16 : 0) | (el[k].masque ? 32 : 0)
                             | (el[k].principal ? 64 : 0) | (el[k].tuile ? 128 : 0) | (el[k].a_cote_du_titre ? 256 : 0) | (el[k].inverse ? 512 : 0) | (el[k].cartes ? 1024 : 0) | (el[k].capitales ? 2048 : 0)
                             | (el[k].dossier ? 4096 : 0));
        t->facultatives << el[k].facultatif;
        t->indications << QString::fromUtf8(el[k].indication ? el[k].indication : "");
        t->unites << (el[k].unite ? QString(el[k].unite_devant ? "<" : ">") + QString::fromUtf8(el[k].unite) : QString());
        t->longueurs << (el[k].borne_max && el[k].type && strcmp(el[k].type, "texte") == 0 ? (int)atol(el[k].borne_max) : 0);
        QStringList ch;
        for (size_t q = 0; q < el[k].nb_choix; q++) ch << QString::fromUtf8(el[k].choix[q]);
        t->choix_zones << ch;
    }
    emit t->ecran_ouvert(QString::fromUtf8(titre), sortes, textes, colonnes);
}

static void iface_ecran_lignes(void *contexte, size_t element, const char *const *cellules, size_t nb_lignes) {
    auto *t = static_cast<Travail *>(contexte);
    // le nombre de colonnes se déduit du total ; la fenêtre connaît les titres
    QStringList c;   // nb_lignes × nb_colonnes cellules, ligne par ligne
    for (size_t k = 0; cellules && k < nb_lignes * t->colonnes_de(element); k++) c << QString::fromUtf8(cellules[k]);
    emit t->ecran_lignes((int)element, c, (int)nb_lignes);
}

static void iface_ecran_apparence(void *contexte, int couleur, const unsigned char *logo, size_t taille, const char *) {
    auto *t = static_cast<Travail *>(contexte);
    emit t->apparence(couleur, logo ? QByteArray(reinterpret_cast<const char *>(logo), (qsizetype)taille) : QByteArray());
}

static void iface_ecran_theme(void *contexte, const char *const *noms, const char *const *clair, const char *const *sombre,
                              size_t n, int clair_impose, const unsigned char *police, size_t taille_police) {
    auto *t = static_cast<Travail *>(contexte);
    QStringList ln, lc, ls;
    for (size_t k = 0; k < n; k++) {
        ln << QString::fromUtf8(noms[k]);
        lc << QString::fromUtf8(clair[k]);
        ls << (sombre[k] ? QString::fromUtf8(sombre[k]) : QString());
    }
    emit t->theme(ln, lc, ls, clair_impose != 0,
                  police ? QByteArray(reinterpret_cast<const char *>(police), (qsizetype)taille_police) : QByteArray());
}

static void iface_montrer_document(void *contexte, const unsigned char *octets, size_t taille, const char *nom) {   // 2.25, 2.26
    emit static_cast<Travail *>(contexte)->document(QByteArray(reinterpret_cast<const char *>(octets), (qsizetype)taille),
                                                    QString::fromUtf8(nom ? nom : "document.pdf"));
}

static void iface_ecran_texte(void *contexte, size_t k, const char *texte) {   // le texte calculé (2.24)
    emit static_cast<Travail *>(contexte)->ecran_texte((int)k, QString::fromUtf8(texte ? texte : ""));
}

static void iface_ecran_signature(void *contexte, const unsigned char *octets, size_t taille, const char *) {
    auto *t = static_cast<Travail *>(contexte);
    emit t->signature(QByteArray(reinterpret_cast<const char *>(octets), (qsizetype)taille));
}

static Issue iface_ecran_attendre(void *contexte, Chaine *sortie, Evenement *e) {
    return static_cast<Travail *>(contexte)->attendre_evenement(sortie, e);
}

static void iface_ecran_erreur(void *contexte, Chaine *sortie, const char *message) {
    auto *t = static_cast<Travail *>(contexte);
    t->vider_sortie(sortie);
    emit t->ecran_erreur(QString::fromUtf8(message));
}

static void iface_ecran_valeurs(void *contexte, const char *const *v, size_t n) {
    QStringList l;
    for (size_t k = 0; k < n; k++) l << (v[k] ? QString::fromUtf8(v[k]) : QString());
    emit static_cast<Travail *>(contexte)->ecran_valeurs(l);
}

static void iface_ecran_taille_titre(void *contexte, int taille) { static_cast<Travail *>(contexte)->taille_titre = taille; }   // § 22.5

static void iface_ecran_image(void *contexte, size_t k, const unsigned char *octets, size_t taille, const char *) {   // § 22.8
    emit static_cast<Travail *>(contexte)->ecran_image((int)k, octets ? QByteArray(reinterpret_cast<const char *>(octets), (qsizetype)taille)
                                                                    : QByteArray());
}

// La transcription d'un son (§ 15.8) : dans le fil de la machine, la fenêtre reste libre pendant ce temps.
static char *iface_transcrire(void *, const unsigned char *octets, size_t taille, char **erreur) {
    QString probleme;
    const QString texte = transcrire_son(QByteArray(reinterpret_cast<const char *>(octets), (qsizetype)taille),
                                         modele_de_transcription(), &probleme);
    if (!probleme.isEmpty()) {
        *erreur = grym_dupliquer(probleme.toUtf8().constData());
        return nullptr;
    }
    return grym_dupliquer(texte.toUtf8().constData());
}

// Un rapport en PDF (§ 15.6) : appelé dans le fil de la machine, dessiné dans celui de l'interface, qui attend.
static unsigned char *iface_rendre_pdf(void *, const char *html, size_t *taille, char **erreur) {
    QByteArray octets;
    QString probleme;
    const QString page = QString::fromUtf8(html);
    auto faire = [&] { octets = pdf_depuis_html(page, &probleme); };
    if (QThread::currentThread() == qApp->thread()) faire();
    else QMetaObject::invokeMethod(qApp, faire, Qt::BlockingQueuedConnection);
    if (octets.isEmpty()) {
        *erreur = grym_dupliquer((probleme.isEmpty() ? QString("Le PDF n'a pas pu être fabriqué.") : probleme).toUtf8().constData());
        return nullptr;
    }
    auto *o = static_cast<unsigned char *>(malloc((size_t)octets.size()));
    memcpy(o, octets.constData(), (size_t)octets.size());
    *taille = (size_t)octets.size();
    return o;
}

static void iface_ecran_erreurs_zones(void *contexte, const size_t *els, const char *const *msgs, size_t n) {   // § 22.6
    QVector<int> e;
    QStringList m;
    for (size_t k = 0; k < n; k++) { e << (int)els[k]; m << QString::fromUtf8(msgs[k]); }
    emit static_cast<Travail *>(contexte)->ecran_erreurs_zones(e, m);
}

static void iface_ecran_etats(void *contexte, const unsigned char *etats, size_t n) {   // § 22.6
    QVector<int> l;
    for (size_t k = 0; k < n; k++) l << etats[k];
    emit static_cast<Travail *>(contexte)->ecran_etats(l);
}

static void iface_ecran_fermer(void *contexte, Chaine *sortie) {
    auto *t = static_cast<Travail *>(contexte);
    t->vider_sortie(sortie);
    if (!t->dessous.isEmpty()) {   // l'écran du dessous reprend sa description
        const auto d = t->dessous.takeLast();
        t->nb_colonnes = d.nb_colonnes;
        t->types_zones = d.types_zones;
        t->facultatives = d.facultatives;
        t->choix_zones = d.choix_zones;
        t->options_zones = d.options_zones;
        t->images = d.images;
        t->places = d.places;
        t->graphiques = d.graphiques;
    } else {
        t->places.clear();
        t->graphiques.clear();
        t->nb_colonnes.clear();
        t->types_zones.clear();
        t->facultatives.clear();
        t->choix_zones.clear();
        t->options_zones.clear();
        t->images.clear();
    }
    emit t->ecran_ferme();
}

Issue Travail::attendre_evenement(Chaine *sortie, Evenement *e) {
    vider_sortie(sortie);
    emit ecran_attente();
    reponse.acquire();   // la fenêtre remplit `issue` et `evenement`, puis libère
    if (issue == 2) return ISSUE_INTERROMPU;
    *e = evenement;
    return ISSUE_REPONDU;
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
static void situer(Machine *m, const QString &chemin, const QString &base_imposee, const QString &ecriture) {
    const QFileInfo i(chemin);
    machine_dossier(m, i.absolutePath().toUtf8().constData());
    if (!ecriture.isEmpty()) machine_dossier_ecriture(m, ecriture.toUtf8().constData());
    const QString base = !base_imposee.isEmpty() ? base_imposee : i.absolutePath() + "/" + i.completeBaseName() + ".grymd";
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
    // un son quelque part (champ d'entité, zone d'écran) : la fenêtre demande le micro tout de suite, pas au premier clic
    for (size_t k = 0; module && k < module->nb_classes; k++)
        for (size_t q = 0; q < module->classes[k].nb_champs; q++)
            if (module->classes[k].types && module->classes[k].types[q]
                && std::strcmp(module->classes[k].types[q], "son") == 0) {
                emit utilise_micro();
                k = module->nb_classes;
                break;
            }
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
    Interface i = {this, iface_disponible, iface_formulaire, iface_effacer, 1, iface_image, iface_fiche,
                   iface_ecran_ouvrir, iface_ecran_lignes, iface_ecran_attendre, iface_ecran_erreur, iface_ecran_fermer,
                   iface_ecran_valeurs, iface_ecran_apparence, iface_ecran_etats, iface_ecran_image,
                   iface_ecran_taille_titre, iface_ecran_erreurs_zones, iface_rendre_pdf,
                   iface_transcrire, iface_ecran_theme, iface_ecran_signature, iface_ecran_texte,
                   iface_montrer_document};
    machine_interface(m, &i);
    situer(m, chemin, base_imposee, dossier_ecriture);
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
    // une fiche affichée (§ 22.13) : un courriel s'écrit, une adresse web s'ouvre, par le système ; rien d'autre
    connect(fil, &QTextBrowser::anchorClicked, this, [this](const QUrl &u) {
        if (u.scheme() == "mailto" || u.scheme() == "http" || u.scheme() == "https") QDesktopServices::openUrl(u);
        else if (u.scheme() == "son") jouer_son_du_fil(u.path().toInt());   // « son:3 » : un son du fil (§ 15.7)
    });
    lecteur_fil = new LecteurSon(this);
    lecteur_fil->hide();   // ses messages vont dans la barre d'état
    lecteur_fil->signaler = [this](const QString &m) { etat->setText(m); };
    fil->setFont(Theme::courant().police_code(13));

    zone = new QWidget;
    auto *defilement = new QScrollArea;
    defilement->setWidget(zone);
    defilement->setWidgetResizable(true);

    bouton_envoyer = new QPushButton("Envoyer");
    bouton_envoyer->setProperty("role", "principal");
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
    bas = new QWidget;
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
    // Ctrl+J : le fil d'exécution et la zone des questions, à la demande, sous un écran (comme la console d'un navigateur)
    auto *voir_fil = new QAction(QString::fromUtf8("Fil d'exécution"), this);
    voir_fil->setShortcut(QKeySequence("Ctrl+J"));
    connect(voir_fil, &QAction::triggered, this, [this] { console = !console; ranger(); });
    addAction(voir_fil);

    // Le fil de la machine émet ; ces signaux arrivent ici, dans l'ordre, par la file des événements.
    connect(&travail, &Travail::texte, this, [this](const QString &t) { ecrire(t); });
    connect(&travail, &Travail::effacer, this, [this] {
        fil->clear();
        lecteur_fil->donner(QByteArray(), QString());   // les liens « son:K » disparaissent avec le texte
        sons_du_fil.clear();
        son_joue = -1;
    });
    connect(&travail, &Travail::son, this, [this](const QByteArray &octets, const QString &nom) {
        const int k = lien_son(nom, octets);
        QTextCursor c(fil->document());
        c.movePosition(QTextCursor::End);
        c.insertHtml(QString::fromUtf8("<a href=\"son:%1\">▶ Écouter « %2 »</a>").arg(k).arg(nom.toHtmlEscaped()));
        c.insertBlock();
        c.setCharFormat(QTextCharFormat());
        fil->setTextCursor(c);
        fil->ensureCursorVisible();
    });
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
    connect(&travail, &Travail::fiche, this, [this](const QString &html, const QList<QByteArray> &liste,
                                                   const QList<QByteArray> &sons) {
        QString h = html;
        for (int k = 0; 2 * k + 1 < sons.size(); k++)   // les sons de la fiche : « son:K » devient leur rang dans le fil
            h.replace(QString("\"son-fiche:%1\"").arg(k),
                      QString("\"son:%1\"").arg(lien_son(QString::fromUtf8(sons[2 * k]), sons[2 * k + 1])));
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
    connect(&travail, &Travail::utilise_micro, this, &Execution::demander_micro);
    connect(&travail, &Travail::document, this, [this](const QByteArray &pdf, const QString &nom) {
        // « Ouvrir le document … » (2.25) : le PDF dans la visionneuse du système (Aperçu sous macOS), d'où l'on
        // imprime ; GRYMOIR_SANS_APERCU (les essais, les captures) le garde sans l'ouvrir
        // chacun dans son dossier : son nom d'origine (« ticket.jpg »), et la visionneuse qui va avec son extension
        const QString sous = documents.filePath(QString::number(++nb_documents));
        QDir().mkpath(sous);
        QString propre = QFileInfo(nom).fileName();
        if (propre.isEmpty()) propre = "document.pdf";
        const QString chemin = sous + "/" + propre;
        QFile f(chemin);
        if (!f.open(QIODevice::WriteOnly) || f.write(pdf) != pdf.size()) { etat->setText("Le document n'a pas pu être écrit."); return; }
        f.close();
        setProperty("dernier_document", chemin);
        if (qEnvironmentVariableIsEmpty("GRYMOIR_SANS_APERCU")) QDesktopServices::openUrl(QUrl::fromLocalFile(chemin));
    });
    connect(&travail, &Travail::ecran_ouvert, this, &Execution::montrer_ecran);
    connect(&travail, &Travail::apparence, this, [this](int couleur, const QByteArray &octets) {
        // L'accent dans la palette de cinq (§ 22.5 ; même ordre que « _ordre » des jetons), pour ce processus seul :
        // l'atelier qui a lancé le programme garde le sien.
        Theme &t = Theme::courant();
        const QStringList accents = t.accents();
        if (couleur >= 0 && couleur < accents.size()) t.appliquer(t.mode(), accents[couleur]);
        logo = QPixmap();
        if (!octets.isEmpty() && !logo.loadFromData(octets)) {
            // un logo SVG (2.13) : dessiné ici, net, sans dépendre d'un greffon de lecture d'images
            QSvgRenderer rendu(octets);
            if (rendu.isValid()) {
                const QSize t = rendu.defaultSize().isEmpty() ? QSize(256, 256) : rendu.defaultSize().scaled(256, 256, Qt::KeepAspectRatio);
                QImage image(t, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QPainter p(&image);
                rendu.render(&p);
                p.end();
                logo = QPixmap::fromImage(image);
            }
        }
        if (!logo.isNull()) {
            setWindowIcon(logo);
            QApplication::setWindowIcon(logo);
        }
    });
    connect(&travail, &Travail::signature, this, [this](const QByteArray &octets) {
        // le bandeau (2.16) : une image, ou un SVG dessiné ici à la hauteur voulue, net
        signature = QPixmap();
        if (signature.loadFromData(octets)) return;
        QSvgRenderer rendu(octets);
        if (!rendu.isValid()) return;
        const QSize t = rendu.defaultSize().isEmpty() ? QSize(800, 100) : rendu.defaultSize().scaled(QSize(4000, 156), Qt::KeepAspectRatio);
        QImage image(t, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        rendu.render(&p);
        p.end();
        signature = QPixmap::fromImage(image);
    });
    connect(&travail, &Travail::theme, this, [](const QStringList &noms, const QStringList &clair, const QStringList &sombre,
                                                bool clair_impose, const QByteArray &police) {
        // le thème libre (§ 22.5, 2.13), pour ce processus seul : l'atelier qui a lancé le programme garde le sien
        QMap<QString, QString> c, s;
        for (int k = 0; k < noms.size(); k++) {
            c[noms[k]] = clair.value(k);
            if (!sombre.value(k).isEmpty()) s[noms[k]] = sombre.value(k);
        }
        Theme::courant().personnaliser(c, s, clair_impose, police);
    });
    connect(&travail, &Travail::ecran_lignes, this, [this](int element, const QStringList &cellules, int nb_lignes) {
        if (auto *gr = dynamic_cast<Graphique *>(controles_ecran.value(element))) {   // libellé, valeur ; une ligne par groupe (§ 22.11)
            QStringList noms;
            QVector<double> vals;
            for (int r = 0; r < nb_lignes && 2 * r + 1 < cellules.size(); r++) {
                noms << cellules[2 * r];
                vals << nombre_affiche(cellules[2 * r + 1]);
            }
            gr->donner(noms, vals);
            return;
        }
        if (element < 0 || element >= tables.size() || !tables[element]) return;
        QTableWidget *t = tables[element];
        const int nc = t->columnCount();
        t->setRowCount(nb_lignes);
        for (int r = 0; r < nb_lignes; r++) {
            const bool total = r * nc < cellules.size() && cellules[r * nc].startsWith(QChar(0x1c));   // § 22.3
            for (int c = 0; c < nc && r * nc + c < cellules.size(); c++) {
                QString x = cellules[r * nc + c];
                if (total && c == 0) x = x.mid(1);
                auto *i = new CelluleTriable(x);   // 70 avant 130, les dates dans l'ordre du calendrier
                if (total) {   // en gras, ni sélectionnable ni choisie
                    QFont f = i->font();
                    f.setBold(true);
                    i->setFont(f);
                    i->setFlags(Qt::ItemIsEnabled);
                    i->setData(ROLE_TOTAL, true);
                }
                if (x.size() > 1 && x[0] == QChar(0x1d)) {   // une étiquette (§ 16.15) : les couleurs du navigateur
                    const auto p = teinte_etiquette(x[1]);
                    i->setText(x.mid(2));
                    if (!p.first.isEmpty()) {
                        i->setBackground(QColor(p.first));
                        i->setForeground(QColor(p.second));
                        QFont f = i->font();
                        f.setBold(true);
                        i->setFont(f);
                    }
                }
                i->setData(Qt::UserRole, r);   // le rang dans la liste de la machine, quel que soit le tri à l'écran
                t->setItem(r, c, i);
            }
        }
        // l'ordre du programme d'abord ; un tri choisi à l'écran (clic sur un titre) se garde d'une relecture à l'autre
        const int col = t->property("tri").isValid() ? t->property("tri").toInt() : -1;
        if (col >= 0) t->sortItems(col, (Qt::SortOrder)t->property("ordre").toInt());
        epingler_total(t);
        if (auto *jeu = qobject_cast<QWidget *>(t->property("cartes").value<QObject *>())) {
            // les cartes (2.19) : la première colonne en grand, les étiquettes à droite, la deuxième dessous, les autres
            // jointes par « · » ; un clic choisit la ligne, comme un double-clic dans une liste
            auto *g = qobject_cast<QGridLayout *>(jeu->layout());
            for (QPushButton *b : jeu->findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly)) b->deleteLater();
            for (QLabel *l : jeu->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly)) l->deleteLater();
            int n = 0;
            for (int r = 0; r < nb_lignes && g; r++) {
                if (r * nc >= cellules.size() || cellules[r * nc].startsWith(QChar(0x1c))) continue;
                auto *b = new QPushButton(jeu);
                b->setProperty("role", "carte");
                auto *dedans = new QVBoxLayout(b);
                dedans->setContentsMargins(20, 16, 20, 16);
                dedans->setSpacing(6);
                auto *tete = new QHBoxLayout;
                auto *grand = new QLabel(cellules[r * nc].startsWith(QChar(0x1d)) ? cellules[r * nc].mid(2) : cellules[r * nc]);
                grand->setProperty("niveau", "carte-grand");
                tete->addWidget(grand, 1);
                QStringList detail;
                QString nom;
                for (int c = 1; c < nc && r * nc + c < cellules.size(); c++) {
                    const QString x = cellules[r * nc + c];
                    if (x.size() > 1 && x[0] == QChar(0x1d)) {   // une étiquette : sa couleur, comme dans une liste
                        auto *e = new QLabel(x.mid(2));
                        const auto p = teinte_etiquette(x[1]);
                        if (!p.first.isEmpty())
                            e->setStyleSheet(QString("background: %1; color: %2; font-weight: 600; font-size: 12px; "
                                                     "padding: 3px 9px; border-radius: 9px;").arg(p.first, p.second));
                        tete->addWidget(e, 0, Qt::AlignTop);
                    } else if (!x.isEmpty()) {
                        if (nom.isEmpty()) nom = x; else detail << x;
                    }
                }
                dedans->addLayout(tete);
                if (!nom.isEmpty()) { auto *l = new QLabel(nom); l->setProperty("niveau", "carte-nom"); dedans->addWidget(l); }
                if (!detail.isEmpty()) { auto *l = new QLabel(detail.join(QString::fromUtf8(" · "))); l->setProperty("niveau", "carte-detail"); dedans->addWidget(l); }
                for (QLabel *l : b->findChildren<QLabel *>()) l->setAttribute(Qt::WA_TransparentForMouseEvents);
                b->setAccessibleName(grand->text() + " " + nom);
                b->setMinimumHeight(dedans->sizeHint().height());
                connect(b, &QPushButton::clicked, this, [this, element, r, t] {
                    // la ligne choisie dans la table cachée : « la séance choisie de l'écran » se lit là
                    for (int q = 0; q < t->rowCount(); q++)
                        if (t->item(q, 0) && t->item(q, 0)->data(Qt::UserRole).toInt() == r) { t->setCurrentCell(q, 0); break; }
                    envoyer_evenement(EVENEMENT_CHOIX, element, r);
                });
                g->addWidget(b, n / 3, n % 3);
                n++;
            }
            if (!n && g) {
                auto *vide = new QLabel(QString::fromUtf8("Rien pour l'instant."), jeu);
                vide->setProperty("niveau", "carte-detail");
                g->addWidget(vide, 0, 0, 1, 3);
            }
        }
    });
    connect(&travail, &Travail::ecran_valeurs, this, [this](const QStringList &v) {
        for (int k = 0; k < v.size() && k < zones.size(); k++) {
            QWidget *z = zones[k];
            if (!z) continue;
            QSignalBlocker b(z);   // montrer la valeur n'est pas un changement
            if (z->property("multiple").toBool()) {   // les clés cochées, U+001E entre elles
                const QStringList coches = v[k].split(QChar(0x1e), Qt::SkipEmptyParts);
                for (QCheckBox *c : z->findChildren<QCheckBox *>()) {
                    QSignalBlocker bc(c);
                    c->setChecked(coches.contains(c->property("cle").toString()));
                }
            } else if (auto *l = qobject_cast<QLineEdit *>(z)) {
                if (!l->property("masque").toBool() || l->text().isEmpty() == v[k].isEmpty()) l->setText(v[k]);
                // masqué : la valeur de la machine ne s'y réécrit pas, sauf pour vider le champ quand elle l'est
            }
            else if (auto *bf = qobject_cast<QPushButton *>(z); bf && bf->property("fichier").toBool()) {   // le fichier actuel
                bf->setText(v[k].isEmpty() ? QString::fromUtf8("Choisir un fichier…") : v[k] + QString::fromUtf8(" (changer…)"));
                // La machine décrit le son à sa façon (« un fichier de 44 octets ») : la première valeur après un envoi
                // est celle du son envoyé ; une autre ensuite (vidé, ou venu d'un objet) n'est plus le son connu ici.
                if (auto *lecteur = qobject_cast<LecteurSon *>(bf->property("lecteur").value<QObject *>())) {
                    if (bf->property("son-envoye").toBool()) {
                        bf->setProperty("son-envoye", false);
                        bf->setProperty("valeur-son", v[k]);
                    } else if (v[k].isEmpty() || v[k] != bf->property("valeur-son").toString())
                        lecteur->donner(QByteArray(), QString());
                }
            }
            else if (z->property("radios").toBool()) {   // boutons radio (§ 22.7) : celui de la valeur, sinon aucun
                auto *g = z->findChild<QButtonGroup *>();
                if (g) {
                    g->setExclusive(false);
                    for (auto *b : g->buttons()) b->setChecked(b->property("valeur").toString() == v[k]);
                    g->setExclusive(true);
                }
            } else if (auto *p = qobject_cast<QPlainTextEdit *>(z)) {
                if (p->toPlainText() != v[k]) p->setPlainText(v[k]);
                p->document()->setModified(false);
            }
            else if (auto *c = qobject_cast<QCheckBox *>(z)) c->setChecked(v[k] == "oui");
            else if (auto *m = qobject_cast<QComboBox *>(z)) {
                // un objet créé pendant que l'écran est ouvert (« Nouveau patient ») n'est pas encore dans la liste : il
                // s'y ajoute, au lieu de laisser la zone vide (le navigateur fait de même)
                if (!v[k].isEmpty() && m->findText(v[k]) < 0) m->addItem(v[k]);
                m->setCurrentIndex(qMax(0, m->findText(v[k])));
            }
        }
    });
    connect(&travail, &Travail::ecran_etats, this, [this](const QVector<int> &e) {   // § 22.6
        for (int k = 0; k < e.size() && k < controles_ecran.size(); k++) {
            QWidget *w = controles_ecran[k];
            if (!w) continue;
            w->setVisible(!(e[k] & 2));
            QWidget *actif = k < zones.size() && zones[k] ? zones[k] : w;   // une zone : son contrôle, le libellé reste lisible
            if (!actif->property("lecture-seule").toBool()) actif->setEnabled(!(e[k] & 1));
            // § 15.7 : le micro suit l'état de sa zone, pas celui de l'écran entier ; isEnabled() regardait aussi l'écran,
            // encore désactivé à ce moment (il ne l'est plus qu'en attente) : le micro restait grisé pour de bon
            if (auto *micro = qobject_cast<QWidget *>(actif->property("micro").value<QObject *>()))
                micro->setEnabled(!(e[k] & 1) && !actif->property("lecture-seule").toBool());
        }
    });
    connect(&travail, &Travail::ecran_erreurs_zones, this, [this](const QVector<int> &els, const QStringList &msgs) {
        // les zones en faute (§ 22.6) : chacune son message sous son libellé, jusqu'au prochain geste
        effacer_erreurs_zones();
        for (int i = 0; i < els.size(); i++) {
            QWidget *conteneur = controles_ecran.value(els[i]);
            auto *rang = conteneur ? qobject_cast<QVBoxLayout *>(conteneur->layout()) : nullptr;
            if (!rang) continue;
            auto *l = new QLabel(msgs.value(i));
            l->setObjectName("erreur-zone");
            l->setWordWrap(true);
            l->setStyleSheet("color: #b3261e; font-weight: 600;");
            rang->insertWidget(qMin(1, rang->count()), l);   // sous le libellé
        }
    });
    connect(&travail, &Travail::ecran_texte, this, [this](int k, const QString &t) {   // le texte calculé (2.24)
        if (auto *l = qobject_cast<QLabel *>(controles_ecran.value(k))) l->setText(t);
    });
    connect(&travail, &Travail::ecran_image, this, [this](int k, const QByteArray &o) {   // l'image calculée (§ 22.8)
        auto *l = qobject_cast<QLabel *>(controles_ecran.value(k));
        if (!l) return;
        const QVector<int> p = k < travail.places.size() ? travail.places[k] : QVector<int>();
        montrer_image(l, o, QString(), p.size() == 4 ? p[2] : 0, p.size() == 4 ? p[3] : 0);
        if (p.size() == 4) l->resize(taille_placee(l, p[2], p[3]));   // en position libre : sa boîte suit l'image
    });
    connect(&travail, &Travail::ecran_attente, this, [this] {
        attente_ecran = true;
        if (vue_ecran) vue_ecran->setEnabled(true);
        etat->setText("L'écran attend.");
    });
    connect(&travail, &Travail::ecran_erreur, this, [this](const QString &m) {
        if (erreur_ecran) { erreur_ecran->setText(m); erreur_ecran->show(); }
    });
    connect(&travail, &Travail::ecran_ferme, this, [this] {
        if (vue_ecran) { vue_ecran->deleteLater(); vue_ecran = nullptr; }
        tables.clear();
        boutons_ecran.clear();
        zones.clear();
        controles_ecran.clear();
        erreur_ecran = nullptr;
        attente_ecran = false;
        if (!recouverts.isEmpty()) {   // l'écran du dessous revient (§ 22.3)
            const EcranMontre e = recouverts.takeLast();
            vue_ecran = e.vue;
            erreur_ecran = e.erreur;
            tables = e.tables;
            boutons_ecran = e.boutons;
            zones = e.zones;
            controles_ecran = e.controles;
            vue_ecran->show();
            vue_ecran->setEnabled(false);
            setWindowTitle(e.titre);
        }
        ranger();
    });
    connect(&travail, &Travail::reponses, this, [this](const QString &echo) {
        QTextCursor c(fil->document());
        c.movePosition(QTextCursor::End);
        QTextCharFormat format;
        format.setFont(fil->font());
        format.setForeground(Theme::courant().couleur("accent"));
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
        if (grym_interruption && attente_ecran) {
            attente_ecran = false;
            travail.issue = 2;
            travail.reponse.release();
        }
    });
    veille->start(150);
}

void Execution::demarrer() { travail.start(); }

// Le micro, à l'ouverture d'un programme qui en a besoin (§ 15.7). macOS ne demande qu'une fois : jamais décidé, la
// question part tout de suite ; déjà refusé, on dit où le rendre, avec un bouton qui ouvre la bonne page des réglages.
// Ailleurs (Linux, Windows, Qt 6.4), rien à demander.
void Execution::demander_micro() {
    setProperty("micro_demande", true);   // pour les essais : la demande est partie
    // les essais (GRYMOIR_ESSAIS) ne demandent rien à macOS : une boîte invisible (QT_QPA_PLATFORM=offscreen) les
    // bloquait jusqu'à ce qu'on les interrompe
    if (!qEnvironmentVariableIsEmpty("GRYMOIR_ESSAIS")) return;
#ifdef GRYM_AUTORISATION_MICRO
    QMicrophonePermission micro;
    switch (qApp->checkPermission(micro)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(micro, this, [this](const QPermission &p) {
            if (p.status() == Qt::PermissionStatus::Denied) demander_micro();
        });
        return;
    case Qt::PermissionStatus::Denied: {
        if (micro_signale) return;   // une fois par lancement
        micro_signale = true;
        // une boîte qui n'arrête rien : le programme continue derrière elle
        auto *boite = new QMessageBox(QMessageBox::Information, QString::fromUtf8("Micro"),
                                      QString::fromUtf8("Ce programme sait enregistrer des dictées, mais le micro lui est refusé."),
                                      QMessageBox::NoButton, this);
        boite->setAttribute(Qt::WA_DeleteOnClose);
        boite->setInformativeText(QString::fromUtf8("Pour l'autoriser : Réglages Système, Confidentialité et sécurité, "
                                                    "Microphone, puis cocher cette application. Elle se relance ensuite."));
        QPushButton *ouvrir = boite->addButton(QString::fromUtf8("Ouvrir les réglages"), QMessageBox::AcceptRole);
        boite->addButton(QString::fromUtf8("Plus tard"), QMessageBox::RejectRole);
        connect(boite, &QMessageBox::buttonClicked, this, [ouvrir](QAbstractButton *b) {
            if (b == ouvrir)
                QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"));
        });
        boite->setModal(false);
        boite->show();
        return;
    }
    case Qt::PermissionStatus::Granted:
        return;
    }
#endif
}

// Détruite pendant l'exécution (fin brutale du processus principal) : on arrête la machine, qui annule,
// et on l'attend ; un fil détruit en marche emporterait tout sans annulation.
Execution::~Execution() {
    if (travail.isRunning()) {
        grym_interruption = 1;
        attente = attente_ecran = false;
        // La machine peut être entre deux attentes : un écran fermé, celui d'en dessous pas encore annoncé (son signal
        // ne passera plus, la boucle d'événements est finie). On lui redit « fermé » jusqu'à ce qu'elle s'arrête, sans
        // jamais laisser plus d'un jeton d'avance ; avant, la fenêtre fermée sur deux écrans emboîtés attendait toujours.
        do {
            travail.issue = 2;
            if (travail.reponse.available() == 0) travail.reponse.release();
        } while (!travail.wait(50));
    }
}

void Execution::ecrire(const QString &t, bool erreur) {
    QTextCursor c(fil->document());
    c.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setFont(fil->font());
    if (erreur) format.setForeground(Theme::courant().couleur("danger"));
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
            const bool image = std::strcmp(ch->type, "image") == 0, son = std::strcmp(ch->type, "son") == 0;
            LecteurSon *lecteur = nullptr;
            EnregistreurSon *micro = nullptr;
            if (son) {   // § 15.7 : écouter ce qui est choisi ou enregistré ; enregistrer au micro
                lecteur = new LecteurSon;
                micro = new EnregistreurSon;
                lecteur->signaler = [this](const QString &m) { etat->setText(m); };
                if (fichiers_choisis[k].fichier) lecteur->donner(fichiers_choisis[k].octets, fichiers_choisis[k].nom_fichier);
                micro->setEnabled(!accepte);
                micro->enregistre = [this, k, nom, lecteur](const QByteArray &octets, const QString &n) {
                    fichiers_choisis[k].fichier = true;
                    fichiers_choisis[k].octets = octets;
                    fichiers_choisis[k].nom_fichier = n;
                    nom->setText(n);
                    lecteur->donner(octets, n);
                };
            }
            connect(choisir, &QPushButton::clicked, this, [this, k, nom, image, son, lecteur] {
                const QString c = QFileDialog::getOpenFileName(this, "Choisir un fichier", QString(),
                                                               image ? "Images (*.png *.jpg *.jpeg *.gif *.webp)"
                                                               : son ? "Sons (*.wav *.mp3 *.m4a *.aac *.ogg *.oga *.opus *.webm *.flac)" : QString());
                if (c.isEmpty()) return;
                QFile f(c);
                if (!f.open(QIODevice::ReadOnly)) return;
                fichiers_choisis[k].fichier = true;
                fichiers_choisis[k].octets = f.readAll();
                fichiers_choisis[k].nom_fichier = QFileInfo(c).fileName();
                nom->setText(fichiers_choisis[k].nom_fichier);
                if (lecteur) lecteur->donner(fichiers_choisis[k].octets, fichiers_choisis[k].nom_fichier);
            });
            h->addWidget(choisir);
            if (lecteur) { h->addWidget(lecteur); h->addWidget(micro); }
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
            if (ch->type && std::strcmp(ch->type, "date") == 0) ajouter_calendrier(ligne);   // comme dans un écran
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
                r->setProperty("erreur", true);
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
    attente = true;
    ranger();
    // sous un écran, le partage a trois volets (écran, fil, questions) : la question prend sa place en bas, le fil
    // reste replié s'il l'était ; avec deux volets seulement, l'écran en recevait la taille et la question restait
    // invisible (« Nouveau patient » depuis la séance ne montrait rien)
    QList<int> tailles;
    for (int k = 0; k < partage->count(); k++) tailles << 0;
    const int dernier = partage->count() - 1;
    const int fil_rang = partage->indexOf(fil);
    const int haut = vue_ecran && !fini ? partage->indexOf(vue_ecran) : fil_rang;
    tailles[dernier] = voulu;
    if (fil_rang >= 0 && fil_rang != haut && fil->isVisible()) {
        tailles[fil_rang] = (total - voulu) / 4;
        tailles[haut] = total - voulu - tailles[fil_rang];
    } else if (haut >= 0) {
        tailles[haut] = total - voulu;
    }
    partage->setSizes(tailles);
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
    ranger();
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
    if (attente_ecran) {
        attente_ecran = false;
        travail.issue = 2;
        travail.reponse.release();
    }
}

void Execution::montrer_ecran(const QString &titre, const QVector<int> &sortes, const QStringList &textes,
                              const QVector<QStringList> &colonnes) {
    if (vue_ecran) {   // l'écran ouvert passe dessous, masqué ; il reviendra à la fermeture de celui-ci
        vue_ecran->hide();
        recouverts.push_back({vue_ecran, erreur_ecran, tables, boutons_ecran, zones, windowTitle(), controles_ecran});
        vue_ecran = nullptr;
    }
    QVector<ElementVue> elements(sortes.size());
    for (int k = 0; k < sortes.size(); k++) {
        elements[k].sorte = sortes[k];
        elements[k].texte = textes[k];
        elements[k].colonnes = colonnes[k];
        elements[k].type = k < travail.types_zones.size() ? travail.types_zones[k] : QString();
        elements[k].choix = k < travail.choix_zones.size() ? travail.choix_zones[k] : QStringList();
        elements[k].facultatif = k < travail.facultatives.size() && travail.facultatives[k];
        elements[k].indication = travail.indications.value(k);
        elements[k].unite = travail.unites.value(k).mid(1);
        elements[k].unite_devant = travail.unites.value(k).startsWith('<');
        elements[k].longueur_max = travail.longueurs.value(k);
        if (k < travail.images.size()) elements[k].octets = travail.images[k];
        const int o = k < travail.options_zones.size() ? travail.options_zones[k] : 0;
        elements[k].plusieurs_lignes = o & 1;
        elements[k].lecture_seule = o & 2;
        elements[k].interrupteur = o & 4;   // § 22.6
        elements[k].principal = o & 64;     // 2.14
        elements[k].tuile = o & 128;        // 2.15
        elements[k].a_cote_du_titre = o & 256;   // 2.17
        elements[k].inverse = o & 512;           // 2.18
        elements[k].cartes = o & 1024;           // 2.19
        elements[k].capitales = o & 2048;        // 2.20
        elements[k].multiple = o & 8;   // § 22.1
        elements[k].recherche = o & 16;   // § 22.6
        elements[k].masque = o & 32;   // § 22.6
        elements[k].dossier = o & 4096;   // 2.32
        elements[k].graphique = travail.graphiques.value(k);   // § 22.11
        if (travail.valeurs_fiche.value(k).size() == 3) {   // une valeur de fiche (§ 22.3)
            const QVector<int> &f = travail.valeurs_fiche[k];
            elements[k].valeur_depuis = f[0];
            elements[k].etiquette = f[1] ? QChar(f[1]) : QChar();
            elements[k].lien = f[2];
        }
        if (k < travail.places.size() && travail.places[k].size() == 4) {   // position libre (§ 22.9)
            const QVector<int> &p = travail.places[k];
            elements[k].place = true;
            elements[k].gauche = p[0];
            elements[k].haut = p[1];
            elements[k].largeur = p[2];
            elements[k].hauteur = p[3];
        }
    }
    // Le logo : grand sur l'écran que le programme ouvre lui-même (aucun écran dessous), petit sur les autres.
    const int taille = Theme::courant().mesure(recouverts.isEmpty() ? "logo-accueil" : "logo-titre");
    // avec une signature (2.16), le logo ne se pose plus près du titre : il reste l'icône de la fenêtre
    VueEcran v = dessiner_ecran(titre, elements, signature.isNull() ? logo : QPixmap(), taille);   // le même dessin que l'aperçu
    taille_du_titre(v.titre, travail.taille_titre);
    vue_ecran = v.vue;
    erreur_ecran = v.erreur;
    tables = v.tables;
    zones = v.zones;
    controles_ecran = v.controles;
    boutons_ecran = v.boutons;
    setWindowTitle(titre);
    for (int k = 0; k < sortes.size(); k++) {   // les événements, branchés sur les contrôles dessinés
        if (auto *tab = v.tables[k]) {
            tab->setToolTip("Double-clic ou Entrée : choisir ; clic sur un titre : trier");
            connect(tab, &QTableWidget::cellActivated, this, [this, k, tab](int ligne, int) {
                const QTableWidgetItem *i = tab->item(ligne, 0);
                if (i && i->data(ROLE_TOTAL).toBool()) return;   // la ligne de total ne mène nulle part (§ 22.3)
                envoyer_evenement(EVENEMENT_CHOIX, k, i ? i->data(Qt::UserRole).toInt() : ligne);
            });
        } else if (QWidget *z = v.zones[k]) {   // la validation d'une zone (Entrée, ou la quitter) est un événement
            if (z->property("multiple").toBool()) {   // des choix multiples : chaque case change la zone (§ 22.1)
                for (QCheckBox *c : z->findChildren<QCheckBox *>())
                    connect(c, &QCheckBox::toggled, this, [this, k, z] {
                        QStringList coches;
                        for (QCheckBox *x : z->findChildren<QCheckBox *>()) if (x->isChecked()) coches << x->property("cle").toString();
                        texte_envoye = coches.join(QChar(0x1e)).toUtf8();
                        envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                    });
            } else if (auto *bf = qobject_cast<QPushButton *>(z); bf && bf->property("fichier").toBool()) {
                // une zone de fichier ou d'image : choisir un fichier ; ses octets et son nom font le changement
                LecteurSon *lecteur = nullptr;
                if (bf->property("son").toBool()) {   // un son (§ 15.7) : l'écouter, ou l'enregistrer au micro
                    auto *ligne = qobject_cast<QHBoxLayout *>(bf->parentWidget() ? bf->parentWidget()->layout() : nullptr);
                    lecteur = new LecteurSon;
                    auto *micro = new EnregistreurSon;
                    if (ligne) {   // avant l'espace qui pousse à droite
                        ligne->insertWidget(1, lecteur);
                        ligne->insertWidget(2, micro);
                    }
                    lecteur->signaler = [this](const QString &m) { etat->setText(m); };
                    micro->setVisible(!bf->property("lecture-seule").toBool() && bf->isEnabled());
                    bf->setProperty("lecteur", QVariant::fromValue<QObject *>(lecteur));
                    bf->setProperty("micro", QVariant::fromValue<QObject *>(micro));
                    micro->enregistre = [this, k, lecteur, bf](const QByteArray &octets, const QString &nom) {
                        lecteur->donner(octets, nom);
                        bf->setProperty("son-envoye", true);
                        octets_envoyes = octets;
                        fichier_envoye = true;
                        texte_envoye = nom.toUtf8();
                        envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                    };
                }
                connect(bf, &QPushButton::clicked, this, [this, k, bf, lecteur] {
                    const QString f = QFileDialog::getOpenFileName(this, "Choisir un fichier", QString(),
                                                                   bf->property("image").toBool()
                                                                       ? "Images (*.png *.jpg *.jpeg *.gif *.webp)"
                                                                   : lecteur ? "Sons (*.wav *.mp3 *.m4a *.aac *.ogg *.oga *.opus *.webm *.flac)"
                                                                             : QString());
                    if (f.isEmpty()) return;
                    QFile fichier(f);
                    if (!fichier.open(QIODevice::ReadOnly)) { etat->setText("Le fichier ne s'ouvre pas : " + f); return; }
                    octets_envoyes = fichier.readAll();
                    fichier_envoye = true;
                    texte_envoye = QFileInfo(f).fileName().toUtf8();
                    if (lecteur) {
                        lecteur->donner(octets_envoyes, QFileInfo(f).fileName());
                        bf->setProperty("son-envoye", true);
                    }
                    envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                });
            } else if (auto *c = qobject_cast<QCheckBox *>(z)) {
                connect(c, &QCheckBox::toggled, this, [this, k](bool x) {
                    texte_envoye = x ? "oui" : "non";
                    envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                });
            } else if (auto *m = qobject_cast<QComboBox *>(z)) {
                connect(m, &QComboBox::activated, this, [this, k, m](int) {
                    texte_envoye = m->currentText().toUtf8();
                    envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                });
                if (m->isEditable())   // une recherche (§ 22.6) : la saisie validée, ou une proposition choisie, change la zone
                    connect(m->lineEdit(), &QLineEdit::editingFinished, this, [this, k, m] {
                        if (m->currentText().toUtf8() == texte_envoye) return;
                        texte_envoye = m->currentText().toUtf8();
                        envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                    });
            } else if (z->property("radios").toBool()) {   // un bouton radio choisi : la zone change (§ 22.7)
                if (auto *g = z->findChild<QButtonGroup *>())
                    connect(g, &QButtonGroup::buttonClicked, this, [this, k](QAbstractButton *b) {
                        texte_envoye = b->property("valeur").toString().toUtf8();
                        envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                    });
            } else if (auto *p = qobject_cast<QPlainTextEdit *>(z)) {   // plusieurs lignes : l'événement à la sortie (§ 22.6)
                p->installEventFilter(new SortieDeZone(p, [this, k, p] {
                    if (!p->document()->isModified()) return;
                    p->document()->setModified(false);
                    texte_envoye = p->toPlainText().toUtf8();
                    envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                }));
            } else if (auto *l = qobject_cast<QLineEdit *>(z)) {
                connect(l, &QLineEdit::editingFinished, this, [this, k, l] {
                    if (!l->isModified()) return;   // quittée sans changement : pas d'événement
                    l->setModified(false);
                    texte_envoye = l->text().toUtf8();
                    envoyer_evenement(EVENEMENT_CHANGEMENT, k, 0);
                });
            }
        } else if (auto *b = qobject_cast<QPushButton *>(v.controles[k])) {
            connect(b, &QPushButton::clicked, this, [this, k] { envoyer_evenement(EVENEMENT_CLIC, k, 0); });
        }
    }
    // L'écran garde une largeur de lecture (« largeur-contenu ») et se centre dans une fenêtre plus large, comme une page
    auto *cadre = new QWidget;
    auto *centre = new QHBoxLayout(cadre);
    centre->setContentsMargins(0, 0, 0, 0);
    vue_ecran->setMaximumWidth(Theme::courant().mesure("largeur-contenu"));
    // rien qui ait besoin de hauteur (une liste, un graphique, un texte sur plusieurs lignes, des onglets) : l'écran
    // reste en haut, l'espace libre dessous, comme une page ; sinon Qt répartissait la hauteur entre les éléments et
    // l'écran flottait au milieu de la fenêtre. Les blocs « côte à côte » s'étirent toujours : on regarde leur contenu.
    const bool etire = [this] {   // une table en cartes (2.19) ne s'étire pas : elle est cachée, ses cartes ont leur hauteur
                           for (QTableWidget *t : vue_ecran->findChildren<QTableWidget *>()) if (!t->property("cartes").isValid()) return true;
                           return false;
                       }() || [this] {   // un texte sur plusieurs lignes posé au pixel (§ 22.9) a sa hauteur : il ne s'étire pas
                              for (QPlainTextEdit *t : vue_ecran->findChildren<QPlainTextEdit *>())
                              {
                                  bool place = false;   // la zone, ou le bloc qui la contient, est posée au pixel
                                  for (QWidget *w = t; w && w != vue_ecran && !place; w = w->parentWidget())
                                      place = w->property("place").toBool();
                                  if (!place) return true;
                              }
                              return false;
                          }()
                       || !vue_ecran->findChildren<QTabWidget *>().isEmpty()
                       || [this] {   // Graphique n'a pas de méta-objet à lui : findChildren les prendrait tous pour des graphiques
                              for (QWidget *w : vue_ecran->findChildren<QWidget *>()) if (dynamic_cast<Graphique *>(w)) return true;
                              return false;
                          }();
    if (auto *pile = qobject_cast<QBoxLayout *>(vue_ecran->layout()); pile && !etire) pile->addStretch(1);
    if (!signature.isNull())
        if (auto *pile = qobject_cast<QBoxLayout *>(vue_ecran->layout())) {   // le bandeau (2.16) : la signature, un filet dessous
            auto *bandeau = new QWidget;
            bandeau->setObjectName("bandeau");
            bandeau->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);   // l'espace libre ne va jamais au bandeau
            auto *b = new QVBoxLayout(bandeau);
            b->setContentsMargins(0, 0, 0, Theme::courant().mesure("espace-2"));
            b->setSpacing(Theme::courant().mesure("espace-4"));
            auto *image = new QLabel;
            const qreal densite = devicePixelRatioF();
            QPixmap p = signature.scaledToHeight(qRound(26 * densite), Qt::SmoothTransformation);
            p.setDevicePixelRatio(densite);
            image->setPixmap(p);
            image->setAccessibleName(QString::fromUtf8("Signature"));
            b->addWidget(image, 0, Qt::AlignLeft);
            auto *filet = new QFrame;
            filet->setFrameShape(QFrame::HLine);
            filet->setProperty("role", "filet");
            b->addWidget(filet);
            pile->insertWidget(0, bandeau);
        }
    centre->addWidget(vue_ecran, 1);
    // plus haut ou plus large que la fenêtre, l'écran défile (un ascenseur) ; sinon, rien ne change à ce qu'on voit
    auto *defile = new QScrollArea;
    defile->setObjectName("defilement-ecran");
    defile->setFrameShape(QFrame::NoFrame);
    defile->setWidgetResizable(true);
    defile->setWidget(cadre);
    defile->viewport()->setAutoFillBackground(false);
    cadre->setAutoFillBackground(false);
    if (ajuster_fenetre && recouverts.isEmpty()) {
        // un écran qui n'en recouvre aucun (la connexion, puis l'accueil) décide de la largeur de la fenêtre : la sienne,
        // plus les marges, jamais plus que l'écran disponible ; la hauteur est celle de l'écran disponible ; les écrans
        // qu'il ouvre gardent cette fenêtre, et défilent s'ils sont plus grands
        if (QScreen *ecran = screen() ? screen() : QGuiApplication::primaryScreen()) {
            const QRect dispo = ecran->availableGeometry();
            const QSize voulu = cadre->sizeHint() + QSize(2 * Theme::courant().mesure("espace-6") + 24, 2 * Theme::courant().mesure("espace-6") + 24);
            const int l = qMin(qMax(voulu.width(), Theme::courant().mesure("largeur-contenu") + 48), dispo.width());
            const int h = dispo.height();   // toute la hauteur de l'écran (20 octobre 2026) : la largeur suit l'accueil
            setWindowState(windowState() & ~(Qt::WindowMaximized | Qt::WindowFullScreen));
            setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, QSize(l, h), dispo));
        }
    }
    vue_ecran = defile;
    vue_ecran->setEnabled(false);   // actif seulement quand la machine attend un événement
    partage->insertWidget(0, vue_ecran);
    partage->setSizes({height() * 2 / 3, height() / 6, height() / 6});
    ranger();
}

void Execution::ranger() {
    const bool ecran = vue_ecran && !fini;
    fil->setVisible(!ecran || console);
    bas->setVisible(!ecran || console || attente);
}

void Execution::effacer_erreurs_zones() {
    for (QWidget *c : controles_ecran)
        if (c) for (QLabel *l : c->findChildren<QLabel *>("erreur-zone", Qt::FindDirectChildrenOnly)) l->deleteLater();
}

void Execution::envoyer_evenement(SorteEvenement sorte, int element, int ligne) {
    effacer_erreurs_zones();   // les messages tiennent jusqu'au prochain geste (§ 22.6)
    if (!attente_ecran) return;
    attente_ecran = false;
    if (vue_ecran) vue_ecran->setEnabled(false);
    if (erreur_ecran) erreur_ecran->hide();
    etat->setText("En cours…");
    // la ligne sélectionnée de chaque liste accompagne l'événement : « le compositeur choisi de l'écran »
    lignes_choisies = QVector<long>(tables.size(), -1);
    for (int k = 0; k < tables.size(); k++)
        if (tables[k] && tables[k]->currentRow() >= 0 && tables[k]->item(tables[k]->currentRow(), 0))
            lignes_choisies[k] = tables[k]->item(tables[k]->currentRow(), 0)->data(Qt::UserRole).toInt();
    const bool avec_fichier = sorte == EVENEMENT_CHANGEMENT && fichier_envoye;
    travail.evenement = {sorte, (size_t)element, (size_t)ligne,
                         sorte == EVENEMENT_CHANGEMENT ? texte_envoye.constData() : nullptr,
                         lignes_choisies.constData(), (size_t)lignes_choisies.size(),
                         avec_fichier ? reinterpret_cast<const unsigned char *>(octets_envoyes.constData()) : nullptr,
                         avec_fichier ? (size_t)octets_envoyes.size() : 0};
    fichier_envoye = false;   // les octets restent dans octets_envoyes jusqu'à l'événement suivant : la machine les lit
    travail.issue = 0;
    travail.reponse.release();
}

void Execution::terminer(bool ok, const QString &erreur, const QString &annulation) {
    fini = true;
    attente = false;
    ranger();
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
    fermer_a_la_fin = true;
    if (attente_ecran) {   // la croix d'un écran le ferme ; le programme continue, puis la fenêtre se ferme (§ 22.2)
        envoyer_evenement(EVENEMENT_FERMETURE, 0, 0);
        e->ignore();
        return;
    }
    arreter();   // ailleurs, fermer pendant l'exécution l'arrête d'abord, proprement
    e->ignore();
}

// --- Les sons du fil (§ 15.7) ---

int Execution::lien_son(const QString &nom, const QByteArray &octets) {
    sons_du_fil.append({nom, octets});
    return sons_du_fil.size() - 1;
}

void Execution::jouer_son_du_fil(int k) {
    if (k < 0 || k >= sons_du_fil.size()) return;
    if (k != son_joue) {   // un autre son : il remplace le précédent
        lecteur_fil->donner(sons_du_fil[k].second, sons_du_fil[k].first);
        son_joue = k;
    }
    lecteur_fil->basculer();   // le même : arrêter, ou reprendre au début
}
