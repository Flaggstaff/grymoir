# Mettre à jour Praxis chez un cabinet

Les versions de Praxis sont toutes dans `projets/Praxis` : `Nicolas`, `Lorianne`, `Praxis` (neutre, le point de départ d'un nouveau
cabinet) et `Test de charge` (l'année 2025 inventée, fabriquée au premier lancement). Elles partagent leur code : une correction se
fait dans une version, puis `outils/praxis-propager <version>` la recopie dans les autres ; `make test` vérifie qu'elles ne
divergent pas (`tests/praxis-versions`).

Marche à suivre, rédigée le 23 octobre 2026 pour la visite chez Nicolas. Elle vaut pour tout cabinet.

## Avant la visite, sur ton Mac

1. Dépôt à jour : `cd ~/Downloads/grymoir && git pull --ff-only && make -s test`. Tous les essais passent.
2. Application du cabinet : dans l'atelier, ouvrir `projets/Praxis/Nicolas` et fabriquer l'application. Elle s'appelle « Praxis »
   (`application, nom : Praxis` dans son `projet.grymatelier`) : c'est ce nom qui désigne le dossier de sa base. Un autre nom, et
   l'application ne retrouverait pas la base de Nicolas.
3. Emporter l'application fabriquée et une clé USB vide.

## Chez le cabinet, dans cet ordre

1. **Fermer Praxis.** Une base ouverte ne se copie pas proprement.
2. **Sauvegarder.** Copier tout le dossier de données de Praxis sur la clé USB. Sous macOS : `~/Library/Application Support/Praxis`
   (dans le Finder : Aller, Aller au dossier). Garder cette copie intacte jusqu'à ce que tout marche.
3. **Essayer la mise à jour sur la copie**, sur ton Mac : `outils/essai-mise-a-jour /Volumes/<clé>/Praxis`. L'outil demande le
   mot de passe de la base et dit ce que le prochain lancement changerait, sans rien écrire.
   - « la mise à jour passe » : continuer.
   - « REFUSÉ » : s'arrêter là, ne rien installer, garder l'ancienne application ; m'envoyer le message.
4. **Installer** l'application fabriquée à la place de l'ancienne.
5. **Lancer et contrôler** avec le cabinet : le mot de passe ouvre la base ; l'accueil affiche les séances du jour ; la page d'un
   patient montre ses séances ; la Comptabilité de l'année tombe juste ; une quittance s'ouvre.
6. **Interrupteurs** : Configuration, Le cabinet. Chez Nicolas (depuis le 28 octobre 2026, `départ.grym`) : « Choix d'agenda »
   allumé, agenda « Calendly », « Facturation aux assurances » éteint, préfixe SMS « NS ». Ne pas éteindre le choix d'agenda :
   l'import des réservations Calendly et les rappels SMS passent par lui.

## Si quelque chose ne va pas

Remettre l'ancienne application, puis recopier le dossier de la clé USB à la place de `~/Library/Application Support/Praxis`.
La base retrouve exactement son état d'avant la visite.
