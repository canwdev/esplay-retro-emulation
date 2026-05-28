#include "hal_storage.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

static char s_root[512];
static bool s_inited;

static void normalize_to_slash(char *s) {
  for (; s && *s; s++) {
    if (*s == '\\')
      *s = '/';
  }
}

static void ensure_root(void) {
  if (s_inited)
    return;

#ifdef _WIN32
  DWORD n = GetCurrentDirectoryA(sizeof(s_root), s_root);
  if (n == 0 || n >= sizeof(s_root)) {
    strncpy(s_root, ".", sizeof(s_root) - 1);
    s_root[sizeof(s_root) - 1] = '\0';
  }
  normalize_to_slash(s_root);
  size_t len = strlen(s_root);
  if (len > 0 && s_root[len - 1] != '/') {
    if (len + 1 < sizeof(s_root)) {
      s_root[len++] = '/';
      s_root[len] = '\0';
    }
  }
  strncat(s_root, "test_sd", sizeof(s_root) - strlen(s_root) - 1);
#else
  if (!getcwd(s_root, sizeof(s_root))) {
    strncpy(s_root, ".", sizeof(s_root) - 1);
    s_root[sizeof(s_root) - 1] = '\0';
  }
  size_t len = strlen(s_root);
  if (len > 0 && s_root[len - 1] != '/') {
    if (len + 1 < sizeof(s_root)) {
      s_root[len++] = '/';
      s_root[len] = '\0';
    }
  }
  strncat(s_root, "test_sd", sizeof(s_root) - strlen(s_root) - 1);
#endif

  size_t end = strlen(s_root);
  while (end > 1 && s_root[end - 1] == '/') {
    s_root[end - 1] = '\0';
    end--;
  }

  s_inited = true;
}

const char *hal_storage_root(void) {
  ensure_root();
  return s_root;
}

bool hal_storage_mount(void) {
  ensure_root();
#ifdef _WIN32
  DWORD attrs = GetFileAttributesA(s_root);
  return (attrs != INVALID_FILE_ATTRIBUTES) &&
         ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
  struct stat st;
  return stat(s_root, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

void hal_storage_get_free_kb(uint32_t *total_kb, uint32_t *free_kb) {
  ensure_root();
  if (total_kb)
    *total_kb = 0;
  if (free_kb)
    *free_kb = 0;

#ifdef _WIN32
  ULARGE_INTEGER free_bytes = {0}, total_bytes = {0};
  char path[512];
  strncpy(path, s_root, sizeof(path) - 1);
  path[sizeof(path) - 1] = '\0';
  for (char *p = path; *p; p++) {
    if (*p == '/')
      *p = '\\';
  }
  if (GetDiskFreeSpaceExA(path, &free_bytes, &total_bytes, NULL)) {
    if (total_kb)
      *total_kb = (uint32_t)(total_bytes.QuadPart / 1024ULL);
    if (free_kb)
      *free_kb = (uint32_t)(free_bytes.QuadPart / 1024ULL);
  }
#else
  struct statvfs vfs;
  if (statvfs(s_root, &vfs) == 0) {
    unsigned long long total = (unsigned long long)vfs.f_blocks *
                               (unsigned long long)vfs.f_frsize;
    unsigned long long freeb = (unsigned long long)vfs.f_bavail *
                               (unsigned long long)vfs.f_frsize;
    if (total_kb)
      *total_kb = (uint32_t)(total / 1024ULL);
    if (free_kb)
      *free_kb = (uint32_t)(freeb / 1024ULL);
  }
#endif
}
