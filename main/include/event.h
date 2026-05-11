#pragma once

#include <stdbool.h>
#include "gamepad.h"

typedef enum {
	EVENT_TYPE_KEYPAD = 0,
	EVENT_TYPE_AUDIO_PLAYER,
	EVENT_TYPE_QUIT,
	EVENT_TYPE_UPDATE,
} event_type_t;

typedef enum {
	AudioPlayerEventStateChanged = 0,
	AudioPlayerEventDone,
	AudioPlayerEventError,
} AudioPlayerEvent;

typedef struct {
	event_type_t type;
	union {
		struct {
			input_gamepad_state state;
			input_gamepad_state last_state;
		} keypad;
		struct {
			AudioPlayerEvent event;
		} audio_player;
	};
} event_t;

void event_init(void);
void event_deinit(void);
void push_event(const event_t *ev);
int wait_event(event_t *out);
