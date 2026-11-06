// Les courriels (grammaire, § 22.27) : SMTP (RFC 5321) sur TLS implicite, port 465 par défaut, authentification
// « AUTH PLAIN » (RFC 4616) ; un message texte en UTF-8, encodé en base64 (RFC 2045), l'objet en mot encodé (RFC 2047).
// Chaque attente : 15 secondes au plus. Rien n'est réessayé : l'échec remonte au programme, qui décide (§ 18.2).
#include "courriel.h"

#include <QByteArray>
#include <QDateTime>
#include <QSslSocket>
#include <QUuid>

namespace {

constexpr int ATTENTE = 15000;

// Une réponse du serveur : son code et ses lignes ; « 250-… » continue, « 250 … » finit.
struct Reponse {
    int code = 0;
    QString texte;
};

bool lire(QSslSocket &s, Reponse *r, QString *erreur) {
    r->code = 0;
    r->texte.clear();
    QByteArray ligne;
    for (;;) {
        while (!s.canReadLine()) {
            if (!s.waitForReadyRead(ATTENTE)) {
                *erreur = QStringLiteral("Le serveur de courriel n'a pas répondu en 15 secondes.");
                return false;
            }
        }
        ligne = s.readLine().trimmed();
        if (ligne.size() < 3) {
            *erreur = QStringLiteral("Le serveur de courriel a répondu de travers : « %1 ».").arg(QString::fromUtf8(ligne));
            return false;
        }
        if (!r->texte.isEmpty()) r->texte += QLatin1Char(' ');
        r->texte += QString::fromUtf8(ligne.mid(4));
        if (ligne.size() == 3 || ligne.at(3) == ' ') break;
    }
    bool ok = false;
    r->code = ligne.left(3).toInt(&ok);
    if (!ok) {
        *erreur = QStringLiteral("Le serveur de courriel a répondu de travers : « %1 ».").arg(QString::fromUtf8(ligne));
        return false;
    }
    return true;
}

bool ecrire(QSslSocket &s, const QByteArray &octets, QString *erreur) {
    s.write(octets);
    while (s.bytesToWrite() > 0)
        if (!s.waitForBytesWritten(ATTENTE)) {
            *erreur = QStringLiteral("Le serveur de courriel ne reçoit plus : %1").arg(s.errorString());
            return false;
        }
    return true;
}

// Une commande et le code attendu ; quoi : ce que le message d'erreur dit de l'étape.
bool echanger(QSslSocket &s, const QByteArray &commande, int attendu, int autre, const QString &quoi, QString *erreur) {
    if (!commande.isEmpty() && !ecrire(s, commande + "\r\n", erreur)) return false;
    Reponse r;
    if (!lire(s, &r, erreur)) return false;
    if (r.code != attendu && r.code != autre) {
        *erreur = QStringLiteral("%1 : le serveur a répondu %2 %3").arg(quoi).arg(r.code).arg(r.texte);
        return false;
    }
    return true;
}

// Le base64 en lignes de 76 caractères (RFC 2045, § 6.8).
QByteArray en_lignes(const QByteArray &b64) {
    QByteArray r;
    for (qsizetype k = 0; k < b64.size(); k += 76) r += b64.mid(k, 76) + "\r\n";
    return r;
}

bool en_ascii_simple(const QString &t) {
    for (const QChar c : t)
        if (c.unicode() < 0x20 || c.unicode() > 0x7E) return false;
    return true;
}

}  // namespace

bool grym_courriel(const QString &serveur, const QString &compte, const QString &cle, const QString &de, const QString &a,
                   const QString &objet, const QString &texte, QString *erreur) {
    QString hote = serveur.trimmed();
    quint16 port = 465;
    if (const qsizetype deux = hote.lastIndexOf(QLatin1Char(':')); deux > 0) {
        bool ok = false;
        const uint p = hote.mid(deux + 1).toUInt(&ok);
        if (!ok || p == 0 || p > 65535) {
            *erreur = QStringLiteral("Le port du serveur de courriel est un nombre de 1 à 65535 : « %1 ».").arg(serveur);
            return false;
        }
        port = static_cast<quint16>(p);
        hote = hote.left(deux);
    }
    if (hote.isEmpty()) {
        *erreur = QStringLiteral("Le serveur de courriel est vide.");
        return false;
    }
    // pour les essais seulement : « mail.exemple.ch=127.0.0.1:2465 » envoie à un faux serveur local, dont le certificat
    // fait maison est accepté ; la cible ne peut être que cette machine elle-même, comme pour les envois web (2.42)
    bool essai = false;
    const QString redirection = qEnvironmentVariable("GRYM_ESSAI_SMTP");
    if (const qsizetype egal = redirection.indexOf(QLatin1Char('=')); egal > 0 && redirection.left(egal) == hote) {
        const QString cible = redirection.mid(egal + 1);
        const qsizetype deux = cible.lastIndexOf(QLatin1Char(':'));
        if (deux > 0 && (cible.left(deux) == QLatin1String("127.0.0.1") || cible.left(deux) == QLatin1String("localhost"))) {
            hote = cible.left(deux);
            port = static_cast<quint16>(cible.mid(deux + 1).toUInt());
            essai = true;
        }
    }
    if (!QSslSocket::supportsSsl()) {
        *erreur = QStringLiteral("Le chiffrement TLS n'est pas disponible sur cet ordinateur : le courriel ne part pas.");
        return false;
    }
    QSslSocket s;
    if (essai) s.setPeerVerifyMode(QSslSocket::VerifyNone);
    s.connectToHostEncrypted(hote, port);
    if (!s.waitForEncrypted(ATTENTE)) {
        *erreur = QStringLiteral("Le serveur de courriel « %1 » ne se joint pas en TLS sur le port %2 : %3")
                      .arg(hote).arg(port).arg(s.errorString());
        return false;
    }
    const QByteArray plain = QByteArray(1, '\0') + compte.toUtf8() + QByteArray(1, '\0') + cle.toUtf8();
    if (!echanger(s, {}, 220, 220, QStringLiteral("Accueil du serveur de courriel"), erreur)
        || !echanger(s, "EHLO grymoir", 250, 250, QStringLiteral("Présentation au serveur de courriel"), erreur)
        || !echanger(s, "AUTH PLAIN " + plain.toBase64(), 235, 235,
                     QStringLiteral("Le compte ou la clé de courriel est refusé"), erreur)
        || !echanger(s, "MAIL FROM:<" + de.toUtf8() + ">", 250, 250, QStringLiteral("L'expéditeur est refusé"), erreur)
        || !echanger(s, "RCPT TO:<" + a.toUtf8() + ">", 250, 251, QStringLiteral("Le destinataire est refusé"), erreur)
        || !echanger(s, "DATA", 354, 354, QStringLiteral("Le serveur refuse le message"), erreur))
        return false;
    QString corps = texte;
    corps.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    corps.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    QByteArray message;
    message += "From: <" + de.toUtf8() + ">\r\n";
    message += "To: <" + a.toUtf8() + ">\r\n";
    message += "Subject: "
               + (en_ascii_simple(objet) ? objet.toUtf8() : "=?UTF-8?B?" + objet.toUtf8().toBase64() + "?=") + "\r\n";
    message += "Date: " + QDateTime::currentDateTime().toString(Qt::RFC2822Date).toUtf8() + "\r\n";
    message += "Message-ID: <" + QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8() + "@grymoir>\r\n";
    message += "MIME-Version: 1.0\r\n";
    message += "Content-Type: text/plain; charset=UTF-8\r\n";
    message += "Content-Transfer-Encoding: base64\r\n\r\n";
    message += en_lignes(corps.toUtf8().toBase64());   // une ligne base64 ne commence jamais par « . »
    message += ".";
    if (!echanger(s, message, 250, 250, QStringLiteral("Le message est refusé"), erreur)) return false;
    QString ignore;
    echanger(s, "QUIT", 221, 221, QString(), &ignore);   // le courriel est parti : un adieu manqué n'y change rien
    s.disconnectFromHost();
    return true;
}
