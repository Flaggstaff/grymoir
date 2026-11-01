#pragma once
// L'écoute (grammaire, 2.69) : une entrée son de l'ordinateur, un de ses canaux, découpée en passages. Chaque passage
// fini devient un WAV de 16 kHz mono, rendu à l'exécution, qui le donne à la machine (EVENEMENT_CAPTURE).
// Le découpeur ne dépend pas du multimédia de Qt : les essais le nourrissent directement.
#include <QByteArray>
#include <QString>
#include <QWidget>
#include <functional>
#include <vector>

// Un passage commence quand le niveau dépasse le seuil, et finit après `silence_ms` sous le seuil. Il garde les
// 300 ms qui précèdent (la première syllabe) et 300 ms de silence après la dernière parole. Moins de 300 ms de parole :
// un craquement, oublié. Plus de 5 minutes : coupé, et un autre commence.
struct Decoupeur {
    int silence_ms = 1500;
    float seuil = 0.02f;     // niveau efficace (RMS), de 0 à 1
    float niveau = 0;        // le niveau de la dernière fenêtre de 20 ms
    float crete = 0;         // sa crête : près de 1, le son sature
    bool en_passage = false;
    // du son en 16 kHz mono ; chaque passage terminé s'ajoute à *finis
    void nourrir(const float *pcm, size_t n, std::vector<std::vector<float>> *finis);
    void finir(std::vector<std::vector<float>> *finis);   // l'arrêt : le passage en cours, s'il compte
    double secondes_en_cours() const { return passage.size() / 16000.0; }

private:
    std::vector<float> fenetre, avant, passage;
    size_t silence = 0, parole = 0;
    void fenetre_pleine(std::vector<std::vector<float>> *finis);
    void clore(std::vector<std::vector<float>> *finis);
};

// Un WAV PCM 16 bits, 16 kHz, mono.
QByteArray wav_16k(const std::vector<float> &pcm);

class QAudioSink;
class QAudioSource;
class QCheckBox;
class QComboBox;
class QIODevice;
class QLabel;
class QProgressBar;
class QSlider;
class QToolButton;

// Le bandeau d'une écoute : l'entrée (appareil et canal), le niveau, le seuil, le haut-parleur, l'état. Les réglages
// sont retenus par nom d'écoute, d'un lancement à l'autre.
class Ecoute : public QWidget {
    Q_OBJECT
public:
    // materiel faux : ni entrée, ni micro, ni haut-parleur (les essais, qui le nourrissent à la main) ;
    // veille_libre faux : la veille est décidée par le programme (« les écoutes des postes … dont la veille est vraie »,
    // 2.70) : la case est cachée, toujours cochée
    Ecoute(const QString &nom, int silence_ms, QWidget *parent = nullptr, bool materiel = true, bool veille_libre = true);
    // Un poste renommé (1er novembre 2026) : son entrée, son seuil, son haut-parleur passent au nouveau nom, sans écraser
    // ce que le nouveau nom aurait déjà ; l'ancien nom les perd. Rend vrai si quelque chose a été reporté.
    static bool reporter_reglages(const QString &ancien, const QString &nouveau);
    QString nom_ecoute() const { return nom; }
    ~Ecoute() override;
    std::function<void(const QByteArray &wav, const QString &nom_fichier)> capte;
    void nourrir(const std::vector<float> &pcm16k);   // pour les essais : du son, comme s'il venait de l'entrée
    const Decoupeur &decoupeur() const { return d; }
    QComboBox *entrees() const { return entree_; }
    QCheckBox *veille() const { return veille_; }   // « En veille » : décochée, l'entrée reste fermée et rien ne part

private:
    QString nom;
    Decoupeur d;
    QComboBox *entree_;
    QCheckBox *veille_;
    bool materiel;
    QProgressBar *niveau_;
    QSlider *seuil_;
    QToolButton *haut_parleur_;
    QLabel *etat_;
    QAudioSource *source = nullptr;
    QIODevice *flux = nullptr;
    QAudioSink *sortie = nullptr;
    QIODevice *flux_sortie = nullptr;
    int frequence = 0, canaux = 1, format = 0, canal = 0;
    int frequence_sortie = 0, canaux_sortie = 1, format_sortie = 0;
    void remplir();
    void demarrer();
    void couper();    // le son seul : l'entrée fermée, le passage en cours oublié ; sûr dans le destructeur
    void arreter();   // couper, puis l'affichage remis à zéro
    void lire();
    void ouvrir_sortie();
    void fermer_sortie();
    void jouer(const std::vector<float> &pcm16k);
    void montrer();
    QString cle(const QString &reglage) const;
};
