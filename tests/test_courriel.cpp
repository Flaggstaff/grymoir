// Essai du client SMTP de l'atelier (grammaire, § 22.27), contre tests/faux_smtp.py, lancé par tests/courriel.
// test_courriel port sortie : trois courriels, au faux serveur en mode « ok » ; le script vérifie aussi les refus.
// test_courriel port sortie refus_auth|refus_rcpt : un courriel, qui doit être refusé avec la réponse du serveur.
#include <QCoreApplication>
#include <QFile>
#include <cstdio>

#include "courriel.h"

static int echecs = 0, total = 0;

static void verifier(bool ok, const char *quoi) {
    total++;
    if (!ok) {
        echecs++;
        std::fprintf(stderr, "ÉCHEC : %s\n", quoi);
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) return 2;
    const QString port = QString::fromUtf8(argv[1]);
    const QString sortie = QString::fromUtf8(argv[2]);
    qputenv("GRYM_ESSAI_SMTP", ("mail.exemple.ch=127.0.0.1:" + port).toUtf8());
    QString erreur;
    if (argc > 3) {   // un refus attendu
        const bool ok = grym_courriel("mail.exemple.ch", "cabinet@exemple.ch", "s3cret", "cabinet@exemple.ch",
                                      "ana@exemple.ch", "Objet", "Texte", &erreur);
        const QByteArray e = erreur.toUtf8();
        std::printf("%s\n", e.constData());
        verifier(!ok, "le refus est signalé");
        if (QByteArray(argv[3]) == "refus_auth") verifier(e.contains("Le compte ou la clé de courriel est refusé : le serveur "
                                                                      "a répondu 535"), "la réponse 535 est dans le motif");
        else verifier(e.contains("Le destinataire est refusé : le serveur a répondu 550"), "la réponse 550 est dans le motif");
    } else {
        // 1. un courriel accentué, sur plusieurs lignes, avec une ligne qui commence par un point
        bool ok = grym_courriel("mail.exemple.ch", "cabinet@exemple.ch", "clé àé", "cabinet@exemple.ch", "ana@exemple.ch",
                                "Votre séance", "Bonjour Ana,\n\nVotre séance est confirmée.\n.Point en tête.", &erreur);
        verifier(ok, "le courriel part");
        if (!ok) std::fprintf(stderr, "  motif : %s\n", erreur.toUtf8().constData());
        // 2. un objet tout ASCII, écrit tel quel
        ok = grym_courriel("mail.exemple.ch:999", "c", "k", "c@exemple.ch", "b@exemple.ch", "Rappel", "x", &erreur);
        verifier(ok, "le port écrit dans le serveur est remplacé par la redirection d'essai");
        // 3. la redirection d'essai ne vise que cette machine : un autre serveur n'est jamais redirigé
        ok = grym_courriel("autre.exemple.invalid", "c", "k", "c@exemple.ch", "b@exemple.ch", "x", "x", &erreur);
        verifier(!ok && erreur.contains("ne se joint pas en TLS"), "un serveur introuvable est signalé");
        // 4. un port impossible
        ok = grym_courriel("mail.exemple.ch:99999", "c", "k", "c@exemple.ch", "b@exemple.ch", "x", "x", &erreur);
        verifier(!ok && erreur.contains("nombre de 1 à 65535"), "un port impossible est refusé");
        QFile f(sortie);
        verifier(f.open(QIODevice::ReadOnly), "le faux serveur a écrit ce qu'il a reçu");
        const QByteArray recu = f.readAll();
        const QList<QByteArray> envois = recu.split('=');
        verifier(recu.contains("AUTH:|cabinet@exemple.ch|clé àé"), "l'identité est envoyée en AUTH PLAIN, en UTF-8");
        verifier(recu.contains("FROM:<cabinet@exemple.ch>") && recu.contains("TO:<ana@exemple.ch>"),
                 "l'expéditeur et le destinataire");
        verifier(recu.contains("Subject: =?UTF-8?B?" + QByteArray("Votre séance").toBase64() + "?="),
                 "un objet accentué en mot encodé");
        verifier(recu.contains("Subject: Rappel\n"), "un objet ASCII tel quel");
        verifier(recu.contains("Content-Type: text/plain; charset=UTF-8") && recu.contains("Content-Transfer-Encoding: base64"),
                 "le type du message");
        // le corps : les lignes base64 après la ligne vide du premier message
        const qsizetype d = recu.indexOf("\n\n");
        const qsizetype fin = recu.indexOf("\n=====", d);
        const QByteArray b64 = recu.mid(d + 2, fin - d - 2).replace("\n", "");
        verifier(QByteArray::fromBase64(b64) == QByteArray("Bonjour Ana,\r\n\r\nVotre séance est confirmée.\r\n.Point en tête."),
                 "le corps revient tel quel, en lignes CRLF");
        (void)envois;
    }
    std::printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}
