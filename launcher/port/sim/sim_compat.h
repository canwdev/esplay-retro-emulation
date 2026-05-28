#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m)&_S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m)&_S_IFMT) == _S_IFREG)
#endif
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static inline int sim_utf8_to_wide(const char *in, wchar_t *out,
                                   int out_chars) {
#ifdef _WIN32
  if (!in || !out || out_chars <= 0)
    return 0;
  int n = MultiByteToWideChar(CP_UTF8, 0, in, -1, out, out_chars);
  if (n <= 0)
    out[0] = L'\0';
  return n;
#else
  (void)in;
  (void)out;
  (void)out_chars;
  return 0;
#endif
}

static inline bool sim_path_get_info_utf8(const char *path, bool *out_is_dir,
                                         bool *out_is_reg,
                                         unsigned long *out_size,
                                         unsigned long *out_winerr) {
#ifdef _WIN32
  if (out_is_dir)
    *out_is_dir = false;
  if (out_is_reg)
    *out_is_reg = false;
  if (out_size)
    *out_size = 0;
  if (out_winerr)
    *out_winerr = 0;

  wchar_t wpath[1024];
  if (sim_utf8_to_wide(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) <=
      0) {
    if (out_winerr)
      *out_winerr = (unsigned long)GetLastError();
    return false;
  }

  WIN32_FILE_ATTRIBUTE_DATA info;
  if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &info)) {
    if (out_winerr)
      *out_winerr = (unsigned long)GetLastError();
    return false;
  }

  bool is_dir = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  if (out_is_dir)
    *out_is_dir = is_dir;
  if (out_is_reg)
    *out_is_reg = !is_dir;
  if (out_size && !is_dir) {
    unsigned long long sz =
        ((unsigned long long)info.nFileSizeHigh << 32ULL) | info.nFileSizeLow;
    *out_size = (unsigned long)(sz > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : sz);
  }
  return true;
#else
  (void)path;
  (void)out_is_dir;
  (void)out_is_reg;
  (void)out_size;
  (void)out_winerr;
  return false;
#endif
}

static inline bool sim_delete_utf8(const char *path,
                                  unsigned long *out_winerr) {
#ifdef _WIN32
  if (out_winerr)
    *out_winerr = 0;
  wchar_t wpath[1024];
  if (sim_utf8_to_wide(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) <=
      0) {
    if (out_winerr)
      *out_winerr = (unsigned long)GetLastError();
    return false;
  }

  DWORD attr = GetFileAttributesW(wpath);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (out_winerr)
      *out_winerr = (unsigned long)GetLastError();
    return false;
  }
  SetFileAttributesW(wpath, attr & ~FILE_ATTRIBUTE_READONLY);

  if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    if (!DeleteFileW(wpath)) {
      if (out_winerr)
        *out_winerr = (unsigned long)GetLastError();
      return false;
    }
    return true;
  }

  wchar_t search[1024];
  int wlen = lstrlenW(wpath);
  if (wlen <= 0 || wlen + 3 >= (int)(sizeof(search) / sizeof(search[0]))) {
    if (out_winerr)
      *out_winerr = ERROR_BUFFER_OVERFLOW;
    return false;
  }
  lstrcpyW(search, wpath);
  if (search[wlen - 1] != L'\\' && search[wlen - 1] != L'/')
    lstrcatW(search, L"\\");
  lstrcatW(search, L"*");

  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(search, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (lstrcmpW(fd.cFileName, L".") == 0 ||
          lstrcmpW(fd.cFileName, L"..") == 0)
        continue;

      wchar_t child[1024];
      if (wlen + 1 + lstrlenW(fd.cFileName) >=
          (int)(sizeof(child) / sizeof(child[0]))) {
        FindClose(h);
        if (out_winerr)
          *out_winerr = ERROR_BUFFER_OVERFLOW;
        return false;
      }
      lstrcpyW(child, wpath);
      if (child[wlen - 1] != L'\\' && child[wlen - 1] != L'/')
        lstrcatW(child, L"\\");
      lstrcatW(child, fd.cFileName);

      int needed = WideCharToMultiByte(CP_UTF8, 0, child, -1, NULL, 0, NULL, NULL);
      char child_utf8[1024];
      if (needed <= 0 || needed > (int)sizeof(child_utf8) ||
          WideCharToMultiByte(CP_UTF8, 0, child, -1, child_utf8,
                              (int)sizeof(child_utf8), NULL, NULL) <= 0) {
        FindClose(h);
        if (out_winerr)
          *out_winerr = (unsigned long)GetLastError();
        return false;
      }
      if (!sim_delete_utf8(child_utf8, out_winerr)) {
        FindClose(h);
        return false;
      }
    } while (FindNextFileW(h, &fd));

    DWORD find_err = GetLastError();
    FindClose(h);
    if (find_err != ERROR_NO_MORE_FILES) {
      if (out_winerr)
        *out_winerr = (unsigned long)find_err;
      return false;
    }
  } else {
    DWORD find_err = GetLastError();
    if (find_err != ERROR_FILE_NOT_FOUND) {
      if (out_winerr)
        *out_winerr = (unsigned long)find_err;
      return false;
    }
  }

  if (!RemoveDirectoryW(wpath)) {
    if (out_winerr)
      *out_winerr = (unsigned long)GetLastError();
    return false;
  }
  return true;
#else
  (void)path;
  (void)out_winerr;
  return false;
#endif
}

static inline FILE *sim_fopen_utf8(const char *path, const char *mode,
                                   unsigned long *out_winerr) {
#ifdef _WIN32
  if (out_winerr)
    *out_winerr = 0;
  wchar_t wpath[1024];
  wchar_t wmode[32];
  if (sim_utf8_to_wide(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) <=
          0 ||
      sim_utf8_to_wide(mode, wmode, (int)(sizeof(wmode) / sizeof(wmode[0]))) <=
          0) {
    if (out_winerr)
      *out_winerr = (unsigned long)GetLastError();
    return NULL;
  }
  FILE *f = _wfopen(wpath, wmode);
  if (!f && out_winerr)
    *out_winerr = (unsigned long)GetLastError();
  return f;
#else
  (void)out_winerr;
  return fopen(path, mode);
#endif
}

static inline size_t strlcpy(char *dst, const char *src, size_t dstsz) {
  size_t srclen = src ? strlen(src) : 0;
  if (dstsz) {
    size_t n = (srclen >= dstsz) ? (dstsz - 1) : srclen;
    if (n)
      memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return srclen;
}

static inline size_t strlcat(char *dst, const char *src, size_t dstsz) {
  size_t dlen = 0;
  while (dlen < dstsz && dst[dlen] != '\0')
    dlen++;

  size_t srclen = src ? strlen(src) : 0;
  if (dlen == dstsz)
    return dlen + srclen;

  size_t space = dstsz - dlen - 1;
  size_t n = (srclen > space) ? space : srclen;
  if (n)
    memcpy(dst + dlen, src, n);
  dst[dlen + n] = '\0';
  return dlen + srclen;
}
