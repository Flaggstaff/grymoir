# Apprendre GrymoiR dans le navigateur

Créé le 9 novembre 2026. La page `docs/site/apprendre.html` enseigne GrymoiR avec le vrai moteur : le cœur de `grym`
(lexeur, analyse, compilateur, machine, SQLite3 Multiple Ciphers) compilé en WebAssembly, embarqué compressé dans un
seul fichier HTML qui s'ouvre sans serveur (`file://`) et se publie tel quel à côté de `docs/site/grymoir.html`.

## Ce qu'il y a ici

| Fichier | Rôle |
|---------|------|
| `moteur.c` | le point d'entrée du moteur dans la page : lancer, mettre en forme, colorer |
| `moteur.js` | la couche WASI minimale (pas de fichiers, une horloge, `crypto.getRandomValues`) et l'appel du moteur ; sert dans la page et dans Node |
| `page.html` | le gabarit de la page : sommaire, leçon, établi (éditeur coloré, réponses, sortie, verdict) |
| `bienvenue.html` | la première leçon |
| `exercices.md` | les exercices : énoncé, départ, solution, sortie attendue |
| `assembler.mjs` | fabrique la page ; exécute chaque chapitre et chaque solution, et refuse d'écrire la page si une sortie annoncée manque |
| `essayer.mjs` | éprouve la page publiée, sans wasi-sdk : `tests/apprendre`, joué par `make test` quand Node est là |
| `construire` | compile le moteur, puis appelle `assembler.mjs` |

## Reconstruire la page

Après un changement du langage, du guide (`docs/guide.md`, `docs/guide/*.grym`) ou des exercices. `tests/apprendre`
avertit quand la page est en retard sur le guide ou sur la grammaire.

1. Récupérer wasi-sdk (version 25 éprouvée), sur https://github.com/WebAssembly/wasi-sdk/releases : l'archive de ton
   système, par exemple `wasi-sdk-25.0-arm64-macos.tar.gz` pour un Mac à puce Apple. La décompresser où tu veux.
2. Depuis la racine du dépôt, avec Node installé :

   ```
   WASI_SDK=/chemin/vers/wasi-sdk-25.0-arm64-macos sh apprendre/construire
   ```

SQLite se compile une fois (une minute environ) dans `apprendre/.sqlite3.wasm.o`, ignoré par Git comme `grym.wasm`.

## Les choix

- **Le cœur sans changement de comportement** : les seules adaptations sont derrière `__wasi__` (ports série,
  WSJT-X, commande externe et bibliothèques natives refusent avec un message ; la source aléatoire passe par
  `getentropy`). SQLite3MC prévoit WebAssembly derrière `__WASM__` : son code n'est pas touché.
- **Une leçon = le programme complet de son chapitre**, celui que `tests/test_guide` exécute : le guide présente les
  déclarations au fil du texte, le programme les range en tête, et aucun découpage ne suit les deux ordres. L'éditeur
  surligne les lignes que la section présente.
- **Les réponses aux questions s'écrivent d'avance** : la page ne peut pas suspendre le moteur en attendant le clavier
  sans les en-têtes d'isolation (COOP, COEP) que ni `file://` ni un hébergement ordinaire n'envoient.
- **Deux Workers** : l'un exécute (arrêté après 5 secondes, puis relancé), l'autre colore par le vrai lexeur, avec le
  partage des mots de l'atelier (`src/atelier/coloration.cpp`, vérifié par `tests/apprendre`).

## Limites

Pas d'écrans ni de documents (chapitre 11), un seul fichier (chapitre 12), une base en mémoire vidée à chaque
lancement, `aujourd'hui` en temps universel (WASI ne connaît pas les fuseaux). Navigateurs : il faut
`DecompressionStream` (Chrome et Edge 80, Firefox 113, Safari 16.4 et après, d'après
https://caniuse.com/mdn-api_decompressionstream_decompressionstream, consulté le 9 novembre 2026) ; éprouvée dans Chromium seulement au
9 novembre 2026.
