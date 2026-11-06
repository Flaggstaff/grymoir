#!/usr/bin/env python3
"""Faux serveur SMTP sur TLS implicite, pour tests/test_courriel.cpp (grammaire, § 22.27).

faux_smtp.py port certificat clé sortie mode [connexions]
mode : « ok », « refuse_auth » (535), « refuse_rcpt » (550). Il sert le nombre de connexions demandé (1 par défaut),
écrit dans « sortie », pour chacune, l'identité reçue, l'expéditeur, le destinataire et le message brut, puis s'arrête.
Il n'écoute que sur 127.0.0.1.
"""
import base64
import socket
import ssl
import sys


def servir(conn, sortie, mode):
    f = conn.makefile("rwb")

    def dire(ligne):
        f.write(ligne.encode() + b"\r\n")
        f.flush()

    def lire():
        return f.readline().decode("utf-8", "replace").rstrip("\r\n")

    dire("220 faux.grymoir ESMTP")
    notes = []
    while True:
        l = lire()
        if not l:
            break
        m = l.upper()
        if m.startswith("EHLO"):
            dire("250-faux.grymoir")
            dire("250 AUTH PLAIN")
        elif m.startswith("AUTH PLAIN "):
            brut = base64.b64decode(l[11:]).split(b"\0")
            notes.append("AUTH:" + "|".join(p.decode() for p in brut))
            dire("535 5.7.8 Identifiants refusés" if mode == "refuse_auth" else "235 2.7.0 Bienvenue")
        elif m.startswith("MAIL FROM:"):
            notes.append("FROM:" + l[10:])
            dire("250 OK")
        elif m.startswith("RCPT TO:"):
            notes.append("TO:" + l[8:])
            dire("550 5.1.1 Boîte inconnue" if mode == "refuse_rcpt" else "250 OK")
        elif m == "DATA":
            dire("354 Fin par une ligne « . »")
            corps = []
            while True:
                d = f.readline().decode("utf-8", "replace")
                if d in (".\r\n", ".\n", ""):
                    break
                corps.append(d.rstrip("\r\n"))
            notes.append("DATA:\n" + "\n".join(corps))
            dire("250 2.0.0 Accepté")
        elif m == "QUIT":
            dire("221 Au revoir")
            break
        else:
            dire("500 Commande inconnue")
    with open(sortie, "a", encoding="utf-8") as s:
        s.write("\n".join(notes) + "\n=====\n")


def main():
    port, certificat, cle, sortie, mode = sys.argv[1:6]
    connexions = int(sys.argv[6]) if len(sys.argv) > 6 else 1
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certificat, cle)
    with socket.socket() as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", int(port)))
        srv.listen(1)
        print("prêt", flush=True)
        for _ in range(connexions):
            conn, _ = srv.accept()
            with ctx.wrap_socket(conn, server_side=True) as tls:
                servir(tls, sortie, mode)


if __name__ == "__main__":
    main()
