// GrymoiR : l'atelier, réécriture chirurgicale (docs/atelier.md, § 3 ; A2-b) : chaque geste de l'éditeur de
// données change les seules phrases concernées, imprimées dans la forme canonique, et laisse le reste des
// fichiers octet pour octet. Après chaque geste, tout le projet est réanalysé : un fichier qui s'analysait
// et ne s'analyse plus fait annuler le geste, et l'erreur est rendue.
#ifndef GRYM_ATELIER_REECRITURE_H
#define GRYM_ATELIER_REECRITURE_H

#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

// Ce qu'un geste a changé : le contenu d'avant de chaque fichier touché (vide : le fichier n'existait pas).
struct Geste {
    QMap<QString, QByteArray> avant;
    QString description;   // « Ajouter le champ « prix » à « œuvre » »
    QVector<QPair<QString, QString>> renommes;   // A5 : (ancien chemin, nouveau), dans l'ordre où ils ont eu lieu
};

struct ChampVoulu {
    QString nom, type;
    bool feminin = false, unique = false, facultatif = false, plusieurs = false;
    bool cascade = false;   // « , et disparaît avec lui » : pour un lien simple (§ 16.12)
    QString depart;         // valeur de départ, telle qu'on l'écrit (« Suisse », « 12,50 », « 01.01.2026 ») ; vide : aucune
};

// Chaque fonction rend un message d'erreur, vide si le geste a réussi ; *geste reçoit de quoi l'annuler.
QString ajouter_entite(const QString &dossier, const QString &fichier, const QString &nom, bool feminin, Geste *geste);
QString renommer_entite(const QString &dossier, const QString &ancien, const QString &nouveau, Geste *geste);
QString supprimer_entite(const QString &dossier, const QString &nom, Geste *geste);
QString ajouter_champ(const QString &dossier, const QString &entite, const ChampVoulu &champ, Geste *geste);
QString modifier_champ(const QString &dossier, const QString &entite, const QString &ancien, const ChampVoulu &champ,
                       Geste *geste);
QString supprimer_champ(const QString &dossier, const QString &entite, const QString &nom, Geste *geste);

// A4-b : un écran pour une entité (liste, Nouveau, Supprimer, Fermer, et leurs événements), écrit dans `cible`
// (docs/atelier.md, § 5 bis) ; l'écran d'accueil le reçoit comme bouton, et naît s'il n'existe pas.
QString generer_ecran(const QString &dossier, const QString &cible, const QString &entite, Geste *geste);
// Un écran vide : « L'écran <nom> montre : un bouton « Fermer ». », avec son événement.
QString nouvel_ecran(const QString &dossier, const QString &cible, const QString &nom, Geste *geste);
// Ajoute une phrase à la fin d'un fichier (« Ouvrir l'écran d'accueil. » dans le programme principal).
QString ajouter_phrase_finale(const QString &dossier, const QString &fichier, const QString &phrase, Geste *geste);

// A4-c : modifier un écran. L'index est celui de l'élément dans la déclaration (EcranLu::elements).
struct ElementNouveau {
    int sorte = 0;            // SorteElement (src/interface.h) : bouton, texte, zone, liste
    QString texte;            // libellé, texte fixe, nom de la zone, entité de la liste
    QString type;             // zone
    bool feminin = false, facultatif = false;
    QString depart;           // zone : valeur de départ, telle qu'on l'écrit ; vide : aucune
    QString tri;              // liste : champ du tri ; vide : ordre de conservation
    bool decroissant = false;
    QStringList colonnes;     // liste : champs à montrer ; vide : les colonnes par défaut
};
QString ecran_titre(const QString &dossier, const QString &ecran, const QString &titre, Geste *geste);
// Ajoute à la fin ; un bouton reçoit son événement (« Remarque : à écrire. »).
QString ecran_ajouter(const QString &dossier, const QString &ecran, const ElementNouveau &e, Geste *geste);
// Un bouton part avec son événement, une liste avec son « Quand on choisit », une zone avec son « Quand on change » ;
// un bloc disparaît, ses éléments restent.
QString ecran_supprimer(const QString &dossier, const QString &ecran, int index, Geste *geste);
// Un bouton renommé l'est aussi dans son événement ; une zone renommée, dans son « Quand on change ».
QString ecran_modifier(const QString &dossier, const QString &ecran, int index, const ElementNouveau &e, Geste *geste);

// A4-d : déplacer l'élément `source` près de l'élément `cible` : 0 au-dessus, 1 en dessous, 2 à gauche, 3 à droite.
// À gauche ou à droite dans une colonne, les deux passent `côte à côte` (bloc créé) ; au-dessus ou en dessous dans
// une rangée, `l'un sous l'autre`. Un bloc réduit à un seul élément disparaît.
enum CoteDepot { DEPOT_AVANT = 0, DEPOT_APRES = 1, DEPOT_GAUCHE = 2, DEPOT_DROITE = 3 };
QString ecran_deplacer(const QString &dossier, const QString &ecran, int source, int cible, int cote, Geste *geste);

// Remet chaque fichier touché dans son état d'avant ; message d'erreur, vide si tout est revenu.
// Un fichier renommé reprend d'abord son ancien nom.
QString annuler_geste(const Geste &geste);

// A5 (docs/atelier.md, § 5 quater) : les fichiers du projet.
// Les fichiers du projet dont une phrase `Utiliser` désigne `fichier` (grammaire, § 21.3), en chemins absolus.
QStringList utilisateurs_de(const QString &dossier, const QString &fichier);
// Renomme `fichier` en `nouveau` (un nom, sans dossier ; l'extension ne change pas). Chaque `Utiliser` qui le
// désigne suit ; un programme emporte sa base (`.grymd`, ses journaux) et son bytecode ; le programme principal
// de `projet.grymatelier` suit. Le projet est réanalysé : un fichier qui s'analysait doit s'analyser encore,
// sinon tout est défait. *nouveau_chemin reçoit le chemin d'arrivée.
QString renommer_fichier(const QString &dossier, const QString &fichier, const QString &nouveau, Geste *geste,
                         QString *nouveau_chemin);
// Met `fichier` à la corbeille du système. Refusé si un autre fichier l'utilise. La base d'un programme reste
// dans le dossier ; le programme principal de `projet.grymatelier` s'efface s'il le désignait. *avis : ce qu'il
// faut savoir après coup (la base gardée).
QString supprimer_fichier(const QString &dossier, const QString &fichier, QString *avis);

#endif
