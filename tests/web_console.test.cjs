const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('firmware/components/web_console/index.html', 'utf8');
const script = html.split('<script>')[1].split('</script>')[0].replace(/loadWorkouts\(\);\s*$/, '');
const elements = new Map();
const makeElement=()=>({textContent:'',value:'0',disabled:false,addEventListener(){},setAttribute(){},appendChild(){}});
const document = { createElement:makeElement, hidden:false, addEventListener(){}, getElementById(id){
  if (!elements.has(id)) elements.set(id, makeElement());
  return elements.get(id);
}};
const context = vm.createContext({document,AbortController,setTimeout,clearTimeout,console,
  fetch:async()=>{throw new Error('No network expected');}});
vm.runInContext(script, context);
const run = code => vm.runInContext(code, context);
run('setWorkoutCatalog('+JSON.stringify(fs.readdirSync('firmware/workouts').filter(f=>f.endsWith('.json')).sort().map(f=>JSON.parse(fs.readFileSync('firmware/workouts/'+f,'utf8'))))+')');
run('render({mode:"idle",software_test:true,session_active:false,sample_age_ms:0,uptime_ms:100,requested_speed_mps:0,requested_grade_percent:0,fault:0})');
assert.equal(elements.get('claim').disabled,false);
assert.equal(elements.get('runToggle').disabled,true);
run('token="123"; state.session_active=true; controls()');
assert.equal(elements.get('runToggle').disabled,false);
run('busy=true;foregroundBusy=false;controls()');
assert.equal(elements.get('runToggle').disabled,false); // Heartbeats must not blink Start.
run('busy=false;state.mode="running";controls()');
assert.equal(elements.get('speedUp').disabled,false);
assert.equal(elements.get('runToggle').textContent,'■ Stop');
assert.equal(elements.get('runToggle').className,'primary stop');
run('unavailable()');
assert.equal(elements.get('speedUp').disabled,true);
assert.equal(run('token'),null);
assert.equal(elements.get('runToggle').disabled,true);
assert.equal(elements.get('runToggle').textContent,'Unavailable');
run('render({mode:"fault",software_test:true,session_active:false,sample_age_ms:0,uptime_ms:100,requested_speed_mps:0,requested_grade_percent:0,fault:1})');
assert.equal(elements.get('runToggle').disabled,true);
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
run('render({mode:"running",software_test:true,session_active:true,sample_age_ms:0,uptime_ms:100,requested_speed_mps:2,requested_grade_percent:3,fault:0,workout:{id:"hills",segment_count:10,intensity:1,active:true,complete:false,segment:2,elapsed_ms:12000,duration_ms:60000,segment_remaining_ms:6000,next_speed_mps:2,next_grade:6,overridden:false}})');
assert.equal(run('selectedWorkout'),'hills');
assert.equal(run('draftSpeedMps'),2);
assert.equal(elements.get('duration').disabled,true);
assert.match(elements.get('workoutProgress').textContent,/Segment 3 \/ 10/);
run('draftSpeedMps=4; render({...state,workout:{...state.workout,overridden:true}})');
assert.equal(run('draftSpeedMps'),4); // Polling preserves current manual adjustment.
run('render({...state,requested_speed_mps:1.5,workout:{...state.workout,segment:3,overridden:false}})');
assert.equal(run('draftSpeedMps'),1.5); // New segment replaces manual draft.
run('render({...state,mode:"idle",requested_speed_mps:0,requested_grade_percent:0,workout:{...state.workout,active:false,complete:true,elapsed_ms:60000}})');
assert.equal(run('draftSpeedMps'),0);
assert.equal(elements.get('workoutProgress').textContent,'Workout complete');
assert.equal(elements.get('duration').disabled,false);
console.log('Workout segment synchronization and completion checks passed.');
run('setWorkoutCatalog([{id:"custom",name:"Custom Climb",description:"From JSON",default_duration_minutes:12,default_intensity_percent:75,segments:[{},{}]}]);chooseWorkout("custom")');
assert.equal(run('selectedWorkout'),'custom');
assert.equal(Number(elements.get('duration').value),12);
assert.equal(Number(elements.get('intensity').value),75);
assert.match(elements.get('workoutDescription').textContent,/From JSON 2 segments/);
assert.equal(run('workoutButtons.get("custom").textContent'),'Custom Climb');
console.log('Custom catalog renders without preset-specific UI code.');
