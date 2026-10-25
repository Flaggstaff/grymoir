// La transcription d'un son (grammaire, § 15.8) : décodage en 16 kHz mono, puis Whisper (whisper.cpp), dans la
// configuration retenue après essai : large-v3-turbo, sans contexte entre les passages (comme « -mc 0 »), sans découpe
// du son, recherche en faisceau comme whisper-cli ; puis le retrait des phrases que Whisper invente dans les silences.
#pragma once

#include <QByteArray>
#include <QString>
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
QString transcrire_son(const QByteArray &octets, const QString &modele, QString *erreur);

// Le texte d'un son déjà en 16 kHz mono ; rapide : la recherche gloutonne (pour le direct).
QString transcrire_pcm(std::vector<float> pcm, const QString &modele, QString *erreur, bool rapide);

// La transcription en direct (24 octobre 2026) : pendant qu'on parle, le son arrive par morceaux ; à chaque passe, tout ce
// qui suit le dernier point acquis est retranscrit (moins de vingt-cinq secondes : pas de mot coupé à raccorder) ; une
// fenêtre pleine devient acquise et la suivante repart de sa fin. Le texte final, à l'arrêt, reprend tout le son.
void pcm_en_16k(const std::vector<float> &entree, int frequence, int canaux, std::vector<float> *pcm);
struct TranscriptionDirecte {
    double fenetre_secondes = 25;
    std::vector<float> son;   // 16 kHz mono, depuis le début de l'enregistrement
    size_t debut = 0;         // le début de la fenêtre en cours
    QString acquis, courant;
    void ajouter(const std::vector<float> &pcm16k);
    bool passe(std::vector<float> *morceau, size_t *fin) const;   // le morceau à transcrire maintenant, s'il y en a un
    void recevoir(const QString &texte, size_t fin);            // le texte du morceau qui finissait à « fin »
    QString texte() const;                                      // ce qu'on montre
};
