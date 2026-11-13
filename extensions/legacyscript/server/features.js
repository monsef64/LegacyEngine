const api = require('./api.json');
const members = require('./members.json');
const keywords = 'and break do else elseif end false for function goto if in local nil not or repeat return then true until while'.split(' ');
const objectMembers = { ...members.GameObject, ...members.MeshRenderer, ...members.CameraObject, ...members.AudioSource, ...members.AudioListener, ...members.DirectionalLight, ...members.PointLight, ...members.Skybox, ...members.SpotLight };
function memberTable(receiver) {
  if (receiver === 'GameObject') return members.GameObjectConstructor;
  if (receiver === 'Camera') return members.CameraConstructor;
  if (receiver === 'debug') return members.LegacyDebug;
  return objectMembers;
}
function completion(doc, position) {
  const prefix = doc.getText().slice(0, doc.offsetAt(position));
  const member = prefix.match(/(\w+)\s*[.:]\s*(\w*)$/);
  if (member) return Object.entries(memberTable(member[1]) || {}).map(([label, detail]) => ({ label, detail, kind: detail.startsWith('fun') ? 2 : 5 }));
  const items = new Map(api.map(entry => [entry.name, { label: entry.name, detail: entry.signature, kind: 3 }]));
  for (const label of keywords) items.set(label, { label, kind: 14 });
  for (const label of ['print','type','tostring','tonumber','pairs','ipairs','assert','error','math','string','table','self']) if (!items.has(label)) items.set(label, { label, kind: 6 });
  for (const match of doc.getText().matchAll(/\b(?:local\s+(?:function\s+)?|function\s+)([A-Za-z_]\w*)/g))
    items.set(match[1], { label: match[1], kind: 6, detail: 'Document symbol' });
  for (const match of doc.getText().matchAll(/^[ \t]*(?:---)?@export\s+([A-Za-z_]\w*)\s+(\w+)/gm))
    items.set(match[1], { label: match[1], kind: 6, detail: `Exported ${match[2]} (assigned in Inspector)` });
  return [...items.values()];
}
function signature(name, receiver) {
  if (receiver) {
    const detail = memberTable(receiver)?.[name];
    if (detail) return `${receiver}.${name} ${detail.replace(/self:[\w]+,?/, '')}`;
  }
  return api.find(entry => entry.name === name)?.signature;
}
function hover(doc, position) {
  const offset = doc.offsetAt(position), text = doc.getText();let start=offset,end=offset;
  while(start>0 && /\w/.test(text[start-1]))--start;
  while(end<text.length && /\w/.test(text[end]))++end;
  const word=text.slice(start,end),receiver=text.slice(0,start).match(/(\w+)\s*[.:]\s*$/)?.[1];
  const exported=[...text.matchAll(/^[ \t]*(?:---)?@export\s+([A-Za-z_]\w*)\s+(\w+)/gm)].find(m=>m[1]===word);
  const detail=exported?`${word}: ${exported[2]} — exported Inspector field`:word==='self'?'self: GameObject — owner of the attached script. Camera methods require a Camera component.':signature(word,receiver);
  return detail?{contents:{kind:'markdown',value:`\`\`\`lua\n${detail}\n\`\`\``},range:{start:doc.positionAt(start),end:doc.positionAt(end)}}:null;
}
function signatureHelp(doc, position) {
  const text=doc.getText().slice(0,doc.offsetAt(position));let depth=0,commas=0;
  for(let i=text.length-1;i>=0;--i){
    const ch=text[i];
    if(')}]'.includes(ch))++depth;
    else if('({['.includes(ch)){
      if(depth){--depth;continue;}if(ch!=='(')return null;
      const match=text.slice(0,i).match(/(?:(\w+)\s*[.:]\s*)?(\w+)\s*$/);
      if(!match)return null;const label=signature(match[2],match[1]);if(!label)return null;
      const parameters=(label.match(/\(([^)]*)\)/)?.[1]||'').split(',').filter(Boolean).map(p=>({label:p.trim()}));
      return {signatures:[{label,parameters}],activeSignature:0,activeParameter:Math.min(commas,Math.max(0,parameters.length-1))};
    }else if(ch===',' && !depth)++commas;
  }return null;
}
function symbols(doc){return [...doc.getText().matchAll(/\bfunction\s+([\w.:]+)\s*\(/g)].map(m=>{
  const range={start:doc.positionAt(m.index),end:doc.positionAt(m.index+m[0].length)};
  return {name:m[1],kind:12,range,selectionRange:range};
});}
module.exports={completion,hover,signatureHelp,symbols};
