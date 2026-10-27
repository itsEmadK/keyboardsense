// SPDX-License-Identifier: GPL-3.0-only
import {ACTIONS} from './mapper.mjs';
import {KEYS,BY_CODE,BY_USAGE} from './keyboard.mjs';
export const RESERVED = new Set([0x41,0x42,0x43,0x45]);
export const actionLabel = a => ({none:'Unbound',left:'Left stick ←',right:'Left stick →',up:'Left stick ↑',down:'Left stick ↓',walk:'Walk modifier',cross:'Cross ×',circle:'Circle ○',square:'Square □',triangle:'Triangle △',l1:'L1',r1:'R1',l2:'L2',r2:'R2',l3:'L3',r3:'R3',dpad_up:'D-pad ↑',dpad_down:'D-pad ↓',dpad_left:'D-pad ←',dpad_right:'D-pad →',options:'Options',touchpad:'Touchpad click',ps:'PS button',create:'Create (experimental)',rs_left:'Right stick ←',rs_right:'Right stick →',rs_up:'Right stick ↑',rs_down:'Right stick ↓'}[a]||a);
export const inputLabel = input => input>=256?['Mouse left','Mouse right','Mouse middle'][input-256]:input>=224&&input<=231?['Left Ctrl','Left Shift','Left Alt','Left Meta','Right Ctrl','Right Shift','Right Alt','Right Meta'][input-224]:BY_USAGE.get(input)?.code.startsWith('Numpad')?'Numpad '+BY_USAGE.get(input).label:BY_USAGE.get(input)?.label||`USB ${input.toString(16).toUpperCase()}`;
export const inputAction = (config,input) => input>=256?config.mouse[input-256]:config.keys[input];
export function setBinding(config,input,action) {
  if(!Number.isInteger(input)||input<1||input>258||RESERVED.has(input)||!Number.isInteger(action)||action<0||action>=ACTIONS.length)throw new Error('Reserved or invalid input');
  if(input>=256)config.mouse[input-256]=action;else config.keys[input]=action;
}
export function configText(c) {
  let text=`sensitivity=${c.sensitivity}\nads_multiplier=${c.ads_multiplier}\ndeadzone=${c.deadzone}\nsmoothing_ms=${c.smoothing_ms}\nwalk_speed=${c.walk_speed}\ninvert_y=${c.invert_y?'true':'false'}\n`;
  c.keys.forEach((a,k)=>{if(k&&!RESERVED.has(k))text+=`bind.usb_${k.toString(16).padStart(2,'0')}=${ACTIONS[a]}\n`;});
  ['left','right','middle'].forEach((name,i)=>{text+=`bind.mouse_${name}=${ACTIONS[c.mouse[i]]}\n`;});
  return text;
}
export function mountSettings(callbacks) {
  const $=id=>document.getElementById(id), drawer=$('settingsDrawer');
  let open=false,draft=null,base=null,profile=null,direction='controller',tab='bindings',selectedAction=6,selectedInput=44;
  let listening=false,conflict=null,stale=false,pending=null,held=new Set(),ignored=new Set();
  const actionSelect=$('actionSelect'),inputSelect=$('inputSelect');
  for(const [i,a] of ACTIONS.entries()){const o=new Option(actionLabel(a),i);actionSelect.append(o);}
  for(const k of KEYS.filter(k=>!RESERVED.has(k.usage))){inputSelect.append(new Option(`${inputLabel(k.usage)} · USB ${k.usage.toString(16).toUpperCase()}`,k.usage));}
  [256,257,258].forEach(i=>inputSelect.append(new Option(inputLabel(i),i)));
  const hitboxes={cross:[482,198,64,64],circle:[528,152,64,64],triangle:[482,106,64,64],square:[436,152,64,64],l1:[76,56,106,52],r1:[458,56,106,52],l2:[100,10,56,58],r2:[482,10,56,58],l3:[171,223,96,96],r3:[372,223,96,96],touchpad:[195,73,249,133],options:[444,88,42,56],create:[152,88,44,56],ps:[286,238,64,54],dpad_up:[96,122,58,66],dpad_down:[96,180,58,68],dpad_left:[62,154,66,60],dpad_right:[122,154,66,60]};
  for(const [action,rect] of Object.entries(hitboxes)){
    const button=document.createElement('button');button.className='binding-target';button.type='button';button.dataset.action=action;
    button.title=actionLabel(action);button.setAttribute('aria-label','Map '+actionLabel(action));
    const [x,y,w,h]=rect;Object.assign(button.style,{left:x/640*100+'%',top:y/480*100+'%',width:w/640*100+'%',height:h/480*100+'%'});
    button.addEventListener('click',()=>chooseAction(ACTIONS.indexOf(action)));
    $('bindingController').append(button);
  }
  for(const action of ['up','left','down','right','rs_up','rs_left','rs_down','rs_right','walk']){
    const button=document.createElement('button');button.type='button';button.dataset.action=action;button.className='action-chip';button.textContent=actionLabel(action);
    button.addEventListener('click',()=>chooseAction(ACTIONS.indexOf(action)));$('axisBindings').append(button);
  }
  for(const [action,rect] of Object.entries(hitboxes)){
    const b=document.createElement('button');b.type='button';b.className='test-binding-target';b.setAttribute('aria-label','Edit '+actionLabel(action)+' binding');
    const [x,y,w,h]=rect;Object.assign(b.style,{left:x/640*100+'%',top:y/480*100+'%',width:w/640*100+'%',height:h/480*100+'%'});
    b.addEventListener('click',()=>{if(callbacks.isCapturing?.())return;if(!open)openEditor();if(open){direction='controller';chooseAction(ACTIONS.indexOf(action));}});
    document.getElementById('controller').append(b);
  }
  const settings=[
    ['sensitivity','Hip sensitivity',.01,20,.01,'×','Aim speed before the game’s turn-speed limit.'],
    ['ads_multiplier','ADS multiplier',.01,4,.01,'×','Sensitivity while holding any input mapped to L2.'],
    ['smoothing_ms','Smoothing',0,100,1,'ms','Lower values respond faster; higher values soften movement.'],
    ['deadzone','Anti-deadzone',0,.5,.01,'%','Compensates for the game’s controller deadzone.'],
    ['walk_speed','Walk speed',.05,1,.01,'%','Left-stick strength while holding the walk modifier.']
  ];
  for(const [name,title,min,max,step,unit,hint] of settings){
    const row=document.createElement('div');row.className='setting-row';
    const heading=document.createElement('label');heading.htmlFor='setting-'+name;heading.textContent=title;
    const factor=unit==='%'?100:1;const number=document.createElement('input');number.type='number';number.id='setting-'+name;number.min=min*factor;number.max=max*factor;number.step=step*factor;number.setAttribute('aria-label',title);
    const range=document.createElement('input');range.type='range';range.min=min*factor;range.max=max*factor;range.step=step*factor;range.setAttribute('aria-label',title+' slider');
    const output=document.createElement('span');output.className='setting-unit';output.textContent=unit;
    const values=document.createElement('div');values.className='setting-values';values.append(number,output);
    const top=document.createElement('div');top.className='setting-heading';top.append(heading,values);
    const help=document.createElement('p');help.textContent=hint;row.append(top,range,help);$('mouseSettings').append(row);
    const change=e=>{
      const value=Number(e.target.value)/factor;
      if(e.target.value===''||!Number.isFinite(value)||value<min||value>max){e.target.setAttribute('aria-invalid','true');$('editorMessage').textContent=`${title} must be between ${min*factor} and ${max*factor}${unit}.`;updateDirty();return;}
      number.removeAttribute('aria-invalid');range.removeAttribute('aria-invalid');draft[name]=value;number.value=range.value=value*factor;
      show(drawer.querySelector('[aria-invalid=true]')?'Correct the highlighted settings before applying.':'Changes stay in the draft until you apply or save them.');
      updateDirty();
    };
    number.addEventListener('input',change);range.addEventListener('input',change);
    settings.find(s=>s[0]===name).elements={number,range,factor};
  }
  function sourceInfo(){return callbacks.getSource()==='preview'?'Local preview · temporary storage':profile?.title?'Game profile · '+profile.title:'Global default · no game detected';}
  function show(message){$('editorMessage').textContent=message;}
  function dirty(){return JSON.stringify(draft)!==JSON.stringify(base);}
  function updateDirty(){
    if(!open)return;
    $('draftState').textContent=dirty()?'Unsaved changes':'No changes';
    const invalid=!!drawer.querySelector('[aria-invalid=true]');
    for(const id of ['applySettings','saveSettings','saveGlobal'])$(id).disabled=stale||!!pending||invalid||!!conflict;
    $('settingsScope').textContent=sourceInfo();
    callbacks.onDraft?.(draft);
  }
  function updateBindings(){
    actionSelect.value=selectedAction;inputSelect.value=selectedInput;document.querySelectorAll('[data-usage]').forEach(e=>e.classList.toggle('binding-selected',Number(e.dataset.usage)===selectedInput));
    document.querySelectorAll('[data-action]').forEach(e=>e.classList.toggle('selected',ACTIONS.indexOf(e.dataset.action)===selectedAction));
    $('controllerDirection').setAttribute('aria-pressed',direction==='controller');$('inputDirection').setAttribute('aria-pressed',direction==='input');
    $('inputFirstControls').hidden=direction!=='input';
    $('bindingPrompt').textContent=listening?'Listening… press a key or click the mouse test box. Escape cancels.':direction==='controller'?`Choose a controller action, then press an input for ${actionLabel(ACTIONS[selectedAction])}.`:`Choose an input, then click its controller action. You can also click the keyboard to select a key.`;
    $('listenInput').textContent=direction==='controller'?'Listen for new binding':'Listen for input';
    $('selectedInputName').textContent=inputLabel(selectedInput);
    const bindings=[];
    for(let k=1;selectedAction!==0&&k<259;k++)if(!RESERVED.has(k)&&inputAction(draft,k)===selectedAction)bindings.push(k);
    $('actionBindingsTitle').textContent='Inputs mapped to '+actionLabel(ACTIONS[selectedAction]);
    $('existingBindings').replaceChildren(...(bindings.length?bindings.map(input=>{
      const b=document.createElement('button');b.type='button';b.textContent=inputLabel(input)+' ×';b.setAttribute('aria-label','Remove '+inputLabel(input)+' binding');
      b.addEventListener('click',()=>{setBinding(draft,input,0);updateBindings();updateDirty();});return b;
    }):[document.createTextNode('No inputs assigned') ]));
    $('conflictBox').hidden=!conflict;
    if(conflict)$('conflictText').textContent=`${inputLabel(conflict.input)} is mapped to ${actionLabel(ACTIONS[conflict.old])}. Replace it with ${actionLabel(ACTIONS[conflict.action])}?`;
    updateDirty();
  }
  function propose(input,action){
    if(stale||pending)return;
    if(RESERVED.has(input)){show('F8, F9, F10 and F12 are reserved for payload controls.');return;}
    const old=inputAction(draft,input);
    if(old!==0&&old!==action){conflict={input,action,old};updateBindings();return;}
    setBinding(draft,input,action);show(`${inputLabel(input)} → ${actionLabel(ACTIONS[action])} in draft.`);updateBindings();
  }
  function chooseAction(action){
    selectedAction=action;conflict=null;
    if(direction==='input')propose(selectedInput,action);
    else{if(action===0){show('Select an input in the other direction to unbind it.');updateBindings();return;}startListening();}
    updateBindings();
  }
  function selectInput(input){
    if(RESERVED.has(input)){show('This key is reserved for the payload.');return;}
    selectedInput=input;listening=false;conflict=null;direction='input';selectedAction=inputAction(draft,input);
    show(`${inputLabel(input)} selected. Choose a controller action.`);updateBindings();
  }
  function startListening(){listening=true;conflict=null;ignored=new Set(held);show('Press your input. Existing controller assignments will be preserved.');updateBindings();}
  function setTab(next){tab=next;listening=false;conflict=null;drawer.querySelector('.drawer-body').scrollTop=0;
    for(const name of ['bindings','mouse','profiles']){$('editor-'+name).hidden=name!==tab;$('tab-'+name).setAttribute('aria-selected',name===tab);}
    updateBindings();
  }
  function openEditor(){
    const current=callbacks.getProfile();if(!current?.revision||!callbacks.getConfig()){show('Waiting for the active profile.');return;}
    callbacks.onOpen?.();open=true;draft=structuredClone(callbacks.getConfig());base=structuredClone(draft);profile=structuredClone(current);
    stale=false;pending=null;conflict=null;listening=false;
    drawer.hidden=false;$('settingsTitle').textContent=profile.title||'Global default';
    $('saveSettings').textContent=profile.title?'Save for this game':'Save global';
    $('profileTitle').textContent=profile.title||'No game detected';$('profileSource').textContent=profile.saved?'Saved game profile':'Inheriting global defaults';
    settings.forEach(s=>{s.elements.number.value=s.elements.range.value=draft[s[0]]*s.elements.factor;s.elements.number.removeAttribute('aria-invalid');});
    $('setting-invert_y').checked=draft.invert_y;
    show('Changes stay in the draft until you apply or save them.');setTab('bindings');$('closeSettings').focus();
  }
  function closeEditor(){open=false;listening=false;conflict=null;drawer.hidden=true;document.querySelectorAll('[data-usage]').forEach(e=>e.classList.remove('binding-selected'));callbacks.onDraft?.(null);$('openSettings').focus();}
  async function save(mode){
    if(stale||pending||conflict)return;
    if(drawer.querySelector('[aria-invalid=true]')){show('Correct invalid settings first.');return;}
    listening=false;pending={id:null,at:performance.now()};updateDirty();
    try{
      const body=`revision=${profile.revision}\nsave=${mode}\ntitle=${profile.title}\n`+configText(draft);
      const response=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-keyboardsense-config','X-KeyboardSense':'editor-v1'},body});
      if(!response.ok)throw new Error(response.status===409?'Profile changed or native editor is open. Reopen settings.':`Save rejected (${response.status}).`);
      const {id}=await response.json();pending={id,at:performance.now()};show('Waiting for the payload to apply settings…');updateDirty();
    }catch(error){pending=null;show(error.message);updateDirty();}
  }
  async function loadDraft(path,message){
    try{const response=await fetch(path,{cache:'no-store'});if(!response.ok)throw new Error('Cannot load profile');
      draft=structuredClone((await response.json()).config);
      settings.forEach(s=>{s.elements.number.value=s.elements.range.value=draft[s[0]]*s.elements.factor;s.elements.number.removeAttribute('aria-invalid');});
      $('setting-invert_y').checked=draft.invert_y;conflict=null;show(message);updateBindings();
    }catch(error){show(error.message);}
  }
  $('openSettings').addEventListener('click',openEditor);$('closeSettings').addEventListener('click',closeEditor);$('cancelSettings').addEventListener('click',closeEditor);
  ['bindings','mouse','profiles'].forEach(name=>$('tab-'+name).addEventListener('click',()=>setTab(name)));
  $('controllerDirection').addEventListener('click',()=>{direction='controller';listening=false;conflict=null;updateBindings();});
  $('inputDirection').addEventListener('click',()=>{direction='input';listening=false;conflict=null;updateBindings();});
  actionSelect.addEventListener('change',()=>chooseAction(Number(actionSelect.value)));
  inputSelect.addEventListener('change',()=>selectInput(Number(inputSelect.value)));
  $('listenInput').addEventListener('click',startListening);
  $('setting-invert_y').addEventListener('change',()=>{draft.invert_y=$('setting-invert_y').checked;updateDirty();});
  $('replaceBinding').addEventListener('click',()=>{if(conflict){setBinding(draft,conflict.input,conflict.action);conflict=null;show('Binding replaced in draft.');updateBindings();}});
  $('keepBinding').addEventListener('click',()=>{conflict=null;show('Existing binding kept.');updateBindings();});
  $('applySettings').addEventListener('click',()=>save(0));$('saveSettings').addEventListener('click',()=>save(1));$('saveGlobal').addEventListener('click',()=>save(2));
  $('resetDraft').addEventListener('click',()=>loadDraft('/api/defaults','Defaults copied into draft. Apply or save to use them.'));
  $('copyGlobal').addEventListener('click',()=>loadDraft('/api/global','Global profile copied into draft.'));
  document.addEventListener('keydown',e=>{
    const key=BY_CODE.get(e.code);if(key)held.add(key.usage);
    if((e.code==='KeyM'&&e.ctrlKey&&e.altKey)||e.code==='F8'){
      e.preventDefault();e.stopImmediatePropagation();if(!e.repeat)open?closeEditor():openEditor();return;
    }
    if(!open)return;
    if(listening){
      e.preventDefault();e.stopImmediatePropagation();
      if(e.code==='Escape'){listening=false;show('Input capture cancelled.');updateBindings();return;}
      if(!key||ignored.has(key.usage)||e.repeat)return;
      if(RESERVED.has(key.usage)){show('This key is reserved for the payload.');return;}
      listening=false;
      if(direction==='input')selectInput(key.usage);else{selectedInput=key.usage;propose(key.usage,selectedAction);}
    }else if(e.code==='Escape'){e.preventDefault();e.stopImmediatePropagation();closeEditor();}
  },true);
  document.addEventListener('keyup',e=>{const key=BY_CODE.get(e.code);if(key){held.delete(key.usage);ignored.delete(key.usage);}},true);
  document.addEventListener('pointerdown',e=>{
    const key=e.target.closest('[data-usage]');
    if(!open&&key&&!callbacks.isCapturing?.()){openEditor();if(open){e.preventDefault();e.stopImmediatePropagation();selectInput(Number(key.dataset.usage));}return;}
    if(!open)return;
    if(key){e.preventDefault();e.stopImmediatePropagation();selectInput(Number(key.dataset.usage));return;}
    if(listening&&e.target.closest('#captureMouseBinding')){
      e.preventDefault();e.stopImmediatePropagation();const button={0:0,2:1,1:2}[e.button];if(button===undefined)return;
      const input=256+button;listening=false;
      if(direction==='input')selectInput(input);else{selectedInput=input;propose(input,selectedAction);}
    }
  },true);
  $('captureMouseBinding').addEventListener('contextmenu',e=>e.preventDefault());
  addEventListener('blur',()=>{held.clear();ignored.clear();if(open){listening=false;updateBindings();}});
  return {
    isOpen:()=>open,
    update(state){
      $('openSettings').disabled=!state.profile?.revision;
      if(!open)return;
      if(pending&&pending.id!==null){
        if(state.command?.id===pending.id){
          const ok=state.command.status===0;pending=null;
          if(ok){callbacks.onApplied?.(state.config);closeEditor();}
          else{show(state.command.message||'Settings were not applied.');stale=true;updateDirty();}
        }else if(state.command?.id>pending.id||performance.now()-pending.at>5000){pending=null;stale=true;show('Apply was not confirmed. Refresh the profile before retrying.');updateDirty();}
      }
      if(open&&!pending&&(state.profile?.revision!==profile.revision||state.profile?.title!==profile.title||state.profile?.editor_open)){
        stale=true;listening=false;show('The active profile changed or the native editor opened. Cancel and reopen to edit the current profile.');updateDirty();
      }
    }
  };
}
