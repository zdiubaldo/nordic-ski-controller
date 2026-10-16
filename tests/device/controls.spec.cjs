const {test,expect,status}=require('./fixtures.cjs');
test('manual controls preserve speed across unit changes',async({operator:page,request})=>{
  await page.locator('#runToggle').click();
  await page.locator('#speed').fill('7.2');await page.locator('#grade').fill('2');
  await expect(page.locator('#acceptedSpeed')).toHaveText('7.2');
  await page.locator('#mph').click();await expect(page.locator('#acceptedSpeed')).toHaveText('4.5');
  await page.locator('#kmh').click();await expect(page.locator('#speed')).toHaveValue('7.20');
  expect((await status(request)).requested_speed_mps).toBeCloseTo(2,2);
});
test('stop takes priority over rapid pending target changes',async({operator:page,request})=>{
  await page.locator('#runToggle').click();
  await page.locator('#speed').fill('10');await page.locator('#grade').fill('5');
  await page.locator('#runToggle').click();
  await expect(page.locator('#mode')).toHaveText('idle');
  expect((await status(request)).requested_speed_mps).toBe(0);
  await expect(page.locator('#speed')).toHaveValue('0.00');
});
test('heartbeat polling does not blink or disable Stop',async({operator:page})=>{
  await page.locator('#runToggle').click();
  const disabled=await page.locator('#runToggle').evaluate(button=>new Promise(resolve=>{
    let seen=false;const observer=new MutationObserver(()=>{seen||=button.disabled;});
    observer.observe(button,{attributes:true,attributeFilter:['disabled']});
    setTimeout(()=>{observer.disconnect();resolve(seen);},2000);
  }));
  expect(disabled).toBe(false);
});
for(const size of [{width:1180,height:820},{width:820,height:1180},{width:390,height:844}]){
  test(`controls fit ${size.width}x${size.height}`,async({operator:page})=>{
    await page.setViewportSize(size);
    await page.getByRole('button',{name:'Rolling Hills',exact:true}).click();
    expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true);
    await expect(page.locator('#runToggle')).toBeInViewport();
    await page.locator('#inclineChart').scrollIntoViewIfNeeded();
    await expect(page.locator('#inclineChart')).toBeVisible();
    const chart=await page.locator('#profileLine').getAttribute('d');expect(chart).not.toMatch(/NaN|Infinity/);
    await page.getByRole('button',{name:'Heaven’s Gate · Breckenridge',exact:true}).click();
    await expect(page.locator('#workoutDescription')).toContainText('Estimated incline');
  });
}
