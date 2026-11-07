# Banc de mesure : la boucle décimale de boucle.grym, en décimal exact (le module decimal de Python).
from decimal import Decimal
somme = Decimal(0)
taux = Decimal("1.05")
for i in range(1, 1000001):
    somme += i * taux
print(somme)
