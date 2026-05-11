#pragma once

#include <stdbool.h>

#include "file_ops.h"

typedef struct {
	Entry *entries;
	int n_entries;
	int index;
	const char *cwd;
	bool play_all;
} AudioPlayerParam;

int audio_player(AudioPlayerParam params);
