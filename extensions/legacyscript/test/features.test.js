const {test}=require('node:test');const assert=require('node:assert/strict');
const {TextDocument}=require('vscode-languageserver-textdocument');
const f=require('../server/features');
function input(text){const doc=TextDocument.create('file:///camera.ls','legacyscript',1,text);return [doc,doc.positionAt(text.length)];}
test('self completion has camera and object methods',()=>{
 const result=f.completion(...input('self:'));assert(result.some(x=>x.label==='setFieldOfView'));assert(result.some(x=>x.label==='setPosition'));
});
test('constructors are scoped',()=>{
 assert.deepEqual(f.completion(...input('Camera.')).map(x=>x.label).sort(),['getActive','new']);
 assert.deepEqual(f.completion(...input('GameObject.')).map(x=>x.label).sort(),['find','new']);
});
test('globals and document locals',()=>{
 const result=f.completion(...input('local player = GameObject.new("Player")\n'));
 for(const name of ['player','self','onStart','print'])assert(result.some(x=>x.label===name));
});
test('hover and signature help',()=>{
 assert.match(f.hover(...input('self:setFieldOfView')).contents.value,/degrees:number/);
 const result=f.signatureHelp(...input('self:setPosition(1, '));assert.equal(result.activeParameter,1);assert.equal(result.signatures[0].parameters.length,3);
 assert.equal(f.signatureHelp(...input('unknown(')),null);
});
test('outline',()=>{assert.equal(f.symbols(...input('function onStart()\nend'))[0].name,'onStart');});
