/* GrymoiR : le contrat d'une bibliothèque native (grammaire, § 21.10, 3.14).
 *
 * Une bibliothèque GrymoiR peut écrire un calcul en C :
 *
 *     Remarque : bibliothèques/Signal/bibliothèque.grym
 *     Cette bibliothèque est « Signal », version 1.
 *     Le niveau d'un signal et d'un plancher vaut la fonction C « signal_niveau ».
 *
 * Le code se compile, à part, en une bibliothèque partagée posée dans le dossier de la bibliothèque :
 * « natif.dylib » (macOS), « natif.so » (Linux), « natif.dll » (Windows). La machine la charge par le système
 * (dlopen, LoadLibrary) au premier appel, puis la garde jusqu'à la fin du programme.
 *
 * Ce fichier est tout le contrat : la bibliothèque n'inclut rien d'autre de GrymoiR et ne voit aucune de ses
 * structures. Elle exporte « grym_natif_version », qui rend GRYM_NATIF_VERSION, et une fonction par calcul, du type
 * grym_fonction, au nom écrit dans le calcul.
 *
 * Les valeurs passent en texte, jamais en pointeur vers la machine :
 *   - un nombre est un décimal exact écrit en chiffres, point décimal, signe moins éventuel : « -12.35 », « 0.10 »
 *     (ses décimales écrites sont gardées : 0,10 arrive « 0.10 ») ; jamais d'exposant. La fonction convertit si elle veut un double ; elle rend un nombre de la même façon ;
 *   - un texte est de l'UTF-8 terminé par un zéro ;
 *   - vrai ou faux est l'entier vrai, 1 ou 0 ;
 *   - absent n'a pas de valeur.
 * Les arguments, et leurs textes, appartiennent à la machine : valables pendant l'appel seulement. La fonction rend sa
 * valeur en appelant exactement une fois l'un des rappels de grym_resultat, qui copie ce qu'on lui donne ; sans
 * rappel, l'appel échoue.
 *
 * Une fonction C est un calcul (§ 9.4) : mêmes arguments, même résultat ; elle n'écrit ni fichier ni réseau, ne garde
 * aucun état d'un appel à l'autre, et rend la main. La machine ne peut pas le vérifier : la bibliothèque le promet.
 * Un plantage de la fonction est un plantage du programme. */
#ifndef GRYM_NATIF_H
#define GRYM_NATIF_H

#define GRYM_NATIF_VERSION 1

#ifdef _WIN32
#define GRYM_EXPORTE __declspec(dllexport)
#else
#define GRYM_EXPORTE __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { GRYM_ABSENT = 0, GRYM_NOMBRE = 1, GRYM_TEXTE = 2, GRYM_VERITE = 3 } grym_sorte;

typedef struct {
    grym_sorte sorte;
    const char *texte;   /* GRYM_NOMBRE : « -12.35 » ; GRYM_TEXTE : de l'UTF-8 ; sinon NULL */
    int vrai;            /* GRYM_VERITE : 1 ou 0 ; sinon 0 */
} grym_valeur;

typedef struct grym_resultat grym_resultat;
struct grym_resultat {
    void *machine;   /* à la machine : la fonction n'y touche pas */
    void (*nombre)(grym_resultat *r, const char *decimal);   /* « 12.5 », « -0.25 » */
    void (*texte)(grym_resultat *r, const char *utf8);
    void (*verite)(grym_resultat *r, int vrai);
    void (*absent)(grym_resultat *r);
    void (*echec)(grym_resultat *r, const char *message);    /* l'appel échoue, avec ce message (sorte « natif ») */
};

typedef void (*grym_fonction)(int nb, const grym_valeur *arguments, grym_resultat *r);

#ifdef __cplusplus
}
#endif

#endif
