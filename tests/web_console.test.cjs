const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('firmware/components/web_console/index.html', 'utf8');
const script = html.split('<script>')[1].split('</script>')[0].replace(/poll\(\);\s*$/, '');
const elements = new Map();
const document = { hidden:false, addEventListener(){}, getElementById(id){
  if (!elements.has(id)) elements.set(id, {textContent:'',value:'0',disabled:false,addEventListener(){},setAttribute(){}});
  return elements.get(id);
}};
const context = vm.createContext({document,AbortController,setTimeout,clearTimeout,console,
  fetch:async()=>{throw new Error('No network expected');}});
vm.runInContext(script, context);
const run = code => vm.runInContext(code, context);
run('render({mode:"idle",software_test:true,session_active:false,sample_age_ms:0,uptime_ms:100,requested_speed_mps:0,requested_grade_percent:0,fault:0})');
assert.equal(elements.get('claim').disabled,false);
assert.equal(elements.get('start').disabled,true);
run('token="123"; state.session_active=true; controls()');
assert.equal(elements.get('start').disabled,false);
run('state.mode="running";controls()');
assert.equal(elements.get('apply').disabled,false);
run('unavailable()');
assert.equal(elements.get('apply').disabled,true);
assert.equal(run('token'),null);
run('render({mode:"fault",software_test:true,session_active:false,sample_age_ms:0,uptime_ms:100,requested_speed_mps:0,requested_grade_percent:0,fault:1})');
assert.equal(elements.get('start').disabled,true);
assert.equal(elements.get('claim').disabled,false);
assert.equal(run('token'),null); // Reconnection must not reclaim or restart.
assert.throws(()=>run('render({...state,sample_age_ms:1001})'),/Stale status/);
run('render({...state,software_test:false,sample_age_ms:0})');
assert.equal(elements.get('claim').disabled,true);
run('draftSpeedMps=2;chooseUnit("mph");chooseUnit("kmh")');
assert.equal(run('draftSpeedMps'),2);
assert.equal(elements.get('speed').value,'7.20');
run('draftSpeedMps=5;adjustSpeed(0.5)');
assert.equal(run('draftSpeedMps'),5);
console.log('Browser control availability and reconnect checks passed.');
