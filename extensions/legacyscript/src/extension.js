const vscode = require('vscode');
const { LanguageClient, TransportKind, ErrorAction, CloseAction } = require('vscode-languageclient/node');
let client;
async function activate(context) {
  const output = vscode.window.createOutputChannel('LegacyScript');
  context.subscriptions.push(output);
  const report = (message, error) => {
    output.appendLine(`${new Date().toISOString()} ${message}: ${error?.stack || error || ''}`);
  };
  let crashes = [];
  client = new LanguageClient('legacyscript', 'LegacyScript Language Server', {
    run: { module: context.asAbsolutePath('server/server.js'), transport: TransportKind.ipc },
    debug: { module: context.asAbsolutePath('server/server.js'), transport: TransportKind.ipc }
  }, {
    outputChannel: output,
    documentSelector: [{ language: 'legacyscript', scheme: 'file' }, { language: 'legacyscript', scheme: 'untitled' }],
    errorHandler: {
      error(error, message, count) {
        report(`Language server error (${count || 1})`, error);
        return {action: ErrorAction.Continue};
      },
      closed() {
        const now = Date.now();
        crashes = crashes.filter(time => now - time < 60000);
        crashes.push(now);
        report('Language server disconnected', '');
        if (crashes.length <= 3) return {action: CloseAction.Restart};
        vscode.window.showErrorMessage('LegacyScript server repeatedly stopped. See the LegacyScript output channel, then reload the window.');
        return {action: CloseAction.DoNotRestart};
      }
    }
  });
  context.subscriptions.push(client);
  try { await client.start(); }
  catch (error) {
    report('Could not start language server', error);
    vscode.window.showErrorMessage('LegacyScript could not start. See the LegacyScript output channel for details.');
  }
}
async function deactivate() {
  try { await client?.stop(); }
  catch (error) { console.error('LegacyScript shutdown failed:', error); }
}
module.exports = { activate, deactivate };
