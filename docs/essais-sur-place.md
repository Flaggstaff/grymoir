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
