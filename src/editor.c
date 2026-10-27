/* SPDX-License-Identifier: GPL-3.0-only */
#include "editor.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool ks_title_valid(const char *title)
{
    size_t n = strlen(title);
    if (n < 4 || n >= KS_TITLE_SIZE || !strncmp(title,"NPXS",4)) return false;
    for (size_t i=0;i<n;++i) if (!((title[i]>='A'&&title[i]<='Z')||(title[i]>='0'&&title[i]<='9'))) return false;
    return true;
}
static void profile_path(const KsEditor *e, const char *title, char path[512])
{
    if (*title) snprintf(path,512,"%s/profiles/%s.ini",e->root,title);
    else snprintf(path,512,"%s/config.ini",e->root);
}
static bool any_input(const KsKeyboard *k,uint8_t mouse)
{
    if (mouse) return true;
    for(unsigned i=1;i<KS_KEY_COUNT;++i)if(k->keys[i])return true;
    return false;
}
static bool edge(const KsEditor *e,const KsKeyboard *k,unsigned usage) { return k->keys[usage]&&!e->previous.keys[usage]; }
static KsAction *binding(KsConfig *c,unsigned input)
{
    if (input>0&&input<256&&!ks_key_reserved(input)) return &c->keys[input];
    if (input>=256&&input<259)return &c->mouse[input-256];
    return NULL;
}
static void input_name(unsigned input,char name[24])
{
    if(input>=256){snprintf(name,24,"Mouse %s",input==256?"left":input==257?"right":"middle");return;}
    if(input>=4&&input<=29){snprintf(name,24,"%c",'A'+input-4);return;}
    const char *special=NULL;
    switch(input){case 40:special="Enter";break;case 41:special="Escape";break;case 43:special="Tab";break;case 44:special="Space";break;case 224:special="Left Ctrl";break;case 225:special="Left Shift";break;case 226:special="Left Alt";break;}
    if(special)snprintf(name,24,"%s",special);else snprintf(name,24,"USB %02X",input);
}
static void current_profile(KsEditor *e)
{
    e->active=e->global;e->game_profile=false;
    if (*e->title) {
        char path[512];profile_path(e,e->title,path);
        if(access(path,F_OK)==0){
            if(ks_config_load(path,&e->active,e->status,sizeof(e->status))==0)e->game_profile=true;
        }
    }
    e->draft=e->active;
}
int ks_editor_init(KsEditor *e,const char *root)
{
    memset(e,0,sizeof(*e));
    if(strlen(root)>=sizeof(e->root)){errno=ENAMETOOLONG;return -1;}
    snprintf(e->root,sizeof(e->root),"%s",root);
    if(mkdir(root,0700)!=0&&errno!=EEXIST)return -1;
    char path[512];snprintf(path,sizeof(path),"%s/profiles",root);
    if(mkdir(path,0700)!=0&&errno!=EEXIST)return -1;
    ks_config_defaults(&e->global);
    profile_path(e,"",path);
    if(access(path,F_OK)==0)ks_config_load(path,&e->global,e->status,sizeof(e->status));
    e->active=e->draft=e->global;e->revision=1;e->cursor_x=1650;e->cursor_y=250;
    return 0;
}
bool ks_editor_game(KsEditor *e,const char *title)
{
    if(*title&&!ks_title_valid(title))return false;
    if(!strcmp(title,e->title))return false;
    bool was_open=e->open;e->open=false;e->capture=0;e->release_gate=true;
    snprintf(e->title,sizeof(e->title),"%s",title);e->status[0]=0;
    current_profile(e);e->revision++;
    if(!*e->status)snprintf(e->status,sizeof(e->status),"%s%s",was_open?"Game changed; draft discarded. ":"",e->game_profile?"Game profile loaded":"Global default loaded");
    return true;
}
int ks_editor_reload(KsEditor *e)
{
    char path[512];profile_path(e,"",path);
    KsConfig next;ks_config_defaults(&next);
    if(access(path,F_OK)==0&&ks_config_load(path,&next,e->status,sizeof(e->status))!=0)return -1;
    e->global=next;e->open=false;e->capture=0;e->release_gate=true;e->status[0]=0;
    current_profile(e);e->revision++;return 0;
}
int ks_editor_commit(KsEditor *e,const KsConfig *config,uint64_t revision,const char *title,unsigned save)
{
    if(revision!=e->revision||strcmp(title,e->title)){
        snprintf(e->status,sizeof(e->status),"Profile changed. Reopen the editor before applying.");return -1;
    }
    char serialized[16384];
    if(save>2||ks_config_format(config,serialized,sizeof(serialized))<0){snprintf(e->status,sizeof(e->status),"Invalid settings");return -1;}
    if(save){
        char path[512];profile_path(e,save==2?"":e->title,path);
        if(ks_config_save(path,config,e->status,sizeof(e->status))!=0)return -1;
        if(save==2||!*e->title)e->global=*config;
        if(save==1&&*e->title)e->game_profile=true;
    }
    e->active=e->draft=*config;e->revision++;e->open=false;e->capture=0;e->release_gate=true;
    snprintf(e->status,sizeof(e->status),"%s",save?(save==2||!*e->title?"Global profile saved":"Game profile saved"):"Applied for this session");
    return 0;
}
static void assign(KsEditor *e)
{
    KsAction *old=binding(&e->draft,e->input);
    if(!old)return;
    *old=e->target;e->capture=0;
    char name[24];input_name(e->input,name);
    snprintf(e->status,sizeof(e->status),"%s -> %s (draft)",name,ks_action_name(e->target));
}
static void propose(KsEditor *e)
{
    KsAction *old=binding(&e->draft,e->input);
    if(!old)return;
    if(*old!=KS_NONE&&*old!=e->target){
        char name[24];input_name(e->input,name);e->capture=3;
        snprintf(e->status,sizeof(e->status),"%s is %s. Enter replaces it; Esc cancels.",name,ks_action_name(*old));
    }else assign(e);
}
static void adjust(KsEditor *e,int direction)
{
    double *value=NULL,step=0,low=0,high=0;
    switch(e->row){
    case 0:value=&e->draft.sensitivity;step=.05;low=.01;high=20;break;
    case 1:value=&e->draft.ads_multiplier;step=.05;low=.01;high=4;break;
    case 2:value=&e->draft.smoothing_ms;step=1;high=100;break;
    case 3:value=&e->draft.deadzone;step=.01;high=.5;break;
    case 4:e->draft.invert_y=!e->draft.invert_y;return;
    case 5:value=&e->draft.walk_speed;step=.05;low=.05;high=1;break;
    }
    if(value)*value=fmax(low,fmin(high,round((*value+step*direction)*100)/100));
}
static void activate(KsEditor *e)
{
    if(e->tab==0){
        e->target=(KsAction)(e->row+1);
        if(e->input_first&&e->input){propose(e);return;}
        e->capture=e->input_first?2:1;e->capture_armed=false;
        snprintf(e->status,sizeof(e->status),"%s",e->input_first?"Release keys, then press the input to map":"Release keys, then press a key or mouse button");
    }else if(e->tab==1)adjust(e,1);
    else if(e->row==0){e->draft=e->global;snprintf(e->status,sizeof(e->status),"Copied global settings into draft");}
    else if(e->row==1){ks_config_defaults(&e->draft);snprintf(e->status,sizeof(e->status),"Defaults restored in draft");}
    else if(e->row==2)ks_editor_commit(e,&e->draft,e->revision,e->title,2);
}
bool ks_editor_tick(KsEditor *e,const KsKeyboard *keys,uint8_t mouse,int dx,int dy,uint64_t now,bool ready)
{
    bool chord=(keys->keys[224]||keys->keys[228])&&(keys->keys[226]||keys->keys[230])&&keys->keys[16];
    bool toggle=(chord&&!e->shortcut_down)||edge(e,keys,0x41);e->shortcut_down=chord;
    if(toggle){
        if(e->open){e->open=false;e->capture=0;e->release_gate=true;e->draft=e->active;}
        else if(ready){e->open=true;e->draft=e->active;e->capture=0;e->tab=0;e->row=0;e->input=0;e->status[0]=0;}
        else{snprintf(e->status,sizeof(e->status),"Sidebar renderer unavailable. Use port 1405 to edit.");e->release_gate=true;}
    }
    if(e->open&&!ready){e->open=false;e->draft=e->active;e->capture=0;e->release_gate=true;snprintf(e->status,sizeof(e->status),"Sidebar renderer lost; input released");}
    bool consume=e->open||e->release_gate;
    if(e->release_gate&&!any_input(keys,mouse))e->release_gate=false;
    if(e->open&&!toggle){
        e->cursor_x=(int)fmax(0,fmin(1919,e->cursor_x+dx));e->cursor_y=(int)fmax(0,fmin(1079,e->cursor_y+dy));
        if(e->capture==1||e->capture==2){
            if(!any_input(keys,mouse))e->capture_armed=true;
            if(e->capture_armed){
                unsigned found=0;
                for(unsigned k=1;k<256;++k)if(keys->keys[k]&&!e->previous.keys[k]&&!ks_key_reserved(k)){found=k;break;}
                for(unsigned b=0;b<3&&!found;++b)if((mouse&(1u<<b))&&!(e->previous_mouse&(1u<<b)))found=256+b;
                if(found){
                    e->input=found;
                    if(e->capture==1)propose(e);
                    else {e->capture=0;char name[24];input_name(found,name);snprintf(e->status,sizeof(e->status),"%s selected. Choose an action and press Enter.",name);}
                }
            }
        }else if(e->capture==3){
            if(edge(e,keys,40))assign(e);
            else if(edge(e,keys,41)){e->capture=0;snprintf(e->status,sizeof(e->status),"Assignment cancelled");}
        }else if(edge(e,keys,41)){e->open=false;e->draft=e->active;e->release_gate=true;}
        else if(edge(e,keys,0x3e))ks_editor_commit(e,&e->draft,e->revision,e->title,0); /* F5 */
        else if(edge(e,keys,0x3f))ks_editor_commit(e,&e->draft,e->revision,e->title,1); /* F6 */
        else if(edge(e,keys,43)){e->tab=(e->tab+1)%3;e->row=0;}
        else if(edge(e,keys,0x3b)){e->input_first=!e->input_first;e->input=0;e->capture=0;}
        else {
            unsigned count=e->tab==0?KS_ACTION_COUNT-1:e->tab==1?6:3;
            bool direction=keys->keys[79]||keys->keys[80]||keys->keys[81]||keys->keys[82];
            bool initial=edge(e,keys,79)||edge(e,keys,80)||edge(e,keys,81)||edge(e,keys,82);
            if(direction&&(initial||now>=e->repeat_at)){
                if(keys->keys[82])e->row=(e->row+count-1)%count;
                else if(keys->keys[81])e->row=(e->row+1)%count;
                else if(e->tab==1)adjust(e,keys->keys[79]?1:-1);
                e->repeat_at=now+(initial?300000:80000);
            }
            if(edge(e,keys,40))activate(e);
            if((mouse&1)&&!(e->previous_mouse&1)){
                int x=e->cursor_x,y=e->cursor_y;
                if(x>=1420&&x<1900&&y>=100&&y<145){e->tab=(unsigned)(x-1420)/160;e->row=0;}
                else if(x>=1420&&x<1900&&y>=185&&y<185+42*KS_SIDEBAR_ROWS){
                    unsigned offset=e->row>=KS_SIDEBAR_ROWS?e->row-KS_SIDEBAR_ROWS+1:0;
                    unsigned row=(unsigned)(y-185)/42+offset;
                    if(row<count){e->row=row;if(e->tab==1)adjust(e,x<1650?-1:1);else activate(e);}
                }else if(y>=970&&y<=1035&&x>=1420&&x<1900){
                    if(x<1560)ks_editor_commit(e,&e->draft,e->revision,e->title,0);
                    else if(x<1770)ks_editor_commit(e,&e->draft,e->revision,e->title,1);
                    else {e->open=false;e->draft=e->active;e->release_gate=true;}
                }
            }
            if(e->tab==0&&edge(e,keys,42)&&e->input){KsAction *a=binding(&e->draft,e->input);if(a)*a=KS_NONE;}
        }
    }
    e->previous=*keys;e->previous_mouse=mouse;
    return consume||e->open||e->release_gate;
}
void ks_editor_frame(const KsEditor *e,uint64_t seq,KsSidebarFrame *f)
{
    memset(f,0,sizeof(*f));f->magic=KS_SIDEBAR_MAGIC;f->version=KS_SIDEBAR_VERSION;f->sequence=seq;
    f->visible=e->open;f->tab=e->tab;f->cursor_x=(uint32_t)e->cursor_x;f->cursor_y=(uint32_t)e->cursor_y;
    snprintf(f->title,sizeof(f->title),"%s",*e->title?e->title:"No game");
    snprintf(f->scope,sizeof(f->scope),"%s",e->game_profile?"Game profile":"Global fallback");
    snprintf(f->heading,sizeof(f->heading),"%s",e->tab==0?(e->input_first?"Input -> controller":"Controller -> input"):e->tab==1?"Mouse sensitivity":"Profiles");
    snprintf(f->status,sizeof(f->status),"%s",e->status);
    unsigned count=e->tab==0?KS_ACTION_COUNT-1:e->tab==1?6:3;
    unsigned offset=e->row>=KS_SIDEBAR_ROWS?e->row-KS_SIDEBAR_ROWS+1:0;
    f->selected_row=e->row-offset;
    for(unsigned row=offset;row<count&&f->row_count<KS_SIDEBAR_ROWS;++row){
        char *line=f->rows[f->row_count++];
        if(e->tab==0){
            KsAction a=(KsAction)(row+1);char bindings[40]="";unsigned shown=0;
            for(unsigned k=1;k<259;++k){
                KsAction old=k<256?e->draft.keys[k]:e->draft.mouse[k-256];
                if(old!=a)continue;
                char name[24];input_name(k,name);
                size_t used=strlen(bindings);
                snprintf(bindings+used,sizeof(bindings)-used,"%s%s",shown?", ":"",name);shown++;
                if(shown>=3)break;
            }
            snprintf(line,80,"%-16s %s",ks_action_name(a),shown?bindings:"Unbound");
        }else if(e->tab==1){
            switch(row){
            case 0:snprintf(line,80,"Sensitivity             < %.2fx >",e->draft.sensitivity);break;
            case 1:snprintf(line,80,"ADS multiplier          < %.2fx >",e->draft.ads_multiplier);break;
            case 2:snprintf(line,80,"Smoothing               < %.0f ms >",e->draft.smoothing_ms);break;
            case 3:snprintf(line,80,"Anti-deadzone           < %.0f%% >",e->draft.deadzone*100);break;
            case 4:snprintf(line,80,"Invert Y                < %s >",e->draft.invert_y?"On":"Off");break;
            case 5:snprintf(line,80,"Walk speed              < %.0f%% >",e->draft.walk_speed*100);break;
            }
        }else snprintf(line,80,"%s",row==0?"Copy global into draft":row==1?"Reset draft to defaults":"Save draft as global default");
    }
    snprintf(f->footer,sizeof(f->footer),"%s",e->capture==1||e->capture==2?"Press a key / mouse button. F8 cancels.":"Tab: section / F2: direction\nF5: Apply / F6: Save / Esc: Cancel");
}
bool ks_sidebar_frame_valid(const KsSidebarFrame *f,size_t size)
{
    if(size!=sizeof(*f)||f->magic!=KS_SIDEBAR_MAGIC||f->version!=KS_SIDEBAR_VERSION||f->visible>1||f->tab>2||f->row_count>KS_SIDEBAR_ROWS||f->selected_row>=KS_SIDEBAR_ROWS||f->cursor_x>1919||f->cursor_y>1079)return false;
    if(!memchr(f->title,0,sizeof(f->title))||!memchr(f->scope,0,sizeof(f->scope))||!memchr(f->heading,0,sizeof(f->heading))||!memchr(f->status,0,sizeof(f->status))||!memchr(f->footer,0,sizeof(f->footer)))return false;
    for(unsigned i=0;i<KS_SIDEBAR_ROWS;++i)if(!memchr(f->rows[i],0,sizeof(f->rows[i])))return false;
    return true;
}
