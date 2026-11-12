const {createConnection,ProposedFeatures,TextDocuments,TextDocumentSyncKind}=require('vscode-languageserver/node');
const {TextDocument}=require('vscode-languageserver-textdocument');
const features=require('./features');
const connection=createConnection(ProposedFeatures.all);
const documents=new TextDocuments(TextDocument);
connection.onInitialize(()=>({capabilities:{textDocumentSync:TextDocumentSyncKind.Incremental,completionProvider:{triggerCharacters:['.',':']},hoverProvider:true,signatureHelpProvider:{triggerCharacters:['(',',']},documentSymbolProvider:true}}));
for(const [handler,feature] of [['onCompletion','completion'],['onHover','hover'],['onSignatureHelp','signatureHelp'],['onDocumentSymbol','symbols']]){
  connection[handler](params=>{const doc=documents.get(params.textDocument.uri);return doc?features[feature](doc,params.position):null;});
}
documents.listen(connection);connection.listen();
