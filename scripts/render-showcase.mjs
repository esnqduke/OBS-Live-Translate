// Development-only: capture the real bundled renderer with sample transcript data.
// Requires Node.js and Chrome; neither is required by the installed OBS plugin.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';

const root=path.resolve('data/overlay'),media=path.resolve('docs/media');
const work=path.resolve('.work/showcase');
fs.mkdirSync(media,{recursive:true});fs.mkdirSync(work,{recursive:true});
const text='Your voice. Live subtitles. Powered by local Whisper. Made for OBS.';
const mime={'.html':'text/html','.css':'text/css','.js':'text/javascript','.mjs':'application/javascript'};
const acknowledged=new Set();
const server=http.createServer((request,response)=>{
  const [session,file='index.html']=new URL(request.url,'http://localhost').pathname.split('/').filter(Boolean);
  response.setHeader('Cache-Control','no-store');
  response.setHeader('Content-Security-Policy',"default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; font-src 'self'; base-uri 'none'; frame-ancestors 'none'");
  if(file==='state'){
    const style=session.replace('-demo','');
    response.setHeader('Content-Type','application/json');
    response.end(JSON.stringify({session,active:true,config:{style,background:'auto'},phrases:acknowledged.has(session)?[]:[{id:'1',revision:1,text,final:true}],preview:{serial:0,text:''},error:''}));return;
  }
  if(file==='ack'){
    let body='';request.on('data',chunk=>body+=chunk);request.on('end',()=>{const value=JSON.parse(body);if(value.session===session&&value.ids?.some(p=>p.id==='1'&&p.revision===1))acknowledged.add(session);response.end('{}');});return;
  }
  if(!['index.html','overlay.css','overlay.js','model.mjs'].includes(file)){response.writeHead(404);response.end();return;}
  response.setHeader('Content-Type',mime[path.extname(file)]);response.end(fs.readFileSync(path.join(root,file)));
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const base=`http://127.0.0.1:${server.address().port}`;
const chrome=spawn(process.env.CHROME_PATH||'C:/Program Files/Google/Chrome/Application/chrome.exe',[
  '--headless=new','--no-sandbox','--disable-gpu','--no-first-run','--no-default-browser-check',
  '--disable-background-timer-throttling','--remote-debugging-pipe',`--user-data-dir=${path.join(work,'chrome')}`
],{stdio:['ignore','ignore','pipe','pipe','pipe'],windowsHide:true});
let sequence=0,buffer='',failure=null;const pending=new Map();
chrome.on('error',error=>{failure=error;for(const request of pending.values())request.reject(error);pending.clear();});
chrome.stdio[4].on('data',chunk=>{buffer+=chunk;let boundary;while((boundary=buffer.indexOf('\0'))>=0){const message=JSON.parse(buffer.slice(0,boundary));buffer=buffer.slice(boundary+1);if(message.id){const request=pending.get(message.id);pending.delete(message.id);message.error?request.reject(Error(JSON.stringify(message.error))):request.resolve(message.result);}}});
function command(method,params={},sessionId){return new Promise((resolve,reject)=>{if(failure){reject(failure);return;}const id=++sequence;const timer=setTimeout(()=>{pending.delete(id);reject(Error(`Chrome command timed out: ${method}`));},10000);pending.set(id,{resolve:value=>{clearTimeout(timer);resolve(value)},reject:error=>{clearTimeout(timer);reject(error)}});chrome.stdio[3].write(JSON.stringify({id,method,params,...sessionId?{sessionId}:{}})+'\0');});}
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
try{
  await command('Browser.getVersion');
  const {targetId}=await command('Target.createTarget',{url:'about:blank'});
  const {sessionId}=await command('Target.attachToTarget',{targetId,flatten:true});
  await command('Page.enable',{},sessionId);
  await command('Emulation.setDeviceMetricsOverride',{width:960,height:240,deviceScaleFactor:1,mobile:false},sessionId);
  const navigate=async style=>{
    acknowledged.delete(style);
    await command('Page.navigate',{url:`${base}/${style}/index.html`},sessionId);
    for(let attempts=0;attempts<50;attempts++){
      const {result}=await command('Runtime.evaluate',{expression:`!!document.querySelector('.row')`,returnByValue:true},sessionId);
      if(result.value)break;await sleep(20);
    }
    await command('Runtime.evaluate',{expression:`document.body.style.backgroundColor='#07110f'`},sessionId);
  };
  const capture=async filename=>{
    const {data}=await command('Page.captureScreenshot',{format:'png',captureBeyondViewport:false},sessionId);
    fs.writeFileSync(filename,Buffer.from(data,'base64'));
  };
  for(const style of ['matrix','console','cyber','matrix_rain','fantasy','cathay']){
    await navigate(style);await sleep(1100);await capture(path.join(media,`${style}.png`));
  }
  // Control only the clock, so screenshot overhead cannot speed up the GIF.
  // Production Canvas drawing, row timing, easing and keyframes remain unchanged.
  await command('Page.addScriptToEvaluateOnNewDocument',{source:`
    let clock=1000,queued=[];const animations=[];
    Object.defineProperty(performance,'now',{value:()=>clock});
    window.requestAnimationFrame=callback=>{queued.push(callback);return queued.length};
    const animate=Element.prototype.animate;
    Element.prototype.animate=function(){const animation=animate.apply(this,arguments);animation.pause();animation.currentTime=0;animations.push({animation,start:clock});return animation};
    window.__advance=ms=>{for(let n=0;n<ms;n+=10){clock+=Math.min(10,ms-n);const callbacks=queued;queued=[];callbacks.forEach(fn=>fn(clock));for(const item of animations)item.animation.currentTime=clock-item.start;}};
  `},sessionId);
  for(const [style,count]of [['matrix',120],['matrix_rain',180]]){
    const directory=path.join(work,style);fs.mkdirSync(directory,{recursive:true});
    await navigate(`${style}-demo`);
    await command('Runtime.evaluate',{expression:'__advance(10)'},sessionId);
    for(let frame=0;frame<count;frame++){
      if(frame)await command('Runtime.evaluate',{expression:'__advance(50)'},sessionId);
      await capture(path.join(directory,`${String(frame).padStart(4,'0')}.png`));
    }
    console.log(`${style}: captured ${count} real renderer frames at 20 FPS.`);
  }
  fs.writeFileSync(path.join(work,'capture.json'),JSON.stringify({renderer:'data/overlay',sampleText:text,width:960,height:240,fps:20,source:'Synthetic transcript fixture; unmodified overlay scripts/styles; controlled animation clock prevents screenshot latency from changing playback speed.',styles:['matrix','console','cyber','matrix_rain','fantasy','cathay']},null,2));
}finally{await command('Browser.close').catch(()=>{});chrome.kill();await new Promise(resolve=>server.close(resolve));}
