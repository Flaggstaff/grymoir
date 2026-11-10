/* GrymoiR : le chargement des bibliothèques natives par le système (grammaire, § 21.10, 3.14). dlopen sous macOS et
 * Linux, LoadLibraryW sous Windows ; le reste (le contrat, les valeurs) est dans vm.c et grym_natif.h. */
#ifndef _WIN32
#define _XOPEN_SOURCE 700   /* dlopen (POSIX 2008) */
#endif

#include "natif.h"
#include "texte.h"

#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#elif !defined(__wasi__)
#include <dlfcn.h>
#endif

const char *natif_extension(void) {
#if defined(_WIN32)
    return "dll";
#elif defined(__APPLE__)
    return "dylib";
#else
    return "so";
#endif
}

#if defined(__wasi__)
/* WebAssembly (l'application pour apprendre, 9 novembre 2026) : une page web ne charge aucune bibliothèque native. */
void *natif_ouvrir(const char *chemin, char **erreur) {
    *erreur = grym_formater("« %s » ne se charge pas ici : une page web ne charge aucune bibliothèque native.", chemin);
    return NULL;
}

void *natif_symbole(void *poignee, const char *nom) {
    (void)poignee; (void)nom;
    return NULL;
}

void natif_fermer(void *poignee) { (void)poignee; }

#elif defined(_WIN32)
void *natif_ouvrir(const char *chemin, char **erreur) {
    *erreur = NULL;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, chemin, -1, NULL, 0);
    if (n <= 0) { *erreur = grym_dupliquer("chemin illisible"); return NULL; }
    wchar_t *w = grym_allouer((size_t)n * sizeof *w);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, chemin, -1, w, n);
    /* ses propres dépendances se cherchent d'abord dans son dossier */
    HMODULE h = LoadLibraryExW(w, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    free(w);
    if (!h) *erreur = grym_formater("erreur %lu de Windows", (unsigned long)GetLastError());
    return (void *)h;
}

void *natif_symbole(void *poignee, const char *nom) {
    FARPROC p = GetProcAddress((HMODULE)poignee, nom);
    void *v;
    memcpy(&v, &p, sizeof v);   /* un pointeur de fonction en pointeur d'objet : permis par Windows, pas par le C99 */
    return v;
}

void natif_fermer(void *poignee) {
    if (poignee) FreeLibrary((HMODULE)poignee);
}
#else
void *natif_ouvrir(const char *chemin, char **erreur) {
    *erreur = NULL;
    void *h = dlopen(chemin, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char *e = dlerror();
        *erreur = grym_dupliquer(e ? e : "raison inconnue");
    }
    return h;
}

void *natif_symbole(void *poignee, const char *nom) {
    dlerror();
    return dlsym(poignee, nom);
}

void natif_fermer(void *poignee) {
    if (poignee) dlclose(poignee);
}
#endif
