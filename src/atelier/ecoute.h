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
// Du son de 16 kHz pour une sortie : sa fréquence, ses canaux, son format (QAudioFormat::SampleFormat).
QByteArray en_sortie(const std::vector<float> &pcm16k, int frequence, int canaux, int format);

// Un filtre du second ordre (RBJ, « Audio EQ Cookbook ») à 16 kHz : passe-haut ou passe-bas, Q = 0,707.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void regler(bool passe_haut, double frequence, double echantillonnage = 16000);
    float passer(float x);
    void vider() { x1 = x2 = y1 = y2 = 0; }
};

// L'affinage du son (2 novembre 2026), avant le découpage, le haut-parleur et la transcription : le gain (en dB, de −30
// à +30), puis, si voulu, le filtre radio (de 150 à 3000 Hz : le ronflement du secteur et le souffle aigu ôtés ; 150 et non
// 300 Hz depuis le 2 novembre 2026 : la fondamentale d'une voix d'homme, vers 190 Hz, y perdait 5 dB). Le
// résultat est borné à ±1 : un gain trop fort sature, et le bandeau le dit.
struct Affinage {
    int gain_db = 0;
    bool filtre = false;
    void appliquer(std::vector<float> *pcm16k);
    void vider() { haut.vider(); bas.vider(); }
    Affinage() { haut.regler(true, 150); bas.regler(false, 3000); }

private:
    Biquad haut, bas;
};

// Le squelch montré à Michel : de −50 à +50, 0 au milieu, un cran pour ½ dB, soit de −60 à −10 dB sur le niveau
// efficace ; à gauche, il laisse passer les sons faibles (il prend plus), à droite seulement les forts.
double squelch_en_db(int position);
int position_du_squelch(double db);

class QAudioSink;
class QAudioSource;
class QCheckBox;
class QComboBox;
class QIODevice;
class QLabel;
class QProgressBar;
class QSlider;
class Reglette;
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
    QSlider *squelch() const;
    QSlider *gain() const;
    QCheckBox *filtre() const { return filtre_; }
    // « Calibrer » (2 novembre 2026) : écouter le bruit de fond, poste allumé sans émission, puis placer le squelch
    // 6 dB au-dessus de son niveau le plus haut ordinaire (le 90e centile des fenêtres de 20 ms)
    void calibrer(int duree_ms = 5000);
    bool finir_calibrage();   // rend faux s'il n'a rien entendu ; les essais l'appellent sans attendre
    bool en_calibrage() const { return calibrage; }
    // La dernière minute (5 novembre 2026) : le son affiné des 60 dernières secondes, gardé en mémoire, passage ou non ;
    // « Réécouter » la rejoue sur le haut-parleur, « Garder » en fait un passage, comme si le squelch s'était ouvert.
    static constexpr size_t MINUTE = 60 * 16000;
    std::vector<float> derniere_minute() const;
    void garder_minute();
    void reecouter_minute();
    // Un fichier son comme entrée (5 novembre 2026) : un WAV, lu au rythme réel, pour essayer réglages, morse et
    // transcription sans poste ; à sa fin, le passage en cours se ferme. avancer_fichier : un pas de 20 ms (les essais).
    bool ouvrir_fichier(const QString &chemin, QString *erreur = nullptr);
    bool avancer_fichier();   // faux à la fin du fichier
    bool en_fichier() const { return !fichier_pcm.empty(); }

private:
    QString nom;
    Decoupeur d;
    QComboBox *entree_;
    QCheckBox *veille_;
    bool materiel;
    QProgressBar *niveau_;
    Reglette *seuil_, *gain_;
    QCheckBox *filtre_;
    Affinage affinage;
    QToolButton *calibrer_ = nullptr;
    QToolButton *reecouter_ = nullptr, *garder_ = nullptr;
    std::vector<float> tampon;   // circulaire, MINUTE échantillons au plus
    size_t tete = 0;             // la prochaine case écrite
    QAudioSink *rejeu = nullptr;
    class QBuffer *rejeu_octets = nullptr;
    std::vector<float> fichier_pcm;
    size_t fichier_pos = 0;
    class QTimer *lecteur = nullptr, *relance = nullptr;
    void fin_du_fichier();
    void choisir_fichier();
    void perte_entree();   // l'entrée s'est tue (débranchée) : on réessaie toutes les 2 secondes
    bool calibrage = false;
    std::vector<float> bruit;
    class QTimer *minuterie = nullptr;
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
    void retenir(const std::vector<float> &pcm16k);
    void montrer();
    QString cle(const QString &reglage) const;
};
