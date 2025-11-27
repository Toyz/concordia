import * as vscode from 'vscode';
import { registerCompletionProviders } from './completion';
import { registerDefinitionProvider } from './definition';
import { registerHoverProvider } from './hover';
import { refreshDiagnostics } from './diagnostics';
import { formatDocument } from './formatting';
import { CND_MODE, LOG_CHANNEL } from './utils';

export function activate(context: vscode.ExtensionContext) {
  LOG_CHANNEL.appendLine('Concordia extension activating...');
  context.subscriptions.push(LOG_CHANNEL);

  const diagnosticCollection = vscode.languages.createDiagnosticCollection('concordia');
  context.subscriptions.push(diagnosticCollection);

  // Diagnostics
  if (vscode.window.activeTextEditor) {
    refreshDiagnostics(vscode.window.activeTextEditor.document, diagnosticCollection);
  }
  context.subscriptions.push(
    vscode.window.onDidChangeActiveTextEditor(editor => {
      if (editor) {
        refreshDiagnostics(editor.document, diagnosticCollection);
      }
    })
  );
  context.subscriptions.push(
    vscode.workspace.onDidChangeTextDocument(e => refreshDiagnostics(e.document, diagnosticCollection))
  );
  context.subscriptions.push(
    vscode.workspace.onDidSaveTextDocument(doc => refreshDiagnostics(doc, diagnosticCollection))
  );

  // Formatting
  context.subscriptions.push(
    vscode.languages.registerDocumentFormattingEditProvider(CND_MODE, {
      provideDocumentFormattingEdits(document: vscode.TextDocument): vscode.TextEdit[] {
        return formatDocument(document);
      }
    })
  );

  // Providers
  registerCompletionProviders(context, CND_MODE);
  registerDefinitionProvider(context, CND_MODE);
  registerHoverProvider(context, CND_MODE);
}

export function deactivate() { }
