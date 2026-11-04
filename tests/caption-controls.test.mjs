import {test} from 'node:test';
import assert from 'node:assert/strict';
import {CaptionModel,resolveConfig} from '../data/overlay/model.mjs';
const phrase=(id,text)=>({id,text,revision:1,final:true});
const state=(phrases,reset=0)=>({session:'controls',active:true,reset,phrases});
const frames=(m,ms)=>{for(let n=0;n<ms;n+=20)m.frame(Math.min(20,ms-n));};

test('should use configured fixed reading time after entrance',()=>{
 const m=new CaptionModel({width:80,effectMs:200,readingMs:800,characterMs:0});
 m.sync(state([phrase('1','hello')]));frames(m,980);
 assert.deepEqual(m.acks(),[]);frames(m,40);assert.equal(m.acks().length,1);
});
test('should retain long Unicode rows for configured per-character reading time',()=>{
 const m=new CaptionModel({width:100,effectMs:0,readingMs:100,characterMs:100});
 m.sync(state([phrase('1','я'.repeat(30))]));frames(m,3980);
 assert.equal(m.acks().length,0);frames(m,40);assert.equal(m.acks().length,1);
});
test('should respect configured row count and interval without dropping queued text',()=>{
 const m=new CaptionModel({width:4,effectMs:0,maxRows:1,readingMs:100,characterMs:0,lineIntervalMs:400});
 m.sync(state([phrase('1','aaa bbb ccc')]));frames(m,200);
 assert.equal(m.rows.length,0);frames(m,200);assert.equal(m.rows[0].text,'bbb ');
 frames(m,600);assert.equal(m.acks().length,1);
});
test('should clear visible and pending rows on reset without reconnecting session',()=>{
 const m=new CaptionModel({width:4,effectMs:0});
 m.sync(state([phrase('1','aaa bbb ccc')]));frames(m,200);
 m.sync(state([],1));assert.equal(m.rows.length,0);assert.deepEqual(m.acks(),[]);
 m.sync(state([phrase('2','new')],1));assert.equal(m.rows[0].text,'new');
});
test('should preserve unread text when reducing row capacity and then finish it',()=>{
 const m=new CaptionModel({width:4,effectMs:0,lineIntervalMs:100,readingMs:10000,characterMs:0});
 m.sync(state([phrase('1','aaa bbb ccc ddd')]));frames(m,220);
 assert.equal(m.rows.length,3);m.layout({maxRows:1,readingMs:100,lineIntervalMs:0});
 const seen=new Set();for(let n=0;n<60;n++){for(const r of m.rows)seen.add(r.text);assert.ok(m.rows.length<=1);m.frame(20);}
 assert.deepEqual([...seen],['aaa ','bbb ','ccc ','ddd']);assert.equal(m.acks().length,1);
});
test('should normalize invalid controls and retain explicit zero values',()=>{
 const c=resolveConfig({readingMs:-10,characterMs:0,lineIntervalMs:0,maxRows:99,animationSpeed:Infinity,fontSize:7,alignment:'bad',animations:false,partialResults:false});
 assert.equal(c.readingMs,100);assert.equal(c.characterMs,0);assert.equal(c.lineIntervalMs,0);
 assert.equal(c.maxRows,3);assert.equal(c.animationSpeed,100);assert.equal(c.fontSize,12);
 assert.equal(c.alignment,'auto');assert.equal(c.animations,false);assert.equal(c.partialResults,false);
});

test('should scale and disable effects without changing reading time',async()=>{
 const {effectDuration}=await import('../data/overlay/model.mjs');
 assert.equal(effectDuration(resolveConfig({style:'console',animationSpeed:50})),1300);
 assert.equal(effectDuration(resolveConfig({style:'matrix_rain',animations:false})),0);
 assert.equal(effectDuration(resolveConfig({style:'plain'})),0);
});

test('should honor manual font size by reducing rows to available height',async()=>{
 const {fitCaptionRows}=await import('../data/overlay/model.mjs');
 assert.deepEqual(fitCaptionRows(resolveConfig({fontSize:64,maxRows:3}),100),{fontSize:64,maxRows:1});
 assert.deepEqual(fitCaptionRows(resolveConfig({fontSize:0,maxRows:3}),24),{fontSize:12,maxRows:1});
 assert.deepEqual(fitCaptionRows(resolveConfig({fontSize:64,maxRows:3}),40),{fontSize:64,maxRows:0});
});
