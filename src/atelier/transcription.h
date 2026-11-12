// La transcription d'un son (grammaire, § 15.8) : décodage en 16 kHz mono, puis Whisper (whisper.cpp), dans la
// configuration retenue après essai : large-v3-turbo, sans contexte entre les passages (comme « -mc 0 »), sans découpe
// du son, recherche en faisceau comme whisper-cli ; puis le retrait des phrases que Whisper invente dans les silences.
#pragma once

#include <QByteArray>
#include <QString>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// Le modèle : à côté de l'application (Contents/Resources/modeles sur Mac, modeles/ ailleurs), sinon dans le dossier
// de données de GrymoiR (celui de l'essai de transcription). Vide s'il est introuvable.
QString modele_de_transcription();

// Un WAV PCM (8, 16, 24, 32 bits ou flottant), n'importe quelle fréquence, mono ou stéréo, en 16 kHz mono ; faux si ce
// n'est pas un WAV lisible. Écrit dans GrymoiR, sans greffon : les essais en dépendent.
bool wav_en_16k(const QByteArray &octets, std::vector<float> *pcm);
// Les autres formats (MP3, M4A, Ogg, WebM, FLAC…) par le décodeur de Qt. À appeler hors du fil de l'interface (il
// attend dans sa propre boucle).
QString extension_de_son(const QByteArray &octets);   // « m4a », « wav », … d'après les premiers octets
bool decoder_en_16k(const QByteArray &octets, std::vector<float> *pcm, QString *erreur);
// Les phrases inventées dans les silences, retirées (formules de génériques de sous-titres).
QString nettoyer_transcription(const QString &texte);

// La relecture (§ 15.8) : des caractères Unicode invisibles, rangés dans le texte même pour qu'ils le suivent partout
// (base, écrans, copies). Un texte transcrit commence par MARQUE_TRANSCRIT (ses nombres sont surlignés) ; un mot dont
// Whisper doute (probabilité sous 0,5) est entre DEBUT_DOUTE et FIN_DOUTE.
inline constexpr char16_t MARQUE_TRANSCRIT = 0x2064, DEBUT_DOUTE = 0x2062, FIN_DOUTE = 0x2063;

// Libère le modèle gardé en mémoire ; appelé de lui-même à la fermeture de l'application (sur Mac, Metal l'exige).
void liberer_transcription();
bool modele_charge();

// Le texte d'un son, ou vide avec *erreur. Le modèle est chargé une fois, puis gardé.
// amorce (2.80) : le vocabulaire attendu (indicatifs, codes, noms propres), donné à Whisper comme texte précédent.
QString transcrire_son(const QByteArray &octets, const QString &modele, QString *erreur, const QString &amorce = QString());

// Le texte d'un son déjà en 16 kHz mono ; rapide : la recherche gloutonne (pour le direct).
QString transcrire_pcm(std::vector<float> pcm, const QString &modele, QString *erreur, bool rapide, const QString &amorce = QString());

// Du son brut (n'importe quelle fréquence, mono ou stéréo entrelacé) ajouté à « pcm », en 16 kHz mono.
void pcm_en_16k(const std::vector<float> &entree, int frequence, int canaux, std::vector<float> *pcm);

// La dictée par segments (12 novembre 2026) : pendant qu'on parle, le son est coupé à chaque pause (0,7 seconde sous le
// seuil, le seuil tiré du bruit de fond de l'enregistrement même), au plus tard à vingt-cinq secondes, à l'endroit le plus
// calme ; chaque segment fermé part aussitôt dans Whisper en recherche en faisceau, la qualité du texte final, dans un fil
// à part. À l'arrêt, il ne reste que le dernier segment à transcrire : le texte final n'attend plus une passe sur tout le
// son. Aucun son n'est écarté : les segments se suivent sans trou, du premier échantillon au dernier. Un segment ne fait
// jamais moins de deux secondes (Whisper invente sur un son trop court) ; la fin de la dictée, si elle est plus courte,
// rejoint le segment d'avant, refait. Le direct (l'aperçu en italique) ne porte plus que sur le segment ouvert.
// Tout est protégé par un verrou : l'interface, le fil des segments et celui qui attend le texte s'y croisent.
struct Dictee {
    double pause_secondes = 0.7, minimum_secondes = 2, maximum_secondes = 25;
    void ajouter(const std::vector<float> &pcm16k);   // du son ; ferme les segments finis
    void finir();                                     // l'arrêt : ce qui reste devient le dernier segment
    void abandonner();                                // rien ne sera lu : le fil s'arrête, qui attend repart sans texte
    // le direct : le segment ouvert, à retranscrire en entier (une seconde et demie au moins)
    bool passe(std::vector<float> *morceau, size_t *debut, size_t *fin) const;
    void recevoir(const QString &texte, size_t debut);   // l'aperçu du morceau qui commençait à « debut »
    // les segments fermés : le premier qui n'a pas son texte ; sa version change s'il est refait entre-temps
    bool segment_a_faire(int *rang, int *version, std::vector<float> *morceau) const;
    void recevoir_segment(int rang, int version, const QString &texte);
    void echouer();                                   // Whisper a échoué : le texte viendra de la passe complète
    bool en_retard() const;                           // un segment fermé attend son texte : le direct lui laisse la place
    QString texte() const;                            // ce qu'on montre pendant la dictée
    QString texte_final() const;                      // les textes des segments, avec leurs marques de relecture
    bool attendre(QString *texte);                    // jusqu'au dernier segment ; faux : abandon ou échec
    void travailler(const QString &modele);           // le fil des segments, jusqu'au dernier
    std::vector<std::pair<size_t, size_t>> bornes() const;   // début et fin de chaque segment fermé
    size_t echantillons() const;                      // tout le son reçu
    bool finie() const;

private:
    struct Segment { size_t debut = 0, fin = 0; QString apercu, texte; bool fait = false; int version = 0; };
    mutable std::mutex verrou_;
    std::condition_variable signal_;
    std::vector<float> son_;          // 16 kHz mono, depuis le début
    std::vector<float> niveaux_;      // le niveau efficace de chaque trame de 20 ms, en dB
    int histogramme_[101] = {0};      // les niveaux, de −100 à 0 dB, hors silence numérique
    int trames_comptees_ = 0;
    size_t total_ = 0;                // le son reçu (son_ est rendu une fois la dictée transcrite)
    size_t debut_ = 0;                // le début du segment ouvert
    std::vector<Segment> segments_;
    QString courant_;
    bool terminee_ = false, abandonnee_ = false, echec_ = false;
    double seuil() const;
    void couper();
    bool tout_fait() const;
};

// Une dictée promise pour un enregistrement : transcrire_son, appelé plus tard avec ces octets et sans amorce, attend
// son texte au lieu de refaire la passe complète (les huit dernières dictées sont gardées). Sans texte (abandon, échec,
// rien reconnu), la passe complète a lieu, comme avant.
void promettre_transcription(const QByteArray &octets, std::shared_ptr<Dictee> dictee);
// GRYMOIR_DICTEE_ENTIERE : la dictée transcrite d'un bloc à l'arrêt, comme avant le 12 novembre 2026 (pour comparer).
bool dictee_par_segments();
// grym-atelier --comparer-dictee son : le même son transcrit d'un bloc puis par segments, les deux textes écrits à côté
// du son, les durées et l'écart en mots affichés. Rend 0 si tout a tourné.
int comparer_dictee(const QString &chemin);
