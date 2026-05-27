#include "dirent.h"

#ifdef _WIN32

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct DIR {
  HANDLE handle;
  WIN32_FIND_DATAW data;
  struct dirent ent;
  bool first;
  wchar_t pattern[MAX_PATH];
};

static void normalize_slashes_w(wchar_t *s) {
  for (; s && *s; s++) {
    if (*s == L'/')
      *s = L'\\';
  }
}

DIR *opendir(const char *name) {
  if (!name || !name[0])
    return NULL;

  DIR *d = (DIR *)calloc(1, sizeof(DIR));
  if (!d)
    return NULL;

  wchar_t wpath[MAX_PATH];
  int wn = MultiByteToWideChar(CP_UTF8, 0, name, -1, wpath, MAX_PATH);
  if (wn <= 0 || wn >= MAX_PATH) {
    free(d);
    return NULL;
  }

  normalize_slashes_w(wpath);

  wcsncpy(d->pattern, wpath, MAX_PATH - 1);
  d->pattern[MAX_PATH - 1] = L'\0';

  size_t n = wcslen(d->pattern);
  if (n == 0 || (n + 3) >= MAX_PATH) {
    free(d);
    return NULL;
  }
  if (d->pattern[n - 1] != L'\\') {
    d->pattern[n++] = L'\\';
    d->pattern[n] = L'\0';
  }
  wcscat(d->pattern, L"*");

  d->handle = FindFirstFileW(d->pattern, &d->data);
  if (d->handle == INVALID_HANDLE_VALUE) {
    free(d);
    return NULL;
  }
  d->first = true;
  return d;
}

static uint8_t dtype_from_attrs(DWORD attrs) {
  if (attrs & FILE_ATTRIBUTE_DIRECTORY)
    return DT_DIR;
  return DT_REG;
}

struct dirent *readdir(DIR *dirp) {
  if (!dirp || dirp->handle == INVALID_HANDLE_VALUE)
    return NULL;

  for (;;) {
    WIN32_FIND_DATAW *data = &dirp->data;
    bool ok = true;
    if (dirp->first) {
      dirp->first = false;
    } else {
      ok = FindNextFileW(dirp->handle, data) ? true : false;
    }
    if (!ok)
      return NULL;

    const wchar_t *name = data->cFileName;
    if (!name || !name[0])
      continue;
    if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0)
      continue;

    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, dirp->ent.d_name,
                                (int)sizeof(dirp->ent.d_name), NULL, NULL);
    if (n <= 0 || n >= (int)sizeof(dirp->ent.d_name))
      continue;
    dirp->ent.d_type = dtype_from_attrs(data->dwFileAttributes);
    return &dirp->ent;
  }
}

void rewinddir(DIR *dirp) {
  if (!dirp || dirp->handle == INVALID_HANDLE_VALUE)
    return;
  FindClose(dirp->handle);
  dirp->handle = FindFirstFileW(dirp->pattern, &dirp->data);
  dirp->first = true;
}

int closedir(DIR *dirp) {
  if (!dirp)
    return -1;
  if (dirp->handle != INVALID_HANDLE_VALUE)
    FindClose(dirp->handle);
  free(dirp);
  return 0;
}

#endif
