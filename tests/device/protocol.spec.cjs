const {test,expect,status}=require('./fixtures.cjs');
test('protocol rejects malformed, unowned, replayed, and out-of-range commands',async({request})=>{
  await expect.poll(async()=> (await status(request)).session_active,{timeout:10000}).toBe(false);
  const claim=await request.post('/api/test/claim',{headers:{'X-Nordic-Test':'1'}});
  expect(claim.status()).toBe(200);const {token}=await claim.json();let seq=0;
  const send=(action,speed=0,grade=0,n=++seq)=>request.post('/api/test/command',{
    headers:{'X-Nordic-Test':'1'},data:`${token} ${n} ${action} ${speed} ${grade}`});
  try{
    if((await status(request)).mode==='fault')expect((await send('reset')).status()).toBe(200);
    expect((await send('start')).status()).toBe(200);
    expect((await send('targets',2,3)).status()).toBe(200);
    expect((await send('stop',0,0,seq)).status()).toBe(409);
    expect((await send('targets',6,3)).status()).toBe(409);
    expect((await status(request)).requested_speed_mps).toBe(2);
    expect((await request.post('/api/test/claim',{headers:{'X-Nordic-Test':'1'}})).status()).toBe(409);
    expect((await request.post('/api/test/command',{data:'invalid'})).status()).toBe(400);
    expect((await request.post('/api/test/command',{headers:{'X-Nordic-Test':'1'},data:'f'.repeat(128)})).status()).toBe(400);
    expect((await request.post('/api/test/command',{headers:{'X-Nordic-Test':'1'},data:'0000000000000000 1 stop 0 0'})).status()).toBe(409);
  }finally{await send('stop');}
});
