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

test('audio methods and creation completion',()=>{
 const result=f.completion(...input('audio:'));for(const name of ['play','stop','pause','resume','setPitch','setVolume','setPan','setDistance','setLooping','isPlaying','playOneShot','fadeIn','fadeOut','mute','unmute','spatialBlend','setSpatialBlend'])assert(result.some(x=>x.label===name));
 assert(f.completion(...input('')).some(x=>x.label==='createAudioSource'));
});

test('Inspector exported field completion and hover',()=>{
 const text='@export target GameObject\n---@export speed number 5\n';
 const result=f.completion(...input(text));assert(result.some(x=>x.label==='target' && x.detail.includes('GameObject')));assert(result.some(x=>x.label==='speed'));
 assert.match(f.hover(...input(text+'target')).contents.value,/GameObject/);
});

test('input APIs are suggested with signatures',()=>{
 const result=f.completion(...input(''));
 for(const name of ['isKeyPressed','isKeyDown','isKeyReleased','isMouseButtonPressed','isMouseButtonDown','isMouseButtonReleased','getMousePosition','getMouseDelta','getMouseWheelDelta','getInputAxis','getInputVector','isGamepadButtonPressed','isGamepadButtonDown','getGamepadAxis','getGamepadStick','isButtonPressed','isButtonReleased','isButtonDown'])assert(result.some(x=>x.label===name && x.detail));
});

test('debug exposes only safe logging methods',()=>{
 assert.deepEqual(f.completion(...input('debug.')).map(x=>x.label).sort(),['error','log','warning']);
});

test('dynamic light APIs and properties are available',()=>{
 const globals=f.completion(...input(''));
 for(const name of ['DirectionalLight','PointLight','SpotLight'])assert(globals.some(x=>x.label===name));
 const fields=f.completion(...input('light.'));
 for(const name of ['color','intensity','enabled','shadows','range','innerAngle','outerAngle','shadowDistance','shadowBias','shadowQuality','setColor','setIntensity','setRange','setEnabled','setShadows','setShadowQuality','setSpotAngles'])assert(fields.some(x=>x.label===name));
});

test('cursor lock and visibility APIs have completions and signatures',()=>{
 const globals=f.completion(...input(''));
 for(const name of ['lockCursor','unlockCursor','hideCursor','showCursor','isCursorVisible','isCursorLocked']){
   assert(globals.some(x=>x.label===name));assert.match(f.hover(...input(name)).contents.value,/boolean/);
 }
});
test('material methods and export-only Texture completion',()=>{
 const result=f.completion(...input('local mesh = self:getComponent(MeshRenderer)\nmesh:'));
 for(const name of ['setMaterial','setMaterialColor','setMaterialTexture','setMaterialNormalMap','setMaterialShader','setMaterialTiling','resetMaterialOverrides'])assert(result.some(x=>x.label===name));
 assert.match(f.hover(...input('mesh:setMaterialColor')).contents.value,/r:number/);
 const exported=f.completion(...input('@export albedo Texture\n'));
 assert(exported.some(x=>x.label==='albedo'));
});

test('Skybox component scripting and HDR Texture export',()=>{
 const result=f.completion(...input('local sky=self:getComponent(Skybox)\nsky:'));
 for(const name of ['setType','setColor','setEnabled','setHDR','setFace','setTextures'])assert(result.some(x=>x.label===name));
 assert.match(f.hover(...input('sky:setTextures')).contents.value,/right:Texture/);
 assert(f.completion(...input('@export panorama Texture\n')).some(x=>x.label==='panorama'));
});
test('level transition globals have completion and hover',()=>{
 const result=f.completion(...input(''));
 for(const name of ['loadLevel','loadLevelAsync','unloadLevel','restartLevel','getCurrentLevel','setCurrentLevel','nextLevel','previousLevel']){
  assert(result.some(x=>x.label===name));assert(f.hover(...input(name)));
 }
});
test('custom events, level callbacks and skybox fades are discoverable',()=>{
 const result=f.completion(...input(''));
 for(const name of ['on','off','emit','onLevelLoaded','onLevelFailedToLoad','fadeSkybox']){
  assert(result.some(x=>x.label===name));assert(f.hover(...input(name)));
 }
 assert(f.completion(...input('self:getComponent(Skybox):')).some(x=>x.label==='fadeTo'));
});

test('activity, waits, mouse and audio events are discoverable',()=>{
 const globals=f.completion(...input(''));
 for(const name of ['wait','waitForSeconds','onMouseDown','onMouseUp','onMouseEnter','onMouseExit','onMouseOver','onAudioStarted','onAudioFinished','onAudioPaused','onAudioResumed','onAudioStopped'])assert(globals.some(x=>x.label===name));
 const methods=f.completion(...input('self:'));for(const name of ['setActive','isActive'])assert(methods.some(x=>x.label===name));
 const audio=f.completion(...input('local audio = self:getComponent(AudioSource)\naudio.'));assert(audio.some(x=>x.label==='onAudioFinished'));
});

test('prefab spawning, delayed destroy and cubemap assets have completion',()=>{
 const globals=f.completion(...input(''));
 for(const name of ['spawn','destroy'])assert(globals.some(x=>x.label===name));
 const methods=f.completion(...input('local sky = self:getComponent(Skybox)\nsky:'));
 assert(methods.some(x=>x.label==='setCubemap'));
});
