#include "audio_player.h"

#include "event.h"
#include "file_ops.h"
#include "audio.h"
#include "display.h"
#include "esplay-ui.h"
#include "gamepad.h"
#include "power.h"
#include "settings.h"
#include "ugui.h"

#include "acodecs.h"

#include <esp_random.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define MAX_FILENAME 40

static AudioCodec choose_codec(FileType ftype)
{
	switch (ftype) {
	case FileTypeMP3:
		return AudioCodecMP3;
	default:
		return AudioCodecUnknown;
	}
}

typedef struct {
	char *filename;
	char *filepath;
	AudioCodec codec;
} Song;

typedef enum {
	PlayerCmdNone,
	PlayerCmdTerminate,
	PlayerCmdPause,
	PlayerCmdNext,
	PlayerCmdPrev,
	PlayerCmdReinitAudio,
	PlayerCmdToggleLoopMode,
} PlayerCmd;

typedef enum {
	PlayingModeNormal = 0,
	PlayingModeRepeatSong,
	PlayingModeRepeatPlaylist,
	PlayingModeShuffle,
	PlayingModeMax,
} PlayingMode;

static const char *playing_mode_str[PlayingModeMax] = {
		"Normal",
		"Repeat Song",
		"Repeat Playlist",
		"Shuffle",
};

typedef struct PlayerState {
	bool playing;
	Song *playlist;
	size_t playlist_length;
	int playlist_index;
	PlayingMode playing_mode;
} PlayerState;

static PlayerState player_state;
static bool backlight_on = true;
static bool speaker_on = true;

static PlayerCmd player_poll_cmd(void);
static void player_send_cmd(PlayerCmd cmd);
static void player_cmd_ack(void);
static void player_start(void);
static void player_terminate(void);
static void player_teardown_task(void);
static void player_task(void *arg);

static void free_playlist(PlayerState *state);

static bool player_task_running = false;
static QueueHandle_t player_cmd_queue;
static QueueHandle_t player_ack_queue;
static TaskHandle_t audio_player_task_handle;

static PlayerCmd player_poll_cmd(void)
{
	PlayerCmd polled_cmd = PlayerCmdNone;
	(void)xQueueReceive(player_cmd_queue, &polled_cmd, 0);
	return polled_cmd;
}

static void player_send_cmd(PlayerCmd cmd)
{
	(void)xQueueSend(player_cmd_queue, &cmd, 0);
	int tmp;
	(void)xQueueReceive(player_ack_queue, &tmp, pdMS_TO_TICKS(100));
}

static void player_cmd_ack(void)
{
	int tmp = 0;
	(void)xQueueSend(player_ack_queue, &tmp, 0);
}

static void player_start(void)
{
	player_cmd_queue = xQueueCreate(4, sizeof(PlayerCmd));
	player_ack_queue = xQueueCreate(4, sizeof(int));
	if (!player_cmd_queue || !player_ack_queue) {
		printf("player_start: queue alloc failed\n");
		return;
	}

	/* Stack in words; dr_mp3 decode uses noticeable stack depth on ESP32. */
	const int stack_words = 8 * 1024;
	/* Same band as sd_mount (3): avoid starving SDIO/FATFS work when decoding at high priority. */
	BaseType_t ok = xTaskCreate(player_task, "player_task", stack_words, NULL, 3, &audio_player_task_handle);
	if (ok != pdPASS) {
		printf("player_start: task create failed\n");
		vQueueDelete(player_cmd_queue);
		vQueueDelete(player_ack_queue);
		player_cmd_queue = NULL;
		player_ack_queue = NULL;
		return;
	}
}

static void player_terminate(void)
{
	if (player_task_running && player_cmd_queue) {
		PlayerCmd term = PlayerCmdTerminate;
		(void)xQueueSend(player_cmd_queue, &term, portMAX_DELAY);
		while (player_task_running) {
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}
	if (player_cmd_queue) {
		vQueueDelete(player_cmd_queue);
		player_cmd_queue = NULL;
	}
	if (player_ack_queue) {
		vQueueDelete(player_ack_queue);
		player_ack_queue = NULL;
	}
}

static void player_teardown_task(void)
{
	vTaskDelete(NULL);
}

typedef enum {
	PlayerResultDone = 0,
	PlayerResultError,
	PlayerResultNextSong,
	PlayerResultPrevSong,
	PlayerResultStop,
} PlayerResult;

static void push_audio_event(AudioPlayerEvent audio_event)
{
	event_t ev = {.type = EVENT_TYPE_AUDIO_PLAYER, .audio_player.event = audio_event};
	push_event(&ev);
}

static PlayerResult handle_cmd(PlayerState *state, const AudioInfo *info, PlayerCmd received_cmd)
{
	if (received_cmd == PlayerCmdNone) {
		return PlayerResultDone;
	}

	PlayerResult res = PlayerResultDone;
	switch (received_cmd) {
	case PlayerCmdPause:
		state->playing = !state->playing;
		if (state->playing) {
			audio_init((int)info->sample_rate);
			if (speaker_on) {
				audio_amp_enable();
			} else {
				audio_amp_disable();
			}
		} else {
			audio_terminate();
		}
		push_audio_event(AudioPlayerEventStateChanged);
		break;
	case PlayerCmdReinitAudio:
		if (state->playing) {
			audio_terminate();
		}
		audio_init((int)info->sample_rate);
		if (speaker_on) {
			audio_amp_enable();
		} else {
			audio_amp_disable();
		}
		push_audio_event(AudioPlayerEventStateChanged);
		break;
	case PlayerCmdToggleLoopMode:
		state->playing_mode = (PlayingMode)((state->playing_mode + 1) % PlayingModeMax);
		(void)settings_save(SettingPlayingMode, (int32_t)state->playing_mode);
		push_audio_event(AudioPlayerEventStateChanged);
		break;
	case PlayerCmdTerminate:
		res = PlayerResultStop;
		break;
	case PlayerCmdNext:
		res = PlayerResultNextSong;
		break;
	case PlayerCmdPrev:
		res = PlayerResultPrevSong;
		break;
	default:
		break;
	}
	player_cmd_ack();
	return res;
}

static PlayerResult play_song(const Song *song)
{
	PlayerState *state = &player_state;
	AudioInfo info;
	void *acodec = NULL;

	AudioDecoder *decoder = acodec_get_decoder(song->codec);
	if (!decoder) {
		printf("play_song: no decoder\n");
		return PlayerResultError;
	}

	if (decoder->open(&acodec, song->filepath) != 0) {
		printf("play_song: open failed %s\n", song->filepath);
		return PlayerResultError;
	}
	if (decoder->get_info(acodec, &info) != 0) {
		decoder->close(acodec);
		return PlayerResultError;
	}

	size_t samples_needed = info.buf_size * info.channels;
	int16_t *audio_buf = calloc(samples_needed, sizeof(int16_t));
	if (!audio_buf) {
		decoder->close(acodec);
		return PlayerResultError;
	}

	audio_init((int)info.sample_rate);
	if (speaker_on) {
		audio_amp_enable();
		/* Let VBAT / SD rail settle after enabling the amp (reduces SDIO CRC errors on ESPlay). */
		vTaskDelay(pdMS_TO_TICKS(40));
	} else {
		audio_amp_disable();
	}

	int n_frames = 0;
	state->playing = true;
	push_audio_event(AudioPlayerEventStateChanged);

	PlayerResult result = PlayerResultDone;

	do {
		result = handle_cmd(state, &info, player_poll_cmd());
		if (result != PlayerResultDone) {
			break;
		}

		if (state->playing) {
			n_frames = decoder->decode(acodec, audio_buf, (int)info.channels, info.buf_size);
			if (n_frames > 0) {
				audio_submit(audio_buf, n_frames);
			} else if (n_frames < 0) {
				result = PlayerResultError;
				break;
			}
			if (n_frames > 0) {
				vTaskDelay(1);
			}
		} else {
			usleep(10 * 1000);
		}
	} while (n_frames > 0);

	decoder->close(acodec);
	free(audio_buf);

	if (state->playing) {
		audio_terminate();
	}
	return result;
}

static int random_other_index(int current, int n)
{
	if (n <= 1) {
		return 0;
	}
	int r = current;
	for (int guard = 0; guard < 32 && r == current; guard++) {
		r = (int)(esp_random() % (uint32_t)n);
	}
	return r;
}

static void player_task(void *arg)
{
	(void)arg;
	player_task_running = true;
	player_state.playing = false;
	PlayerState *state = &player_state;

	for (;;) {
		int song_index = state->playlist_index;
		Song *song = &state->playlist[song_index];
		PlayerResult res = play_song(song);

		if (res == PlayerResultDone || res == PlayerResultNextSong) {
			if (state->playing_mode == PlayingModeNormal && song_index == (int)(state->playlist_length - 1)) {
				push_audio_event(AudioPlayerEventDone);
				break;
			}

			if (res == PlayerResultNextSong) {
				if (state->playing_mode == PlayingModeShuffle) {
					song_index = random_other_index(song_index, (int)state->playlist_length);
				} else if (state->playing_mode != PlayingModeRepeatSong) {
					song_index = (song_index + 1) % (int)state->playlist_length;
				}
			}

			state->playlist_index = song_index;
			push_audio_event(AudioPlayerEventStateChanged);
		} else if (res == PlayerResultPrevSong) {
			if (--song_index < 0) {
				song_index = (int)state->playlist_length - 1;
			}
			state->playlist_index = song_index;
			push_audio_event(AudioPlayerEventStateChanged);
		} else if (res == PlayerResultStop) {
			push_audio_event(AudioPlayerEventDone);
			break;
		} else if (res == PlayerResultError) {
			push_audio_event(AudioPlayerEventError);
			break;
		}
	}

	player_task_running = false;
	player_teardown_task();
}

static void draw_player(const PlayerState *state)
{
	ui_clear_screen();

	UG_FontSelect(&FONT_8X12);
	UG_SetForecolor(C_WHITE);
	UG_SetBackcolor(C_BLACK);
	UG_PutString(80, 4, "Music Player");

	int32_t volume = 20;
	(void)settings_load(SettingAudioVolume, &volume);
	char vol_str[8];
	snprintf(vol_str, sizeof(vol_str), "%ld", (long)volume);
	if (volume == 0) {
		UG_SetForecolor(C_RED);
	}
	UG_PutString(280, 4, vol_str);
	UG_SetForecolor(C_WHITE);

	battery_state bat = {0};
	battery_level_read(&bat);
	char bat_str[24];
	snprintf(bat_str, sizeof(bat_str), "Bat %d%%", bat.percentage);
	UG_PutString(200, 4, bat_str);

	const int line_h = 14;
	int y = 28;

	Song *song = &state->playlist[state->playlist_index];
	char trunc[MAX_FILENAME];
	strncpy(trunc, song->filename, sizeof(trunc) - 1);
	trunc[sizeof(trunc) - 1] = '\0';

	char line[320];
	snprintf(line, sizeof(line), "%s", trunc);
	UG_PutString(4, y, line);
	y += line_h + 8;

	UG_FontSelect(&FONT_6X8);
	snprintf(line, sizeof(line), "Mode: %s", playing_mode_str[state->playing_mode]);
	UG_PutString(4, y, line);
	y += line_h;
	snprintf(line, sizeof(line), "Volume: %d%%", audio_volume_get());
	UG_PutString(4, y, line);
	y += line_h + 6;

	UG_SetForecolor(state->playing ? C_GREEN : C_YELLOW);
	snprintf(line, sizeof(line), "%s", state->playing ? "Playing" : "Paused");
	UG_PutString(4, y, line);
	UG_SetForecolor(C_WHITE);
	y += line_h + 10;

	UG_FontSelect(&FONT_6X8);
	UG_PutString(4, y, "A pause/start   B exit");
	y += line_h;
	UG_PutString(4, y, "< prev    next >");
	y += line_h;
	UG_PutString(4, y, "^ vol+    v vol-");
	y += line_h;
	UG_PutString(4, y, "START cycle mode  MENU speaker");
	y += line_h;
	UG_PutString(4, y, "SELECT backlight");

	ui_flush();
}

static void handle_keypress(input_gamepad_state cur, input_gamepad_state prev, bool *quit)
{
	if (!prev.values[GAMEPAD_INPUT_A] && cur.values[GAMEPAD_INPUT_A]) {
		player_send_cmd(PlayerCmdPause);
	}
	if (!prev.values[GAMEPAD_INPUT_B] && cur.values[GAMEPAD_INPUT_B]) {
		*quit = true;
	}
	if (!prev.values[GAMEPAD_INPUT_UP] && cur.values[GAMEPAD_INPUT_UP]) {
		int prev_vol = audio_volume_get();
		int vol = prev_vol + 1;
		if (vol > 100) {
			vol = 100;
		}
		audio_volume_set(vol);
		if (prev_vol == 0 && vol > 0 && player_state.playing) {
			player_send_cmd(PlayerCmdReinitAudio);
		}
		(void)settings_save(SettingAudioVolume, (int32_t)vol);
		draw_player(&player_state);
	}
	if (!prev.values[GAMEPAD_INPUT_DOWN] && cur.values[GAMEPAD_INPUT_DOWN]) {
		int vol = audio_volume_get() - 1;
		if (vol < 0) {
			vol = 0;
		}
		audio_volume_set(vol);
		if (vol == 0) {
			audio_terminate();
		}
		(void)settings_save(SettingAudioVolume, (int32_t)vol);
		draw_player(&player_state);
	}
	if (!prev.values[GAMEPAD_INPUT_RIGHT] && cur.values[GAMEPAD_INPUT_RIGHT]) {
		player_send_cmd(PlayerCmdNext);
	}
	if (!prev.values[GAMEPAD_INPUT_LEFT] && cur.values[GAMEPAD_INPUT_LEFT]) {
		player_send_cmd(PlayerCmdPrev);
	}
	if (!prev.values[GAMEPAD_INPUT_START] && cur.values[GAMEPAD_INPUT_START]) {
		player_send_cmd(PlayerCmdToggleLoopMode);
	}
	if (!prev.values[GAMEPAD_INPUT_SELECT] && cur.values[GAMEPAD_INPUT_SELECT]) {
		set_display_brightness(backlight_on ? 0 : 50);
		backlight_on = !backlight_on;
	}
	if (!prev.values[GAMEPAD_INPUT_MENU] && cur.values[GAMEPAD_INPUT_MENU]) {
		speaker_on = !speaker_on;
		if (speaker_on) {
			audio_amp_enable();
		} else {
			audio_amp_disable();
		}
		draw_player(&player_state);
	}
}

#define MAX_SONGS 512

static int make_playlist(PlayerState *state, const AudioPlayerParam params)
{
	char pathbuf[PATH_MAX];

	if (params.play_all) {
		size_t song_indices[MAX_SONGS];
		size_t n_songs = 0;
		size_t start_song = 0;

		for (size_t i = 0; i < (size_t)params.n_entries && n_songs < MAX_SONGS; i++) {
			Entry *entry = &params.entries[i];
			AudioCodec codec = choose_codec(fops_determine_filetype(entry));
			if (codec != AudioCodecUnknown) {
				if ((size_t)params.index == i) {
					start_song = n_songs;
				}
				song_indices[n_songs++] = i;
			}
		}

		state->playlist = calloc(n_songs, sizeof(Song));
		if (!state->playlist) {
			return -1;
		}
		state->playlist_length = n_songs;

		for (size_t i = 0; i < n_songs; i++) {
			Entry *entry = &params.entries[song_indices[i]];
			state->playlist[i].codec = choose_codec(fops_determine_filetype(entry));
			int printed = snprintf(pathbuf, PATH_MAX, "%s/%s", params.cwd, entry->name);
			if (printed < 0 || printed >= PATH_MAX) {
				free_playlist(state);
				return -1;
			}
			state->playlist[i].filename = strdup(entry->name);
			state->playlist[i].filepath = strdup(pathbuf);
			if (!state->playlist[i].filename || !state->playlist[i].filepath) {
				free_playlist(state);
				return -1;
			}
		}
		state->playlist_index = (int)start_song;
	} else {
		Entry *entry = &params.entries[params.index];
		AudioCodec codec = choose_codec(fops_determine_filetype(entry));
		if (codec == AudioCodecUnknown) {
			return -1;
		}

		state->playlist = calloc(1, sizeof(Song));
		if (!state->playlist) {
			return -1;
		}
		state->playlist_length = 1;
		state->playlist_index = 0;
		state->playlist[0].codec = codec;
		int printed = snprintf(pathbuf, PATH_MAX, "%s/%s", params.cwd, entry->name);
		if (printed < 0 || printed >= PATH_MAX) {
			free(state->playlist);
			state->playlist = NULL;
			return -1;
		}
		state->playlist[0].filepath = strdup(pathbuf);
		state->playlist[0].filename = strdup(entry->name);
		if (!state->playlist[0].filepath || !state->playlist[0].filename) {
			free(state->playlist[0].filepath);
			free(state->playlist[0].filename);
			free(state->playlist);
			state->playlist = NULL;
			return -1;
		}
	}

	return 0;
}

static void free_playlist(PlayerState *state)
{
	if (!state->playlist || state->playlist_length < 1) {
		return;
	}
	for (size_t i = 0; i < state->playlist_length; i++) {
		free(state->playlist[i].filepath);
		free(state->playlist[i].filename);
	}
	free(state->playlist);
	state->playlist = NULL;
	state->playlist_length = 0;
}

static void load_settings(PlayerState *state)
{
	int32_t mode = 0;
	if (settings_load(SettingPlayingMode, &mode) == 0 && mode >= 0 && mode < PlayingModeMax) {
		state->playing_mode = (PlayingMode)mode;
	} else {
		state->playing_mode = PlayingModeNormal;
	}
}

int audio_player(AudioPlayerParam params)
{
	if (params.n_entries < 1 || !params.entries || !params.cwd) {
		printf("audio_player: invalid params\n");
		return -1;
	}

	event_init();
	memset(&player_state, 0, sizeof(PlayerState));
	load_settings(&player_state);

	if (make_playlist(&player_state, params) != 0) {
		printf("audio_player: unsupported or empty playlist\n");
		event_deinit();
		return -1;
	}

	draw_player(&player_state);
	player_start();

	bool quit = false;
	event_t event;

	while (!quit) {
		if (wait_event(&event) < 0) {
			continue;
		}
		switch (event.type) {
		case EVENT_TYPE_KEYPAD:
			handle_keypress(event.keypad.state, event.keypad.last_state, &quit);
			break;
		case EVENT_TYPE_AUDIO_PLAYER:
			if (event.audio_player.event == AudioPlayerEventDone) {
				quit = true;
			} else if (event.audio_player.event == AudioPlayerEventError) {
				printf("audio_player: playback error\n");
				quit = true;
			} else {
				draw_player(&player_state);
			}
			break;
		case EVENT_TYPE_QUIT:
			quit = true;
			break;
		case EVENT_TYPE_UPDATE:
			ui_flush();
			break;
		default:
			break;
		}
	}

	player_terminate();
	free_playlist(&player_state);
	event_deinit();

	int32_t bright = 50;
	(void)settings_load(SettingBacklight, &bright);
	set_display_brightness((int)bright);

	return 0;
}
