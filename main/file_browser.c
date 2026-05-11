#include "file_browser.h"

#include <stdbool.h>
#include <dirent.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_player.h"
#include "esplay-ui.h"
#include "file_ops.h"
#include "gamepad.h"
#include "ugui.h"

#define MAX_CHR (320 / 9)
#define MAX_ITEM (193 / 15)
#define FM_MAX_ENTRIES 384

typedef struct {
    char *name;
    bool is_dir;
} fm_entry_t;

static void join_path(char *out, size_t out_sz, const char *dir, const char *name)
{
    size_t ld = strlen(dir);
    if (ld > 0 && dir[ld - 1] == '/') {
        snprintf(out, out_sz, "%s%s", dir, name);
    } else {
        snprintf(out, out_sz, "%s/%s", dir, name);
    }
}

static int fm_entry_cmp(const void *a, const void *b)
{
    const fm_entry_t *ea = (const fm_entry_t *)a;
    const fm_entry_t *eb = (const fm_entry_t *)b;
    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }
    return strcasecmp(ea->name, eb->name);
}

static void fm_free_entries(fm_entry_t *e, int n)
{
    if (!e) {
        return;
    }
    for (int i = 0; i < n; i++) {
        free(e[i].name);
    }
    free(e);
}

static int fm_scan(const char *path, fm_entry_t **out, int *out_n)
{
    *out = NULL;
    *out_n = 0;

    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }

    fm_entry_t *list = calloc(FM_MAX_ENTRIES, sizeof(fm_entry_t));
    if (!list) {
        closedir(d);
        return -1;
    }

    int n = 0;
    struct dirent *de;
    char full[512];
    struct stat st;

    while ((de = readdir(d)) != NULL && n < FM_MAX_ENTRIES) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
            continue;
        }

        join_path(full, sizeof(full), path, de->d_name);
        if (stat(full, &st) != 0) {
            continue;
        }

        list[n].name = strdup(de->d_name);
        if (!list[n].name) {
            fm_free_entries(list, n);
            closedir(d);
            return -1;
        }
        list[n].is_dir = S_ISDIR(st.st_mode);
        n++;
    }
    closedir(d);

    qsort(list, (size_t)n, sizeof(fm_entry_t), fm_entry_cmp);
    *out = list;
    *out_n = n;
    return 0;
}

static void fm_draw_footer_line(const fm_entry_t *e, const char *full_path)
{
    char line[MAX_CHR];
    if (!e || !e->name) {
        snprintf(line, sizeof(line), "---");
    } else if (e->is_dir) {
        snprintf(line, sizeof(line), "<DIR>");
    } else {
        struct stat st;
        if (stat(full_path, &st) == 0) {
            snprintf(line, sizeof(line), "%llu B", (unsigned long long)st.st_size);
        } else {
            snprintf(line, sizeof(line), "?");
        }
    }

    UG_FillFrame(0, 240 - 16 - 20, 319, 240 - 17, C_BLACK);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLACK);
    char trunc[MAX_CHR];
    strncpy(trunc, line, MAX_CHR - 1);
    trunc[MAX_CHR - 1] = '\0';
    UG_PutString(4, 240 - 16 - 18, trunc);
}

static void fm_draw_title(const char *title_path)
{
    char buf[40];
    size_t len = strlen(title_path);
    const char *show = title_path;
    if (len > sizeof(buf) - 5) {
        snprintf(buf, sizeof(buf), "...%s", title_path + len - (sizeof(buf) - 5));
        show = buf;
    }
    UG_PutString((320 / 2) - (int)(strlen(show) * 9 / 2), 2, show);
}

static void fm_draw_ui(const char *title_path, fm_entry_t *entries, int n, int selected)
{
    UG_FillFrame(0, 0, 319, 15, C_BLUE);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLUE);
    fm_draw_title(title_path);

    UG_FillFrame(0, 240 - 16, 319, 239, C_BLUE);
    const char *msg = " A open/mp3   B up   < > page ";
    UG_PutString((320 / 2) - (int)(strlen(msg) * 9 / 2), 240 - 15, msg);

    const int inner_top = 17;
    const int inner_bottom = 240 - 16 - 22;
    const int inner_h = inner_bottom - inner_top + 1;
    const int item_h = inner_h / MAX_ITEM;

    UG_FillFrame(0, inner_top, 319, inner_bottom, C_BLACK);

    if (n < 1) {
        UG_SetForecolor(C_RED);
        UG_SetBackcolor(C_BLACK);
        msg = "(empty)";
        UG_PutString((320 / 2) - (int)(strlen(msg) * 9 / 2), inner_top + inner_h / 2, msg);
        ui_flush();
        return;
    }

    int page = (selected / MAX_ITEM) * MAX_ITEM;

    for (int line = 0; line < MAX_ITEM; line++) {
        int idx = page + line;
        if (idx >= n) {
            break;
        }
        int top = inner_top + line * item_h;
        bool sel = (idx == selected);
        if (sel) {
            UG_FillFrame(0, top - 1, 319, top + 12, C_YELLOW);
            UG_SetForecolor(C_BLACK);
            UG_SetBackcolor(C_YELLOW);
        } else {
            UG_SetForecolor(C_WHITE);
            UG_SetBackcolor(C_BLACK);
        }
        char row[MAX_CHR + 8];
        snprintf(row, sizeof(row), "%s%s", entries[idx].is_dir ? "[D] " : "    ", entries[idx].name);
        char trunc[MAX_CHR];
        strncpy(trunc, row, MAX_CHR - 1);
        trunc[MAX_CHR - 1] = '\0';
        UG_PutString(4, top, trunc);
    }

    char sel_full[512];
    if (selected >= 0 && selected < n) {
        join_path(sel_full, sizeof(sel_full), title_path, entries[selected].name);
        fm_draw_footer_line(&entries[selected], sel_full);
    } else {
        fm_draw_footer_line(NULL, "");
    }

    ui_flush();
}

static void fm_go_parent(char *path, const char *root)
{
    if (strcmp(path, root) == 0) {
        return;
    }
    char *last = strrchr(path, '/');
    if (!last || last == path) {
        return;
    }
    *last = '\0';
    if (strlen(path) < strlen(root)) {
        strcpy(path, root);
    }
}

static void fm_show_msg(const char *line1, const char *line2)
{
    UG_FillFrame(20, 80, 300, 160, C_BLACK);
    UG_DrawFrame(20, 80, 300, 160, C_WHITE);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLACK);
    UG_PutString(40, 100, line1);
    if (line2) {
        UG_PutString(40, 118, line2);
    }
    UG_PutString(40, 142, "[B] close");
    ui_flush();

    input_gamepad_state prev, cur;
    gamepad_read(&prev);
    for (;;) {
        gamepad_read(&cur);
        if (!prev.values[GAMEPAD_INPUT_B] && cur.values[GAMEPAD_INPUT_B]) {
            break;
        }
        prev = cur;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void file_browser_run(const char *root_path)
{
    char cwd[512];
    strncpy(cwd, root_path, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';

    fm_entry_t *entries = NULL;
    int n = 0;

    if (fm_scan(cwd, &entries, &n) != 0) {
        fm_show_msg("Cannot open", cwd);
        return;
    }

    int selected = 0;
    fm_draw_ui(cwd, entries, n, selected);

    input_gamepad_state prev, cur;
    gamepad_read(&prev);

    for (;;) {
        gamepad_read(&cur);

        int page = (selected / MAX_ITEM) * MAX_ITEM;

        if (!prev.values[GAMEPAD_INPUT_DOWN] && cur.values[GAMEPAD_INPUT_DOWN]) {
            if (n > 0) {
                selected++;
                if (selected >= n) {
                    selected = 0;
                }
                fm_draw_ui(cwd, entries, n, selected);
            }
        } else if (!prev.values[GAMEPAD_INPUT_UP] && cur.values[GAMEPAD_INPUT_UP]) {
            if (n > 0) {
                selected--;
                if (selected < 0) {
                    selected = n - 1;
                }
                fm_draw_ui(cwd, entries, n, selected);
            }
        } else if (!prev.values[GAMEPAD_INPUT_RIGHT] && cur.values[GAMEPAD_INPUT_RIGHT]) {
            if (n > 0) {
                if (page + MAX_ITEM < n) {
                    selected = page + MAX_ITEM;
                } else {
                    selected = 0;
                }
                fm_draw_ui(cwd, entries, n, selected);
            }
        } else if (!prev.values[GAMEPAD_INPUT_LEFT] && cur.values[GAMEPAD_INPUT_LEFT]) {
            if (n > 0) {
                if (page >= MAX_ITEM) {
                    selected = page - MAX_ITEM;
                } else {
                    selected = page;
                    while (selected + MAX_ITEM < n) {
                        selected += MAX_ITEM;
                    }
                }
                fm_draw_ui(cwd, entries, n, selected);
            }
        } else if (!prev.values[GAMEPAD_INPUT_B] && cur.values[GAMEPAD_INPUT_B]) {
            fm_go_parent(cwd, root_path);
            fm_free_entries(entries, n);
            entries = NULL;
            if (fm_scan(cwd, &entries, &n) != 0) {
                strncpy(cwd, root_path, sizeof(cwd) - 1);
                cwd[sizeof(cwd) - 1] = '\0';
                if (fm_scan(cwd, &entries, &n) != 0) {
                    fm_show_msg("Read error", cwd);
                    fm_free_entries(entries, n);
                    return;
                }
            }
            selected = 0;
            fm_draw_ui(cwd, entries, n, selected);
        } else if (!prev.values[GAMEPAD_INPUT_A] && cur.values[GAMEPAD_INPUT_A]) {
            if (n < 1) {
                prev = cur;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            char full[512];
            join_path(full, sizeof(full), cwd, entries[selected].name);
            if (entries[selected].is_dir) {
                strncpy(cwd, full, sizeof(cwd) - 1);
                cwd[sizeof(cwd) - 1] = '\0';
                fm_free_entries(entries, n);
                entries = NULL;
                if (fm_scan(cwd, &entries, &n) != 0) {
                    fm_show_msg("Cannot open dir", cwd);
                    fm_go_parent(cwd, root_path);
                    if (fm_scan(cwd, &entries, &n) != 0) {
                        strncpy(cwd, root_path, sizeof(cwd) - 1);
                        cwd[sizeof(cwd) - 1] = '\0';
                        fm_scan(cwd, &entries, &n);
                    }
                }
                selected = 0;
                fm_draw_ui(cwd, entries, n, selected);
            } else {
                Entry probe;
                strncpy(probe.name, entries[selected].name, sizeof(probe.name) - 1);
                probe.name[sizeof(probe.name) - 1] = '\0';

                if (fops_determine_filetype(&probe) == FileTypeMP3) {
                    Entry *plist = calloc((size_t)n, sizeof(Entry));
                    if (!plist) {
                        fm_show_msg("Out of memory", NULL);
                    } else {
                        for (int i = 0; i < n; i++) {
                            strncpy(plist[i].name, entries[i].name, sizeof(plist[i].name) - 1);
                            plist[i].name[sizeof(plist[i].name) - 1] = '\0';
                        }
                        AudioPlayerParam ap = {
                                .entries = plist,
                                .n_entries = n,
                                .index = selected,
                                .cwd = cwd,
                                .play_all = true,
                        };
                        audio_player(ap);
                        free(plist);
                        fm_draw_ui(cwd, entries, n, selected);
                    }
                } else {
                    struct stat st;
                    char sz[48];
                    if (stat(full, &st) == 0) {
                        snprintf(sz, sizeof(sz), "%llu bytes", (unsigned long long)st.st_size);
                    } else {
                        strcpy(sz, "?");
                    }
                    fm_show_msg(entries[selected].name, sz);
                    fm_draw_ui(cwd, entries, n, selected);
                }
            }
        }

        prev = cur;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
