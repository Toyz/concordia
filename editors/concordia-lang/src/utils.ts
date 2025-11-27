import * as vscode from 'vscode';

export const CND_MODE: vscode.DocumentFilter = { language: 'concordia', scheme: 'file' };
export const LOG_CHANNEL = vscode.window.createOutputChannel("Concordia");

export interface SymbolDef {
  name: string;
  kind: 'struct' | 'packet' | 'enum';
  file: string;
  line: number;
  doc?: string;
}

export function getCompilerPath(): string {
  const config = vscode.workspace.getConfiguration('concordia');
  return config.get<string>('compilerPath') || 'cnd';
}

