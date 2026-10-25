/* SPDX-License-Identifier: MPL-2.0 */
import {decodeFrame,applyFrame} from './pixels.mjs';
const $=id=>document.getElementById(id),ctx=$('screen').getContext('2d'),fb=new Uint16Array(240*240);
let midi,input,output,heartbeat,timeout,confirmed=false,paintPending=false,serial=0;
const say=text=>{$('status').textContent=text;};
const bounded=p=>Promise.race([p,new Promise((_,reject)=>setTimeout(()=>reject(new Error('MIDI port did not respond. Close other MIDI apps and retry.')),2500))]);
function command(op){output?.send([0xf0,0x7d,0x57,0x60,op,0xf7]);}
function paint(){
  paintPending=false;const img=ctx.createImageData(240,240);
  for(let i=0;i<fb.length;i++){const p=fb[i],at=i*4;img.data[at]=Math.round((p>>11)*255/31);img.data[at+1]=Math.round(((p>>5)&63)*255/63);img.data[at+2]=Math.round((p&31)*255/31);img.data[at+3]=255;}
  ctx.putImageData(img,0,0);
}
function receive(event){
  const frame=decodeFrame(event.data);if(!frame)return;
  if(frame.hello){
    if(!frame.on){disconnect(frame.error?'FIELD has too little free memory to mirror right now. Try again after the sound starts.':'FIELD ended the mirror session.');return;}
    confirmed=true;clearTimeout(timeout);say('Connected · live FIELD display');$('save').disabled=$('refresh').disabled=false;return;
  }
  applyFrame(fb,frame);if(!paintPending){paintPending=true;requestAnimationFrame(paint);}
}
async function disconnect(message='Disconnected. FIELD keeps playing.'){
  serial++;clearInterval(heartbeat);clearTimeout(timeout);confirmed=false;
  const oldInput=input,oldOutput=output;input=output=null;
  if(oldInput)oldInput.removeEventListener('midimessage',receive);
  try{oldOutput?.send([0xf0,0x7d,0x57,0x60,0,0xf7]);}catch{}
  await Promise.allSettled([oldInput,oldOutput].filter(Boolean).map(p=>bounded(p.close())));
  $('connect').disabled=false;$('disconnect').disabled=true;$('save').disabled=$('refresh').disabled=true;$('ports').hidden=true;say(message);
}
async function usePorts(){
  const attempt=++serial;input=midi.inputs.get($('input').value);output=midi.outputs.get($('output').value);
  if(!input||!output){say('Choose one MIDI input and output.');return;}
  $('ports').hidden=true;$('disconnect').disabled=false;say('Waiting for FIELD…');
  const selected=[input,output];
  try{
    await Promise.all(selected.map(p=>bounded(p.open().then(()=>{if(attempt!==serial)p.close().catch(()=>{});} ))));
    if(attempt!==serial)return;
    input.addEventListener('midimessage',receive);command(2);
    timeout=setTimeout(()=>disconnect('No FIELD mirror reply. Install FIELD 0.1.8 or later, leave update mode, and retry.'),3500);
    heartbeat=setInterval(()=>{try{command(1);}catch{disconnect('USB connection lost. Reconnect and press Connect FIELD.');}},2000);
  }catch(e){if(attempt===serial)await disconnect(e.message);}
}
function populate(id,ports){
  const select=$(id);select.replaceChildren();const list=[...ports.values()].filter(p=>p.state==='connected');
  list.sort((a,b)=>Number(/field|fm.?1|felucca|aporee/i.test(b.name))-Number(/field|fm.?1|felucca|aporee/i.test(a.name)));
  for(const p of list){const option=document.createElement('option');option.value=p.id;option.textContent=p.name||'Unnamed MIDI port';select.append(option);}return list;
}
$('connect').addEventListener('click',async()=>{
  if(!navigator.requestMIDIAccess){say('Use desktop Chrome or Edge to access USB MIDI.');return;}
  $('connect').disabled=true;
  try{
    midi=await navigator.requestMIDIAccess({sysex:true});
    midi.onstatechange=()=>{if(input?.state==='disconnected'||output?.state==='disconnected')disconnect('USB connection lost. Reconnect and press Connect FIELD.');};
    const ins=populate('input',midi.inputs),outs=populate('output',midi.outputs);
    if(!ins.length||!outs.length)throw new Error('No MIDI input/output found. Check the USB data cable and close other MIDI apps.');
    if(ins.length===1&&outs.length===1)await usePorts();
    else{$('ports').hidden=false;$('disconnect').disabled=false;say('Select the FM-1’s input and output.');}
  }catch(e){await disconnect(e.message);}
});
$('use').addEventListener('click',usePorts);$('disconnect').addEventListener('click',()=>disconnect());
$('refresh').addEventListener('click',()=>{try{command(2);}catch{disconnect('USB connection lost.');}});
$('full').addEventListener('click',async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await $('display').requestFullscreen();}catch(e){say(e.message);}});
$('save').addEventListener('click',()=>{if(!confirmed)return;const a=document.createElement('a');a.download=`FIELD-${new Date().toISOString().replaceAll(':','-')}.png`;a.href=$('screen').toDataURL('image/png');a.click();});
$('back').addEventListener('click',async event=>{event.preventDefault();await disconnect();location.href='../';});
addEventListener('pagehide',()=>{disconnect();});
if(typeof BroadcastChannel!=='undefined'){
  const owner=new BroadcastChannel('field-midi-owner');
  owner.onmessage=async event=>{if(event.data?.type==='installer'){await disconnect('Mirror closed so the installer can use USB.');owner.postMessage({type:'released'});}};
}
fb.fill(0x1144);paint();
