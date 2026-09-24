// OreSweep - Whiskerwood native mod (dsound.dll proxy)
// Hold a key (default CTRL) while releasing a mining-tool drag: only cells that
// contain ore are marked; plain stone (and unsurveyed cells) are skipped.
// No CRT, no Windows SDK headers: everything is declared by hand.

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned short wchar;
typedef void *HANDLE;
typedef int BOOL;

#define WINAPI __stdcall
#define IMPORT __declspec(dllimport)

IMPORT HANDLE WINAPI GetModuleHandleW(const wchar *);
IMPORT u32 WINAPI GetModuleFileNameW(HANDLE, wchar *, u32);
IMPORT HANDLE WINAPI LoadLibraryW(const wchar *);
IMPORT void *WINAPI GetProcAddress(HANDLE, const char *);
IMPORT u32 WINAPI GetSystemDirectoryW(wchar *, u32);
IMPORT BOOL WINAPI VirtualProtect(void *, u64, u32, u32 *);
IMPORT BOOL WINAPI FlushInstructionCache(HANDLE, const void *, u64);
IMPORT HANDLE WINAPI GetCurrentProcess(void);
IMPORT HANDLE WINAPI CreateThread(void *, u64, u32(WINAPI *)(void *), void *, u32, u32 *);
IMPORT BOOL WINAPI CloseHandle(HANDLE);
IMPORT u32 WINAPI GetPrivateProfileIntW(const wchar *, const wchar *, i32, const wchar *);
IMPORT BOOL WINAPI DisableThreadLibraryCalls(HANDLE);
IMPORT short WINAPI GetAsyncKeyState(int);

// ---- tiny libc replacements (the compiler may emit calls to these) ----
void *memset(void *d, int c, u64 n) { u8 *p = (u8 *)d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, u64 n) { u8 *p = (u8 *)d; const u8 *q = (const u8 *)s; while (n--) *p++ = *q++; return d; }
int _fltused = 0;

static void wcopy(wchar *d, const wchar *s) { while ((*d++ = *s++)) {} }
static u32 wlen(const wchar *s) { u32 n = 0; while (s[n]) n++; return n; }

// ---- dsound forwarding ----
void *real_DirectSoundCreate, *real_DirectSoundEnumerateA, *real_DirectSoundEnumerateW,
    *real_DllCanUnloadNow, *real_DllGetClassObject, *real_DirectSoundCaptureCreate,
    *real_DirectSoundCaptureEnumerateA, *real_DirectSoundCaptureEnumerateW, *real_GetDeviceID,
    *real_DirectSoundFullDuplexCreate, *real_DirectSoundCreate8, *real_DirectSoundCaptureCreate8;

static void LoadRealDsound(void) {
    wchar path[520];
    u32 n = GetSystemDirectoryW(path, 400);
    const wchar *tail = L"\\dsound.dll";
    wcopy(path + n, tail);
    HANDLE h = LoadLibraryW(path);
    if (!h) return;
#define R(x) real_##x = GetProcAddress(h, #x)
    R(DirectSoundCreate); R(DirectSoundEnumerateA); R(DirectSoundEnumerateW);
    R(DllCanUnloadNow); R(DllGetClassObject); R(DirectSoundCaptureCreate);
    R(DirectSoundCaptureEnumerateA); R(DirectSoundCaptureEnumerateW); R(GetDeviceID);
    R(DirectSoundFullDuplexCreate); R(DirectSoundCreate8); R(DirectSoundCaptureCreate8);
#undef R
}

// ---- config + log ----
static wchar g_dir[520];     // folder of this dll, with trailing backslash
static int g_key = 17;          // VK_CONTROL (either Ctrl)
static int g_allowUnsurveyed = 0; // 1 = also keep cells the player hasn't surveyed
static int g_enabled = 1;

static void InitPathsAndConfig(HANDLE self) {
    u32 n = GetModuleFileNameW(self, g_dir, 512);
    while (n && g_dir[n - 1] != '\\') n--;
    g_dir[n] = 0;
    wchar p[600];
    wcopy(p, g_dir); wcopy(p + wlen(p), L"OreSweep.ini");
    g_enabled = GetPrivateProfileIntW(L"OreSweep", L"Enabled", 1, p);
    g_key = GetPrivateProfileIntW(L"OreSweep", L"HotkeyVK", 17, p);
    g_allowUnsurveyed = GetPrivateProfileIntW(L"OreSweep", L"KeepUnsurveyed", 0, p);
}

// ---- pattern scanning ----
static u8 *g_textBase; static u64 g_textSize;

static int FindText(void) {
    u8 *base = (u8 *)GetModuleHandleW(0);
    i32 peOff = *(i32 *)(base + 0x3c);
    u8 *nt = base + peOff;
    u16 nsec = *(u16 *)(nt + 6);
    u16 optSize = *(u16 *)(nt + 20);
    u8 *sec = nt + 24 + optSize;
    for (u16 i = 0; i < nsec; i++, sec += 40) {
        if (sec[0] == '.' && sec[1] == 't' && sec[2] == 'e' && sec[3] == 'x' && sec[4] == 't') {
            g_textBase = base + *(u32 *)(sec + 12);
            g_textSize = *(u32 *)(sec + 8);
            return 1;
        }
    }
    return 0;
}

// pattern: "48 8B ?? 05" style
static int ParsePattern(const char *s, i32 *out) {
    int n = 0;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (s[0] == '?') { out[n++] = -1; s += (s[1] == '?') ? 2 : 1; continue; }
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = *s++;
            v = v * 16 + (c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0');
        }
        out[n++] = v;
    }
    return n;
}

static u8 *Scan(const char *pattern, int *count) {
    i32 pat[400];
    int n = ParsePattern(pattern, pat);
    u8 *first = 0; int c = 0;
    for (u64 i = 0; i + n <= g_textSize; i++) {
        u8 *p = g_textBase + i;
        if (p[0] != (u8)pat[0]) continue;
        int j = 1;
        for (; j < n; j++) if (pat[j] >= 0 && p[j] != (u8)pat[j]) break;
        if (j == n) { if (!first) first = p; c++; }
    }
    if (count) *count = c;
    return first;
}

static u8 *Rel32(u8 *instrAfterOpcode) { return instrAfterOpcode + 4 + *(i32 *)instrAfterOpcode; }

// ---- game data access ----
void *g_retKeep, *g_iterNext, *g_loopHead;   // used by ore_tramp (stubs.S)
extern void ore_tramp(void);
static void **g_gamePtr;   // address of the global game-state pointer
static u32 g_gridOff;      // offset of terrain grid inside game state

// Terrain grid: IntVector origin @0, IntVector size @0xC, cells* @0x18, cell = 0x1C bytes.
// Cell byte0: 0x80 = surveyed, 0x20 = has resource slots; u16 @+4 = 8 two-bit slots,
// first slot == 0b11 is the ore index; none -> plain stone.
int OreFilter_ShouldSkip(const i32 *pos) {
    if (!g_enabled) return 0;
    if (!(GetAsyncKeyState(g_key) & 0x8000)) return 0;
    u8 *game = (u8 *)*g_gamePtr;
    if (!game) return 0;
    u8 *grid = game + g_gridOff;
    i32 *o = (i32 *)grid;
    i32 x = pos[0] - o[0], y = pos[1] - o[1], z = pos[2] - o[2];
    if (x < 0 || y < 0 || z < 0 || x >= o[3] || y >= o[4] || z >= o[5]) return 0;
    u8 *cells = *(u8 **)(grid + 0x18);
    if (!cells) return 0;
    u8 *cell = cells + (i64)((z * o[4] + y) * o[3] + x) * 0x1c;
    u8 flags = cell[0];
    if (!(flags & 0x80)) {
        return g_allowUnsurveyed ? 0 : 1;
    }
    if (!(flags & 0x20)) return 0;
    u16 slots = *(u16 *)(cell + 4);
    for (int i = 0; i < 8; i++) {
        u16 m = (u16)(3u << (2 * i));
        if ((slots & m) == m) return 0;
    }
    return 1;
}

static int InstallHook(void) {
    if (!FindText()) return 0;

    int c1, c2, c3, c4;
    // per-cell body of the "mark for mining" commit loop (the add-mark variant, sets byte +0x48 = 1)
    u8 *loop = Scan("48 63 45 DC 48 8B 55 C0 3B C6 75 0F 4C 39 75 D0 75 09 49 3B D7 0F 84 ?? ?? ?? ?? "
                    "33 C9 48 C7 44 24 74 00 00 00 00 0F 57 C0 48 89 8D D0 02 00 00 48 89 4D 9C 48 89 4D B8 "
                    "48 8D 0C 80 48 8B 02 0F 11 44 24 7C 0F 11 45 8C 8B 7C C8 08 F2 0F 10 34 C8 8B 44 24 48 "
                    "0F 11 45 A8 3B 44 24 4C 75 14 B9 50 08 00 00 4C 8D 44 24 4C 48 8D 54 24 40 E8 ?? ?? ?? ?? "
                    "0F 10 45 88 8D 48 01 48 98 F2 0F 10 4D 98 89 4C 24 48 33 C9 48 8D 1C 80 48 8B 44 24 74 "
                    "48 C1 E3 04 48 03 5C 24 40 48 89 43 04 8B 45 A0 0F 11 43 18 89 43 30 8B 45 CC "
                    "F3 0F 10 85 D0 02 00 00 F7 D0 21 45 D8 F2 0F 11 73 0C 66 C7 03 2B 00 89 7B 14 "
                    "F2 0F 11 4B 28 48 89 4B 38 F3 0F 11 43 34 48 89 4B 40 48 8D 4D C8 C6 43 48 01 "
                    "E8 ?? ?? ?? ?? E9 ?? ?? ?? ??", &c1);
    // game-state getter call right before "mov r15,[rax+0x3508]" in the same function
    u8 *getterCall = Scan("E8 ?? ?? ?? ?? 8B CB 89 5C 24 50 C7 44 24 54 01 00 00 00 44 8B C3 "
                          "C7 44 24 60 FF FF FF FF 4C 8B B8 08 35 00 00", &c2);
    // "add rax, <gridOff>; lea rdx,[r15+0x588]" in MineTool info code
    u8 *gridAdd = Scan("48 05 ?? ?? ?? ?? 49 8D 97 88 05 00 00", &c3);
    // terrain cell lookup (to confirm the grid layout we re-implement)
    u8 *lookup = Scan("48 83 EC 18 44 8B 11 44 39 12 7C ?? 44 8B 41 04 44 39 42 04 7C ?? "
                      "44 8B 49 08 44 39 4A 08 7C", &c4);
    if (c1 != 1 || c2 < 1 || c3 != 1 || c4 < 1) return 0;  // game code changed: stay inactive
    // layout check inside lookup: imul rax,rax,0x1c ; add rax,[rcx+0x18]
    int layoutOk = 0;
    for (int i = 0; i < 0x80; i++)
        if (lookup[i] == 0x48 && lookup[i + 1] == 0x6B && lookup[i + 2] == 0xC0 && lookup[i + 3] == 0x1C &&
            lookup[i + 4] == 0x48 && lookup[i + 5] == 0x03 && lookup[i + 6] == 0x41 && lookup[i + 7] == 0x18) layoutOk = 1;
    if (!layoutOk) return 0;

    u8 *getter = Rel32(getterCall + 1);
    if (!(getter[0] == 0x48 && getter[1] == 0x8B && getter[2] == 0x05 && getter[7] == 0xC3)) return 0;
    g_gamePtr = (void **)Rel32(getter + 3);
    g_gridOff = *(u32 *)(gridAdd + 2);

    u8 *hook = loop + 0x1B;                 // xor ecx,ecx ; mov [rsp+74],0 ; xorps xmm0,xmm0 (14 bytes)
    g_retKeep = hook + 14;
    u8 *callIter = loop + 0xDE;             // E8 rel32 (iterator ++)
    u8 *jmpHead = loop + 0xE3;              // E9 rel32 (back to loop head)
    if (callIter[0] != 0xE8 || jmpHead[0] != 0xE9) return 0;
    g_iterNext = Rel32(callIter + 1);
    g_loopHead = Rel32(jmpHead + 1);
    if (g_loopHead != (void *)loop) return 0;

    u8 patch[14] = {0xFF, 0x25, 0, 0, 0, 0};  // jmp qword ptr [rip+0]
    *(u64 *)(patch + 6) = (u64)(void *)ore_tramp;
    u32 old;
    if (!VirtualProtect(hook, 14, 0x40, &old)) return 0;
    for (int i = 0; i < 14; i++) hook[i] = patch[i];
    VirtualProtect(hook, 14, old, &old);
    FlushInstructionCache(GetCurrentProcess(), hook, 14);
    return 1;
}

static u32 WINAPI Worker(void *arg) {
    (void)arg;
    InstallHook();
    return 0;
}

BOOL WINAPI DllMain(HANDLE inst, u32 reason, void *reserved) {
    (void)reserved;
    if (reason == 1) {
        DisableThreadLibraryCalls(inst);
        LoadRealDsound();
        InitPathsAndConfig(inst);
        if (g_enabled) {
            HANDLE t = CreateThread(0, 0, Worker, 0, 0, 0);
            if (t) CloseHandle(t);
        }
    }
    return 1;
}
