#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>
#include <errno.h>
#include "../vitals_core.h"
#include "../debug_flags.h"

#if !defined(TV_VITALS_ONLY) || defined(TV_HUD_ONLY)
#error This file builds Tarnished Vitals only (/DTV_VITALS_ONLY); the HUD mods use src\TarnishedVitals.c.
#endif

#define MOD_INI_NAME L"TarnishedVitals.ini"
#define MOD_LOG_NAME L"TarnishedVitals.log"
#define MOD_BANNER "Tarnished Vitals v6 (Elden Ring 1.17, 1.17.1, 1.17.1 JP, 1.17.2, 1.17.2 JP). Live reload: NativeTweaks Save; Infinite changes need a restart."

/* Targets: eldenring.exe FileVersion 2.7.0.0 (1.17), 2.7.1.0 (1.17.1),
   2.7.1.1 (1.17.1, Japanese executable), 2.7.2.0 (1.17.2) and 2.7.2.1 (1.17.2,
   Japanese executable). WorldChrMan is
   found by its load pattern and the debug flag block by debug_flags.h; the
   RVAs below (1.17-1.17.1; 2.7.2.0 adds 0x3020) are only logged for
   comparison. The player offset 0x1E508 is the same in all five. */
#define WORLD_RVA 0x3D69FF8u
#define DEBUG_RVA 0x3D6A210u
#define PLAYER_OFFSET 0x1E508u
static uintptr_t world_address, debug_flags_address;
static const char *game_build_name="unknown";
static HMODULE own_module;
static wchar_t ini_path[32768], log_path[32768];
typedef struct {
    int enabled, fit_screen, resource_enabled[3], fixed_width[3], custom_limit[3];
    double width[3];
} BarConfig;
typedef struct {
    int enabled;
    ResourceConfig resources[3];
    BarConfig bars;
} RuntimeConfig;
typedef struct { uintptr_t row, player_data; int initialized, vanilla[3], baseline_maximum[3]; } BarState;
static RuntimeConfig active_config;
static SRWLOCK config_lock = SRWLOCK_INIT;
static SRWLOCK reload_lock = SRWLOCK_INIT;
static volatile LONG config_generation;
static volatile LONG worker_ready;
typedef struct { uint32_t id, status; } ReloadRecord;
static ReloadRecord reload_records[16];
static uint32_t next_reload_id;
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
    FILE *f = _wfopen(log_path, L"a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u ", t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    va_list a; va_start(a, format); vfprintf(f, format, a); va_end(a);
    fputc('\n', f); fclose(f);
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
        log_line("Invalid setting [%ls] %ls (value=%ls; allowed %.3g to %.3g, decimal point). Candidate config rejected.",section,key,text,min,max); return 0;
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
static int load_config(RuntimeConfig *candidate) {
    if (!candidate) return 0;
    memset(candidate,0,sizeof(*candidate));
    int enabled=0;
    if (GetFileAttributesW(ini_path)==INVALID_FILE_ATTRIBUTES) {
        log_line("INI missing; nothing changed."); return 0;
    }
    if (!boolean(L"General",L"Enabled",0,&enabled)) return 0;
    candidate->enabled=enabled;
    #if !defined(TV_HUD_ONLY)
    for (int i=0;i<3;++i) {
        ResourceConfig *c=&candidate->resources[i]; const wchar_t *s=sections[i];
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
    if (!boolean(L"BarSize",L"Enabled",0,&candidate->bars.enabled) ||
        !boolean(L"BarSize",L"FitScreen",0,&candidate->bars.fit_screen) ||
        !boolean(L"BarSize",L"FixedWidth",1,&common_fixed)) return 0;
    for (int i=0;i<3;++i) {
        int fixed_override=-1;
        if (!boolean(L"BarSize",bar_enabled_keys[i],1,&candidate->bars.resource_enabled[i]) ||
            !integer_number(L"BarSize",bar_fixed_keys[i],-1,-1,1,&fixed_override) ||
            !number(L"BarSize",bar_width_keys[i],1,candidate->bars.fit_screen?0.01:0.1,candidate->bars.fit_screen?1:10,&candidate->bars.width[i]) ||
            !integer_number(L"BarSize",bar_limit_keys[i],0,0,1000000,&candidate->bars.custom_limit[i])) return 0;
        candidate->bars.fixed_width[i]=fixed_override<0?common_fixed:fixed_override;
    }
    #endif
    return 1;
}

static void publish_config(const RuntimeConfig *candidate) {
    AcquireSRWLockExclusive(&config_lock);
    active_config=*candidate;
    InterlockedIncrement(&config_generation);
    ReleaseSRWLockExclusive(&config_lock);
}

static LONG read_config_snapshot(RuntimeConfig *snapshot) {
    AcquireSRWLockShared(&config_lock);
    *snapshot=active_config;
    LONG generation=config_generation;
    ReleaseSRWLockShared(&config_lock);
    return generation;
}

static int same_ini_path(const wchar_t *requested) {
    wchar_t requested_full[32768], owned_full[32768];
    if (!requested || !*requested) return 0;
    DWORD requested_length=GetFullPathNameW(requested,32768,requested_full,NULL);
    DWORD owned_length=GetFullPathNameW(ini_path,32768,owned_full,NULL);
    if (!requested_length || requested_length>=32768 ||
        !owned_length || owned_length>=32768) return 0;
    return _wcsicmp(requested_full,owned_full)==0;
}

static int reload_requires_restart(const RuntimeConfig *old_config,
                                   const RuntimeConfig *new_config) {
#if !defined(TV_HUD_ONLY)
    for (int i=0;i<3;++i) {
        const ResourceConfig *old=&old_config->resources[i];
        const ResourceConfig *next=&new_config->resources[i];
        /* The infinite mode sets game/player protection flags whose original
           values cannot be safely inferred after another mod or the game
           changes them. Do not pretend that toggling it off is reversible. */
        if (old->infinite!=next->infinite) return 1;
        if (old_config->enabled!=new_config->enabled &&
            ((old_config->enabled && old->enabled && old->infinite) ||
             (new_config->enabled && next->enabled && next->infinite))) return 1;
        if (old->enabled!=next->enabled && (old->infinite || next->infinite)) return 1;
    }
#else
    (void)old_config;
    (void)new_config;
#endif
    return 0;
}

static void set_reload_status(uint32_t id,uint32_t status) {
    AcquireSRWLockExclusive(&reload_lock);
    ReloadRecord *record=&reload_records[id%16u];
    if (record->id==id) record->status=status;
    ReleaseSRWLockExclusive(&reload_lock);
}

__declspec(dllexport) uint32_t __cdecl
ERModSettingsHub_RequestReloadV1(const wchar_t *requested_ini) {
    if (!same_ini_path(requested_ini)) {
        log_line("Hub reload rejected: the requested INI is not owned by this DLL.");
        return 0;
    }
    if (InterlockedCompareExchange(&worker_ready,0,0)!=1) {
        log_line("Hub reload unavailable: the Vitals runtime worker is not ready.");
        return 0;
    }

    AcquireSRWLockExclusive(&reload_lock);
    uint32_t id=++next_reload_id;
    if (!id) id=++next_reload_id;
    reload_records[id%16u]=(ReloadRecord){id,0};
    ReleaseSRWLockExclusive(&reload_lock);

    uint32_t result=2;
    RuntimeConfig candidate,old_config;
    if (!load_config(&candidate)) {
        log_line("Hub reload rejected: the INI did not pass validation; current settings remain active.");
    } else {
        (void)read_config_snapshot(&old_config);
        if (reload_requires_restart(&old_config,&candidate)) {
            result=3;
            log_line("Hub reload requires restart: a non-reversible infinite-mode setting changed; current settings remain active.");
        } else {
            publish_config(&candidate);
            result=1;
            log_line("Hub reload applied: the complete validated configuration is active.");
        }
    }
    set_reload_status(id,result);
    return id;
}

__declspec(dllexport) uint32_t __cdecl
ERModSettingsHub_GetReloadStatusV1(uint32_t id) {
    if (!id) return 4;
    AcquireSRWLockShared(&reload_lock);
    const ReloadRecord record=reload_records[id%16u];
    ReleaseSRWLockShared(&reload_lock);
    return record.id==id?record.status:4;
}

/* Current NativeTweaks names (include/erms_live_reload_v1.h); it still falls
   back to the ERModSettingsHub_ names above for older builds. */
__declspec(dllexport) uint32_t __cdecl NativeTweaks_RequestReloadV1(const wchar_t *requested_ini) {
    return ERModSettingsHub_RequestReloadV1(requested_ini);
}
__declspec(dllexport) uint32_t __cdecl NativeTweaks_GetReloadStatusV1(uint32_t id) {
    return ERModSettingsHub_GetReloadStatusV1(id);
}

typedef struct { DWORD ms, ls; const char *name; } GameBuild;
static const GameBuild supported_builds[]={
    {0x00020007,0x00000000,"1.17 (2.7.0.0)"},
    {0x00020007,0x00010000,"1.17.1 (2.7.1.0)"},
    {0x00020007,0x00010001,"1.17.1 JP (2.7.1.1)"},
    {0x00020007,0x00020000,"1.17.2 (2.7.2.0)"},
    {0x00020007,0x00020001,"1.17.2 JP (2.7.2.1)"},
};

static int check_version(void) {
    wchar_t exe[32768];
    if (!GetModuleFileNameW(NULL,exe,32768)) return 0;
    wchar_t *name=wcsrchr(exe,L'\\');
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
    if (!ok && GetPrivateProfileIntW(L"General",L"AllowUnknownGameVersion",0,ini_path)==1) {
        /* Asked for on Nexus (wzysgdhr): keep working after a game update.
           Nothing uses a fixed address: every piece is found by signature
           and checked (Layout check), and a check that fails still changes
           nothing. */
        game_build_name="unknown version (AllowUnknownGameVersion=1)";
        log_line("Unknown executable version: running anyway (General.AllowUnknownGameVersion=1). If the layout checks below fail, nothing changes.");
        return 1;
    }
    if (ok) log_line("Supported game: Elden Ring %s.",game_build_name);
    else log_line("Unsupported executable version. Supported: 2.7.0.0, 2.7.1.0, 2.7.1.1, 2.7.2.0, 2.7.2.1. Nothing changed.");
    return ok;
}

/* WorldChrMan: exactly one "mov rax,[rip+X]; test rax,rax; jz +0F; cmp [rax+..]"
   load whose target lies in the game's .data. The debug flag block
   (debug_flags.h) is only needed by Infinite FP/stamina. Both come from the
   first .text/.data sections; later .text sections are the protection layer. */
static int check_layout(uintptr_t image) {
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt;
    IMAGE_SECTION_HEADER text_sec={0}, data_sec={0};
    if (!read_memory(image,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000) return 0;
    if (!read_memory(image+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64) return 0;
    uintptr_t table=image+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i=0;i<nt.FileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER sec;
        if (!read_memory(table+i*sizeof(sec),&sec,sizeof(sec))) return 0;
        if (sec.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage || sec.VirtualAddress>nt.OptionalHeader.SizeOfImage-sec.Misc.VirtualSize) return 0;
        if (!memcmp(sec.Name,".text\0",6) && !text_sec.Misc.VirtualSize) text_sec=sec;
        else if (!memcmp(sec.Name,".data\0",6) && !data_sec.Misc.VirtualSize) data_sec=sec;
    }
    if (!text_sec.Misc.VirtualSize || !data_sec.Misc.VirtualSize) { log_line("Layout check: FAIL (PE headers)"); return 0; }
    size_t size=text_sec.Misc.VirtualSize;
    unsigned char *copy=malloc(size); if (!copy) return 0;
    if (!read_memory(image+text_sec.VirtualAddress,copy,size)) { free(copy); return 0; }
    uintptr_t text_start=image+text_sec.VirtualAddress;
    uintptr_t data_start=image+data_sec.VirtualAddress, data_end=data_start+data_sec.Misc.VirtualSize;
    int found=0; uintptr_t target=0;
    const unsigned char prefix[]={0x48,0x8B,0x05};
    const unsigned char suffix[]={0x48,0x85,0xC0,0x74,0x0F,0x48,0x39,0x88};
    for (size_t j=0;j+15<=size;++j) {
        if (!memcmp(copy+j,prefix,3) && !memcmp(copy+j+7,suffix,8)) {
            int32_t rel; memcpy(&rel,copy+j+3,4);
            target=text_start+j+7+rel; ++found;
        }
    }
    int matches[3]={0};
    uintptr_t flags=find_debug_flags(text_start,copy,size,data_start,data_end,matches);
    free(copy);
    int world_ok=found==1 && target>=data_start && target<data_end;
    log_line("Layout check: %s (%d signature matches, WorldChrMan at RVA 0x%llX; 1.17-1.17.1 use 0x%X)",
        world_ok?"PASS":"FAIL",found,(unsigned long long)(found==1?target-image:0),WORLD_RVA);
    log_line("Debug flags %s: block 0x%llX (signature matches %d %d %d; 1.17-1.17.1 use 0x%X)",
        flags?"PASS":"FAIL",(unsigned long long)(flags?flags-image:0),matches[0],matches[1],matches[2],DEBUG_RVA);
    if (!world_ok) return 0;
    world_address=target; debug_flags_address=flags;
    if (!flags) log_line("Debug flags not found: Infinite FP/stamina only refills every tick (no zero-cost flag); everything else works.");
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

static void enable_byte(uintptr_t address) {
    unsigned char v=0;
    if (!debug_flags_address) return;
    if (read_memory(address,&v,1) && v==0) { v=1; write_memory(address,&v,1); }
}

static void restore_disabled_maxima(uintptr_t data,
                                    const RuntimeConfig *old_config,
                                    const RuntimeConfig *new_config,
                                    MaximumState maxima[3]) {
    for (int i=0;i<3;++i) {
        const ResourceConfig *old=&old_config->resources[i];
        const ResourceConfig *next=&new_config->resources[i];
        if (!old_config->enabled || !old->modify_maximum ||
            (new_config->enabled && next->modify_maximum) ||
            !maxima[i].initialized) continue;
        int observed=0;
        uintptr_t address=data+base_offsets[i];
        if (integer_at(address,&observed) && observed==maxima[i].last_written &&
            maxima[i].baseline>=1 && maxima[i].baseline<=1000000) {
            write_memory(address,&maxima[i].baseline,sizeof(maxima[i].baseline));
        }
        memset(&maxima[i],0,sizeof(maxima[i]));
    }
}

static DWORD WINAPI worker(void *unused) {
    (void)unused;
    /* Pin before doing work: unloading a DLL with a live worker is unsupported. */
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(uintptr_t)&worker,&pinned)) return 0;
    if (!paths()) return 0;
    log_line(MOD_BANNER);
    RuntimeConfig initial_config;
    if (!check_version() || !load_config(&initial_config)) return 0;
    publish_config(&initial_config);
    if (GetModuleHandleW(L"EasyAntiCheat_EOS.dll") || GetModuleHandleW(L"EasyAntiCheat_x64.dll")) {
        log_line("Anti-cheat module detected; nothing changed."); return 0;
    }
    uintptr_t image=(uintptr_t)GetModuleHandleW(NULL);
    /* Native loaders may inject DLLs before the world is ready. Use a bounded retry. */
    int ready=0;
    for (int tries=0;tries<6 && !ready;++tries) { Sleep(1000); ready=check_layout(image); }
    if (!ready) { log_line("Layout unavailable. Nothing changed."); return 0; }
    InterlockedExchange(&worker_ready,1);
    log_line("Ready; waiting for a living local player in an offline modded session.");
    uintptr_t previous_data=0, previous_player=0;
    MaximumState maxima[3]={{0}};
    double fractions[3]={0};
    RuntimeConfig previous_config=initial_config;
    LONG applied_generation=0;
    RuntimeConfig frame_config;
    applied_generation=read_config_snapshot(&frame_config);
    LARGE_INTEGER frequency,last,now;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&last);
    for (;;) {
        Sleep(16); QueryPerformanceCounter(&now);
        double dt=(double)(now.QuadPart-last.QuadPart)/frequency.QuadPart; last=now;
        LONG frame_generation=read_config_snapshot(&frame_config);
        uintptr_t player=0, data=player_data(image,&player);
        if (!data) { memset(fractions,0,sizeof(fractions)); continue; }
        int player_changed=data!=previous_data || player!=previous_player;
        if (player_changed) {
            memset(maxima,0,sizeof(maxima)); memset(fractions,0,sizeof(fractions));
            previous_data=data; previous_player=player;
            log_line("Local player acquired.");
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
        if (frame_generation!=applied_generation) {
            if (!player_changed)
                restore_disabled_maxima(data,&previous_config,&frame_config,maxima);
            memset(fractions,0,sizeof(fractions));
            previous_config=frame_config;
            applied_generation=frame_generation;
            log_line("A validated live configuration snapshot became active.");
        }
        if (!frame_config.enabled) continue;
        for (int i=0;i<3;++i) {
            const ResourceConfig *c=&frame_config.resources[i]; if (!c->enabled) continue;
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
                    enable_byte(debug_flags_address+5);
                    enable_byte(debug_flags_address+0x11); /* Ashes of War FP */
                } else enable_byte(debug_flags_address+4);
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
