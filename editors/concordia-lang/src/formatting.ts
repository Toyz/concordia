import * as cp from 'child_process';
import * as vscode from 'vscode';
import { getCompilerPath, LOG_CHANNEL } from './utils';

export function formatDocument(doc: vscode.TextDocument): vscode.TextEdit[] {
  const compilerPath = getCompilerPath();
  const filePath = doc.fileName;

  LOG_CHANNEL.appendLine(`Formatting document: ${filePath}`);

  try {
    const stdout = cp.execSync(`${compilerPath} fmt "${filePath}"`).toString();

    const firstLine = doc.lineAt(0);
    const lastLine = doc.lineAt(doc.lineCount - 1);
    const fullRange = new vscode.Range(firstLine.range.start, lastLine.range.end);

    return [vscode.TextEdit.replace(fullRange, stdout)];
  } catch (e) {
    LOG_CHANNEL.appendLine(`Formatting failed: ${e}`);
    vscode.window.showErrorMessage('Concordia formatting failed. Check Output panel.');
    return [];
  }
}

