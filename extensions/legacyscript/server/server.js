const {createConnection, ProposedFeatures, TextDocuments, TextDocumentSyncKind} = require('vscode-languageserver/node');
const {TextDocument} = require('vscode-languageserver-textdocument');
const features = require('./features');
const {diagnostics} = require('./diagnostics');
const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
const timers = new Map();
const lastReported = new Map();
function report(feature, error) {
  connection.console.error(`LegacyScript ${feature}: ${error.stack || error}`);
  const now = Date.now();
  if (now - (lastReported.get(feature) || 0) > 60000) {
    lastReported.set(feature, now);
    connection.window.showErrorMessage(`LegacyScript ${feature} failed. See Output → LegacyScript for details.`);
  }
}
function validate(doc) {
  try { connection.sendDiagnostics({uri: doc.uri, version: doc.version, diagnostics: diagnostics(doc)}); }
  catch (error) {
    report('validation', error);
    connection.sendDiagnostics({uri: doc.uri, version: doc.version, diagnostics: [{
      range: {start: {line: 0, character: 0}, end: {line: 0, character: 0}},
      severity: 2, source: 'LegacyScript', code: 'validation-failed',
      message: 'Validation failed internally; script validity is unknown. See Output → LegacyScript.'
    }]});
  }
}
function schedule(doc, delay = 200) {
  clearTimeout(timers.get(doc.uri));
  timers.set(doc.uri, setTimeout(() => {
    timers.delete(doc.uri);
    const current = documents.get(doc.uri);
    if (current) validate(current);
  }, delay));
}
connection.onInitialize(() => ({capabilities: {
  textDocumentSync: {openClose: true, change: TextDocumentSyncKind.Incremental, save: true},
  completionProvider: {triggerCharacters: ['.', ':']}, hoverProvider: true,
  signatureHelpProvider: {triggerCharacters: ['(', ',']}, documentSymbolProvider: true
}}));
documents.onDidOpen(({document}) => schedule(document, 0));
documents.onDidChangeContent(({document}) => schedule(document));
documents.onDidSave(({document}) => schedule(document, 0));
documents.onDidClose(({document}) => {
  clearTimeout(timers.get(document.uri)); timers.delete(document.uri);
  connection.sendDiagnostics({uri: document.uri, diagnostics: []});
});
connection.onShutdown(() => { for (const timer of timers.values()) clearTimeout(timer); timers.clear(); });
for (const [handler, feature] of [['onCompletion', 'completion'], ['onHover', 'hover'], ['onSignatureHelp', 'signatureHelp'], ['onDocumentSymbol', 'symbols']]) {
  connection[handler](params => {
    try { const doc = documents.get(params.textDocument.uri); return doc ? features[feature](doc, params.position) : null; }
    catch (error) { report(feature, error); return null; }
  });
}
documents.listen(connection); connection.listen();
