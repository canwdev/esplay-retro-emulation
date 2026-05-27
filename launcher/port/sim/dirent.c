#include "dirent.h"

#ifdef _WIN32

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct DIR {
  HANDLE handle;
  WIN32_FIND_DATAA data;
  struct dirent ent;
  bool first;
  char pattern[MAX_PATH];
};

static void normalize_slashes(char *s) {
  for (; s && *s; s++) {
    if (*s == '/')
      *s = '\\';
  }
}

DIR *opendir(const char *name) {
  if (!name || !name[0])
    return NULL;

  DIR *d = (DIR *)calloc(1, sizeof(DIR));
  if (!d)
    return NULL;

  strncpy(d->pattern, name, sizeof(d->pattern) - 1);
  d->pattern[sizeof(d->pattern) - 1] = '\0';
  normalize_slashes(d->pattern);

  size_t n = strlen(d->pattern);
  if (n == 0 || (n + 3) >= sizeof(d->pattern)) {
    free(d);
    return NULL;
  }
  if (d->pattern[n - 1] != '\\') {
    d->pattern[n++] = '\\';
    d->pattern[n] = '\0';
  }
  strcat(d->pattern, "*");

  d->handle = FindFirstFileA(d->pattern, &d->data);
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
    WIN32_FIND_DATAA *data = &dirp->data;
    bool ok = true;
    if (dirp->first) {
      dirp->first = false;
    } else {
      ok = FindNextFileA(dirp->handle, data) ? true : false;
    }
    if (!ok)
      return NULL;

    const char *name = data->cFileName;
    if (!name || !name[0])
      continue;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    strncpy(dirp->ent.d_name, name, sizeof(dirp->ent.d_name) - 1);
    dirp->ent.d_name[sizeof(dirp->ent.d_name) - 1] = '\0';
    dirp->ent.d_type = dtype_from_attrs(data->dwFileAttributes);
    return &dirp->ent;
  }
}

void rewinddir(DIR *dirp) {
  if (!dirp || dirp->handle == INVALID_HANDLE_VALUE)
    return;
  FindClose(dirp->handle);
  dirp->handle = FindFirstFileA(dirp->pattern, &dirp->data);
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
