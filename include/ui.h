/*
 * ui.h - Aurora-style coverflow rendering (cairo based)
 */

#ifndef __UI_H__
#define __UI_H__

#include <cairo/cairo.h>

/* build the prerendered background + remember resolution */
int ui_init(int w, int h);

/* optional font face (freetype). NULL = use cairo default fonts */
void ui_set_font(cairo_font_face_t *face);

/* draw one full frame:
 *   pos = current (fractional) selection position
 *   t   = seconds since launch (for subtle animation)
 */
void ui_frame(cairo_t *cr, int w, int h, float pos, double t);

void ui_shutdown(void);

#endif