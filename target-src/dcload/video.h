#ifndef __VIDEO_H__
#define __VIDEO_H__

void draw_string(int x, int y, const char *string, int colour);
void clrscr(int colour);
unsigned char *get_font_address(void);

#endif
