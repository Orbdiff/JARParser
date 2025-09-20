#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static wchar_t* GetDriveLetterFromSerial(DWORD serial) {
    wchar_t volumeName[] = L"A:\\";
    for (wchar_t drive = L'A'; drive <= L'Z'; ++drive) {
        volumeName[0] = drive;
        DWORD volSerial = 0;
        if (GetVolumeInformationW(volumeName, NULL, 0, &volSerial, NULL, NULL, NULL, 0)) {
            if (volSerial == serial) {
                wchar_t* res = (wchar_t*)malloc(4 * sizeof(wchar_t));
                if (!res) return NULL;
                res[0] = drive;
                res[1] = L':';
                res[2] = L'\\';
                res[3] = L'\0';
                return res;
            }
        }
    }
    return NULL;
}

static wchar_t* ReplaceVolumeWithDrive(const wchar_t* path) {
    if (!path) return NULL;
    const wchar_t* prefix = L"\\VOLUME{";
    const wchar_t* found = wcsstr(path, prefix);
    size_t pathlen = wcslen(path);

    if (!found) {
        wchar_t* copy = (wchar_t*)malloc((pathlen + 1) * sizeof(wchar_t));
        if (!copy) return NULL;
        wcscpy(copy, path);
        return copy;
    }

    const wchar_t* end = wcschr(found, L'}');
    if (!end) goto fail_copy;

    size_t start_idx = (size_t)(found - path);
    size_t end_idx = (size_t)(end - path);

    size_t prefix_len = wcslen(prefix);
    size_t volid_len = end_idx - (start_idx + prefix_len);
    if (volid_len == 0) goto fail_copy;

    wchar_t* volid = (wchar_t*)malloc((volid_len + 1) * sizeof(wchar_t));
    if (!volid) return NULL;
    wcsncpy(volid, path + start_idx + prefix_len, volid_len);
    volid[volid_len] = L'\0';

    wchar_t* last_dash = wcsrchr(volid, L'-');
    if (!last_dash) { free(volid); goto fail_copy; }

    wchar_t* serial_str = last_dash + 1;
    DWORD serial = (DWORD)wcstoul(serial_str, NULL, 16);
    free(volid);

    wchar_t* drive = GetDriveLetterFromSerial(serial);
    if (!drive) goto fail_copy;

    size_t include_slash = 0;
    if (end_idx + 1 < pathlen && path[end_idx + 1] == L'\\') include_slash = 1;

    size_t replace_len = (end_idx - start_idx) + 1 + include_slash;
    size_t drive_len = wcslen(drive);
    size_t newlen = pathlen - replace_len + drive_len;

    wchar_t* newpath = (wchar_t*)malloc((newlen + 1) * sizeof(wchar_t));
    if (!newpath) { free(drive); return NULL; }

    size_t pos = 0;
    if (start_idx > 0) {
        wmemcpy(newpath + pos, path, start_idx);
        pos += start_idx;
    }
    wmemcpy(newpath + pos, drive, drive_len);
    pos += drive_len;

    size_t rest_start = start_idx + replace_len;
    if (rest_start < pathlen) {
        size_t rest_len = pathlen - rest_start;
        wmemcpy(newpath + pos, path + rest_start, rest_len);
        pos += rest_len;
    }
    newpath[pos] = L'\0';

    free(drive);
    return newpath;

fail_copy:
    {
        wchar_t* copy = (wchar_t*)malloc((pathlen + 1) * sizeof(wchar_t));
        if (!copy) return NULL;
        wcscpy(copy, path);
        return copy;
    }
}