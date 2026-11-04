// Pure delivery, measured wrapping and frame-time model; no wall clock or DOM.
const styles=['plain','matrix','console','cyber','matrix_rain','fantasy','cathay'];
const presets={
  plain:['solid','#101318','#202734','#59606c',82,38,24,16,0],
  matrix:['solid','#080808','#080808','#082008',42,38,24,0,0],
  console:['solid','#080808','#080808','#082008',72,38,24,0,0],
  cyber:['solid','#081020','#081020','#00ccff',55,38,24,0,0],
  matrix_rain:['gradient','#010b05','#052118','#1a673c',90,40,24,8,1],
  fantasy:['parchment','#f2dfae','#c5a26a','#a47a3f',96,72,25,18,2],
  cathay:['cathay','#082d2b','#061916','#b99a51',96,82,25,18,2]
};
const clamp=(v,min,max,fallback)=>Number.isFinite(v)?Math.min(max,Math.max(min,v)):fallback;
export function resolveConfig(input={}) {
  const style=styles.includes(input.style)?input.style:'plain';
  const [background,color,color2,borderColor,opacity,paddingX,paddingY,radius,borderWidth]=presets[style];
  const out={style,background,color,color2,borderColor,opacity,paddingX,paddingY,radius,borderWidth,imageFit:'cover',imageVersion:typeof input.imageVersion==='string'?input.imageVersion.slice(0,200):''};
  if(['none','solid','gradient','parchment','cathay','image'].includes(input.background))out.background=input.background;
  if(['parchment','cathay'].includes(input.background)){
    const palette=presets[input.background==='parchment'?'fantasy':'cathay'];
    [out.color,out.color2,out.borderColor]=palette.slice(1,4);
  }
  if(input.custom===true){
    for(const key of ['color','color2','borderColor'])if(/^#[\da-f]{6}$/i.test(input[key]))out[key]=input[key];
    for(const [key,max] of Object.entries({opacity:100,paddingX:256,paddingY:128,radius:128,borderWidth:32}))out[key]=clamp(input[key],0,max,out[key]);
    if(input.imageFit==='contain')out.imageFit='contain';
  }
  for(const [key,min,max,fallback] of [['readingMs',100,60000,3000],['characterMs',0,500,60],['lineIntervalMs',0,30000,1500],['maxRows',1,3,3],['animationSpeed',10,500,100],['fontSize',0,200,0]])out[key]=Math.round(clamp(input[key],min,max,fallback));
  if(out.fontSize)out.fontSize=Math.max(12,out.fontSize);
  out.alignment=['left','center','right'].includes(input.alignment)?input.alignment:'auto';
  out.animations=input.animations!==false;out.partialResults=input.partialResults!==false;
  return out;
}
export function fitCaptionRows(config,availableHeight){
  const preferred=['matrix','console','cyber'].includes(config.style)?64:72;
  const fontSize=config.fontSize||Math.max(12,Math.min(preferred,Math.floor(availableHeight/(config.maxRows*1.3+.15))));
  const lineHeight=Math.ceil(fontSize*1.23),gap=Math.max(2,Math.round(fontSize*.07));
  return {fontSize,maxRows:Math.max(0,Math.min(config.maxRows,Math.floor((availableHeight+gap)/(lineHeight+gap))))};
}
export function effectDuration(config,reduced=false){
  if(!config.animations)return 0;
  const base=config.style==='plain'?0:config.style==='matrix_rain'?880:config.style==='fantasy'?800:config.style==='cathay'?820:650;
  return (reduced?Math.min(100,base):base)*100/config.animationSpeed;
}
export const admissionTime=text=>Math.max(1500,1000+60*Array.from(text).length);
export const readingTime=text=>Math.max(3000,admissionTime(text));
const segmenter=typeof Intl.Segmenter==='function'?new Intl.Segmenter(undefined,{granularity:'grapheme'}):null;
export const graphemes=text=>segmenter?Array.from(segmenter.segment(text),s=>s.segment):Array.from(text);
// Match native reveal timing/frontier while preserving whole graphemes.
export function legacyFrame(style,text,elapsedMs){
  const chars=graphemes(text),seconds=Math.max(0,elapsedMs)/1000;
  const visible=Math.min(chars.length,Math.floor(seconds*Math.max(45,chars.length/.65)));
  let value=chars.slice(0,visible).join('');
  if(style==='console'){
    if(Math.floor(seconds*2)%2===0&&!value.endsWith('\n'))value+='_';
  }else if(visible<chars.length){
    const symbols='01#$%&<>[]',count=Math.min(style==='matrix'?5:3,chars.length-visible);
    for(let i=0;i<count;i++)value+=symbols[(Math.floor(seconds*30)+i*3)%symbols.length];
  }
  return {text:value,visible};
}
// Unitless glyph coordinates, ending exactly at final DOM text positions.
export function glyphMotion(style,index,count,progress){
  const position=count>1?index/(count-1):0;
  const delay=style==='matrix_rain'?position*.18+((index*17)%11)/110:position*.34;
  const t=Math.max(0,Math.min(1,(progress-delay)/(1-delay)));
  const ease=1-(1-t)**3;
  return {t,alpha:style==='matrix_rain'?Math.min(1,t*5):t*t*(3-2*t),y:t===1?0:-(1-ease)*(style==='matrix_rain'?.8:style==='fantasy'?.055:0)};
}
export function wrapText(text,measure,width){
  if(!Number.isFinite(width)||width<=0||!text)return [];
  const chars=graphemes(text),rows=[];
  let start=0,index=0;
  while(index<chars.length){
    let end=index,line='',lastSpace=-1;
    while(end<chars.length){
      const next=chars[end];
      if(line&&measure(line+next)>width)break;
      line+=next;end++;
      if(/\s/u.test(next))lastSpace=end;
      if(next.includes('\n'))break;
    }
    if(end<chars.length&&!line.endsWith('\n')&&lastSpace>index)end=lastSpace;
    const value=chars.slice(index,end).join('');
    rows.push({text:value,start,end:start+value.length});start+=value.length;index=end;
  }
  return rows;
}
export class CaptionModel {
  constructor({measure,width,effectMs=650,readingMs=3000,characterMs=60,lineIntervalMs=1500,maxRows=3}={}){
    this.measure=measure|| (s=>Array.from(s).length);this.width=width??80;
    this.effectMs=clamp(effectMs,0,9000,650);const options=resolveConfig({readingMs,characterMs,lineIntervalMs,maxRows});for(const key of ['readingMs','characterMs','lineIntervalMs','maxRows'])this[key]=options[key];this.resetSerial=0;this.session='';this.rows=[];this.records=new Map();
    this.cooldown=0;this.serial=0;this.previewSerial=0;this.preview=false;this.active=false;this.error='';
  }
  reset(){this.rows=[];this.records.clear();this.cooldown=0;this.preview=false;this.error='';}
  sync(state){
    if(!state||typeof state.session!=='string')return;
    const reset=Number.isSafeInteger(state.reset)&&state.reset>=0?state.reset:0;
    if(state.session!==this.session||reset!==this.resetSerial){this.reset();this.session=state.session;this.resetSerial=reset;this.previewSerial=0;}
    this.active=state.active===true;
    if(!this.active){this.reset();return;}
    const incoming=Array.isArray(state.phrases)?state.phrases:[];
    const valid=incoming.filter(p=>p&&typeof p.id==='string'&&/^(0|[1-9]\d{0,19})$/.test(p.id)&&BigInt(p.id)<=18446744073709551615n&&typeof p.text==='string'&&Number.isSafeInteger(p.revision)&&p.revision>0&&typeof p.final==='boolean');
    valid.sort((a,b)=>BigInt(a.id)<BigInt(b.id)?-1:BigInt(a.id)>BigInt(b.id)?1:0);
    if(valid.length&&this.preview)this.reset();
    const present=new Set(valid.map(p=>p.id));
    for(const [id,r] of this.records)if(r.done&&!present.has(id))this.records.delete(id);
    this.error=typeof state.error==='string'?state.error:'';
    for(const p of valid){
      const old=this.records.get(p.id);
      if(old&&(old.final||p.revision<=old.revision))continue;
      const bytes=new TextEncoder().encode(p.text).length;
      const used=Array.from(this.records.values()).reduce((n,r)=>n+r.bytes,0)-(old?.bytes||0);
      if((!old&&this.records.size>=512)||used+bytes>262144){this.error='Caption queue overflow';continue;}
      if(old){Object.assign(old,p,{bytes,done:false});this.reflow();}
      else this.records.set(p.id,{...p,bytes,retired:0,next:0,done:false});
    }
    const preview=state.preview;
    if(preview&&preview.serial!==this.previewSerial){
      this.previewSerial=preview.serial;
      if(!valid.length&&!this.records.size&&typeof preview.text==='string'&&preview.text){
        const text=preview.text.slice(0,4096);this.preview=true;
        this.records.set('preview',{id:'preview',text,revision:1,final:false,bytes:text.length*3,retired:0,next:0,done:false});
      }
    }
    this.complete();this.admit();
  }
  layout({measure=this.measure,width=this.width,effectMs=this.effectMs,readingMs=this.readingMs,characterMs=this.characterMs,lineIntervalMs=this.lineIntervalMs,maxRows=this.maxRows}={}){
    const options=resolveConfig({readingMs,characterMs,lineIntervalMs,maxRows});
    for(const key of ['readingMs','characterMs','lineIntervalMs','maxRows'])this[key]=options[key];
    this.measure=measure;this.width=width;this.effectMs=clamp(effectMs,0,9000,this.effectMs);
    for(const row of this.rows){const progress=row.effectMs?row.effect/row.effectMs:1;row.effectMs=this.effectMs;row.effect=progress*this.effectMs;}
    this.reflow();this.admit();
  }
  interval(text){return this.characterMs?Math.max(this.lineIntervalMs,1000+this.characterMs*Array.from(text).length):this.lineIntervalMs;}
  reading(text){return this.characterMs?Math.max(this.readingMs,1000+this.characterMs*Array.from(text).length):this.readingMs;}
  reflow(){
    const previous=this.rows;this.rows=[];
    for(const r of this.records.values()){
      r.next=Math.min(r.retired,r.text.length);
      // Preserve measured unchanged visible rows, including their readable time.
      const wrapped=wrapText(r.text.slice(r.next),this.measure,this.width);
      for(const w of wrapped){
        const start=w.start+r.retired,end=w.end+r.retired;
        const old=previous.find(row=>row.id===r.id&&row.start===start&&row.end===end&&row.text===w.text);
        if(!old||this.rows.length>=this.maxRows)break;
        this.rows.push(old);r.next=end;
      }
    }
    this.cooldown=0;
  }
  admit(){
    if(this.cooldown>0||this.rows.length>=this.maxRows)return;
    for(const r of this.records.values()){
      if(r.next>=r.text.length)continue;
      const w=wrapText(r.text.slice(r.next),this.measure,this.width)[0];
      if(!w)return;
      const row={id:r.id,key:++this.serial,text:w.text,start:r.next,end:r.next+w.text.length,effect:0,effectMs:this.effectMs,read:0};
      this.rows.push(row);r.next=row.end;this.cooldown=this.interval(row.text);return;
    }
  }
  complete(){
    for(const r of this.records.values())if(r.final&&r.retired>=r.text.length&&!this.rows.some(row=>row.id===r.id))r.done=true;
    if(this.preview&&this.rows.length===0&&Array.from(this.records.values()).every(r=>r.retired>=r.text.length)){this.records.clear();this.preview=false;}
  }
  frame(delta,{visible=true,connected=true}={}){
    // A long gap is a stalled renderer, never earned screen time.
    if(!this.active||!visible||!connected||!Number.isFinite(delta)||delta<=0||delta>100)return;
    this.cooldown=Math.max(0,this.cooldown-delta);
    for(const row of this.rows){
      if(row.effect<row.effectMs)row.effect=Math.min(row.effectMs,row.effect+delta);
      else row.read+=delta;
    }
    while(this.rows.length&&this.rows[0].read>=this.reading(this.rows[0].text)){
      const row=this.rows.shift(),r=this.records.get(row.id);if(r)r.retired=Math.max(r.retired,row.end);
    }
    this.complete();this.admit();
  }
  acks(){return Array.from(this.records.values()).filter(r=>r.done&&r.final).map(({id,revision})=>({id,revision}));}
}
