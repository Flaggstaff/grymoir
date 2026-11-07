# Banc de mesure : base.grym avec sqlite3, une seule transaction comme une exécution de GrymoiR.
import os, sqlite3, sys
chemin = sys.argv[1] if len(sys.argv) > 1 else "base_py.sqlite"
if os.path.exists(chemin):
    os.remove(chemin)
b = sqlite3.connect(chemin)
b.execute("create table compte (id integer primary key, numero integer unique not null, solde text not null)")
with b:
    for i in range(1, 10001):
        b.execute("insert into compte (numero, solde) values (?, ?)", (i, str((i * 37) % 10000)))
print(b.execute("select count(*) from compte where cast(solde as integer) > 5000").fetchone()[0])
for i in range(1, 1001):
    d = b.execute("select solde from compte where numero = ?", (i * 7,)).fetchone()
print(d[0])
b.close()
os.remove(chemin)
