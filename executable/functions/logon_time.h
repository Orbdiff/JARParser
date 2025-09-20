#pragma once

#include <windows.h>
#include <ntsecapi.h>
#include <stdio.h>
#include <time.h>

#pragma comment(lib, "Secur32.lib")

static time_t FileTimeToTimeT(const FILETIME* ft) {
    if (!ft) return 0;
    ULARGE_INTEGER ull;
    ull.LowPart = ft->dwLowDateTime;
    ull.HighPart = ft->dwHighDateTime;
    return (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

static time_t GetCurrentUserLogonTime(void) {
    wchar_t username[256];
    DWORD size = sizeof(username) / sizeof(wchar_t);
    if (!GetUserNameW(username, &size)) return 0;

    ULONG count = 0;
    PLUID sessions = NULL;
    if (LsaEnumerateLogonSessions(&count, &sessions) != 0) return 0;

    time_t result = 0;
    for (ULONG i = 0; i < count; i++) {
        PSECURITY_LOGON_SESSION_DATA pData = NULL;
        if (LsaGetLogonSessionData(&sessions[i], &pData) == 0 && pData) {
            if (pData->UserName.Buffer && pData->LogonType == Interactive &&
                _wcsicmp(pData->UserName.Buffer, username) == 0) {

                FILETIME ft;
                ft.dwLowDateTime = pData->LogonTime.LowPart;
                ft.dwHighDateTime = pData->LogonTime.HighPart;
                result = FileTimeToTimeT(&ft);
                LsaFreeReturnBuffer(pData);
                break;
            }
            LsaFreeReturnBuffer(pData);
        }
    }
    if (sessions) LsaFreeReturnBuffer(sessions);
    return result;
}

static void print_time(time_t t) {
    if (t == 0) return;
    struct tm timeinfo;
    localtime_s(&timeinfo, &t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    printf("%s", buf);
}