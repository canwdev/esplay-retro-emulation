#include "gui.h"

rgb_t gui_pal[GUI_TOTALCOLORS];

void gui_setrefresh(int frequency) { (void)frequency; }
void gui_sendmsg(int color, char *format, ...) { (void)color; (void)format; }
int  gui_init(void) { return 0; }
void gui_shutdown(void) {}

void gui_savesnap(void) {}
void gui_togglefs(void) {}
void gui_toggleoam(void) {}
void gui_togglewave(void) {}
void gui_togglepattern(void) {}
void gui_incpatterncol(void) {}
void gui_decpatterncol(void) {}
void gui_togglefps(void) {}
void gui_displayinfo(void) {}
void gui_togglegui(void) {}
void gui_toggle_chan(int chan) { (void)chan; }
void gui_setfilter(int filter) { (void)filter; }
void gui_togglesprites(void) {}
