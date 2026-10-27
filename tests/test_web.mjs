// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mapInput,createMapper} from '../web/mapper.mjs';
import {KEYS,BY_CODE,BY_USAGE} from '../web/keyboard.mjs';
import {setBinding,inputAction,configText,inputLabel} from '../web/settings.mjs';
assert.equal(KEYS.length,104);
assert.equal(BY_CODE.size,104);
assert.equal(BY_USAGE.size,104);
assert.equal(BY_CODE.get('KeyW').usage,26);
assert.equal(BY_CODE.get('NumpadEnter').usage,0x58);
const records=execFileSync('./build/web_fixture',{encoding:'utf8',maxBuffer:8*1024*1024}).trim().split('\n').map(JSON.parse);
let mapper=createMapper(),count=0;
for(const r of records) {
  if(r.serialization){
    assert.equal(r.serialization.source,'ps5');
    assert.equal(r.serialization.keys.length,256);
    assert.equal(r.serialization.pulse_keys.length,256);
    assert.equal(r.serialization.devices.length,8);
    assert.equal(r.serialization.devices[0].name,'a"\\\nb');
    continue;
  }
  if(r.reset)mapper=createMapper();
  const s=r.state;
  const pad=mapInput(mapper,s.config,new Set(s.keys),s.mouse.buttons,s.mouse.dx,s.mouse.dy,r.now);
  assert.deepEqual(pad,s.pad,`JS/C mapping divergence at frame ${count}`);count++;
}
console.log(`Web tests passed: 104 unique keys, JSON bounds/escaping, ${count} exact C/JS mapper parity frames.`);

const config=structuredClone(records[0].state.config);
const oldSpace=config.keys[44];setBinding(config,29,6);
assert.equal(inputAction(config,29),6);assert.equal(config.keys[44],oldSpace);
setBinding(config,257,7);assert.equal(inputAction(config,257),7);
assert.throws(()=>setBinding(config,0x41,6));assert.throws(()=>setBinding(config,44,100));
assert.equal(inputLabel(224),'Left Ctrl');assert.equal(inputLabel(258),'Mouse middle');
assert.match(configText(config),/bind\.usb_1d=cross/);
assert.doesNotMatch(configText(config),/bind\.usb_41=/);
console.log('Sidebar model passed: key/mouse bindings, preserved aliases, reserved keys, raw-HID serialization.');
