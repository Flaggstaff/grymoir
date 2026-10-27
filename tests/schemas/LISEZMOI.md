# Schémas des essais

- `generalInvoiceRequest_500.xsd` : le schéma de la facture électronique suisse (generalInvoice 5.0), © 2024 Forum
  Datenaustausch, https://www.forum-datenaustausch.ch, copié **sans modification**, comme sa licence l'exige (voir son
  en-tête). Source de la copie : le dépôt public github.com/brewengineering-ch/tarif595 (app/schemas), 27 octobre 2026.
- `signature-vide.xsd`, `chiffrement-vide.xsd` : deux schémas minimaux, écrits pour GrymoiR, qui tiennent lieu des schémas du
  W3C (signature XML, chiffrement XML) que le schéma importe par Internet ; la facture électronique de Praxis n'emploie ni
  l'un ni l'autre. `tests/praxis-electronique` les branche sur une copie temporaire du schéma, jamais sur l'original.
