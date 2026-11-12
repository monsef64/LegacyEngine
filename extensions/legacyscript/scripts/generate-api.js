// Keep the extension's checked-in API catalog synchronized with the engine.
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../../..');
const text = fs.readFileSync(path.join(root, 'src/scripting/lua_definitions.hpp'), 'utf8');
const api = [...text.matchAll(/\{"([^"]+)","([^"]+)"\}/g)].map(m => ({ name: m[1], signature: m[2] }));
fs.writeFileSync(path.join(__dirname, '../server/api.json'), JSON.stringify(api, null, 2) + '\n');
const definitions = fs.readFileSync(path.join(root, 'scripts/definitions/legacyengine.lua'), 'utf8');
const members = {};let owner = '';
for (const line of definitions.split('\n')) {
  const cls = line.match(/^---@class (\w+)/); if (cls) owner = cls[1];
  const field = line.match(/^---@field (\w+) (.+)$/);
  if (field) (members[owner] ||= {})[field[1]] = field[2];
}
fs.writeFileSync(path.join(__dirname, '../server/members.json'), JSON.stringify(members, null, 2) + '\n');
