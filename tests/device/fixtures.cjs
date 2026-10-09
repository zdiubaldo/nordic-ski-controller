const {test:base,expect}=require('@playwright/test');
const test=base.extend({
  deviceReady:[async({request},use)=>{
    const response=await request.get('/api/status',{timeout:10000});
    expect(response.ok(),'ESP32 must be reachable').toBeTruthy();
    const state=await response.json();
    if(process.env.NORDIC_ALLOW_PREVIEW!=='1')expect(state.local_preview,'Must run against real ESP32, not localhost preview').not.toBe(true);
    expect(state.software_test,'Requires software-only firmware').toBe(true);
    if(state.local_preview)expect(state.physical_outputs).toBe(false);
    else {expect(state.commissioned).toBe(false);expect(state.motion_available).toBe(false);expect(state.hardware_verified).toBe(false);}
    await use();
  },{auto:true}],
  operator:async({page},use)=>{
    await page.goto('/');
    await expect(page.locator('#connection')).toHaveText(/^(Connected|Local preview connected)$/);
    await expect(page.locator('#claim'),'Close other controlling tablet/browser pages before running the suite').toBeEnabled();
    await page.locator('#claim').click();
    await expect(page.locator('#claim')).toHaveText('Controls enabled');
    if(await page.locator('#mode').textContent()==='fault')await page.locator('#reset').click();
    await expect(page.locator('#mode')).toHaveText('idle');
    const errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await use(page);
    // Best-effort stop; closing the context also removes heartbeats. Never auto-resume.
    await page.unrouteAll({behavior:'ignoreErrors'});
    if(!page.isClosed()&&await page.locator('#mode').textContent().catch(()=>null)==='running')
      await page.locator('#runToggle').click({timeout:2000}).catch(()=>{});
    await page.close();
    expect(errors,'Uncaught page errors').toEqual([]);
  }
});
async function startWorkout(page,name='Rolling Hills',minutes='1'){
  await page.getByRole('button',{name,exact:true}).click();
  await page.locator('#duration').fill(minutes);
  await page.locator('#runToggle').click();
  await expect(page.locator('#mode')).toHaveText('running');
}
async function status(request){const r=await request.get('/api/status');expect(r.ok()).toBeTruthy();return r.json();}
module.exports={test,expect,startWorkout,status};
