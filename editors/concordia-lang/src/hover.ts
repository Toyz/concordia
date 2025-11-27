import * as vscode from 'vscode';
import * as path from 'path';
import { scanSymbols } from './symbols';

export function registerHoverProvider(context: vscode.ExtensionContext, mode: vscode.DocumentFilter) {
  context.subscriptions.push(
    vscode.languages.registerHoverProvider(mode, {
      provideHover(document, position, token) {
        const range = document.getWordRangeAtPosition(position);
        if (!range) return undefined;

        const word = document.getText(range);
        
        // Scan symbols (naive approach: re-scan on every hover)
        // In a real LS, this would be cached.
        const symbols = scanSymbols(document.fileName, new Set(), document.getText());
        const symbol = symbols.find(s => s.name === word);

        if (symbol) {
          const md = new vscode.MarkdownString();
          md.appendCodeblock(`${symbol.kind} ${symbol.name}`, 'concordia');
          md.appendMarkdown(`\nDefined in **${path.basename(symbol.file)}**\n\n`);
          if (symbol.doc) {
            md.appendMarkdown('___\n');
            md.appendMarkdown(symbol.doc);
          }
          return new vscode.Hover(md);
        }
        
        return undefined;
      }
    })
  );
}
