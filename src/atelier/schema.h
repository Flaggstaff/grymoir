// GrymoiR : l'atelier, le schéma des données (docs/atelier.md, A2-a) : les entités du projet, lues dans
// l'arbre des fichiers, et leur dessin. En lecture seule : rien ne s'écrit dans les .grym.
#ifndef GRYM_ATELIER_SCHEMA_H
#define GRYM_ATELIER_SCHEMA_H

#include <QGraphicsView>
#include <QMap>
#include <QStringList>
#include <QVector>

struct ChampSchema {
    QString nom, type;
    QString depart;   // valeur de départ, telle qu'on l'écrit (« Suisse », « 12,50 »), ou vide
    bool unique = false, facultatif = false, multiple = false, cascade = false, lien = false, feminin = false;
};

struct EntiteSchema {
    QString nom, parent, fichier;   // fichier : chemin absolu de la déclaration
    int ligne = 0;
    bool feminin = false;
    QStringList aptitudes;
    QVector<ChampSchema> champs;
};

// Les entités déclarées dans les fichiers .grym et .grymc du dossier, sous-dossiers compris, chacune une fois.
// *problemes reçoit les fichiers qui ne s'analysent pas (leurs entités manquent au schéma).
QVector<EntiteSchema> lire_schema(const QString &dossier, QStringList *problemes = nullptr);

class QGraphicsScene;

class VueSchema : public QGraphicsView {
    Q_OBJECT
public:
    explicit VueSchema(QWidget *parent = nullptr);
    // Dessine les entités ; une entité sans position reçoit une place libre, que positions() rendra.
    void montrer(const QVector<EntiteSchema> &entites, const QMap<QString, QPointF> &positions);
    QMap<QString, QPointF> positions() const;
    int nombre_de_boites() const;
    int nombre_de_liens() const;
    void choisir(const QString &entite);   // sélectionne la boîte, sans rien émettre de plus que « choisie »

signals:
    void deplacee();                                        // une boîte a bougé : la disposition est à garder
    void ouvrir(const QString &fichier, int ligne);          // double-clic : la déclaration dans le code
    void choisie(const QString &entite);                     // la boîte choisie (vide : aucune)
    void lier(const QString &de, const QString &vers);       // Maj+glisser d'une boîte à l'autre, ou menu « Lier à »
    void lien_choisi(const QString &de, const QString &champ, bool supprimer);   // double-clic ou menu sur une flèche

private:
    void relier();
    void contextMenuEvent(QContextMenuEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    class QGraphicsLineItem *trait = nullptr;   // le lien en train d'être tiré (Maj+glisser)
    QString trait_de;
    QGraphicsScene *scene_;
    bool en_construction = false;   // pendant montrer(), les boîtes qui se placent ne comptent pas comme déplacées
    QVector<EntiteSchema> entites;
};

#endif
