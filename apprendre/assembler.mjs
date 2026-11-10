// Assemble la page autonome docs/site/apprendre.html (9 novembre 2026) : le moteur apprendre/grym.wasm (compressé,
// embarqué), les leçons tirées de docs/guide.md et les exercices de apprendre/exercices.md.
// Rien n'entre dans la page sans avoir tourné : chaque leçon et chaque solution d'exercice passe par le vrai moteur, et
// sa sortie doit contenir celle que le texte annonce ; sinon la construction échoue (le principe de tests/test_guide.c).
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { createHash, webcrypto } from 'node:crypto';
if (!globalThis.crypto) globalThis.crypto = webcrypto;   // Node avant la version 19
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const ici = path.dirname(fileURLToPath(import.meta.url));
const racine = path.resolve(ici, '..');
const require = createRequire(import.meta.url);
const { creerMoteur } = require('./moteur.js');
const octets = fs.readFileSync(path.join(ici, 'grym.wasm'));
const moteur = creerMoteur(octets);
const erreurs = [];

// ---------- Un Markdown réduit, celui du guide : paragraphes, listes, `code`, **gras**, *italique* ----------
const echapper = (t) => t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
function enLigne(t) {
  const morceaux = t.split('`');
  return morceaux.map((m, k) => {
    if (k % 2) return '<code>' + echapper(m) + '</code>';
    return echapper(m).replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>').replace(/(^|[^*])\*([^*\s][^*]*)\*/g, '$1<em>$2</em>')
      .replace(/'/g, '’').replace(/« /g, '«\u00a0').replace(/ »/g, '\u00a0»').replace(/ ([:;?!])/g, '\u202f$1');
  }).join('');
}
function prose(lignes) {
  const html = [];
  let para = [], liste = [];
  const fermer = () => {
    if (para.length) html.push('<p>' + enLigne(para.join(' ')) + '</p>');
    if (liste.length) html.push('<ul>' + liste.map((l) => '<li>' + enLigne(l) + '</li>').join('') + '</ul>');
    para = []; liste = [];
  };
  for (const l of lignes) {
    if (!l.trim()) { fermer(); continue; }
    const m = /^\s*[-*] (.*)$/.exec(l);
    if (m) { if (para.length) { html.push('<p>' + enLigne(para.join(' ')) + '</p>'); para = []; } liste.push(m[1]); }
    else if (liste.length && /^\s+\S/.test(l)) liste[liste.length - 1] += ' ' + l.trim();
    else { if (liste.length) fermer(); para.push(l.trim()); }
  }
  fermer();
  return html.join('\n');
}

// ---------- Le guide, en chapitres et sections ----------
const guide = fs.readFileSync(path.join(racine, 'docs/guide.md'), 'utf8').split('\n');
const chapitres = [];   // { numero, titre, introduction: [lignes], sections: [{ titre, morceaux }] }
let ch = null, sec = null, bloc = null, introGuide = [];
for (const l of guide) {
  if (bloc) {
    if (l.startsWith('```')) { (sec || ch).morceaux.push(bloc); bloc = null; }
    else bloc.lignes.push(l);
    continue;
  }
  const mc = /^## (\d+)\. (.*)$/.exec(l);
  if (mc) { ch = { numero: +mc[1], titre: mc[2], morceaux: [], sections: [] }; chapitres.push(ch); sec = null; continue; }
  const ms = /^### (.*)$/.exec(l);
  if (ms && ch) { sec = { titre: ms[1], morceaux: [] }; ch.sections.push(sec); continue; }
  if (/^# /.test(l)) continue;
  const mb = /^```(\w*)$/.exec(l);
  if (mb && ch) { bloc = { sorte: mb[1] || 'texte', lignes: [] }; continue; }
  if (!ch) { introGuide.push(l); continue; }
  const cible = sec || ch;
  const dernier = cible.morceaux[cible.morceaux.length - 1];
  if (dernier && dernier.sorte === 'prose') dernier.lignes.push(l);
  else cible.morceaux.push({ sorte: 'prose', lignes: [l] });
}

// Le programme complet d'un chapitre : docs/guide/chapitre-NN.grym, ou le dossier chapitre-NN (plusieurs fichiers)
function programmeDu(numero) {
  const nn = String(numero).padStart(2, '0');
  const f = path.join(racine, 'docs/guide', 'chapitre-' + nn + '.grym');
  if (fs.existsSync(f)) return { texte: fs.readFileSync(f, 'utf8'), plusieurs: false };
  return { texte: null, plusieurs: true };
}

// La sortie attendue est-elle dans la sortie réelle, lignes dans l'ordre ?
function contient(reelle, attendue) {
  const r = reelle.split('\n'), a = attendue.split('\n').filter((l) => l.length);
  let i = 0;
  for (const l of r) if (i < a.length && l === a[i]) i++;
  return i === a.length;
}

const lecons = [];
lecons.push({
  id: 'bienvenue', chapitre: 'Avant de commencer', titre: 'Bienvenue',
  html: fs.readFileSync(path.join(ici, 'bienvenue.html'), 'utf8'),
  preambule: '', code: 'Afficher « Bonjour, GrymoiR. ».\n', attendu: 'Bonjour, GrymoiR.', reponses: '', lancable: true,
});
// Une leçon par section. Toutes les leçons d'un chapitre partagent le programme complet du chapitre (celui que
// tests/test_guide exécute) : l'éditeur le montre entier, surligne les lignes que la section présente (ses extraits,
// cherchés au retrait près, comme le fait test_guide) et le verdict cherche la sortie que la section annonce.
for (const c of chapitres) {
  const prog = programmeDu(c.numero);
  const sections = c.sections.length ? c.sections : [{ titre: c.titre, morceaux: c.morceaux }];
  if (c.sections.length && c.morceaux.length) sections[0] = { titre: sections[0].titre, morceaux: [...c.morceaux, ...sections[0].morceaux] };
  let issue = null;
  if (prog.texte) {
    issue = moteur.lancer(prog.texte);
    if (!issue.ok && !/écrans ou des documents/.test(issue.message))
      erreurs.push(`Chapitre ${c.numero} : son programme échoue : ${issue.message} (ligne ${issue.ligne})`);
  }
  const lancable = prog.texte && issue && issue.ok;
  sections.forEach((s, k) => {
    const html = [], attendus = [], extraits = [];
    for (const m of s.morceaux) {
      if (m.sorte === 'prose') html.push(prose(m.lignes));
      else if (m.sorte === 'grymoir') {
        html.push('<pre class="extrait">' + echapper(m.lignes.join('\n')) + '</pre>');
        const l = m.lignes.map((x) => x.trim()).filter(Boolean);
        if (l.length) extraits.push(l);
      } else if (m.sorte === 'sortie') {
        html.push('<pre class="sortie-guide">' + echapper(m.lignes.join('\n')) + '</pre>');
        attendus.push(m.lignes.join('\n'));
      } else html.push('<pre>' + echapper(m.lignes.join('\n')) + '</pre>');
    }
    const lecon = {
      id: `ch${c.numero}-${k + 1}`, chapitre: `${c.numero}. ${c.titre}`, titre: s.titre, html: html.join('\n'),
      programme: `ch${c.numero}`, preambule: '', code: prog.texte || '', attendu: attendus.join('\n'), extraits,
      reponses: '', lancable: !!lancable,
    };
    if (!lancable) lecon.raison = prog.plusieurs ? 'plusieurs' : 'ecrans';
    else if (lecon.attendu && !contient(issue.sortie, lecon.attendu))
      erreurs.push(`Chapitre ${c.numero}, « ${s.titre} » : la sortie annoncée n'apparaît pas.`);
    lecons.push(lecon);
  });
}

// ---------- Les exercices (apprendre/exercices.md) ----------
// ## Titre  (chapitre N)   puis l'énoncé, et les blocs ```depart, ```solution, ```reponses (facultatif), ```attendu
const exTexte = fs.readFileSync(path.join(ici, 'exercices.md'), 'utf8').split('\n');
const exercices = [];
let ex = null; bloc = null;
for (const l of exTexte) {
  if (bloc) { if (l.startsWith('```')) { ex[bloc.sorte] = bloc.lignes.join('\n'); bloc = null; } else bloc.lignes.push(l); continue; }
  const mt = /^## (.*) \(chapitre (\d+)\)$/.exec(l);
  if (mt) { ex = { titre: mt[1], chapitre: +mt[2], enonce: [] }; exercices.push(ex); continue; }
  const mb = /^```(\w+)$/.exec(l);
  if (mb && ex) { bloc = { sorte: mb[1], lignes: [] }; continue; }
  if (ex) ex.enonce.push(l);
}
exercices.forEach((e, k) => {
  const nom = `Exercice « ${e.titre} »`;
  for (const champ of ['depart', 'solution', 'attendu']) if (e[champ] === undefined) erreurs.push(`${nom} : le bloc ${champ} manque.`);
  if (e.solution === undefined) return;
  const r = moteur.lancer(e.solution + '\n', e.reponses || '');
  if (!r.ok) erreurs.push(`${nom} : la solution échoue : ${r.message} (ligne ${r.ligne})`);
  else if (!contient(r.sortie, e.attendu)) erreurs.push(`${nom} : la solution n'affiche pas l'attendu.\nObtenu :\n${r.sortie}`);
  const d = moteur.lancer((e.depart || '') + '\n', e.reponses || '');
  if (d.ok && contient(d.sortie, e.attendu)) erreurs.push(`${nom} : le départ réussit déjà, il n'y a rien à faire.`);
  const c = chapitres.find((x) => x.numero === e.chapitre);
  lecons.push({
    id: `ex${k + 1}`, chapitre: 'Exercices', titre: `${e.titre} (ch. ${e.chapitre})`, exercice: true,
    html: prose(e.enonce) + (c ? `<p class="renvoi">Revois au besoin le chapitre ${c.numero}, « ${enLigne(c.titre)} ».</p>` : ''),
    preambule: '', code: e.depart.replace(/\n*$/, '\n'), solution: e.solution.replace(/\n*$/, '\n'),
    attendu: e.attendu, reponses: e.reponses || '', lancable: true,
  });
});
lecons.push({
  id: 'bac', chapitre: 'Exercices', titre: 'Bac à sable', html: '<p>Une page blanche : écris ce que tu veux, le vrai GrymoiR l’exécute.</p>',
  preambule: '', code: 'Remarque : à toi.\n', attendu: '', reponses: '', lancable: true,
});

// Les licences : la page redistribue du code compilé et des polices ; leurs avis l'accompagnent (MIT, BSD, OFL)
{
  const lire = (f) => fs.readFileSync(path.join(racine, f), 'utf8');
  const qr = lire('vendor/qrcodegen/qrcodegen.c').split('\n');
  const finQr = qr.findIndex((l, k) => k > 0 && /\*\//.test(l));
  const blocs = [
    ['GrymoiR', 'Le langage, son moteur et cette page : Licence publique de l’Union européenne (EUPL) v. 1.2.', 'LICENCE'],
    ['SQLite', 'Domaine public.', 'vendor/sqlite/LICENSE.md'],
    ['SQLite3 Multiple Ciphers (Ulrich Telle)', 'Licence MIT.', 'vendor/sqlite/LICENSE-SQLite3MC.txt'],
    ['Monocypher', 'BSD à 2 clauses ou CC0, au choix.', 'vendor/monocypher/LICENCE.md'],
    ['QR Code generator (Project Nayuki)', 'Licence MIT.', null, qr.slice(0, finQr + 1).join('\n')],
    ['Atkinson Hyperlegible Next', 'SIL Open Font License 1.1.', 'vendor/atkinson/OFL-Next.txt'],
    ['Atkinson Hyperlegible Mono', 'SIL Open Font License 1.1.', 'vendor/atkinson/OFL-Mono.txt'],
  ];
  lecons.push({
    id: 'licences', chapitre: 'À propos', titre: 'Licences', lancable: false, raison: 'lecture',
    preambule: '', code: '', attendu: '', reponses: '',
    html: '<p>Cette page embarque le moteur de GrymoiR, compilé en WebAssembly avec les bibliothèques qu’il contient, et deux polices. Voici leurs licences.</p>'
      + blocs.map(([nom, resume, fichier, texte]) => `<details><summary><strong>${echapper(nom)}</strong> : ${echapper(resume)}</summary><pre>${echapper(texte || lire(fichier))}</pre></details>`).join('\n'),
  });
}

if (erreurs.length) {
  console.error(erreurs.join('\n\n'));
  console.error(`\n${erreurs.length} problème(s) : la page n'est pas écrite.`);
  process.exit(1);
}

// ---------- La page ----------
const gabarit = fs.readFileSync(path.join(ici, 'page.html'), 'utf8');
const wasm64 = zlib.gzipSync(octets, { level: 9 }).toString('base64');
const version = (/Version (\d+\.\d+)/.exec(fs.readFileSync(path.join(racine, 'docs/grammaire.md'), 'utf8')) || [])[1] || '?';
// L'empreinte des sources de la page : tests/apprendre avertit quand le guide ou les exercices ont changé depuis
const sources = ['docs/guide.md', 'apprendre/exercices.md', 'apprendre/bienvenue.html',
  ...fs.readdirSync(path.join(racine, 'docs/guide')).filter((f) => f.endsWith('.grym')).sort().map((f) => 'docs/guide/' + f)];
const empreinte = createHash('sha256');
for (const f of sources) empreinte.update(f + '\0' + fs.readFileSync(path.join(racine, f)));
const donnees = JSON.stringify({ lecons, version, sources: empreinte.digest('hex') }).replace(/</g, '\\u003c');
const police = (f) => fs.readFileSync(path.join(racine, 'vendor/atkinson', f)).toString('base64');
const page = gabarit
  .replace('POLICE_NEXT_REGULIER', () => police('AtkinsonHyperlegibleNext-Regular.ttf'))
  .replace('POLICE_NEXT_ITALIQUE', () => police('AtkinsonHyperlegibleNext-Italic.ttf'))
  .replace('POLICE_NEXT_DEMIGRAS', () => police('AtkinsonHyperlegibleNext-SemiBold.ttf'))
  .replace('POLICE_NEXT_GRAS', () => police('AtkinsonHyperlegibleNext-Bold.ttf'))
  .replace('POLICE_MONO_REGULIER', () => police('AtkinsonHyperlegibleMono-Regular.ttf'))
  .replace('POLICE_MONO_DEMIGRAS', () => police('AtkinsonHyperlegibleMono-SemiBold.ttf'))
  .replace('/*MOTEUR_JS*/', () => fs.readFileSync(path.join(ici, 'moteur.js'), 'utf8').replace(/<\/script/gi, '<\\/script'))
  .replace('"DONNEES"', () => donnees)
  .replace('WASM_GZ_BASE64', () => wasm64)
  .replace(/GRAMMAIRE_VERSION/g, version);
const sortie = path.join(racine, 'docs/site/apprendre.html');
fs.writeFileSync(sortie, page);
const nbLancables = lecons.filter((l) => l.lancable).length;
console.log(`docs/site/apprendre.html : ${(page.length / 1e6).toFixed(2)} Mo, ${lecons.length} leçons (${nbLancables} à lancer), `
  + `${exercices.length} exercices, grammaire ${version}`);
