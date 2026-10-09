const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const readline = require('node:readline');
const root = path.resolve(__dirname, '..');
const controller = spawn(path.join(root,'.local','preview-controller'), [], {stdio:['pipe','pipe','inherit']});
let waiting = null, dead = false, queue = Promise.resolve();
const fail = () => {dead=true;if(waiting){waiting.reject(new Error('Controller stopped'));waiting=null;}};
controller.on('error',fail);controller.on('exit',fail);
readline.createInterface({input:controller.stdout}).on('line',line=>{
  if(!waiting)return;
  const pending=waiting;waiting=null;
  try{pending.resolve(JSON.parse(line));}catch(error){pending.reject(error);}
});
function command(line){
  const result=queue.then(()=>new Promise((resolve,reject)=>{
    if(dead)return reject(new Error('Controller unavailable'));
    waiting={resolve,reject};controller.stdin.write(line+'\n');
  }));
  queue=result.catch(()=>{});return result;
}
const files={
  '/':['index.html','text/html; charset=utf-8'],
  '/assets/logo.svg':['assets/logo.svg','image/svg+xml'],
  '/assets/hero.jpg':['assets/hero.jpg','image/jpeg']
};
function reply(res,code,body,type='text/plain'){
  res.writeHead(code,{'Content-Type':type,'Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});
  res.end(body);
}
const server=http.createServer(async(req,res)=>{
  if(!['127.0.0.1:8766','localhost:8766'].includes(req.headers.host))return reply(res,403,'Local preview only');
  try{
    if(req.method==='GET'&&files[req.url]){
      const [name,type]=files[req.url];
      return reply(res,200,fs.readFileSync(path.join(root,'firmware/components/web_console',name)),type);
    }
    if(req.method==='GET'&&req.url==='/api/workouts')
      return reply(res,200,JSON.stringify(await command('workouts')),'application/json');
    if(req.method==='GET'&&req.url==='/api/status')
      return reply(res,200,JSON.stringify(await command('status')),'application/json');
    if(req.method!=='POST'||req.headers['x-nordic-test']!=='1')return reply(res,404,'Not found');
    let body='';
    for await (const chunk of req){body+=chunk.toString('utf8');if(body.length>127)return reply(res,400,'Command too large');}
    if(req.url==='/api/test/claim'&&!body){
      const token=crypto.randomBytes(8).toString('hex');
      const result=await command('claim '+token);
      return result.ok?reply(res,200,JSON.stringify({token}),'application/json'):reply(res,409,'Another page owns control. Wait three seconds after closing it.');
    }
    if(req.url==='/api/test/command'){
      const fields=body.split(' ');
      if(fields.length!==5||!/^[0-9a-f]{16}$/.test(fields[0])||!/^\d{1,10}$/.test(fields[1])||
         Number(fields[1])<1||Number(fields[1])>0xffffffff||
         (!['start','stop','targets','heartbeat','reset'].includes(fields[2])&&!/^w:[a-z][a-z0-9-]{0,23}$/.test(fields[2]))||
         !fields.slice(3).every(v=>/^-?(?:\d+\.?\d*|\.\d+)$/.test(v)&&Number.isFinite(Number(v))))
        return reply(res,400,'Malformed command');
      const result=await command('command '+body);
      return result.ok?reply(res,200,JSON.stringify(result),'application/json'):reply(res,409,'Command rejected: check session, state, and limits');
    }
    reply(res,404,'Not found');
  }catch(error){reply(res,503,'Local controller unavailable');}
});
server.requestTimeout=3000;
server.listen(8766,'127.0.0.1',()=>console.log('Interactive local preview: http://127.0.0.1:8766/ (no hardware connection)'));
for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>{controller.kill();server.close(()=>process.exit(0));});
