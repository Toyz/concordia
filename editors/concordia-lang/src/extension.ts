import * as vscode from 'vscode';
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind
} from 'vscode-languageclient/node';
import { getCompilerPath, LOG_CHANNEL } from './utils';

let client: LanguageClient;

export function activate(context: vscode.ExtensionContext) {
  LOG_CHANNEL.appendLine('Concordia LSP extension activating...');

  const serverPath = getCompilerPath();
  LOG_CHANNEL.appendLine(`Using server path: ${serverPath}`);

  // If the extension is launched in debug mode then the debug server options are used
  // Otherwise the run options are used
  const serverOptions: ServerOptions = {
    run: { command: serverPath, args: ["lsp"], transport: TransportKind.stdio },
    debug: { command: serverPath, args: ["lsp"], transport: TransportKind.stdio }
  };

  // Options to control the language client
  const clientOptions: LanguageClientOptions = {
    // Register the server for plain text documents
    documentSelector: [{ scheme: 'file', language: 'concordia' }],
    synchronize: {
      // Notify the server about file changes to '.cnd' files contained in the workspace
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cnd')
    },
    outputChannel: LOG_CHANNEL
  };

  // Create the language client and start the client.
  client = new LanguageClient(
    'concordiaLanguageServer',
    'Concordia Language Server',
    serverOptions,
    clientOptions
  );

  // Start the client. This will also launch the server
  client.start();
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
