#pragma once
// Écouter un son, en enregistrer un au micro (grammaire, § 15.7) : dans la fenêtre d'exécution de l'atelier et dans les
// applications fabriquées, par Qt Multimedia. Le navigateur a les siens (balise audio, attribut capture).
#include <QByteArray>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

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
    class QComboBox *micros() const { return micro_; }
    class QToolButton *choix_du_micro() const { return choix_; }

    // Le format choisi pour ce système : l'extension (« m4a »), vide si aucun ne s'écrit ici.
    static QString extension_choisie();

private:
    void commencer();
    void fini();
    QPushButton *bouton_;
    QLabel *message_;
    class QComboBox *micro_;   // le micro choisi (24 octobre 2026), retenu d'une fois à l'autre ; caché depuis le 27 octobre
    class QToolButton *choix_;   // « Micro ▾ » : le menu des micros, étroit, à la place de la liste déroulante
    void remplir_micros();
    QMediaCaptureSession *session = nullptr;
    QAudioInput *entree = nullptr;
    QMediaRecorder *enregistreur = nullptr;
    QTemporaryDir *dossier = nullptr;
    QString extension;
    bool muet_ = false;   // arrêté faute de son (24 octobre 2026) : rien n'est gardé
    // le direct (24 octobre 2026) : le micro lu aussi en PCM, transcrit pendant qu'on parle, montré à côté ; par segments
    // depuis le 12 novembre 2026 : à l'arrêt, le texte final est presque prêt (transcription.h, Dictee)
    class QAudioSource *source_directe = nullptr;
    class QIODevice *flux_direct = nullptr;
    class QTimer *minuterie_directe = nullptr;
    std::shared_ptr<struct Dictee> direct;
    int frequence_directe = 0, canaux_directs = 1, format_direct = 0;
    bool passe_en_cours = false;
    qint64 duree_ms_ = 0;   // la durée que l'enregistreur annonce : le direct doit avoir reçu le même son
    void demarrer_direct(const class QAudioDevice &micro);
    void lire_direct();      // ce que le micro a donné depuis la dernière lecture
    void figer_direct();     // l'arrêt demandé : le micro du direct se ferme, la dictée reste
    void arreter_direct();   // tout s'arrête, la dictée est abandonnée
    void lancer_passe();
    void montrer_direct();
public:
    QString texte_direct() const;   // ce que le direct montre en ce moment (pour les essais)
    void nourrir_direct(const std::vector<float> &pcm16k);   // pour les essais : du son, comme s'il venait du micro
    std::shared_ptr<struct Dictee> dictee() const { return direct; }   // pour les essais
};
