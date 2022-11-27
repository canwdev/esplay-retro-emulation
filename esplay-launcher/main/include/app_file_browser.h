#pragma once

typedef struct FileBrowserParam {
	const char* cwd;
} FileBrowserParam;

/** Launch file browser */
int app_file_browser(FileBrowserParam params);
