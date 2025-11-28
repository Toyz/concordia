import * as path from 'path';
import * as vscode from 'vscode';
import { scanSymbols } from './symbols';

export function registerCompletionProviders(context: vscode.ExtensionContext, mode: vscode.DocumentFilter) {
  // IntelliSense - Keywords & Types
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(mode, {
      provideCompletionItems(document: vscode.TextDocument, position: vscode.Position) {
        const linePrefix = document.lineAt(position).text.substr(0, position.character);

        // Enum underlying type completion
        // Matches: "enum Name :" or "enum Name : "
        if (/enum\s+\w+\s*:\s*$/.test(linePrefix)) {
          const intTypes = [
            'uint8', 'u8', 'byte',
            'int8', 'i8',
            'uint16', 'u16',
            'int16', 'i16',
            'uint32', 'u32',
            'int32', 'i32',
            'uint64', 'u64',
            'int64', 'i64'
          ].map(t => new vscode.CompletionItem(t, vscode.CompletionItemKind.Class));
          return intTypes;
        }

        if (linePrefix.trim().endsWith('@')) {
          return undefined; // Let the decorator provider handle this
        }

        const keywords = [
          'import', 'const', 'enum', 'true', 'false', 'prefix', 'until', 'max',
          'switch', 'case', 'default'
        ].map(k => new vscode.CompletionItem(k, vscode.CompletionItemKind.Keyword));

        const types = [
          'bool', 'uint8', 'uint16', 'uint32', 'uint64',
          'int8', 'int16', 'int32', 'int64',
          'float32', 'float64', 'string', 'bytes',
          'float', 'double', 'byte', 'u8', 'u16', 'u32', 'u64', 'i8', 'i16', 'i32', 'i64', 'f32', 'f64'
        ].map(t => new vscode.CompletionItem(t, vscode.CompletionItemKind.Class));

        const structSnippet = new vscode.CompletionItem('struct', vscode.CompletionItemKind.Snippet);
        structSnippet.insertText = new vscode.SnippetString('struct ${1:Name} {\n\t$0\n}');
        structSnippet.detail = 'Define a new struct';

        const packetSnippet = new vscode.CompletionItem('packet', vscode.CompletionItemKind.Snippet);
        packetSnippet.insertText = new vscode.SnippetString('packet ${1:Name} {\n\t$0\n}');
        packetSnippet.detail = 'Define a new packet';

        const enumSnippet = new vscode.CompletionItem('enum', vscode.CompletionItemKind.Snippet);
        enumSnippet.insertText = new vscode.SnippetString('enum ${1:Name} : ${2:uint32} {\n\t${3:Member} = ${4:0}\n}');
        enumSnippet.detail = 'Define a new enum';

        // Dynamic: Scan for user-defined structs/packets in the current file AND imported files
        const symbols = scanSymbols(document.fileName, new Set(), document.getText());
        const userTypes = symbols
          .filter(s => s.kind !== 'packet') // Packets cannot be used as types
          .map(s => {
            const kind = s.kind === 'enum' ? vscode.CompletionItemKind.Enum : vscode.CompletionItemKind.Struct;
            const item = new vscode.CompletionItem(s.name, kind);
            item.detail = `Defined in ${path.basename(s.file)}`;
            if (s.doc) {
              item.documentation = new vscode.MarkdownString(s.doc);
            }
            return item;
          });

        return [...keywords, ...types, ...userTypes, structSnippet, packetSnippet, enumSnippet];
      }
    }, ':')
  );

  // IntelliSense - Decorators
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(mode, {
      provideCompletionItems(document: vscode.TextDocument, position: vscode.Position) {
        const linePrefix = document.lineAt(position).text.substr(0, position.character);
        if (!linePrefix.endsWith('@')) {
          return undefined;
        }

        const decorators = [
          'version', 'import', 'big_endian', 'little_endian', 'be', 'le',
          'fill', 'crc_refin', 'crc_refout', 'optional',
          'count', 'const', 'pad', 'range', 'depends_on',
          'crc', 'crc_poly', 'crc_init', 'crc_xor',
          'scale', 'offset', 'mul', 'div', 'add', 'sub'
        ].map(d => new vscode.CompletionItem(d, vscode.CompletionItemKind.Function));

        return decorators;
      }
    }, '@')
  );

  // IntelliSense - Enum Values (Enum.Value)
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(mode, {
      provideCompletionItems(document: vscode.TextDocument, position: vscode.Position) {
        const linePrefix = document.lineAt(position).text.substr(0, position.character);
        
        // Matches "EnumName."
        const match = /([A-Z][a-zA-Z0-9_]*)\.$/.exec(linePrefix);
        if (!match) return undefined;

        const enumName = match[1];
        const symbols = scanSymbols(document.fileName, new Set(), document.getText());
        const enumDef = symbols.find(s => s.name === enumName && s.kind === 'enum');

        if (enumDef && enumDef.members) {
          return enumDef.members.map(m => {
            const item = new vscode.CompletionItem(m.name, vscode.CompletionItemKind.EnumMember);
            item.detail = `${enumName}.${m.name}`;
            if (m.doc) {
                item.documentation = new vscode.MarkdownString(m.doc);
            }
            return item;
          });
        }
        return undefined;
      }
    }, '.')
  );
}
