// SPDX-License-Identifier: GPL-3.0-only
import {ACTIONS,BITS,neutral,createMapper,mapInput} from './mapper.mjs';
import {KEYS,BY_CODE,BY_USAGE} from './keyboard.mjs';
const $ = id => document.getElementById(id);
const svgNode = (tag,attrs={}) => {
  const e=document.createElementNS('http://www.w3.org/2000/svg',tag);
  for(const [name,value] of Object.entries(attrs)) e.setAttribute(name,value);
  return e;
};
const LABELS = {none:'Unbound',left:'LS ←',right:'LS →',up:'LS ↑',down:'LS ↓',walk:'Walk',cross:'Cross ×',circle:'Circle ○',square:'Square □',triangle:'Triangle △',dpad_up:'D-pad ↑',dpad_down:'D-pad ↓',dpad_left:'D-pad ←',dpad_right:'D-pad →',options:'Options',touchpad:'Touchpad',ps:'PS',create:'Create'};
const label = action => LABELS[action] || action?.toUpperCase() || 'Unbound';
const keyNodes=new Map();
for(const k of KEYS) {
  const w=k.w*34-3,h=k.h*34-3;
  const g=svgNode('g',{class:'keycap','data-usage':k.usage,'data-code':k.code,transform:`translate(${8+k.x*34} ${8+k.y*34})`});
  const title=svgNode('title'); g.append(title);
  g.append(svgNode('rect',{class:'key-shadow',width:w,height:h}));
  g.append(svgNode('rect',{width:w,height:h}));
  if(k.glyph) g.append(svgNode('image',{class:'key-glyph',href:`/assets/keys/${k.glyph}.svg`,x:(w-Math.min(w-8,43))/2,y:h/2-9,width:Math.min(w-8,43),height:18}));
  else {const text=svgNode('text',{x:w/2,y:h/2}); text.textContent=k.label; g.append(text);}
  const bind=svgNode('text',{class:'key-bind',x:w/2,y:h-6}); g.append(bind);
  $('keyboard').append(g); keyNodes.set(k.usage,{g,title,bind});
}
const layout = {
  triangle:[482,106,64,64],circle:[528,152,64,64],cross:[482,198,64,64],square:[436,152,64,64],
  dpad_up:[96,122,58,66],dpad_down:[96,180,58,68],dpad_left:[62,154,66,60],dpad_right:[122,154,66,60],
  l1:[76,56,106,52],r1:[458,56,106,52],create:[152,88,44,56],options:[444,88,42,56],ps:[286,238,64,54],touchpad:[182,60,276,160]
};
const position = (e,[x,y,w,h]) => Object.assign(e.style,{left:`${x/640*100}%`,top:`${y/480*100}%`,width:`${w/640*100}%`,height:`${h/480*100}%`});
const padImage = (file,rect,cls='') => {
  const img=document.createElement('img'); img.src=`/assets/dualsense/${file}`; img.alt=''; img.draggable=false;
  img.className=cls; position(img,rect); $('controller').append(img); return img;
};
padImage('base.png',[0,0,640,480]);
const overlays={};
for(const [name,rect] of Object.entries(layout)) overlays[name]=padImage(`pressed/${name}.png`,rect,'button-overlay');
const sticks = [padImage('stick_cap.png',[170.58,222.67,96,96],'stick'),padImage('stick_cap.png',[372.17,222.36,96,96],'stick')];
const triggerRects=[[100,10,56,58],[482,10,56,58]];
const triggers=triggerRects.map((rect,i)=>{
  const box=document.createElement('div');box.className='trigger-fill';position(box,rect);
  const img=document.createElement('img');img.src=`/assets/dualsense/${i?'r2':'l2'}_fill.png`;img.alt='';
  box.append(img);$('controller').append(box);return box;
});
let config=null, configSignature='', mode='live', manualSource=false, capturing=false, demoEnabled=true;
let mapper=createMapper(), pressed=new Set(), simulated=new Set(), browserMouse=0, pendingX=0,pendingY=0;
let snapshot=null, connected=false, lastSequence=-1, sequenceChanged=0, lastLiveAt=0;
let history=[], lastKeys=new Set(), lastMouse=0, lastRenderedHistory=null;
const allDemoKeys = () => new Set([...pressed,...simulated]);
function resetInputs() {
  pressed.clear();simulated.clear();browserMouse=0;pendingX=pendingY=0;mapper=createMapper();
}
function setCapture(value) {
  capturing=value && mode==='demo' && !!config;
  resetInputs();
  if(capturing)demoEnabled=true;
  if(!capturing && document.pointerLockElement) document.exitPointerLock();
  if(capturing) $('testSurface').focus({preventScroll:true});
  updateSourceUI();
}
function setMode(value) {
  setCapture(false);mode=value;history=[];lastKeys.clear();lastMouse=0;lastRenderedHistory=null;
  $('source').value=value;document.body.classList.toggle('demo',value==='demo');
  updateSourceUI();
}
function updateSourceUI() {
  $('capture').disabled = !config || mode==='live';
  $('capture').textContent = mode==='live'?'Watching console':capturing?'Stop test':'Start test';
  $('capture').classList.toggle('capturing',capturing);
  $('lockMouse').hidden = mode!=='demo';
  $('mouseHint').textContent = mode==='live'?'Mouse connected directly to PS5':capturing?'Move or click here to test':'Start the test to capture input';
  const isReal=mode==='live' && connected && snapshot?.source==='ps5';
  const stale=isReal && performance.now()-sequenceChanged>1500;
  const banner=$('modeBanner');banner.className='mode-banner'+(mode==='live'?(isReal&&!stale?' live':' error'):'');
  $('modeText').textContent = mode==='demo'?'Browser demo · Inputs stay in this browser. No input is sent to the PS5.':
    isReal&&!stale?'PS5 live · Reading console USB input and the virtual controller state submitted by the payload.':
    snapshot?.source==='preview'?'No PS5 is connected to this local preview. Select Browser demo to test here.':
    stale?'Input stream stalled. Waiting for the payload…':'Console disconnected. Waiting for the payload on port 1405…';
  $('bannerDetail').textContent = mode==='demo'?(capturing?'CAPTURE ACTIVE':'LOCAL PREVIEW'):'READ ONLY · :1405';
  $('connection').dataset.state = connected?'ready':'error';
  $('connectionText').textContent = connected?(snapshot?.source==='ps5'?'Payload connected':'Local preview'):'Server disconnected';
  $('footerText').textContent = mode==='demo'?'Browser simulation · Click Stop test to release input':'Controller emulation · Console telemetry is visible on your LAN';
}
function applyConfig(next) {
  const signature=JSON.stringify(next);if(signature===configSignature)return;
  config=next;configSignature=signature;mapper=createMapper();
  for(const k of KEYS) {
    const action=ACTIONS[config.keys[k.usage]]||'none', node=keyNodes.get(k.usage);
    node.g.classList.toggle('mapped',action!=='none');
    node.title.textContent=`${k.label} · USB 0x${k.usage.toString(16)} · ${label(action)}`;
    const short = {cross:'×',circle:'○',square:'□',triangle:'△',up:'LS ↑',down:'LS ↓',left:'LS ←',right:'LS →'}[action] || label(action);
    node.bind.textContent=action==='none'?'':short;
    if(action!=='none') {
      const legend=node.g.querySelector('text:not(.key-bind)');if(legend)legend.setAttribute('y',10);
      const glyph=node.g.querySelector('image');if(glyph)glyph.setAttribute('y',2);
    }
  }
  $('sensitivity').textContent=config.sensitivity.toFixed(2)+'×';
  $('ads').textContent=config.ads_multiplier.toFixed(2)+'×';
  $('deadzone').textContent=Math.round(config.deadzone*100)+'%';
  $('smoothing').textContent=config.smoothing_ms+' ms';
  updateSourceUI();
}
async function poll() {
  const controller=new AbortController(), timeout=setTimeout(()=>controller.abort(),1000);
  try {
    const response=await fetch('/api/state',{cache:'no-store',signal:controller.signal});
    if(!response.ok)throw new Error('HTTP '+response.status);
    const next=await response.json();
    if(!['ps5','preview'].includes(next.source)||!Array.isArray(next.config?.keys)||next.config.keys.length!==256)throw new Error('Invalid state');
    snapshot=next;connected=true;lastLiveAt=performance.now();
    if(next.sequence!==lastSequence){lastSequence=next.sequence;sequenceChanged=lastLiveAt;}
    applyConfig(next.config);
    if(!manualSource && next.source==='preview' && mode!=='demo')setMode('demo');
    updateSourceUI();
  } catch {connected=false;updateSourceUI();}
  finally{clearTimeout(timeout);setTimeout(poll,33);}
}
function outputLabels(p) {
  const names=Object.entries(BITS).filter(([,bit])=>p.buttons&bit).map(([name])=>label(name));
  if(p.lx!==128||p.ly!==128)names.unshift('Left stick');
  if(p.rx!==128||p.ry!==128)names.push('Right stick');
  return names;
}
function paint(keys,mouse,dx,dy,p,enabled,valid,pulseKeys=new Set(),pulseButtons=0) {
  for(const [usage,{g}] of keyNodes) {
    g.classList.toggle('pressed',keys.has(usage));g.classList.toggle('pulse',pulseKeys.has(usage));
  }
  $('keyCount').textContent=`${keys.size} ${keys.size===1?'key':'keys'} held`;
  ['mouseLeft','mouseRight','mouseMiddle'].forEach((id,i)=>$(id).classList.toggle('pressed',!!(mouse&(1<<i))));
  $('mouseDx').textContent=dx;$('mouseDy').textContent=dy;
  for(const [name,img] of Object.entries(overlays)){
    img.classList.toggle('pressed',!!(p.buttons&BITS[name]));img.classList.toggle('pulse',!!(pulseButtons&BITS[name]));
  }
  [[p.lx,p.ly,218.58,270.67,2],[p.rx,p.ry,420.17,270.36,4]].forEach(([x,y,cx,cy,bit],i)=>{
    const ax=(x-128)/(x<128?128:127),ay=(y-128)/(y<128?128:127);
    position(sticks[i],[cx+ax*16-48,cy+ay*16-48,96,96]);
    const file=p.buttons&bit?'stick_cap_l3.png':'stick_cap.png';
    if(sticks[i].dataset.file!==file){sticks[i].src='/assets/dualsense/'+file;sticks[i].dataset.file=file;}
  });
  [p.l2,p.r2].forEach((value,i)=>{
    const [x,y,w,h]=triggerRects[i];position(triggers[i],[x,y+h*(1-value/255),w,h*value/255]);
    triggers[i].firstElementChild.style.width='100%';
  });
  const axis = n => ((n-128)/(n<128?128:127)).toFixed(2);
  $('leftAxes').textContent=`${axis(p.lx)} / ${axis(p.ly)}`;
  $('rightAxes').textContent=`${axis(p.rx)} / ${axis(p.ry)}`;
  $('l2Value').textContent=Math.round(p.l2/255*100)+'%';$('r2Value').textContent=Math.round(p.r2/255*100)+'%';
  const names=[...keys].map(k=>BY_USAGE.get(k)?.label||`USB ${k}`);
  ['LMB','RMB','MMB'].forEach((name,i)=>{if(mouse&(1<<i))names.push(name);});
  const outputs=outputLabels(p);
  $('inputSignal').textContent=names.join(' + ')||(dx||dy?'Mouse movement':'Press a key');
  const actions=config?[...new Set([...keys].map(k=>ACTIONS[config.keys[k]]).concat(config.mouse.filter((_,i)=>mouse&(1<<i)).map(a=>ACTIONS[a])))].filter(a=>a&&a!=='none'):[];
  $('mappingSignal').textContent=!enabled?'Paused':actions.map(label).join(' + ')||(dx||dy?'Mouse → RS':'Waiting');
  $('outputSignal').textContent=outputs.join(' + ')||'Neutral';
  $('outputStatus').textContent=mode==='demo'?(enabled?'Simulated':'Paused'):!valid?'Offline':!snapshot?.submit_ok?'Submit error':enabled?'Submitted':'Paused';
  $('signalState').textContent=!valid?'Waiting for connection':!enabled?'Input paused':names.length||dx||dy?'Input active':'Ready for input';
  $('controller').dataset.buttons=p.buttons;$('controller').dataset.lx=p.lx;$('controller').dataset.ly=p.ly;$('controller').dataset.rx=p.rx;$('controller').dataset.ry=p.ry;
  $('controller').setAttribute('aria-label','DualSense output: '+(outputs.join(', ')||'neutral'));
  const activityKeys=new Set([...keys,...pulseKeys]);
  const fresh=[...activityKeys].filter(k=>!lastKeys.has(k)).map(k=>BY_USAGE.get(k)?.label||`USB ${k}`);
  ['LMB','RMB','MMB'].forEach((name,i)=>{if((mouse&(1<<i))&&!(lastMouse&(1<<i)))fresh.push(name);});
  for(const name of fresh)history.unshift(name);
  history=history.slice(0,10);lastKeys=activityKeys;lastMouse=mouse;
  const historySignature=history.join('|');
  if(historySignature!==lastRenderedHistory){
    $('eventStrip').replaceChildren(...(history.length?history:['Recent inputs will appear here.']).map(name=>{const chip=document.createElement('span');chip.className=history.length?'event-chip':'empty-events';chip.textContent=name;return chip;}));
    lastRenderedHistory=historySignature;
  }
}
function tick(now) {
  let keys=new Set(),mouse=0,dx=0,dy=0,p=neutral(),enabled=true,valid=false,pulseKeys=new Set(),pulseButtons=0;
  if(mode==='demo'&&config){
    valid=true;enabled=demoEnabled;keys=allDemoKeys();mouse=browserMouse;dx=pendingX;dy=pendingY;pendingX=pendingY=0;
    p=enabled?mapInput(mapper,config,keys,mouse,dx,dy,now*1000):neutral();
    $('deviceList').textContent=capturing?'Browser keyboard + mouse':'Browser input · idle';
  } else {
    valid=connected&&snapshot?.source==='ps5'&&now-lastLiveAt<1500&&now-sequenceChanged<1500;
    if(valid){keys=new Set(snapshot.keys);pulseKeys=new Set(snapshot.pulse_keys||[]);pulseButtons=snapshot.pulse_buttons||0;mouse=snapshot.mouse.buttons;dx=snapshot.mouse.dx;dy=snapshot.mouse.dy;p=snapshot.pad;enabled=snapshot.enabled;}
    const devices=valid?snapshot.devices:[];
    const text=devices.length?devices.map(d=>`${d.kind==='keyboard'?'Keyboard':'Mouse'} · ${d.name}`).join('\n'):'No USB devices';
    $('deviceList').textContent=text;
  }
  paint(keys,mouse,dx,dy,p,enabled,valid,pulseKeys,pulseButtons);
  requestAnimationFrame(tick);
}
$('source').addEventListener('change',e=>{manualSource=true;setMode(e.target.value);});
$('capture').addEventListener('click',()=>setCapture(!capturing));
function browserKey(e,down) {
  if(!capturing||mode!=='demo')return;
  const key=BY_CODE.get(e.code);if(!key)return;
  e.preventDefault();
  if(down){
    if(e.code==='F12'){setCapture(false);return;}
    if(e.code==='F10'&&!e.repeat){demoEnabled=!demoEnabled;mapper=createMapper();}
    pressed.add(key.usage);
  } else pressed.delete(key.usage);
}
addEventListener('keydown',e=>browserKey(e,true));addEventListener('keyup',e=>browserKey(e,false));
addEventListener('blur',()=>{if(capturing)setCapture(false);});
document.addEventListener('visibilitychange',()=>{if(document.hidden)setCapture(false);});
const pointers=new Map();
$('keyboard').addEventListener('pointerdown',e=>{
  if(mode!=='demo'||!capturing)return;
  const g=e.target.closest('[data-usage]');if(!g)return;
  e.preventDefault();const usage=Number(g.dataset.usage);simulated.add(usage);pointers.set(e.pointerId,usage);
  $('keyboard').setPointerCapture(e.pointerId);
});
function releasePointer(e){const usage=pointers.get(e.pointerId);if(usage!==undefined)simulated.delete(usage);pointers.delete(e.pointerId);}
$('keyboard').addEventListener('pointerup',releasePointer);$('keyboard').addEventListener('pointercancel',releasePointer);
const mouseBit=button=>({0:1,1:4,2:2}[button]||0);
$('mouseZone').addEventListener('pointerdown',e=>{
  if(!capturing||mode!=='demo'||e.target.closest('button'))return;
  e.preventDefault();browserMouse|=mouseBit(e.button);$('mouseZone').setPointerCapture(e.pointerId);
});
addEventListener('pointerup',e=>{browserMouse&=~mouseBit(e.button);});
$('mouseZone').addEventListener('pointercancel',()=>{browserMouse=0;});
$('mouseZone').addEventListener('contextmenu',e=>{if(capturing)e.preventDefault();});
addEventListener('mousemove',e=>{
  if(!capturing||mode!=='demo')return;
  if(document.pointerLockElement===$('mouseZone')||$('mouseZone').contains(e.target)){
    pendingX+=e.movementX;pendingY+=e.movementY;
  }
});
$('lockMouse').addEventListener('click',async()=>{
  if(!capturing)setCapture(true);
  try{await $('mouseZone').requestPointerLock();}catch{$('mouseHint').textContent='Cursor lock unavailable. Move inside this area.';}
});
document.addEventListener('pointerlockchange',()=>{browserMouse=0;pendingX=pendingY=0;mapper=createMapper();});
poll();requestAnimationFrame(tick);
