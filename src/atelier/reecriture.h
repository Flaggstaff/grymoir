// GrymoiR : l'atelier, réécriture chirurgicale (docs/atelier.md, § 3 ; A2-b) : chaque geste de l'éditeur de
// données change les seules phrases concernées, imprimées dans la forme canonique, et laisse le reste des
// fichiers octet pour octet. Après chaque geste, tout le projet est réanalysé : un fichier qui s'analysait
// et ne s'analyse plus fait annuler le geste, et l'erreur est rendue.
#ifndef GRYM_ATELIER_REECRITURE_H
#define GRYM_ATELIER_REECRITURE_H

#include <QMap>
#include <QPair>
#include <QPoint>
#include <QRect>
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
    QStringList valeurs;    // valeurs permises (grammaire, § 16.15) : un champ texte fermé ; vide : aucune
};

// Chaque fonction rend un message d'erreur, vide si le geste a réussi ; *geste reçoit de quoi l'annuler.
QString ajouter_entite(const QString &dossier, const QString &fichier, const QString &nom, bool feminin, Geste *geste);
QString renommer_entite(const QString &dossier, const QString &ancien, const QString &nouveau, Geste *geste);
QString supprimer_entite(const QString &dossier, const QString &nom, Geste *geste);
QString ajouter_champ(const QString &dossier, const QString &entite, const ChampVoulu &champ, Geste *geste);
QString modifier_champ(const QString &dossier, const QString &entite, const QString &ancien, const ChampVoulu &champ,
                       Geste *geste);
QString supprimer_champ(const QString &dossier, const QString &entite, const QString &nom, Geste *geste);
// Importer un tableau (docs/atelier.md, § 5 sexies) : `code` s'écrit dans `fichier`, avant la première phrase de
// premier niveau qui montre quelque chose (« Ouvrir », « Afficher », fiche, « Effacer l'écran », « Exporter »),
// sinon à la fin.
QString ajouter_importation(const QString &dossier, const QString &fichier, const QString &code, Geste *geste);

// A4-b : un écran pour une entité (liste, Nouveau, Supprimer, Fermer, et leurs événements), écrit dans `cible`
// (docs/atelier.md, § 5 bis) ; l'écran d'accueil le reçoit comme bouton, et naît s'il n'existe pas.
QString generer_ecran(const QString &dossier, const QString &cible, const QString &entite, Geste *geste);
// Un écran vide : « L'écran <nom> montre : un bouton « Fermer ». », avec son événement ; `libre` : le bouton dans un
// bloc en position libre (grammaire, § 22.9), pour placer au pixel dès la création.
QString nouvel_ecran(const QString &dossier, const QString &cible, const QString &nom, Geste *geste, bool libre = false);
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
    bool plusieurs_lignes = false, lecture_seule = false;   // zone (§ 22.6)
    QStringList valeurs;      // zone (texte) : valeurs permises (§ 22.7) ; groupe : rien
    QString actif_si, visible_si;   // bouton, zone (actif) ; bouton, texte, zone (visible) : la condition telle qu'écrite
    QString source;           // image calculée : la valeur, sans « de » (« le portrait du compositeur choisi de l'écran ») ;
                              // vide : l'image d'un fichier, `texte` (§ 22.8)
};
QString ecran_titre(const QString &dossier, const QString &ecran, const QString &titre, Geste *geste);
// Ajoute à la fin ; un bouton reçoit son événement (« Remarque : à écrire. »).
// `dans` : l'index d'un bloc (groupe, onglet, bloc libre…) où le mettre, à la fin ; un onglet qui contient un bloc libre
// le reçoit dans ce bloc, placé sous ce qui y est (§ 22.9, § 22.10). −1 : à la fin de l'écran, ou de son dernier bloc libre.
QString ecran_ajouter(const QString &dossier, const QString &ecran, const ElementNouveau &e, Geste *geste, int dans = -1);
// Range l'élément (ou le bloc) `source` à la fin du bloc `dans` : un autre onglet, un groupe… ; dans un bloc libre, il
// prend une place sous ce qui y est (§ 5 decies).
// `ou` : la place où il a été lâché dans le bloc libre visé (sinon, sous ce qui y est).
QString ecran_mettre_dans(const QString &dossier, const QString &ecran, int source, int dans, Geste *geste, QPoint ou = QPoint(-1, -1));
// Un bouton part avec son événement, une liste avec son « Quand on choisit », une zone avec son « Quand on change » ;
// un bloc disparaît, ses éléments restent.
QString ecran_supprimer(const QString &dossier, const QString &ecran, int index, Geste *geste);
// Entoure l'élément d'un groupe titré, « le groupe « Adresse » : » (grammaire, § 22.7).
QString ecran_grouper(const QString &dossier, const QString &ecran, int index, const QString &titre, Geste *geste);
// Un bouton renommé l'est aussi dans son événement ; une zone renommée, dans son « Quand on change ».
QString ecran_modifier(const QString &dossier, const QString &ecran, int index, const ElementNouveau &e, Geste *geste);

// A4-d : déplacer l'élément `source` près de l'élément `cible` : 0 au-dessus, 1 en dessous, 2 à gauche, 3 à droite.
// À gauche ou à droite dans une colonne, les deux passent `côte à côte` (bloc créé) ; au-dessus ou en dessous dans
// une rangée, `l'un sous l'autre`. Un bloc réduit à un seul élément disparaît.
enum CoteDepot { DEPOT_AVANT = 0, DEPOT_APRES = 1, DEPOT_GAUCHE = 2, DEPOT_DROITE = 3 };
QString ecran_deplacer(const QString &dossier, const QString &ecran, int source, int cible, int cote, Geste *geste);

// Position libre (grammaire, § 22.9 ; docs/atelier.md, § 5 octies). Placer un élément de bloc libre : gauche et haut
// en pixels ; largeur et hauteur : −1 garde ce qui est écrit, 0 retire (taille naturelle), sinon la fixe. Les éléments
// du bloc se réordonnent de haut en bas, puis de gauche à droite : l'ordre de lecture suit la place.
// *nouvel_index reçoit l'index de l'élément après ce tri.
QString ecran_placer(const QString &dossier, const QString &ecran, int index, int gauche, int haut, int largeur, int hauteur,
                     Geste *geste, int *nouvel_index = nullptr);
// Met tout l'écran dans un bloc en position libre, chaque élément à la place mesurée dans l'aperçu (`places`, un
// rectangle par élément, ignoré pour un bloc) ; les blocs disparaissent. Largeur écrite pour chaque élément, hauteur
// pour une liste : ce que l'on voit reste ce que l'on a.
QString ecran_liberer(const QString &dossier, const QString &ecran, const QVector<QRect> &places, Geste *geste);
// Ajoute « les onglets : » à la fin de l'écran (dans son dernier bloc libre s'il en a un, sous ce qui y est), avec deux
// onglets, « Premier » et « Second », un texte dans chacun (grammaire, § 22.10).
QString ecran_ajouter_onglets(const QString &dossier, const QString &ecran, Geste *geste);

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
