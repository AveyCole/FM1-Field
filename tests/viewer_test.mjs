// SPDX-License-Identifier: MPL-2.0
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {decodeFrame,applyFrame} from '../docs/mirror/pixels.mjs';
const html=fs.readFileSync(new URL('../docs/mirror/index.html',import.meta.url),'utf8');
const elements=new Map();let paints=0,closed=0,opened=0,rx;
for(const [,id] of html.matchAll(/\bid="([^"]+)"/g))elements.set(id,{value:'',events:{},addEventListener(k,f){this.events[k]=f;},replaceChildren(){},append(o){if(!this.value)this.value=o.value;},getContext(){return {createImageData(){return {data:new Uint8ClampedArray(240*240*4)};},putImageData(){paints++;}};}});
const input={id:'in',name:'FIELD',state:'connected',open:async()=>{opened++;},close:async()=>{closed++;},addEventListener(_,f){rx=f;},removeEventListener(){rx=null;}};
const commands=[];const output={id:'out',name:'FIELD',state:'connected',open:async()=>{opened++;},close:async()=>{closed++;},send(p){commands.push(p);}};
let channel;class Channel{constructor(){channel=this;}postMessage(p){this.last=p;}}
const context=vm.createContext({Uint16Array,Math,Date,Promise,setTimeout,clearTimeout,setInterval,clearInterval,BroadcastChannel:Channel,decodeFrame,applyFrame,requestAnimationFrame:f=>f(),addEventListener(){},location:{},navigator:{requestMIDIAccess:async()=>({inputs:new Map([['in',input]]),outputs:new Map([['out',output]])})},document:{getElementById:id=>elements.get(id),createElement:()=>({})}});
const source=fs.readFileSync(new URL('../docs/mirror/viewer.js',import.meta.url),'utf8').replace(/^import .*;$/m,'');
vm.runInContext(source,context);
assert.equal(paints,1);await elements.get('connect').events.click();assert.equal(opened,2);assert.deepEqual(Array.from(commands[0]),[240,125,87,96,2,247]);
const raw=fs.readFileSync(new URL('../app/build/tests/sampler.syx',import.meta.url));
for(let start=0,i=0;i<raw.length;i++){if(raw[i]===240)start=i;if(raw[i]===247)rx?.({data:raw.subarray(start,i+1)});}
assert.match(elements.get('status').textContent,/Connected/);assert(paints>10);assert.equal(elements.get('save').disabled,false);
await channel.onmessage({data:{type:'installer'}});assert.equal(closed,2);assert.equal(rx,null);assert.equal(channel.last.type,'released');assert.equal(commands.at(-1)[4],0);assert.equal(elements.get('connect').disabled,false);
console.log('PASS viewer subscription, real firmware pixels, connected status, disconnect and installer port release');
