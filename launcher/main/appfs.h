#ifndef APPFS_H
#define APPFS_H

#include <stdbool.h>
#include <stddef.h>

bool appfs_init(void);
void *appfs_load_file(const char *path, size_t *out_size);
void appfs_release(void);

#endif
