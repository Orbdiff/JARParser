#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include <winsvc.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef long ssize_t;
#endif
#endif

static DWORD GetDcomLaunchPID() {
    DWORD pid = 0;
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hSCM) return 0;

    DWORD bytesNeeded = 0, servicesReturned = 0, resumeHandle = 0;
    EnumServicesStatusExW(hSCM,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
        NULL,
        0,
        &bytesNeeded,
        &servicesReturned,
        &resumeHandle,
        NULL);

    if (GetLastError() == ERROR_MORE_DATA) {
        BYTE* buffer = (BYTE*)malloc(bytesNeeded);
        if (buffer) {
            if (EnumServicesStatusExW(hSCM,
                SC_ENUM_PROCESS_INFO,
                SERVICE_WIN32,
                SERVICE_ACTIVE,
                buffer,
                bytesNeeded,
                &bytesNeeded,
                &servicesReturned,
                &resumeHandle,
                NULL))
            {
                ENUM_SERVICE_STATUS_PROCESS* services = (ENUM_SERVICE_STATUS_PROCESS*)buffer;
                for (DWORD i = 0; i < servicesReturned; i++) {
                    if (_wcsicmp(services[i].lpServiceName, L"DcomLaunch") == 0) {
                        pid = services[i].ServiceStatusProcess.dwProcessId;
                        break;
                    }
                }
            }
            free(buffer);
        }
    }
    CloseServiceHandle(hSCM);
    return pid;
}

static void extract_strings_from_buffer(const unsigned char* buf, size_t bufSize, ssize_t minLen, int* foundJar) {
    ssize_t start = -1, pos = 0;

    for (pos = 0; pos < (ssize_t)bufSize; pos++) {
        if (buf[pos] >= 32 && buf[pos] <= 126) {
            if (start == -1) start = pos;
        }
        else {
            if (start != -1) {
                ssize_t length = pos - start;
                if (length >= minLen) {
                    char tmp[1024];
                    if (length < (ssize_t)sizeof(tmp)) {
                        memcpy(tmp, buf + start, length);
                        tmp[length] = '\0';
                        if (strstr(tmp, "-jar")) {
                            printf("%s\n", tmp);
                            *foundJar = 1;
                        }
                    }
                }
                start = -1;
            }
        }
    }

    start = -1;
    for (pos = 0; pos < (ssize_t)bufSize - 1; pos += 2) {
        WCHAR wc = *(WCHAR*)(buf + pos);
        if (wc >= 32 && wc <= 126) {
            if (start == -1) start = pos;
        }
        else {
            if (start != -1) {
                ssize_t length = (pos - start) / 2;
                if (length >= minLen) {
                    WCHAR tmp[1024];
                    if (length < (ssize_t)(sizeof(tmp) / sizeof(WCHAR))) {
                        memcpy(tmp, buf + start, length * 2);
                        tmp[length] = L'\0';
                        if (wcsstr(tmp, L"-jar")) {
                            wprintf(L"%ls\n", tmp);
                            *foundJar = 1;
                        }
                    }
                }
                start = -1;
            }
        }
    }
}

static void DcomLaunchStrings() {
    DWORD pid = GetDcomLaunchPID();
    if (!pid) {
        wprintf(L"\nCould not get DcomLaunch PID.\n");
        return;
    }

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD originalAttrs = csbi.wAttributes;

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    wprintf(L"\n[!] Check DcomLaunch (PID: %lu)\n", pid);
    SetConsoleTextAttribute(hConsole, originalAttrs);

    HANDLE hProc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) {
        wprintf(L"\nCould not open DcomLaunch process (PID %lu).\n", pid);
        return;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* addr = 0;
    int foundJar = 0;

    while (addr < (unsigned char*)si.lpMaximumApplicationAddress) {
        if (VirtualQueryEx(hProc, addr, &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        if ((mbi.State == MEM_COMMIT) && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_READ))) {
            unsigned char* buffer = (unsigned char*)malloc(mbi.RegionSize);
            if (buffer) {
                SIZE_T bytesRead;
                if (ReadProcessMemory(hProc, addr, buffer, mbi.RegionSize, &bytesRead)) {
                    extract_strings_from_buffer(buffer, bytesRead, 4, &foundJar);
                }
                free(buffer);
            }
        }
        addr += mbi.RegionSize;
    }

    if (!foundJar) {
        wprintf(L"No strings with \"-jar\" were found.\n");
    }

    CloseHandle(hProc);
}