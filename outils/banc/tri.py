# Banc de mesure : tri.grym. GrymoiR trie dans l'ordre du dictionnaire (sans accents ni casse) ; ici, casefold.
mots = []
for i in range(1, 100001):
    mots.append("k" + str((i * 7919) % 100003))
classes = sorted(mots, key=str.casefold)
print(classes[0], classes[-1])
