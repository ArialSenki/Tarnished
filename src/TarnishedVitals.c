#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>
#include <errno.h>
#include "vitals_core.h"

#if defined(TV_HUD_ONLY) && defined(TV_VITALS_ONLY)
#error Build one standalone mod at a time.
#endif


/* HUD builds apply INI changes in game: NativeTweaks' live-reload request
   (include/erms_live_reload_v1.h of NativeTweaks) and a once-per-second poll. */
#if defined(TV_HUD_ONLY)
#define TV_LIVE_RELOAD 1
#endif

#if defined(TV_HUD_ONLY)
#define MOD_INI_NAME L"TarnishedHUD.ini"
#define MOD_LOG_NAME L"TarnishedHUD.log"
#define MOD_BANNER "Tarnished HUD build 6 (Elden Ring 1.17, 1.17.1, 1.17.1 JP, 1.17.2, 1.17.2 JP). Live reload: NativeTweaks Save or INI re-read about once per second."
#else
#define MOD_INI_NAME L"TarnishedVitals.ini"
#define MOD_LOG_NAME L"TarnishedVitals.log"
#if defined(TV_VITALS_ONLY)
#define MOD_BANNER "Tarnished Vitals 0.4.0 preview (resources only). Read INI once; restart game after edits."
#else
#define MOD_BANNER "Tarnished Vitals. Hot-reloads INI about once per second."
#endif
#endif

/* Targets: eldenring.exe FileVersion 2.7.0.0 (1.17), 2.7.1.0 (1.17.1),
   2.7.1.1 (1.17.1, Japanese executable), 2.7.2.0 (1.17.2) and 2.7.2.1 (1.17.2,
   Japanese executable, same code and data layout as 2.7.2.0). Addresses are
   resolved at run time (WorldChrMan by its load pattern, SoloParamRepositoryImp
   by RTTI, its instance slot by the call that constructs it). The first three
   builds share these RVAs (only the slot store moves: 2.7.0.0 0xD22082,
   2.7.1.x 0xD220F2); 2.7.2.0 moves them by 0x3020 (WorldChrMan 0x3D6D018,
   slot 0x3D88F78, store 0xD237E2). The player offset 0x1E508 is unchanged.
   The values below are logged for comparison, never trusted. */
#define WORLD_RVA 0x3D69FF8u
#define DEBUG_RVA 0x3D6A210u
#define PLAYER_OFFSET 0x1E508u
#define SOLO_IMP_VTABLE_RVA 0x02BB84C8u
#define SOLO_INSTANCE_SLOT_RVA 0x03D85F58u
static const char solo_imp_type_name[]=".?AVSoloParamRepositoryImp@CS@@";
static uintptr_t world_address, solo_vtable_address, solo_slot_address;
static const char *game_build_name="unknown";
static HMODULE own_module;
static wchar_t ini_path[32768], log_path[32768];
static ResourceConfig config[3];
typedef struct {
    int enabled, fit_screen, resource_enabled[3], fixed_width[3], custom_limit[3];
    double width[3];
} BarConfig;
typedef struct { uintptr_t row, player_data; int initialized, vanilla[3], baseline_maximum[3], written[3]; } BarState;
static BarConfig bar_config;
#if !defined(TV_HUD_ONLY)
static const wchar_t *sections[3] = {L"Health", L"FP", L"Stamina"};
#endif
#if !defined(TV_VITALS_ONLY)
static const wchar_t *bar_enabled_keys[3] = {L"HPEnabled", L"FPEnabled", L"StaminaEnabled"};
static const wchar_t *bar_fixed_keys[3] = {L"HPFixedWidth", L"FPFixedWidth", L"StaminaFixedWidth"};
static const wchar_t *bar_width_keys[3] = {L"HPWidth", L"FPWidth", L"StaminaWidth"};
static const wchar_t *bar_limit_keys[3] = {L"HPVanillaLimit", L"FPVanillaLimit", L"StaminaVanillaLimit"};
#endif
static const size_t current_offsets[3] = {0x138, 0x148, 0x154};
static const size_t maximum_offsets[3] = {0x13C, 0x14C, 0x158};
static const size_t base_offsets[3] = {0x144, 0x150, 0x15C};

static void log_line(const char *format, ...) {
#if defined(TV_AUDIT)
    va_list audit_args; va_start(audit_args, format); vprintf(format, audit_args); va_end(audit_args);
    putchar('\n');
#else
    FILE *f = _wfopen(log_path, L"a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u ", t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    va_list a; va_start(a, format); vfprintf(f, format, a); va_end(a);
    fputc('\n', f); fclose(f);
#endif
}

static int read_memory(uintptr_t at, void *out, size_t size) {
    SIZE_T got = 0;
    return at > 0x10000 && ReadProcessMemory(GetCurrentProcess(), (void*)at, out, size, &got) && got == size;
}
static int write_memory(uintptr_t at, const void *in, size_t size) {
    SIZE_T got = 0;
    return at > 0x10000 && WriteProcessMemory(GetCurrentProcess(), (void*)at, in, size, &got) && got == size;
}
static uintptr_t pointer_at(uintptr_t at) {
    uintptr_t p = 0;
    return read_memory(at, &p, sizeof(p)) ? p : 0;
}
static int integer_at(uintptr_t at, int *n) { return read_memory(at,n,sizeof(*n)); }

static int paths(void) {
    DWORD n = GetModuleFileNameW(own_module, ini_path, 32768);
    if (!n || n >= 32700) return 0;
    wchar_t *slash = wcsrchr(ini_path,L'\\');
    if (!slash) return 0;
    wcscpy(slash+1,MOD_INI_NAME);
    wcscpy(log_path,ini_path);
    wcscpy(wcsrchr(log_path,L'\\')+1,MOD_LOG_NAME);
    return 1;
}

static int number(const wchar_t *section, const wchar_t *key, double def,
                  double min, double max, double *out) {
    wchar_t text[128], fallback[64], *end;
    swprintf(fallback,64,L"%.8g",def);
    DWORD n = GetPrivateProfileStringW(section,key,fallback,text,128,ini_path);
    errno=0; double value=wcstod(text,&end);
    while (*end==L' ' || *end==L'\t') ++end;
    if (!n || n>=127 || end==text || *end || errno || !isfinite(value) || value<min || value>max) {
        log_line("Invalid setting [%ls] %ls (value=%ls; allowed %.3g to %.3g, decimal point). Mod remains inactive.",section,key,text,min,max); return 0;
    }
    *out=value; return 1;
}
static int boolean(const wchar_t *section,const wchar_t *key,int def,int *out) {
    double d;
    if (!number(section,key,def,0,1,&d) || (d!=0 && d!=1)) {
        log_line("Expected 0 or 1: [%ls] %ls",section,key); return 0;
    }
    *out=(int)d; return 1;
}
#if !defined(TV_VITALS_ONLY)
static int integer_number(const wchar_t *section,const wchar_t *key,int def,int min,int max,int *out) {
    double value;
    if (!number(section,key,def,min,max,&value)) return 0;
    if (value!=floor(value)) { log_line("Expected a whole number: [%ls] %ls",section,key); return 0; }
    *out=(int)value; return 1;
}
#endif
/* Returns 1 when the mod is enabled, 2 when the file is valid but
   General.Enabled = 0, and 0 when a value is invalid (callers keep the
   previous configuration then). */
static int load_config(void) {
    int enabled=0;
    if (GetFileAttributesW(ini_path)==INVALID_FILE_ATTRIBUTES) {
        log_line("INI missing; nothing changed."); return 0;
    }
    if (!boolean(L"General",L"Enabled",0,&enabled)) return 0;
    if (!enabled) { log_line("Disabled by General.Enabled."); return 2; }
    #if !defined(TV_HUD_ONLY)
    for (int i=0;i<3;++i) {
        ResourceConfig *c=&config[i]; const wchar_t *s=sections[i];
        if (!boolean(s,L"Enabled",0,&c->enabled) ||
            !boolean(s,L"ModifyMaximum",0,&c->modify_maximum) ||
            !boolean(s,L"Infinite",0,&c->infinite) ||
            !boolean(s,L"Regeneration",0,&c->regenerate) ||
            !number(s,L"MaximumMultiplier",1,0.1,100,&c->maximum_multiplier) ||
            !number(s,L"MaximumAdd",0,0,1000000,&c->maximum_add) ||
            !number(s,L"RegenPointsPerSecond",0,0,1000000,&c->points_per_second) ||
            !number(s,L"RegenPercentPerSecond",0,0,100,&c->percent_per_second)) return 0;
    }
    #endif
    #if !defined(TV_VITALS_ONLY)
    int common_fixed=1;
    if (!boolean(L"BarSize",L"Enabled",0,&bar_config.enabled) ||
        !boolean(L"BarSize",L"FitScreen",0,&bar_config.fit_screen) ||
        !boolean(L"BarSize",L"FixedWidth",1,&common_fixed)) return 0;
    for (int i=0;i<3;++i) {
        int fixed_override=-1;
        if (!boolean(L"BarSize",bar_enabled_keys[i],1,&bar_config.resource_enabled[i]) ||
            !integer_number(L"BarSize",bar_fixed_keys[i],-1,-1,1,&fixed_override) ||
            !number(L"BarSize",bar_width_keys[i],1,bar_config.fit_screen?0.01:0.1,bar_config.fit_screen?1:10,&bar_config.width[i]) ||
            !integer_number(L"BarSize",bar_limit_keys[i],0,0,1000000,&bar_config.custom_limit[i])) return 0;
        bar_config.fixed_width[i]=fixed_override<0?common_fixed:fixed_override;
    }
    #endif
    return 1;
}

typedef struct { DWORD ms, ls; const char *name; } GameBuild;
static const GameBuild supported_builds[]={
#if defined(TV_HUD_ONLY)
    {0x00020007,0x00000000,"1.17 (2.7.0.0)"},
    {0x00020007,0x00010000,"1.17.1 (2.7.1.0)"},
    {0x00020007,0x00010001,"1.17.1 JP (2.7.1.1)"},
    {0x00020007,0x00020000,"1.17.2 (2.7.2.0)"},
    {0x00020007,0x00020001,"1.17.2 JP (2.7.2.1)"},
#else
    {0x00020007,0x00010000,"1.17.1 (2.7.1.0)"},
#endif
};

static int check_version_of(const wchar_t *exe) {
    const wchar_t *name=wcsrchr(exe,L'\\');
    if (!name || _wcsicmp(name+1,L"eldenring.exe")) {
        log_line("Host is not eldenring.exe; nothing changed."); return 0;
    }
    DWORD ignored=0, size=GetFileVersionInfoSizeW(exe,&ignored);
    if (!size) return 0;
    void *buffer=malloc(size); if (!buffer) return 0;
    VS_FIXEDFILEINFO *v=NULL; UINT len=0;
    int ok=GetFileVersionInfoW(exe,0,size,buffer) && VerQueryValueW(buffer,L"\\",(void**)&v,&len) && len>=sizeof(*v);
    if (ok) {
        log_line("Executable version: %u.%u.%u.%u",HIWORD(v->dwFileVersionMS),LOWORD(v->dwFileVersionMS),HIWORD(v->dwFileVersionLS),LOWORD(v->dwFileVersionLS));
        ok=0;
        for (size_t i=0;i<sizeof(supported_builds)/sizeof(supported_builds[0]);++i)
            if (v->dwFileVersionMS==supported_builds[i].ms && v->dwFileVersionLS==supported_builds[i].ls) {
                ok=1; game_build_name=supported_builds[i].name;
            }
    }
    free(buffer);
    if (ok) log_line("Supported game: Elden Ring %s.",game_build_name);
    else log_line("Unsupported executable version. Supported: %s. Nothing changed.",
#if defined(TV_HUD_ONLY)
        "2.7.0.0, 2.7.1.0, 2.7.1.1, 2.7.2.0, 2.7.2.1"
#else
        "2.7.1.0"
#endif
        );
    return ok;
}

static int check_version(void) {
    wchar_t exe[32768];
    if (!GetModuleFileNameW(NULL,exe,32768)) return 0;
    return check_version_of(exe);
}

typedef struct { uintptr_t image; IMAGE_SECTION_HEADER text, rdata, data; size_t image_size; uint64_t image_base; } ImageSections;
static int image_sections(uintptr_t image,ImageSections *out) {
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; IMAGE_SECTION_HEADER sec;
    memset(out,0,sizeof(*out)); out->image=image;
    if (!read_memory(image,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000) return 0;
    if (!read_memory(image+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64) return 0;
    out->image_size=nt.OptionalHeader.SizeOfImage; out->image_base=nt.OptionalHeader.ImageBase;
    uintptr_t table=image+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i=0;i<nt.FileHeader.NumberOfSections;++i) {
        if (!read_memory(table+i*sizeof(sec),&sec,sizeof(sec))) return 0;
        if (sec.Misc.VirtualSize>out->image_size || sec.VirtualAddress>out->image_size-sec.Misc.VirtualSize) return 0;
        /* The first section of each name is the game's own; later .text is the protection layer. */
        if (!memcmp(sec.Name,".text\0",6) && !out->text.Misc.VirtualSize) out->text=sec;
        else if (!memcmp(sec.Name,".rdata\0",7) && !out->rdata.Misc.VirtualSize) out->rdata=sec;
        else if (!memcmp(sec.Name,".data\0",6) && !out->data.Misc.VirtualSize) out->data=sec;
    }
    return out->text.Misc.VirtualSize && out->rdata.Misc.VirtualSize && out->data.Misc.VirtualSize;
}
static unsigned char *copy_section(const ImageSections *s,const IMAGE_SECTION_HEADER *sec) {
    unsigned char *copy=malloc(sec->Misc.VirtualSize);
    if (copy && !read_memory(s->image+sec->VirtualAddress,copy,sec->Misc.VirtualSize)) { free(copy); copy=NULL; }
    return copy;
}

/* WorldChrMan: exactly one "mov rax,[rip+X]; test rax,rax; jz +0F; cmp [rax+..]" load. */
static void resolve_solo(const ImageSections *s);

static int check_layout(uintptr_t image) {
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; ImageSections s;
    if (!image_sections(image,&s)) { log_line("Layout check: FAIL (PE headers)"); return 0; }
    if (!read_memory(image,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000) return 0;
    if (!read_memory(image+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.SizeOfImage<=DEBUG_RVA+0x20) return 0;
    uintptr_t table=image+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
    int found=0; uintptr_t target=0;
    const unsigned char prefix[]={0x48,0x8B,0x05};
    const unsigned char suffix[]={0x48,0x85,0xC0,0x74,0x0F,0x48,0x39,0x88};
    for (WORD i=0;i<nt.FileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER sec;
        if (!read_memory(table+i*sizeof(sec),&sec,sizeof(sec))) return 0;
        if (memcmp(sec.Name,".text",5)) continue;
        size_t size=sec.Misc.VirtualSize;
        if (size>nt.OptionalHeader.SizeOfImage || sec.VirtualAddress>nt.OptionalHeader.SizeOfImage-size) return 0;
        unsigned char *copy=malloc(size); if (!copy) return 0;
        if (!read_memory(image+sec.VirtualAddress,copy,size)) { free(copy); return 0; }
        for (size_t j=0;j+15<=size;++j) {
            if (!memcmp(copy+j,prefix,3) && !memcmp(copy+j+7,suffix,8)) {
                int32_t rel; memcpy(&rel,copy+j+3,4);
                target=image+sec.VirtualAddress+j+7+rel; ++found;
            }
        }
        free(copy);
    }
    uintptr_t data_start=image+s.data.VirtualAddress, data_end=data_start+s.data.Misc.VirtualSize;
    int ok=found==1 && target>=data_start && target<data_end;
    log_line("Layout check: %s (%d signature matches, WorldChrMan at RVA 0x%llX; 1.17-1.17.1 use 0x%X)",
        ok?"PASS":"FAIL",found,(unsigned long long)(found==1?target-image:0),WORLD_RVA);
    if (!ok) return 0;
    world_address=target;
    resolve_solo(&s);
    return 1;
}

static uintptr_t player_data(uintptr_t image, uintptr_t *player) {
    (void)image;
    uintptr_t world=pointer_at(world_address);
    if (!world) return 0;
    *player=pointer_at(world+PLAYER_OFFSET);
    if (!*player) return 0;
    uintptr_t modules=pointer_at(*player+0x190);
    return modules ? pointer_at(modules) : 0;
}

enum InstructionKind { IK_NONE, IK_LEA_RCX, IK_LEA_R8, IK_LEA_R9, IK_MOV_R9, IK_MOV_EDX };
typedef struct { enum InstructionKind kind; size_t position, length; } Instruction;
typedef const char *(__cdecl *SingletonNameFunction)(const void *reflection);

static int bytes_at(const unsigned char *text,size_t size,size_t at,const unsigned char *value,size_t n) {
    return at<=size && n<=size-at && !memcmp(text+at,value,n);
}
static enum InstructionKind instruction_at(const unsigned char *text,size_t size,size_t at,size_t *length) {
    static const unsigned char lea_rcx[]={0x48,0x8D,0x0D};
    static const unsigned char lea_r8[]={0x4C,0x8D,0x05};
    static const unsigned char lea_r9[]={0x4C,0x8D,0x0D};
    static const unsigned char mov_r9[]={0x4C,0x8B,0xC8};
    if (bytes_at(text,size,at,lea_rcx,3)) { *length=7; return IK_LEA_RCX; }
    if (bytes_at(text,size,at,lea_r8,3)) { *length=7; return IK_LEA_R8; }
    if (bytes_at(text,size,at,lea_r9,3)) { *length=7; return IK_LEA_R9; }
    if (bytes_at(text,size,at,mov_r9,3)) { *length=3; return IK_MOV_R9; }
    return IK_NONE;
}
static int singleton_candidate(const unsigned char *text,size_t size,size_t mov_edx,
                               size_t *first,size_t *derived_name_disp) {
    Instruction in[4]={{IK_MOV_EDX,mov_edx,5}}; int count=1;
    while (count<4) {
        size_t next=in[count-1].position+in[count-1].length, length=0;
        enum InstructionKind kind=instruction_at(text,size,next,&length);
        if (kind!=IK_NONE) in[count++]=(Instruction){kind,next,length};
        else if (next<size && text[next]==0xE8) break;
        else return 0;
    }
    while (count<4) {
        size_t position=in[0].position, length=0; enum InstructionKind kind;
        if (position<7) return 0;
        kind=instruction_at(text,size,position-7,&length);
        if (kind==IK_NONE) {
            kind=instruction_at(text,size,position-3,&length);
            if (kind!=IK_MOV_R9) return 0;
            position-=3;
        } else position-=7;
        memmove(in+1,in,(size_t)count*sizeof(*in)); in[0]=(Instruction){kind,position,length}; ++count;
    }
    int mask=0;
    for (int i=0;i<4;++i) {
        if (in[i].kind==IK_LEA_RCX) mask|=1;
        else if (in[i].kind==IK_LEA_R8) mask|=2;
        else if (in[i].kind==IK_LEA_R9) mask|=4;
        else if (in[i].kind==IK_MOV_R9) mask|=8;
    }
    *first=in[0].position;
    *derived_name_disp=0;
    if (mask==7) {
        for (int i=0;i<4;++i) if (in[i].kind==IK_LEA_R9) {
            *derived_name_disp=in[i].position+3; break;
        }
        if (!*derived_name_disp) return 0;
    }
    return mask;
}
static int conditional_jump_before(const unsigned char *text,size_t size,size_t position,size_t *jump) {
    if (position>=2 && text[position-2]==0x75) { *jump=position-2; return 1; }
    if (position>=6 && text[position-6]==0x0F && text[position-5]==0x85) { *jump=position-6; return 1; }
    (void)size; return 0;
}
static uintptr_t relative_target(uintptr_t section,size_t displacement_position,const unsigned char *text) {
    int32_t displacement; memcpy(&displacement,text+displacement_position,4);
    return section+displacement_position+4+(intptr_t)displacement;
}

static int read_c_string(uintptr_t at,char *out,size_t capacity);

/* Current builds keep the derived singleton name in .rdata. Its initializer
   loads the static instance pointer, tests it, then references that name. */
static uintptr_t derived_static_by_name(uintptr_t image,IMAGE_SECTION_HEADER text_sec,
                                        IMAGE_SECTION_HEADER data_sec,IMAGE_SECTION_HEADER rdata_sec,
                                        const unsigned char *text,size_t text_size,const char *wanted) {
    size_t rdata_size=rdata_sec.Misc.VirtualSize, wanted_size=strlen(wanted)+1;
    if (!rdata_size || wanted_size>rdata_size) return 0;
    unsigned char *rdata=malloc(rdata_size); if (!rdata) return 0;
    if (!read_memory(image+rdata_sec.VirtualAddress,rdata,rdata_size)) { free(rdata); return 0; }
    uintptr_t result=0, text_start=image+text_sec.VirtualAddress;
    uintptr_t data_start=image+data_sec.VirtualAddress, data_end=data_start+data_sec.Misc.VirtualSize;
    static const unsigned char lea_rcx[]={0x48,0x8D,0x0D};
    static const unsigned char mov_rax[]={0x48,0x8B,0x05};
    static const unsigned char test_rax[]={0x48,0x85,0xC0};
    for (size_t name_at=0;name_at+wanted_size<=rdata_size && !result;++name_at) {
        if (memcmp(rdata+name_at,wanted,wanted_size)) continue;
        uintptr_t name_address=image+rdata_sec.VirtualAddress+name_at;
        for (size_t lea=0;lea+7<=text_size && !result;++lea) {
            if (!bytes_at(text,text_size,lea,lea_rcx,3) || relative_target(text_start,lea+3,text)!=name_address) continue;
            size_t earliest=lea>40?lea-40:0;
            for (size_t mov=lea;mov-- > earliest;) {
                if (!bytes_at(text,text_size,mov,mov_rax,3) ||
                    !bytes_at(text,text_size,mov+7,test_rax,3)) continue;
                uintptr_t address=relative_target(text_start,mov+3,text);
                if (address>=data_start && address<data_end) { result=address; break; }
            }
        }
    }
    free(rdata); return result;
}

/* Locate FromSoftware singletons through Dantelion2 reflection. Pattern logic
   follows from-singleton 3.0.1 (MIT OR Apache-2.0), adapted to C. */
static uintptr_t singleton_named(uintptr_t image,const char *wanted) {
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; IMAGE_SECTION_HEADER sec, text_sec={0}, data_sec={0}, rdata_sec={0};
    if (!read_memory(image,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE) return 0;
    if (!read_memory(image+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE) return 0;
    uintptr_t table=image+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i=0;i<nt.FileHeader.NumberOfSections;++i) {
        if (!read_memory(table+i*sizeof(sec),&sec,sizeof(sec))) return 0;
        if (!memcmp(sec.Name,".text",5) && !text_sec.Misc.VirtualSize) text_sec=sec;
        else if (!memcmp(sec.Name,".data",5) && !data_sec.Misc.VirtualSize) data_sec=sec;
        else if (!memcmp(sec.Name,".rdata",6) && !rdata_sec.Misc.VirtualSize) rdata_sec=sec;
    }
    if (!text_sec.Misc.VirtualSize || !data_sec.Misc.VirtualSize || !rdata_sec.Misc.VirtualSize) return 0;
    size_t size=text_sec.Misc.VirtualSize; unsigned char *text=malloc(size); if (!text) return 0;
    if (!read_memory(image+text_sec.VirtualAddress,text,size)) { free(text); return 0; }
    uintptr_t current_static=derived_static_by_name(image,text_sec,data_sec,rdata_sec,text,size,wanted);
    if (current_static) { uintptr_t value=pointer_at(current_static); if (value) { free(text); return value; } }
    uintptr_t static_addresses[512], reflections[512], name_function=0, derived_static=0;
    size_t found=0; int function_mismatch=0;
    uintptr_t text_start=image+text_sec.VirtualAddress, text_end=text_start+size;
    uintptr_t data_start=image+data_sec.VirtualAddress, data_end=data_start+data_sec.Misc.VirtualSize;
    for (size_t start=0;start<size && found<512;++start) {
        size_t first, derived_name_disp; int candidate_kind;
        if (text[start]!=0xBA || !(candidate_kind=singleton_candidate(text,size,start,&first,&derived_name_disp)) ||
            (candidate_kind!=7 && candidate_kind!=11)) continue;
        if (candidate_kind==7) {
            size_t jump; if (!conditional_jump_before(text,size,first,&jump) || jump<3) continue;
            const unsigned char *test=text+jump-3; unsigned char test_rex=test[0], test_modrm=test[2];
            int test_reg1=test_modrm&7, test_reg2=(test_modrm>>3)&7;
            if ((test_rex&0xF8)!=0x48 || (test_modrm&0xC0)!=0xC0 || test_reg1!=test_reg2) continue;
            for (size_t mov_pad=0;mov_pad<=3;++mov_pad) {
                if (jump<3+7+mov_pad) continue;
                size_t mov=jump-3-7-mov_pad; const unsigned char *m=text+mov;
                unsigned char rex=m[0], modrm=m[2];
                if ((rex&0xF8)!=0x48 || (modrm&0xC0)!=0 || (modrm&7)!=5 ||
                    (((rex>>2)&1)!=(test_rex&1)) || ((modrm>>3)&7)!=test_reg1) continue;
                uintptr_t address=relative_target(text_start,mov+3,text);
                uintptr_t name_address=relative_target(text_start,derived_name_disp,text);
                if (address<data_start || address>=data_end) break;
                char name[128];
                if (read_c_string(name_address,name,sizeof(name)) && !strcmp(name,wanted)) derived_static=address;
                break;
            }
            continue;
        }
        for (size_t call_pad=0;call_pad<=1;++call_pad) {
            if (first<5+call_pad) continue;
            size_t call=first-5-call_pad; if (text[call]!=0xE8 || call<7) continue;
            size_t lea=call-7; static const unsigned char lea_rcx[]={0x48,0x8D,0x0D};
            if (!bytes_at(text,size,lea,lea_rcx,3)) continue;
            size_t jump; if (!conditional_jump_before(text,size,lea,&jump) || jump<3) continue;
            const unsigned char *test=text+jump-3; unsigned char test_rex=test[0], test_modrm=test[2];
            int test_reg1=test_modrm&7, test_reg2=(test_modrm>>3)&7;
            if ((test_rex&0xF8)!=0x48 || (test_modrm&0xC0)!=0xC0 || test_reg1!=test_reg2) continue;
            for (size_t mov_pad=0;mov_pad<=3;++mov_pad) {
                if (jump<3+7+mov_pad) continue;
                size_t mov=jump-3-7-mov_pad; const unsigned char *m=text+mov;
                unsigned char rex=m[0], modrm=m[2];
                if ((rex&0xF8)!=0x48 || (modrm&0xC0)!=0 || (modrm&7)!=5 ||
                    (((rex>>2)&1)!=(test_rex&1)) || ((modrm>>3)&7)!=test_reg1) continue;
                uintptr_t address=relative_target(text_start,mov+3,text);
                uintptr_t reflection=relative_target(text_start,lea+3,text);
                uintptr_t function=relative_target(text_start,call+1,text);
                if (address<data_start || address>=data_end || function<text_start || function>=text_end) break;
                int duplicate=0; for (size_t i=0;i<found;++i) if (static_addresses[i]==address) duplicate=1;
                if (!duplicate) { static_addresses[found]=address; reflections[found]=reflection; ++found; }
                if (!name_function) name_function=function; else if (name_function!=function) function_mismatch=1;
                break;
            }
            break;
        }
    }
    free(text);
    if (derived_static) { uintptr_t value=pointer_at(derived_static); if (value) return value; }
    if (!found || !name_function || function_mismatch) return 0;
    int initialized=0; for (size_t i=0;i<found;++i) if (pointer_at(static_addresses[i])) { initialized=1; break; }
    if (!initialized) return 0;
    SingletonNameFunction get_name=(SingletonNameFunction)name_function;
    for (size_t i=0;i<found;++i) {
        const char *name=get_name((const void*)reflections[i]);
        if (name && !strcmp(name,wanted)) { uintptr_t value=pointer_at(static_addresses[i]); if (value) return value; }
    }
    return 0;
}

static int read_c_string(uintptr_t at,char *out,size_t capacity) {
    if (!capacity) return 0;
    for (size_t i=0;i<capacity;++i) {
        if (!read_memory(at+i,out+i,1)) return 0;
        if (!out[i]) return 1;
    }
    out[capacity-1]=0; return 0;
}

static void format_qword_dump(uintptr_t base,size_t offset,size_t count,char *out,size_t capacity) {
    size_t used=0;
    if (!capacity) return;
    out[0]=0;
    for (size_t i=0;i<count && used+32<capacity;++i) {
        uint64_t value=0;
        int ok=read_memory(base+offset+i*8,&value,sizeof(value));
        int n=ok?snprintf(out+used,capacity-used,"%s+%zX:%016llX",i?" ":"",offset+i*8,(unsigned long long)value):snprintf(out+used,capacity-used,"%s+%zX:????????????????",i?" ":"",offset+i*8);
        if (n<0 || (size_t)n>=capacity-used) break;
        used+=(size_t)n;
    }
}

enum MenuLookupStage {
    MENU_LOOKUP_NOT_STARTED=0,
    MENU_LOOKUP_SINGLETON_MISSING=1,
    MENU_LOOKUP_REPOSITORY_FOUND=2,
    MENU_LOOKUP_HOLDER_INVALID=3,
    MENU_LOOKUP_PARAM_FILE_FOUND=4,
    MENU_LOOKUP_MENU_PARAM_FOUND=5,
    MENU_LOOKUP_READY=6
};
static int menu_lookup_stage=MENU_LOOKUP_NOT_STARTED;
static int menu_lookup_bucket=-1;
static int menu_lookup_holder_offset=0;
static uint32_t menu_lookup_bucket_count=0;
static int menu_lookup_route=0;
static uintptr_t fd4_repository_diagnostic=0, fd4_buckets_diagnostic=0;
static uint32_t fd4_bucket_count_diagnostic=0, fd4_bucket_scan_limit=0, fd4_entries_visited=0, fd4_files_seen=0;
static uintptr_t fd4_first_head=0, fd4_first_entry=0, fd4_candidate_entry=0, fd4_candidate_file=0;
static uint64_t fd4_candidate_size=0;
static uint32_t solo_holder143_count=0;
static uintptr_t solo_holder143_first=0, solo_param_cap_diagnostic=0, solo_param_file_diagnostic=0;
static uint64_t solo_param_file_size=0;
static uintptr_t solo_repository_diagnostic=0;
static uint32_t solo_holder_base=0, solo_holder_stride=0;
static int solo_rtti_valid=0;
static uintptr_t solo_rtti_vtable=0, solo_rtti_first_candidate=0, solo_slot_value=0, solo_slot_vtable=0;
static char menu_lookup_last_param_name[64]="";
static uint16_t menu_lookup_last_row_count=0;
static unsigned char menu_lookup_last_format=0;

static uintptr_t menu_row_from_file(uintptr_t file,uint64_t file_size) {
    unsigned char header[0x30];
    if (!read_memory(file,header,sizeof(header))) return 0;
    uint16_t rows; memcpy(&rows,header+0x0A,2);
    menu_lookup_last_row_count=rows;
    unsigned char format=header[0x2D]; menu_lookup_last_format=format;
    char name[64]={0}; uintptr_t name_at=file+0x0C;
    if (format&0x80) {
        uint32_t offset; memcpy(&offset,header+0x10,4);
        if (!offset || offset>=file_size) return 0;
        name_at=file+offset;
    }
    if (!read_c_string(name_at,name,sizeof(name))) { strcpy(menu_lookup_last_param_name,"<unreadable>"); return 0; }
    strcpy(menu_lookup_last_param_name,name);
    if (!rows || strcmp(name,"MENU_COMMON_PARAM_ST")) return 0;
    menu_lookup_stage=MENU_LOOKUP_MENU_PARAM_FOUND;
    size_t descriptor=0x30+(((format&4)||((format&1)&&(format&2)))?0x10:0);
    uint32_t row_id=1; if (!read_memory(file+descriptor,&row_id,4) || row_id!=0) return 0;
    uint64_t data_offset=0;
    if (format&4) { if (!read_memory(file+descriptor+8,&data_offset,8)) return 0; }
    else { uint32_t short_offset; if (!read_memory(file+descriptor+4,&short_offset,4)) return 0; data_offset=short_offset; }
    if (data_offset+0x14>file_size) return 0;
    uintptr_t row=file+(uintptr_t)data_offset; int limits[3];
    if (!read_memory(row+8,limits,sizeof(limits))) return 0;
    for (int i=0;i<3;++i) if (limits[i]<1 || limits[i]>1000000) return 0;
    menu_lookup_stage=MENU_LOOKUP_READY;
    return row;
}

static uintptr_t menu_common_row_general(uintptr_t repository) {
    /* FD4ParamRepository embeds FD4ResCapHolder at +0x78. Its bucket count
       and buckets pointer are at +0x94/+0x98. Each entry is FD4ParamResCap:
       next_item at +0x50, file size at +0x78 and file data at +0x80. */
    uint32_t bucket_count=0;
    if (!read_memory(repository+0x94,&bucket_count,sizeof(bucket_count)) || bucket_count>65536) return 0;
    uintptr_t buckets=pointer_at(repository+0x98);
    if (!buckets || (buckets&7)) return 0;
    fd4_bucket_count_diagnostic=bucket_count;
    fd4_buckets_diagnostic=buckets;
    fd4_first_head=fd4_first_entry=fd4_candidate_entry=fd4_candidate_file=0;
    fd4_candidate_size=0;
    /* A zero count is not a table. Scanning it produced false positives in
       2.7.0.0 because the pointer was actually a class-name string. */
    if (!bucket_count) return 0;
    uint32_t scan_limit=bucket_count;
    fd4_bucket_scan_limit=scan_limit;
    menu_lookup_holder_offset=0x94;
    menu_lookup_bucket_count=bucket_count;
    size_t visited=0;
    uintptr_t heads[128];
    for (uint32_t block=0;block<scan_limit && visited<65536;block+=128) {
        uint32_t block_count=scan_limit-block; if (block_count>128) block_count=128;
        uintptr_t *block_at=(uintptr_t*)(buckets+(uintptr_t)block*sizeof(uintptr_t));
        if (!read_memory((uintptr_t)block_at,heads,(size_t)block_count*sizeof(uintptr_t))) {
            for (uint32_t i=0;i<block_count;++i) heads[i]=pointer_at((uintptr_t)block_at+(uintptr_t)i*sizeof(uintptr_t));
        }
        for (uint32_t item=0;item<block_count && visited<65536;++item) {
        uint32_t bucket_index=block+item;
        uintptr_t current=heads[item];
        if (current && !fd4_first_head) fd4_first_head=current;
        size_t chain=0;
        while (current && chain++<1024 && visited++<65536) {
            ++fd4_entries_visited;
            if (!fd4_first_entry) fd4_first_entry=current;
            uint64_t file_size=0;
            if (read_memory(current+0x78,&file_size,sizeof(file_size)) && file_size>=0x60 && file_size<=0x1000000) {
                uintptr_t file=pointer_at(current+0x80);
                if (file) {
                    ++fd4_files_seen;
                    if (!fd4_candidate_entry) {
                        fd4_candidate_entry=current; fd4_candidate_file=file; fd4_candidate_size=file_size;
                    }
                    menu_lookup_stage=MENU_LOOKUP_PARAM_FILE_FOUND;
                    uintptr_t row=menu_row_from_file(file,file_size);
                    if (row) { menu_lookup_bucket=(int)bucket_index; return row; }
                }
            }
            uintptr_t next=pointer_at(current+0x50);
            if (next==current) break;
            current=next;
        }
        }
    }
    return 0;
}

static uintptr_t menu_common_row_solo(uintptr_t repository) {
    /* The 2.7.1.0 constructor allocates 0x37C8 bytes and accesses a member
       at +0x3710, matching 194 holders of 0x48 bytes from +0x80. Try the
       known index first, then verify every other holder by the param name. */
    solo_holder143_count=0; solo_holder143_first=0;
    solo_param_cap_diagnostic=solo_param_file_diagnostic=0; solo_param_file_size=0;
    solo_holder_base=0x80; solo_holder_stride=0x48;
    for (uint32_t order=0;order<194;++order) {
        uint32_t holder_index=order?order-1:143;
        if (order && holder_index>=143) ++holder_index;
        uintptr_t holder=repository+solo_holder_base+(uintptr_t)holder_index*solo_holder_stride;
        uint32_t count=0;
        if (!read_memory(holder,&count,sizeof(count))) continue;
        if (holder_index==143) solo_holder143_count=count;
        if (count<1 || count>8) continue;
        for (uint32_t cap_index=0;cap_index<count;++cap_index) {
            uintptr_t wrapper=pointer_at(holder+8u+(uintptr_t)cap_index*8u);
            if (!wrapper) continue;
            if (holder_index==143 && !solo_holder143_first) solo_holder143_first=wrapper;
            uintptr_t param_cap=pointer_at(wrapper+0x80);
            if (!param_cap) continue;
            uint64_t file_size=0;
            if (!read_memory(param_cap+0x78,&file_size,sizeof(file_size)) || file_size<0x60 || file_size>0x1000000) continue;
            uintptr_t file=pointer_at(param_cap+0x80);
            if (!file) continue;
            solo_param_cap_diagnostic=param_cap; solo_param_file_diagnostic=file; solo_param_file_size=file_size;
            menu_lookup_stage=MENU_LOOKUP_PARAM_FILE_FOUND;
            uintptr_t row=menu_row_from_file(file,file_size);
            if (row) { menu_lookup_bucket=(int)holder_index; return row; }
        }
    }
    return 0;
}

/* SoloParamRepositoryImp by RTTI: type descriptor (name in .data) -> complete
   object locator in .rdata (signature 1, self RVA) -> vtable after the COL
   pointer. Its instance slot is the .data qword written right after a call to
   a function that loads that vtable (the repository constructor). */
static void resolve_solo(const ImageSections *s) {
    solo_vtable_address=solo_slot_address=0;
    uintptr_t image=s->image;
    unsigned char *data=copy_section(s,&s->data), *rdata=copy_section(s,&s->rdata), *text=copy_section(s,&s->text);
    uint32_t type_rva=0, col_rva=0, vtable_rva=0, slot_rva=0, store_rva=0, leas[8];
    int vtables=0, slots=0, conflict=0, lea_count=0;
    if (!data || !rdata || !text) { log_line("RTTI lookup: out of memory."); goto done; }
    size_t data_size=s->data.Misc.VirtualSize, rdata_size=s->rdata.Misc.VirtualSize, text_size=s->text.Misc.VirtualSize;
    for (size_t i=0x10;i+sizeof(solo_imp_type_name)<=data_size;++i)
        if (!memcmp(data+i,solo_imp_type_name,sizeof(solo_imp_type_name))) { type_rva=(uint32_t)(s->data.VirtualAddress+i-0x10); break; }
    if (!type_rva) goto done;
    for (size_t i=0;i+24<=rdata_size;i+=4) {
        uint32_t col[6]; memcpy(col,rdata+i,sizeof(col));
        if (col[0]==1 && !col[1] && !col[2] && col[3]==type_rva && col[5]==s->rdata.VirtualAddress+i) { col_rva=col[5]; break; }
    }
    if (!col_rva) goto done;
    for (size_t i=0;i+16<=rdata_size;i+=8) {
        uint64_t q; memcpy(&q,rdata+i,8);
        if (q!=s->image_base+col_rva && q!=image+col_rva) continue;
        uint64_t first; memcpy(&first,rdata+i+8,8);
        int in_image=(first>=image && first<image+s->image_size) ||
                     (first>=s->image_base && first<s->image_base+s->image_size);
        if (in_image) { vtable_rva=(uint32_t)(s->rdata.VirtualAddress+i+8); ++vtables; }
    }
    if (vtables!=1) goto done;
    for (size_t i=0;i+7<=text_size && lea_count<8;++i) {
        if (text[i]!=0x48 || text[i+1]!=0x8D || (text[i+2]&0xC7)!=0x05) continue;
        int32_t rel; memcpy(&rel,text+i+3,4);
        if (s->text.VirtualAddress+i+7+(int64_t)rel==vtable_rva) leas[lea_count++]=(uint32_t)(s->text.VirtualAddress+i);
    }
    uint32_t data_lo=s->data.VirtualAddress, data_hi=data_lo+(uint32_t)data_size;
    for (size_t i=0;i+5<=text_size;++i) {
        if (text[i]!=0xE8) continue;
        int32_t rel; memcpy(&rel,text+i+1,4);
        int64_t callee=(int64_t)s->text.VirtualAddress+(int64_t)i+5+rel; int near_ctor=0;
        for (int k=0;k<lea_count;++k) if (callee<=leas[k] && callee>=(int64_t)leas[k]-0x400) near_ctor=1;
        if (!near_ctor) continue;
        for (size_t j=i+5;j+7<=text_size && j<i+29;++j) {
            if (text[j]!=0x48 || text[j+1]!=0x89 || text[j+2]!=0x05) continue;
            memcpy(&rel,text+j+3,4);
            int64_t target=(int64_t)s->text.VirtualAddress+(int64_t)j+7+rel;
            if (target>=data_lo && target<data_hi) {
                if (slots && slot_rva!=(uint32_t)target) conflict=1;
                slot_rva=(uint32_t)target; store_rva=(uint32_t)(s->text.VirtualAddress+j); ++slots;
            }
            break;
        }
    }
    if (slots && !conflict) { solo_vtable_address=image+vtable_rva; solo_slot_address=image+slot_rva; }
done:
    log_line("RTTI lookup %s: type 0x%X, COL 0x%X, vtable 0x%X (%d), constructor loads %d, slot 0x%X store 0x%X (%d%s); 1.17-1.17.1 vtable 0x%X slot 0x%X.",
        solo_slot_address?"PASS":"FAIL",type_rva,col_rva,vtable_rva,vtables,lea_count,slot_rva,store_rva,slots,conflict?", conflicting":"",
        SOLO_IMP_VTABLE_RVA,SOLO_INSTANCE_SLOT_RVA);
    free(data); free(rdata); free(text);
}

#if defined(TV_HUD_ONLY)
#include "gfx_patch.h"

/* Primary vtable of an RTTI class: type descriptor name in .data -> complete
   object locator in .rdata -> the qword after the COL pointer. */
static uintptr_t rtti_vtable(const ImageSections *s,const unsigned char *data,const unsigned char *rdata,const char *name) {
    size_t n=strlen(name)+1, data_size=s->data.Misc.VirtualSize, rdata_size=s->rdata.Misc.VirtualSize;
    uint32_t type_rva=0, col_rva=0;
    for (size_t i=0x10;i+n<=data_size;++i)
        if (data[i]=='.' && !memcmp(data+i,name,n)) { type_rva=(uint32_t)(s->data.VirtualAddress+i-0x10); break; }
    if (!type_rva) return 0;
    for (size_t i=0;i+24<=rdata_size;i+=4) {
        uint32_t col[6]; memcpy(col,rdata+i,sizeof(col));
        if (col[0]==1 && !col[1] && col[3]==type_rva && col[5]==s->rdata.VirtualAddress+i) { col_rva=col[5]; break; }
    }
    if (!col_rva) return 0;
    uintptr_t found=0; int count=0;
    for (size_t i=0;i+16<=rdata_size;i+=8) {
        uint64_t q; memcpy(&q,rdata+i,8);
        if (q==s->image_base+col_rva || q==s->image+col_rva) { found=s->image+s->rdata.VirtualAddress+i+8; ++count; }
    }
    return count==1?found:0;
}

typedef void *(__fastcall *OpenFileFn)(void *opener,const char *url,int flags,int mode);
static uintptr_t gfx_opener_vtable, gfx_memory_vtable, gfx_memory_slots[3];
static OpenFileFn gfx_next_open_file;
static volatile LONG gfx_opened, gfx_result;
static SRWLOCK gfx_lock=SRWLOCK_INIT;
static const unsigned char *gfx_cache_out; static size_t gfx_cache_in_size, gfx_cache_out_size;
static uint64_t gfx_cache_in_hash, gfx_cache_out_hash;

static uint64_t gfx_hash(const unsigned char *data,size_t size) {
    uint64_t h=0xCBF29CE484222325ull;
    for (size_t i=0;i<size;++i) { h^=data[i]; h*=0x100000001B3ull; }
    return h;
}

/* A code pointer read from .rdata; an image mapped for auditing is not
   relocated, so pointers based on the preferred base are moved to the view. */
static uintptr_t image_pointer(const ImageSections *s,uintptr_t value) {
    if (s->image!=s->image_base && value>=s->image_base && value<s->image_base+s->image_size) return s->image+(value-s->image_base);
    return value;
}

/* Scaleform::MemoryFile: IsValid (slot 2) reads +0x28, Tell (4) +0x24,
   GetLength (6) +0x20; the buffer pointer is at +0x18. */
static int gfx_find_targets(const ImageSections *s) {
    unsigned char *data=copy_section(s,&s->data), *rdata=copy_section(s,&s->rdata);
    int ok=0;
    gfx_opener_vtable=gfx_memory_vtable=0;
    if (data && rdata) {
        gfx_opener_vtable=rtti_vtable(s,data,rdata,".?AVCSScaleformFileOpener@CS@@");
        gfx_memory_vtable=rtti_vtable(s,data,rdata,".?AVMemoryFile@Scaleform@@");
    }
    free(data); free(rdata);
    uintptr_t text_lo=s->image+s->text.VirtualAddress, text_hi=text_lo+s->text.Misc.VirtualSize;
    static const unsigned char is_valid[]={0x0F,0xB6,0x41,0x28,0xC3}, tell[]={0x8B,0x41,0x24,0xC3}, length[]={0x8B,0x41,0x20,0xC3};
    const unsigned char *code[3]={is_valid,tell,length}; const size_t code_size[3]={5,4,4}; const int slot[3]={2,4,6};
    uintptr_t open_file=gfx_opener_vtable?image_pointer(s,pointer_at(gfx_opener_vtable+8)):0;
    if (gfx_memory_vtable && open_file>=text_lo && open_file<text_hi) {
        ok=1;
        for (int i=0;i<3 && ok;++i) {
            unsigned char bytes[5];
            gfx_memory_slots[i]=image_pointer(s,pointer_at(gfx_memory_vtable+(uintptr_t)slot[i]*8));
            if (gfx_memory_slots[i]<text_lo || gfx_memory_slots[i]>=text_hi ||
                !read_memory(gfx_memory_slots[i],bytes,code_size[i]) || memcmp(bytes,code[i],code_size[i])) ok=0;
        }
    }
    log_line("GFX hook lookup %s: CSScaleformFileOpener vtable 0x%llX, OpenFile 0x%llX, MemoryFile vtable 0x%llX",
        ok?"PASS":"FAIL",(unsigned long long)(gfx_opener_vtable?gfx_opener_vtable-s->image:0),
        (unsigned long long)(open_file>=s->image?open_file-s->image:0),
        (unsigned long long)(gfx_memory_vtable?gfx_memory_vtable-s->image:0));
    return ok;
}

static int gfx_is_memory_file(void *file) {
    uintptr_t vtable=pointer_at((uintptr_t)file);
    if (!vtable) return 0;
    if (vtable==gfx_memory_vtable) return 1;
    /* Wrappers that reuse MemoryFile's accessors (e.g. ERNativeUI's) share its layout. */
    return pointer_at(vtable+16)==gfx_memory_slots[0] && pointer_at(vtable+32)==gfx_memory_slots[1] &&
           pointer_at(vtable+48)==gfx_memory_slots[2];
}

static const char *gfx_result_text(int result) {
    return result==GFX_PATCHED?"bar layout applied in memory":result==GFX_ALREADY?"bar layout already present (left as is)":
           result==GFX_NOT_FOUND?"bar placements not found as vanilla (another mod changed them); left unchanged":"unsupported movie format; left unchanged";
}

static void *__fastcall gfx_open_file(void *opener,const char *url,int flags,int mode) {
    void *file=gfx_next_open_file(opener,url,flags,mode);
    if (!file || !url) return file;
    __try {
        if (!gfx_url_is_fe(url)) return file;
        InterlockedIncrement(&gfx_opened);
        if (!gfx_is_memory_file(file)) { log_line("GFX: %s is not a memory file; layout not applied.",url); InterlockedExchange(&gfx_result,GFX_BAD); return file; }
        unsigned char valid=0; const unsigned char *buffer=(const unsigned char*)pointer_at((uintptr_t)file+0x18); int32_t size=0;
        if (!read_memory((uintptr_t)file+0x28,&valid,1) || !valid || !buffer || !read_memory((uintptr_t)file+0x20,&size,4) || size<=0) return file;
        AcquireSRWLockExclusive(&gfx_lock);
        int result; const unsigned char *output=NULL; size_t output_size=0;
        uint64_t hash=gfx_hash(buffer,(size_t)size);
        if (gfx_cache_out && (size_t)size==gfx_cache_in_size && hash==gfx_cache_in_hash) {
            result=GFX_PATCHED; output=gfx_cache_out; output_size=gfx_cache_out_size;
            log_line("GFX: %s opened again; same bar layout served.",url);
        } else if (gfx_cache_out && (size_t)size==gfx_cache_out_size && hash==gfx_cache_out_hash) {
            result=GFX_ALREADY;
        } else {
            int seen[GFX_RECORD_COUNT]; unsigned char *patched=NULL;
            result=gfx_patch_fe(buffer,(size_t)size,&patched,&output_size,seen);
            /* Served buffers stay alive for the whole process, like the game's own. */
            if (result==GFX_PATCHED) {
                output=patched; gfx_cache_out=patched; gfx_cache_out_size=output_size;
                gfx_cache_in_size=(size_t)size; gfx_cache_in_hash=hash; gfx_cache_out_hash=gfx_hash(patched,output_size);
            }
            log_line("GFX: %s (%d bytes): %s [SP %d, MP %d, HP %d, HPRegain %d, frame %d; 1 vanilla, 2 ours, 0 missing].",
                url,size,gfx_result_text(result),seen[0],seen[1],seen[2],seen[3],seen[4]);
        }
        ReleaseSRWLockExclusive(&gfx_lock);
        InterlockedExchange(&gfx_result,result);
        if (output) {
            int32_t new_size=(int32_t)output_size, index=0;
            write_memory((uintptr_t)file+0x18,&output,sizeof(output));
            write_memory((uintptr_t)file+0x20,&new_size,4);
            write_memory((uintptr_t)file+0x24,&index,4);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return file;
}

/* Executable page within rel32 range of target (for the relay and trampoline). */
static unsigned char *gfx_alloc_near(uintptr_t target) {
    SYSTEM_INFO info; GetSystemInfo(&info);
    const uintptr_t step=info.dwAllocationGranularity, range=0x70000000u;
    uintptr_t start=target&~(step-1);
    for (uintptr_t distance=step;distance<range;distance+=step) {
        uintptr_t candidates[2]={start-distance,start+distance};
        for (int i=0;i<2;++i) {
            if (candidates[i]<0x10000) continue;
            void *page=VirtualAlloc((void*)candidates[i],4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
            if (page) return page;
        }
    }
    return NULL;
}

/* 5-byte jmp rel32 at OpenFile's entry (16-byte aligned; its first bytes are
   push rbp/rbx/rsi/rdi in every supported build), written with one 8-byte
   compare-exchange. The vtable is left alone: ERNativeUI and Multiple Spirit
   Ashes require vtable[1] to point into the game. If the entry already starts
   with a jmp (MinHook, MSA or another hook), that jump's destination becomes
   the next link; otherwise a trampoline runs the 5 original bytes and jumps
   back to entry+5. Nothing is ever restored (the DLL is pinned). */
static int gfx_hook_entry(uintptr_t entry,uintptr_t image) {
    static const unsigned char vanilla[5]={0x40,0x55,0x53,0x56,0x57};
    if (entry&7) { log_line("GFX: OpenFile entry 0x%llX is not 8-byte aligned; not hooked.",(unsigned long long)(entry-image)); return 0; }
    unsigned char *page=gfx_alloc_near(entry);
    if (!page) { log_line("GFX: no executable page near OpenFile; not hooked."); return 0; }
    /* page+0x00: jmp [rip+0] -> gfx_open_file; page+0x20: trampoline. */
    static const unsigned char jump_abs[6]={0xFF,0x25,0,0,0,0};
    uintptr_t detour=(uintptr_t)gfx_open_file, back=entry+5;
    memcpy(page,jump_abs,6); memcpy(page+6,&detour,8);
    memcpy(page+0x20,vanilla,5); memcpy(page+0x25,jump_abs,6); memcpy(page+0x2B,&back,8);
    FlushInstructionCache(GetCurrentProcess(),page,0x40);
    int64_t rel=(int64_t)(uintptr_t)page-(int64_t)(entry+5);
    if (rel<INT32_MIN || rel>INT32_MAX) { log_line("GFX: relay out of range; not hooked."); return 0; }
    DWORD old;
    if (!VirtualProtect((void*)entry,8,PAGE_EXECUTE_READWRITE,&old)) { log_line("GFX: cannot unprotect OpenFile (%lu); not hooked.",GetLastError()); return 0; }
    int ok=0, chained=0;
    for (int attempt=0;attempt<8 && !ok;++attempt) {
        LONG64 current=*(volatile LONG64*)entry;
        unsigned char bytes[8]; memcpy(bytes,&current,8);
        if (bytes[0]==0xE9) {
            int32_t next_rel; memcpy(&next_rel,bytes+1,4);
            gfx_next_open_file=(OpenFileFn)(entry+5+(intptr_t)next_rel); chained=1;
        } else if (!memcmp(bytes,vanilla,5)) {
            gfx_next_open_file=(OpenFileFn)(uintptr_t)(page+0x20); chained=0;
        } else break;
        unsigned char patched[8]; memcpy(patched,bytes,8);
        int32_t rel32=(int32_t)rel; patched[0]=0xE9; memcpy(patched+1,&rel32,4);
        LONG64 desired; memcpy(&desired,patched,8);
        ok=InterlockedCompareExchange64((volatile LONG64*)entry,desired,current)==current;
    }
    FlushInstructionCache(GetCurrentProcess(),(void*)entry,8);
    VirtualProtect((void*)entry,8,old,&old);
    if (ok) log_line("GFX: OpenFile hooked at RVA 0x%llX%s; 01_000_fe.gfx gets the bar layout in memory.",
        (unsigned long long)(entry-image),chained?" (chained after an existing hook)":"");
    else log_line("GFX: OpenFile entry has unknown bytes (another kind of hook?); not hooked, the bars keep the game's layout.");
    return ok;
}

static int gfx_install(uintptr_t image) {
    ImageSections s;
    if (!image_sections(image,&s) || !gfx_find_targets(&s)) { log_line("GFX: layout hook not installed; the bars keep the game's layout."); return 0; }
    return gfx_hook_entry(image_pointer(&s,pointer_at(gfx_opener_vtable+8)),image);
}
#endif

static uintptr_t menu_common_row_rtti(uintptr_t image) {
    (void)image;
    uintptr_t vtable=solo_vtable_address;
    solo_rtti_vtable=vtable;
    if (!vtable || !solo_slot_address) return 0;
    solo_rtti_valid=1;
    menu_lookup_route=3;
    solo_slot_value=pointer_at(solo_slot_address);
    if (!solo_slot_value) { menu_lookup_stage=MENU_LOOKUP_HOLDER_INVALID; return 0; }
    solo_slot_vtable=pointer_at(solo_slot_value);
    if (solo_slot_vtable!=vtable) { menu_lookup_stage=MENU_LOOKUP_HOLDER_INVALID; return 0; }
    solo_rtti_first_candidate=solo_slot_value;
    solo_repository_diagnostic=solo_slot_value;
    menu_lookup_stage=MENU_LOOKUP_REPOSITORY_FOUND;
    uintptr_t row=menu_common_row_solo(solo_slot_value);
    if (!row && menu_lookup_stage<MENU_LOOKUP_PARAM_FILE_FOUND) menu_lookup_stage=MENU_LOOKUP_HOLDER_INVALID;
    return row;
}

static int singleton_is_type_descriptor(uintptr_t instance,const char *name) {
    char actual[80];
    uintptr_t name_pointer=pointer_at(instance+0x38);
    return name_pointer && read_c_string(name_pointer,actual,sizeof(actual)) && !strcmp(actual,name);
}

static uintptr_t menu_common_row(uintptr_t image) {
    menu_lookup_stage=MENU_LOOKUP_SINGLETON_MISSING; menu_lookup_bucket=-1;
    menu_lookup_holder_offset=0; menu_lookup_bucket_count=0; menu_lookup_route=0;
    fd4_repository_diagnostic=fd4_buckets_diagnostic=0;
    fd4_bucket_count_diagnostic=fd4_bucket_scan_limit=fd4_entries_visited=fd4_files_seen=0;
    fd4_first_head=fd4_first_entry=fd4_candidate_entry=fd4_candidate_file=0; fd4_candidate_size=0;
    solo_repository_diagnostic=0;
    solo_holder143_count=0; solo_holder143_first=0;
    solo_param_cap_diagnostic=solo_param_file_diagnostic=0; solo_param_file_size=0;
    solo_holder_base=0x80; solo_holder_stride=0x48;
    solo_rtti_valid=0; solo_rtti_vtable=solo_rtti_first_candidate=solo_slot_value=solo_slot_vtable=0;
    menu_lookup_last_param_name[0]=0; menu_lookup_last_row_count=0; menu_lookup_last_format=0;
    uintptr_t rtti_row=menu_common_row_rtti(image);
    if (rtti_row) return rtti_row;
    if (solo_rtti_valid) return 0;
    uintptr_t fd4=singleton_named(image,"FD4ParamRepository");
    if (fd4 && !singleton_is_type_descriptor(fd4,"FD4ParamRepository")) {
        fd4_repository_diagnostic=fd4;
        menu_lookup_stage=MENU_LOOKUP_REPOSITORY_FOUND;
        menu_lookup_route=1;
        uintptr_t general_row=menu_common_row_general(fd4);
        if (general_row) return general_row;
    }
    /* Try the documented indexed route if the general repository did not
       yield a validated row. Clear any candidate-name diagnostics first. */
    menu_lookup_last_param_name[0]=0; menu_lookup_last_row_count=0; menu_lookup_last_format=0;
    if (fd4) menu_lookup_stage=MENU_LOOKUP_REPOSITORY_FOUND;
    uintptr_t repository=singleton_named(image,"SoloParamRepository");
    if (!repository || singleton_is_type_descriptor(repository,"SoloParamRepository")) {
        menu_lookup_stage=MENU_LOOKUP_HOLDER_INVALID;
        return 0;
    }
    menu_lookup_stage=MENU_LOOKUP_REPOSITORY_FOUND; menu_lookup_route=2;
    solo_repository_diagnostic=repository;
    uintptr_t solo_row=menu_common_row_solo(repository);
    menu_lookup_holder_offset=(int)solo_holder_base; menu_lookup_bucket_count=solo_holder_stride;
    if (solo_row) { menu_lookup_holder_offset=(int)solo_holder_base; menu_lookup_bucket_count=solo_holder_stride; return solo_row; }
    if (menu_lookup_stage<MENU_LOOKUP_PARAM_FILE_FOUND) menu_lookup_stage=MENU_LOOKUP_HOLDER_INVALID;
    return 0;
}

static void update_bar_sizes(uintptr_t row,const int maximum[3],uintptr_t player_data,BarState *state) {
    if (!row) return;
    if (!state->initialized || state->row!=row) {
        int original[3]; if (!read_memory(row+8,original,sizeof(original))) return;
        for (int i=0;i<3;++i) if (original[i]<1 || original[i]>1000000) return;
        state->row=row; state->player_data=0; state->initialized=1;
        memcpy(state->vanilla,original,sizeof(original));
        memset(state->written,0,sizeof(state->written));
        log_line("HUD limits acquired: HP=%d FP=%d Stamina=%d",original[0],original[1],original[2]);
    }
    if (state->player_data!=player_data) {
        state->player_data=player_data;
        memcpy(state->baseline_maximum,maximum,sizeof(state->baseline_maximum));
        log_line("HUD baseline maxima: HP=%d FP=%d Stamina=%d",maximum[0],maximum[1],maximum[2]);
    }
    for (int i=0;i<3;++i) {
        int current; uintptr_t address=row+8+(uintptr_t)i*4;
        if (!bar_config.resource_enabled[i]) {
            /* Switched off live: give the game its own limit back, but only if
               the value is still the one this mod wrote. */
            if (state->written[i] && integer_at(address,&current) && current==state->written[i])
                write_memory(address,&state->vanilla[i],sizeof(state->vanilla[i]));
            state->written[i]=0;
            continue;
        }
        int vanilla=bar_config.custom_limit[i]>0?bar_config.custom_limit[i]:state->vanilla[i];
        int target=bar_config.fit_screen
            ? fit_screen_bar_limit(maximum[i],bar_config.width[i])
            : visual_bar_limit(maximum[i],vanilla,state->baseline_maximum[i],bar_config.width[i],bar_config.fixed_width[i]);
        if (!integer_at(address,&current)) continue;
        if (current==target || write_memory(address,&target,sizeof(target))) state->written[i]=target;
    }
}

#if defined(TV_LIVE_RELOAD)
/* Whole mod or BarSize switched off live: restore every limit this mod wrote. */
static void restore_bar_sizes(BarState *state) {
    if (!state->initialized || !state->row) return;
    int restored=0;
    for (int i=0;i<3;++i) {
        int current; uintptr_t address=state->row+8+(uintptr_t)i*4;
        if (state->written[i] && integer_at(address,&current) && current==state->written[i] &&
            write_memory(address,&state->vanilla[i],sizeof(state->vanilla[i]))) ++restored;
        state->written[i]=0;
    }
    if (restored) log_line("Bar limits restored to the game's values (%d).",restored);
}

/* NativeTweaks live reload (erms_live_reload_v1.h). The worker thread owns the
   configuration; requests only bump a counter and wait for its verdict. */
#define RELOAD_PENDING 0u
#define RELOAD_APPLIED 1u
#define RELOAD_REJECTED 2u
#define RELOAD_FAILED 4u
static volatile LONG reload_requested=0, reload_completed=0, live_reload_ready=0;
static volatile LONG reload_ids[16], reload_status[16];

static uint32_t request_reload(const wchar_t *path) {
    if (!live_reload_ready) return 0;
    if (path) {
        const wchar_t *name=wcsrchr(path,L'\\'), *alt=wcsrchr(path,L'/');
        if (alt && (!name || alt>name)) name=alt;
        name=name?name+1:path;
        if (_wcsicmp(name,MOD_INI_NAME)) return 0;
    }
    LONG id=InterlockedIncrement(&reload_requested);
    if (id<=0) return 0;
    return (uint32_t)id;
}
static uint32_t reload_status_of(uint32_t id) {
    if (!id || (LONG)id>reload_requested) return RELOAD_FAILED;
    if ((LONG)id>reload_completed) return RELOAD_PENDING;
    LONG slot=(LONG)(id%16);
    return reload_ids[slot]==(LONG)id ? (uint32_t)reload_status[slot] : RELOAD_APPLIED;
}
static void complete_reloads(LONG upto,uint32_t status) {
    for (LONG id=reload_completed+1;id<=upto;++id) {
        LONG slot=id%16;
        InterlockedExchange(&reload_status[slot],(LONG)status);
        InterlockedExchange(&reload_ids[slot],id);
    }
    InterlockedExchange(&reload_completed,upto);
}

__declspec(dllexport) uint32_t __cdecl NativeTweaks_RequestReloadV1(const wchar_t *ini) { return request_reload(ini); }
__declspec(dllexport) uint32_t __cdecl NativeTweaks_GetReloadStatusV1(uint32_t id) { return reload_status_of(id); }
/* NativeTweaks' dedicated Tarnished HUD page: 0 pending, 1 applied, 2 failed. */
__declspec(dllexport) uint32_t __cdecl TarnishedHUD_RequestLiveReloadV1(void) { return request_reload(NULL); }
__declspec(dllexport) uint32_t __cdecl TarnishedHUD_GetLiveReloadStatusV1(uint32_t id) {
    uint32_t status=reload_status_of(id);
    return status==RELOAD_PENDING?0u:status==RELOAD_APPLIED?1u:2u;
}
#endif

static void enable_byte(uintptr_t address) {
    unsigned char v=0;
    if (read_memory(address,&v,1) && v==0) { v=1; write_memory(address,&v,1); }
}

static DWORD WINAPI worker(void *unused) {
    (void)unused;
    /* Pin before doing work: unloading a DLL with a live worker is unsupported. */
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(uintptr_t)&worker,&pinned)) return 0;
    if (!paths()) return 0;
    log_line(MOD_BANNER);
    if (!check_version()) return 0;
#if defined(TV_HUD_ONLY)
    /* The game opens 01_000_fe.gfx once while starting up: hook before anything waits. */
    if (GetModuleHandleW(L"EasyAntiCheat_EOS.dll") || GetModuleHandleW(L"EasyAntiCheat_x64.dll")) {
        log_line("Anti-cheat module detected; nothing changed."); return 0;
    }
    gfx_install((uintptr_t)GetModuleHandleW(NULL));
#endif
#if defined(TV_LIVE_RELOAD)
    /* Invalid or disabled settings leave the bars vanilla but keep the worker,
       so a corrected or re-enabled INI applies without restarting. */
    int loaded=load_config(), mod_enabled=loaded==1;
    if (!loaded) {
        memset(&bar_config,0,sizeof(bar_config));
        log_line("Settings not applied; the bars stay vanilla until the INI is valid (it is re-read live).");
    }
#else
    if (load_config()!=1) return 0;
    const int mod_enabled=1;
#endif
    if (bar_config.enabled && bar_config.fit_screen)
        log_line("Fit-screen bar sizing enabled: limits follow resource maxima; widths are fractions of the HUD artwork.");
    if (GetModuleHandleW(L"EasyAntiCheat_EOS.dll") || GetModuleHandleW(L"EasyAntiCheat_x64.dll")) {
        log_line("Anti-cheat module detected; nothing changed."); return 0;
    }
    uintptr_t image=(uintptr_t)GetModuleHandleW(NULL);
    /* Native loaders may inject DLLs before the world is ready. Use a bounded retry. */
    int ready=0;
    for (int tries=0;tries<6 && !ready;++tries) { Sleep(1000); ready=check_layout(image); }
    if (!ready) { log_line("Layout unavailable. Nothing changed."); return 0; }
    log_line("Ready; waiting for a living local player in an offline modded session.");
    uintptr_t previous_data=0, previous_player=0;
    MaximumState maxima[3]={{0}};
    BarState bars={0}; uintptr_t menu_row=0; int menu_attempts=0, menu_retry_ticks=0;
#if defined(TV_LIVE_RELOAD)
    FILETIME config_stamp={0}; int have_config_stamp=0; unsigned config_poll=0;
    {
        WIN32_FILE_ATTRIBUTE_DATA attrs;
        if (GetFileAttributesExW(ini_path,GetFileExInfoStandard,&attrs)) { config_stamp=attrs.ftLastWriteTime; have_config_stamp=1; }
    }
    InterlockedExchange(&live_reload_ready,1);
    log_line("Live settings reload is available (NativeTweaks Save, or the INI is re-read about once per second).");
#endif
    double fractions[3]={0};
    LARGE_INTEGER frequency,last,now;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&last);
    for (;;) {
        Sleep(16); QueryPerformanceCounter(&now);
#if defined(TV_LIVE_RELOAD)
        LONG requested=reload_requested;
        int forced=requested!=reload_completed;
        if (forced || ++config_poll>=60) {
            config_poll=0;
            WIN32_FILE_ATTRIBUTE_DATA attrs;
            int have=GetFileAttributesExW(ini_path,GetFileExInfoStandard,&attrs);
            if (forced || (have && (!have_config_stamp || CompareFileTime(&attrs.ftLastWriteTime,&config_stamp)!=0))) {
                BarConfig previous=bar_config;
                int result=load_config();
                if (result) {
                    mod_enabled=result==1;
                    log_line("Live settings reload applied successfully (%s; mod %s).",
                        forced?"NativeTweaks request":"INI changed",mod_enabled?"on":"off");
                } else {
                    bar_config=previous;
                    log_line("Live settings reload rejected; the active settings were kept.");
                }
                if (have) { config_stamp=attrs.ftLastWriteTime; have_config_stamp=1; }
                if (forced) complete_reloads(requested,result?RELOAD_APPLIED:RELOAD_REJECTED);
                if (!mod_enabled || !bar_config.enabled) restore_bar_sizes(&bars);
            }
        }
#endif
        double dt=(double)(now.QuadPart-last.QuadPart)/frequency.QuadPart; last=now;
        uintptr_t player=0, data=player_data(image,&player);
        if (!data) { memset(fractions,0,sizeof(fractions)); continue; }
        if (data!=previous_data || player!=previous_player) {
            memset(maxima,0,sizeof(maxima)); memset(fractions,0,sizeof(fractions));
            previous_data=data; previous_player=player;
            log_line("Local player acquired.");
#if defined(TV_HUD_ONLY)
            static int gfx_checked;
            if (!gfx_checked && gfx_next_open_file) {
                gfx_checked=1;
                if (!gfx_opened) log_line("GFX: 01_000_fe.gfx was not opened after the hook was installed (the game loaded it earlier); the bars keep the game's layout this session.");
            }
#endif
        }
        int hp;
        if (!integer_at(data+0x138,&hp) || hp<=0) { memset(fractions,0,sizeof(fractions)); continue; }
        int current[3], maximum[3], base[3], valid=1;
        for (int i=0;i<3;++i) {
            if (!integer_at(data+current_offsets[i],&current[i]) || !integer_at(data+maximum_offsets[i],&maximum[i]) || !integer_at(data+base_offsets[i],&base[i]) || current[i]<0 || maximum[i]<1 || maximum[i]>1000000 || base[i]<1 || base[i]>1000000 || current[i]>1000000) valid=0;
        }
        if (!valid) { memset(fractions,0,sizeof(fractions)); continue; }
        uintptr_t verified_player=0;
        if (player_data(image,&verified_player)!=data || verified_player!=player) continue;
        if (mod_enabled && bar_config.enabled) {
            if (menu_retry_ticks>0) --menu_retry_ticks;
            if (!menu_row && menu_attempts<6 && menu_retry_ticks==0) {
                menu_row=menu_common_row(image); ++menu_attempts; menu_retry_ticks=60;
                if (menu_row) log_line("Visual bar sizing enabled (repository route %d, index %d).",menu_lookup_route,menu_lookup_bucket);
                else if (menu_attempts==6) {
                    char candidate_head[384]="";
                    if (solo_rtti_first_candidate) format_qword_dump(solo_rtti_first_candidate,0,12,candidate_head,sizeof(candidate_head));
                    log_line("Could not acquire MenuCommonParam (lookup stage %d, route=%d, rttiValid=%d, vtable=%p, instanceSlot=%p, instanceVtable=%p, candidateHead=%s, holder143_count=%u, wrapper=%p, paramCap=%p, file=%p, fileSize=%llu, name=%s, rows=%u, format=0x%02X); resource features remain active.",menu_lookup_stage,menu_lookup_route,solo_rtti_valid,(void*)solo_rtti_vtable,(void*)solo_slot_value,(void*)solo_slot_vtable,candidate_head,solo_holder143_count,(void*)solo_holder143_first,(void*)solo_param_cap_diagnostic,(void*)solo_param_file_diagnostic,(unsigned long long)solo_param_file_size,menu_lookup_last_param_name,menu_lookup_last_row_count,menu_lookup_last_format);
                }
            }
            update_bar_sizes(menu_row,maximum,data,&bars);
        }
        for (int i=0;i<3;++i) {
            ResourceConfig *c=&config[i]; if (!c->enabled) continue;
            if (c->modify_maximum) {
                int adjusted=track_maximum(base[i],c,&maxima[i]);
                if (adjusted!=base[i]) write_memory(data+base_offsets[i],&adjusted,sizeof(adjusted));
            }
            int next=current[i];
            if (c->infinite) {
                if (i==0) {
                    /* Per-player no-damage/no-death bits, never global enemy flags. */
                    unsigned char flags=0;
                    if (read_memory(data+0x19B,&flags,1)) { flags|=3; write_memory(data+0x19B,&flags,1); }
                } else if (i==1) {
                    enable_byte(image+DEBUG_RVA+5);
                    enable_byte(image+DEBUG_RVA+0x11); /* Ashes of War FP */
                } else enable_byte(image+DEBUG_RVA+4);
                next=maximum[i]; fractions[i]=0;
            } else next=regenerated_value(current[i],maximum[i],hp>0,c,dt,&fractions[i]);
            /* Re-read to avoid overwriting damage/healing that occurred during this tick. */
            if (next!=current[i]) {
                int latest;
                if (integer_at(data+current_offsets[i],&latest) && latest==current[i] && (i!=0 || latest>0))
                    write_memory(data+current_offsets[i],&next,sizeof(next));
            }
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved) {
    (void)reserved;
    if (reason==DLL_PROCESS_ATTACH) {
        own_module=module;
        DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}



