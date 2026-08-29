#pragma once
#include "pch.h"
#include "rfwm_log.h"

static int g_blockFiveM = 1;
static int g_blockCfxRe = 1;
static int g_blockEmoji = 1;
static int g_blockBuild = 1;
static int g_blockVersion = 1;
static int g_blockChannel = 1;
static wchar_t g_customKw[256] = {0};

static void LoadConfig() {
    wchar_t iniPath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, iniPath, MAX_PATH);
    wchar_t* slash = wcsrchr(iniPath, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (int)(slash - iniPath) - 1, L"RFWM.ini");
    else wcscpy_s(iniPath, L"RFWM.ini");

    g_blockFiveM = GetPrivateProfileIntW(L"RFWM", L"BlockFiveM", 1, iniPath);
    g_blockCfxRe = GetPrivateProfileIntW(L"RFWM", L"BlockCfxRe", 1, iniPath);
    g_blockEmoji = GetPrivateProfileIntW(L"RFWM", L"BlockAllEmoji", 1, iniPath);
    g_blockBuild = GetPrivateProfileIntW(L"RFWM", L"BlockBuildNumber", 1, iniPath);
    g_blockVersion = GetPrivateProfileIntW(L"RFWM", L"BlockVersion", 1, iniPath);
    g_blockChannel = GetPrivateProfileIntW(L"RFWM", L"BlockChannelTags", 1, iniPath);
    GetPrivateProfileStringW(L"RFWM", L"CustomKeywords", L"", g_customKw, 256, iniPath);

    LOG("Config: FiveM=%d CfxRe=%d Emoji=%d Build=%d Ver=%d Chan=%d KW=%d",
        g_blockFiveM, g_blockCfxRe, g_blockEmoji, g_blockBuild, g_blockVersion,
        g_blockChannel, (g_customKw[0] ? 1 : 0));
}