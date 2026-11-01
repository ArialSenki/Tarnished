/* BarModsAudit.exe <eldenring.exe>...: runs the mods' own version check,
   WorldChrMan pattern and SoloParamRepositoryImp RTTI/slot lookup against
   each executable mapped as an image (SEC_IMAGE, nothing is executed), plus
   Tarnished Vitals' debug flag lookup. */
#include "TarnishedVitals.c"
#include "debug_flags.h"

static uintptr_t audit_debug_flags(uintptr_t image) {
    ImageSections s; int matches[3]={0};
    if (!image_sections(image,&s)) return 0;
    unsigned char *text=copy_section(&s,&s.text);
    if (!text) return 0;
    uintptr_t data_start=image+s.data.VirtualAddress;
    uintptr_t flags=find_debug_flags(image+s.text.VirtualAddress,text,s.text.Misc.VirtualSize,
                                     data_start,data_start+s.data.Misc.VirtualSize,matches);
    free(text);
    printf("Debug flags %s: block 0x%llX (signature matches %d %d %d; 1.17-1.17.1 use 0x%X)\n",
        flags?"PASS":"FAIL",(unsigned long long)(flags?flags-image:0),matches[0],matches[1],matches[2],DEBUG_RVA);
    return flags;
}

static int audit_one(const wchar_t *path) {
    printf("== %ls\n", path);
    world_address = solo_vtable_address = solo_slot_address = 0;
    int version_ok = check_version_of(path);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) { printf("ERROR cannot open the file\n"); return 0; }
    HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
    CloseHandle(file);
    if (!mapping) { printf("ERROR cannot map the image (%lu)\n", GetLastError()); return 0; }
    void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) { CloseHandle(mapping); printf("ERROR cannot view the image\n"); return 0; }
    uintptr_t image = (uintptr_t)view;
    int layout_ok = check_layout(image);
    uintptr_t debug_flags = audit_debug_flags(image);
    ImageSections sections;
    int gfx_ok = image_sections(image, &sections) && gfx_find_targets(&sections);
    if (gfx_ok) {
        /* The inline hook needs the vanilla entry: push rbp/rbx/rsi/rdi, 8-byte aligned. */
        static const unsigned char entry_bytes[5] = {0x40, 0x55, 0x53, 0x56, 0x57};
        uintptr_t entry = image_pointer(&sections, pointer_at(gfx_opener_vtable + 8));
        unsigned char bytes[5] = {0};
        int entry_ok = !(entry & 15) && read_memory(entry, bytes, 5) && !memcmp(bytes, entry_bytes, 5);
        printf("GFX hook entry %s: OpenFile 0x%llX starts %02X %02X %02X %02X %02X, 16-byte aligned %s\n", entry_ok ? "PASS" : "FAIL",
            (unsigned long long)(entry - image), bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], (entry & 15) ? "no" : "yes");
        gfx_ok = entry_ok;
    }
    int ok = version_ok && layout_ok && solo_slot_address && debug_flags && gfx_ok;
    printf("Summary: version %s, WorldChrMan %s (RVA 0x%llX), SoloParamRepository %s (vtable 0x%llX, slot 0x%llX) -> %s\n",
        version_ok ? "supported" : "UNSUPPORTED", layout_ok ? "ok" : "FAIL",
        (unsigned long long)(world_address ? world_address - image : 0),
        solo_slot_address ? "ok" : "FAIL",
        (unsigned long long)(solo_vtable_address ? solo_vtable_address - image : 0),
        (unsigned long long)(solo_slot_address ? solo_slot_address - image : 0),
        ok ? "OK" : "NOT OK");
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return ok;
}

static unsigned char *read_whole(const wchar_t *path, size_t *size) {
    FILE *f = _wfopen(path, L"rb"); unsigned char *data = NULL; long n;
    if (!f) return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n)) &&
        fread(data, 1, (size_t)n, f) == (size_t)n) *size = (size_t)n;
    else { free(data); data = NULL; }
    fclose(f); return data;
}

/* --gfx <vanilla 01_000_fe.gfx> <expected patched file>: the in-memory patch
   of the vanilla movie must equal the build 4 file byte for byte, and
   patching the result again must leave it alone. */
static int audit_gfx(const wchar_t *vanilla_path, const wchar_t *expected_path) {
    size_t vanilla_size = 0, expected_size = 0, out_size = 0, again_size = 0;
    unsigned char *vanilla = read_whole(vanilla_path, &vanilla_size), *expected = read_whole(expected_path, &expected_size);
    unsigned char *out = NULL, *again = NULL; int seen[GFX_RECORD_COUNT], seen2[GFX_RECORD_COUNT];
    if (!vanilla || !expected) { printf("GFX patch: cannot read the files\n"); return 0; }
    int r1 = gfx_patch_fe(vanilla, vanilla_size, &out, &out_size, seen);
    int same = r1 == GFX_PATCHED && out_size == expected_size && !memcmp(out, expected, out_size);
    int r2 = out ? gfx_patch_fe(out, out_size, &again, &again_size, seen2) : 0;
    int r3 = gfx_patch_fe(expected, expected_size, &again, &again_size, seen2);
    int ok = same && r2 == GFX_ALREADY && r3 == GFX_ALREADY;
    printf("GFX patch %s: vanilla %zu bytes -> %s (%zu bytes, %s the build 4 file); patching again -> %s; build 4 file -> %s\n",
        ok ? "PASS" : "FAIL", vanilla_size, gfx_result_text(r1), out_size, same ? "identical to" : "DIFFERENT from",
        r2 ? gfx_result_text(r2) : "-", gfx_result_text(r3));
    free(vanilla); free(expected); free(out); free(again);
    return ok;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) { printf("usage: BarModsAudit [--gfx <vanilla.gfx> <expected.gfx>] <eldenring.exe>...\n"); return 2; }
    int failures = 0, first = 1;
    if (argc >= 4 && !wcscmp(argv[1], L"--gfx")) {
        if (!audit_gfx(argv[2], argv[3])) ++failures;
        first = 4;
    }
    for (int i = first; i < argc; ++i) if (!audit_one(argv[i])) ++failures;
    printf("%d of %d checks OK\n", argc - first - failures + (first > 1), argc - first + (first > 1));
    return failures ? 1 : 0;
}
