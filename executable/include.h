#pragma once

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "logon_time.h"
#include "prefetch_parser.h"
#include "volume_parser.h"
#include "DcomLaunch_strings.h"
#include "usnjrnl_parser.h"

static void print_jar(wchar_t* text) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD originalAttrs = csbi.wAttributes;

    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprintf(L"Valid File JAR: %ls\n", text);
    SetConsoleTextAttribute(hConsole, originalAttrs);
}

static void print_volume(wchar_t* text) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD originalAttrs = csbi.wAttributes;

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprintf(L"Not Found: %ls\n", text);
    SetConsoleTextAttribute(hConsole, originalAttrs);
}

static void print_modified(wchar_t* text) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD originalAttrs = csbi.wAttributes;

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    wprintf(L"File JAR Modified Extension or ZIP File: %ls\n", text);
    SetConsoleTextAttribute(hConsole, originalAttrs);
}

static void JARParser(void) {
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    wchar_t searchPath[MAX_PATH];

    wcscpy(searchPath, L"C:\\Windows\\Prefetch\\*.pf");

    time_t logonTime = GetCurrentUserLogonTime();
    printf("[#] Logon Time: ");
    print_time(logonTime);
    printf("\nOnly results after logon time are displayed.\n");

    hFind = FindFirstFileW(searchPath, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        wprintf(L"No Prefetch files found.\n");
        return;
    }

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (_wcsnicmp(ffd.cFileName, L"JAVA", 4) != 0 &&
                _wcsnicmp(ffd.cFileName, L"JAVAW", 5) != 0) {
                continue;
            }

            wchar_t fullPath[MAX_PATH];
            if (swprintf(fullPath, MAX_PATH, L"C:\\Windows\\Prefetch\\%ls", ffd.cFileName) < 0) {
                continue;
            }

            char path[4096];
            wcstombs(path, fullPath, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';

            prefetch_t* p = prefetch_open(path);
            if (!p || !prefetch_success(p)) {
                wprintf(L"Could not parse %ls\n", ffd.cFileName);
                if (p) prefetch_close(p);
                continue;
            }

            time_t executed = prefetch_executed_time(p);
            if (executed <= logonTime) {
                prefetch_close(p);
                continue;
            }

            wprintf(L"\nFile Name: %ls\n", ffd.cFileName);

            struct tm tmv;
            localtime_s(&tmv, &executed);
            wchar_t buf[128];
            wcsftime(buf, 128, L"%Y-%m-%d %H:%M:%S", &tmv);
            wprintf(L"Time Executed: %ls\n", buf);

            size_t names_count = 0;
            wchar_t** names = prefetch_get_filenames(p, &names_count);
            wprintf(L"Imports:\n");
            if (names) {
                for (size_t i = 0; i < names_count; ++i) {
                    wchar_t* fixed = ReplaceVolumeWithDrive(names[i]);
                    if (!fixed) continue;

                    char temp[4096];
                    size_t conv = wcstombs(temp, fixed, sizeof(temp) - 1);
                    if (conv == (size_t)-1) { free(fixed); continue; }
                    temp[conv] = '\0';

                    int isPK = 0;
                    FILE* f = fopen(temp, "rb");
                    if (f) {
                        unsigned char sig[4] = { 0 };
                        size_t r = fread(sig, 1, 4, f);
                        fclose(f);
                        if (r == 4 && sig[0] == 'P' && sig[1] == 'K' && sig[2] == 3 && sig[3] == 4)
                            isPK = 1;
                    }

                    size_t len = wcslen(fixed);
                    if (wcsstr(fixed, L"\\VOLUME{") != NULL) {
                        print_volume(fixed);
                    }
                    else if (len > 4 && _wcsicmp(fixed + len - 4, L".jar") == 0) {
                        print_jar(fixed);
                    }
                    else if (isPK) {
                        print_modified(fixed);
                    }

                    free(fixed);
                }
                prefetch_free_filenames(names, names_count);
            }

            prefetch_close(p);
        }
    } while (FindNextFileW(hFind, &ffd) != 0);

    FindClose(hFind);

    DcomLaunchStrings();
    ProcessUSNJournal();
}