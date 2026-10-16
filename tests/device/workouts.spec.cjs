const {test,expect,startWorkout,status}=require('./fixtures.cjs');
test('refresh during workout settles safely and can regain control',async({operator:page})=>{
  await startWorkout(page);
  await page.reload();
  await expect(page.locator('#connection')).toHaveText(/^(Connected|Local preview connected)$/);
  await expect(page.locator('#mode')).not.toHaveText('running',{timeout:10000});
  await expect(page.locator('#workoutProgress')).not.toContainText('Ready');
  await expect(page.locator('#message')).not.toContainText('Workout running');
  await expect(page.locator('#claim')).toBeEnabled();
  await page.locator('#claim').click();
  if(await page.locator('#mode').textContent()==='fault')await page.locator('#reset').click();
  await expect(page.locator('#mode')).toHaveText('idle');
  await page.locator('#runToggle').click();
  await expect(page.locator('#mode')).toHaveText('running');
});
test('manual refresh never restarts motion',async({operator:page,request})=>{
  await page.locator('#runToggle').click();
  await page.locator('#speedUp').click();
  await expect(page.locator('#acceptedSpeed')).toHaveText('0.5');
  await page.reload();
  await expect.poll(async()=> (await status(request)).mode).not.toBe('running');
  expect((await status(request)).requested_speed_mps).toBe(0);
  await expect(page.locator('#runToggle')).toBeDisabled();
});
test('all catalog workouts appear with JSON defaults and profile',async({operator:page,request})=>{
  const catalog=await (await request.get('/api/workouts')).json();
  expect(catalog.length).toBeGreaterThan(0);
  for(const w of catalog){
    await page.getByRole('button',{name:w.name,exact:true}).click();
    await expect(page.locator('#duration')).toHaveValue(String(w.default_duration_minutes));
    await expect(page.locator('#intensity')).toHaveValue(String(w.default_intensity_percent));
    await expect(page.locator('#workoutDescription')).toContainText(w.description);
    await expect(page.locator('#profileLine')).toHaveAttribute('d',/^M/);
  }
});
test('segment transition replaces manual override',async({operator:page,request})=>{
  await startWorkout(page);
  await page.locator('#grade').fill('8');
  await expect(page.locator('#acceptedGrade')).toHaveText('8.0');
  await expect(page.locator('#workoutNext')).toContainText('Manual adjustment');
  await expect.poll(async()=> (await status(request)).workout.segment).toBe(1);
  await expect(page.locator('#acceptedGrade')).toHaveText('1.0');
  await expect(page.locator('#grade')).toHaveValue('1');
  await expect(page.locator('#workoutNext')).not.toContainText('Manual adjustment');
});
test('workout completes and clears targets',async({operator:page,request})=>{
  test.skip(process.env.NORDIC_LONG_TESTS!=='1','Full-duration device check is opt-in; C tests cover completion with explicit timestamps.');
  test.setTimeout(85000);
  await startWorkout(page,'Steady');
  await expect(page.locator('#workoutProgress')).toHaveText('Workout complete',{timeout:70000});
  await expect(page.locator('#mode')).toHaveText('idle');
  const s=await status(request);
  expect(s.requested_speed_mps).toBe(0);expect(s.requested_grade_percent).toBe(0);
  await expect(page.locator('#profilePosition')).toHaveText('Complete');
});
test('stop cancels preset and clears targets',async({operator:page,request})=>{
  await startWorkout(page);
  await page.locator('#runToggle').click();
  await expect(page.locator('#mode')).toHaveText('idle');
  const s=await status(request);
  expect(s.workout.active).toBe(false);expect(s.workout.complete).toBe(false);
  expect(s.requested_speed_mps).toBe(0);expect(s.requested_grade_percent).toBe(0);
});
test('invalid duration and intensity cannot start a workout',async({operator:page})=>{
  await page.getByRole('button',{name:'Steady',exact:true}).click();
  for(const [duration,intensity] of [['0','100'],['121','100'],['1.5','100'],['1','151'],['1','49']]){
    await page.locator('#duration').fill(duration);await page.locator('#intensity').fill(intensity);
    await page.locator('#runToggle').click();
    await expect(page.locator('#message')).toContainText('Choose 1–120');
    await expect(page.locator('#mode')).toHaveText('idle');
  }
});
test('countdown and profile advance between delayed device responses',async({operator:page})=>{
  await startWorkout(page,'Steady','2');
  await page.route('**/api/status',async route=>{
    const response=await route.fetch();
    await new Promise(resolve=>setTimeout(resolve,900));
    await route.fulfill({response});
  });
  const sample=await page.evaluate(()=>new Promise(resolve=>{
    const widths=[];const start=performance.now();
    const timer=setInterval(()=>{
      widths.push(Number(document.getElementById('profileClipRect').getAttribute('width')));
      if(performance.now()-start>=1100){clearInterval(timer);resolve(widths);}
    },80);
  }));
  expect(new Set(sample).size).toBeGreaterThan(4);
  for(let i=1;i<sample.length;i++)expect(sample[i]).toBeGreaterThanOrEqual(sample[i-1]);
  expect((sample.at(-1)-sample[0])*120000/800).toBeGreaterThan(600);
  expect((sample.at(-1)-sample[0])*120000/800).toBeLessThan(1800);
});
