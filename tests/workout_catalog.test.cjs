const assert=require('node:assert/strict');
const {validateCatalog,generate}=require('../tools/generate-workouts.cjs');
const base={id:'custom',name:'Custom workout',description:'A custom profile',default_duration_minutes:12,default_intensity_percent:75,segments:[{weight:1,speed_mps:1,grade_percent:0},{weight:3,speed_mps:2,grade_percent:4}]};
assert.equal(validateCatalog([base])[0].segments.length,2);
assert.deepEqual(validateCatalog([]),[]); // A manual-only installation is valid.
for(const change of [
  {id:'manual'}, {id:'bad id'}, {default_duration_minutes:1.5}, {default_intensity_percent:0},
  {unexpected:true}, {segments:[]}, {segments:[{weight:0,speed_mps:1,grade_percent:0}]},
  {segments:[{weight:1,speed_mps:4,grade_percent:0}]},
  {segments:[{weight:1,speed_mps:1,grade_percent:7}]},
  {segments:[{weight:1,speed_mps:NaN,grade_percent:0}]}
])assert.throws(()=>validateCatalog([{...base,...change}]));
assert.throws(()=>validateCatalog([base,base]));
assert.throws(()=>validateCatalog([{...base,segments:Array(11).fill({weight:1000,speed_mps:1,grade_percent:0})}]));
generate(true);
console.log('Workout schema, limits, uniqueness, and generated catalog checks passed.');
