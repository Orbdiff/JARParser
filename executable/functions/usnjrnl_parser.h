#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "logon_time.h"

typedef struct RenameEntry {
    ULONGLONG fileRef;
    char* oldName;
    time_t time;
    struct RenameEntry* next;
} RenameEntry;

typedef struct {
    RenameEntry* head;
} RenameCache;

static void RenameCache_Init(RenameCache* c) { c->head = NULL; }

static void RenameCache_Free(RenameCache* c) {
    RenameEntry* e = c->head;
    while (e) {
        RenameEntry* n = e->next;
        if (e->oldName) free(e->oldName);
        free(e);
        e = n;
    }
    c->head = NULL;
}

static void RenameCache_Insert(RenameCache* c, ULONGLONG fileRef, const char* oldName, time_t t)
{
    RenameEntry* it = c->head;
    while (it) {
        if (it->fileRef == fileRef) {
            free(it->oldName);
            it->oldName = _strdup(oldName);
            it->time = t;
            return;
        }
        it = it->next;
    }

    RenameEntry* n = (RenameEntry*)malloc(sizeof(RenameEntry));
    if (n == NULL)
        return;

    n->fileRef = fileRef;
    n->oldName = _strdup(oldName);
    n->time = t;
    n->next = c->head;
    c->head = n;
}

static RenameEntry* RenameCache_Find(RenameCache* c, ULONGLONG fileRef) {
    RenameEntry* it = c->head;
    while (it) {
        if (it->fileRef == fileRef) return it;
        it = it->next;
    }
    return NULL;
}

static void RenameCache_Remove(RenameCache* c, ULONGLONG fileRef) {
    RenameEntry* prev = NULL;
    RenameEntry* it = c->head;
    while (it) {
        if (it->fileRef == fileRef) {
            if (prev) prev->next = it->next;
            else c->head = it->next;
            if (it->oldName) free(it->oldName);
            free(it);
            return;
        }
        prev = it;
        it = it->next;
    }
}

static int ends_with_ci(const char* str, const char* suf) {
    size_t n = strlen(str);
    size_t m = strlen(suf);
    if (n < m) return 0;
    const char* a = str + (n - m);
    for (size_t i = 0; i < m; ++i) {
        char ca = a[i], cb = suf[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

static int is_target_pf(const char* filename) {
    if (!filename) return 0;
    if (!ends_with_ci(filename, ".pf")) return 0;
    if (_strnicmp(filename, "JAVA.EXE-", 9) == 0) return 1;
    if (_strnicmp(filename, "JAVAW.EXE-", 10) == 0) return 1;
    return 0;
}

static int ProcessUSNJournal(void) {
    const wchar_t* volumeLetter = L"C:";
    wchar_t devicePath[16];
    swprintf(devicePath, _countof(devicePath), L"\\\\.\\%s", volumeLetter);

    HANDLE hVol = CreateFileW(
        devicePath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    if (hVol == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"Error opening volume %s (error %u)\n", devicePath, GetLastError());
        return 0;
    }

    USN_JOURNAL_DATA_V0 journalData;
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, NULL, 0,
        &journalData, sizeof(journalData),
        &bytesReturned, NULL)) {
        fwprintf(stderr, L"FSCTL_QUERY_USN_JOURNAL failed (error %u)\n", GetLastError());
        CloseHandle(hVol);
        return 0;
    }

    const DWORD bufferSize = 32 * 1024 * 1024;
    BYTE* buffer = (BYTE*)VirtualAlloc(NULL, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer) {
        fwprintf(stderr, L"VirtualAlloc failed\n");
        CloseHandle(hVol);
        return 0;
    }

    READ_USN_JOURNAL_DATA_V0 readData;
    ZeroMemory(&readData, sizeof(readData));
    readData.StartUsn = journalData.FirstUsn;
    readData.ReasonMask = 0xFFFFFFFF;
    readData.UsnJournalID = journalData.UsnJournalID;

    DWORD cb = 0;
    size_t totalRecords = 0;
    time_t logonTime = GetCurrentUserLogonTime();

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD originalAttrs = csbi.wAttributes;

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    wprintf(L"\n[*] Starting USN Journal analysis...\n\n");
    SetConsoleTextAttribute(hConsole, originalAttrs);

    RenameCache cache;
    RenameCache_Init(&cache);

    while (DeviceIoControl(hVol, FSCTL_READ_USN_JOURNAL,
        &readData, sizeof(readData),
        buffer, bufferSize,
        &cb, NULL)) {
        if (cb <= sizeof(USN)) break;

        BYTE* ptr = buffer + sizeof(USN);
        BYTE* end = buffer + cb;

        while (ptr < end) {
            USN_RECORD_V2* rec = (USN_RECORD_V2*)ptr;
            if (rec->RecordLength == 0) break;

            int wcharCount = (int)(rec->FileNameLength / sizeof(WCHAR));
            char filenameUtf8[1024] = { 0 };
            if (wcharCount > 0) {
                int len = WideCharToMultiByte(CP_UTF8, 0,
                    rec->FileName, wcharCount,
                    filenameUtf8,
                    (int)sizeof(filenameUtf8) - 1,
                    NULL, NULL);
                if (len > 0) filenameUtf8[len] = '\0';
            }

            FILETIME ft;
            ft.dwLowDateTime = rec->TimeStamp.LowPart;
            ft.dwHighDateTime = rec->TimeStamp.HighPart;
            time_t usnTime = FileTimeToTimeT(&ft);

            if (usnTime > logonTime) {
                ULONGLONG fileRef = rec->FileReferenceNumber;

                if (rec->Reason & USN_REASON_RENAME_OLD_NAME) {
                    RenameCache_Insert(&cache, fileRef, filenameUtf8, usnTime);
                }
                else if (rec->Reason & USN_REASON_RENAME_NEW_NAME) {
                    RenameEntry* e = RenameCache_Find(&cache, fileRef);
                    if (e) {
                        if (is_target_pf(e->oldName)) {
                            printf("Rename: %s -> %s\n", e->oldName, filenameUtf8);
                            printf("Time: ");
                            print_time(usnTime);
                            printf("\n\n");
                            totalRecords++;
                        }
                        RenameCache_Remove(&cache, fileRef);
                    }
                }
                else if (rec->Reason & USN_REASON_FILE_DELETE) {
                    if (is_target_pf(filenameUtf8)) {
                        printf("File Deleted: %s\n", filenameUtf8);
                        printf("Time: ");
                        print_time(usnTime);
                        printf("\n\n");
                        totalRecords++;
                    }
                }
            }

            ptr += rec->RecordLength;
        }

        readData.StartUsn = *(USN*)buffer;
    }

    printf("[+] Total JAVA/JAVAW .pf delete/rename events after logon time: %zu\n", totalRecords);

    RenameCache_Free(&cache);
    VirtualFree(buffer, 0, MEM_RELEASE);
    CloseHandle(hVol);
    return 1;
}