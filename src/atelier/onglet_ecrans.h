// GrymoiR : l'atelier, l'onglet « Écrans » (docs/atelier.md, § 5 bis, A4-a) : les écrans du projet, leur
// aperçu (la structure, pas les données), et les propriétés de l'élément choisi. En lecture seule.
#ifndef GRYM_ATELIER_ONGLET_ECRANS_H
#define GRYM_ATELIER_ONGLET_ECRANS_H

#include "reecriture.h"
#include "vue_ecran.h"

#include <functional>

#include <QHash>
#include <QWidget>

// Un élément d'écran, tel que l'arbre le décrit.
struct ElementLu {
    int sorte = 0;           // SorteElement (src/interface.h)
    QString texte;           // libellé, texte fixe, nom de la zone, entité de la liste
    QStringList colonnes;    // liste : titres des colonnes, par défaut ou choisies (« avec »)
    bool colonnes_choisies = false;
    QString type;            // zone : type
    bool facultatif = false, feminin = false;
    bool plusieurs_lignes = false, lecture_seule = false;   // zone (§ 22.6)
    QStringList valeurs;     // zone : valeurs permises (§ 22.7)
    QString actif_si, visible_si;   // la condition telle qu'écrite dans le fichier, vide sinon (§ 22.6)
    QString source;          // image calculée : la valeur telle qu'écrite, sans « de » (§ 22.8) ; vide sinon
    QString depart;          // zone : valeur de départ, telle qu'écrite
    QString tri;             // liste : champ du tri, « » sinon
    bool decroissant = false;
    QString ecrit;           // le texte GrymoiR de l'élément, tel qu'il est dans le fichier
    QString evenement_fichier;   // l'événement de l'élément (clic, choix, changement), s'il existe
    QString evenement_ecrit;     // son texte GrymoiR, pour la confirmation d'une suppression
    int evenement_ligne = 0;
    bool place = false;          // élément d'un bloc en position libre (§ 22.9)
    int graphique = 0;           // graphique (§ 22.11) : 1, + 2 en secteurs, + 4 une somme
    QString graphe_cle, graphe_somme;   // telles qu'écrites, « écrit ␝ chemin » ; vide : pas de somme
    int gauche = 0, haut = 0, largeur = 0, hauteur = 0;   // largeur, hauteur : 0 si non dites
};

struct EcranLu {
    QString nom, titre, fichier;   // nom : « des compositeurs » ; fichier : chemin absolu de la déclaration
    int ligne = 0;
    QVector<ElementLu> elements;
    // L'apparence du programme qui le déclare (grammaire, § 22.5) : rang de la couleur (−1 : bleue par défaut),
    // chemin absolu du logo (vide sans logo) ; accueil : l'écran que ce programme ouvre lui-même au premier niveau.
    int couleur = -1;
    QString logo;
    bool accueil = false;
};

// Les écrans déclarés dans les fichiers .grym et .grymc du dossier, fichiers utilisés compris, chacun une fois.
// *problemes reçoit les fichiers qui ne s'analysent pas.
QVector<EcranLu> lire_ecrans(const QString &dossier, QStringList *problemes = nullptr);

class QListWidget;
class QTextBrowser;
class QPushButton;
class QScrollArea;
class QLabel;

class OngletEcrans : public QWidget {
    Q_OBJECT
public:
    explicit OngletEcrans(QWidget *parent = nullptr);
    void montrer(const QString &dossier);   // relit le projet ; garde l'écran et l'élément choisis
    void choisir_ecran(const QString &nom);
    void choisir_element(int k);            // −1 : l'écran lui-même
    const QVector<EcranLu> &ecrans() const { return lus; }
    QString proprietes() const;             // le texte du panneau des propriétés (pour les essais)
    QWidget *controle(int k) const { return k >= 0 && k < vue.controles.size() ? vue.controles[k] : nullptr; }
    // A4-d : où tombe un élément lâché au point p d'un contrôle de cette taille (CoteDepot) : le quart gauche ou
    // droit met à côté, la moitié haute au-dessus, la moitié basse en dessous.
    static int cote_de_depot(const QSize &taille, const QPointF &p);
    void deposer(int source, int cible, int cote);   // le geste de déplacement
    // Position libre (docs/atelier.md, § 5 octies) : une valeur ramenée sur la grille de 8 pixels, et le geste qui
    // place l'élément k (pour les essais comme pour la souris).
    static int aimanter(int v) { return qMax(0, (v + 4) / 8 * 8); }
    void placer(int k, int gauche, int haut, int largeur, int hauteur);
    // « Mettre en position libre » : l'écran entier, tel qu'on le voit ; avec k, l'élément k va à `nouveau` (coordonnées
    // de l'aperçu) : c'est le premier geste sur un écran décrit, qui le passe en position libre et place l'élément.
    void liberer(int k = -1, QRect nouveau = QRect());
    QRect rect_vu(int k) const;                      // l'élément ou le bloc k dans l'aperçu ; vide pour une fin de bloc
    QWidget *grille_visible() const;                 // la grille d'alignement, si elle est montrée (essais)
    QWidget *poignee_visible() const;                // la poignée de taille, si elle est montrée (essais)
    QString texte_etat() const;                      // la barre d'état de l'onglet (essais)
    // Où « Ajouter » pose (§ 5 decies) : le bloc qui contient l'élément choisi, ou le bloc choisi lui-même ; pour des
    // onglets, leur page ouverte. −1 : la fin de l'écran.
    int cible_ajout() const;
    int onglet_ouvert(int k) const;                  // l'index de l'onglet ouvert des onglets k, ou −1
    int onglets_de(int k) const;                     // les onglets qui contiennent k (ou dont k est un onglet), ou −1
    int onglet_sous(QPoint global, int sauf) const;  // l'onglet dont l'en-tête est sous la souris, hors du bloc sauf ; −1
    // L'onglet dont la page ouverte est sous la souris, si k n'y est pas déjà ; *toile reçoit son bloc libre (ou nullptr).
    int page_sous(QPoint global, int k, QWidget **toile) const;
    bool touche(int t, Qt::KeyboardModifiers m = Qt::NoModifier) { return clavier(t, m); }   // pour les essais

signals:
    void ouvrir(const QString &fichier, int ligne);   // « Voir l'événement », « Voir la déclaration »
    void generer();                                   // « Écran pour une entité… » (A4-b)
    void ecran_vide();                                // « Nouvel écran vide »
    void geste(const std::function<QString(Geste *)> &faire);   // A4-c : une modification, que la fenêtre applique

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void dessiner();
    void editer();                 // le formulaire de l'élément choisi (A4-c)
    void supprimer();
    void ajouter(int sorte);
    QString dossier;
    QWidget *edition = nullptr;
    int glisse = -1;               // l'élément qu'on commence à glisser, et d'où
    QPoint depart;
    int depot_cible = -1, depot_cote = 0;
    class QFrame *repere = nullptr;   // le trait qui montre où l'élément atterrira
    // un élément en position libre qu'on déplace (ou qu'on agrandit par son coin), sa géométrie de départ
    int libre_k = -1;
    int libre_taille = 0;   // 0 : déplacer ; 1 : la largeur (bord droit) ; 2 : la hauteur (bord bas) ; 3 : les deux (coin)
    bool libre_bouge = false;
    bool libre_decrit = false;            // l'élément saisi est dans un écran décrit : un fantôme le montre, le lâcher convertit
    class QFrame *fantome = nullptr;
    QRect libre_fin;                      // où le fantôme est, au lâcher
    QHash<QString, int> pages_ouvertes;   // « écran#k » → la page ouverte des onglets k, gardée d'un aperçu à l'autre
    class QFrame *guide_v = nullptr, *guide_h = nullptr;   // les traits d'alignement sur un voisin (§ 5 octies)
    // Colle r (position ou taille selon libre_taille) aux bords et centres des voisins à moins de 5 pixels, d'après
    // brut (la position sans aimant) ; montre les traits dans `sur`. Rend vrai si un guide a servi.
    bool aligner(QRect &r, const QRect &brut, const QVector<QRect> &voisins, QWidget *sur);
public:
    QWidget *guide_visible() const;               // un trait d'alignement montré (essais)
private:
    static int bord_sous(QWidget *w, QPoint global);   // ce que saisit la souris en ce point de w (même codage)
    QRect libre_depart;
    QWidget *grille = nullptr;
    class QFrame *poignee = nullptr;   // le coin de l'élément placé choisi : le saisir l'agrandit
    bool clavier(int touche, Qt::KeyboardModifiers m);   // flèches : déplacer ; avec Alt : agrandir (§ 5 octies)
    int element_sous(QObject *o) const;
    QVector<EcranLu> lus;
    int courant = -1, element = -1;
    VueEcran vue;
    QListWidget *liste;
    QScrollArea *centre;
    QTextBrowser *panneau;
    QPushButton *voir_evenement, *voir_declaration;
    QLabel *etat;
};

#endif
