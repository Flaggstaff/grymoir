/* GrymoiR : le morse d'un son (grammaire 2.78, 1er novembre 2026) : « le morse de l'enregistrement du p ».
 * Un passage d'Écoutille (un WAV PCM 16 bits, mono, à toute fréquence d'échantillonnage) ; le texte décodé en capitales,
 * les mots séparés par une espace ; NULL s'il n'y a pas de morse (de la voix, du bruit, un fichier qui n'est pas un WAV
 * de ce genre). Le décodage s'adapte à la hauteur de la note (300 à 1500 Hz) et à la vitesse (de 5 à 50 mots par minute).
 */
#ifndef GRYM_MORSE_H
#define GRYM_MORSE_H

#include <stddef.h>

char *morse_decoder(const unsigned char *octets, size_t taille);   /* à libérer ; NULL : pas de morse */

/* Pour les essais : un WAV de morse fabriqué (16 kHz, mono, 16 bits) : le texte, à cette vitesse et cette note, avec un
 * bruit blanc de cette amplitude (0 à 1, pour une note d'amplitude 0,5). *taille reçoit sa taille. À libérer. */
unsigned char *morse_fabriquer(const char *texte, int mots_par_minute, int hertz, double bruit, size_t *taille);

#endif
