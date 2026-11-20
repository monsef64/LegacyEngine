// Keep the extension's checked-in API catalog synchronized with the engine.
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../../..');
const text = fs.readFileSync(path.join(root, 'src/scripting/lua_definitions.hpp'), 'utf8');
const api = [...text.matchAll(/\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}/g)].map(m => ({ name: m[1], signature: m[2] }));
fs.writeFileSync(path.join(__dirname, '../server/api.json'), JSON.stringify(api, null, 2) + '\n');
const definitions = fs.readFileSync(path.join(root, 'scripts/definitions/legacyengine.lua'), 'utf8');
const members = {};let owner = '';
for (const line of definitions.split('\n')) {
  const cls = line.match(/^---@class (\w+)/); if (cls) owner = cls[1];
  const field = line.match(/^---@field (\w+) (.+)$/);
  if (field) (members[owner] ||= {})[field[1]] = field[2];
}
fs.writeFileSync(path.join(__dirname, '../server/members.json'), JSON.stringify(members, null, 2) + '\n');
// Sublime Text 4 uses the same API source; no independent hand-maintained catalog.
const sublime = path.join(root, 'extensions/sublime-legacyscript');
fs.mkdirSync(sublime, {recursive:true});
fs.writeFileSync(path.join(sublime, 'api.json'), JSON.stringify(api, null, 2) + '\n');
const completions = api.map(item => ({trigger:item.name, contents:item.name, annotation:'LegacyEngine', details:item.signature}));
for (const [type, fields] of Object.entries(members)) for (const [name, description] of Object.entries(fields)) {
  completions.push({trigger:name, contents:name, annotation:type, details:description});
}
fs.writeFileSync(path.join(sublime, 'LegacyScript.sublime-completions'), JSON.stringify({scope:'source.legacyscript - comment - string', completions}, null, 2) + '\n');
