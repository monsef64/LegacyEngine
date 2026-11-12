const { LanguageClient, TransportKind } = require('vscode-languageclient/node');
let client;
function activate(context) {
  client = new LanguageClient('legacyscript', 'LegacyScript Language Server', {
    run: { module: context.asAbsolutePath('server/server.js'), transport: TransportKind.ipc },
    debug: { module: context.asAbsolutePath('server/server.js'), transport: TransportKind.ipc }
  }, { documentSelector: [{ language: 'legacyscript', scheme: 'file' }, { language: 'legacyscript', scheme: 'untitled' }] });
  context.subscriptions.push(client);
  return client.start();
}
function deactivate() { return client?.stop(); }
module.exports = { activate, deactivate };
