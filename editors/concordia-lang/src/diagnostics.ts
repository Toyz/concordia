import * as cp from 'child_process';
import * as path from 'path';
import * as vscode from 'vscode';
import { getCompilerPath, LOG_CHANNEL } from './utils';

export function refreshDiagnostics(doc: vscode.TextDocument, collection: vscode.DiagnosticCollection) {
  if (doc.languageId !== 'concordia') {
    return;
  }

  const compilerPath = getCompilerPath();
  const filePath = doc.fileName;

  runCompilerDiagnostics(compilerPath, filePath, collection);
}

function runCompilerDiagnostics(compilerPath: string, filePath: string, collection: vscode.DiagnosticCollection) {
  const tempOut = path.join(path.dirname(filePath), 'temp.il');
  // Remove --json to get GCC-style output which includes column numbers
  const cmd = `${compilerPath} compile "${filePath}" "${tempOut}"`;

  LOG_CHANNEL.appendLine(`Running diagnostics: ${cmd}`);

  cp.exec(cmd, (err, stdout, stderr) => {
    if (err) {
      LOG_CHANNEL.appendLine(`Compiler returned error code: ${err.code}`);
    }
    if (stderr) {
      LOG_CHANNEL.appendLine(`Compiler stderr: ${stderr}`);
    }

    collection.clear();

    let output = stdout.toString();
    // Strip ANSI color codes
    output = output.replace(/\x1B\[[0-9;]*[a-zA-Z]/g, '');

    if (!output.trim()) return;

    try {
      const lines = output.split('\n');
      const diagnostics: vscode.Diagnostic[] = [];

      // Regex for GCC-style output: file:line:col: error: message
      const errorRegex = /^(.+):(\d+):(\d+):\s+(?:error|warning):\s+(.+)$/;

      for (const line of lines) {
        const match = line.trim().match(errorRegex);
        if (match) {
            const lineNo = parseInt(match[2]) - 1;
            const colNo = parseInt(match[3]) - 1;
            const message = match[4];

            const range = new vscode.Range(
                lineNo, colNo,
                lineNo, 1000 // Highlight until end of line
            );
            const diagnostic = new vscode.Diagnostic(
                range,
                message,
                vscode.DiagnosticSeverity.Error
            );
            diagnostics.push(diagnostic);
        }
      }

      collection.set(vscode.Uri.file(filePath), diagnostics);

    } catch (e) {
      LOG_CHANNEL.appendLine(`Failed to parse compiler output: ${e}`);
    }
  });
}

