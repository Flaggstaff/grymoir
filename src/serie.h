/* Les ports série (grammaire 2.82, 3 novembre 2026) : le pilotage d'un poste de radio par son port COM virtuel (CAT de
 * Yaesu, Kenwood, Elecraft). Une question, une réponse, en texte ; rien de propre à une marque. */
#ifndef GRYM_SERIE_H
#define GRYM_SERIE_H

#include <stddef.h>

/* Les ports série de l'ordinateur, triés : /dev/cu.* sous macOS, /dev/ttyUSB* et /dev/ttyACM* sous Linux, COM1 à
 * COM256 sous Windows. Rend leur nombre ; *noms reçoit un tableau de textes alloués (à libérer avec serie_liberer). */
size_t serie_ports(char ***noms);
void serie_liberer(char **noms, size_t n);

/* Ouvre le port à la vitesse donnée (8 bits, sans parité, 2 bits d'arrêt, sans contrôle de flux, DTR et RTS baissés :
 * sur le second port d'un Yaesu, ils commandent l'émission), vide ce qui attend,
 * écrit la question, lit la réponse jusqu'à 100 ms de silence après le dernier octet reçu, ou 1 seconde sans rien ;
 * ferme le port. Rend la réponse (allouée, éventuellement vide), ou NULL et *erreur, un message en français alloué. */
char *serie_echanger(const char *port, long bauds, const char *question, char **erreur);

/* Les vitesses acceptées, en bauds : de 1200 à 115200. */
int serie_vitesse_connue(long bauds);

/* Les décodages de WSJT-X (grammaire 2.88) : au premier appel pour un port, une prise UDP s'ouvre sur 127.0.0.1:port et le
 * reste jusqu'à la fin du processus ; chaque appel lit sans attendre ce qui est arrivé depuis. Les messages « Status » (1)
 * donnent la fréquence et le mode ; chaque « Decode » (2) nouveau devient un objet JSON alloué : heure, mode, fréquence (Hz,
 * absente si inconnue), rapport (dB), décalage (Hz), écart (s), message, douteux. Rend leur nombre, *decodages les textes
 * (à libérer avec serie_liberer) ; (size_t)-1 et *erreur si la prise ne s'ouvre pas. */
size_t wsjtx_decodages(long port, char ***decodages, char **erreur);
/* Le même décodage, depuis un datagramme déjà reçu : NULL si ce n'en est pas un (les essais). */
char *wsjtx_lire_datagramme(const unsigned char *d, size_t n);

#endif
