const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
function extension(failStart = false) {
  const logs = [], messages = [];
  let options;
  const sandbox = {module: {exports: {}}, console, Date, require(name) {
    if (name === 'vscode') return {window: {
      createOutputChannel() { return {appendLine(line) {logs.push(line);}, dispose() {}}; },
      showErrorMessage(message) {messages.push(message);}
    }};
    if (name === 'vscode-languageclient/node') return {
      TransportKind: {ipc: 1}, ErrorAction: {Continue: 1}, CloseAction: {Restart: 1, DoNotRestart: 2},
      LanguageClient: class {
        constructor(id, title, server, config) {options = config;}
        async start() {if (failStart) throw new Error('startup test failure');}
        async stop() {}
      }
    };
    throw new Error(`Unexpected module: ${name}`);
  }};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../src/extension.js'), 'utf8'), sandbox);
  return {api: sandbox.module.exports, logs, messages, options: () => options};
}
const context = () => ({subscriptions: [], asAbsolutePath: value => value});
test('extension reports startup failures instead of rejecting activation', async () => {
  const e = extension(true);
  await e.api.activate(context());
  assert(e.messages.some(message => message.includes('could not start')));
  assert(e.logs.some(line => line.includes('startup test failure')));
  await e.api.deactivate();
});
test('extension bounds automatic restarts and logs protocol errors', async () => {
  const e = extension();
  await e.api.activate(context());
  const handler = e.options().errorHandler;
  assert.equal(handler.error(new Error('protocol failure'), null, 1).action, 1);
  for (let i = 0; i < 3; ++i) assert.equal(handler.closed().action, 1);
  assert.equal(handler.closed().action, 2);
  assert(e.logs.some(line => line.includes('protocol failure')));
  assert.equal(e.messages.length, 1);
});
