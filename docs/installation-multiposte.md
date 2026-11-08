# Installer Praxis sur plusieurs postes d'un cabinet

Version 1.0, rédigée le 8 novembre 2026. Pour un cabinet équipé de Mac, qui veut ouvrir Praxis sur plusieurs postes.

Par défaut, Praxis tourne sur **un seul poste** : on l'ouvre, on travaille, rien d'autre à régler. Ce guide ne sert que si le
cabinet choisit le **multiposte**. Ce choix active aussi la Direction et l'Équipe (deux mots de passe) et le Personnel
(les salaires).

Le principe : un Mac du cabinet, le **poste principal**, garde la base et la sert aux autres par le réseau du cabinet,
chiffrée. Les autres postes ouvrent simplement une adresse dans Safari. Personne d'autre que les postes du cabinet ne voit
quoi que ce soit, et rien ne passe en clair.

## Ce qu'il faut

- Le poste principal : le Mac où Praxis est déjà installée et où vit la base. Il reste allumé pendant les heures de travail.
- `grym`, construit sur ce Mac depuis le dépôt de GrymoiR (`make` dans `~/Downloads/grymoir`). Les commandes ci-dessous
  l'appellent `~/Downloads/grymoir/grym`.
- Les autres postes : des Mac (ou toute machine avec un navigateur récent), sur le même réseau que le poste principal.

## 1. Allumer le multiposte (poste principal, une fois)

1. Ouvrir Praxis, entrer en Direction.
2. Configuration, fiche du cabinet : allumer **Multiposte**.
3. Configuration, **Accès** : poser le mot de passe de la Direction, puis celui de l'Équipe.

Sans ces deux étapes, Praxis refuse d'être servie par le réseau, avec un message qui dit quoi faire.

## 2. Donner un nom au poste principal (une fois)

Les autres postes le trouveront par ce nom. Sur le poste principal : menu Pomme, Réglages Système, Général, Partage ; en
bas, « Nom d'hôte local », Modifier. Choisir un nom court, sans espace ni accent, par exemple `praxis` : le Mac s'appelle
alors `praxis.local` sur le réseau du cabinet.

Source : Apple, « Modifier le nom ou le nom d'hôte local de votre ordinateur sur Mac »,
https://support.apple.com/fr-afri/guide/mac-help/mchlp2322 (consulté le 8 novembre 2026). Apple y précise que le nom
d'hôte local se termine par `.local` et ne distingue pas les majuscules.

## 3. Créer les certificats du cabinet (une fois, puis tous les deux ans)

Dans le Terminal du poste principal :

```
~/Downloads/grymoir/grym certificat ~/Cabinet "Cabinet Seydoux" praxis.local
```

Quatre fichiers apparaissent dans `~/Cabinet` :

| Fichier | Ce que c'est | Où il va |
|---|---|---|
| `autorite.crt` | l'autorité du cabinet | sur **chaque poste**, comme autorité de confiance (étape 4) |
| `autorite.cle` | sa clé | **hors du poste principal**, sur une clé USB rangée au cabinet |
| `serveur.crt`, `serveur.cle` | le certificat du poste principal | restent dans `~/Cabinet` |

Le certificat du serveur vaut 800 jours. Avant son échéance, ou si le nom change : remettre `autorite.cle` dans
`~/Cabinet`, relancer la même commande (l'autorité ne change pas, les postes n'ont rien à refaire), puis ranger de nouveau
la clé.

## 4. Faire confiance à l'autorité, sur chaque poste (une fois par poste)

Copier `autorite.crt` sur le poste (clé USB, AirDrop), puis :

1. Double-cliquer le fichier : Trousseaux d'accès l'ajoute. Choisir le trousseau « Session ».
2. Dans Trousseaux d'accès, double-cliquer le certificat « Autorité GrymoiR de Cabinet Seydoux ».
3. Déplier « Se fier », puis choisir **Toujours approuver** pour « Lors de l'utilisation de ce certificat ».
4. Fermer la fenêtre ; macOS demande le mot de passe du Mac.

Sources : Apple, « Modifier les réglages de confiance d'un certificat dans Trousseaux d'accès sur Mac »,
https://support.apple.com/fr-afri/guide/keychain-access/kyca11871/mac ; « Modifier des politiques de confiance concernant
les certificats sur Mac », https://support.apple.com/fr-afri/guide/keychain-access/mchlp2824/mac (consultés le
8 novembre 2026).

Ne jamais accepter une alerte « Cette connexion n'est pas privée » en cliquant « Visiter quand même » : si elle apparaît,
c'est que l'étape 4 manque sur ce poste, ou que quelqu'un se fait passer pour le poste principal.

## 5. Ouvrir le service (poste principal, chaque matin)

Fermer d'abord l'application Praxis sur le poste principal, puis, dans le Terminal :

```
~/Downloads/grymoir/grym servir "/Applications/Praxis-N.app/Contents/Resources/Programme/Praxis.grym" \
    --reseau ~/Cabinet \
    --base "$HOME/Library/Application Support/Praxis-N/Praxis.grymd" \
    --ecriture "$HOME/Documents/Praxis-N"
```

(`Praxis-N` : le nom de l'application du cabinet, celui qu'affiche le Finder ; `Praxis-L` chez Lorianne.)

- `grym` demande **une fois** le mot de passe des données de Praxis, celui que l'application demande à chaque lancement.
  Les autres postes n'ont pas à le connaître.
- `--base` et `--ecriture` font servir **la base de l'application**, pas une copie : ce que les postes font se retrouve
  dans Praxis, et inversement.
- Le Terminal affiche l'adresse : `https://praxis.local:8443/?jeton=…`. C'est elle qu'on ouvre sur chaque poste (un
  marque-page suffit, une fois). Elle reste la même tant que le service tourne ; elle change à chaque relance.
- macOS peut demander si `grym` accepte les connexions entrantes : répondre Autoriser.
- Le soir : Ctrl+C dans le Terminal arrête le service, et toutes les sessions avec lui.

Sur un poste, Safari ouvre l'adresse ; Praxis demande « Qui est là ? » : le mot de passe de la Direction ou celui de
l'Équipe. Si deux personnes changent le même champ du même patient en même temps, la seconde reçoit un message et
recommence ; rien ne s'écrase (grammaire, § 16.6).

## 6. Depuis l'extérieur

Jamais directement par internet : le service ne s'y montre pas, et ne doit pas s'y montrer. Pour travailler hors du
cabinet, un VPN (WireGuard, Tailscale ou celui du routeur) place le portable sur le réseau du cabinet ; il ouvre alors la
même adresse. Le choix et le réglage du VPN dépendent du routeur du cabinet : ce guide ne les couvre pas encore.

## Ce qui n'est pas encore fait

- **Le démarrage automatique** au matin. Il demanderait de ranger le mot de passe des données sur le disque, en clair ou
  presque : pas avant une solution sûre (le trousseau du Mac).
- **L'adresse stable.** Le jeton change à chaque relance du service : il faut rouvrir l'adresse affichée.
- **Le poste principal comme poste de travail** pendant le service : par Safari, sur la même adresse, comme les autres.
  L'application et le service ouverts en même temps sur la même base n'ont pas encore été éprouvés sur un Mac.
- **Windows** : le service ne tourne pas encore sous Windows ; un poste Windows peut toutefois s'y connecter par son
  navigateur, après avoir installé l'autorité (magasin de certificats de Windows).

Le détail technique : `docs/v2.md`, § 11 (les sessions) et § 12 (le réseau).
