import {CaptionModel,resolveConfig,graphemes,legacyFrame,glyphMotion,effectDuration,fitCaptionRows} from './model.mjs';

const panel=document.getElementById('panel'),rowsElement=document.getElementById('rows'),status=document.getElementById('status');
const reduced=matchMedia('(prefers-reduced-motion: reduce)');
const measureCanvas=document.createElement('canvas'),measureContext=measureCanvas.getContext('2d');
const model=new CaptionModel({width:0,effectMs:reduced.matches?100:650});
const expectedSession=location.pathname.split('/').filter(Boolean)[0];
const elements=new Map();
let config=resolveConfig(),configKey='',lastSuccess=0,connected=false,lastFrame=0,layoutValid=false,font='',lineHeight=0;
let pendingAck=false,polling=false,raf=0,obsVisible=true;
let renderedKeys='',motionUntil=0,baseline=0,panelInset=0,rowGap=0;

function configure(input){
  const next=resolveConfig(input),key=JSON.stringify(next);
  if(key===configKey)return;
  config=next;configKey=key;
  if(!config.animations){for(const element of elements.values())element.getAnimations().forEach(animation=>animation.cancel());motionUntil=0;}
  const root=document.documentElement;root.dataset.style=config.style;root.dataset.background=config.background;
  const luminance=color=>{const rgb=color.match(/[\da-f]{2}/ig).map(v=>parseInt(v,16)/255);return rgb[0]*.2126+rgb[1]*.7152+rgb[2]*.0722;};
  const lightness=(luminance(config.color)+luminance(config.color2))/2;
  root.dataset.tone=!['none','image'].includes(config.background)&&config.opacity>=30&&lightness>.55?'light':'dark';
  document.getElementById('backdrop').style.backgroundImage=config.background==='image'?`url("background?version=${encodeURIComponent(config.imageVersion)}")`:'';
  for(const [key,value] of Object.entries({color:config.color,color2:config.color2,'border-color':config.borderColor,opacity:config.opacity/100,radius:config.radius+'px','image-fit':config.imageFit}))root.style.setProperty('--'+key,value);
  const alignment=config.alignment==='auto'?(['matrix','cyber'].includes(config.style)?'center':'left'):config.alignment;
  rowsElement.style.textAlign=alignment;
  root.dataset.animations=String(config.animations);
  layout();
}
function layout(){
  const width=innerWidth,height=innerHeight;
  const px=Math.min(config.paddingX,width*.12),py=Math.min(config.paddingY,height*.09),border=Math.min(config.borderWidth,width*.025,height*.025);
  const {fontSize,maxRows}=fitCaptionRows(config,height-py*2-border*2);
  lineHeight=Math.ceil(fontSize*1.23);const gap=Math.max(2,Math.round(fontSize*.07));
  panelInset=py*2+border*2;rowGap=gap;
  const root=document.documentElement;
  for(const [key,value]of Object.entries({px:px+'px',py:py+'px',border:border+'px','font-size':fontSize+'px','line-height':lineHeight+'px',gap:gap+'px','panel-height':Math.min(height,lineHeight*maxRows+gap*Math.max(0,maxRows-1)+py*2+border*2)+'px'}))root.style.setProperty('--'+key,value);
  const computed=getComputedStyle(rowsElement);font=`${computed.fontWeight} ${fontSize}px ${computed.fontFamily}`;
  measureContext.font=font;
  const metrics=measureContext.measureText('Hg');
  const ascent=metrics.fontBoundingBoxAscent??fontSize*.9,descent=metrics.fontBoundingBoxDescent??fontSize*.22;
  baseline=Math.floor((lineHeight-ascent-descent)/2+ascent);
  layoutValid=maxRows>0&&fontSize>=12&&width-px*2-border*2>=fontSize*2;
  model.layout({measure:s=>measureContext.measureText(s.replace(/[\r\n]/g,'')).width,width:layoutValid?width-px*2-border*2-(config.style==='console'?measureContext.measureText('_').width:0):0,effectMs:effectDuration(config,reduced.matches),readingMs:config.readingMs,characterMs:config.characterMs,lineIntervalMs:config.lineIntervalMs,maxRows:Math.max(1,maxRows)});
  for(const element of elements.values())delete element.glyphs;
  render();
}
function showStatus(){
  const message=!layoutValid?'Caption area is too small. Increase the Browser Source size.':model.error;
  status.textContent=message;status.hidden=!message;
}
function render(){
  panel.hidden=!layoutValid||!model.rows.length||!model.active||(!connected&&performance.now()-lastSuccess>1500);
  const keys=model.rows.map(r=>r.key).join(','),changed=keys!==renderedKeys;
  const positions=new Map();
  if(changed&&!reduced.matches&&config.animations)for(const [key,element]of elements)positions.set(key,element.getBoundingClientRect().top);
  const height=Math.min(innerHeight,lineHeight*Math.max(1,model.rows.length)+rowGap*Math.max(0,model.rows.length-1)+panelInset)+'px';
  if(panel.style.height!==height)panel.style.height=height;
  for(const [key,element]of elements)if(!model.rows.some(r=>r.key===key)){element.remove();elements.delete(key);}
  for(const row of model.rows){
    let element=elements.get(row.key);
    if(!element){
      element=document.createElement('div');element.className='row';
      const text=document.createElement('span');text.className='row-text';text.textContent=row.text.replace(/[\r\n]/g,'');
      const canvas=document.createElement('canvas');canvas.setAttribute('aria-hidden','true');
      element.append(text,canvas);elements.set(row.key,element);
    }
    if(changed)rowsElement.append(element);
    const text=element.firstElementChild,canvas=element.lastElementChild;
    const progress=row.effectMs?Math.min(1,row.effect/row.effectMs):1;
    text.style.visibility=progress<1?'hidden':'visible';canvas.hidden=progress>=1;
    if(progress<1)drawEntrance(canvas,row,progress);
  }
  if(changed){
    renderedKeys=keys;
    if(!reduced.matches&&config.animations&&!panel.hidden)for(const [key,element]of elements){
      const before=positions.get(key);if(before===undefined)continue;
      const offset=before-element.getBoundingClientRect().top;
      if(Math.abs(offset)>.5){
        element.getAnimations().forEach(animation=>animation.cancel());
        element.animate([{transform:`translateY(${offset}px)`},{transform:'translateY(0)'}],{duration:180*100/config.animationSpeed,easing:'cubic-bezier(.22,.7,.3,1)'});
        motionUntil=performance.now()+180*100/config.animationSpeed;
      }
    }
  }
  showStatus();
}
function drawEntrance(canvas,row,p){
  const width=rowsElement.clientWidth,dpr=Math.min(2,devicePixelRatio||1),height=lineHeight;
  if(canvas.width!==Math.ceil(width*dpr)||canvas.height!==Math.ceil(height*dpr)){canvas.width=Math.ceil(width*dpr);canvas.height=Math.ceil(height*dpr);}
  const ctx=canvas.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,width,height);
  ctx.font=font;ctx.textBaseline='alphabetic';const text=row.text.replace(/[\r\n]/g,'');
  const ink=getComputedStyle(rowsElement).color;
  const align=rowsElement.style.textAlign;
  const offset=value=>Math.max(0,(width-measureContext.measureText(value).width)*(align==='right'?1:align==='center'?.5:0));
  const x0=offset(text);
  ctx.fillStyle=ink;
  if(reduced.matches){ctx.globalAlpha=p;ctx.fillText(text,x0,baseline);ctx.globalAlpha=1;return;}
  if(['matrix','console','cyber'].includes(config.style)){
    const frame=legacyFrame(config.style,text,p*650);
    const x=offset(frame.text);
    ctx.save();ctx.beginPath();ctx.rect(0,0,width,height);ctx.clip();
    ctx.lineJoin='round';ctx.lineWidth=config.style==='console'?2:4;
    ctx.strokeStyle=config.style==='cyber'?(row.effect<350&&Math.floor(row.effect*14/1000)%2?'#ff22cc':'#00ccff'):'#082008';
    ctx.strokeText(frame.text,x,baseline);ctx.fillText(frame.text,x,baseline);ctx.restore();return;
  }
  const element=elements.get(row.key);
  if(!element.glyphs){
    let prefix='';element.glyphs=graphemes(text).map(char=>{const x=measureContext.measureText(prefix).width;prefix+=char;return {char,x};});
  }
  const glyphs=element.glyphs,codes='アイウエオカキクケコサシスセソ012345789';
  for(let i=0;i<glyphs.length;i++){
    const {char,x:relativeX}=glyphs[i];const x=relativeX+x0;if(/^\s+$/u.test(char))continue;
    const {t,alpha,y}=glyphMotion(config.style,i,glyphs.length,p);
    if(!alpha)continue;
    ctx.save();ctx.globalAlpha=alpha;ctx.fillStyle=ink;
    if(config.style==='matrix_rain'){
      const head=baseline+y*height;
      ctx.shadowColor='#48f379';ctx.shadowBlur=(1-t)*6;
      ctx.fillStyle=t>.76?ink:'#d8ffe1';
      const cell=measureContext.measureText(char).width;
      ctx.fillText(t>.62?char:codes[(i*7+Math.floor(p*28))%codes.length],x,head,cell);
      ctx.shadowBlur=0;
      for(let trail=1;trail<=4&&t<.86;trail++){
        ctx.globalAlpha=alpha*(1-t)*.45/trail;ctx.fillStyle='#43e372';
        ctx.fillText(codes[(i*11+Math.floor(p*28)+trail)%codes.length],x,head-trail*height*.22,cell);
      }
    }else if(config.style==='fantasy'){
      ctx.shadowColor='#d1a45b';ctx.shadowBlur=Math.sin(t*Math.PI)*5;
      ctx.fillText(char,x,baseline+y*height);
      if(t>.08&&t<.85){ctx.globalAlpha=alpha*Math.sin(t*Math.PI)*.32;ctx.strokeStyle='#d9b368';ctx.lineWidth=.65;ctx.strokeText(char,x,baseline+y*height);}
    }else if(config.style==='cathay'){
      // Brush-like top-to-bottom reveal with one restrained gold edge.
      const top=Math.max(0,baseline-height*.85),edge=top+(height-top)*Math.min(1,t*1.6);
      ctx.beginPath();ctx.rect(x-3,top,measureContext.measureText(char).width+6,edge-top);ctx.clip();
      ctx.fillText(char,x,baseline);
      if(t<.7){ctx.globalAlpha=alpha*Math.sin(t*Math.PI)*.4;ctx.strokeStyle='#d9b66b';ctx.lineWidth=.8;ctx.strokeText(char,x,baseline);}
    }else ctx.fillText(char,x,baseline);
    ctx.restore();
  }
}
async function poll(){
  if(polling)return;polling=true;
  try{
    const response=await fetch('state',{cache:'no-store',signal:AbortSignal.timeout(1800)});
    if(!response.ok)throw Error('state');
    const state=await response.json();
    if(state.session!==expectedSession)throw Error('session');
    connected=true;lastSuccess=performance.now();
    configure(state.config);model.sync(state);render();
    const ids=model.acks();
    if(ids.length&&!pendingAck){
      pendingAck=true;
      try{await fetch('ack',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({session:model.session,ids}),signal:AbortSignal.timeout(1800)});}finally{pendingAck=false;}
    }
  }catch{connected=false;render();}
  finally{polling=false;setTimeout(poll,150);}
}
function frame(now){
  const delta=lastFrame?now-lastFrame:0;lastFrame=now;
  model.frame(delta,{visible:obsVisible&&document.visibilityState==='visible'&&layoutValid&&now>=motionUntil,connected:connected&&now-lastSuccess<2200});
  render();raf=requestAnimationFrame(frame);
}
document.addEventListener('visibilitychange',()=>{lastFrame=0;});
addEventListener('obsSourceVisibleChanged',event=>{if(typeof event.detail?.visible==='boolean'){obsVisible=event.detail.visible;lastFrame=0;}});
addEventListener('resize',layout);reduced.addEventListener('change',layout);
configure({});layout();poll();raf=requestAnimationFrame(frame);
addEventListener('pagehide',()=>cancelAnimationFrame(raf),{once:true});
