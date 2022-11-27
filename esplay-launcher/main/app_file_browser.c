#include <limits.h> /* PATH_MAX */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "event.h"
#include "file_ops.h"

#include "app_file_browser.h"
#include "display.h"
#include "ugui.h"
#include "esplay-ui.h"

/* Global state. */
static struct FileBrowser
{
    char cwd[PATH_MAX];
    struct Entry *cwd_entries;
    int n_entries; // number of cwd_entries
    int selection; // which of n_entries is selected
    int scroll;    // entry from which the list gets displayed on
    bool stat_enabled;
} browser;

#define STACKSIZE 128

/** Number of entries that can be displayed at once. */
static const int MAX_WIN_ENTRIES = 13;

static int selection_stack[128];
static int scroll_stack[128];
static int stack_top = 0;

/// Push/Save selection and scroll to stack
static void stack_push(void)
{
    selection_stack[stack_top] = browser.selection;
    scroll_stack[stack_top] = browser.scroll;
    if (++stack_top > STACKSIZE)
    {
        stack_top = STACKSIZE;
    }
    // printf("pushed: sel:%d scroll:%d top:%d\n", browser.selection, browser.scroll, stack_top);
}

/// Pop/Load selection and scroll to stack
static void stack_pop(void)
{
    if (--stack_top < 0)
    {
        stack_top = 0;
    }
    browser.selection = selection_stack[stack_top];
    browser.scroll = scroll_stack[stack_top];
    // printf("popped: sel:%d scroll:%d top:%d\n", browser.selection, browser.scroll, stack_top);
}

// Print human readable representation of file size into dst
int sprint_human_size(char *dst, size_t strsize, off_t size)
{
    char *suffix, *suffixes = "BKMGTPEZY";
    off_t human_size = size * 10;
    for (suffix = suffixes; human_size >= 10240; suffix++)
        human_size = (human_size + 512) / 1024;
    return snprintf(dst, strsize, "%d.%d %c", (int)human_size / 10, (int)human_size % 10, *suffix);
}

static void ui_draw_browser(void)
{
    ui_clear_screen();
    UG_SetForecolor(FG_COLOR_1);
    UG_SetBackcolor(BG_COLOR);

    /* START Path Bar */
    char selection_str[16];
    char *right = NULL;
    if (browser.n_entries > 0)
    {
        snprintf(selection_str, 16, "%d/%d", browser.selection + 1, browser.n_entries);
        right = selection_str;
    }
    UG_PutString(5, 5, browser.cwd);
    ui_draw_y_right_string(5, right);
    /* END Path Bar */

    UG_SetForecolor(FG_COLOR_2);
    // Show message if directory is empty
    if (browser.n_entries == 0)
    {
        browser.selection = 0;
        ui_draw_x_center_string(100, "This directory is empty");
        return;
    }

    // TODO: Display icon image

    // Draw entries
    int top = 20;
    for (int i = browser.scroll; i < browser.scroll + MAX_WIN_ENTRIES; i++)
    {
        if (i > browser.n_entries - 1)
        {
            continue;
        }

        if (i == browser.selection)
        {
            UG_FillFrame(0, top, SCREEN_WIDTH, top + 12, C_DARK_CYAN);
            UG_SetBackcolor(C_DARK_CYAN);
            UG_SetForecolor(C_WHITE);
        }
        else
        {
            UG_SetBackcolor(BG_COLOR);
            UG_SetForecolor(FG_COLOR_2);
        }

        // Draw filename
        const Entry *entry = &browser.cwd_entries[i];
        char fname_buf[41];
        char *filename;
        if (strlen(entry->name) > 40)
        {
            fruncate_str(fname_buf, entry->name, 40);
            filename = fname_buf;
        }
        else
        {
            filename = entry->name;
        }
        UG_PutString(5, top, filename);

        UG_SetForecolor(FG_COLOR_1);
        // Draw file size
        char filesize_buf[32];
        sprint_human_size(filesize_buf, 32, entry->size);
        ui_draw_y_right_string(top, filesize_buf);

        top += 17;
    }
}
static void browser_scroll(int amount)
{
    if (amount == 0)
    {
        return;
    }
    if (amount > 0)
    {
        for (int i = 0; i < amount; i++)
        {
            if (++browser.selection >= browser.n_entries - 1)
            {
                browser.selection = browser.n_entries - 1;
            }
            if (browser.selection - browser.scroll > (MAX_WIN_ENTRIES - 1))
            {
                browser.scroll++;
            }
        }
    }
    else
    {
        amount = -amount;
        for (int i = 0; i < amount; i++)
        {
            if (--browser.selection < 0)
            {
                browser.selection = 0;
            }
            if (browser.selection - browser.scroll < 0)
            {
                browser.scroll--;
            }
        }
    }
    ui_draw_browser();
}

static void ui_draw_details(Entry *entry, const char *cwd)
{
    // Try to retrieve file stats
    if (fops_stat_entry(entry, cwd) == -1)
    {
        printf("Could not get file status\n");
        return;
    }
    ui_clear_screen();
    UG_SetForecolor(FG_COLOR_1);
    UG_SetBackcolor(BG_COLOR);

    char str_buf[300];
    char filesize_buf[32];

    const int line_height = 16;
    short y = 34;

    UG_PutString(5, y, "File Details");
    y += line_height * 2;
    // Full file name
    // TODO: Figure out wordwrapping or scrolling for long filenames
    snprintf(str_buf, 300, "Name: %s", entry->name);
    UG_PutString(5, y, str_buf);
    y += line_height * 2;
    // File size
    sprint_human_size(filesize_buf, 32, entry->size);
    snprintf(str_buf, 256, "Size: %s(%ld Bytes)", filesize_buf, entry->size);
    UG_PutString(5, y, str_buf);
    y += line_height;
    // Modification time
    sprint_human_size(filesize_buf, 32, entry->size);
    ctime_r(&entry->mtime, filesize_buf);
    snprintf(str_buf, 256, "ModTime: %s", filesize_buf);
    UG_PutString(5, y, str_buf);
    y += line_height;
    // Permissions
    const mode_t permissions = entry->mode & 0777;
    snprintf(str_buf, 256, "Permissions: %o", permissions);
    UG_PutString(5, y, str_buf);
    // TODO Later: filetype using libmagic?

    ui_flush();

    input_gamepad_state prevKey;
    gamepad_read(&prevKey);
    while (1)
    {
        input_gamepad_state key;
        gamepad_read(&key);
        if (!prevKey.values[GAMEPAD_INPUT_B] && key.values[GAMEPAD_INPUT_B])
        {
            break;
        }
    }
}

static void browser_init(const char *cwd)
{
    strncpy(browser.cwd, cwd, strlen(cwd) + 1);
    browser.cwd_entries = NULL;
    browser.selection = 0;
    browser.scroll = 0;
    browser.stat_enabled = false;
    printf("browser_init %s\n", cwd);
}

static int browser_cd(const char *new_cwd)
{
    Entry *new_entries;

    int n_entries = fops_list_dir(&new_entries, new_cwd);
    if (n_entries < 0)
    {
        printf("Can't change directory\n");
        return -1;
    }

    // Apply cd after successfully listing new directory
    fops_free_entries(&browser.cwd_entries, browser.n_entries);
    browser.n_entries = n_entries;
    browser.cwd_entries = new_entries;
    strncpy(browser.cwd, new_cwd, PATH_MAX);
    if (browser.selection >= browser.n_entries || browser.scroll >= browser.n_entries)
    {
        browser.selection = 0;
        browser.scroll = 0;
    }

    if (browser.stat_enabled)
        fops_stat_entries(browser.cwd_entries, browser.n_entries, browser.cwd);

    return 0;
}

static int path_cd_up(char *cwd, char *new_cwd)
{
    if (strlen(cwd) <= 1 && cwd[0] == '/')
    {
        // Cant go up anymore
        return -1;
    }

    // Remove upmost directory
    bool copy = false;
    int len = 0;
    for (ssize_t i = (ssize_t)strlen(cwd); i >= 0; i--)
    {
        if (cwd[i] == '/')
            copy = true;
        if (copy)
        {
            new_cwd[i] = cwd[i];
            len++;
        }
    }
    // remove trailing slash
    if (len > 1 && new_cwd[len - 1] == '/')
        len--;
    new_cwd[len] = '\0';

    return 0;
}

static int browser_cd_down(const char *dir)
{
    char new_cwd[PATH_MAX];
    const char *sep = !strncmp(browser.cwd, "/", 2) ? "" : "/"; // Don't append / if at /
    snprintf(new_cwd, PATH_MAX, "%s%s%s", browser.cwd, sep, dir);

    browser.scroll = 0;
    browser.selection = 0;

    return browser_cd(new_cwd);
}

static int browser_cd_up(void)
{
    char new_cwd[PATH_MAX];
    if (path_cd_up(browser.cwd, new_cwd) < 0)
    {
        printf("Can't go up anymore! Already at topmost directory.\n");
        return -1;
    }

    return browser_cd(new_cwd);
}

static void open_file(Entry *entry)
{
    ui_draw_details(entry, browser.cwd);
    return;
    // TODO: Proper File handlers
    const FileType ftype = fops_determine_filetype(entry);
    if (ftype == FileTypeMP3 || ftype == FileTypeOGG || ftype == FileTypeMOD || ftype == FileTypeWAV || ftype == FileTypeFLAC ||
        ftype == FileTypeGME)
    {
        // audio_player((AudioPlayerParam){browser.cwd_entries, browser.n_entries, browser.selection, browser.cwd, true});
    }
    else if (ftype == FileTypeJPEG || ftype == FileTypePNG || ftype == FileTypeBMP || ftype == FileTypeGIF)
    {
        // image_viewer((ImageViewerParams){browser.cwd_entries, browser.n_entries, browser.selection, browser.cwd});
    }
    else if (ftype == FileTypeGB || ftype == FileTypeGBC || ftype == FileTypeNES || ftype == FileTypeGG || ftype == FileTypeCOL ||
             ftype == FileTypeSMS)
    {
        // emulator_launcher((EmulatorLauncherParam){.entry = &browser.cwd_entries[browser.selection],
        //                                           .rom_filetype = ftype,
        //                                           .cwd = browser.cwd,
        //                                           .fb_selection = browser.selection,
        //                                           .fb_scroll = browser.scroll});
    }
    else
    {
        ui_draw_details(entry, browser.cwd);
    }
}

int app_file_browser(FileBrowserParam params)
{
    bool quit = false;
    // event_t event;

    browser_init(params.cwd);
    // TODO: browser_load_settings();

    browser.n_entries = fops_list_dir(&browser.cwd_entries, browser.cwd);
    if (browser.n_entries < 0)
    {
        browser.n_entries = 0;
    }

    ui_draw_browser();

    int doRefresh = 1;
    input_gamepad_state prevKey;
    gamepad_read(&prevKey);
    while (!quit)
    {
        input_gamepad_state key;
        gamepad_read(&key);

        if (!prevKey.values[GAMEPAD_INPUT_DOWN] && key.values[GAMEPAD_INPUT_DOWN])
        {
            browser_scroll(1);

            doRefresh = 1;
        }
        else if (!prevKey.values[GAMEPAD_INPUT_UP] && key.values[GAMEPAD_INPUT_UP])
        {
            browser_scroll(-1);
            doRefresh = 1;
        }
        else if (!prevKey.values[GAMEPAD_INPUT_LEFT] && key.values[GAMEPAD_INPUT_LEFT])
        {
            browser_scroll(-MAX_WIN_ENTRIES);
            doRefresh = 1;
        }
        else if (!prevKey.values[GAMEPAD_INPUT_RIGHT] && key.values[GAMEPAD_INPUT_RIGHT])
        {
            browser_scroll(MAX_WIN_ENTRIES);
            doRefresh = 1;
        }
        if (!prevKey.values[GAMEPAD_INPUT_A] && key.values[GAMEPAD_INPUT_A])
        {
            if (browser.n_entries <= 0)
            {
                continue;
            }
            Entry *entry = &browser.cwd_entries[browser.selection];
            if (S_ISDIR(entry->mode))
            {
                stack_push();
                browser_cd_down(entry->name);
            }
            else
            {
                open_file(entry);
            }
            ui_draw_browser();
            doRefresh = 1;
        }
        else if (!prevKey.values[GAMEPAD_INPUT_B] && key.values[GAMEPAD_INPUT_B])
        {
            stack_pop();
            int i = browser_cd_up();
            if (i < 0)
            {
                // Back main menu
                break;
            }
            else
            {
                ui_draw_browser();
                doRefresh = 1;
            }
        }

        if (doRefresh)
        {
            ui_flush();
        }
        doRefresh = 0;
        prevKey = key;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    fops_free_entries(&browser.cwd_entries, browser.n_entries);

    return 0;
}