// SPDX-License-Identifier: GPL-3.0-only
// ANSI 104-key physical USB layout; displayed labels are US-QWERTY positions.
export const KEYS = [];
const key = (code,usage,label,x,y,w=1,h=1,glyph=null) => KEYS.push({code,usage,label,x,y,w,h,glyph});
const row = (y,items) => {
  let x=0;
  for(const [code,usage,label,w=1,glyph=null] of items) {key(code,usage,label,x,y,w,1,glyph); x+=w;}
};
key('Escape',0x29,'Esc',0,0);
for(let i=1;i<=12;i++) key(`F${i}`,0x39+i,`F${i}`,i+(i<=4?1:i<=8?1.5:2),0);
[['PrintScreen',0x46,'PrtSc'],['ScrollLock',0x47,'ScrLk'],['Pause',0x48,'Pause']].forEach((v,i)=>key(...v,15.5+i,0));
row(1.5,[['Backquote',0x35,'`'],...Array.from({length:10},(_,i)=>[`Digit${(i+1)%10}`,0x1e+i,`${(i+1)%10}`]),['Minus',0x2d,'−'],['Equal',0x2e,'='],['Backspace',0x2a,'Backspace',2,'backspace']]);
const letters = (text) => [...text].map(c=>[`Key${c}`,4+c.charCodeAt(0)-65,c]);
row(2.5,[['Tab',0x2b,'Tab',1.5,'tab'],...letters('QWERTYUIOP'),['BracketLeft',0x2f,'['],['BracketRight',0x30,']'],['Backslash',0x31,'\\',1.5]]);
row(3.5,[['CapsLock',0x39,'Caps',1.75],...letters('ASDFGHJKL'),['Semicolon',0x33,';'],['Quote',0x34,"'"],['Enter',0x28,'Enter',2.25,'enter']]);
row(4.5,[['ShiftLeft',0xe1,'Shift',2.25,'shift'],...letters('ZXCVBNM'),['Comma',0x36,','],['Period',0x37,'.'],['Slash',0x38,'/'],['ShiftRight',0xe5,'Shift',2.75,'shift']]);
row(5.5,[['ControlLeft',0xe0,'Ctrl',1.25,'control'],['MetaLeft',0xe3,'Win',1.25,'windows'],['AltLeft',0xe2,'Alt',1.25],['Space',0x2c,'Space',6.25],['AltRight',0xe6,'Alt',1.25],['MetaRight',0xe7,'Win',1.25,'windows'],['ContextMenu',0x65,'Menu',1.25],['ControlRight',0xe4,'Ctrl',1.25,'control']]);
[['Insert',0x49,'Ins'],['Home',0x4a,'Home'],['PageUp',0x4b,'PgUp'],['Delete',0x4c,'Del'],['End',0x4d,'End'],['PageDown',0x4e,'PgDn']].forEach((v,i)=>key(...v,15.5+i%3,1.5+Math.floor(i/3)));
key('ArrowUp',0x52,'↑',16.5,4.5,1,1,'uparrow');
key('ArrowLeft',0x50,'←',15.5,5.5,1,1,'leftarrow');
key('ArrowDown',0x51,'↓',16.5,5.5,1,1,'downarrow');
key('ArrowRight',0x4f,'→',17.5,5.5,1,1,'rightarrow');
[['NumLock',0x53,'Num'],['NumpadDivide',0x54,'/'],['NumpadMultiply',0x55,'×'],['NumpadSubtract',0x56,'−']].forEach((v,i)=>key(...v,19+i,1.5));
for(let n=1;n<=9;n++) key(`Numpad${n}`,0x58+n,`${n}`,19+(n-1)%3,4.5-Math.floor((n-1)/3));
key('NumpadAdd',0x57,'+',22,2.5,1,2);
key('NumpadEnter',0x58,'Enter',22,4.5,1,2);
key('Numpad0',0x62,'0',19,5.5,2);
key('NumpadDecimal',0x63,'.',21,5.5);
export const BY_CODE = new Map(KEYS.map(k=>[k.code,k]));
export const BY_USAGE = new Map(KEYS.map(k=>[k.usage,k]));
