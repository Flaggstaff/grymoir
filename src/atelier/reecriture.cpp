// GrymoiR : l'atelier, réécriture chirurgicale.
#include "reecriture.h"
#include "editeur.h"
#include "schema.h"

extern "C" {
#include "interface.h"
}

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QRegularExpression>
#include <QVector>
#include <memory>

#include <algorithm>
#include <functional>

extern "C" {
#include "analyseur.h"
#include "imprimeur.h"
#include "texte.h"
}

namespace {

// Un fichier du projet, analysé seul (ses fichiers utilisés servent à l'analyse, pas à la réécriture).
struct FichierAnalyse {
    QString chemin, texte;
    bool compacte = false, ok = false;
    Programme p = {};
    QVector<int> utf16;   // point de code k → position dans `texte` (le lexeur compte en points de code)

    FichierAnalyse() = default;
    FichierAnalyse(const FichierAnalyse &) = delete;
    FichierAnalyse &operator=(const FichierAnalyse &) = delete;
    ~FichierAnalyse() { programme_liberer(&p); }

    bool lire(const QString &c) {
        chemin = c;
        compacte = c.endsWith(".grymc", Qt::CaseInsensitive);
        QFile f(c);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray octets = f.readAll();
        texte = QString::fromUtf8(octets);
        for (int i = 0; i < texte.size(); i++) {
            utf16 << i;
            if (texte.at(i).isHighSurrogate() && i + 1 < texte.size()) i++;
        }
        utf16 << texte.size();
        Portee *portee = portee_creer();
        const QByteArray cc = c.toUtf8();
        portee_fichier(portee, cc.constData());
        Diagnostic d = {};
        ok = compacte ? analyser_compact(octets.constData(), (size_t)octets.size(), portee, &p, &d)
                      : analyser(octets.constData(), (size_t)octets.size(), portee, 0, &p, &d);
        diagnostic_liberer(&d);
        portee_detruire(portee);
        return true;
    }
    // Un texte qui n'est pas en NFC décalerait les positions du lexeur : on ne le réécrit pas.
    bool nfc() const { return texte.normalized(QString::NormalizationForm_C) == texte; }
    int pos(size_t point_de_code) const { return utf16.value((int)qMin(point_de_code, (size_t)utf16.size() - 1)); }

    // Étendue de la phrase k de premier niveau : de son premier jeton au point final (avant la phrase suivante).
    QPair<int, int> etendue(size_t k) const {
        const int debut = pos(p.phrases[k]->debut);
        int fin = k + 1 < p.nb ? pos(p.phrases[k + 1]->debut) : texte.size();
        while (fin > debut && texte.at(fin - 1).isSpace()) fin--;
        return {debut, fin};
    }

    // Une phrase, imprimée seule dans la forme du fichier, sans son dernier saut de ligne.
    QString imprimer(Noeud *n) const {
        char *t = imprimer_phrase(ok ? &p : nullptr, n, compacte);   // les accords selon les déclarations du fichier
        QString r = QString::fromUtf8(t);
        free(t);
        while (r.endsWith('\n')) r.chop(1);
        return r;
    }
};

using Projet = std::vector<std::unique_ptr<FichierAnalyse>>;

Projet lire_projet(const QString &dossier) {
    Projet r;
    QStringList chemins;
    QDirIterator it(dossier, {"*.grym", "*.grymc"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) chemins << QFileInfo(it.next()).absoluteFilePath();
    chemins.sort();
    for (const QString &c : chemins) {
        auto f = std::make_unique<FichierAnalyse>();
        if (f->lire(c)) r.push_back(std::move(f));
    }
    return r;
}

bool est_entite(const Noeud *n, const QString &nom) {
    return n->type == P_CLASSE && (n->forme & 32) && QString::fromUtf8(n->texte) == nom;
}

// Remplacements dans un fichier : (début, fin, texte), appliqués du dernier au premier.
struct Remplacement {
    int debut, fin;
    QString texte;
};

class Chantier {
public:
    explicit Chantier(const QString &d) : dossier(d), projet(lire_projet(d)) {
        for (auto &f : projet) avant_ok.insert(f->chemin, f->ok);
    }

    FichierAnalyse *fichier(const QString &chemin) {
        for (auto &f : projet)
            if (f->chemin == QFileInfo(chemin).absoluteFilePath()) return f.get();
        return nullptr;
    }

    // Chaque phrase de premier niveau des fichiers qui s'analysent, avec son fichier et son rang.
    void pour_chaque_phrase(const std::function<void(FichierAnalyse &, size_t)> &f) {
        for (auto &x : projet)
            if (x->ok)
                for (size_t k = 0; k < x->p.nb; k++) f(*x, k);
    }

    void remplacer(FichierAnalyse &f, int debut, int fin, const QString &texte) {
        travaux[f.chemin].push_back({debut, fin, texte});
    }

    // Écrit les fichiers touchés, réanalyse le projet ; annule tout si un fichier sain est cassé.
    QString conclure(Geste *geste) {
        if (travaux.isEmpty()) return "Rien à changer.";
        for (auto i = travaux.begin(); i != travaux.end(); ++i) {
            FichierAnalyse *f = fichier(i.key());
            if (!f->nfc()) return QString("« %1 » contient des lettres décomposées : ouvrez-le et enregistrez-le d'abord.")
                                  .arg(QFileInfo(f->chemin).fileName());
        }
        geste->avant.clear();
        for (auto i = travaux.begin(); i != travaux.end(); ++i) {
            FichierAnalyse *f = fichier(i.key());
            QString t = f->texte;
            auto &r = i.value();
            std::sort(r.begin(), r.end(), [](const Remplacement &a, const Remplacement &b) { return a.debut > b.debut; });
            for (const auto &x : r) t.replace(x.debut, x.fin - x.debut, x.texte);
            geste->avant.insert(f->chemin, f->texte.toUtf8());
            QSaveFile s(f->chemin);
            if (!s.open(QIODevice::WriteOnly) || s.write(t.toUtf8()) < 0 || !s.commit()) {
                annuler_geste(*geste);
                return QString("« %1 » n'a pas pu être écrit.").arg(f->chemin);
            }
        }
        // Le projet après le geste : un fichier qui s'analysait doit s'analyser encore.
        for (const QString &c : avant_ok.keys()) {
            if (!avant_ok.value(c)) continue;
            QFile f(c);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const Diagnostic_atelier d = analyser_source(QString::fromUtf8(f.readAll()), c.endsWith(".grymc"), c);
            if (d.message.isEmpty()) continue;
            annuler_geste(*geste);
            return QString("Geste annulé : il casserait « %1 », ligne %2 : %3")
                .arg(QDir(dossier).relativeFilePath(c)).arg(d.ligne).arg(d.message);
        }
        return QString();
    }

    QString dossier;
    Projet projet;
    QMap<QString, bool> avant_ok;
    QMap<QString, std::vector<Remplacement>> travaux;
};

// Une valeur de départ, lue par l'analyseur lui-même : « Le v vaut 12,50. », pour un champ de ce type.
Noeud *valeur_de_depart(const ChampVoulu &c, QString *erreur) {
    const QString v = c.depart.trimmed();
    const QString ecrit = c.type == "texte" ? QString("« %1 »").arg(v) : v;
    const QByteArray source = QString("Le v vaut %1.").arg(ecrit).toUtf8();
    Portee *portee = portee_creer();
    Programme p = {};
    Diagnostic d = {};
    Noeud *r = nullptr;
    if (analyser(source.constData(), (size_t)source.size(), portee, 0, &p, &d) && p.nb == 1
        && p.phrases[0]->type == P_CREATION && p.phrases[0]->nb_enfants == 1) {
        const TypeNoeud t = p.phrases[0]->enfants[0]->type;
        if (t == N_TEXTE || t == N_NOMBRE || t == N_DATE || t == N_BOOLEEN || t == N_NEGATION) {
            r = p.phrases[0]->enfants[0];
            p.phrases[0]->nb_enfants = 0;
        }
    }
    if (!r) *erreur = QString("« %1 » n'est pas une valeur de départ pour un champ (%2) : une constante, "
                              "comme « Suisse », 12,50, 01.01.2026 ou vrai.").arg(v, c.type);
    diagnostic_liberer(&d);
    programme_liberer(&p);
    portee_detruire(portee);
    return r;
}

// Nœud champ, tel que l'analyseur l'écrit (arbre.h, P_CLASSE) ; nullptr et *erreur si la valeur de départ est illisible.
Noeud *nouveau_champ(const ChampVoulu &c, QString *erreur = nullptr) {
    Noeud *depart = nullptr;
    if (!c.depart.trimmed().isEmpty() && !c.plusieurs) {
        QString e;
        depart = valeur_de_depart(c, &e);
        if (!depart) {
            if (erreur) *erreur = e;
            return nullptr;
        }
    }
    Noeud *n = noeud_creer(N_NOM, 0, 0, 0);
    n->texte = grym_dupliquer(c.nom.toUtf8().constData());
    n->texte2 = grym_dupliquer(c.type.toUtf8().constData());
    n->forme = c.plusieurs ? 3 : c.feminin ? 2 : 1;
    if (c.unique && !c.plusieurs) n->op = 'U';
    if (c.facultatif && !c.plusieurs) n->entier |= 1;
    if (c.cascade && !c.plusieurs) n->entier |= 2;
    if (depart) noeud_ajouter(n, depart);
    return n;
}

QString nom_valide(const QString &nom) {
    if (nom.trimmed().isEmpty()) return "Donnez un nom.";
    if (nom.contains(QRegularExpression("[.,:;«»\"()\\[\\]]")))
        return QString("« %1 » : un nom ne contient ni ponctuation ni parenthèses.").arg(nom);
    return QString();
}

// Le champ `nom` d'une entité : la déclaration qui le porte (l'entité ou son complément « Un x a : »).
struct Trouve {
    FichierAnalyse *f = nullptr;
    size_t k = 0;
    Noeud *phrase = nullptr;
    size_t enfant = 0;
};

Trouve trouver_champ(Chantier &ch, const QString &entite, const QString &nom) {
    Trouve t;
    ch.pour_chaque_phrase([&](FichierAnalyse &f, size_t k) {
        Noeud *n = f.p.phrases[k];
        if (t.f || !est_entite(n, entite)) return;
        for (size_t j = 0; j < n->nb_enfants; j++)
            if (n->enfants[j]->type == N_NOM && QString::fromUtf8(n->enfants[j]->texte) == nom) {
                t = {&f, k, n, j};
                return;
            }
    });
    return t;
}

// La déclaration qui reçoit les champs propres : le complément s'il existe, sinon la déclaration elle-même.
Trouve trouver_porteur(Chantier &ch, const QString &entite) {
    Trouve t;
    ch.pour_chaque_phrase([&](FichierAnalyse &f, size_t k) {
        Noeud *n = f.p.phrases[k];
        if (!est_entite(n, entite)) return;
        const bool complement = n->forme & 4, par_est = n->forme & 16;
        if (complement || (!par_est && !t.f) || !t.f) t = {&f, k, n, 0};
    });
    return t;
}

}  // namespace

QString annuler_geste(const Geste &geste) {
    QString erreur;
    for (auto i = geste.avant.constBegin(); i != geste.avant.constEnd(); ++i) {
        QSaveFile s(i.key());
        if (!s.open(QIODevice::WriteOnly) || s.write(i.value()) < 0 || !s.commit())
            erreur = QString("« %1 » n'a pas pu être rétabli.").arg(i.key());
    }
    return erreur;
}

QString ajouter_entite(const QString &dossier, const QString &fichier, const QString &nom, bool feminin, Geste *geste) {
    if (QString e = nom_valide(nom); !e.isEmpty()) return e;
    Chantier ch(dossier);
    FichierAnalyse *f = ch.fichier(fichier);
    if (!f) return QString("« %1 » n'est pas dans le projet.").arg(fichier);
    if (!f->ok) return QString("« %1 » contient une erreur : corrigez-la d'abord.").arg(QFileInfo(fichier).fileName());
    // Après la dernière déclaration d'entité ou de classe ; à défaut, après les remarques et « Utiliser » de tête.
    int apres = -1;
    for (size_t k = 0; k < f->p.nb; k++) {
        const TypeNoeud t = f->p.phrases[k]->type;
        if (t == P_CLASSE || t == P_APTITUDE) apres = (int)k;
    }
    if (apres < 0)
        for (size_t k = 0; k < f->p.nb && (f->p.phrases[k]->type == P_REMARQUE || f->p.phrases[k]->type == P_UTILISER); k++)
            apres = (int)k;
    Noeud *n = noeud_creer(P_CLASSE, 0, 0, 0);
    n->texte = grym_dupliquer(nom.toUtf8().constData());
    n->forme = (feminin ? 2 : 1) | 32;
    ChampVoulu cle;   // un nom unique : la clé qu'un formulaire montre pour désigner l'objet (§ 19)
    cle.nom = "nom";
    cle.type = "texte";
    cle.unique = true;
    noeud_ajouter(n, nouveau_champ(cle));
    const QString texte = f->imprimer(n);
    noeud_liberer(n);
    if (apres < 0) ch.remplacer(*f, 0, 0, texte + "\n");
    else {
        const int fin = f->etendue((size_t)apres).second;
        ch.remplacer(*f, fin, fin, "\n" + texte);
    }
    geste->description = QString("Ajouter l'entité « %1 »").arg(nom);
    return ch.conclure(geste);
}

QString renommer_entite(const QString &dossier, const QString &ancien, const QString &nouveau, Geste *geste) {
    if (QString e = nom_valide(nouveau); !e.isEmpty()) return e;
    Chantier ch(dossier);
    // La déclaration, ses compléments, les liens qui la désignent et les entités qui en héritent : tout suit.
    ch.pour_chaque_phrase([&](FichierAnalyse &f, size_t k) {
        Noeud *n = f.p.phrases[k];
        if (n->type != P_CLASSE) return;
        bool touche = false;
        if (QString::fromUtf8(n->texte) == ancien) { free(n->texte); n->texte = grym_dupliquer(nouveau.toUtf8().constData()); touche = true; }
        if (n->texte2 && QString::fromUtf8(n->texte2) == ancien) { free(n->texte2); n->texte2 = grym_dupliquer(nouveau.toUtf8().constData()); touche = true; }
        for (size_t j = 0; j < n->nb_enfants; j++) {
            Noeud *c = n->enfants[j];
            if (c->type == N_NOM && c->texte2 && QString::fromUtf8(c->texte2) == ancien) {
                free(c->texte2);
                c->texte2 = grym_dupliquer(nouveau.toUtf8().constData());
                touche = true;
            }
        }
        if (touche) {
            const auto e = f.etendue(k);
            ch.remplacer(f, e.first, e.second, f.imprimer(n));
        }
    });
    geste->description = QString("Renommer l'entité « %1 » en « %2 »").arg(ancien, nouveau);
    return ch.conclure(geste);
}

QString supprimer_entite(const QString &dossier, const QString &nom, Geste *geste) {
    Chantier ch(dossier);
    ch.pour_chaque_phrase([&](FichierAnalyse &f, size_t k) {
        if (!est_entite(f.p.phrases[k], nom)) return;
        auto e = f.etendue(k);
        int fin = e.second;
        if (fin < f.texte.size() && f.texte.at(fin) == '\n') fin++;   // la ligne entière disparaît
        ch.remplacer(f, e.first, fin, QString());
    });
    geste->description = QString("Supprimer l'entité « %1 »").arg(nom);
    return ch.conclure(geste);
}

QString ajouter_champ(const QString &dossier, const QString &entite, const ChampVoulu &champ, Geste *geste) {
    if (QString e = nom_valide(champ.nom); !e.isEmpty()) return e;
    Chantier ch(dossier);
    Trouve t = trouver_porteur(ch, entite);
    if (!t.f) return QString("L'entité « %1 » est introuvable, ou son fichier contient une erreur.").arg(entite);
    QString erreur;
    Noeud *neuf = nouveau_champ(champ, &erreur);
    if (!neuf) return erreur;
    noeud_ajouter(t.phrase, neuf);
    const auto e = t.f->etendue(t.k);
    ch.remplacer(*t.f, e.first, e.second, t.f->imprimer(t.phrase));
    geste->description = QString("Ajouter le champ « %1 » à « %2 »").arg(champ.nom, entite);
    return ch.conclure(geste);
}

QString modifier_champ(const QString &dossier, const QString &entite, const QString &ancien, const ChampVoulu &champ,
                       Geste *geste) {
    if (QString e = nom_valide(champ.nom); !e.isEmpty()) return e;
    Chantier ch(dossier);
    Trouve t = trouver_champ(ch, entite, ancien);
    if (!t.f) return QString("Le champ « %1 » de « %2 » est introuvable.").arg(ancien, entite);
    Noeud *vieux = t.phrase->enfants[t.enfant];
    QString erreur;
    Noeud *neuf = nouveau_champ(champ, &erreur);
    if (!neuf) return erreur;
    t.phrase->enfants[t.enfant] = neuf;
    noeud_liberer(vieux);
    const auto e = t.f->etendue(t.k);
    ch.remplacer(*t.f, e.first, e.second, t.f->imprimer(t.phrase));
    geste->description = QString("Modifier le champ « %1 » de « %2 »").arg(ancien, entite);
    return ch.conclure(geste);
}

QString supprimer_champ(const QString &dossier, const QString &entite, const QString &nom, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_champ(ch, entite, nom);
    if (!t.f) return QString("Le champ « %1 » de « %2 » est introuvable.").arg(nom, entite);
    size_t restants = 0;
    for (size_t j = 0; j < t.phrase->nb_enfants; j++) restants += t.phrase->enfants[j]->type == N_NOM;
    if (restants <= 1 && !(t.phrase->forme & 4))
        return QString("« %1 » est le dernier champ de « %2 » : une entité garde au moins un champ. Supprimez plutôt l'entité.")
            .arg(nom, entite);
    noeud_liberer(t.phrase->enfants[t.enfant]);
    for (size_t j = t.enfant; j + 1 < t.phrase->nb_enfants; j++) t.phrase->enfants[j] = t.phrase->enfants[j + 1];
    t.phrase->nb_enfants--;
    auto e = t.f->etendue(t.k);
    if (restants <= 1) {   // un complément « Un x a : » vidé disparaît entier
        if (e.second < t.f->texte.size() && t.f->texte.at(e.second) == '\n') e.second++;
        ch.remplacer(*t.f, e.first, e.second, QString());
    } else {
        ch.remplacer(*t.f, e.first, e.second, t.f->imprimer(t.phrase));
    }
    geste->description = QString("Supprimer le champ « %1 » de « %2 »").arg(nom, entite);
    return ch.conclure(geste);
}

// ---------------------------------------------------------------------------------------------
// A4-b : écrire des écrans (docs/atelier.md, § 5 bis)
// ---------------------------------------------------------------------------------------------

namespace {

bool voyelle(const QString &m) {   // « l'œuvre », « de l'orgue » : devant une voyelle ou un h (muet par défaut)
    return !m.isEmpty() && QString("aeiouyhàâäéèêëîïôöùûüœAEIOUYHÀÂÉÈÊÎÔÙÛŒ").contains(m.at(0));
}
QString le(const QString &m, bool f) { return voyelle(m) ? "l'" + m : (f ? "la " : "le ") + m; }
QString un(const QString &m, bool f) { return (f ? "une " : "un ") + m; }
QString du(const QString &m, bool f) { return voyelle(m) ? "de l'" + m : (f ? "de la " : "du ") + m; }
QString nouveau(const QString &m, bool f) { return f ? "une nouvelle " + m : voyelle(m) ? "un nouvel " + m : "un nouveau " + m; }
QString capitale(const QString &s) { return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1); }

// Après la dernière déclaration (classe, aptitude, écran, événement) ; à défaut après les remarques et
// « Utiliser » de tête ; à défaut au début. Un écran se déclare avant d'être ouvert (§ 22).
int position_des_ecrans(const FichierAnalyse &f) {
    int apres = -1;
    for (size_t k = 0; k < f.p.nb; k++) {
        const TypeNoeud t = f.p.phrases[k]->type;
        if (t == P_CLASSE || t == P_APTITUDE || t == P_ECRAN || t == P_QUAND) apres = (int)k;
    }
    if (apres < 0)
        for (size_t k = 0; k < f.p.nb && (f.p.phrases[k]->type == P_REMARQUE || f.p.phrases[k]->type == P_UTILISER); k++)
            apres = (int)k;
    return apres < 0 ? 0 : f.etendue((size_t)apres).second;
}

QString evenement_fermer(const QString &ecran) {
    return QString("Quand on clique sur « Fermer » dans l'écran %1 :\n    Fermer l'écran.\n").arg(ecran);
}

}  // namespace

QString generer_ecran(const QString &dossier, const QString &cible, const QString &entite, Geste *geste) {
    Chantier ch(dossier);
    FichierAnalyse *f = ch.fichier(cible);
    if (!f) return QString("« %1 » n'est pas dans le projet.").arg(cible);
    if (!f->ok) return QString("« %1 » contient une erreur : corrigez-la d'abord.").arg(QFileInfo(cible).fileName());
    if (f->compacte) return QString("Les écrans générés s'écrivent en forme littéraire : choisissez un fichier .grym.");
    EntiteSchema e;
    for (const auto &x : lire_schema(dossier)) if (x.nom == entite) e = x;
    if (e.nom.isEmpty()) return QString("L'entité « %1 » est introuvable.").arg(entite);
    const QString pl = e.pluriel, nom = "des " + pl, titre = capitale(pl);
    const bool fe = e.feminin;
    QString cle;   // le tri : le champ texte unique, la clé des menus et des fiches
    for (const auto &c : e.champs) if (cle.isEmpty() && c.unique && c.type == "texte" && !c.multiple) cle = c.nom;
    bool existe = false, accueil = false;
    Noeud *n_accueil = nullptr;
    FichierAnalyse *f_accueil = nullptr;
    size_t k_accueil = 0;
    ch.pour_chaque_phrase([&](FichierAnalyse &x, size_t k) {
        Noeud *n = x.p.phrases[k];
        if (n->type != P_ECRAN) return;
        if (QString::fromUtf8(n->texte) == nom) existe = true;
        if (QString::fromUtf8(n->texte) == "d'accueil") { accueil = true; n_accueil = n; f_accueil = &x; k_accueil = k; }
    });
    if (existe) return QString("L'écran %1 existe déjà.").arg(nom);
    QString t;
    t += QString("L'écran %1 montre :\n    la liste des %2 %3%4,\n").arg(nom, pl, fe ? "conservées" : "conservés",
                                                                    cle.isEmpty() ? QString() : ", par " + cle);
    t += "    un bouton « Nouveau »,\n    un bouton « Supprimer »,\n    un bouton « Fermer ».\n";
    t += QString("Quand on choisit %1 dans l'écran %2 :\n    Ouvrir la fiche %3.\n").arg(un(entite, fe), nom, du(entite, fe));
    t += QString("Quand on clique sur « Nouveau » dans l'écran %1 :\n    Le nouveau vaut %2 %3.\n    Conserver nouveau.\n")
             .arg(nom, nouveau(entite, fe), fe ? "saisie" : "saisi");
    const QString choisi = le(entite + (fe ? " choisie" : " choisi"), fe) + " de l'écran";
    t += QString("Quand on clique sur « Supprimer » dans l'écran %1 :\n    Si %2 est %3, supprimer %2.\n")
             .arg(nom, choisi, fe ? "présente" : "présent");
    t += evenement_fermer(nom);
    // l'écran d'accueil : un bouton par écran généré ; il naît au premier (§ 5 bis)
    if (!accueil) {
        t += QString("L'écran d'accueil montre :\n    un bouton « %1 »,\n    un bouton « Fermer ».\n").arg(titre);
        t += evenement_fermer("d'accueil");
    } else {
        for (size_t q = 0; q < n_accueil->nb_enfants; q++)
            if (n_accueil->enfants[q]->type == N_BOUTON && QString::fromUtf8(n_accueil->enfants[q]->texte) == titre)
                return QString("L'écran d'accueil a déjà un bouton « %1 ».").arg(titre);
        Noeud *b = noeud_creer(N_BOUTON, 0, 0, 0);
        b->texte = grym_dupliquer(titre.toUtf8().constData());
        noeud_ajouter(n_accueil, b);
        size_t fermer = n_accueil->nb_enfants - 1;   // avant « Fermer », s'il existe : il reste le dernier
        for (size_t q = 0; q + 1 < n_accueil->nb_enfants; q++)
            if (n_accueil->enfants[q]->type == N_BOUTON && strcmp(n_accueil->enfants[q]->texte, "Fermer") == 0) fermer = q;
        for (size_t q = n_accueil->nb_enfants - 1; q > fermer; q--) n_accueil->enfants[q] = n_accueil->enfants[q - 1];
        n_accueil->enfants[fermer] = b;
        const auto et = f_accueil->etendue(k_accueil);
        ch.remplacer(*f_accueil, et.first, et.second, f_accueil->imprimer(n_accueil));
    }
    t += QString("Quand on clique sur « %1 » dans l'écran d'accueil :\n    Ouvrir l'écran %2.\n").arg(titre, nom);
    const int pos = position_des_ecrans(*f);
    ch.remplacer(*f, pos, pos, (pos ? "\n" : "") + t.left(t.size() - (pos ? 1 : 0)) + (pos ? "" : "\n"));
    geste->description = QString("Écran pour « %1 »").arg(entite);
    return ch.conclure(geste);
}

QString nouvel_ecran(const QString &dossier, const QString &cible, const QString &nom, Geste *geste) {
    Chantier ch(dossier);
    FichierAnalyse *f = ch.fichier(cible);
    if (!f) return QString("« %1 » n'est pas dans le projet.").arg(cible);
    if (!f->ok) return QString("« %1 » contient une erreur : corrigez-la d'abord.").arg(QFileInfo(cible).fileName());
    if (f->compacte) return QString("Les écrans générés s'écrivent en forme littéraire : choisissez un fichier .grym.");
    const QString n = nom.simplified();
    if (n.isEmpty()) return "Donnez un nom : « de recherche », « des factures », « d'accueil ».";
    QString t = QString("L'écran %1 montre :\n    un bouton « Fermer ».\n").arg(n) + evenement_fermer(n);
    const int pos = position_des_ecrans(*f);
    ch.remplacer(*f, pos, pos, (pos ? "\n" : "") + t.left(t.size() - (pos ? 1 : 0)) + (pos ? "" : "\n"));
    geste->description = QString("Nouvel écran « %1 »").arg(n);
    return ch.conclure(geste);
}

QString ajouter_phrase_finale(const QString &dossier, const QString &fichier, const QString &phrase, Geste *geste) {
    Chantier ch(dossier);
    FichierAnalyse *f = ch.fichier(fichier);
    if (!f) return QString("« %1 » n'est pas dans le projet.").arg(fichier);
    QString t = f->texte;
    int fin = t.size();
    while (fin > 0 && t.at(fin - 1).isSpace()) fin--;
    ch.remplacer(*f, fin, t.size(), (fin ? "\n" : "") + phrase + "\n");
    geste->description = QString("Ajouter « %1 »").arg(phrase);
    return ch.conclure(geste);
}

// ---------------------------------------------------------------------------------------------
// A4-c : modifier un écran (docs/atelier.md, § 5 bis)
// ---------------------------------------------------------------------------------------------

namespace {

Trouve trouver_ecran(Chantier &ch, const QString &nom) {
    Trouve t;
    ch.pour_chaque_phrase([&](FichierAnalyse &f, size_t k) {
        Noeud *n = f.p.phrases[k];
        if (!t.f && n->type == P_ECRAN && QString::fromUtf8(n->texte) == nom) t = {&f, k, n, 0};
    });
    return t;
}

Trouve trouver_evenement(Chantier &ch, const QString &ecran, int forme, const QString &objet) {
    Trouve t;
    ch.pour_chaque_phrase([&](FichierAnalyse &f, size_t k) {
        Noeud *n = f.p.phrases[k];
        if (!t.f && n->type == P_QUAND && n->forme == forme && QString::fromUtf8(n->texte3) == ecran
            && QString::fromUtf8(n->enfants[2]->texte) == objet) t = {&f, k, n, 0};
    });
    return t;
}

void reimprimer(Chantier &ch, const Trouve &t) {
    const auto e = t.f->etendue(t.k);
    ch.remplacer(*t.f, e.first, e.second, t.f->imprimer(t.phrase));
}

void retirer(Chantier &ch, const Trouve &t) {   // la phrase, et sa fin de ligne
    auto e = t.f->etendue(t.k);
    if (e.second < t.f->texte.size() && t.f->texte.at(e.second) == '\n') e.second++;
    ch.remplacer(*t.f, e.first, e.second, QString());
}

// Après le dernier événement de l'écran, dans son fichier ; à défaut, après sa déclaration.
int apres_l_ecran(const Trouve &t, const QString &ecran) {
    size_t dernier = t.k;
    for (size_t k = t.k + 1; k < t.f->p.nb; k++) {
        const Noeud *n = t.f->p.phrases[k];
        if (n->type == P_QUAND && QString::fromUtf8(n->texte3) == ecran) dernier = k;
    }
    return t.f->etendue(dernier).second;
}

void renommer_evenement(Chantier &ch, const QString &ecran, int forme, const QString &ancien, const QString &nouveau) {
    Trouve q = trouver_evenement(ch, ecran, forme, ancien);
    if (!q.f) return;
    Noeud *o = q.phrase->enfants[2];
    free(o->texte);
    o->texte = grym_dupliquer(nouveau.toUtf8().constData());
    reimprimer(ch, q);
}

// Les colonnes choisies : « le titre ␝ titre ␞ la date de naissance ␝ date de naissance ».
QString colonnes_ecrites(const QString &entite, const QString &dossier, const QStringList &champs) {
    EntiteSchema e;
    for (const auto &x : lire_schema(dossier)) if (x.nom == entite) e = x;
    QStringList r;
    for (const QString &c : champs) {
        bool f = false;
        for (const auto &ch : e.champs) if (ch.nom == c) f = ch.feminin;
        r << le(c, f) + QChar(0x1d) + c;
    }
    return r.join(QChar(0x1e));
}

Noeud *nouvel_element(const ElementNouveau &e, const QString &dossier, QString *erreur) {
    Noeud *n = nullptr;
    const QByteArray t = e.texte.trimmed().toUtf8();
    if (e.texte.trimmed().isEmpty()) { *erreur = "Donnez un libellé, un texte ou un nom."; return nullptr; }
    if (e.sorte == ELEMENT_BOUTON || e.sorte == ELEMENT_TEXTE) {
        if (e.texte.contains(QChar(0x00BB)) || e.texte.contains(QChar(0x00AB))) { *erreur = "Pas de guillemets « » dans un libellé."; return nullptr; }
        n = noeud_creer(e.sorte == ELEMENT_BOUTON ? N_BOUTON : N_TEXTE_ECRAN, 0, 0, 0);
        n->texte = grym_dupliquer(t.constData());
    } else if (e.sorte == ELEMENT_ZONE) {
        if (QString e2 = nom_valide(e.texte); !e2.isEmpty()) { *erreur = e2; return nullptr; }
        n = noeud_creer(N_NOM, 0, 0, 0);
        n->texte = grym_dupliquer(t.constData());
        n->texte2 = grym_dupliquer(e.type.toUtf8().constData());
        n->forme = e.feminin ? 2 : 1;
        if (e.facultatif) n->entier |= 1;
        if (!e.depart.trimmed().isEmpty()) {
            ChampVoulu c;
            c.type = e.type;
            c.depart = e.depart;
            Noeud *d = valeur_de_depart(c, erreur);
            if (!d) { noeud_liberer(n); return nullptr; }
            noeud_ajouter(n, d);
        }
    } else if (e.sorte == ELEMENT_LISTE) {
        n = noeud_creer(N_CHERCHER, 0, 0, 0);
        n->texte = grym_dupliquer(t.constData());
        if (!e.tri.isEmpty()) n->texte2 = grym_dupliquer(e.tri.toUtf8().constData());
        n->entier = e.decroissant && !e.tri.isEmpty();
        if (!e.colonnes.isEmpty()) n->texte3 = grym_dupliquer(colonnes_ecrites(e.texte, dossier, e.colonnes).toUtf8().constData());
    } else {
        *erreur = "Élément inconnu.";
    }
    return n;
}

}  // namespace

QString ecran_titre(const QString &dossier, const QString &ecran, const QString &titre, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    if (titre.contains(QChar(0x00BB)) || titre.contains(QChar(0x00AB))) return "Pas de guillemets « » dans un titre.";
    free(t.phrase->texte2);
    t.phrase->texte2 = titre.trimmed().isEmpty() ? nullptr : grym_dupliquer(titre.trimmed().toUtf8().constData());
    reimprimer(ch, t);
    geste->description = QString("Titre de l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

QString ecran_ajouter(const QString &dossier, const QString &ecran, const ElementNouveau &e, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    QString erreur;
    Noeud *n = nouvel_element(e, dossier, &erreur);
    if (!n) return erreur;
    noeud_ajouter(t.phrase, n);
    reimprimer(ch, t);
    if (e.sorte == ELEMENT_BOUTON) {   // un bouton sans événement serait refusé (§ 22.2)
        const int pos = apres_l_ecran(t, ecran);
        ch.remplacer(*t.f, pos, pos, QString("\nQuand on clique sur « %1 » dans l'écran %2 :\n    Remarque : à écrire.")
                                         .arg(e.texte.trimmed(), ecran));
    }
    geste->description = QString("Ajouter à l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

QString ecran_supprimer(const QString &dossier, const QString &ecran, int index, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    Noeud *n = t.phrase;
    if (index < 0 || (size_t)index >= n->nb_enfants) return "Élément introuvable.";
    Noeud *el = n->enfants[index];
    auto enlever = [&](size_t i) {
        noeud_liberer(n->enfants[i]);
        for (size_t q = i; q + 1 < n->nb_enfants; q++) n->enfants[q] = n->enfants[q + 1];
        n->nb_enfants--;
    };
    size_t reels = 0;
    for (size_t q = 0; q < n->nb_enfants; q++) reels += n->enfants[q]->type != N_DISPOSITION;
    if (el->type != N_DISPOSITION && reels <= 1) return "C'est le dernier élément : un écran montre au moins un élément.";
    if (el->type == N_DISPOSITION) {   // le bloc disparaît, avec sa fin ; ses éléments remontent d'un niveau
        int prof = 0;
        size_t autre = (size_t)index;
        if (el->forme) {
            for (size_t q = (size_t)index; q < n->nb_enfants; q++) {
                if (n->enfants[q]->type != N_DISPOSITION) continue;
                prof += n->enfants[q]->forme ? 1 : -1;
                if (prof == 0) { autre = q; break; }
            }
        } else {
            for (size_t q = (size_t)index + 1; q-- > 0;) {
                if (n->enfants[q]->type != N_DISPOSITION) continue;
                prof += n->enfants[q]->forme ? -1 : 1;
                if (prof == 0) { autre = q; break; }
            }
        }
        const size_t a = qMin((size_t)index, autre), b = qMax((size_t)index, autre);
        enlever(b);
        if (a != b) enlever(a);
    } else {
        const int forme = el->type == N_BOUTON ? 1 : el->type == N_CHERCHER ? 2 : el->type == N_NOM ? 3 : 0;
        const QString objet = QString::fromUtf8(el->texte);
        if (forme) {   // son événement part avec lui
            Trouve q = trouver_evenement(ch, ecran, forme, objet);
            if (q.f) retirer(ch, q);
        }
        enlever((size_t)index);
    }
    reimprimer(ch, t);
    geste->description = QString("Supprimer un élément de l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

QString ecran_modifier(const QString &dossier, const QString &ecran, int index, const ElementNouveau &e, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    Noeud *n = t.phrase;
    if (index < 0 || (size_t)index >= n->nb_enfants || n->enfants[index]->type == N_DISPOSITION) return "Élément introuvable.";
    Noeud *vieux = n->enfants[index];
    const QString ancien = QString::fromUtf8(vieux->texte);
    ElementNouveau x = e;
    if (vieux->type == N_CHERCHER) x.texte = ancien;   // l'entité d'une liste ne change pas ici
    QString erreur;
    Noeud *neuf = nouvel_element(x, dossier, &erreur);
    if (!neuf) return erreur;
    if (vieux->type == N_CHERCHER && vieux->nb_enfants) {   // la condition « dont » reste, telle qu'écrite
        for (size_t q = 0; q < vieux->nb_enfants; q++) noeud_ajouter(neuf, vieux->enfants[q]);
        vieux->nb_enfants = 0;
    }
    n->enfants[index] = neuf;
    noeud_liberer(vieux);
    reimprimer(ch, t);
    const QString nouveau_nom = x.texte.trimmed();
    if (nouveau_nom != ancien) {
        if (neuf->type == N_BOUTON) renommer_evenement(ch, ecran, 1, ancien, nouveau_nom);
        if (neuf->type == N_NOM) renommer_evenement(ch, ecran, 3, ancien, nouveau_nom);
    }
    geste->description = QString("Modifier un élément de l'écran %1").arg(ecran);
    return ch.conclure(geste);
}
