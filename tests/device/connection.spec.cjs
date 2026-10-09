const {test,expect,startWorkout,status}=require('./fixtures.cjs');
test('network loss faults workout and reconnect never resumes it',async({operator:page,context,request})=>{
  await startWorkout(page);
  await context.setOffline(true);
  try{
    await expect(page.locator('#connection')).toHaveText('Controller unavailable');
    await expect.poll(async()=> (await status(request)).mode).toBe('fault');
  }finally{await context.setOffline(false);}
  await expect(page.locator('#connection')).toHaveText(/^(Connected|Local preview connected)$/);
  await expect(page.locator('#mode')).toHaveText('fault');
  await expect(page.locator('#runToggle')).toBeDisabled();
  await expect(page.locator('#claim')).toBeEnabled();
  await page.locator('#claim').click();await page.locator('#reset').click();
  await expect(page.locator('#mode')).toHaveText('idle');
  expect((await status(request)).requested_speed_mps).toBe(0);
});
test('second browser cannot take over and updates its manual display',async({operator:page,browser})=>{
  await page.locator('#runToggle').click();
  const otherContext=await browser.newContext();
  try{
    const other=await otherContext.newPage();await other.goto(page.url());
    await expect(other.locator('#claim')).toHaveText('In use on another screen');
    await expect(other.locator('#claim')).toBeDisabled();
    await expect(other.locator('#runToggle')).toBeDisabled();
    await page.locator('#speedUp').click();
    await expect(other.locator('#acceptedSpeed')).toHaveText('0.5');
    await expect(other.locator('#speed')).toHaveValue('0.50');
  }finally{await otherContext.close();}
});
test('delayed status cannot overwrite a newer stop response',async({operator:page})=>{
  await startWorkout(page);
  let intercepted;
  const captured=new Promise(resolve=>intercepted=resolve);
  let release;
  const gate=new Promise(resolve=>release=resolve);
  await page.route('**/api/status',async route=>{
    const response=await route.fetch();intercepted();await gate;
    await route.fulfill({response});
  },{times:1});
  await captured;
  await page.locator('#runToggle').click();
  await expect(page.locator('#mode')).toHaveText('idle');
  release();
  // Observe each rendered mutation, including transient regressions.
  const modes=await page.evaluate(()=>new Promise(resolve=>{
    const values=[];const observer=new MutationObserver(()=>values.push(document.getElementById('mode').textContent));
    observer.observe(document.getElementById('mode'),{childList:true,subtree:true,characterData:true});
    setTimeout(()=>{observer.disconnect();resolve(values);},1200);
  }));
  expect(modes).not.toContain('running');
});
test('lost command reply leads to explicit recovery, never automatic restart',async({operator:page,request})=>{
  await page.route('**/api/test/command',async route=>{
    if(route.request().postData().includes(' w:')){await route.fetch();await route.abort();}
    else await route.continue();
  });
  await page.getByRole('button',{name:'Steady',exact:true}).click();
  await page.locator('#runToggle').click();
  await expect.poll(async()=> (await status(request)).mode).toBe('fault');
  await expect(page.locator('#runToggle')).toBeDisabled();
  await expect(page.locator('#message')).not.toContainText('Workout running');
});
