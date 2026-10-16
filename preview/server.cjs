const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const readline = require('node:readline');
const root = path.resolve(__dirname, '..');
const controller = spawn(path.join(root,'.local','preview-controller'), process.env.NORDIC_BUTTON_PREVIEW==='1'?['--buttons']:[], {stdio:['pipe','pipe','inherit']});
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
    if(req.method!=='POST')return reply(res,404,'Not found');
    if(req.headers['x-nordic-test']!=='1')return reply(res,400,'Invalid command header');
    let body='';
    for await (const chunk of req){body+=chunk.toString('utf8');if(body.length>159)return reply(res,400,'Command too large');}
    if(['/api/test/claim','/api/session'].includes(req.url)&&!body){
      const token=crypto.randomBytes(8).toString('hex');
      const result=await command((req.url==='/api/session'?'register ':'claim ')+token);
      return reply(res,result.ok?200:409,JSON.stringify(result.ok?
        {protocol_version:1,token,client_id:result.command_result.client_id,control_epoch:result.control_epoch}:result),'application/json');
    }
    if(req.url==='/api/test/command'){
      if(!/^v1 [0-9a-f]{16} [0-9]{1,10} [0-9]{1,10} [a-z_:0-9-]+ -?(?:[0-9]+\.?[0-9]*|\.[0-9]+) -?(?:[0-9]+\.?[0-9]*|\.[0-9]+)$/.test(body))
        return reply(res,400,'Malformed v1 command');
      const result=await command('command '+body);
      return reply(res,result.ok?200:409,JSON.stringify(result),'application/json');
    }
    reply(res,404,'Not found');
  }catch(error){reply(res,503,'Local controller unavailable');}
});
server.requestTimeout=3000;
server.listen(8766,'127.0.0.1',()=>console.log('Interactive local preview: http://127.0.0.1:8766/ (no hardware connection)'));
for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>{controller.kill();server.close(()=>process.exit(0));});
