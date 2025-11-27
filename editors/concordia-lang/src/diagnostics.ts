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
  const cmd = `${compilerPath} compile "${filePath}" "${tempOut}" --json`;

  LOG_CHANNEL.appendLine(`Running diagnostics: ${cmd}`);

  cp.exec(cmd, (err, stdout, stderr) => {
    if (err) {
      LOG_CHANNEL.appendLine(`Compiler returned error code: ${err.code}`);
    }
    if (stderr) {
      LOG_CHANNEL.appendLine(`Compiler stderr: ${stderr}`);
    }

    collection.clear();

    const output = stdout.toString();
    if (!output.trim()) return;

    try {
      const lines = output.split('\n');
      const diagnostics: vscode.Diagnostic[] = [];

      for (const line of lines) {
        if (!line.trim()) continue;
        try {
          const msg = JSON.parse(line);
          if (msg.error) {
            LOG_CHANNEL.appendLine(`Compiler error: ${msg.error}`);
            // vscode.window.showErrorMessage(msg.error); // Too noisy
          } else if (msg.line) {
            const lineNo = msg.line - 1;
            const colNo = 0;
            const range = new vscode.Range(
              lineNo, colNo,
              lineNo, 1000
            );
            const diagnostic = new vscode.Diagnostic(
              range,
              msg.message,
              vscode.DiagnosticSeverity.Error
            );
            diagnostics.push(diagnostic);
          }
        } catch (e) {
          LOG_CHANNEL.appendLine(`Non-JSON output: ${line}`);
        }
      }

      collection.set(vscode.Uri.file(filePath), diagnostics);

    } catch (e) {
      LOG_CHANNEL.appendLine(`Failed to parse compiler output: ${e}`);
    }
  });
}

