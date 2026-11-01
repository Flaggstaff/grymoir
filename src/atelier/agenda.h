// GrymoiR : l'atelier, la liste en agenda (grammaire 2.74, § 22.24, 1er novembre 2026) : une grille horaire, une colonne
// par jour montré, les objets posés à leur heure et sur leur durée. Un clic choisit un objet ; le glisser le dépose
// ailleurs, au quart d'heure, et la fenêtre envoie « Quand on déplace … ». La grille vient du programme : un texte
// « JJ.MM.AAAA|JJ.MM.AAAA|1 08:00-12:00 13:30-18:00|2 …|… » : le premier et le dernier jour montrés, puis, pour chaque jour
// travaillé de la semaine (1 lundi … 7 dimanche), ses plages ; un jour sans plage n'est pas montré ; sans aucune plage,
// tous les jours le sont, sans rien griser.
#ifndef GRYM_ATELIER_AGENDA_H
#define GRYM_ATELIER_AGENDA_H

#include <QDate>
#include <QMap>
#include <QTime>
#include <QVector>
#include <QWidget>

class Agenda : public QWidget {
    Q_OBJECT
public:
    explicit Agenda(QWidget *parent = nullptr);
    void donner_grille(const QString &grille);
    // les lignes de la liste : date, heure, durée en minutes (vide : 60), puis les colonnes montrées dans le bloc
    void donner_lignes(const QStringList &cellules, int nb_lignes, int nb_colonnes);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    // pour les essais : les jours montrés, la place d'un objet, et un dépôt simulé
    QVector<QDate> jours() const { return jours_; }
    QRect place(int rang) const;
    QPoint point_de(const QDate &jour, const QTime &heure) const;

signals:
    void choisir(int rang);
    void deplacer(int rang, const QString &arrivee);   // « JJ.MM.AAAA|HH:MM »
    void creneau(const QString &arrivee);              // un clic hors des objets, au quart d'heure inférieur (2.75)

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    struct Bloc { QDate jour; QTime debut; int duree = 60; QStringList textes; int rang = -1; };
    QDate premier_, dernier_;
    QMap<int, QVector<QPair<QTime, QTime>>> plages_;   // jour de la semaine (1 à 7) → plages
    QVector<QDate> jours_;
    QVector<Bloc> blocs_;
    int heure_debut_ = 7, heure_fin_ = 20;
    int saisi_ = -1;          // le bloc sous la souris au clic
    bool glisse_ = false;
    bool vide_ = false;       // le clic est tombé hors des objets
    QPoint depart_, courant_;
    void recalculer();
    int largeur_heures() const { return 56; }
    int haut_tete() const { return 44; }
    double pixels_par_minute() const { return 52.0 / 60.0; }
    int colonne_x(int c) const;
    int largeur_colonne() const;
    bool arrivee(const QPoint &p, QDate *jour, QTime *heure) const;
};

#endif
