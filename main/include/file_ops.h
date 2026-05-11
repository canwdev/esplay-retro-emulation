#pragma once

#include <stddef.h>

typedef enum {
	FileTypeUnknown = 0,
	FileTypeMP3,
} FileType;

typedef struct {
	char name[256];
} Entry;

FileType fops_determine_filetype(const Entry *entry);
