import * as fs from 'fs';
import * as path from 'path';
import { SymbolDef } from './utils';

// Helper to scan symbols recursively (simple regex based)
export function scanSymbols(filePath: string, visited: Set<string> = new Set(), fileContent?: string): SymbolDef[] {
  if (visited.has(filePath)) return [];
  visited.add(filePath);

  let content = '';
  if (fileContent) {
    content = fileContent;
  } else {
    if (!fs.existsSync(filePath)) return [];
    content = fs.readFileSync(filePath, 'utf-8');
  }

  const symbols: SymbolDef[] = [];
  const lines = content.split('\n');

  // Regex for definitions
  const defRegex = /^\s*(struct|packet|enum)\s+([A-Z][a-zA-Z0-9_]*)/;
  // Regex for imports: @import("path/to/file.cnd")
  const importRegex = /@import\s*\(\s*"([^"]+)"\s*\)/;

  let docBuffer: string[] = [];

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const trimmed = line.trim();

    // 1. Doc Comment
    if (trimmed.startsWith('///')) {
      docBuffer.push(trimmed.substring(3).trim());
      continue;
    }

    // 2. Decorators (skip, keep doc buffer)
    if (trimmed.startsWith('@')) {
      // Check if it's an import (special case)
      const importMatch = importRegex.exec(line);
      if (importMatch) {
        const importPath = importMatch[1];
        const dir = path.dirname(filePath);
        const fullPath = path.resolve(dir, importPath);
        symbols.push(...scanSymbols(fullPath, visited));
        docBuffer = []; // Clear buffer after import
      }
      continue;
    }

    // 3. Definition
    const defMatch = defRegex.exec(line);
    if (defMatch) {
      symbols.push({
        name: defMatch[2],
        kind: defMatch[1] as any,
        file: filePath,
        line: i,
        doc: docBuffer.length > 0 ? docBuffer.join('\n') : undefined
      });
      docBuffer = [];
      continue;
    }

    // 4. Empty lines or other content -> Clear buffer
    if (trimmed.length > 0) {
      docBuffer = [];
    }
  }

  return symbols;
}
