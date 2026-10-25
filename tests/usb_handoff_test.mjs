// SPDX-License-Identifier: MPL-2.0
// Run the published updater against native-handle and enumeration failures.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const page = fs.readFileSync(new URL('../docs/flasher.js',import.meta.url),'utf8');
const core = page.slice(page.indexOf('const HS_QUERY'),page.indexOf('const $ ='));
const base = fs.readFileSync(new URL('../base/web/fm1ota.js',import.meta.url),'utf8').replaceAll('export ','');
assert.equal(core.slice(core.indexOf('class Updater')).replaceAll('\r','').trim(),base.slice(base.indexOf('class Updater')).replaceAll('\r','').trim(),'published and reusable updater match');
const ctx = vm.createContext({setTimeout,clearTimeout,TextEncoder,console});
vm.runInContext(core+'\nthis.api={Updater,Link,pack7,unpack7,IS_OTA,parseRequest};',ctx);
const {Updater,pack7,unpack7,IS_OTA}=ctx.api;
const PRODUCT='FM-1_84000103';
class Device {
  constructor() {
    this.inputs=new Map();this.outputs=new Map();this.listeners=new Set();
    this.closes=0;this.writes=0;this.serial=0;this.enter('firmware');
    // Old disconnected output retained by WebMIDI, before the current output.
    this.outputs=new Map([['stale',{name:'FIELD',state:'disconnected',send(){throw Error('stale output selected');}}],...this.outputs]);
  }
  addEventListener(type,fn){assert.equal(type,'statechange');this.listeners.add(fn);}
  removeEventListener(type,fn){this.listeners.delete(fn);}
  enter(phase) {
    this.phase=phase;this.serial++;
    const name=phase==='loader'?'Felucca Update':'FIELD';
    const input={name,state:'connected',connection:'closed',onmidimessage:null};
    const output={name,state:'connected',connection:'closed'};
    const close=port=>async()=>{
      port.connection='closed';this.closes++;
      if(this.pending && input.connection==='closed' && output.connection==='closed') {
        input.state=output.state='disconnected';const next=this.pending;this.pending=null;this.enter(next);
        for(const fn of this.listeners)fn();
      }
    };
    input.open=output.open=async function(){this.connection='open';};
    input.close=close(input);output.close=close(output);
    output.send=bytes=>{
      assert.equal(output.connection,'open');
      if(bytes.length===10 && bytes[1]===0 && bytes[2]===0x32) {
        const text=phase==='loader'?'ota-FM-1_900':PRODUCT;
        const raw=[0,0x59,0x11,0,0,0,...[...text].map(c=>c.charCodeAt(0))];
        while(raw.length<34)raw.push(0);
        this.tx(input,[0xF0,...pack7(raw),0xF7]);
      } else if(bytes[1]===0x22) {
        this.request(input,0,16);
      } else {
        const raw=unpack7(bytes.slice(1,-1));
        const addr=(raw[7]|raw[8]<<8|raw[9]<<16|raw[10]<<24)>>>0;
        if(addr>=0xE0000000) this.pending=phase==='loader'?'returned':'loader';
        else {
          assert.deepEqual(Array.from(raw.slice(14,30)),Array(16).fill(73));this.writes++;
          this.request(input,phase==='loader'?0xF0000000:0xE0000000,8);
        }
      }
    };
    this.inputs.set(`in-${this.serial}`,input);this.outputs.set(`out-${this.serial}`,output);
  }
  tx(input,data){setTimeout(()=>input.onmidimessage?.({data:Uint8Array.from(data)}),0);}
  request(input,addr,len) {
    const raw=[0,0x59,0x30,8,0,0,0,addr&255,addr>>>8&255,addr>>>16&255,addr>>>24,len,0,0];
    raw.push(~raw.slice(6).reduce((a,b)=>a+b,0)&255);
    const packet=[0xF0,...pack7(raw),0xF7];
    assert(ctx.api.parseRequest(packet), `invalid fake request ${addr.toString(16)} ${raw}`);
    this.tx(input,packet);
  }
}
const device=new Device(), updater=new Updater(device), steps=[];
assert.equal(await updater.install(new Uint8Array(512).fill(73),PRODUCT,k=>steps.push(k)),PRODUCT);
assert.equal(device.phase,'returned');assert.equal(device.writes,2);
assert.deepEqual(steps,['start','verify','loader','write','reboot','done']);
await updater.release();
assert([...device.inputs.values(),...device.outputs.values()].filter(p=>p.state==='connected').every(p=>p.connection==='closed'));
assert.equal(device.listeners.size,0);
console.log('PASS: FIELD → loader → FIELD with stale endpoints; native handles must close to enumerate; no replug; ports/listeners released');
// Failure after only one port opened must release that port, too.
let inputClosed=0,outputClosed=0;
const broken={name:'FIELD',state:'connected',open:async()=>{},close:async()=>{inputClosed++;}};
const brokenOut={name:'FIELD',state:'connected',open:async()=>{throw Error('driver busy');},close:async()=>{outputClosed++;}};
assert.equal(await new Updater({inputs:new Map([[1,broken]]),outputs:new Map([[1,brokenOut]])}).find(IS_OTA),null);
assert.equal(inputClosed,1);assert.equal(outputClosed,1);
console.log('PASS: partial-open failure closes both handles');
// State events wake discovery; listeners are removed on wake and timeout.
const events=new Device(), watcher=new Updater(events);
const waiting=watcher.portsChanged(1000);for(const fn of events.listeners)fn();await waiting;
assert.equal(events.listeners.size,0);await watcher.portsChanged(1);assert.equal(events.listeners.size,0);
console.log('PASS: state-change wake and polling timeout clean up listeners');
// The loader can arrive after the initial Install click. Windows can expose
// different input/output names while retaining another connected MIDI output.
const late=new Device();
for(const p of [...late.inputs.values(),...late.outputs.values()])p.state='disconnected';
late.outputs.set('ghost',{name:'FIELD ghost',state:'connected',connection:'closed',open:async()=>{},close:async()=>{},send(){}});
setTimeout(()=>{
  late.enter('loader');
  late.inputs.get(`in-${late.serial}`).name='MIDIIN2 (Felucca Update)';
  late.outputs.get(`out-${late.serial}`).name='MIDIOUT2 (Felucca Update)';
  for(const fn of late.listeners)fn();
},100);
const staleAccess={inputs:new Map(),outputs:new Map(),
  addEventListener:late.addEventListener.bind(late),removeEventListener:late.removeEventListener.bind(late)};
let refreshes=0;const lateUpdater=new Updater(staleAccess,undefined,async()=>{refreshes++;return late;});
const lateSteps=[];
assert.equal(await lateUpdater.install(new Uint8Array(512).fill(73),PRODUCT,k=>lateSteps.push(k)),PRODUCT);
assert.equal(late.writes,1,'already-running loader skips staging');
assert.deepEqual(lateSteps,['start','write','reboot','done']);
assert(refreshes>0);await lateUpdater.release();
console.log('PASS: delayed loader discovery, mismatched port names, multiple outputs, fresh MIDI access, direct write and return verification');
const prefixed=new Device();
prefixed.inputs.get('in-1').name='MIDIIN2 (FIELD)';prefixed.outputs.get('out-1').name='MIDIOUT2 (FIELD)';
const prefixUpdater=new Updater(prefixed),found=await prefixUpdater.find();
assert.equal(found?.id.text,PRODUCT);await prefixUpdater.release();
console.log('PASS: Windows-prefixed FIELD input/output names recognized');
// A native open can hang forever while a stale Windows endpoint remains
// "connected". It must not hide the live FIELD endpoint behind it.
const stuck=new Device();let finishIn,finishOut,lateCloses=0;
const stalePort=(name,setFinish)=>({name,state:'connected',connection:'closed',
  open(){return new Promise(resolve=>setFinish(()=>{this.connection='open';resolve();}));},
  async close(){this.connection='closed';lateCloses++;},send(){}});
const staleIn=stalePort('FIELD stale input',fn=>finishIn=fn);
const staleOut=stalePort('FIELD stale output',fn=>finishOut=fn);
stuck.inputs.set('hung',staleIn);stuck.outputs.set('hung',staleOut);
const boundedUpdater=new Updater(stuck);boundedUpdater.portTimeout=25;
const started=Date.now(),live=await boundedUpdater.find(undefined,Date.now()+1000);
assert.equal(live?.id.text,PRODUCT);assert(Date.now()-started<500);
finishIn();finishOut();await new Promise(r=>setTimeout(r,20));
assert.equal(staleIn.connection,'closed');assert.equal(staleOut.connection,'closed');
assert(lateCloses>=4,'late native opens are closed after the timeout, too');
await boundedUpdater.release();assert.equal(boundedUpdater.opening.size,0);
console.log('PASS: hung native opens cannot block running FIELD; late handles are released');
const hungClose={onmidimessage:null,close:()=>new Promise(()=>{})};
const closing=Date.now();await new ctx.api.Link(hungClose,hungClose).close();
assert(Date.now()-closing<1500);
console.log('PASS: hung native close is bounded');
