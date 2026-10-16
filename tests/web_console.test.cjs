const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('firmware/components/web_console/index.html', 'utf8');
const script = html.split('<script>')[1].split('</script>')[0].replace(/loadWorkouts\(\);\s*$/, '');
for(const id of ['workoutSection','speedCaption','gradeCaption','usageHint'])assert.ok(html.includes('id="'+id+'"'),id+' must exist in the page');
const elements = new Map();
const makeElement=()=>({textContent:'',value:'0',disabled:false,addEventListener(){},setAttribute(){},appendChild(){}});
const document = { createElement:makeElement, hidden:false, addEventListener(){}, getElementById(id){
  if (!elements.has(id)) elements.set(id, makeElement());
  return elements.get(id);
}};
let browserNow=0;
const context = vm.createContext({document,AbortController,setTimeout,clearTimeout,setInterval:()=>0,performance:{now:()=>browserNow},console,
  fetch:async()=>{throw new Error('No network expected');}});
vm.runInContext(script, context);
vm.runInContext(`const realRender=render; render=next=>realRender({protocol_version:1,boot_id:1,revision:'1',control_epoch:1,owner_client_id:token?clientId:0,capabilities:{button_controls:false,calibrated_targets:true,workouts:true},...next});`,context);
const run = code => vm.runInContext(code, context);
run('setWorkoutCatalog('+JSON.stringify(fs.readdirSync('firmware/workouts').filter(f=>f.endsWith('.json')).sort().map(f=>JSON.parse(fs.readFileSync('firmware/workouts/'+f,'utf8'))))+')');
run('render({mode:"idle",software_test:true,session_active:false,sample_age_ms:0,uptime_ms:100,requested_speed_mps:0,requested_grade_percent:0,fault:0})');
assert.equal(elements.get('claim').disabled,false);
assert.equal(elements.get('runToggle').disabled,true);
run('token="123"; clientId=1;state.owner_client_id=1;state.session_active=true; controls()');
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
run('token="owner";clientId=1;state.owner_client_id=1;draftSpeedMps=4; render({...state,workout:{...state.workout,overridden:true}})');
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

run('render({...state,mode:"running",workout:{id:"hills",segment_count:10,intensity:1,active:true,complete:false,segment:0,elapsed_ms:1000,duration_ms:60000,segment_remaining_ms:5000,next_speed_mps:1.5,next_grade:1,overridden:false}})');
browserNow+=1000;
assert.equal(run('displayedWorkout().elapsed_ms'),2000);
browserNow+=4000;
assert.equal(run('displayedWorkout().elapsed_ms'),2500); // Freeze when confirmation is overdue.
run('render({...state,workout:{...state.workout,elapsed_ms:5900,segment_remaining_ms:100}})');
browserNow+=1000;
assert.equal(run('displayedWorkout().elapsed_ms'),6000); // Never invent the next segment.
run('unavailable()');
assert.equal(run('displayedWorkout().elapsed_ms'),5900); // Offline uses confirmed device data.
console.log('Display clock interpolation, stale-data bound, and segment boundary checks passed.');
run('render({mode:"idle",software_test:true,session_active:false,sample_age_ms:0,uptime_ms:100,requested_speed_mps:0,requested_grade_percent:0,fault:0,physical_outputs:true,relays:{enabled:true,known:true,mask:1,start:1,stop:6,speed_up:2,speed_down:3,incline_up:4,incline_down:5,pulse_ms:500}})');
assert.match(elements.get('outputNotice').textContent,/Physical relays enabled/);
assert.match(elements.get('driverDetail').textContent,/Start CH1; Stop CH6/);
assert.match(elements.get('driverDetail').textContent,/speed.*CH2\/3/);
assert.match(elements.get('driverDetail').textContent,/no movement or contact position is measured/);
console.log('Physical relay mode and configurable mapping display checks passed.');

run('token="viewer";clientId=2;render({...state,mode:"running",session_active:true,owner_client_id:1,owner_transport:3,revision:"10",capabilities:{button_controls:true,calibrated_targets:false,workouts:false}})');
assert.equal(elements.get('speedUp').disabled,true);
assert.ok(!html.includes('id="stopAll"'));
assert.ok(!html.includes('id="release"'));
assert.equal(elements.get('runToggle').disabled,false);
assert.equal(elements.get('acceptedSpeed').textContent,'0.0');
assert.equal(elements.get('acceptedGrade').textContent,'0.0');
assert.equal(elements.get('speed').disabled,true); // Viewer cannot edit.
assert.ok(!elements.get('workoutSection')?.hidden);
assert.match(elements.get('claim').textContent,/In use on another screen/);
run('render({...state,revision:"9",mode:"idle"})');
assert.equal(run('state.mode'),'running');
run('render({...state,revision:"11",mode:"idle",control_epoch:2})');
assert.equal(run('state.mode'),'idle');
run('render({...state,boot_id:2,revision:"1"})');
assert.equal(run('token'),null);
run('token="owner";clientId=1;render({...state,mode:"running",owner_client_id:1,requested_speed_mps:2,requested_grade_percent:3,capabilities:{button_controls:true,numeric_targets:true,calibrated_targets:false,workouts:true}})');
assert.equal(elements.get('speed').disabled,false);
assert.equal(elements.get('speedSlider').disabled,false);
assert.equal(elements.get('grade').disabled,false);
assert.equal(elements.get('acceptedSpeed').textContent,'7.2');
assert.equal(elements.get('acceptedGrade').textContent,'3.0');
console.log('Viewer Stop, restored physical settings, stale revisions, and reboot session reset passed.');
