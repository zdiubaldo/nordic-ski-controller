const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('typescript');
function load(file, imports) {
  const code = ts.transpileModule(fs.readFileSync(file, 'utf8'), {compilerOptions:{module:ts.ModuleKind.CommonJS, target:ts.ScriptTarget.ES2022, esModuleInterop:true}}).outputText;
  const exports = {};
  vm.runInNewContext(code, {exports, require:id => imports[id], setTimeout, clearTimeout, Date, console});
  return exports;
}
const shared = load('../packages/controller-client/src/index.ts', {});
function setup(platform = 'android') {
  const calls = [], listeners = {};
  const ble = {
    start:async()=>{}, checkState:async()=> 'on', scan:async()=>{},
    connect:async()=>calls.push('connect'), disconnect:async()=>calls.push('disconnect'),
    retrieveServices:async()=>calls.push('services'),
    read:async()=>[...Buffer.from(JSON.stringify({protocol_version:1, framing:1, boot_id:42}))],
    createBond:async()=>calls.push('bond'),
    onDisconnectPeripheral:handler=> {listeners.disconnect = handler; return {remove(){delete listeners.disconnect;}};},
    onDidUpdateValueForCharacteristic:handler=> {listeners.notify = handler; return {remove(){delete listeners.notify;}};},
    startNotification:async(_id,_service,char)=>calls.push(char),
    write:async(_id,_service,_char,packet,size)=>{calls.push({packet,size});},
  };
  const {BleTransport} = load('src/ble.ts', {'react-native':{Platform:{OS:platform}}, 'react-native-ble-manager':ble, '@nordic/controller-client':shared});
  const t = new BleTransport('opaque-ios-compatible-id');
  function notify(kind,id,value) {
    const data = [...Buffer.from(JSON.stringify(value))];
    for (let offset=0;offset<data.length;offset+=12) listeners.notify({peripheral:t.id, service:shared.UUID[0], characteristic:shared.UUID[kind+1], value:[1,kind,id&255,id>>8,data.length&255,data.length>>8,offset&255,offset>>8,...data.slice(offset,offset+12)]});
  }
  return {t, ble, calls, listeners, notify};
}
test('native Android path bonds, subscribes, frames writes, and matches application results', async()=>{
  const {t,calls,notify} = setup(); await t.open();
  assert.deepEqual(calls.slice(0,3), ['connect','services','bond']);
  assert.equal(calls.length,6);
  const response = t.request('connect');
  await new Promise(resolve=>setImmediate(resolve));
  const write = calls.find(c=>c.packet);
  assert.equal(write.size,20); assert.equal(write.packet[1],1);
  notify(2,1,{ok:true,client_id:1});
  assert.equal((await response).client_id,1); await t.close();
});
test('iOS path uses opaque identifiers without Android bonding', async()=>{
  const {t,calls} = setup('ios'); await t.open(); assert.equal(calls.includes('bond'),false); await t.close();
});
test('link loss rejects outstanding commands and removes listeners', async()=>{
  const {t,listeners} = setup(); await t.open();
  const pending = t.request('connect');
  listeners.disconnect({peripheral:t.id});
  await assert.rejects(pending,/disconnected/); assert.equal(t.closed,true); assert.equal(listeners.notify,undefined);
});
test('controller boot change closes the session before delivering status', async()=>{
  const {t,notify} = setup(); await t.open();
  let received=false; t.onState=()=>{received=true;};
  notify(3,1,{boot_id:43}); assert.equal(received,false); assert.equal(t.closed,true);
});
test('ambiguous native write failure is not retried', async()=>{
  const {t,ble} = setup(); await t.open(); let writes=0;
  ble.write=async()=>{writes++; throw new Error('GATT failure');};
  await assert.rejects(t.request('connect'),/write failed/); assert.equal(writes,1); await t.close();
});
