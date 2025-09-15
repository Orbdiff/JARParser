#pragma once

#include <Windows.h>
#include <winioctl.h>
#include <chrono>
#include <string>
#include <format>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <map>

#include "time_utils.h"

class USNJournalReader {
public:
    USNJournalReader(const std::wstring& volumeLetter)
        : volumeLetter_(volumeLetter) {
    }

    int Run()
    {
        std::wcout << L"[*] Starting USN Journal analysis...\n\n";

        if (!Dump()) {
            std::wcerr << L"[-] Failed to read the USN Journal.\n";
            return 1;
        }

        return 0;
    }

private:
    std::wstring volumeLetter_;
    HANDLE volumeHandle_ = INVALID_HANDLE_VALUE;
    BYTE* buffer_ = nullptr;
    USN_JOURNAL_DATA_V0 journalData_{};

    struct RenameCache {
        std::string oldName;
        time_t time;
    };
    std::unordered_map<ULONGLONG, RenameCache> renameCache;

    bool Dump()
    {
        if (!OpenVolume()) return false;
        if (!QueryJournal()) { CloseVolume(); return false; }
        if (!AllocateBuffer()) { CloseVolume(); return false; }

        bool result = ReadJournalAndPrint();
        Cleanup();
        return result;
    }

    bool OpenVolume()
    {
        std::wstring devicePath = L"\\\\.\\" + volumeLetter_;
        volumeHandle_ = CreateFileW(
            devicePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
        return volumeHandle_ != INVALID_HANDLE_VALUE;
    }

    bool QueryJournal()
    {
        DWORD bytesReturned = 0;
        return DeviceIoControl(
            volumeHandle_,
            FSCTL_QUERY_USN_JOURNAL,
            nullptr, 0,
            &journalData_, sizeof(journalData_),
            &bytesReturned,
            nullptr);
    }

    bool AllocateBuffer()
    {
        const DWORD bufferSize = 32 * 1024 * 1024; // 32 MB
        buffer_ = (BYTE*)VirtualAlloc(nullptr, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        return buffer_ != nullptr;
    }

    bool ReadJournalAndPrint()
    {
        READ_USN_JOURNAL_DATA_V0 readData = {};
        readData.StartUsn = journalData_.FirstUsn;
        readData.ReasonMask = 0xFFFFFFFF;
        readData.UsnJournalID = journalData_.UsnJournalID;

        const DWORD bufferSize = 32 * 1024 * 1024;
        DWORD bytesReturned = 0;
        size_t totalRecords = 0;

        time_t logonTime = GetCurrentUserLogonTime();

        while (DeviceIoControl(
            volumeHandle_,
            FSCTL_READ_USN_JOURNAL,
            &readData, sizeof(readData),
            buffer_, bufferSize,
            &bytesReturned,
            nullptr)) {

            if (bytesReturned <= sizeof(USN)) break;

            BYTE* ptr = buffer_ + sizeof(USN);
            BYTE* end = buffer_ + bytesReturned;

            while (ptr < end) {
                USN_RECORD_V2* record = reinterpret_cast<USN_RECORD_V2*>(ptr);
                if (record->RecordLength == 0) break;

                std::wstring filenameW(record->FileName, record->FileNameLength / sizeof(WCHAR));
                char filenameUtf8[1024] = {};
                int len = WideCharToMultiByte(CP_UTF8, 0, record->FileName,
                    record->FileNameLength / sizeof(WCHAR),
                    filenameUtf8, sizeof(filenameUtf8) - 1, nullptr, nullptr);
                filenameUtf8[len] = '\0';

                FILETIME ft;
                ft.dwLowDateTime = record->TimeStamp.LowPart;
                ft.dwHighDateTime = record->TimeStamp.HighPart;
                time_t usnTime = FileTimeToTimeT(ft);

                if (usnTime > logonTime) {
                    HandleUsnRecord(record, filenameUtf8, usnTime, totalRecords);
                }

                ptr += record->RecordLength;
            }

            readData.StartUsn = *(USN*)buffer_;
        }

        std::wcout << std::format(L"[+] Total JAVA/JAVAW .pf delete/rename events after logon time: {}\n", totalRecords);
        return true;
    }

    bool IsTargetPF(const std::string& filename)
    {
        if (filename.size() < 4) return false;

        if (_stricmp(filename.substr(filename.size() - 3).c_str(), ".pf") != 0)
            return false;

        std::string upper = filename;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        return (upper.rfind("JAVA.EXE-", 0) == 0 || upper.rfind("JAVAW.EXE-", 0) == 0);
    }

    void HandleUsnRecord(const USN_RECORD_V2* record, const std::string& filename, time_t usnTime, size_t& totalRecords)
    {
        auto fileRef = record->FileReferenceNumber;

        if (record->Reason & USN_REASON_RENAME_OLD_NAME) {
            renameCache[fileRef] = { filename, usnTime };
        }
        else if (record->Reason & USN_REASON_RENAME_NEW_NAME) {
            auto it = renameCache.find(fileRef);
            if (it != renameCache.end()) {
                if (IsTargetPF(it->second.oldName)) {
                    std::cout << "Rename: " << it->second.oldName
                        << " -> " << filename << "\n";
                    std::cout << "Time: ";
                    print_time(usnTime);
                    std::cout << "\n\n";
                    totalRecords++;
                }
                renameCache.erase(it);
            }
        }
        else if (record->Reason & USN_REASON_FILE_DELETE) {
            if (IsTargetPF(filename)) {
                std::cout << "File Deleted: " << filename << "\n";
                std::cout << "Time: ";
                print_time(usnTime);
                std::cout << "\n\n";
                totalRecords++;
            }
        }
    }


    void Cleanup()
    {
        FreeBuffer();
        CloseVolume();
    }

    void FreeBuffer()
    {
        if (buffer_) {
            VirtualFree(buffer_, 0, MEM_RELEASE);
            buffer_ = nullptr;
        }
    }

    void CloseVolume()
    {
        if (volumeHandle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(volumeHandle_);
            volumeHandle_ = INVALID_HANDLE_VALUE;
        }
    }
};
