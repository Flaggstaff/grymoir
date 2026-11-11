# Essais sur place (6 novembre 2026)

Ce que les essais automatiques ne peuvent pas vérifier : une vraie banque, de vraies radios. Chaque essai dit quoi faire, ce
qu'on doit voir, et quoi rapporter s'il échoue.

## Praxis chez Nicolas : la livraison du vendredi

Avant de partir :

1. Copier la base du cabinet (Praxis.grymd et Praxis.grymd.clés) sur une clé, puis
   `outils/essai-mise-a-jour <dossier de la copie> Nicolas` : il doit répondre que la base est à jour ou ne recevoir que des
   tables ajoutées (les virements reçus, au lot 1). Un refus : ne pas installer, rapporter le message.
2. Fabriquer l'application avec l'atelier, version 2026.11.06 (Configuration, en bas).

Sur place :

1. Faire une copie de la base depuis Configuration, Sauvegardes, avant d'installer.
2. Installer, ouvrir. L'accueil ne doit signaler aucun échec de sauvegarde. Le lendemain, la copie de la veille est vérifiée
   à l'ouverture : un message, s'il y en a un, dit laquelle ne s'ouvre pas.
3. **QR-facture** : faire une facture d'essai, l'aperçu doit montrer une « Référence » commençant par RF. La payer d'un franc
   depuis le natel de Nicolas en scannant le code n'est utile que si la banque transmet la référence ; on le saura à l'étape 4.
4. **Virements reçus** : dans l'e-banking du cabinet, chercher l'export « camt.054 » (avis de crédit) ou « camt.053 »
   (relevé de compte), en XML. Le lire dans Facturation, Virements reçus.
   - Attendu : les virements des patients apparaissent, rapprochés ou à vérifier avec une raison ; les débits n'apparaissent pas.
   - Relire le même fichier n'ajoute rien.
   - Si la banque ne donne qu'un fichier ZIP : Praxis ne lit pas les ZIP ; l'ouvrir d'abord et lire le XML qu'il contient.
   - Un refus (« Ce fichier n'est ni un avis de crédit… », « XML mal formé… ») : rapporter le message exact et le nom de la
     banque, sans le fichier (il contient des noms de patients).
   - Je ne sais pas si l'e-banking du cabinet propose ces exports, ni à quel prix : c'est la première question à poser.
5. **Inscription** : créer un patient avec le natel d'un patient existant : Praxis doit le signaler ; « Autre personne »
   permet de passer.
6. **Historique et journal** : changer le natel d'un patient d'essai ; son « Historique » le montre ; le « Journal » de la
   Configuration aussi, signé Direction ou Équipe si la page de garde est en service.
7. **Attestation annuelle** : sur un patient qui a payé cette année, l'attestation liste ses séances payées et le total.

## Écoutille chez Michel

Sur un poste branché en USB, en veille :

1. **Dernière minute** : laisser tourner une minute, cliquer « Réécouter 1 min » (le haut-parleur rejoue), puis
   « Garder 1 min » : un passage d'environ 60 s apparaît, transcrit.
2. **Débranchement** : débrancher le câble USB du poste. Attendu : l'entrée reste affichée « (débranchée) », aucune autre radio
   ne prend sa place. Rebrancher : « En écoute. » revient seul en quelques secondes, sans toucher à rien. À rapporter si ce
   n'est pas le cas : le système (macOS ou Windows) et ce que dit la ligne d'état.
3. **Fichier son** : dans la liste des entrées, « Un fichier son… », choisir un WAV enregistré (du morse, une phrase) : il se
   lit au rythme réel et ses passages apparaissent comme ceux d'un poste.
4. **Langue** : sur un poste réglé « traduction voulue », écouter un QSO en anglais ou en allemand. Attendu : la transcription
   est dans la langue parlée et la traduction en français suit ; un QSO en français n'est pas envoyé à DeepL (le quota du
   mois, bouton « Quota DeepL », ne bouge pas). Les QSO très courts ou faits d'indicatifs partent quand même chez DeepL :
   la langue n'y est pas reconnaissable.
5. **WSJT-X** : dans WSJT-X, Réglages, Rapports, « UDP Server » : 127.0.0.1, port 2237, cases « Accept UDP requests »
   sans importance. Décoder quelques minutes de FT8, puis dans Écoutille, bouton « WSJT-X » : les décodages apparaissent,
   avec l'heure UTC, le mode, la bande et le message. Si l'avis dit que le port ne s'ouvre pas : JTAlert ou GridTracker
   l'écoutent déjà ; les fermer, ou régler l'un des deux sur un autre port (Écoutille lit le 2237). À rapporter si rien
   n'arrive : la version de WSJT-X (le format UDP de WSJT-X n'a été vérifié ici que contre des messages fabriqués).
6. **Parole minimale** (11 novembre 2026) : dans « Réglages », « Parole minimale » à 3 mots. Laisser un poste en veille sur
   une fréquence sans trafic, squelch ouvert. Attendu : les passages de souffle restent dans la veille avec leur son, sans
   texte ; un vrai QSO garde sa parole. À rapporter : une parole utile perdue (son nombre de mots), ou un texte inventé
   de trois mots et plus qui passe encore (le recopier : il dira s'il faut un autre filtre que la longueur).
7. **Pays** : ouvrir un passage qui a des indicatifs. Attendu : une ligne « Pays : HB9ABC Suisse, F4XYZ France ». C'est
   l'attribution de l'UIT, pas l'entité DXCC : la Guadeloupe sort « France ». À rapporter : un pays faux, avec l'indicatif.
8. **Minuterie** (à faire d'abord chez Flaggstaff, sans radio) : WSJT-X lancé, ouvrir « WSJT-X » dans Écoutille et ne rien
   toucher. Attendu : les décodages arrivent seuls, l'avis change toutes les 10 secondes environ. C'est le premier essai réel
   de la minuterie (grammaire 3.03) dans l'atelier. À rapporter : rien ne bouge sans « Relever », ou l'écran se fige.
9. **Remettre l'accord** : sur un Yaesu, WSJT-X fermé (il tient le port), ouvrir un passage ancien qui a un accord, cliquer
   « Remettre l'accord », confirmer. Attendu : le poste revient sur la fréquence et le mode du passage, sans émettre. Seul le
   VFO A change. À rapporter : le message, et le menu CAT RATE du poste s'il ne répond pas.
10. **Courriel d'alerte** : dans « Réglages », un destinataire, l'adresse, le serveur (port 465), le compte et la clé de la
    messagerie, puis une alerte sur un mot entendu souvent (« CQ »). Attendu : un courriel au premier passage qui allume
    l'alerte, aucun autre dans les 10 minutes qui suivent, même si l'alerte s'allume encore à l'écran. À rapporter : le
    message « Courriel d'alerte non parti », ou un courriel en double dans les 10 minutes.

## Les deux applications : un plantage

Si l'application se ferme brusquement, le lancement suivant le dit et donne le chemin de `Plantages.txt` (dans Documents,
le dossier de l'application). Ce fichier ne contient aucune donnée de patient ni de passage : il peut être envoyé tel quel.
