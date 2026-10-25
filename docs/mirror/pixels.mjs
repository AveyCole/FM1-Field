/* SPDX-License-Identifier: MPL-2.0 */
/* Validate a complete FIELD pixel message before changing the framebuffer. */
export function decodeFrame(raw) {
  if(raw.length<4 || raw[0]!==0xf0 || raw.at(-1)!==0xf7) return null;
  const bytes=[];let acc=0,bits=0;
  for(let i=1;i<raw.length-1;i++) {
    if(raw[i]>127) return null;
    acc|=raw[i]<<bits;bits+=7;
    if(bits>=8) {bytes.push(acc&255);acc>>>=8;bits-=8;}
  }
  if(bytes.length<7 || bytes[0]!==0 || bytes[1]!==0x59) return null;
  const n=bytes[3]+bytes[4]*256+bytes[5]*65536;
  if(n+7!==bytes.length) return null;
  const body=bytes.slice(6,6+n);
  if(((body.reduce((a,b)=>a+b,0)+bytes.at(-1))&255)!==255) return null;
  if(bytes[2]===0x50) return n===5?{hello:true,on:body[1]!==0,error:body[4]}:null;
  if(bytes[2]!==0x51 && bytes[2]!==0x52 || n<7) return null;
  const [x,y,w,h]=body;
  if(!w || !h || x+w>240 || y+h>240) return null;
  const pixels=new Uint16Array(w*h);
  if(bytes[2]===0x52) {
    if(n!==7) return null;
    pixels.fill(body[5]*256+body[6]);
  } else {
    let at=5,out=0;
    while(at<n) {
      const op=body[at++],run=(op&63)+1;
      if((op&192)!==192 || at+2>n || out+run>pixels.length) return null;
      const color=body[at++]*256+body[at++];pixels.fill(color,out,out+run);out+=run;
    }
    if(out!==pixels.length) return null;
  }
  return {x,y,w,h,pixels};
}
export function applyFrame(buffer,frame) {
  for(let row=0;row<frame.h;row++) buffer.set(frame.pixels.subarray(row*frame.w,(row+1)*frame.w),(frame.y+row)*240+frame.x);
}
