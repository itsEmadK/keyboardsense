/* SPDX-License-Identifier: GPL-3.0-only
 * App-id lookup follows etaHEN's public sceKernelGetAppInfo pattern.
 * Checked Sony kinfo_proc offsets follow ps5-dualsense-overlay; see NOTICE. */
#include "game.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>
struct KsAppInfo { uint32_t app_id; uint64_t unknown; char title[14], reserved[0x3c]; };
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceKernelGetAppInfo(pid_t,struct KsAppInfo *);
int ks_game_query(KsGame *game)
{
    memset(game,0,sizeof(*game));game->pid=-1;
    int app=sceSystemServiceGetAppIdOfRunningBigApp();if(app<=0)return 0;
    int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};size_t size=0;
    if(sysctl(mib,4,NULL,&size,NULL,0)!=0||size==0||size>8*1024*1024)return -1;
    size_t capacity=size+size/4+4096;
    uint8_t *data=malloc(capacity);if(!data)return -1;
    size=capacity;
    if(sysctl(mib,4,data,&size,NULL,0)!=0||size>capacity){free(data);return -1;}
    int result=-1;
    for(size_t offset=0;offset<size;){
        if(size-offset<sizeof(int))break;
        int record;memcpy(&record,data+offset,sizeof(record));
        if(record<4||(size_t)record>size-offset)break;
        if(record>=76){
            pid_t pid;memcpy(&pid,data+offset+72,sizeof(pid));
            struct KsAppInfo info={0};
            if(pid>0&&pid!=getpid()&&sceKernelGetAppInfo(pid,&info)==0&&info.app_id==(uint32_t)app){
                memcpy(game->title,info.title,sizeof(info.title));game->title[sizeof(info.title)]=0;
                if(ks_title_valid(game->title)){game->pid=pid;result=1;break;}
            }
        }
        offset+=(size_t)record;
    }
    free(data);return result;
}
