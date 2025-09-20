#pragma once

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

typedef struct {
    unsigned char* data;
    size_t size;
    int ok;
} prefetch_t;

static int read_file_to_buffer(const char* path, unsigned char** out_buf, size_t* out_size) {
    if (!path || !out_buf || !out_size) return 0;
    *out_buf = NULL;
    *out_size = 0;

    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }

    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (r != (size_t)sz) { free(buf); return 0; }

    *out_buf = buf;
    *out_size = (size_t)sz;
    return 1;
}

static uint32_t read_u32_le(const unsigned char* p, size_t offset, size_t maxlen) {
    if (!p || offset + 4 > maxlen) return 0;
    uint32_t v;
    memcpy(&v, p + offset, 4);
    return v;
}

static uint64_t read_u64_le(const unsigned char* p, size_t offset, size_t maxlen) {
    if (!p || offset + 8 > maxlen) return 0;
    uint64_t v;
    memcpy(&v, p + offset, 8);
    return v;
}

prefetch_t* prefetch_open(const char* file_path) {
    if (!file_path) return NULL;

    unsigned char* raw = NULL;
    size_t raw_size = 0;
    if (!read_file_to_buffer(file_path, &raw, &raw_size)) return NULL;
    if (raw_size < 0x100) { free(raw); return NULL; }

    prefetch_t* p = (prefetch_t*)calloc(1, sizeof(prefetch_t));
    if (!p) { free(raw); return NULL; }

    if (raw[0] == 'M' && raw[1] == 'A' && raw[2] == 'M') {
        if (raw_size < 8) { free(raw); free(p); return NULL; }
        uint32_t signature = read_u32_le(raw, 0, raw_size);
        uint32_t decompressed_size = read_u32_le(raw, 4, raw_size);

        if ((signature & 0x00FFFFFF) != 0x004D414D) { free(raw); free(p); return NULL; }
        uint32_t compression_format = (signature & 0x0F000000) >> 24;

        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll) { free(raw); free(p); return NULL; }

        typedef NTSTATUS(NTAPI* RtlGetCompressionWorkSpaceSize_t)(USHORT, PULONG, PULONG);
        typedef NTSTATUS(NTAPI* RtlDecompressBufferEx_t)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG, PVOID);

        RtlGetCompressionWorkSpaceSize_t fGet = (RtlGetCompressionWorkSpaceSize_t)GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize");
        RtlDecompressBufferEx_t fDecomp = (RtlDecompressBufferEx_t)GetProcAddress(ntdll, "RtlDecompressBufferEx");

        if (!fGet || !fDecomp) { free(raw); free(p); return NULL; }

        ULONG workspace1 = 0, workspace2 = 0;
        NTSTATUS rc = fGet((USHORT)compression_format, &workspace1, &workspace2);
        if (rc != 0) { free(raw); free(p); return NULL; }

        unsigned char* decomp_buf = (unsigned char*)malloc((size_t)decompressed_size);
        if (!decomp_buf) { free(raw); free(p); return NULL; }

        void* workspace = malloc((size_t)workspace1);
        if (!workspace) { free(raw); free(decomp_buf); free(p); return NULL; }

        ULONG final_uncomp = 0;
        PUCHAR compressed_ptr = raw + 8;
        ULONG compressed_len = (ULONG)(raw_size - 8);

        rc = fDecomp((USHORT)compression_format,
            (PUCHAR)decomp_buf,
            (ULONG)decompressed_size,
            compressed_ptr,
            compressed_len,
            &final_uncomp,
            workspace);

        free(workspace);
        free(raw);
        if (rc != 0) { free(decomp_buf); free(p); return NULL; }

        p->data = decomp_buf;
        p->size = final_uncomp ? (size_t)final_uncomp : (size_t)decompressed_size;
        p->ok = 1;
        return p;
    }
    else if (raw_size >= 8 && raw[4] == 'S' && raw[5] == 'C' && raw[6] == 'C' && raw[7] == 'A') {
        p->data = raw;
        p->size = raw_size;
        p->ok = 1;
        return p;
    }
    else {
        free(raw);
        free(p);
        return NULL;
    }
}

void prefetch_close(prefetch_t* p) {
    if (!p) return;
    if (p->data) free(p->data);
    free(p);
}

int prefetch_success(const prefetch_t* p) {
    return p && p->ok;
}

uint64_t prefetch_executed_timestamp_raw(const prefetch_t* p) {
    if (!prefetch_success(p) || p->size < 0x88) return 0;
    return read_u64_le(p->data, 0x80, p->size);
}

static time_t filetime_to_time_t_uint64(uint64_t ftu) {
    if (ftu == 0) return (time_t)0;
    uint64_t secs = ftu / 10000000ULL;
    const uint64_t EPOCH_DIFF = 11644473600ULL;
    if (secs <= EPOCH_DIFF) return (time_t)0;
    return (time_t)(secs - EPOCH_DIFF);
}

time_t prefetch_executed_time(const prefetch_t* p) {
    uint64_t raw = prefetch_executed_timestamp_raw(p);
    return filetime_to_time_t_uint64(raw);
}

int prefetch_file_name_strings_offset(const prefetch_t* p) {
    if (!prefetch_success(p) || p->size < 0x6C) return 0;
    return (int)read_u32_le(p->data, 0x64, p->size);
}
int prefetch_file_name_strings_size(const prefetch_t* p) {
    if (!prefetch_success(p) || p->size < 0x6C) return 0;
    return (int)read_u32_le(p->data, 0x68, p->size);
}

wchar_t** prefetch_get_filenames(const prefetch_t* p, size_t* out_count) {
    if (!prefetch_success(p) || !out_count) return NULL;
    *out_count = 0;

    int offset = prefetch_file_name_strings_offset(p);
    int sz = prefetch_file_name_strings_size(p);
    if (offset <= 0 || sz <= 0) return NULL;
    if ((size_t)offset >= p->size) return NULL;
    if ((size_t)offset + (size_t)sz > p->size) sz = (int)(p->size - offset);

    const unsigned char* start = p->data + offset;
    int bytes = sz;

    size_t count = 0;
    int i = 0;
    while (i + 1 < bytes) {
        uint16_t ch = start[i] | (start[i + 1] << 8);
        if (ch == 0) { ++count; i += 2; continue; }
        i += 2;
    }

    if (count == 0) return NULL;

    wchar_t** arr = (wchar_t**)malloc(sizeof(wchar_t*) * count);
    if (!arr) return NULL;

    size_t idx = 0;
    i = 0;
    int cur_start = 0;
    while (i + 1 < bytes && idx < count) {
        uint16_t ch = start[i] | (start[i + 1] << 8);
        if (ch == 0) {
            int wlen = (i - cur_start) / 2;
            wchar_t* w = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
            if (!w) { for (size_t k = 0; k < idx; ++k) free(arr[k]); free(arr); return NULL; }
            for (int k = 0; k < wlen; ++k) {
                uint16_t ch2 = start[cur_start + k * 2] | (start[cur_start + k * 2 + 1] << 8);
                w[k] = (wchar_t)ch2;
            }
            w[wlen] = L'\0';
            arr[idx++] = w;
            i += 2;
            cur_start = i;
            continue;
        }
        i += 2;
    }

    *out_count = idx;
    return arr;
}

void prefetch_free_filenames(wchar_t** arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; ++i) free(arr[i]);
    free(arr);
}