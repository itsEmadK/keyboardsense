/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "editor.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static uint64_t now=1000000;
static void tick(KsEditor *e,unsigned usage,uint8_t mouse,bool ready){KsKeyboard keys={0};if(usage)keys.keys[usage]=true;now+=4000;ks_editor_tick(e,&keys,mouse,0,0,now,ready);}
static void tap(KsEditor *e,unsigned usage){tick(e,usage,0,true);tick(e,0,0,true);}
static void open_menu(KsEditor *e){tap(e,0x41);assert(e->open);}
int main(void)
{
    char root[]="/tmp/keyboardsense-editor-XXXXXX";assert(mkdtemp(root));
    KsEditor e;assert(ks_editor_init(&e,root)==0);assert(e.active.sensitivity==1);
    assert(!ks_title_valid("../PPSA00001")&&!ks_title_valid("NPXS00001")&&!ks_title_valid("lowercase"));
    assert(ks_title_valid("PPSA00001"));
    assert(ks_editor_game(&e,"PPSA00001"));assert(!e.game_profile);
    tick(&e,0,0,false);tick(&e,0x41,0,false);assert(!e.open&&strstr(e.status,"unavailable"));tick(&e,0,0,false);assert(!e.release_gate);
    open_menu(&e);tap(&e,43);assert(e.tab==1);tap(&e,79);assert(e.draft.sensitivity>1&&e.active.sensitivity==1);
    tap(&e,0x3e);assert(!e.open&&e.active.sensitivity>1);tick(&e,0,0,true);
    assert(ks_editor_game(&e,"PPSA00002"));assert(e.active.sensitivity==1); /* session-only apply did not save */
    assert(ks_editor_game(&e,"PPSA00001"));assert(e.active.sensitivity==1);
    KsConfig changed=e.active;changed.sensitivity=2.75;changed.ads_multiplier=.35;changed.keys[0x62]=KS_CROSS;changed.keys[0x12]=KS_RS_UP;
    uint64_t stale=e.revision;assert(ks_editor_commit(&e,&changed,e.revision,e.title,1)==0);assert(e.game_profile);
    assert(ks_editor_commit(&e,&changed,stale,e.title,1)==-1);
    assert(ks_editor_commit(&e,&changed,e.revision,"PPSA99999",1)==-1);
    assert(ks_editor_game(&e,"PPSA00002"));assert(e.active.sensitivity==1);
    assert(ks_editor_game(&e,"PPSA00001"));assert(e.active.sensitivity==2.75&&e.active.keys[0x62]==KS_CROSS&&e.active.keys[0x12]==KS_RS_UP);
    tick(&e,0,0,true);open_menu(&e);e.row=KS_CROSS-1;
    tap(&e,40);assert(e.capture==1); /* Enter that armed capture was not bound. */
    tick(&e,0,0,true);tap(&e,29);assert(e.draft.keys[29]==KS_CROSS&&e.draft.keys[44]==KS_CROSS);
    tap(&e,40);tick(&e,0,0,true);tap(&e,26);assert(e.capture==3&&e.draft.keys[26]==KS_UP);
    tap(&e,41);assert(e.capture==0&&e.draft.keys[26]==KS_UP);
    tap(&e,40);tick(&e,0,0,true);tap(&e,26);tap(&e,40);assert(e.draft.keys[26]==KS_CROSS&&e.draft.keys[44]==KS_CROSS);
    tap(&e,0x3b);assert(e.input_first);tap(&e,40);tick(&e,0,0,true);tick(&e,0,2,true);tick(&e,0,0,true);
    assert(e.input==257&&e.capture==0);e.row=KS_CROSS-1;tap(&e,40);assert(e.capture==3);tap(&e,40);
    assert(e.draft.mouse[1]==KS_CROSS&&e.draft.mouse[0]==KS_R2);
    tap(&e,41);assert(!e.open&&e.active.keys[26]==KS_UP); /* cancel discards all draft remaps */
    tick(&e,0,0,true);open_menu(&e);e.draft.sensitivity=4;
    assert(ks_editor_game(&e,"PPSA00002")&&!e.open&&e.active.sensitivity==1);
    tick(&e,0,0,true);open_menu(&e);tick(&e,0,0,false);assert(!e.open&&strstr(e.status,"lost"));
    /* A global save changes fallback games without overwriting a saved game. */
    KsConfig global=e.active;global.sensitivity=2.1;
    assert(ks_editor_commit(&e,&global,e.revision,e.title,2)==0);
    assert(ks_editor_game(&e,"PPSA00001")&&e.active.sensitivity==2.75);
    assert(ks_editor_game(&e,"PPSA00003")&&e.active.sensitivity==2.1);
    assert(ks_editor_reload(&e)==0&&e.active.sensitivity==2.1);
    /* A persistence failure must not report success or change the active map. */
    char profiles[512],parked[512];snprintf(profiles,sizeof(profiles),"%s/profiles",root);snprintf(parked,sizeof(parked),"%s/parked",root);
    assert(rename(profiles,parked)==0);uint64_t before_failure=e.revision;global.sensitivity=3.5;
    assert(ks_editor_commit(&e,&global,e.revision,e.title,1)==-1&&e.revision==before_failure&&e.active.sensitivity==2.1);
    assert(rename(parked,profiles)==0);
    KsSidebarFrame frame;ks_editor_frame(&e,10,&frame);assert(ks_sidebar_frame_valid(&frame,sizeof(frame)));
    assert(!ks_sidebar_frame_valid(&frame,sizeof(frame)-1));frame.row_count=100;assert(!ks_sidebar_frame_valid(&frame,sizeof(frame)));
    ks_editor_frame(&e,10,&frame);memset(frame.status,'x',sizeof(frame.status));assert(!ks_sidebar_frame_valid(&frame,sizeof(frame)));
    char text[16384],error[128];int size=ks_config_format(&changed,text,sizeof(text));assert(size>0);
    KsConfig roundtrip;assert(ks_config_parse(text,(size_t)size,&roundtrip,error,sizeof(error))==0);
    assert(roundtrip.sensitivity==2.75&&roundtrip.keys[0x62]==KS_CROSS);
    changed.keys[0x41]=KS_CROSS;assert(ks_config_format(&changed,text,sizeof(text))==-1);
    const char invalid[]="bind.usb_41=cross\n";assert(ks_config_parse(invalid,sizeof(invalid)-1,&roundtrip,error,sizeof(error))==-1);
    const char nul[]="sensitivity=1\0sensitivity=4";assert(ks_config_parse(nul,sizeof(nul)-1,&roundtrip,error,sizeof(error))==-1);
    char path[512];snprintf(path,sizeof(path),"%s/profiles/PPSA00001.ini",root);unlink(path);snprintf(path,sizeof(path),"%s/config.ini",root);unlink(path);rmdir(profiles);rmdir(root);
    puts("Editor passed: bidirectional bindings, conflicts, cancellation, sensitivity, game saves, stale revisions, renderer loss, config roundtrip.");
    return 0;
}
