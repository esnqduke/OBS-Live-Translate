import {test} from 'node:test';
import assert from 'node:assert/strict';
import {CaptionModel, wrapText, resolveConfig} from '../data/overlay/model.mjs';

const phrase = (id='1', text='Hello world', revision=1, final=true) => ({id,text,revision,final});
const state = (phrases, extra={}) => ({session:'test-session',active:true,config:{style:'plain'},phrases,preview:{serial:0,text:''},...extra});
const make = (width=20) => new CaptionModel({measure:s=>Array.from(s).length,width,effectMs:0});
function frames(model, ms, options) { for(let n=0;n<ms;n+=20) model.frame(Math.min(20,ms-n),options); }

test('should reserve three full seconds after entrance and repeat exact final ACK until removed',()=>{
  const m=new CaptionModel({measure:s=>s.length,width:100,effectMs:900});
  m.sync(state([phrase('18446744073709551614')]));
  frames(m,3880); assert.equal(m.acks().length,0);
  frames(m,40); assert.deepEqual(m.acks(),[{id:'18446744073709551614',revision:1}]);
  m.sync(state([phrase('18446744073709551614')]));
  assert.equal(m.rows.length,0); assert.equal(m.acks().length,1);
  m.sync(state([])); assert.deepEqual(m.acks(),[]);
});
test('should stop reading and entrance time for hidden, disconnected, invalid and stalled frames',()=>{
  const m=make(); m.sync(state([phrase()]));
  for(const dt of [3000,Infinity,NaN,-1]) m.frame(dt);
  frames(m,4000,{visible:false}); frames(m,4000,{connected:false});
  assert.equal(m.acks().length,0);
  frames(m,2980); assert.equal(m.acks().length,0);
  frames(m,40); assert.equal(m.acks().length,1);
});
test('should retain all final rows until read, pace admissions and never exceed three rows',()=>{
  const m=make(5); m.sync(state([phrase('2','aaaa bbbb cccc dddd eeee ffff')]));
  const seen=new Set();
  for(let t=0;t<14000;t+=20){m.frame(20); assert.ok(m.rows.length<=3);for(const r of m.rows)seen.add(r.text);}
  assert.deepEqual([...seen],['aaaa ','bbbb ','cccc ','dddd ','eeee ','ffff']);
  assert.equal(m.acks().length,1);
});
test('should ignore repeated and late revisions and update a partial without duplicating it',()=>{
  const m=make(50); m.sync(state([phrase('1','draft',1,false)])); frames(m,200);
  m.sync(state([phrase('1','draft text',2,false)]));
  assert.equal(m.rows.length,1);assert.equal(m.rows[0].text,'draft text');
  m.sync(state([phrase('1','draft text',3,true)]));
  m.sync(state([phrase('1','stale',2,false)]));
  m.sync(state([phrase('1','late mutation',4,true)]));
  assert.equal(m.rows[0].text,'draft text');frames(m,3100);
  assert.deepEqual(m.acks(),[{id:'1',revision:3}]);
});
test('should never replay retired partial prefixes on finalization or style and layout changes',()=>{
  const m=make(5);m.sync(state([phrase('1','aaaa bbbb',1,false)]));frames(m,3100);
  assert.ok(!m.rows.some(r=>r.text==='aaaa '));
  m.sync(state([phrase('1','aaaa bbbb cccc',2,true)],{config:{style:'fantasy',background:'cathay'}}));
  m.layout({measure:s=>Array.from(s).length,width:6});
  assert.ok(m.rows.every(r=>!r.text.startsWith('aaaa')));frames(m,10000);
  assert.deepEqual(m.acks(),[{id:'1',revision:2}]);
});
test('should replay unfinished finals after reload and reset old state on session change or stop',()=>{
  const a=make();a.sync(state([phrase()]));frames(a,1000);
  const b=make();b.sync(state([phrase()]));assert.equal(b.rows[0].text,'Hello world');
  b.sync(state([phrase('2','New')],{session:'other'}));assert.equal(b.rows[0].text,'New');
  b.sync(state([],{session:'other',active:false}));assert.equal(b.rows.length,0);assert.deepEqual(b.acks(),[]);
});
test('should only display each preview serial while the real queue is empty and never ACK it',()=>{
  const m=make();m.sync(state([],{preview:{serial:1,text:'Preview'}}));assert.equal(m.rows[0].text,'Preview');
  m.sync(state([phrase()],{preview:{serial:2,text:'Never interrupt'}}));assert.equal(m.rows[0].text,'Hello world');
  frames(m,4000);assert.deepEqual(m.acks(),[{id:'1',revision:1}]);
  m.sync(state([],{preview:{serial:2,text:'Never interrupt'}}));assert.equal(m.rows.length,0);
});
test('should preserve Unicode and whitespace through measured wrapping including tiny and invalid widths',()=>{
  for(const text of ['','a b c','😀😀界界','e\u0301👩‍💻 hello','a\nb\n','   ']){
    const rows=wrapText(text,s=>Array.from(s).length,3);
    assert.equal(rows.map(r=>r.text).join(''),text);
    assert.ok(rows.every(r=>r.end>r.start));
  }
  assert.deepEqual(wrapText('😀',()=>20,1).map(r=>r.text),['😀']);
  for(const width of [0,-1,NaN,Infinity])assert.deepEqual(wrapText('abc',s=>s.length,width),[]);
});
test('should extend reading time for long rows',()=>{
  const m=make(100);m.sync(state([phrase('1','x'.repeat(60))]));frames(m,4000);
  assert.equal(m.acks().length,0);frames(m,700);assert.equal(m.acks().length,1);
});
test('should preserve earned reading time on unchanged measurement and restart reflowed unread rows safely',()=>{
  const m=make(30);m.sync(state([phrase('1','one two three four')]));frames(m,2000);
  const key=m.rows[0].key;m.layout({width:31});assert.equal(m.rows[0].key,key);
  frames(m,980);assert.equal(m.acks().length,0);frames(m,40);assert.equal(m.acks().length,1);
  const narrow=make(30);narrow.sync(state([phrase('1','one two three four')]));frames(narrow,2500);
  narrow.layout({width:8});frames(narrow,1000);assert.equal(narrow.acks().length,0);
  frames(narrow,10000);assert.equal(narrow.acks().length,1);
});
test('should pause an impossible layout without losing finals and recover after a resize',()=>{
  const m=make(0);m.sync(state([phrase()]));frames(m,10000);assert.equal(m.acks().length,0);
  assert.equal(m.rows.length,0);m.layout({width:30});frames(m,3020);
  assert.deepEqual(m.acks(),[{id:'1',revision:1}]);
});
test('should acknowledge an empty final and preserve a final omitted by a transient snapshot',()=>{
  const m=make();m.sync(state([phrase('1',''),phrase('2','Kept')]));
  assert.deepEqual(m.acks(),[{id:'1',revision:1}]);m.sync(state([]));
  assert.equal(m.rows[0].text,'Kept');frames(m,3100);assert.deepEqual(m.acks(),[{id:'2',revision:1}]);
});
test('should reject overflow explicitly without evicting previously accepted finals',()=>{
  const m=make();m.sync(state([phrase()]));
  m.sync(state([phrase(),...Array.from({length:512},(_,i)=>phrase(String(i+2),'x'))]));
  assert.match(m.error,/overflow/i);assert.equal(m.rows[0].text,'Hello world');
  const huge=make();huge.sync(state([phrase('1','界'.repeat(90000))]));assert.match(huge.error,/overflow/i);assert.equal(huge.rows.length,0);
});
test('should sort decimal IDs without precision loss and ignore malformed wire entries',()=>{
  const m=make();m.sync(state([phrase('18446744073709551614','last'),phrase('2','first'),phrase('3x','bad'),phrase('4','bad',NaN)]));
  assert.equal(m.rows[0].text,'first');
});
test('should keep explicit backgrounds independent and apply defaults until custom scalars enabled',()=>{
  const a=resolveConfig({style:'fantasy',background:'cathay',custom:false,opacity:0,paddingX:256});
  assert.equal(a.background,'cathay');assert.notEqual(a.opacity,0);assert.notEqual(a.paddingX,256);
  const b=resolveConfig({style:'matrix_rain',background:'none',custom:true,color:'#abcdef',opacity:0,paddingX:999,paddingY:-1,borderWidth:99,radius:99,imageFit:'contain'});
  assert.equal(b.background,'none');assert.equal(b.color,'#abcdef');assert.equal(b.opacity,0);assert.equal(b.paddingX,256);assert.equal(b.paddingY,0);assert.equal(b.borderWidth,32);assert.equal(b.imageFit,'contain');
  assert.equal(resolveConfig({style:'bogus',color:'url(evil)',custom:true}).style,'plain');
});
test('should use legible parchment and jade preset palettes with any text theme, retaining manual overrides',()=>{
  assert.equal(resolveConfig({style:'cyber',background:'parchment'}).color,'#f2dfae');
  assert.equal(resolveConfig({style:'fantasy',background:'cathay'}).color,'#082d2b');
  assert.equal(resolveConfig({style:'cyber',background:'parchment',custom:true,color:'#112233'}).color,'#112233');
});
test('should preserve generated Unicode input in bounded wrapping (seed 0x51a7, 120 inputs)',()=>{
  let seed=0x51a7;const alphabet=['界','😀','a',' ','\n','e\u0301','👩‍💻'];
  for(let input=0;input<120;input++){
    let text='';for(let j=0;j<input;j++){seed=(Math.imul(seed,1664525)+1013904223)>>>0;text+=alphabet[seed%alphabet.length];}
    const rows=wrapText(text,s=>Array.from(s).length,1+input%11);
    assert.equal(rows.map(r=>r.text).join(''),text,JSON.stringify({seed:0x51a7,input,text}));
  }
});
