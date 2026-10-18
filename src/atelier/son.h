#pragma once
// Écouter un son, en enregistrer un au micro (grammaire, § 15.7) : dans la fenêtre d'exécution de l'atelier et dans les
// applications fabriquées, par Qt Multimedia. Le navigateur a les siens (balise audio, attribut capture).
#include <QByteArray>
#include <QString>
#include <QWidget>
#include <functional>

class QAudioInput;
class QAudioOutput;
class QBuffer;
class QLabel;
class QMediaCaptureSession;
class QMediaPlayer;
class QMediaRecorder;
class QPushButton;
class QTemporaryDir;

// Des octets de son, à leur signature, comme la machine les reconnaît (format_son, src/vm.c) : WAV, MP3, AAC, M4A,
// Ogg, WebM, FLAC.
bool octets_de_son(const QByteArray &o);

// « ▶ Écouter » puis « ■ Arrêter » : un son en mémoire, rien sur le disque. Désactivé tant qu'il n'a rien à jouer.
class LecteurSon : public QWidget {
    Q_OBJECT
public:
    explicit LecteurSon(QWidget *parent = nullptr);
    ~LecteurSon() override;
    // `nom` donne l'extension, qui aide le décodeur (« memo.m4a ») ; des octets vides : plus rien à écouter
    void donner(const QByteArray &octets, const QString &nom);
    bool a_un_son() const { return !octets.isEmpty(); }
    void basculer();   // écouter, ou arrêter
    QPushButton *bouton() const { return bouton_; }
    QLabel *message() const { return message_; }
    QString nom_du_son() const { return nom; }
    std::function<void(const QString &)> signaler;   // un message (erreur, durée), en plus de l'étiquette ; facultatif

private:
    void arreter();
    QPushButton *bouton_;
    QLabel *message_;
    QByteArray octets;
    QString nom;
    QMediaPlayer *lecteur = nullptr;
    QAudioOutput *sortie = nullptr;
    QBuffer *tampon = nullptr;
};

// « ● Enregistrer » puis « ■ Arrêter (0:12) » : le micro par défaut, dans un fichier provisoire, rendu en octets.
// Le format : le premier que ce système sait écrire parmi M4A (AAC), Ogg (Opus, Vorbis), FLAC et WAV, tous reconnus
// par la machine (§ 15.7). Sous macOS, l'autorisation du micro est demandée au premier enregistrement.
class EnregistreurSon : public QWidget {
    Q_OBJECT
public:
    explicit EnregistreurSon(QWidget *parent = nullptr);
    ~EnregistreurSon() override;
    // appelé à la fin d'un enregistrement : ses octets, et un nom (« enregistrement-2026-10-18-1432.m4a »)
    std::function<void(const QByteArray &, const QString &)> enregistre;
    void basculer();   // commencer, ou arrêter
    bool enregistre_en_cours() const;
    QPushButton *bouton() const { return bouton_; }
    QLabel *message() const { return message_; }

    // Le format choisi pour ce système : l'extension (« m4a »), vide si aucun ne s'écrit ici.
    static QString extension_choisie();

private:
    void commencer();
    void fini();
    QPushButton *bouton_;
    QLabel *message_;
    QMediaCaptureSession *session = nullptr;
    QAudioInput *entree = nullptr;
    QMediaRecorder *enregistreur = nullptr;
    QTemporaryDir *dossier = nullptr;
    QString extension;
};
