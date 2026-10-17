const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const ts=require('typescript');
function load(file,imports={}) {
  const code=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText;
  const exports={};vm.runInNewContext(code,{exports,require:id=>imports[id],setTimeout,Date});return exports;
}
const shared=load('../packages/controller-client/src/index.ts');
const {WebBridge}=load('src/web-bridge.ts',{'@nordic/controller-client':shared});
function setup() {
  const sent=[];
  const client={closed:false,token:'1234567890abcdef',clientId:1,epoch:2,
    state:{protocol_version:1,boot_id:5,revision:'10',control_epoch:2,owner_client_id:0,mode:'idle',fault:0},
    invalidate(){this.closed=true;},
    async command(action,a,b){sent.push([action,a,b]);setTimeout(()=>{this.state={...this.state,revision:'11',mode:action==='start'?'running':'idle'};},20);return {ok:true,revision:'11'};}};
  const bridge=new WebBridge(client,[{id:'steady'}]);
  const request=(body)=>bridge.handle({type:'request',id:1,path:'/api/test/command',method:'POST',body});
  return {client,bridge,request,sent};
}
test('website session and catalog use the native BLE session without claiming control',async()=>{
  const {bridge,sent}=setup();
  const session=await bridge.handle({type:'request',id:1,path:'/api/session',method:'POST'});
  assert.equal(session.body.token,'1234567890abcdef');assert.equal(session.body.client_id,1);
  const catalog=await bridge.handle({type:'request',id:2,path:'/api/workouts',method:'GET'});
  assert.equal(catalog.body[0].id,'steady');assert.equal(sent.length,0);
});
test('website commands wait for the confirmed status before returning HTTP-shaped replies',async()=>{
  const {request,sent}=setup();
  const reply=await request('v1 1234567890abcdef 1 2 start 0 0');
  assert.equal(reply.status,200);assert.equal(reply.body.mode,'running');assert.equal(reply.body.revision,'11');
  assert.deepEqual(sent,[['start',0,0]]);
});
test('stale movement is rejected; Stop remains available across epoch changes',async()=>{
  const {request,sent}=setup();
  const stale=await request('v1 1234567890abcdef 1 1 start 0 0');
  assert.equal(stale.status,409);assert.equal(sent.length,0);
  assert.equal((await request('v1 1234567890abcdef 2 1 stop 0 0')).status,200);
  assert.equal(sent[0][0],'stop');
});
test('foreign tokens, repeated sequences and unknown routes never reach BLE',async()=>{
  const {request,bridge,sent}=setup();
  await assert.rejects(request('v1 aaaaaaaaaaaaaaaa 1 2 start 0 0'),/Invalid/);
  await request('v1 1234567890abcdef 1 2 stop 0 0');
  await assert.rejects(request('v1 1234567890abcdef 1 2 start 0 0'),/Invalid/);
  await assert.rejects(bridge.handle({type:'request',id:2,path:'https://example.com',method:'GET'}),/Unsupported/);
  assert.equal(sent.length,1);
});
test('closed sessions and ambiguous failures cannot be reused',async()=>{
  const {client,request}=setup();client.command=async()=>{client.closed=true;throw new Error('unknown outcome');};
  await assert.rejects(request('v1 1234567890abcdef 1 2 start 0 0'),/unknown outcome/);
  await assert.rejects(request('v1 1234567890abcdef 2 2 start 0 0'),/disconnected/);
});
test('page fetch bridge resolves replies and cancels timed-out requests without network fallback',async()=>{
  const messages=[];const window={ReactNativeWebView:{postMessage:raw=>messages.push(JSON.parse(raw))}};
  vm.runInNewContext(fs.readFileSync('src/web-fetch.js','utf8'),{window,Map,JSON,Error,Object});
  const pending=window.fetch('/api/status');
  window.__nordicReply({id:messages[0].id,status:200,body:{mode:'idle'}});
  assert.equal(JSON.parse(await (await pending).text()).mode,'idle');
  await assert.rejects(window.fetch('https://example.com'),/Unsupported/);
  const abort=new AbortController();const cancelled=window.fetch('/api/status',{signal:abort.signal});abort.abort();
  await assert.rejects(cancelled,/cancelled/);assert.equal(messages.at(-1).type,'cancel');
});
test('app bundles the original website styles, controls, scripts, and artwork',()=>{
  require('../scripts/bundle-web.cjs');
  const page=load('src/generated/controller-page.ts').controllerPage;
  const original=fs.readFileSync('../firmware/components/web_console/index.html','utf8');
  assert.ok(page.includes(original.match(/<style>[\s\S]*?<\/style>/)[0]));
  assert.ok(page.includes(original.slice(original.lastIndexOf('<script>')).replace('timer=setTimeout(()=>abort.abort(),2000)','timer=setTimeout(()=>abort.abort(),15000)')));
  for (const [name,mime] of [['logo.svg','image/svg+xml'],['hero.jpg','image/jpeg']]) {
    const encoded=fs.readFileSync('../firmware/components/web_console/assets/'+name).toString('base64');
    assert.ok(page.includes('data:'+mime+';base64,'+encoded));
  }
  assert.equal(page.includes('/assets/'),false);
  assert.ok(page.includes("connect-src 'none'"));
});
