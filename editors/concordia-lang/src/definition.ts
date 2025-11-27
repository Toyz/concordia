import * as vscode from 'vscode';
import { scanSymbols } from './symbols';

export function registerDefinitionProvider(context: vscode.ExtensionContext, mode: vscode.DocumentFilter) {
  context.subscriptions.push(
    vscode.languages.registerDefinitionProvider(mode, {
      provideDefinition(document: vscode.TextDocument, position: vscode.Position, token: vscode.CancellationToken) {
        const wordRange = document.getWordRangeAtPosition(position);
        if (!wordRange) return undefined;
        const word = document.getText(wordRange);

        // Scan for definitions
        const symbols = scanSymbols(document.fileName);
        const def = symbols.find(s => s.name === word);

        if (def) {
          return new vscode.Location(
            vscode.Uri.file(def.file),
            new vscode.Position(def.line, 0)
          );
        }
        return undefined;
      }
    })
  );
}
