#pragma once
#include <cstdio>
#include <cstdarg>

static char g_logPath[MAX_PATH] = "rfwm.log";

static void InitLogPath() {
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* slash = strrchr(exePath, '\\');
    if (slash) {
        strcpy_s(slash + 1, MAX_PATH - (int)(slash - exePath) - 1, "rfwm.log");
        strcpy_s(g_logPath, exePath);
    }
}

static void LogWrite(const char* fmt, ...) {
    char buf[1024];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int hlen = sprintf_s(buf, sizeof(buf), "[%02d:%02d:%02d] ",
        st.wHour, st.wMinute, st.wSecond);
    if (hlen < 0) hlen = 0;
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(buf + hlen, sizeof(buf) - hlen, _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") == 0 && f) {
        fputs(buf, f);
        fputc('\n', f);
        fclose(f);
    }
}

#define LOG(...) LogWrite(__VA_ARGS__)