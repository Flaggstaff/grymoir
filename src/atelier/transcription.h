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
bool decoder_en_16k(const QByteArray &octets, std::vector<float> *pcm, QString *erreur);
// Les phrases inventées dans les silences, retirées (formules de génériques de sous-titres).
QString nettoyer_transcription(const QString &texte);

// Le texte d'un son, ou vide avec *erreur. Le modèle est chargé une fois, puis gardé.
QString transcrire_son(const QByteArray &octets, const QString &modele, QString *erreur);
