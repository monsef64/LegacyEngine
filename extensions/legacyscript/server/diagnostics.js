const lua = require('luaparse');
const types = new Set(('GameObject Transform RigidBody Collider MeshRenderer ObjectName LuaScript Camera AudioSource AudioListener DirectionalLight PointLight SpotLight Skybox CharacterController FPSCamera Raycast Animation ParticleSystem Asset AudioClip Texture number boolean string').split(' '));
const reserved = new Set('self onStart onUpdate onFixedUpdate onDestroy onTriggerEnter onTriggerStay onTriggerExit onCollisionEnter onCollisionStay onCollisionExit'.split(' '));

// Preserve UTF-16 offsets and line breaks when removing engine-only declarations.
// Strings and long comments are skipped, so examples containing @export are text.
function preprocess(text) {
  const output = text.split('');
  let atLineStart = true;
  const exports = [];
  for (let i = 0; i < text.length;) {
    const start = i;
    const leading = atLineStart;
    const bare = leading && text.startsWith('@export', i) && (!text[i + 7] || /\s/.test(text[i + 7]));
    const annotation = leading && text.startsWith('---@export', i) && (!text[i + 10] || /\s/.test(text[i + 10]));
    if (bare || annotation) {
      let end = text.indexOf('\n', i); if (end < 0) end = text.length;
      exports.push({start: i, end, body: text.slice(i + (bare ? 7 : 10), end)});
      if (bare) for (let j = i; j < end; ++j) if (text[j] !== '\r') output[j] = ' ';
      atLineStart = false; i = end; continue;
    }
    const comment = text.startsWith('--', i);
    const long = text.slice(i + (comment ? 2 : 0)).match(/^\[(=*)\[/);
    if (long) {
      const close = ']' + long[1] + ']';
      const end = text.indexOf(close, i + (comment ? 2 : 0) + long[0].length);
      atLineStart = false; i = end < 0 ? text.length : end + close.length; continue;
    }
    if (comment) { atLineStart = false; const end = text.indexOf('\n', i); i = end < 0 ? text.length : end; continue; }
    if (text[i] === '"' || text[i] === "'") {
      atLineStart = false;
      const quote = text[i++];
      while (i < text.length) { if (text[i] === '\\') { i += 2; continue; } if (text[i++] === quote) break; }
      continue;
    }
    // Lua 5.4 local attributes aren't understood by the bundled 5.3 grammar.
    // Only mask attributes in local declarations, never inside text/comments.
    if (text[i] === '<') {
      const attribute = text.slice(i).match(/^<\s*(const|close)\s*>/);
      const prefix = attribute && text.slice(0, i).match(/\blocal\s+[A-Za-z_]\w*(?:\s*<\s*(?:const|close)\s*>\s*)?(?:\s*,\s*[A-Za-z_]\w*(?:\s*<\s*(?:const|close)\s*>\s*)?)*\s*$/);
      if (attribute && prefix) {
        for (let j = i; j < i + attribute[0].length; ++j) if (text[j] !== '\n' && text[j] !== '\r') output[j] = ' ';
        i += attribute[0].length; continue;
      }
    }
    if (text[i] === '\n') atLineStart = true;
    else if (text[i] !== ' ' && text[i] !== '\t' && text[i] !== '\r') atLineStart = false;
    i = start + 1;
  }
  return {text: output.join(''), exports};
}

function diagnostics(doc) {
  const source = doc.getText();
  const result = [];
  const diagnostic = (start, end, message, code, severity = 1) => ({
    range: {start: doc.positionAt(start), end: doc.positionAt(Math.max(start, end))},
    severity, source: 'LegacyScript', code, message
  });
  if (source.length > 2 * 1024 * 1024) return [diagnostic(0, 0, 'Live validation is limited to 2 MiB to keep the editor responsive.', 'validation-size', 2)];
  const processed = preprocess(source);
  const names = new Set();
  for (const field of processed.exports) {
    const declaration = field.body.trim().match(/^([A-Za-z_]\w*)\s+(\w+)(?:\s|$)/);
    let message;
    if (!declaration) message = 'Expected @export name Type [defaultValue].';
    else if (reserved.has(declaration[1]) || declaration[1].length > 128) message = `Invalid or reserved exported name '${declaration[1]}'.`;
    else if (!types.has(declaration[2])) message = `Unknown export type '${declaration[2]}'.`;
    else if (names.has(declaration[1])) message = `Duplicate exported field '${declaration[1]}'.`;
    if (declaration) names.add(declaration[1]);
    if (message) result.push(diagnostic(field.start, field.end, message, 'export-declaration'));
    if (result.length >= 100) break;
  }
  try {
    lua.parse(processed.text, {luaVersion: '5.3', locations: true, ranges: true, comments: false, scope: false});
  } catch (error) {
    if (!(error instanceof SyntaxError)) throw error;
    const start = Math.max(0, Math.min(source.length, Number.isInteger(error.index) ? error.index : 0));
    result.push(diagnostic(start, Math.min(source.length, start + 1), error.message.replace(/^\[\d+:\d+\]\s*/, ''), 'syntax'));
  }
  return result;
}
module.exports = {diagnostics};
