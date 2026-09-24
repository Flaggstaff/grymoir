// GrymoiR : l'atelier, coloration par le lexeur (docs/atelier.md, § 6.3).
#include "coloration.h"

#include <QSet>

extern "C" {
#include "lexeur.h"
}

// Les mots qui ouvrent ou conduisent une construction, puis les autres mots du langage. Même partage que
// la coloration de l'extension VS Code (editeurs/vscode), pour qu'un fichier ait les mêmes couleurs partout.
static const QSet<QString> &controles() {
    static const QSet<QString> s = { "autrement", "répéter", "essayer", "chaque", "sortir", "passer", "rendre",
                                     "sinon", "selon", "échec", "fois", "pour", "tant", "cas", "que", "si" };
    return s;
}

static const QSet<QString> &mots_cles() {
    static const QSet<QString> s = {
        "décroissant", "enregistrer", "inférieure", "supérieure", "conservées", "différente", "croissant",
        "conservés", "supérieur", "supprimer", "inférieur", "différent", "conserver", "conservée", "afficher",
        "positive", "négative", "nouvelle", "conservé", "positif", "négatif", "nouveau", "devient", "départ",
        "unique", "nouvel", "alors", "nulle", "égale", "dont", "dans", "vaut", "égal", "faux", "vrai", "puis",
        "non", "nul", "les", "une", "pas", "par", "est", "de", "ou", "du", "et", "au", "la", "le", "un", "a",
        "à", "des", "supprimé", "supprimée", "supprimés", "supprimées", "rétablir", "définitivement", "absent",
        "absente", "présent", "présente", "facultatif", "facultative", "parmi", "gagnent", "perdent", "suivi",
        "sur", "saisi", "saisie", "réponse", "hui", "aujourd", "avec", "disparaît", "boucle", "tour", "suivant"};
    return s;
}

static int sorte_jeton(const Jeton &j, bool compacte) {
    switch (j.type) {
    case J_MOT: {
        const QString m = QString::fromUtf8(j.valeur ? j.valeur : "");
        if (!compacte && controles().contains(m)) return Coloration::Controle;
        if (!compacte && mots_cles().contains(m)) return Coloration::MotCle;
        return -1;
    }
    case J_ELISION:
        return Coloration::MotCle;
    case J_MOT_CLE: {
        const QString m = QString::fromUtf8(j.valeur ? j.valeur : "");
        return controles().contains(m) || m == "fin" || m == "alors" ? Coloration::Controle : Coloration::MotCle;
    }
    case J_NOMBRE:
    case J_DATE:
        return Coloration::Nombre;
    case J_TEXTE:
        return Coloration::Texte;
    case J_REMARQUE:
        return Coloration::Remarque;
    case J_CROCHETS:
        return j.synthetique ? -1 : Coloration::Nom;
    case J_ERREUR:
        return Coloration::Erreur;
    default:
        return j.type == J_FIN || j.type == J_ARTICLE_IMPLICITE ? -1 : Coloration::Operateur;
    }
}

QVector<int> Coloration::sortes(const QString &ligne, bool compacte) {
    QVector<int> r(ligne.size(), -1);
    // Le lexeur compte en points de code, sur la source normalisée NFC ; l'éditeur, en unités UTF-16.
    // Une ligne que la normalisation change (lettres décomposées) reste sans couleur plutôt que mal colorée.
    if (ligne.normalized(QString::NormalizationForm_C) != ligne) return r;
    QVector<int> debut_utf16;  // point de code k → position UTF-16
    for (int i = 0; i < ligne.size(); i++) {
        debut_utf16.append(i);
        if (ligne.at(i).isHighSurrogate() && i + 1 < ligne.size()) i++;
    }
    debut_utf16.append(ligne.size());
    const QByteArray octets = ligne.toUtf8();
    char *erreur = nullptr;
    Lexeur *lx = compacte ? lexeur_creer_compact(octets.constData(), (size_t)octets.size(), &erreur)
                          : lexeur_creer(octets.constData(), (size_t)octets.size(), &erreur);
    if (!lx) {
        free(erreur);
        return r;
    }
    for (;;) {
        Jeton j = lexeur_suivant(lx);
        if (j.type == J_FIN) {
            jeton_liberer(&j);
            break;
        }
        const int sorte = sorte_jeton(j, compacte);
        const int n = debut_utf16.size() - 1;
        int a = (int)j.debut, b = (int)(j.debut + j.longueur);
        if (j.type == J_ERREUR) b = n;   // tout ce qui suit l'erreur reste marqué
        a = qBound(0, a, n);
        b = qBound(a, b, n);
        if (sorte >= 0 && !j.synthetique)
            for (int k = debut_utf16[a]; k < debut_utf16[b]; k++) r[k] = sorte;
        const bool fin = j.type == J_ERREUR;
        jeton_liberer(&j);
        if (fin) break;
    }
    lexeur_detruire(lx);
    return r;
}

Coloration::Coloration(QTextDocument *document, bool compacte) : QSyntaxHighlighter(document), compacte(compacte) {
    formats[Controle].setForeground(QColor(0x8e, 0x3b, 0xa8));
    formats[Controle].setFontWeight(QFont::Bold);
    formats[MotCle].setForeground(QColor(0x1f, 0x5f, 0xa8));
    formats[Nombre].setForeground(QColor(0x0b, 0x7a, 0x5b));
    formats[Texte].setForeground(QColor(0xa8, 0x4a, 0x1f));
    formats[Remarque].setForeground(QColor(0x80, 0x80, 0x80));
    formats[Remarque].setFontItalic(true);
    formats[Operateur].setForeground(QColor(0x55, 0x55, 0x55));
    formats[Nom].setForeground(QColor(0x2b, 0x2b, 0x2b));
    formats[Nom].setFontItalic(true);
    formats[Erreur].setUnderlineStyle(QTextCharFormat::WaveUnderline);
    formats[Erreur].setUnderlineColor(Qt::red);
}

void Coloration::highlightBlock(const QString &ligne) {
    const QVector<int> s = sortes(ligne, compacte);
    int i = 0;
    while (i < s.size()) {
        int j = i;
        while (j < s.size() && s[j] == s[i]) j++;
        if (s[i] >= 0) setFormat(i, j - i, formats[s[i]]);
        i = j;
    }
}
