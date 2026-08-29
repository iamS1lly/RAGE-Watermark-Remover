#pragma once
#include "pch.h"
#include "rfwm_log.h"
#include "rfwm_config.h"

typedef void(__fastcall* DrawTextOrig)(
    void*, const void*, const void*, const void*, float, float, const void*);

static DrawTextOrig g_origDrawText = nullptr;
static DrawTextOrig g_origDrawTextV2 = nullptr;
static HMODULE g_ourModule = nullptr;

extern "C" BOOL SafeReadMem(const void* addr, void* buf, SIZE_T size) {
    __try {
        memcpy(buf, addr, size);
        return TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

static BOOL MatchW(const wchar_t* str, const wchar_t* pat) {
    if (!str || !pat) return FALSE;
    return wcsstr(str, pat) != nullptr;
}

static BOOL IsDigits(const wchar_t* text, size_t minLen, size_t maxLen) {
    if (!text) return FALSE;
    size_t len = 0;
    while (text[len]) {
        if (text[len] < L'0' || text[len] > L'9') return FALSE;
        len++;
    }
    return len >= minLen && len <= maxLen;
}

static BOOL HasSurrogate(const wchar_t* text) {
    if (!text) return FALSE;
    for (int i = 0; text[i]; i++) {
        if (text[i] >= 0xD800 && text[i] <= 0xDBFF) return TRUE;
    }
    return FALSE;
}

static BOOL RunBlockChecks(const wchar_t* text) {
    if (g_blockFiveM) {
        if (MatchW(text, L"FiveM") || MatchW(text, L"RedM") || MatchW(text, L"LibertyM"))
            return TRUE;
    }
    if (g_blockCfxRe && (MatchW(text, L"Cfx.re") || MatchW(text, L"cfx.re")))
        return TRUE;
    if (g_blockEmoji && HasSurrogate(text))
        return TRUE;
    if (g_blockBuild && (MatchW(text, L"mod") || MatchW(text, L"Mod") || MatchW(text, L"MOD")))
        return TRUE;
    if (g_blockVersion && (MatchW(text, L"Ver.") || IsDigits(text, 4, 6)))
        return TRUE;
    if (g_blockChannel) {
        if (MatchW(text, L" (Canary)") || MatchW(text, L" (Beta)") || MatchW(text, L" (SDK)"))
            return TRUE;
        if (MatchW(text, L" (b") || MatchW(text, L" [e"))
            return TRUE;
    }
    if (g_customKw[0]) {
        wchar_t kw[256];
        wcscpy_s(kw, g_customKw);
        wchar_t* ctx = nullptr;
        wchar_t* tok = wcstok_s(kw, L",", &ctx);
        while (tok) {
            while (*tok == L' ') tok++;
            if (*tok && MatchW(text, tok)) return TRUE;
            tok = wcstok_s(nullptr, L",", &ctx);
        }
    }
    return FALSE;
}

static BOOL ShouldBlockText(const void* textParam) {
    if (!textParam) return FALSE;

    const wchar_t* raw = (const wchar_t*)textParam;
    if (RunBlockChecks(raw)) return TRUE;

    size_t myRes = 0;
    if (!SafeReadMem((const BYTE*)textParam + 0x18, &myRes, sizeof(myRes)))
        return FALSE;

    if (myRes >= 8) {
        const wchar_t* heapPtr = nullptr;
        if (!SafeReadMem(textParam, &heapPtr, sizeof(heapPtr)))
            return FALSE;
        if (!heapPtr) return FALSE;

        size_t mySize = 0;
        if (!SafeReadMem((const BYTE*)textParam + 0x10, &mySize, sizeof(mySize)))
            return FALSE;

        size_t readLen = (mySize > 0 && mySize < 255) ? mySize : 0;
        if (readLen == 0) return FALSE;

        wchar_t heapBuf[256] = { 0 };
        if (!SafeReadMem(heapPtr, heapBuf, readLen * sizeof(wchar_t)))
            return FALSE;
        heapBuf[readLen] = L'\0';

        if (RunBlockChecks(heapBuf)) return TRUE;
    }

    return FALSE;
}

static void DoCallOrig(DrawTextOrig fn, void* p1, const void* p2, const void* p3,
    const void* p4, float f1, float f2, const void* p5) {
    __try {
        fn(p1, p2, p3, p4, f1, f2, p5);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void HandleDrawText(DrawTextOrig origFn,
    void* thisPtr, const void* textParam,
    const void* drawRect, const void* color,
    float fontSize, float fontScale, const void* fontName)
{
    if (ShouldBlockText(textParam)) return;
    DoCallOrig(origFn, thisPtr, textParam, drawRect, color, fontSize, fontScale, fontName);
}

static void __fastcall HookedDrawTextV1(
    void* thisPtr, const void* textParam,
    const void* drawRect, const void* color,
    float fontSize, float fontScale, const void* fontName)
{
    HandleDrawText(g_origDrawText, thisPtr, textParam, drawRect, color, fontSize, fontScale, fontName);
}

static void __fastcall HookedDrawTextV2(
    void* thisPtr, const void* textParam,
    const void* drawRect, const void* color,
    float fontSize, float fontScale, const void* fontName)
{
    HandleDrawText(g_origDrawTextV2, thisPtr, textParam, drawRect, color, fontSize, fontScale, fontName);
}

static FARPROC FindDataExport(HMODULE hMod, const char* partialName) {
    if (!hMod) return nullptr;
    BYTE* base = (BYTE*)hMod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    DWORD exportRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!exportRva) return nullptr;
    IMAGE_EXPORT_DIRECTORY* exp = (IMAGE_EXPORT_DIRECTORY*)(base + exportRva);
    DWORD* names = (DWORD*)(base + exp->AddressOfNames);
    WORD* ordinals = (WORD*)(base + exp->AddressOfNameOrdinals);
    DWORD* funcs = (DWORD*)(base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        const char* name = (const char*)(base + names[i]);
        if (strstr(name, partialName)) {
            return (FARPROC)(base + funcs[ordinals[i]]);
        }
    }
    return nullptr;
}

static void PatchMem(BYTE* addr, SIZE_T len) {
    DWORD op;
    if (VirtualProtect(addr, len, PAGE_READWRITE, &op)) {
        memset(addr, 0, len);
        VirtualProtect(addr, len, op, &op);
    }
}

static void ToWideBytes(const char* ascii, BYTE* out, SIZE_T* outLen) {
    SIZE_T alen = strlen(ascii);
    for (SIZE_T i = 0; i < alen; i++) {
        out[i * 2] = (BYTE)ascii[i];
        out[i * 2 + 1] = 0;
    }
    *outLen = alen * 2;
}

static BOOL IsFiveMModule(const char* name) {
    char lower[MAX_PATH] = { 0 };
    for (int i = 0; name[i] && i < MAX_PATH - 1; i++) {
        lower[i] = (char)tolower((unsigned char)name[i]);
    }
    if (strstr(lower, "five") || strstr(lower, "citizen") || strstr(lower, "cfx"))
        return TRUE;
    if (strstr(lower, "font-renderer"))
        return TRUE;
    return FALSE;
}

struct PatInfo {
    const char* text;
    int cfgGroup;
};

static int ScanModuleStrings(HMODULE hMod, const char* modName) {
    if (!hMod) return 0;
    if (hMod == g_ourModule) return 0;
    if (!IsFiveMModule(modName)) return 0;

    __try {
        BYTE* base = (BYTE*)hMod;
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        WORD numSec = nt->FileHeader.NumberOfSections;

        PatInfo pats[] = {
            {"Ver. ",   0},
            {"Ver.",    0},
            {"mod packs", 2},
        };
        int patCount = sizeof(pats) / sizeof(pats[0]);
        int count = 0;

        const char* wideLits[] = {
            "FiveM", "RedM", "LibertyM",
            "Cfx.re", "cfx.re",
            " (b%d)", " [e%d]",
            " (SDK)", " (Canary)", " (Beta)",
        };
        int litCount = sizeof(wideLits) / sizeof(wideLits[0]);

        for (WORD s = 0; s < numSec; s++) {
            BOOL isData = (sec[s].Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) &&
                !(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE);
            if (!isData) continue;

            BYTE* sb = base + sec[s].VirtualAddress;
            SIZE_T ss = sec[s].Misc.VirtualSize;

            for (int p = 0; p < patCount; p++) {
                if (pats[p].cfgGroup == 0 && !g_blockVersion) continue;
                if (pats[p].cfgGroup == 2 && !g_blockBuild) continue;

                SIZE_T nlen = strlen(pats[p].text);

                for (SIZE_T i = 0; i + nlen <= ss; i++) {
                    if (i > 0 && sb[i - 1] != 0) { i++; continue; }
                    if (memcmp(sb + i, pats[p].text, nlen) == 0) {
                        SIZE_T end = i + nlen;
                        while (end < ss && sb[end] != 0) end++;
                        SIZE_T slen = end - i;
                        PatchMem(sb + i, slen);
                        LOG("[%s] N '%s' +0x%zX %zuB", modName, pats[p].text, (size_t)(sec[s].VirtualAddress + i), slen);
                        count++;
                        i = end;
                    }
                }

                BYTE wideBuf[128];
                SIZE_T wlen;
                ToWideBytes(pats[p].text, wideBuf, &wlen);

                for (SIZE_T i = 0; i + wlen + 1 < ss; i++) {
                    if (i >= 2 && !(sb[i - 2] == 0 && sb[i - 1] == 0)) continue;
                    if (memcmp(sb + i, wideBuf, wlen) != 0) continue;

                    SIZE_T start = i;
                    if (start >= 2) {
                        SIZE_T back = start - 2;
                        while (back >= 2 && !(sb[back] == 0 && sb[back + 1] == 0)) back -= 2;
                        if (sb[back] == 0 && sb[back + 1] == 0) start = back + 2;
                    }

                    SIZE_T end = i + wlen;
                    while (end + 1 < ss) {
                        if (sb[end] == 0 && sb[end + 1] == 0) { end += 2; break; }
                        end += 2;
                    }

                    SIZE_T slen = end - start;
                    PatchMem(sb + start, slen);
                    LOG("[%s] W '%s' +0x%zX %zuB", modName, pats[p].text, (size_t)(sec[s].VirtualAddress + start), slen);
                    count++;
                    i = end;
                }
            }

            for (int l = 0; l < litCount; l++) {
                BOOL skip = FALSE;
                if (l <= 2 && !g_blockFiveM) skip = TRUE;
                if ((l == 3 || l == 4) && !g_blockCfxRe) skip = TRUE;
                if ((l >= 5 && l <= 8) && !g_blockChannel) skip = TRUE;
                if (skip) continue;

                BYTE wb[128];
                SIZE_T wl;
                ToWideBytes(wideLits[l], wb, &wl);
                SIZE_T need = wl + 4;

                for (SIZE_T i = 0; i + need <= ss; i++) {
                    if (sb[i] != 0 || sb[i + 1] != 0) continue;
                    if (memcmp(sb + i + 2, wb, wl) != 0) continue;
                    if (sb[i + 2 + wl] != 0 || sb[i + 3 + wl] != 0) continue;

                    PatchMem(sb + i + 2, wl);
                    LOG("[%s] Lit '%s' +0x%zX", modName, wideLits[l], (size_t)(sec[s].VirtualAddress + i + 2));
                    count++;
                    i += need - 1;
                }
            }

            if (g_blockEmoji) {
                for (SIZE_T i = 0; i + 7 < ss; i++) {
                    if (sb[i] != 0 || sb[i + 1] != 0) continue;
                    BYTE hiH = sb[i + 3];
                    if (hiH < 0xD8 || hiH > 0xDB) continue;
                    BYTE loH = sb[i + 5];
                    if (loH < 0xDC || loH > 0xDF) continue;
                    if (sb[i + 6] != 0 || sb[i + 7] != 0) continue;

                    PatchMem(sb + i + 2, 4);
                    LOG("[%s] Emoji +0x%zX", modName, (size_t)(sec[s].VirtualAddress + i + 2));
                    count++;
                    i += 7;
                }
            }
        }

        return count;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static void PatchAllModules() {
    HMODULE mods[512];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) return;
    DWORD numMods = needed / sizeof(HMODULE);
    int total = 0;

    for (DWORD i = 0; i < numMods && i < 512; i++) {
        char name[MAX_PATH] = { 0 };
        GetModuleBaseNameA(GetCurrentProcess(), mods[i], name, MAX_PATH);
        int found = ScanModuleStrings(mods[i], name);
        if (found > 0) total += found;
    }

    if (total > 0) LOG("Patched %d across all modules", total);
}

static void HookVtableEntry(void** vtable, int idx, void* hookFn, DrawTextOrig* origOut) {
    void* orig = nullptr;
    if (!SafeReadMem(vtable + idx, &orig, sizeof(orig)) || !orig) return;
    DWORD op;
    if (!VirtualProtect(vtable + idx, sizeof(void*), PAGE_READWRITE, &op)) return;
    vtable[idx] = hookFn;
    VirtualProtect(vtable + idx, sizeof(void*), op, &op);
    *origOut = (DrawTextOrig)orig;
    LOG("vtable[%d] hooked", idx);
}

static void InstallFontRendererHook() {
    HMODULE hMod = GetModuleHandleA("font-renderer.dll");
    if (!hMod) return;

    LOG("font-renderer.dll %p", hMod);

    FARPROC exportAddr = FindDataExport(hMod, "TheFonts");
    if (!exportAddr) { LOG("TheFonts not found"); return; }

    void* fontObj = nullptr;
    if (!SafeReadMem(exportAddr, &fontObj, sizeof(fontObj)) || !fontObj) {
        LOG("TheFonts not init"); return;
    }

    void** vtable = nullptr;
    if (!SafeReadMem(fontObj, &vtable, sizeof(vtable)) || !vtable) {
        LOG("vtable fail"); return;
    }

    HookVtableEntry(vtable, 1, (void*)&HookedDrawTextV1, &g_origDrawText);
    HookVtableEntry(vtable, 2, (void*)&HookedDrawTextV2, &g_origDrawTextV2);

    PatchAllModules();

    LOG("RFWM ready");
}