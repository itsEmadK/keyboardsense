// SPDX-License-Identifier: GPL-3.0-only
// Browser demo of src/input.c; parity-checked against the C implementation.
export const ACTIONS = ['none','left','right','up','down','walk','cross','circle','square','triangle','l1','r1','l2','r2','l3','r3','dpad_up','dpad_down','dpad_left','dpad_right','options','touchpad','ps','create','rs_left','rs_right','rs_up','rs_down'];
export const BITS = {l3:2,r3:4,options:8,dpad_up:16,dpad_right:32,dpad_down:64,dpad_left:128,l2:256,r2:512,l1:1024,r1:2048,triangle:4096,circle:8192,cross:16384,square:32768,ps:65536,touchpad:1048576,create:2097152};
export const neutral = () => ({buttons:0,lx:128,ly:128,rx:128,ry:128,l2:0,r2:0});
export const createMapper = () => ({filtered_x:0,filtered_y:0,last_motion_us:0,last_tick_us:0,has_motion:false});
const clamp = (v,min,max) => Math.max(min,Math.min(max,v));
const axis = v => Math.round(128 + clamp(v,-1,1) * (v < 0 ? 128 : 127));
export function mapInput(m,c,keys,mouse,dx,dy,now) {
  const a = new Set([...keys].map(k => ACTIONS[c.keys[k]]));
  c.mouse.forEach((action,i) => { if(mouse & (1 << i)) a.add(ACTIONS[action]); });
  const p = neutral();
  let x = Number(a.has('right')) - Number(a.has('left'));
  let y = Number(a.has('down')) - Number(a.has('up'));
  let length = Math.hypot(x,y);
  if(length > 1) { x /= length; y /= length; }
  if(a.has('walk')) { x *= c.walk_speed; y *= c.walk_speed; }
  p.lx = axis(x); p.ly = axis(y);
  for(const action of a) p.buttons |= BITS[action] || 0;
  p.l2 = a.has('l2') ? 255 : 0; p.r2 = a.has('r2') ? 255 : 0;
  let dt = m.last_tick_us && now > m.last_tick_us ? (now-m.last_tick_us)/1e6 : .004;
  dt = clamp(dt,.001,.1); m.last_tick_us = now;
  if(dx || dy) { m.last_motion_us = now; m.has_motion = true; }
  const scale = c.sensitivity * (a.has('l2') ? c.ads_multiplier : 1)/(2000*dt);
  let tx = dx*scale, ty = dy*scale*(c.invert_y ? -1 : 1);
  length = Math.hypot(tx,ty);
  if(length > 1) { tx /= length; ty /= length; }
  const alpha = c.smoothing_ms > 0 ? 1-Math.exp(-dt*1000/c.smoothing_ms) : 1;
  m.filtered_x += alpha*(tx-m.filtered_x); m.filtered_y += alpha*(ty-m.filtered_y);
  if(!m.has_motion || now-m.last_motion_us >= 32000) m.filtered_x = m.filtered_y = 0;
  x = m.filtered_x; y = m.filtered_y; length = Math.hypot(x,y);
  if(length > .0001) {
    const magnitude = c.deadzone + Math.min(1,length)*(1-c.deadzone);
    x = x/length*magnitude; y = y/length*magnitude;
  } else x = y = 0;
  x += Number(a.has('rs_right'))-Number(a.has('rs_left'));
  y += Number(a.has('rs_down'))-Number(a.has('rs_up'));
  length=Math.hypot(x,y);if(length>1){x/=length;y/=length;}
  p.rx = axis(x); p.ry = axis(y);
  return p;
}
