#ifndef PREVIEW_AUDIO_PLAYLIST_H
#define PREVIEW_AUDIO_PLAYLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_PLAYLIST_MAX 512
#define AUDIO_PATH_MAX     256

typedef struct {
  char **items;
  int    count;
  int    current_index;
  char   cwd[AUDIO_PATH_MAX];
  bool   from_shared;
  int    shuffle_order[AUDIO_PLAYLIST_MAX];
  int    shuffle_pos;
} audio_playlist_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise an audio_playlist_t to a safe empty state. */
void audio_playlist_init(audio_playlist_t *pl);

/** Free all heap-allocated playlist entries and reset to empty. */
void audio_playlist_free(audio_playlist_t *pl);

/**
 * Build the playlist from either shared file names or a directory scan.
 *
 * @param pl              Playlist to populate.
 * @param cwd_override    If non-NULL, used as the playlist directory;
 *                        otherwise the directory part of @a current is used.
 * @param current         Path of the currently selected file (used to find
 *                        the initial current_index and, if no cwd_override,
 *                        the directory).
 * @param shared_names    If non-NULL, pre-sorted array of file-name strings
 *                        (stride = FM_NAME_LEN). Ownership is transferred to
 *                        this function (it will free the buffer).
 * @param shared_count    Number of entries in shared_names.
 * @param shared_index    Index of the current file in shared_names.
 * @param shared_name_stride  Stride in bytes between consecutive names.
 * @return true on success.
 */
bool audio_playlist_build(audio_playlist_t *pl, const char *cwd_override,
                          const char *current,
                          char *shared_names, int shared_count,
                          int shared_index, int shared_name_stride);

/** Reset the Fisher-Yates shuffle order, keeping @a start_index first. */
void audio_playlist_shuffle_reset(audio_playlist_t *pl, int start_index);

/**
 * Step the shuffle position by @a delta (+1 or -1) and return the
 * playlist index of the next track.
 *
 * @param restart_round  When true and stepping forward past the end,
 *                       reshuffle and start a new round.
 */
int audio_playlist_shuffle_step(audio_playlist_t *pl, int delta,
                                bool restart_round);

/**
 * Build a full path: cwd + "/" + name -> @a out.
 * @return true on success.
 */
bool audio_playlist_build_path(const audio_playlist_t *pl,
                               char *out, size_t out_sz, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PREVIEW_AUDIO_PLAYLIST_H */
