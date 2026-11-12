const {test}=require('node:test');const assert=require('node:assert/strict');
const {spawn}=require('node:child_process');const path=require('node:path');
const {createMessageConnection,StreamMessageReader,StreamMessageWriter}=require('vscode-jsonrpc/node');
test('real LSP initialize, document sync and completion',{timeout:10000},async()=>{
 const child=spawn(process.execPath,[path.join(__dirname,'../server/server.js'),'--stdio']);
 const connection=createMessageConnection(new StreamMessageReader(child.stdout),new StreamMessageWriter(child.stdin));connection.listen();
 try{
 const initialized=await connection.sendRequest('initialize',{processId:process.pid,rootUri:null,capabilities:{}});
 assert(initialized.capabilities.completionProvider);connection.sendNotification('initialized',{});
 connection.sendNotification('textDocument/didOpen',{textDocument:{uri:'file:///test.ls',languageId:'legacyscript',version:1,text:'self:'}});
 const result=await connection.sendRequest('textDocument/completion',{textDocument:{uri:'file:///test.ls'},position:{line:0,character:5}});
 assert(result.some(x=>x.label==='setFieldOfView'));
 connection.sendNotification('textDocument/didChange',{textDocument:{uri:'file:///test.ls',version:2},contentChanges:[{text:'Camera.'}]});
 const changed=await connection.sendRequest('textDocument/completion',{textDocument:{uri:'file:///test.ls'},position:{line:0,character:7}});
 assert(changed.some(x=>x.label==='getActive'));assert(!changed.some(x=>x.label==='setPosition'));
 await connection.sendRequest('shutdown');await connection.sendNotification('exit');
 await new Promise(resolve=>child.once('exit',resolve));
 }finally{connection.dispose();child.kill();}
});
