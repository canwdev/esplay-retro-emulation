#include "preview_audio_playlist.h"

#include "file_manager.h"
#include "platform_log.h"
#include "platform_mem.h"

#ifdef TARGET_SIM
#include "sim_compat.h"
#else
#include "esp_random.h"
#endif

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "audio_playlist";

#define PL_LOGE(...) platform_log(PLATFORM_LOG_ERROR, TAG, __VA_ARGS__)

/* ---- internal helpers ---- */

static char *playlist_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  char *d = malloc(len + 1);
  if (d)
    memcpy(d, s, len + 1);
  return d;
}

static uint32_t playlist_random_u32(void) {
#ifdef TARGET_SIM
  return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
#else
  return esp_random();
#endif
}

static int playlist_compare(const void *a, const void *b) {
  return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

static bool playlist_copy_dirname(const char *path, char *out, size_t out_sz) {
  if (!path || !out || out_sz == 0)
    return false;
  const char *slash = strrchr(path, '/');
  if (!slash)
    return false;
  size_t len = (size_t)(slash - path);
  if (len == 0 || len >= out_sz)
    return false;
  memcpy(out, path, len);
  out[len] = '\0';
  return true;
}

/* ---- public API ---- */

void audio_playlist_init(audio_playlist_t *pl) {
  pl->items         = NULL;
  pl->count         = 0;
  pl->current_index = 0;
  pl->cwd[0]        = '\0';
  pl->from_shared   = false;
  pl->shuffle_pos   = 0;
  /* shuffle_order is populated by shuffle_reset; no need to zero here. */
}

void audio_playlist_free(audio_playlist_t *pl) {
  if (pl->items) {
    for (int i = 0; i < pl->count; i++) {
      if (pl->items[i])
        free(pl->items[i]);
    }
    free(pl->items);
    pl->items = NULL;
  }
  pl->count = 0;
}

bool audio_playlist_build(audio_playlist_t *pl, const char *cwd_override,
                          const char *current,
                          char *shared_names, int shared_count,
                          int shared_index, int shared_name_stride) {
  audio_playlist_free(pl);
  pl->current_index = 0;
  pl->shuffle_pos   = 0;

  if (cwd_override)
    strlcpy(pl->cwd, cwd_override, sizeof(pl->cwd));
  else
    playlist_copy_dirname(current, pl->cwd, sizeof(pl->cwd));

  /* ---- shared-names fast path ---- */
  if (shared_names && shared_count > 0 &&
      shared_name_stride == FM_NAME_LEN) {
    pl->items = malloc((size_t)shared_count * sizeof(char *));
    if (!pl->items) {
      PL_LOGE("Failed to allocate memory for playlist from shared");
      platform_free(shared_names);
      return false;
    }
    for (int i = 0; i < shared_count; i++) {
      const char *src = shared_names + (size_t)i * FM_NAME_LEN;
      pl->items[i] = playlist_strdup(src);
    }
    pl->count = shared_count;
    platform_free(shared_names);
    pl->from_shared   = true;
    pl->current_index = shared_index >= 0 ? shared_index : 0;
    if (pl->current_index >= pl->count)
      pl->current_index = 0;
    audio_playlist_shuffle_reset(pl, pl->current_index);
    return true;
  }

  /* ---- directory scan ---- */
  pl->items = malloc(AUDIO_PLAYLIST_MAX * sizeof(char *));
  if (!pl->items) {
    PL_LOGE("Failed to allocate memory for playlist");
    return false;
  }

  DIR *dir = opendir(pl->cwd);
  if (!dir) {
    free(pl->items);
    pl->items = NULL;
    return false;
  }

  struct dirent *de;
  while ((de = readdir(dir)) != NULL && pl->count < AUDIO_PLAYLIST_MAX) {
    if (de->d_type != DT_REG && de->d_type != DT_UNKNOWN)
      continue;
    if (!fm_is_playable_audio_filename(de->d_name))
      continue;
    pl->items[pl->count] = playlist_strdup(de->d_name);
    if (pl->items[pl->count])
      pl->count++;
  }
  closedir(dir);

  if (pl->count > 1)
    qsort(pl->items, pl->count, sizeof(char *), playlist_compare);

  const char *cur = fm_base_name(current);
  for (int i = 0; i < pl->count; i++) {
    if (strcasecmp(pl->items[i], cur) == 0) {
      pl->current_index = i;
      break;
    }
  }

  audio_playlist_shuffle_reset(pl, pl->current_index);
  return true;
}

void audio_playlist_shuffle_reset(audio_playlist_t *pl, int start_index) {
  if (pl->count <= 0)
    return;

  if (start_index < 0 || start_index >= pl->count)
    start_index = 0;

  for (int i = 0; i < pl->count; i++)
    pl->shuffle_order[i] = i;

  if (start_index != 0) {
    int tmp = pl->shuffle_order[0];
    pl->shuffle_order[0] = pl->shuffle_order[start_index];
    pl->shuffle_order[start_index] = tmp;
  }

  for (int i = pl->count - 1; i > 1; i--) {
    uint32_t r = playlist_random_u32();
    int j = 1 + (int)(r % (uint32_t)i);
    int tmp = pl->shuffle_order[i];
    pl->shuffle_order[i] = pl->shuffle_order[j];
    pl->shuffle_order[j] = tmp;
  }

  pl->shuffle_pos = 0;
}

static int shuffle_find_pos(audio_playlist_t *pl, int playlist_index) {
  for (int i = 0; i < pl->count; i++) {
    if (pl->shuffle_order[i] == playlist_index)
      return i;
  }
  return 0;
}

int audio_playlist_shuffle_step(audio_playlist_t *pl, int delta,
                                bool restart_round) {
  if (pl->count <= 1)
    return pl->current_index;

  pl->shuffle_pos = shuffle_find_pos(pl, pl->current_index);

  if (delta > 0) {
    if (restart_round && pl->shuffle_pos >= pl->count - 1) {
      audio_playlist_shuffle_reset(pl, pl->current_index);
      if (pl->count > 1)
        pl->shuffle_pos = 1;
      return pl->shuffle_order[pl->shuffle_pos];
    }
    pl->shuffle_pos++;
    if (pl->shuffle_pos >= pl->count)
      pl->shuffle_pos = 0;
  } else if (delta < 0) {
    pl->shuffle_pos--;
    if (pl->shuffle_pos < 0)
      pl->shuffle_pos = pl->count - 1;
  }

  return pl->shuffle_order[pl->shuffle_pos];
}

bool audio_playlist_build_path(const audio_playlist_t *pl,
                               char *out, size_t out_sz, const char *name) {
  if (!out || out_sz == 0 || !name || name[0] == '\0' || pl->cwd[0] == '\0')
    return false;
  strlcpy(out, pl->cwd, out_sz);
  if (strlcat(out, "/", out_sz) >= out_sz)
    return false;
  if (strlcat(out, name, out_sz) >= out_sz)
    return false;
  return true;
}
