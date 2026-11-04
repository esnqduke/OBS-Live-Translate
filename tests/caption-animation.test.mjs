import {test} from 'node:test';
import assert from 'node:assert/strict';
import {legacyFrame,glyphMotion,graphemes} from '../data/overlay/model.mjs';

test('console restores native 45 characters per second, cursor and exact Unicode text',()=>{
  assert.equal(legacyFrame('console','ABCDE',0).text,'_');
  assert.equal(legacyFrame('console','ABCDE',50).text,'AB_');
  assert.equal(legacyFrame('console','ABCDE',510).text,'ABCDE');
  const text='Я😀e\u0301好';
  assert.equal(legacyFrame('console',text,700).text,text);
  assert.equal(legacyFrame('console',text,1000).text,text+'_');
});
test('matrix and cyber restore a bounded changing symbol frontier without replacing settled letters',()=>{
  const text='A'.repeat(100);
  const a=legacyFrame('matrix',text,130),b=legacyFrame('matrix',text,160);
  assert.equal(a.visible,20);
  assert.match(a.text,/^A{20}[01#$%&<>\[\]]{5}$/);
  assert.notEqual(a.text.slice(-5),b.text.slice(-5));
  assert.equal(legacyFrame('cyber',text,130).text.length,23);
  for(const style of ['matrix','cyber'])assert.equal(legacyFrame(style,text,650).text,text);
});
test('new effects have staggered visible motion and end exactly at the final glyph position',()=>{
  for(const style of ['matrix_rain','fantasy','cathay']){
    const early=glyphMotion(style,0,40,.25),late=glyphMotion(style,39,40,.25);
    assert.ok(early.alpha>0,'first glyph should appear promptly');
    assert.ok(early.alpha>late.alpha,'effects must cascade rather than fade the entire line');
    for(let i=0;i<40;i++){
      let lastAlpha=0;
      for(let step=0;step<=100;step++){
        const m=glyphMotion(style,i,40,step/100);
        assert.ok(m.alpha>=lastAlpha&&m.alpha<=1&&m.y<=0&&Number.isFinite(m.y));
        lastAlpha=m.alpha;
      }
      const end=glyphMotion(style,i,40,1);
      assert.equal(end.alpha,1);assert.equal(end.y,0);assert.equal(end.t,1);
    }
  }
  assert.ok(glyphMotion('matrix_rain',0,10,.25).y<-.1,'rain should physically fall');
  assert.ok(graphemes('Я😀e\u0301好').length===4);
});
