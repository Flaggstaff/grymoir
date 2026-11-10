// GrymoiR dans une page (9 novembre 2026) : la couche WASI minimale et l'appel du moteur (apprendre/moteur.c).
// Le même texte sert dans le navigateur (dans un Worker) et dans Node (apprendre/essayer.mjs). Aucune dépendance.
// WASI preview 1 : https://github.com/WebAssembly/WASI/blob/main/legacy/preview1/docs.md
// Pas de système de fichiers : sans dossier ouvert d'avance, toute ouverture de fichier échoue, et la base vit en mémoire.
function creerMoteur(octets) {
  const ERRNO = { SUCCES: 0, BADF: 8, INVAL: 28, NOSYS: 52, NOTCAPABLE: 76 };
  let instance = null;
  const memoire = () => new DataView(instance.exports.memory.buffer);
  const u8 = () => new Uint8Array(instance.exports.memory.buffer);
  const decodeur = new TextDecoder('utf-8');
  const encodeur = new TextEncoder();
  let journal = '';   // ce que le moteur écrit sur stdout ou stderr (rien en temps normal)
  class Sortie extends Error {}
  const wasi = {
    args_get: () => ERRNO.SUCCES,
    args_sizes_get: (argc, taille) => { memoire().setUint32(argc, 0, true); memoire().setUint32(taille, 0, true); return ERRNO.SUCCES; },
    environ_get: () => ERRNO.SUCCES,
    environ_sizes_get: (n, taille) => { memoire().setUint32(n, 0, true); memoire().setUint32(taille, 0, true); return ERRNO.SUCCES; },
    clock_res_get: (id, res) => { memoire().setBigUint64(res, 1000000n, true); return ERRNO.SUCCES; },
    clock_time_get: (id, precision, temps) => {
      // 0 : l'heure réelle (UTC, la page ne connaît pas de fuseau dans WASI) ; les autres : une horloge monotone
      const ns = id === 0 ? BigInt(Date.now()) * 1000000n
                          : BigInt(Math.round((typeof performance !== 'undefined' ? performance.now() : Date.now()) * 1e6));
      memoire().setBigUint64(temps, ns, true);
      return ERRNO.SUCCES;
    },
    random_get: (ptr, n) => { crypto.getRandomValues(u8().subarray(ptr, ptr + n)); return ERRNO.SUCCES; },
    fd_write: (fd, iovs, nb, ecrit) => {
      if (fd !== 1 && fd !== 2) return ERRNO.BADF;
      const v = memoire();
      let total = 0;
      for (let k = 0; k < nb; k++) {
        const p = v.getUint32(iovs + 8 * k, true), l = v.getUint32(iovs + 8 * k + 4, true);
        journal += decodeur.decode(u8().slice(p, p + l));
        total += l;
      }
      v.setUint32(ecrit, total, true);
      return ERRNO.SUCCES;
    },
    fd_read: (fd, iovs, nb, lu) => { memoire().setUint32(lu, 0, true); return fd === 0 ? ERRNO.SUCCES : ERRNO.BADF; },
    fd_fdstat_get: (fd, stat) => {
      if (fd > 2) return ERRNO.BADF;
      const v = memoire();
      for (let k = 0; k < 24; k++) v.setUint8(stat + k, 0);
      v.setUint8(stat, 2);   // un périphérique caractère
      v.setBigUint64(stat + 8, 0xffffffffn, true);
      return ERRNO.SUCCES;
    },
    fd_prestat_get: () => ERRNO.BADF,   // aucun dossier ouvert d'avance
    fd_prestat_dir_name: () => ERRNO.BADF,
    fd_close: () => ERRNO.SUCCES,
    fd_seek: () => ERRNO.BADF,
    fd_sync: () => ERRNO.SUCCES,
    fd_fdstat_set_flags: () => ERRNO.SUCCES,
    fd_filestat_get: () => ERRNO.BADF,
    fd_filestat_set_size: () => ERRNO.BADF,
    fd_readdir: () => ERRNO.BADF,
    path_open: () => ERRNO.NOTCAPABLE,
    path_create_directory: () => ERRNO.NOTCAPABLE,
    path_filestat_get: () => ERRNO.NOTCAPABLE,
    path_filestat_set_times: () => ERRNO.NOTCAPABLE,
    path_readlink: () => ERRNO.NOTCAPABLE,
    path_remove_directory: () => ERRNO.NOTCAPABLE,
    path_rename: () => ERRNO.NOTCAPABLE,
    path_unlink_file: () => ERRNO.NOTCAPABLE,
    poll_oneoff: () => ERRNO.NOSYS,
    proc_exit: (code) => { throw new Sortie('Le moteur s\'est arrêté (code ' + code + ').'); },
  };
  const module = new WebAssembly.Module(octets);
  instance = new WebAssembly.Instance(module, { wasi_snapshot_preview1: wasi });
  instance.exports._initialize();
  const x = instance.exports;

  function texteVersMemoire(t) {
    const o = encodeur.encode(t);
    const p = x.grym_web_allouer(o.length + 1);
    u8().set(o, p);
    u8()[p + o.length] = 0;
    return p;
  }
  function memoireVersTexte(p) {
    const m = u8();
    let f = p;
    while (m[f] !== 0) f++;
    return decodeur.decode(m.slice(p, f));
  }
  // « OK\n… » ou « ERREUR\tligne\tcolonne\tmessage\n… » → { ok, ligne, colonne, message, sortie }
  function lireIssue(t) {
    const nl = t.indexOf('\n');
    const tete = nl < 0 ? t : t.slice(0, nl), sortie = nl < 0 ? '' : t.slice(nl + 1);
    if (tete === 'OK') return { ok: true, sortie };
    const [, ligne, colonne, message] = tete.split('\t');
    return { ok: false, ligne: +ligne, colonne: +colonne, message, sortie };
  }
  function appeler(fonction, ...textes) {
    journal = '';
    const ptrs = textes.map(texteVersMemoire);
    try {
      const r = fonction(...ptrs);
      const t = memoireVersTexte(r);
      x.grym_web_liberer(r);
      return lireIssue(t);
    } finally {
      ptrs.forEach((p) => x.grym_web_liberer(p));
    }
  }
  return {
    lancer: (programme, reponses = '') => appeler(x.grym_web_lancer, programme, reponses),
    formater: (programme) => appeler(x.grym_web_formater, programme),
    // une lettre par point de code (apprendre/moteur.c, grym_web_colorer)
    colorer: (programme) => {
      const p = texteVersMemoire(programme);
      try {
        const r = x.grym_web_colorer(p);
        const t = memoireVersTexte(r);
        x.grym_web_liberer(r);
        return t;
      } finally { x.grym_web_liberer(p); }
    },
    journal: () => journal,
  };
}
if (typeof module !== 'undefined') module.exports = { creerMoteur };
