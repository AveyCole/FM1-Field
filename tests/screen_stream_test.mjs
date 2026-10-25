// SPDX-License-Identifier: MPL-2.0
// Decode the real host firmware's USB byte stream and compare every LCD pixel.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {decodeFrame,applyFrame} from '../docs/mirror/pixels.mjs';
const root=new URL('../app/build/tests/',import.meta.url);
for(const name of ['sampler','player','effects']) {
  const raw=fs.readFileSync(new URL(`${name}.syx`,root)),fb=new Uint16Array(240*240);let start=-1,frames=0;
  for(let i=0;i<raw.length;i++){
    if(raw[i]===0xf0)start=i;
    if(raw[i]!==0xf7||start<0)continue;
    const frame=decodeFrame(raw.subarray(start,i+1));assert(frame,`${name}: invalid packet at ${start}`);start=-1;
    if(!frame.hello){applyFrame(fb,frame);frames++;}
  }
  assert(frames>180);const actual=fs.readFileSync(new URL(`${name}.rgb`,root));assert.equal(actual.length,fb.length*2);
  let mismatches=0;for(let i=0;i<fb.length;i++)if(fb[i]!==actual.readUInt16BE(i*2))mismatches++;
  assert.equal(mismatches,0,`${name}: missing or stale LCD pixels`);console.log(`PASS ${name}: ${frames} packets, all 57,600 pixels match`);
}
// Invalid / truncated input cannot be interpreted as a pixel write.
for(const raw of [[],[0xf0,0xf7],[0xf0,0x80,0xf7],[0xf0,0,0,0xf7]]) assert.equal(decodeFrame(Uint8Array.from(raw)),null);
const low=fs.readFileSync(new URL('low-memory.syx',root));const reply=decodeFrame(low.subarray(0,low.indexOf(0xf7)+1));assert.equal(reply?.error,1);assert.equal(reply?.on,false);
console.log('PASS malformed packets and low-memory refusal');
