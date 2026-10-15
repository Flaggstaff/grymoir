// GrymoiR : l'atelier, réécriture chirurgicale.
#include "reecriture.h"
#include "editeur.h"
#include "projet.h"
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

static bool deplacer(const QString &a, const QString &b);

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
            if (!f->nfc()) return QString("« %1 » contient des lettres décomposées : ouvre-le et enregistre-le d'abord.")
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
// Les valeurs permises d'un champ (§ 16.15) : vide si elles s'écrivent, sinon ce qui les empêche.
QString valeurs_valides(const ChampVoulu &c) {
    if (c.valeurs.isEmpty()) return QString();
    if (c.type != "texte" || c.plusieurs) return "Des valeurs permises ne vont qu'à un champ (texte) simple.";
    if (c.valeurs.size() < 2) return "Au moins deux valeurs permises : « oui ; non ».";
    QStringList vues;
    for (const QString &v : c.valeurs) {
        if (v.isEmpty()) return "Une valeur permise n'est pas vide.";
        if (v.contains(QChar(0x00AB)) || v.contains(QChar(0x00BB)) || v.contains('"'))
            return QString("« %1 » : pas de guillemets dans une valeur permise.").arg(v);
        if (vues.contains(v)) return QString("« %1 » est déjà dans la liste.").arg(v);
        vues << v;
    }
    if (!c.depart.trimmed().isEmpty() && !c.valeurs.contains(c.depart.trimmed()))
        return QString("« %1 » n'est pas une valeur permise de « %2 » : la valeur de départ doit en être une.")
            .arg(c.depart.trimmed(), c.nom);
    return QString();
}

Noeud *nouveau_champ(const ChampVoulu &c, QString *erreur = nullptr) {
    if (const QString e = valeurs_valides(c); !e.isEmpty()) {
        if (erreur) *erreur = e;
        return nullptr;
    }
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
    if (!c.valeurs.isEmpty()) n->texte3 = grym_dupliquer(c.valeurs.join(QChar(0x1e)).toUtf8().constData());
    if (depart) noeud_ajouter(n, depart);
    return n;
}

QString nom_valide(const QString &nom) {
    if (nom.trimmed().isEmpty()) return "Donne un nom.";
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
    for (int k = geste.renommes.size() - 1; k >= 0; k--) {   // les noms d'abord : les contenus se rétablissent ensuite
        const auto &r = geste.renommes[k];
        if (!deplacer(r.second, r.first)) erreur = QString("« %1 » n'a pas pu reprendre son nom.").arg(r.first);
    }
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
    if (!f->ok) return QString("« %1 » contient une erreur : corrige-la d'abord.").arg(QFileInfo(fichier).fileName());
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
        return QString("« %1 » est le dernier champ de « %2 » : une entité garde au moins un champ. Supprime plutôt l'entité.")
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
    if (!f->ok) return QString("« %1 » contient une erreur : corrige-la d'abord.").arg(QFileInfo(cible).fileName());
    if (f->compacte) return QString("Les écrans générés s'écrivent en forme littéraire : choisis un fichier .grym.");
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

QString nouvel_ecran(const QString &dossier, const QString &cible, const QString &nom, Geste *geste, bool libre) {
    Chantier ch(dossier);
    FichierAnalyse *f = ch.fichier(cible);
    if (!f) return QString("« %1 » n'est pas dans le projet.").arg(cible);
    if (!f->ok) return QString("« %1 » contient une erreur : corrige-la d'abord.").arg(QFileInfo(cible).fileName());
    if (f->compacte) return QString("Les écrans générés s'écrivent en forme littéraire : choisis un fichier .grym.");
    const QString n = nom.simplified();
    if (n.isEmpty()) return "Donne un nom : « de recherche », « des factures », « d'accueil ».";
    QString t = (libre ? QString("L'écran %1 montre :\n    en position libre :\n        un bouton « Fermer », à 16 de la gauche et 16 du haut.\n")
                       : QString("L'écran %1 montre :\n    un bouton « Fermer ».\n")).arg(n) + evenement_fermer(n);
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

// Importer un tableau (§ 5 sexies) : le code, juste avant la première phrase de premier niveau qui montre quelque
// chose (« Ouvrir », « Afficher », une fiche, « Effacer l'écran », « Exporter »), sinon à la fin : les données de
// départ écrites plus haut existent déjà (un lien les trouve), et ce qui est montré voit les objets importés.
QString ajouter_importation(const QString &dossier, const QString &fichier, const QString &code, Geste *geste) {
    Chantier ch(dossier);
    FichierAnalyse *f = ch.fichier(fichier);
    if (!f) return QString("« %1 » n'est pas dans le projet.").arg(fichier);
    if (fichier.endsWith(".grymc")) return "L'importation s'écrit dans un fichier en forme littéraire (.grym).";
    if (!f->ok) return QString("« %1 » ne s'analyse pas : corrige-le d'abord.").arg(QFileInfo(fichier).fileName());
    if (code.trimmed().isEmpty()) return "Rien à importer.";
    QString bloc = code;
    while (bloc.endsWith('\n')) bloc.chop(1);
    for (size_t k = 0; k < f->p.nb; k++) {
        const int t = f->p.phrases[k]->type;
        if (t != P_OUVRIR && t != P_AFFICHAGE && t != P_FICHE && t != P_EFFACER && t != P_EXPORTER) continue;
        const int debut = f->etendue(k).first;
        ch.remplacer(*f, debut, debut, bloc + "\n\n");
        geste->description = "Importer un tableau";
        return ch.conclure(geste);
    }
    const QString t = f->texte;
    int fin = t.size();
    while (fin > 0 && t.at(fin - 1).isSpace()) fin--;
    ch.remplacer(*f, fin, t.size(), (fin ? "\n\n" : "") + bloc + "\n");
    geste->description = "Importer un tableau";
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

// « actif si », « visible si » (§ 22.6) : la condition reste le texte que le développeur a écrit. L'arbre porte une
// marque, que l'impression remplace par ce texte ; la réanalyse du projet (Chantier::conclure) le vérifie.
const char *const MARQUE_ACTIF = "\x01" "actif" "\x01", *const MARQUE_VISIBLE = "\x01" "visible" "\x01";   // « \x01ac » serait un seul échappement
const char *const MARQUE_SOURCE = "\x01" "source" "\x01";   // la valeur d'une image calculée (§ 22.8)

// « de » devant la valeur d'une image calculée, contracté comme le langage le veut (grammaire, § 5.2) : « du portrait »,
// « des », « d'une », « de la », « de l' ».
QString de_valeur(const QString &v) {
    for (const auto &[article, contracte] : {std::pair<const char *, const char *>{"le ", "du "}, {"les ", "des "},
                                             {"un ", "d'un "}, {"une ", "d'une "}})
        if (v.startsWith(QString::fromUtf8(article))) return QString::fromUtf8(contracte) + v.mid(QString::fromUtf8(article).size());
    return "de " + v;
}

QString avec_conditions(QString imprime, const ElementNouveau &e) {
    imprime.replace(QString("« %1 »").arg(QString::fromUtf8(MARQUE_ACTIF)), e.actif_si.simplified());
    imprime.replace(QString("« %1 »").arg(QString::fromUtf8(MARQUE_VISIBLE)), e.visible_si.simplified());
    imprime.replace(QString("de « %1 »").arg(QString::fromUtf8(MARQUE_SOURCE)), de_valeur(e.source.simplified()));
    return imprime;
}

void reimprimer_avec(Chantier &ch, const Trouve &t, const ElementNouveau &e) {
    const auto x = t.f->etendue(t.k);
    ch.remplacer(*t.f, x.first, x.second, avec_conditions(t.f->imprimer(t.phrase), e));
}

// Les états de l'élément : options d'une zone, conditions ; vide si tout va bien.
QString etats_voulus(Noeud *n, const ElementNouveau &e) {
    if (e.sorte == ELEMENT_ZONE) {
        if (e.plusieurs_lignes && e.type != "texte") return "Seule une zone (texte) s'écrit sur plusieurs lignes.";
        if (!e.valeurs.isEmpty()) {   // valeurs permises (§ 22.7) : mêmes règles qu'un champ d'entité
            ChampVoulu c;
            c.nom = e.texte;
            c.type = e.type;
            c.valeurs = e.valeurs;
            c.depart = e.depart.trimmed();
            if (c.depart.startsWith(QChar(0x00AB)) && c.depart.endsWith(QChar(0x00BB))) c.depart = c.depart.mid(1, c.depart.size() - 2).trimmed();
            const QString x = valeurs_valides(c);
            if (!x.isEmpty()) return x;
            if (e.plusieurs_lignes) return "Une zone à valeurs permises se choisit : pas sur plusieurs lignes.";
            n->texte3 = grym_dupliquer(e.valeurs.join(QChar(0x1e)).toUtf8().constData());
        }
        if (e.plusieurs_lignes) n->entier |= 2;
        if (e.lecture_seule) n->entier |= 4;
    }
    const QString actif = e.actif_si.simplified(), visible = e.visible_si.simplified();
    if (!actif.isEmpty() && e.sorte != ELEMENT_BOUTON && e.sorte != ELEMENT_ZONE)
        return "Seuls un bouton ou une zone sont actifs ou non.";
    if (!visible.isEmpty() && e.sorte == ELEMENT_LISTE) return "Une liste est toujours visible, pour l'instant.";
    for (int k = 0; k < 2; k++) {
        const QString &c = k ? visible : actif;
        if (c.isEmpty()) continue;
        if (c.endsWith('.') || c.endsWith(',')) return "La condition finit sans point ni virgule.";
        Noeud *si = noeud_creer(N_SI_ECRAN, 0, 0, 0);
        si->forme = k ? 2 : 1;
        Noeud *m = noeud_creer(N_TEXTE, 0, 0, 0);
        m->texte = grym_dupliquer(k ? MARQUE_VISIBLE : MARQUE_ACTIF);
        noeud_ajouter(si, m);
        noeud_ajouter(n, si);
    }
    return QString();
}

Noeud *nouvel_element(const ElementNouveau &e, const QString &dossier, QString *erreur) {
    Noeud *n = nullptr;
    const QByteArray t = e.texte.trimmed().toUtf8();
    if (e.sorte == ELEMENT_IMAGE && !e.source.trimmed().isEmpty()) {   // « l'image de … » (§ 22.8)
        const QString v = e.source.simplified();
        if (v.endsWith('.') || v.endsWith(',')) { *erreur = "La valeur finit sans point ni virgule."; return nullptr; }
        if (v.startsWith("de ") || v.startsWith("du ") || v.startsWith("d'")) { *erreur = "Écris la valeur sans « de » : « le portrait du compositeur choisi de l'écran »."; return nullptr; }
        n = noeud_creer(N_IMAGE_ECRAN, 0, 0, 0);
        n->forme = 1;
        Noeud *si = noeud_creer(N_SI_ECRAN, 0, 0, 0);
        si->forme = 3;
        Noeud *m = noeud_creer(N_TEXTE, 0, 0, 0);
        m->texte = grym_dupliquer(MARQUE_SOURCE);
        noeud_ajouter(si, m);
        noeud_ajouter(n, si);
        const QString x = etats_voulus(n, e);
        if (!x.isEmpty()) { *erreur = x; noeud_liberer(n); return nullptr; }
        return n;
    }
    if (e.texte.trimmed().isEmpty()) { *erreur = "Donne un libellé, un texte ou un nom."; return nullptr; }
    if (e.sorte == ELEMENT_BOUTON || e.sorte == ELEMENT_TEXTE || e.sorte == ELEMENT_IMAGE) {
        if (e.texte.contains(QChar(0x00BB)) || e.texte.contains(QChar(0x00AB))) { *erreur = "Pas de guillemets « » dans un libellé."; return nullptr; }
        n = noeud_creer(e.sorte == ELEMENT_BOUTON ? N_BOUTON : e.sorte == ELEMENT_IMAGE ? N_IMAGE_ECRAN : N_TEXTE_ECRAN, 0, 0, 0);
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
    } else if (e.sorte == ELEMENT_GRAPHIQUE) {   // « le graphique des tâches conservées, par état » (§ 22.11)
        if (e.cle.trimmed().isEmpty()) { *erreur = "Un graphique dit son regroupement : un champ de l'entité."; return nullptr; }
        n = noeud_creer(N_CHERCHER, 0, 0, 0);
        n->texte = grym_dupliquer(t.constData());
        n->graphique = 1 | (e.secteurs ? 2 : 0) | (e.somme.trimmed().isEmpty() ? 0 : 4);
        QString cols = e.cle.trimmed() + QChar(0x1d) + e.cle.trimmed();
        if (!e.somme.trimmed().isEmpty())   // écrit sans article : l'imprimeur le retrouve par le genre (« de la durée »)
            cols += QChar(0x1e) + e.somme.trimmed() + QChar(0x1d) + e.somme.trimmed();
        n->texte3 = grym_dupliquer(cols.toUtf8().constData());
    } else if (e.sorte == ELEMENT_LISTE) {
        n = noeud_creer(N_CHERCHER, 0, 0, 0);
        n->texte = grym_dupliquer(t.constData());
        if (!e.tri.isEmpty()) n->texte2 = grym_dupliquer(e.tri.toUtf8().constData());
        n->entier = e.decroissant && !e.tri.isEmpty();
        if (!e.colonnes.isEmpty()) n->texte3 = grym_dupliquer(colonnes_ecrites(e.texte, dossier, e.colonnes).toUtf8().constData());
    } else {
        *erreur = "Élément inconnu.";
    }
    if (n) {
        const QString x = etats_voulus(n, e);
        if (!x.isEmpty()) { *erreur = x; noeud_liberer(n); return nullptr; }
    }
    return n;
}

}  // namespace

namespace {
int bloc_de(Noeud *n, size_t i);
void inserer(Noeud *n, size_t i, Noeud *x);
size_t fin_d_unite(Noeud *n, size_t i);
size_t ouverture_de(Noeud *n, size_t i);
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

namespace {
size_t fin_d_unite(Noeud *n, size_t i);
int bloc_de(Noeud *n, size_t i);
void inserer(Noeud *n, size_t i, Noeud *x);

// Met x à la fin du bloc qui s'ouvre en ouv. Un onglet ou un groupe qui contient un bloc libre le donne à ce bloc ; dans
// un bloc libre, x prend une place sous l'élément le plus bas (§ 22.9).
void mettre_dans(Noeud *p, size_t ouv, Noeud *x, QPoint ou = QPoint(-1, -1)) {
    size_t fin = fin_d_unite(p, ouv);
    if (p->enfants[ouv]->forme != 4)
        for (size_t q = ouv + 1; q < fin; q = fin_d_unite(p, q) + 1)
            if (p->enfants[q]->type == N_DISPOSITION && p->enfants[q]->forme == 4) { ouv = q; fin = fin_d_unite(p, q); break; }
    if (p->enfants[ouv]->forme == 4) {
        int bas = -32;
        for (size_t q = ouv + 1; q < fin; q = fin_d_unite(p, q) + 1)
            if (p->enfants[q]->place_dite & 1) bas = qMax(bas, p->enfants[q]->place[1]);
        x->place[0] = ou.x() >= 0 ? qMin(4000, ou.x()) : 16;   // où il a été lâché, sinon sous ce qui y est
        x->place[1] = ou.y() >= 0 ? qMin(4000, ou.y()) : qMin(4000, bas + 48);
        x->place_dite = (x->place_dite & 6) | 1;   // sa taille dite le suit
    }
    inserer(p, fin, x);
}
}  // namespace

QString ecran_mettre_dans(const QString &dossier, const QString &ecran, int source, int dans, Geste *geste, QPoint ou) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    Noeud *p = t.phrase;
    if (source < 0 || dans < 0 || (size_t)source >= p->nb_enfants || (size_t)dans >= p->nb_enfants
        || (p->enfants[source]->type == N_DISPOSITION && (!p->enfants[source]->forme || p->enfants[source]->forme == 6))
        || p->enfants[dans]->type != N_DISPOSITION || !p->enfants[dans]->forme || p->enfants[dans]->forme == 5)
        return "Choisis un élément, et un onglet, un groupe ou un bloc où le mettre.";
    const size_t fs = fin_d_unite(p, (size_t)source);
    if ((size_t)dans >= (size_t)source && (size_t)dans <= fs) return "Un bloc ne se met pas dans lui-même.";
    if (ouverture_de(p, (size_t)source) == (size_t)dans
        || (p->enfants[dans]->forme == 6 && ouverture_de(p, ouverture_de(p, (size_t)source)) == (size_t)dans))
        return "Il y est déjà.";
    // Le seul élément d'un bloc qui le quitte : un bloc de rangement vidé (côte à côte, l'un sous l'autre, groupe, bloc
    // libre placé) disparaît avec lui ; une page d'onglet (l'onglet, ou le bloc libre qui la remplit) reste, et le geste
    // est refusé : un onglet vide ne se dit pas.
    size_t vide = p->nb_enfants;   // le bloc à retirer après le déplacement, s'il y en a un
    {
        const size_t anc = ouverture_de(p, (size_t)source);
        if (anc < p->nb_enfants) {
            int unites = 0;
            for (size_t q = anc + 1; q < fin_d_unite(p, anc); q = fin_d_unite(p, q) + 1) unites++;
            if (unites <= 1) {
                const size_t parent = ouverture_de(p, anc);
                const bool page = p->enfants[anc]->forme == 6
                                  || (parent < p->nb_enfants && p->enfants[parent]->forme == 6);
                if (page) return "C'est le seul élément de cet onglet, qui resterait vide : ajoutes-y d'abord autre chose.";
                if ((size_t)dans == anc) return "Il y est déjà.";
                vide = anc;
            }
        }
    }
    QVector<Noeud *> unite;
    for (size_t q = (size_t)source; q <= fs; q++) unite << p->enfants[q];
    const size_t nb = (size_t)unite.size();
    for (size_t q = (size_t)source; q + nb < p->nb_enfants; q++) p->enfants[q] = p->enfants[q + nb];
    p->nb_enfants -= nb;
    size_t d = (size_t)dans > fs ? (size_t)dans - nb : (size_t)dans;
    Noeud *bloc_vide = vide < p->nb_enfants ? p->enfants[vide] : nullptr;   // repéré par son nœud : les index bougent
    unite[0]->place_dite &= 6;   // sa place ne valait que dans l'ancien bloc ; le nouveau la redonne s'il est libre
    mettre_dans(p, d, unite[0], ou);
    if (!(unite[0]->place_dite & 1)) unite[0]->place_dite = 0;   // hors d'un bloc libre, ni place ni taille
    const size_t rang = (size_t)(std::find(p->enfants, p->enfants + p->nb_enfants, unite[0]) - p->enfants);
    for (size_t q = 1; q < nb; q++) inserer(p, rang + q, unite[(int)q]);
    if (bloc_vide) {   // le bloc de rangement vidé : son ouverture et sa fin, désormais côte à côte, s'en vont
        const size_t i = (size_t)(std::find(p->enfants, p->enfants + p->nb_enfants, bloc_vide) - p->enfants);
        if (i + 1 < p->nb_enfants && p->enfants[i + 1]->type == N_DISPOSITION && !p->enfants[i + 1]->forme) {
            noeud_liberer(p->enfants[i]);
            noeud_liberer(p->enfants[i + 1]);
            for (size_t q = i; q + 2 < p->nb_enfants; q++) p->enfants[q] = p->enfants[q + 2];
            p->nb_enfants -= 2;
        }
    }
    reimprimer(ch, t);
    geste->description = QString("Déplacer un élément de l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

QString ecran_ajouter(const QString &dossier, const QString &ecran, const ElementNouveau &e, Geste *geste, int dans) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    QString erreur;
    Noeud *n = nouvel_element(e, dossier, &erreur);
    if (!n) return erreur;
    Noeud *p = t.phrase;
    if (dans >= 0 && (size_t)dans < p->nb_enfants && p->enfants[dans]->type == N_DISPOSITION && p->enfants[dans]->forme
        && p->enfants[dans]->forme != 5) {   // dans le bloc choisi : un onglet ouvert, un groupe, un bloc libre…
        mettre_dans(p, (size_t)dans, n);
        reimprimer_avec(ch, t, e);
        geste->description = QString("Ajouter un élément à l'écran %1").arg(ecran);
        return ch.conclure(geste);
    }
    // Un écran qui a un bloc en position libre reçoit l'élément dans le dernier, sous ce qui y est déjà (§ 22.9).
    size_t fin_libre = 0;
    bool libre = false;
    for (size_t q = 0; q < p->nb_enfants; q++)
        if (p->enfants[q]->type == N_DISPOSITION && p->enfants[q]->forme == 0 && bloc_de(p, q) == 4) { fin_libre = q; libre = true; }
    if (libre) {
        int bas = 0;
        for (size_t q = 0; q < fin_libre; q++)
            if (bloc_de(p, q) == 4 && (p->enfants[q]->place_dite & 1)) bas = qMax(bas, p->enfants[q]->place[1]);
        n->place[0] = 16;
        n->place[1] = qMin(4000, bas + 48);
        n->place_dite = 1;
        inserer(p, fin_libre, n);
    } else
        noeud_ajouter(t.phrase, n);
    reimprimer_avec(ch, t, e);
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
    // Les onglets, ou un onglet : ils partent avec leur contenu (un onglet sans les onglets, ou du contenu hors d'un onglet,
    // n'aurait pas de sens) ; le dernier onglet emporte ses onglets (§ 22.10).
    if (el->type == N_DISPOSITION && (el->forme == 5 || el->forme == 6)) {
        size_t debut = (size_t)index;
        if (el->forme == 6) {
            const size_t parent = ouverture_de(n, debut);
            int freres = 0;
            if (parent < n->nb_enfants)
                for (size_t q = parent + 1; q < fin_d_unite(n, parent); q = fin_d_unite(n, q) + 1) freres++;
            if (freres <= 1 && parent < n->nb_enfants) debut = parent;
        }
        const size_t fin = fin_d_unite(n, debut);
        size_t restants = 0;
        for (size_t q = 0; q < n->nb_enfants; q++) restants += n->enfants[q]->type != N_DISPOSITION && (q < debut || q > fin);
        if (!restants) return "C'est tout l'écran : un écran montre au moins un élément.";
        for (size_t q = fin + 1; q-- > debut;) enlever(q);
        reimprimer(ch, t);
        geste->description = QString("Supprimer %1 de l'écran %2").arg(debut == (size_t)index && el->forme == 6 ? "un onglet" : "des onglets", ecran);
        return ch.conclure(geste);
    }
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
        // Ses unités directes remontent d'un niveau (§ 22.9) : dans un bloc libre, elles prennent la place du bloc,
        // l'une sous l'autre, si elles n'en avaient pas ; ailleurs, elles perdent leur place.
        const bool parent_libre = bloc_de(n, a) == 4;
        const int g0 = n->enfants[a]->place[0];
        int h0 = n->enfants[a]->place[1];
        for (size_t q = a + 1; q < b; q = fin_d_unite(n, q) + 1) {
            Noeud *x = n->enfants[q];
            if (!parent_libre) { x->place_dite = 0; continue; }
            if (n->enfants[a]->forme == 4 && (x->place_dite & 1)) {   // un bloc libre retiré : les places restent, décalées
                x->place[0] = qMin(4000, x->place[0] + g0);
                x->place[1] = qMin(4000, x->place[1] + h0);
                continue;
            }
            x->place[0] = g0;
            x->place[1] = qMin(4000, h0);
            x->place_dite = (x->place_dite & ~1) | 1;
            h0 += 48;
        }
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
    if (index >= 0 && (size_t)index < n->nb_enfants && n->enfants[index]->type == N_DISPOSITION
        && ((n->enfants[index]->forme == 3 && e.sorte == ELEMENT_GROUPE)
            || (n->enfants[index]->forme == 6 && e.sorte == ELEMENT_ONGLET))) {   // le titre d'un groupe, d'un onglet (§ 22.7, § 22.10)
        const QString titre = e.texte.trimmed();
        if (titre.isEmpty()) return "Un groupe porte un titre.";
        if (titre.contains(QChar(0x00BB)) || titre.contains(QChar(0x00AB))) return "Pas de guillemets « » dans un titre.";
        free(n->enfants[index]->texte);
        n->enfants[index]->texte = grym_dupliquer(titre.toUtf8().constData());
        reimprimer(ch, t);
        geste->description = QString("Titre d'un groupe de l'écran %1").arg(ecran);
        return ch.conclure(geste);
    }
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
    for (int q = 0; q < 4; q++) neuf->place[q] = vieux->place[q];   // la place reste (§ 22.9)
    neuf->place_dite = vieux->place_dite;
    n->enfants[index] = neuf;
    noeud_liberer(vieux);
    reimprimer_avec(ch, t, x);
    const QString nouveau_nom = x.texte.trimmed();
    if (nouveau_nom != ancien) {
        if (neuf->type == N_BOUTON) renommer_evenement(ch, ecran, 1, ancien, nouveau_nom);
        if (neuf->type == N_NOM) renommer_evenement(ch, ecran, 3, ancien, nouveau_nom);
    }
    geste->description = QString("Modifier un élément de l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

// ---------------------------------------------------------------------------------------------
// A4-d : déplacer un élément (docs/atelier.md, § 5 bis)
// ---------------------------------------------------------------------------------------------

namespace {

// La sorte du bloc qui contient directement l'élément i : 0 l'écran (une colonne), 1 côte à côte, 2 l'un sous l'autre.
int bloc_de(Noeud *n, size_t i) {
    QVector<int> pile;
    for (size_t q = 0; q < i && q < n->nb_enfants; q++) {
        const Noeud *x = n->enfants[q];
        if (x->type != N_DISPOSITION) continue;
        if (x->forme) pile << x->forme;
        else if (!pile.isEmpty()) pile.pop_back();
    }
    return pile.isEmpty() ? 0 : pile.last();
}

void inserer(Noeud *n, size_t i, Noeud *x) {
    noeud_ajouter(n, x);   // agrandit le tableau ; puis on décale
    for (size_t q = n->nb_enfants - 1; q > i; q--) n->enfants[q] = n->enfants[q - 1];
    n->enfants[i] = x;
}

Noeud *retirer_enfant(Noeud *n, size_t i) {
    Noeud *x = n->enfants[i];
    for (size_t q = i; q + 1 < n->nb_enfants; q++) n->enfants[q] = n->enfants[q + 1];
    n->nb_enfants--;
    return x;
}

Noeud *marque(int forme, const QString &titre = QString()) {
    Noeud *m = noeud_creer(N_DISPOSITION, 0, 0, 0);
    m->forme = forme;
    if (forme == 3 || forme == 6) m->texte = grym_dupliquer(titre.toUtf8().constData());
    return m;
}

// Un bloc qui ne contient plus qu'un élément (ou aucun) ne sert à rien : il disparaît, son élément reste.
void nettoyer_blocs(Noeud *n) {
    for (bool change = true; change;) {
        change = false;
        for (size_t i = 0; i < n->nb_enfants && !change; i++) {
            if (n->enfants[i]->type != N_DISPOSITION || !n->enfants[i]->forme) continue;
            int prof = 0, directs = 0;
            size_t fin = i;
            for (size_t q = i + 1; q < n->nb_enfants; q++) {
                const Noeud *x = n->enfants[q];
                if (x->type == N_DISPOSITION && !x->forme) {
                    if (prof == 0) { fin = q; break; }
                    prof--;
                    continue;
                }
                if (prof == 0) directs++;
                if (x->type == N_DISPOSITION) prof++;
            }
            // un groupe titré (§ 22.7) a un sens avec un seul élément ; il ne part que vide
            // un bloc en position libre aussi : ses éléments ont une place qui ne vaut que là (§ 22.9)
            if (fin > i && (n->enfants[i]->forme >= 3 ? directs == 0 : directs <= 1)) {
                noeud_liberer(retirer_enfant(n, fin));
                noeud_liberer(retirer_enfant(n, i));
                change = true;
            }
        }
    }
}

}  // namespace

QString ecran_deplacer(const QString &dossier, const QString &ecran, int source, int cible, int cote, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    Noeud *n = t.phrase;
    const int nb = (int)n->nb_enfants;
    if (source < 0 || source >= nb || cible < 0 || cible >= nb || source == cible) return "Rien à déplacer.";
    if (n->enfants[source]->type == N_DISPOSITION || n->enfants[cible]->type == N_DISPOSITION)
        return "Un bloc ne se déplace pas : déplace ses éléments.";
    if (bloc_de(n, (size_t)source) == 4 || bloc_de(n, (size_t)cible) == 4 || bloc_de(n, (size_t)cible + 1) == 4)
        return "Dans un bloc en position libre, chaque élément se place au pixel : glisse-le dans son bloc.";
    Noeud *x = retirer_enfant(n, (size_t)source);
    size_t c = (size_t)(cible > source ? cible - 1 : cible);   // la cible, après le retrait de la source
    const int bloc = bloc_de(n, c);
    const bool horizontal = cote == DEPOT_GAUCHE || cote == DEPOT_DROITE;
    const bool avant = cote == DEPOT_AVANT || cote == DEPOT_GAUCHE;
    if ((horizontal && bloc == 1) || (!horizontal && bloc != 1)) {
        // dans le sens du bloc qui contient la cible : une simple place avant ou après elle
        inserer(n, avant ? c : c + 1, x);
    } else {
        // à travers : la cible et l'élément déplacé forment un bloc, côte à côte ou l'un sous l'autre
        const int forme = horizontal ? 1 : 2;
        inserer(n, c + 1, marque(0));
        inserer(n, avant ? c : c + 1, x);
        inserer(n, c, marque(forme));
    }
    nettoyer_blocs(n);
    reimprimer(ch, t);
    geste->description = QString("Déplacer un élément de l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

// Mettre un élément dans un groupe titré (§ 22.7) : « le groupe « Adresse » : » l'entoure, à sa place.
QString ecran_grouper(const QString &dossier, const QString &ecran, int index, const QString &titre, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    Noeud *n = t.phrase;
    const QString x = titre.trimmed();
    if (x.isEmpty()) return "Un groupe porte un titre.";
    if (x.contains(QChar(0x00BB)) || x.contains(QChar(0x00AB))) return "Pas de guillemets « » dans un titre.";
    if (index < 0 || (size_t)index >= n->nb_enfants || n->enfants[index]->type == N_DISPOSITION)
        return "Choisis un élément, pas un bloc.";
    inserer(n, (size_t)index + 1, marque(0));
    inserer(n, (size_t)index, marque(3, x));
    if (bloc_de(n, (size_t)index) == 4) {   // dans un bloc libre : le groupe prend la place et la taille de l'élément (§ 22.9)
        Noeud *gr = n->enfants[index], *el = n->enfants[index + 1];
        for (int q = 0; q < 4; q++) gr->place[q] = el->place[q];
        gr->place_dite = el->place_dite;
        el->place_dite = 0;
    }
    reimprimer(ch, t);
    geste->description = QString("Grouper un élément de l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

// ---------------------------------------------------------------------------------------------------------------
// Position libre (grammaire, § 22.9 ; docs/atelier.md, § 5 octies)
// ---------------------------------------------------------------------------------------------------------------

namespace {

// La fin du bloc qui s'ouvre en i (son N_DISPOSITION de forme 0), ou i pour un élément.
size_t fin_d_unite(Noeud *n, size_t i) {
    if (n->enfants[i]->type != N_DISPOSITION || !n->enfants[i]->forme) return i;
    int prof = 0;
    for (size_t q = i; q < n->nb_enfants; q++) {
        if (n->enfants[q]->type != N_DISPOSITION) continue;
        prof += n->enfants[q]->forme ? 1 : -1;
        if (prof == 0) return q;
    }
    return n->nb_enfants - 1;
}

// L'ouverture du bloc qui contient directement i, ou n->nb_enfants s'il est au niveau de l'écran.
size_t ouverture_de(Noeud *n, size_t i) {
    QVector<size_t> pile;
    for (size_t q = 0; q < i && q < n->nb_enfants; q++) {
        const Noeud *x = n->enfants[q];
        if (x->type != N_DISPOSITION) continue;
        if (x->forme) pile << q;
        else if (!pile.isEmpty()) pile.pop_back();
    }
    return pile.isEmpty() ? n->nb_enfants : pile.last();
}

// Les unités du bloc libre qui contient l'unité i (un élément, ou un bloc avec tout son contenu), de haut en bas puis de
// gauche à droite ; rend le nouvel index de l'unité qui était en i.
size_t trier_bloc_libre(Noeud *n, size_t i) {
    const size_t ouv = ouverture_de(n, i);
    if (ouv >= n->nb_enfants) return i;
    const size_t fin = fin_d_unite(n, ouv);
    QVector<QVector<Noeud *>> unites;
    Noeud *suivi = n->enfants[i];
    for (size_t q = ouv + 1; q < fin;) {
        const size_t f = fin_d_unite(n, q);
        QVector<Noeud *> u;
        for (size_t x = q; x <= f; x++) u << n->enfants[x];
        unites << u;
        q = f + 1;
    }
    std::stable_sort(unites.begin(), unites.end(), [](const QVector<Noeud *> &a, const QVector<Noeud *> &b) {
        return a[0]->place[1] != b[0]->place[1] ? a[0]->place[1] < b[0]->place[1] : a[0]->place[0] < b[0]->place[0];
    });
    size_t q = ouv + 1, r = i;
    for (const auto &u : unites)
        for (Noeud *x : u) { if (x == suivi) r = q; n->enfants[q++] = x; }
    return r;
}

}  // namespace

QString ecran_placer(const QString &dossier, const QString &ecran, int index, int gauche, int haut, int largeur, int hauteur,
                     Geste *geste, int *nouvel_index) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    Noeud *n = t.phrase;
    if (index < 0 || (size_t)index >= n->nb_enfants || (n->enfants[index]->type == N_DISPOSITION && !n->enfants[index]->forme)
        || bloc_de(n, (size_t)index) != 4)
        return "Choisis un élément ou un bloc d'un bloc en position libre.";
    if (gauche < 0 || haut < 0 || gauche > 4000 || haut > 4000) return "La place va de 0 à 4000 pixels.";
    if ((largeur > 0 && (largeur < 8 || largeur > 4000)) || (hauteur > 0 && (hauteur < 8 || hauteur > 4000)))
        return "La taille va de 8 à 4000 pixels.";
    Noeud *el = n->enfants[index];
    el->place[0] = gauche;
    el->place[1] = haut;
    el->place_dite |= 1;
    if (largeur == 0) el->place_dite &= ~2;
    else if (largeur > 0) { el->place[2] = largeur; el->place_dite |= 2; }
    if (hauteur == 0) el->place_dite &= ~4;
    else if (hauteur > 0) { el->place[3] = hauteur; el->place_dite |= 4; }
    const size_t apres = trier_bloc_libre(n, (size_t)index);
    if (nouvel_index) *nouvel_index = (int)apres;
    reimprimer(ch, t);
    geste->description = QString("Placer un élément de l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

QString ecran_liberer(const QString &dossier, const QString &ecran, const QVector<QRect> &places, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    Noeud *n = t.phrase;
    if ((size_t)places.size() != n->nb_enfants) return "L'aperçu ne correspond plus à l'écran : réessaie.";
    for (size_t q = 0; q < n->nb_enfants; q = fin_d_unite(n, q) + 1)
        if (n->enfants[q]->type == N_DISPOSITION && n->enfants[q]->forme == 4) return "Cet écran est déjà en position libre.";
    // Chaque unité du premier niveau (un élément, ou un bloc avec tout son contenu, qui garde sa règle) prend la place
    // mesurée dans l'aperçu (§ 22.9) : largeur toujours, hauteur pour une liste et pour un bloc.
    for (size_t q = 0; q < n->nb_enfants; q = fin_d_unite(n, q) + 1) {
        Noeud *x = n->enfants[q];
        const QRect r = places[(int)q];
        x->place[0] = qBound(0, r.x(), 4000);
        x->place[1] = qBound(0, r.y(), 4000);
        x->place[2] = qBound(8, r.width(), 4000);
        x->place[3] = qBound(8, r.height(), 4000);
        x->place_dite = 1 | 2 | (x->type == N_CHERCHER || x->type == N_DISPOSITION ? 4 : 0);
    }
    inserer(n, n->nb_enfants, marque(0));
    inserer(n, 0, marque(4));
    if (n->nb_enfants > 2) trier_bloc_libre(n, 1);
    reimprimer(ch, t);
    geste->description = QString("Mettre l'écran %1 en position libre").arg(ecran);
    return ch.conclure(geste);
}

QString ecran_ajouter_onglets(const QString &dossier, const QString &ecran, Geste *geste) {
    Chantier ch(dossier);
    Trouve t = trouver_ecran(ch, ecran);
    if (!t.f) return QString("L'écran %1 est introuvable.").arg(ecran);
    Noeud *p = t.phrase;
    auto texte = [](const char *x) {
        Noeud *n = noeud_creer(N_TEXTE_ECRAN, 0, 0, 0);
        n->texte = grym_dupliquer(x);
        return n;
    };
    // chaque onglet reçoit un bloc libre : ce qu'on y pose se place et s'agrandit au pixel, comme ailleurs (§ 5 decies)
    auto place = [](Noeud *n) { n->place[0] = n->place[1] = 16; n->place_dite = 1; return n; };
    QVector<Noeud *> bloc = {marque(5), marque(6, "Premier"), marque(4), place(texte("Premier onglet")), marque(0), marque(0),
                             marque(6, "Second"), marque(4), place(texte("Second onglet")), marque(0), marque(0), marque(0)};
    size_t ou = p->nb_enfants;   // à la fin, ou dans le dernier bloc libre, sous ce qui y est (§ 22.9)
    for (size_t q = 0; q < p->nb_enfants; q++)
        if (p->enfants[q]->type == N_DISPOSITION && p->enfants[q]->forme == 0 && bloc_de(p, q) == 4) ou = q;
    if (ou < p->nb_enfants) {
        int bas = 0;
        for (size_t q = 0; q < ou; q++)
            if (bloc_de(p, q) == 4 && (p->enfants[q]->place_dite & 1)) bas = qMax(bas, p->enfants[q]->place[1]);
        bloc[0]->place[0] = 16;
        bloc[0]->place[1] = qMin(4000, bas + 48);
        bloc[0]->place[2] = 480;   // de la place pour y poser des éléments (§ 5 decies)
        bloc[0]->place[3] = 320;
        bloc[0]->place_dite = 1 | 2 | 4;
    }
    for (int q = 0; q < bloc.size(); q++) inserer(p, ou + (size_t)q, bloc[q]);
    reimprimer(ch, t);
    geste->description = QString("Ajouter des onglets à l'écran %1").arg(ecran);
    return ch.conclure(geste);
}

// ---------------------------------------------------------------------------------------------------------------
// A5 : renommer et supprimer un fichier du projet (docs/atelier.md, § 5 quater)
// ---------------------------------------------------------------------------------------------------------------

// `Utiliser « … ».` et `_utiliser « … »`, en tête de ligne : la phrase ne vit qu'en tête du fichier (§ 21.1), et une
// remarque commence par « Remarque ». Un fichier qui ne s'analyse plus garde ses phrases `Utiliser` lisibles ainsi.
static const QRegularExpression &forme_utiliser() {
    static const QRegularExpression r("^([ \\t]*(?:Utiliser|_utiliser)[ \\t]+«\\s*)([^»\\n]*?)(\\s*»)",
                                      QRegularExpression::MultilineOption | QRegularExpression::UseUnicodePropertiesOption);
    return r;
}

// Le fichier sans son extension, en chemin absolu nettoyé : ce que désigne `Utiliser « … »` (§ 21.3).
static QString souche(const QString &chemin) {
    const QFileInfo i(chemin);
    return QDir::cleanPath(i.absolutePath() + "/" + i.completeBaseName());
}

static QStringList fichiers_du_projet(const QString &dossier) {
    QStringList r;
    QDirIterator it(dossier, {"*.grym", "*.grymc"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) r << QFileInfo(it.next()).absoluteFilePath();
    r.sort();
    return r;
}

static QString lire_texte(const QString &chemin, bool *ok = nullptr) {
    QFile f(chemin);
    const bool lu = f.open(QIODevice::ReadOnly);
    if (ok) *ok = lu;
    return lu ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList utilisateurs_de(const QString &dossier, const QString &fichier) {
    const QString cible = souche(fichier);
    QStringList r;
    for (const QString &c : fichiers_du_projet(dossier)) {
        if (c == QFileInfo(fichier).absoluteFilePath()) continue;
        auto it = forme_utiliser().globalMatch(lire_texte(c));
        while (it.hasNext()) {
            const auto m = it.next();
            if (QDir::cleanPath(QFileInfo(c).absolutePath() + "/" + m.captured(2).trimmed()) == cible) { r << c; break; }
        }
    }
    return r;
}

// Renomme a en b ; un nom qui ne diffère que par la casse passe par un nom provisoire (macOS, Windows).
static bool deplacer(const QString &a, const QString &b) {
    if (QFileInfo(a).absoluteFilePath().compare(QFileInfo(b).absoluteFilePath(), Qt::CaseInsensitive) == 0 && a != b) {
        const QString t = a + ".renommage";
        return QFile::rename(a, t) && QFile::rename(t, b);
    }
    return QFile::rename(a, b);
}

static bool meme_fichier(const QString &a, const QString &b) {
    const QString ca = QFileInfo(a).canonicalFilePath(), cb = QFileInfo(b).canonicalFilePath();
    return !ca.isEmpty() && ca == cb;
}

QString renommer_fichier(const QString &dossier, const QString &fichier, const QString &nouveau, Geste *geste,
                         QString *nouveau_chemin) {
    const QFileInfo ancien(fichier);
    if (!ancien.isFile()) return QString("« %1 » introuvable.").arg(fichier);
    const QString ext = "." + ancien.suffix();   // « .grym » ou « .grymc » : la forme du fichier
    if (ext != ".grym" && ext != ".grymc") return "Seul un fichier .grym ou .grymc se renomme ici.";
    QString n = nouveau.trimmed();
    if (n.endsWith(".grym", Qt::CaseInsensitive) || n.endsWith(".grymc", Qt::CaseInsensitive)) {
        const QString e = n.mid(n.lastIndexOf('.'));
        if (e.compare(ext, Qt::CaseInsensitive) != 0)
            return QString("L'extension ne change pas : elle dit la forme du fichier. Pour passer à l'autre forme : grym traduire.");
        n.chop(e.size());
    }
    if (n.isEmpty()) return "Donne un nom au fichier.";
    if (n.contains('/') || n.contains('\\')) return "Un nom de fichier, sans dossier : « données ».";
    if (n.startsWith('.')) return "Un nom qui commence par un point serait caché.";
    if (n.contains(QChar(0x00AB)) || n.contains(QChar(0x00BB)) || n.contains('"'))
        return "Pas de guillemets dans un nom de fichier : « Utiliser » ne saurait plus l'écrire.";
    const QDir ici = ancien.absoluteDir();
    const QString arrivee = ici.filePath(n + ext);
    if (arrivee == ancien.absoluteFilePath()) return "Rien à changer.";
    // ni l'une ni l'autre forme ne doit exister sous le nouveau nom : « données » désignerait les deux (§ 21.1)
    for (const QString &e : {QString(".grym"), QString(".grymc")}) {
        const QString autre = ici.filePath(n + e);
        if (QFileInfo::exists(autre) && !meme_fichier(autre, fichier))
            return QString("« %1 » existe déjà : il n'est jamais écrasé.").arg(n + e);
    }
    // la base et ce qui l'accompagne : un programme les emporte (grammaire, § 16.5)
    const QStringList compagnons = {".grymd", ".grymd-journal", ".grymd-wal", ".grymd-shm", ".grymb"};
    QVector<QPair<QString, QString>> a_deplacer = {{ancien.absoluteFilePath(), arrivee}};
    for (const QString &e : compagnons) {
        const QString de = ici.filePath(ancien.completeBaseName() + e), vers = ici.filePath(n + e);
        if (!QFileInfo::exists(de)) continue;
        if (QFileInfo::exists(vers) && !meme_fichier(vers, de))
            return QString("« %1 » existe déjà : la base de « %2 » ne peut pas prendre ce nom, et une base n'est jamais écrasée.")
                .arg(n + e, ancien.fileName());
        a_deplacer.push_back({de, vers});
    }

    // les phrases Utiliser qui désignent le fichier, réécrites en mémoire d'abord
    const QString cible = souche(fichier);
    QMap<QString, QString> textes;
    QMap<QString, bool> sains;
    for (const QString &c : fichiers_du_projet(dossier)) {
        bool ok = false;
        const QString t = lire_texte(c, &ok);
        if (!ok) continue;
        sains.insert(c, analyser_source(t, c.endsWith(".grymc"), c).message.isEmpty());
        QString r = t;
        auto it = forme_utiliser().globalMatch(t);
        QVector<QRegularExpressionMatch> trouves;
        while (it.hasNext()) trouves << it.next();
        for (int k = trouves.size() - 1; k >= 0; k--) {   // de la fin vers le début : les positions restent justes
            const auto &m = trouves[k];
            const QString ecrit = m.captured(2).trimmed();
            if (QDir::cleanPath(QFileInfo(c).absolutePath() + "/" + ecrit) != cible) continue;
            const int barre = ecrit.lastIndexOf('/');
            r.replace(m.capturedStart(2), m.capturedLength(2), (barre < 0 ? QString() : ecrit.left(barre + 1)) + n);
        }
        if (r != t) textes.insert(c, r);
    }
    // le programme principal, dans projet.grymatelier
    FichierProjet fp;
    fp.lire(dossier);
    const bool principal = !fp.programme_principal.isEmpty()
        && QDir(dossier).absoluteFilePath(fp.programme_principal) == ancien.absoluteFilePath();

    geste->avant.clear();
    geste->renommes.clear();
    geste->description = QString("Renommer « %1 » en « %2 »").arg(ancien.fileName(), n + ext);
    for (const auto &d : a_deplacer) {
        if (!deplacer(d.first, d.second)) {
            annuler_geste(*geste);
            return QString("« %1 » n'a pas pu être renommé.").arg(QFileInfo(d.first).fileName());
        }
        geste->renommes.push_back(d);
    }
    for (auto i = textes.constBegin(); i != textes.constEnd(); ++i) {
        const QString c = i.key() == ancien.absoluteFilePath() ? arrivee : i.key();
        geste->avant.insert(c, lire_texte(c).toUtf8());
        QSaveFile s(c);
        if (!s.open(QIODevice::WriteOnly) || s.write(i.value().toUtf8()) < 0 || !s.commit()) {
            annuler_geste(*geste);
            return QString("« %1 » n'a pas pu être écrit.").arg(QDir(dossier).relativeFilePath(c));
        }
    }
    if (principal) {
        QFile f(FichierProjet::chemin(dossier));
        geste->avant.insert(FichierProjet::chemin(dossier), f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray());
        fp.programme_principal = QDir(dossier).relativeFilePath(arrivee);
        if (!fp.ecrire()) {
            annuler_geste(*geste);
            return "projet.grymatelier n'a pas pu être écrit.";
        }
    }
    // Le projet après le geste : un fichier qui s'analysait doit s'analyser encore.
    for (auto i = sains.constBegin(); i != sains.constEnd(); ++i) {
        if (!i.value()) continue;
        const QString c = i.key() == ancien.absoluteFilePath() ? arrivee : i.key();
        const Diagnostic_atelier d = analyser_source(lire_texte(c), c.endsWith(".grymc"), c);
        if (d.message.isEmpty()) continue;
        annuler_geste(*geste);
        return QString("Renommage annulé : il casserait « %1 », ligne %2 : %3")
            .arg(QDir(dossier).relativeFilePath(c)).arg(d.ligne).arg(d.message);
    }
    if (nouveau_chemin) *nouveau_chemin = arrivee;
    return QString();
}

QString supprimer_fichier(const QString &dossier, const QString &fichier, QString *avis) {
    const QFileInfo i(fichier);
    if (!i.isFile()) return QString("« %1 » introuvable.").arg(fichier);
    const QStringList par = utilisateurs_de(dossier, fichier);
    if (!par.isEmpty()) {
        QStringList noms;
        for (const QString &c : par) noms << "« " + QDir(dossier).relativeFilePath(c) + " »";
        return QString("« %1 » est utilisé par %2 : retire d'abord la phrase « Utiliser ».")
            .arg(i.fileName(), noms.join(", "));
    }
    QString chemin = i.absoluteFilePath();
    if (!QFile::moveToTrash(chemin)) return QString("« %1 » n'a pas pu être mis à la corbeille.").arg(i.fileName());
    FichierProjet fp;
    fp.lire(dossier);
    if (!fp.programme_principal.isEmpty() && QDir(dossier).absoluteFilePath(fp.programme_principal) == i.absoluteFilePath()) {
        fp.programme_principal.clear();
        fp.ecrire();
    }
    const QString base = i.absoluteDir().filePath(i.completeBaseName() + ".grymd");
    if (avis) *avis = QFileInfo::exists(base)
        ? QString("« %1 » est à la corbeille. Sa base, « %2 », reste dans le dossier : les données ne se jettent pas avec le code.")
              .arg(i.fileName(), QFileInfo(base).fileName())
        : QString("« %1 » est à la corbeille.").arg(i.fileName());
    return QString();
}
