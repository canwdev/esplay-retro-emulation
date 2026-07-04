#ifndef _GUI_H_
#define _GUI_H_

#include <noftypes.h>
#include <bitmap.h>

enum {
   GUI_BLACK = 192,
   GUI_WHITE,
   GUI_RED,
   GUI_GREEN,
   GUI_BLUE,
   GUI_YELLOW,
   GUI_ORANGE,
   GUI_PURPLE,
   GUI_TEAL,
   GUI_DKGREEN,
   GUI_DKBLUE,
   GUI_LTGRAY,
   GUI_GRAY,
   GUI_DKGRAY,
};

#define GUI_TOTALCOLORS   14

extern rgb_t gui_pal[GUI_TOTALCOLORS];

void gui_setrefresh(int frequency);
void gui_sendmsg(int color, char *format, ...);
int  gui_init(void);
void gui_shutdown(void);

void gui_savesnap(void);
void gui_togglefs(void);
void gui_toggleoam(void);
void gui_togglewave(void);
void gui_togglepattern(void);
void gui_incpatterncol(void);
void gui_decpatterncol(void);
void gui_togglefps(void);
void gui_displayinfo(void);
void gui_togglegui(void);
void gui_toggle_chan(int chan);
void gui_setfilter(int filter);
void gui_togglesprites(void);

#endif
