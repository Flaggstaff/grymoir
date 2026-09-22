// GrymoiR pour VS Code : lance « grym lsp » et lui confie les fichiers .grym et .grymc.
// Spécification du serveur : docs/lsp.md.
'use strict';
const fs = require('fs');
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

// Message compréhensible quand l'outil grym manque ou ne se lance pas.
function expliquer(chemin, e) {
    const introuvable = (e && (e.code === 'ENOENT' || /ENOENT/.test(String(e.message))))
        || (chemin.includes('/') && !fs.existsSync(chemin));
    if (introuvable)
        return `GrymoiR : l'outil grym est introuvable à « ${chemin} ». Construisez-le avec « make » à la racine du ` +
               'dépôt, vérifiez le réglage « grymoir.chemin », puis rechargez la fenêtre (commande « Reload Window »).';
    return `GrymoiR : impossible de lancer « ${chemin} lsp » (${e && e.message ? e.message : e}). ` +
           'Vérifiez le réglage « grymoir.chemin », puis rechargez la fenêtre.';
}

function activate(context) {
    const chemin = vscode.workspace.getConfiguration('grymoir').get('chemin') || 'grym';
    if (chemin.includes('/') && !fs.existsSync(chemin)) {
        vscode.window.showErrorMessage(expliquer(chemin));   // inutile de tenter le lancement
        return;
    }
    const serveur = { command: chemin, args: ['lsp'] };
    client = new LanguageClient('grymoir', 'GrymoiR', { run: serveur, debug: serveur }, {
        documentSelector: [
            { scheme: 'file', language: 'grymoir' },
            { scheme: 'file', language: 'grymoir-compact' },
            { scheme: 'untitled', language: 'grymoir' },
            { scheme: 'untitled', language: 'grymoir-compact' }
        ]
    });
    client.start().catch((e) => vscode.window.showErrorMessage(expliquer(chemin, e)));
    context.subscriptions.push({ dispose: () => client && client.stop() });
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration((ev) => {
        if (ev.affectsConfiguration('grymoir.chemin'))
            vscode.window.showInformationMessage('GrymoiR : rechargez la fenêtre (commande « Reload Window ») pour utiliser le nouveau chemin de grym.');
    }));
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
