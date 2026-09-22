// GrymoiR pour VS Code : lance « grym lsp » et lui confie les fichiers .grym et .grymc.
// Spécification du serveur : docs/lsp.md.
'use strict';
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

function activate(context) {
    const chemin = vscode.workspace.getConfiguration('grymoir').get('chemin') || 'grym';
    const serveur = { command: chemin, args: ['lsp'] };
    client = new LanguageClient('grymoir', 'GrymoiR', { run: serveur, debug: serveur }, {
        documentSelector: [
            { scheme: 'file', language: 'grymoir' },
            { scheme: 'file', language: 'grymoir-compact' },
            { scheme: 'untitled', language: 'grymoir' },
            { scheme: 'untitled', language: 'grymoir-compact' }
        ]
    });
    client.start().catch((e) => {
        vscode.window.showErrorMessage(
            `GrymoiR : impossible de lancer « ${chemin} lsp » (${e.message}). ` +
            'Indiquez le chemin de l\'outil grym dans le réglage « grymoir.chemin ».');
    });
    context.subscriptions.push({ dispose: () => client && client.stop() });
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration((ev) => {
        if (ev.affectsConfiguration('grymoir.chemin'))
            vscode.window.showInformationMessage('GrymoiR : rechargez la fenêtre pour utiliser le nouveau chemin de grym.');
    }));
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
