#include "file_ops.h"

#include <strings.h>
#include <string.h>

FileType fops_determine_filetype(const Entry *entry)
{
	if (!entry || entry->name[0] == '\0') {
		return FileTypeUnknown;
	}
	const char *dot = strrchr(entry->name, '.');
	if (!dot) {
		return FileTypeUnknown;
	}
	if (strcasecmp(dot, ".mp3") == 0) {
		return FileTypeMP3;
	}
	return FileTypeUnknown;
}
