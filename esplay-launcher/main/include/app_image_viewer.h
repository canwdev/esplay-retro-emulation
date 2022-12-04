#pragma once

#include <file_ops.h>

typedef struct ImageViewerParams {
	Entry *entries;
	int n_entries;
	int index;
	const char *cwd;
} ImageViewerParams;

/// Start the image viewer at the file entry at index.
int app_image_viewer(ImageViewerParams param);
