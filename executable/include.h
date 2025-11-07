#pragma once

#ifndef __cplusplus
typedef int bool;
#define true 1
#define false 0
#endif

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

#include <jni.h>

static void print_jar(wchar_t* text) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD originalAttrs = csbi.wAttributes;

    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprintf(L"Valid File JAR: %ls", text);
    SetConsoleTextAttribute(hConsole, originalAttrs);
}

static void print_volume(wchar_t* text) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD originalAttrs = csbi.wAttributes;

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
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

int ReadRegistryStringC(HKEY hKeyRoot, const wchar_t* subKey, const wchar_t* valueName, wchar_t* buffer, DWORD bufferSize)
{
    if (buffer == NULL || bufferSize == 0)
        return 0;

    HKEY hKey;
    LONG result = RegOpenKeyExW(hKeyRoot, subKey, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
        return 0;

    DWORD type = 0;
    DWORD dataSize = bufferSize;

    result = RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)buffer, &dataSize);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || type != REG_SZ)
        return 0;

    size_t lastChar = dataSize / sizeof(wchar_t);
    if (lastChar >= bufferSize / sizeof(wchar_t))
        lastChar = (bufferSize / sizeof(wchar_t)) - 1;

    buffer[lastChar] = L'\0';
    return 1;
}

void CallJarInspector_listClasses(JNIEnv* env, const char* jarPath) {
    jclass cls = (*env)->FindClass(env, "JarInspector");
    if (cls == NULL) {
        (*env)->ExceptionDescribe(env);
        return;
    }

    jmethodID mid = (*env)->GetStaticMethodID(env, cls, "listClasses", "(Ljava/lang/String;)[Ljava/lang/String;");
    if (mid == NULL) {
        (*env)->ExceptionDescribe(env);
        return;
    }

    jstring jJarPath = (*env)->NewStringUTF(env, jarPath);
    if (!jJarPath) {
        return;
    }

    jobjectArray classArray = (jobjectArray)(*env)->CallStaticObjectMethod(env, cls, mid, jJarPath);

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return;
    }

    if (classArray == NULL) {
        return;
    }

    jsize length = (*env)->GetArrayLength(env, classArray);

    for (jsize i = 0; i < length; i++) {
        jstring className = (jstring)(*env)->GetObjectArrayElement(env, classArray, i);
        const char* cClassName = (*env)->GetStringUTFChars(env, className, NULL);
        printf("  %s\n", cClassName);
        (*env)->ReleaseStringUTFChars(env, className, cClassName);
        (*env)->DeleteLocalRef(env, className);
    }

    (*env)->DeleteLocalRef(env, classArray);
    (*env)->DeleteLocalRef(env, jJarPath);
}

static void inspectJarClasses(wchar_t* jarPath, JNIEnv* env) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, jarPath, -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) {
        return;
    }

    char* temp = (char*)malloc(size_needed);
    if (!temp) {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, jarPath, -1, temp, size_needed, NULL, NULL);

    if (env) {
        CallJarInspector_listClasses(env, temp);
    }

    free(temp);
}

int GetJavaVM(JNIEnv** env, JavaVM** jvm) {
    wchar_t currentVersion[128];
    if (!ReadRegistryStringC(HKEY_LOCAL_MACHINE, L"SOFTWARE\\JavaSoft\\Java Runtime Environment", L"CurrentVersion", currentVersion, sizeof(currentVersion))) {
        return 0;
    }

    wchar_t versionKey[256];
    _snwprintf_s(versionKey, sizeof(versionKey) / sizeof(wchar_t), _TRUNCATE, L"SOFTWARE\\JavaSoft\\Java Runtime Environment\\%s", currentVersion);

    wchar_t jvmDllPath[MAX_PATH];
    if (!ReadRegistryStringC(HKEY_LOCAL_MACHINE, versionKey, L"RuntimeLib", jvmDllPath, sizeof(jvmDllPath))) {
        return 0;
    }

    HMODULE hJvm = LoadLibraryW(jvmDllPath);
    if (!hJvm) {
        return 0;
    }

    typedef jint(JNICALL* CreateJavaVM_func)(JavaVM**, void**, void*);
    CreateJavaVM_func JNI_CreateJavaVM = (CreateJavaVM_func)GetProcAddress(hJvm, "JNI_CreateJavaVM");
    if (!JNI_CreateJavaVM) {
        FreeLibrary(hJvm);
        return 0;
    }

    JavaVMInitArgs vm_args;
    vm_args.version = JNI_VERSION_1_8;
    vm_args.nOptions = 0;
    vm_args.options = NULL;
    vm_args.ignoreUnrecognized = JNI_FALSE;

    jint res = JNI_CreateJavaVM(jvm, (void**)env, &vm_args);
    if (res != JNI_OK) {
        FreeLibrary(hJvm);
        return 0;
    }

    return 1;
}

BOOL is_jar_signature(const wchar_t* path) {
    BYTE buffer[4];
    DWORD bytesRead = 0;
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    BOOL result = FALSE;
    if (ReadFile(hFile, buffer, 4, &bytesRead, NULL) && bytesRead == 4) {
        if (buffer[0] == 'P' && buffer[1] == 'K' && buffer[2] == 0x03 && buffer[3] == 0x04)
            result = TRUE;
    }

    CloseHandle(hFile);
    return result;
}

void JARParser(void) {
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

    JavaVM* jvm = NULL;
    JNIEnv* env = NULL;

    // JVM
    if (!GetJavaVM(&env, &jvm)) {
        FindClose(hFind);
        return;
    }

    bool found = false;

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
                if (p) prefetch_close(p);
                continue;
            }

            time_t executed = prefetch_executed_time(p);
            if (executed <= logonTime) {
                prefetch_close(p);
                continue;
            }

            found = true;

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

                    DWORD attrs = GetFileAttributesW(fixed);
                    BOOL exists = (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));

                    if (!exists) {
                        print_volume(fixed);
                    }
                    else {
                        size_t len = wcslen(fixed);
                        if (len > 4 && _wcsicmp(fixed + len - 4, L".jar") == 0) {
                            print_jar(fixed);
                            inspectJarClasses(fixed, env);
                            wprintf(L"\n");
                        }
                        else if (is_jar_signature(fixed)) {
                            print_modified(fixed);
                        }
                    }

                    free(fixed);
                }
                prefetch_free_filenames(names, names_count);
            }

            prefetch_close(p);
        }
    } while (FindNextFileW(hFind, &ffd) != 0);

    FindClose(hFind);

    if (!found) {
        wprintf(L"\n[!] No JAVA or JAVAW pfs files after logon time were found.\n");
    }
}