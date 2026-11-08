/* GrymoiR : le chiffrement du trafic (TLS) et les certificats du cabinet, sur Mbed TLS 3.6 (vendor/mbedtls ; docs/v2.md,
 * § 12 ; 8 novembre 2026). Seul l'aiguilleur parle TLS, aux navigateurs du réseau ; les sessions restent en clair sur
 * 127.0.0.1, derrière lui.
 *
 * L'autorité du cabinet : une clé et un certificat créés une fois (P-256, ECDSA-SHA256, dix ans), que chaque poste installe
 * une fois comme autorité de confiance. Elle signe le certificat du serveur : ses noms et ses adresses, 800 jours (Apple
 * refuse au-delà de 825 jours un certificat de serveur émis après juillet 2019), l'usage « serverAuth ». Les clés se
 * rangent lisibles par leur seul propriétaire. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tls.h"
#include "texte.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/oid.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

struct TlsServeur {
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context cle;
    mbedtls_entropy_context entropie;
    mbedtls_ctr_drbg_context hasard;
};

struct TlsConnexion {
    mbedtls_ssl_context ssl;
    int fd;
};

static char *message_mbedtls(const char *quoi, int code) {
    char b[160];
    mbedtls_strerror(code, b, sizeof b);
    return grym_formater("%s : %s (-0x%04X).", quoi, b, (unsigned)-code);
}

static int preparer_hasard(mbedtls_entropy_context *e, mbedtls_ctr_drbg_context *h, char **erreur) {
    mbedtls_entropy_init(e);
    mbedtls_ctr_drbg_init(h);
    static const char perso[] = "grymoir-tls";
    int r = mbedtls_ctr_drbg_seed(h, mbedtls_entropy_func, e, (const unsigned char *)perso, sizeof perso - 1);
    if (r != 0) { *erreur = message_mbedtls("Aucun hasard du système", r); return 0; }
    psa_status_t s = psa_crypto_init();   /* TLS 1.3 passe par l'interface PSA, même sans MBEDTLS_USE_PSA_CRYPTO */
    if (s != PSA_SUCCESS) { *erreur = grym_formater("La cryptographie ne démarre pas (PSA %d).", (int)s); return 0; }
    return 1;
}

/* ---------- Les certificats ---------- */

static int ecrire_fichier(const char *chemin, const unsigned char *d, size_t n, int secret, char **erreur) {
    FILE *f = fopen(chemin, "wb");
    if (!f) { *erreur = grym_formater("« %s » ne s'écrit pas.", chemin); return 0; }
#ifndef _WIN32
    if (secret) fchmod(fileno(f), 0600);   /* une clé : son propriétaire seul la lit */
#else
    (void)secret;
#endif
    int ok = fwrite(d, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    if (!ok) *erreur = grym_formater("« %s » ne s'écrit pas en entier.", chemin);
    return ok;
}

static int fichier_existe(const char *chemin) {
    FILE *f = fopen(chemin, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* « 20261108153000 » : maintenant, plus des jours ; en UTC. */
static void horodatage(char *t, size_t n, long jours) {
    time_t s = time(NULL) + (time_t)jours * 86400;
    struct tm u;
#ifdef _WIN32
    gmtime_s(&u, &s);
#else
    gmtime_r(&s, &u);
#endif
    strftime(t, n, "%Y%m%d%H%M%S", &u);
}

static int nouvelle_cle(mbedtls_pk_context *k, mbedtls_ctr_drbg_context *h, char **erreur) {
    mbedtls_pk_init(k);
    int r = mbedtls_pk_setup(k, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (r == 0) r = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(*k), mbedtls_ctr_drbg_random, h);
    if (r != 0) { *erreur = message_mbedtls("La clé ne se crée pas", r); return 0; }
    return 1;
}

static int ecrire_cle(mbedtls_pk_context *k, const char *chemin, char **erreur) {
    unsigned char b[4096];
    int r = mbedtls_pk_write_key_pem(k, b, sizeof b);
    if (r != 0) { *erreur = message_mbedtls("La clé ne s'écrit pas", r); return 0; }
    const int ok = ecrire_fichier(chemin, b, strlen((char *)b), 1, erreur);
    mbedtls_platform_zeroize(b, sizeof b);
    return ok;
}

/* Écrit le certificat de sujet (clé k) signé par l'émetteur (clé ke) ; ca : une autorité. */
static int ecrire_certificat(const char *chemin, const char *sujet, const char *emetteur, mbedtls_pk_context *k,
                             mbedtls_pk_context *ke, int ca, long jours, const mbedtls_x509_san_list *noms,
                             mbedtls_ctr_drbg_context *h, char **erreur) {
    mbedtls_x509write_cert c;
    mbedtls_x509write_crt_init(&c);
    mbedtls_x509write_crt_set_version(&c, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&c, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&c, k);
    mbedtls_x509write_crt_set_issuer_key(&c, ke);
    unsigned char serie[16];
    int r = mbedtls_ctr_drbg_random(h, serie, sizeof serie);
    serie[0] &= 0x7F;   /* un numéro positif */
    char debut[16], fin[16];
    horodatage(debut, sizeof debut, -1);   /* un jour de marge pour une horloge en retard */
    horodatage(fin, sizeof fin, jours);
    if (r == 0) r = mbedtls_x509write_crt_set_serial_raw(&c, serie, sizeof serie);
    if (r == 0) r = mbedtls_x509write_crt_set_subject_name(&c, sujet);
    if (r == 0) r = mbedtls_x509write_crt_set_issuer_name(&c, emetteur);
    if (r == 0) r = mbedtls_x509write_crt_set_validity(&c, debut, fin);
    if (r == 0) r = mbedtls_x509write_crt_set_basic_constraints(&c, ca, ca ? 0 : -1);
    if (r == 0) r = mbedtls_x509write_crt_set_subject_key_identifier(&c);
    if (r == 0) r = mbedtls_x509write_crt_set_authority_key_identifier(&c);
    if (r == 0) r = mbedtls_x509write_crt_set_key_usage(&c, ca ? MBEDTLS_X509_KU_KEY_CERT_SIGN | MBEDTLS_X509_KU_CRL_SIGN
                                                               : MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    if (r == 0 && !ca) {
        static const char oid_serveur[] = MBEDTLS_OID_SERVER_AUTH;
        mbedtls_asn1_sequence u;
        memset(&u, 0, sizeof u);
        u.buf.tag = MBEDTLS_ASN1_OID;
        u.buf.p = (unsigned char *)oid_serveur;
        u.buf.len = sizeof oid_serveur - 1;
        r = mbedtls_x509write_crt_set_ext_key_usage(&c, &u);
    }
    if (r == 0 && noms) r = mbedtls_x509write_crt_set_subject_alternative_name(&c, noms);
    unsigned char b[4096];
    if (r == 0) r = mbedtls_x509write_crt_pem(&c, b, sizeof b, mbedtls_ctr_drbg_random, h);
    mbedtls_x509write_crt_free(&c);
    if (r != 0) { *erreur = message_mbedtls("Le certificat ne se crée pas", r); return 0; }
    return ecrire_fichier(chemin, b, strlen((char *)b), 0, erreur);
}

/* Le nom distinctif « CN=…,O=GrymoiR » : les virgules, signes égal et barres obliques du nom deviennent des espaces. */
static char *nom_distinctif(const char *cn) {
    char *n = grym_dupliquer(cn);
    for (char *p = n; *p; p++)
        if (*p == ',' || *p == '=' || *p == '\\' || *p == '+' || *p == ';' || *p == '"' || *p == '<' || *p == '>') *p = ' ';
    char *d = grym_formater("CN=%s,O=GrymoiR", n);
    free(n);
    return d;
}

int tls_preparer_certificats(const char *dossier, const char *nom, const char *const *noms, size_t nb, char **erreur) {
    *erreur = NULL;
    mbedtls_entropy_context e;
    mbedtls_ctr_drbg_context h;
    if (!preparer_hasard(&e, &h, erreur)) return 0;
    char *ca_crt = grym_formater("%s/autorite.crt", dossier), *ca_cle = grym_formater("%s/autorite.cle", dossier);
    char *sv_crt = grym_formater("%s/serveur.crt", dossier), *sv_cle = grym_formater("%s/serveur.cle", dossier);
    char *cn_ca = grym_formater("Autorité GrymoiR de %s", nom);
    char *dn_ca = nom_distinctif(cn_ca);
    char *dn_sv = nom_distinctif(nb ? noms[0] : nom);
    mbedtls_pk_context kca, ksv;
    mbedtls_pk_init(&kca);
    mbedtls_pk_init(&ksv);
    mbedtls_x509_san_list *liste = NULL;
    unsigned char (*ips)[16] = nb ? grym_allouer(nb * sizeof *ips) : NULL;
    int ok = 1;
    if (!fichier_existe(ca_crt) || !fichier_existe(ca_cle)) {   /* l'autorité, une fois : elle se garde ensuite */
        ok = nouvelle_cle(&kca, &h, erreur) && ecrire_cle(&kca, ca_cle, erreur)
          && ecrire_certificat(ca_crt, dn_ca, dn_ca, &kca, &kca, 1, 3650, NULL, &h, erreur);
    } else {
        int r = mbedtls_pk_parse_keyfile(&kca, ca_cle, NULL, mbedtls_ctr_drbg_random, &h);
        if (r != 0) { *erreur = message_mbedtls("La clé de l'autorité ne se lit pas", r); ok = 0; }
    }
    /* le serveur : ses noms (DNS) et ses adresses (IPv4, IPv6), dans l'ordre donné */
    for (size_t k = nb; ok && k > 0; k--) {
        mbedtls_x509_san_list *n = grym_allouer(sizeof *n);
        memset(n, 0, sizeof *n);
        const char *x = noms[k - 1];
        if (inet_pton(AF_INET, x, ips[k - 1]) == 1) {
            n->node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
            n->node.san.unstructured_name.p = ips[k - 1];
            n->node.san.unstructured_name.len = 4;
        } else if (inet_pton(AF_INET6, x, ips[k - 1]) == 1) {
            n->node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
            n->node.san.unstructured_name.p = ips[k - 1];
            n->node.san.unstructured_name.len = 16;
        } else {
            n->node.type = MBEDTLS_X509_SAN_DNS_NAME;
            n->node.san.unstructured_name.p = (unsigned char *)x;
            n->node.san.unstructured_name.len = strlen(x);
        }
        n->next = liste;
        liste = n;
    }
    if (ok) ok = nouvelle_cle(&ksv, &h, erreur) && ecrire_cle(&ksv, sv_cle, erreur)
              && ecrire_certificat(sv_crt, dn_sv, dn_ca, &ksv, &kca, 0, 800, liste, &h, erreur);
    while (liste) { mbedtls_x509_san_list *s = liste->next; free(liste); liste = s; }
    free(ips);
    mbedtls_pk_free(&kca);
    mbedtls_pk_free(&ksv);
    mbedtls_ctr_drbg_free(&h);
    mbedtls_entropy_free(&e);
    free(ca_crt); free(ca_cle); free(sv_crt); free(sv_cle); free(cn_ca); free(dn_ca); free(dn_sv);
    return ok;
}

/* ---------- Le serveur ---------- */

TlsServeur *tls_serveur_creer(const char *dossier, char **erreur) {
    *erreur = NULL;
    TlsServeur *s = grym_allouer(sizeof *s);
    memset(s, 0, sizeof *s);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->cert);
    mbedtls_pk_init(&s->cle);
    if (!preparer_hasard(&s->entropie, &s->hasard, erreur)) { tls_serveur_detruire(s); return NULL; }
    char *crt = grym_formater("%s/serveur.crt", dossier), *cle = grym_formater("%s/serveur.cle", dossier);
    int r = mbedtls_x509_crt_parse_file(&s->cert, crt);
    if (r != 0) *erreur = grym_formater("Le certificat « %s » ne se lit pas : crée-le avec « grym certificat ».", crt);
    if (r == 0) {
        r = mbedtls_pk_parse_keyfile(&s->cle, cle, NULL, mbedtls_ctr_drbg_random, &s->hasard);
        if (r != 0) *erreur = message_mbedtls("La clé du serveur ne se lit pas", r);
    }
    if (r == 0) {
        r = mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
        if (r == 0) {
            mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->hasard);
            /* TLS 1.2 au moins : les versions plus anciennes sont cassées */
            mbedtls_ssl_conf_min_tls_version(&s->conf, MBEDTLS_SSL_VERSION_TLS1_2);
            r = mbedtls_ssl_conf_own_cert(&s->conf, &s->cert, &s->cle);
        }
        if (r != 0) *erreur = message_mbedtls("Le chiffrement ne se prépare pas", r);
    }
    free(crt);
    free(cle);
    if (r != 0) { tls_serveur_detruire(s); return NULL; }
    return s;
}

void tls_serveur_detruire(TlsServeur *s) {
    if (!s) return;
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_x509_crt_free(&s->cert);
    mbedtls_pk_free(&s->cle);
    mbedtls_ctr_drbg_free(&s->hasard);
    mbedtls_entropy_free(&s->entropie);
    free(s);
}

static int envoyer_fd(void *c, const unsigned char *b, size_t n) {
    const int fd = *(int *)c;
#ifdef _WIN32
    int r = send((SOCKET)fd, (const char *)b, (int)n, 0);
#else
    ssize_t r = write(fd, b, n);
    if (r < 0 && (errno == EINTR)) return MBEDTLS_ERR_SSL_WANT_WRITE;
#endif
    return r < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : (int)r;
}

static int recevoir_fd(void *c, unsigned char *b, size_t n) {
    const int fd = *(int *)c;
#ifdef _WIN32
    int r = recv((SOCKET)fd, (char *)b, (int)n, 0);
#else
    ssize_t r = read(fd, b, n);
    if (r < 0 && errno == EINTR) return MBEDTLS_ERR_SSL_WANT_READ;
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return MBEDTLS_ERR_SSL_TIMEOUT;   /* le délai de la prise */
#endif
    return r < 0 ? MBEDTLS_ERR_NET_RECV_FAILED : (int)r;
}

TlsConnexion *tls_accepter(TlsServeur *s, int fd, char **erreur) {
    *erreur = NULL;
    TlsConnexion *c = grym_allouer(sizeof *c);
    memset(c, 0, sizeof *c);
    c->fd = fd;
    mbedtls_ssl_init(&c->ssl);
    int r = mbedtls_ssl_setup(&c->ssl, &s->conf);
    if (r == 0) {
        mbedtls_ssl_set_bio(&c->ssl, &c->fd, envoyer_fd, recevoir_fd, NULL);
        do r = mbedtls_ssl_handshake(&c->ssl);
        while (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE);
    }
    if (r != 0) {
        *erreur = message_mbedtls("Poignée de main TLS refusée", r);
        mbedtls_ssl_free(&c->ssl);
        free(c);
        return NULL;
    }
    return c;
}

long tls_lire(TlsConnexion *c, void *b, size_t n) {
    for (;;) {
        int r = mbedtls_ssl_read(&c->ssl, b, n);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        /* TLS 1.3 : un ticket de session arrive comme un message ; il ne porte pas de données */
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        return r < 0 ? -1 : r;
    }
}

int tls_ecrire(TlsConnexion *c, const void *b, size_t n) {
    const unsigned char *p = b;
    while (n) {
        int r = mbedtls_ssl_write(&c->ssl, p, n);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (r <= 0) return 0;
        p += r;
        n -= (size_t)r;
    }
    return 1;
}

void tls_fermer(TlsConnexion *c, int prevenir) {
    if (!c) return;
    if (prevenir) mbedtls_ssl_close_notify(&c->ssl);
    mbedtls_ssl_free(&c->ssl);
    free(c);
}
