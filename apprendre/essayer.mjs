// Éprouve la page construite docs/site/apprendre.html (9 novembre 2026), sans wasi-sdk : son moteur, sorti de la page,
// rejoue dans Node chaque leçon à lancer et chaque solution d'exercice, et la sortie doit contenir ce qu'elles annoncent.
// Avertit (sans échouer) si le guide, ses programmes ou les exercices ont changé depuis la construction de la page.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import vm from 'node:vm';
import { createHash, webcrypto } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const racine = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const page = fs.readFileSync(path.join(racine, 'docs/site/apprendre.html'), 'utf8');
let total = 0, echecs = 0;
const verifier = (ok, quoi, detail) => { total++; if (!ok) { echecs++; console.log('ÉCHEC : ' + quoi + (detail ? '\n' + detail : '')); } };

const moteurJs = /<script id="moteur-source" type="text\/plain">([\s\S]*?)<\/script>/.exec(page);
const donnees = /const DONNEES = (\{[\s\S]*?\});\nconst WASM_GZ = "([A-Za-z0-9+/=]+)";/.exec(page);
verifier(moteurJs && donnees, 'la page contient le moteur et les leçons');
if (!moteurJs || !donnees) process.exit(1);
const bac = { module: { exports: {} }, TextDecoder, TextEncoder, WebAssembly, crypto: globalThis.crypto || webcrypto, performance, BigInt };
vm.runInNewContext(moteurJs[1], bac);
const moteur = bac.module.exports.creerMoteur(zlib.gunzipSync(Buffer.from(donnees[2], 'base64')));
const { lecons, version, sources } = JSON.parse(donnees[1]);

function contient(reelle, attendue) {
  const a = attendue.split('\n').filter((l) => l.length);
  let i = 0;
  for (const l of reelle.split('\n')) if (i < a.length && l === a[i]) i++;
  return i === a.length;
}
for (const l of lecons) {
  if (!l.lancable) continue;
  const programme = l.exercice ? l.solution : l.code;
  const r = moteur.lancer(programme, l.reponses || '');
  verifier(r.ok, `« ${l.titre} » se lance`, r.ok ? null : `${r.message} (ligne ${r.ligne})`);
  if (r.ok && l.attendu) verifier(contient(r.sortie, l.attendu), `« ${l.titre} » affiche ce qu'elle annonce`, r.sortie);
}
// une erreur garde sa position ; la mise en forme ; la coloration, une lettre par point de code
const e = moteur.lancer('Afficher 1 / 0.\n');
verifier(!e.ok && e.ligne === 1 && e.colonne === 12 && e.message === 'Division par zéro.', 'une erreur et sa position');
const f = moteur.formater('afficher   «x».');
verifier(f.ok && f.sortie === 'Afficher « x ».\n', 'la mise en forme');
const t = 'Si l’âge ≥ 65, afficher « ok ».';
verifier(moteur.colorer(t).length === Array.from(t).length, 'la coloration couvre chaque point de code');
// la grammaire de la page est celle du dépôt
const v = /Version (\d+\.\d+)/.exec(fs.readFileSync(path.join(racine, 'docs/grammaire.md'), 'utf8'))[1];
if (v !== version) console.log(`ATTENTION : la page a été construite avec la grammaire ${version}, le dépôt est à la ${v} : relance apprendre/construire.`);
const liste = ['docs/guide.md', 'apprendre/exercices.md', 'apprendre/bienvenue.html',
  ...fs.readdirSync(path.join(racine, 'docs/guide')).filter((x) => x.endsWith('.grym')).sort().map((x) => 'docs/guide/' + x)];
const h = createHash('sha256');
for (const x of liste) h.update(x + '\0' + fs.readFileSync(path.join(racine, x)));
if (h.digest('hex') !== sources) console.log('ATTENTION : le guide ou les exercices ont changé depuis la construction de la page : relance apprendre/construire.');
// les mots colorés de la page sont ceux de l'atelier (src/atelier/coloration.cpp)
const mots = (texte, debut) => { const k = texte.indexOf(debut); const f = texte.indexOf('}', k); return [...texte.slice(k, f).matchAll(/"([^"]+)"/g)].map((m) => m[1]).sort().join(' '); };
const atelier = fs.readFileSync(path.join(racine, 'src/atelier/coloration.cpp'), 'utf8');
const web = fs.readFileSync(path.join(racine, 'apprendre/moteur.c'), 'utf8');
verifier(mots(atelier, 'static const QSet<QString> s = { "autrement"') === mots(web, 'CONTROLES[] = {'), 'les mots de construction de la page sont ceux de l\'atelier');
verifier(mots(atelier, '"décroissant"') === mots(web, 'MOTS_CLES[] = {'), 'les mots du langage colorés par la page sont ceux de l\'atelier');
console.log(`${total - echecs}/${total} tests réussis`);
process.exit(echecs ? 1 : 0);
