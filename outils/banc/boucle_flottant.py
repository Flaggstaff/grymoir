# Banc de mesure : la même boucle en flottants binaires, ce que GrymoiR refuse (résultat inexact) ; pour l'ordre de grandeur.
somme = 0.0
for i in range(1, 1000001):
    somme += i * 1.05
print(somme)
