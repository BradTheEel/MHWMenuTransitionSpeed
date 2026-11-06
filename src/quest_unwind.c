#include "quest_stub.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

extern const unsigned char quest_stub_begin[],quest_stub_end[],quest_code_end[];
extern const unsigned char quest_after_alloc[],quest_after_push[],quest_after_pop[];
extern const unsigned char quest_restore_push[],quest_restore_pop[],quest_after_free[];
extern const unsigned char quest_helper_ptr[],quest_resume_ptr[];
struct QuestStub { unsigned char *memory; RUNTIME_FUNCTION rf[7]; int registered; };
#define OFF(x) ((size_t)((x)-quest_stub_begin))
static void put32(unsigned char *p, unsigned v) { memcpy(p,&v,4); }
/* Aggregate the already-established native frame with the active scratch.
   FAR saves use byte offsets, including the transient 8-byte push regions. */
static void unwind_info(unsigned char *p,unsigned extra) {
    unsigned offsets[3]={extra+0x60,extra+0xd0,extra+0x50};
    unsigned regs[3]={13,6,14};
    memset(p,0,32); p[0]=1; p[1]=0; p[2]=12;
    unsigned k=4;
    for(unsigned i=0;i<3;i++) {
        p[k++]=0; p[k++]=(unsigned char)((regs[i]<<4)|9); put32(p+k,offsets[i]); k+=4;
    }
    p[k++]=0; p[k++]=1; /* UWOP_ALLOC_LARGE, scaled */
    unsigned short scaled=(unsigned short)((0xe0+extra)/8); memcpy(p+k,&scaled,2); k+=2;
    p[k++]=0; p[k++]=0x60; /* UWOP_PUSH_NONVOL RSI */
}
static unsigned char *allocate_near(uintptr_t target) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t gran=si.dwAllocationGranularity;
    uintptr_t lo=target>0x7fff0000u?target-0x7fff0000u:gran;
    uintptr_t hi=target<UINTPTR_MAX-0x7fff0000u?target+0x7fff0000u:UINTPTR_MAX;
    uintptr_t at=lo & ~(gran-1);
    while(at<hi) {
        MEMORY_BASIC_INFORMATION mbi;
        if(!VirtualQuery((void *)at,&mbi,sizeof(mbi))) break;
        uintptr_t base=(uintptr_t)mbi.BaseAddress, end=base+mbi.RegionSize;
        if(end<=at) break;
        if(mbi.State==MEM_FREE) {
            uintptr_t candidate=((base>lo?base:lo)+gran-1)&~(gran-1);
            if(candidate>=lo && candidate<hi && end-candidate>=0x10000) {
                unsigned char *p=VirtualAlloc((void *)candidate,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
                if(p) return p;
            }
        }
        at=end;
    }
    return NULL;
}
int CreateQuestStub(uintptr_t gameBase,QuestRateHelper helper,QuestStub **out) {
    if(!out) return 0; *out=NULL;
    if(!helper || gameBase>UINTPTR_MAX-0x128642bu) return 0;
    size_t size=OFF(quest_stub_end), code=OFF(quest_code_end);
    if(size>0x1000 || code>size) return 0;
    QuestStub *s=calloc(1,sizeof(*s)); if(!s) return 0;
    s->memory=allocate_near(gameBase+0x1286424u); if(!s->memory) { free(s); return 0; }
    memcpy(s->memory,quest_stub_begin,size);
    uintptr_t h=(uintptr_t)helper,resume=gameBase+0x128642bu;
    memcpy(s->memory+OFF(quest_helper_ptr),&h,8);
    memcpy(s->memory+OFF(quest_resume_ptr),&resume,8);
    size_t ends[7]={OFF(quest_after_alloc),OFF(quest_after_push),OFF(quest_after_pop),OFF(quest_restore_push),OFF(quest_restore_pop),OFF(quest_after_free),code};
    unsigned extras[7]={0,0xe0,0xe8,0xe0,0xe8,0xe0,0};
    size_t start=0;
    for(unsigned i=0;i<7;i++) {
        if(ends[i]<=start || ends[i]>code) goto fail;
        unsigned meta=0x1000+i*32;
        unwind_info(s->memory+meta,extras[i]);
        s->rf[i].BeginAddress=(DWORD)start; s->rf[i].EndAddress=(DWORD)ends[i]; s->rf[i].UnwindData=meta;
        start=ends[i];
    }
    DWORD old;
    if(!VirtualProtect(s->memory,0x10000,PAGE_EXECUTE_READ,&old)) goto fail;
    if(!FlushInstructionCache(GetCurrentProcess(),s->memory,size)) goto fail;
    if(!RtlAddFunctionTable(s->rf,7,(DWORD64)(uintptr_t)s->memory)) goto fail;
    s->registered=1; *out=s; return 1;
fail:
    VirtualFree(s->memory,0,MEM_RELEASE); free(s); return 0;
}
void *QuestStubEntry(const QuestStub *s) { return s?s->memory:NULL; }
void DestroyQuestStub(QuestStub *s) {
    if(!s) return;
    if(s->registered && !RtlDeleteFunctionTable(s->rf)) return; /* preserve metadata if deletion fails */
    VirtualFree(s->memory,0,MEM_RELEASE); free(s);
}




