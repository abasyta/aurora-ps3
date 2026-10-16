/*
 * ui.c - Aurora-style coverflow renderer (cairo based)
 *
 * Visual layout (matches the Xbox 360 Aurora dashboard look):
 *   - purple/blue gradient background with aurora ribbons + glows
 *   - covers flying left/right, center cover enlarged with white glow
 *   - reflective floor under the covers
 *   - dark strip with game title + "N of M"
 *   - light strip at the bottom with button hints
 *   - "PlayStation 3" label at top right
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include "ui.h"
#include "games.h"

#define CLAMPF(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
#define LERPF(a, b, t)  ((a) + ((b) - (a)) * (t))

static cairo_surface_t *bg_surface = NULL;
static cairo_font_face_t *ui_font = NULL;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void round_rect(cairo_t *cr, float x, float y, float w, float h, float r)
{
	cairo_move_to(cr, x + r, y);
	cairo_line_to(cr, x + w - r, y);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0f, 0.0f);
	cairo_line_to(cr, x + w, y + h - r);
	cairo_arc(cr, x + w - r, y + h - r, r, 0.0f, M_PI / 2.0f);
	cairo_line_to(cr, x + r, y + h);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0f, M_PI);
	cairo_line_to(cr, x, y + r);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5f * M_PI);
	cairo_close_path(cr);
}

/* align: 0 = centered on x, -1 = right-aligned at x, 1 = left-aligned at x */
static void text(cairo_t *cr, const char *s, float x, float y, float size,
                 int align, float r, float g, float b, float a)
{
	if (!s || !s[0])
		return;

	cairo_set_source_rgba(cr, r, g, b, a);
	if (ui_font)
		cairo_set_font_face(cr, ui_font);
	else
		cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
		                       CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, size);

	cairo_text_extents_t te;
	cairo_text_extents(cr, s, &te);

	float px = x;
	if (align == 0)
		px = x - te.width / 2.0f - te.x_bearing;
	else if (align == -1)
		px = x - te.width - te.x_bearing;

	cairo_move_to(cr, px, y);
	cairo_show_text(cr, s);
}

/* ------------------------------------------------------------------ */
/* background                                                          */
/* ------------------------------------------------------------------ */

static void glow(cairo_t *cr, float x, float y, float r,
                 float rr, float gg, float bb, float a)
{
	cairo_pattern_t *p = cairo_pattern_create_radial(x, y, 0, x, y, r);
	cairo_pattern_add_color_stop_rgba(p, 0, rr, gg, bb, a);
	cairo_pattern_add_color_stop_rgba(p, 1, rr, gg, bb, 0);
	cairo_set_source(cr, p);
	cairo_paint(cr);
	cairo_pattern_destroy(p);
}

static void ribbon(cairo_t *cr, float W, float H, float ph,
                   float r, float g, float b, float a)
{
	cairo_save(cr);
	cairo_new_path(cr);
	cairo_move_to(cr, -60, H * 0.80f);
	cairo_curve_to(cr, W * 0.22f, H * (0.50f + ph),
	                    W * 0.58f, H * (0.66f - ph * 0.5f),
	                    W + 60, H * 0.30f);
	for (int i = 4; i > 0; i--) {
		cairo_set_source_rgba(cr, r, g, b, a / (float)(i * i));
		cairo_set_line_width(cr, H * 0.045f * (float)i);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static cairo_surface_t *make_background(int W, int H)
{
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
	if (!s)
		return NULL;

	cairo_t *cr = cairo_create(s);

	/* vertical gradient: deep purple -> dark navy */
	cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, H);
	cairo_pattern_add_color_stop_rgb(g, 0.00, 0.13, 0.09, 0.30);
	cairo_pattern_add_color_stop_rgb(g, 0.42, 0.17, 0.12, 0.40);
	cairo_pattern_add_color_stop_rgb(g, 0.75, 0.06, 0.07, 0.18);
	cairo_pattern_add_color_stop_rgb(g, 1.00, 0.02, 0.02, 0.06);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);

	/* glows */
	glow(cr, W * 0.14f, H * 0.72f, W * 0.36f, 0.55f, 0.20f, 0.60f, 0.30f);
	glow(cr, W * 0.86f, H * 0.24f, W * 0.30f, 0.14f, 0.36f, 0.78f, 0.26f);
	glow(cr, W * 0.52f, H * 0.10f, W * 0.24f, 0.28f, 0.14f, 0.58f, 0.14f);

	/* aurora ribbons */
	ribbon(cr, W, H, 0.02f, 0.35f, 0.95f, 0.85f, 0.10f);   /* cyan  */
	ribbon(cr, W, H, 0.10f, 0.55f, 0.40f, 0.95f, 0.10f);   /* violet*/
	ribbon(cr, W, H, 0.18f, 0.95f, 0.45f, 0.75f, 0.08f);   /* pink  */

	/* vignette */
	cairo_pattern_t *v = cairo_pattern_create_radial(W * 0.5f, H * 0.45f, W * 0.25f,
	                                                 W * 0.5f, H * 0.5f, W * 0.75f);
	cairo_pattern_add_color_stop_rgba(v, 0, 0, 0, 0, 0);
	cairo_pattern_add_color_stop_rgba(v, 1, 0, 0, 0, 0.55f);
	cairo_set_source(cr, v);
	cairo_paint(cr);
	cairo_pattern_destroy(v);

	cairo_destroy(cr);
	return s;
}

/* ------------------------------------------------------------------ */
/* hints                                                               */
/* ------------------------------------------------------------------ */

static void hint_glyph(cairo_t *cr, float x, float y, float r,
                       const char *glyph, float rr, float gg, float bb)
{
	/* colored filled circle with white glyph */
	cairo_set_source_rgba(cr, rr, gg, bb, 0.95f);
	cairo_arc(cr, x, y, r, 0, 2 * M_PI);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	if (ui_font)
		cairo_set_font_face(cr, ui_font);
	else
		cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
		                       CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, r * 1.05f);

	cairo_text_extents_t te;
	cairo_text_extents(cr, glyph, &te);
	cairo_move_to(cr, x - te.width / 2.0f - te.x_bearing,
	                  y + te.height / 2.0f - te.y_bearing / 2.0f);
	cairo_show_text(cr, glyph);
}

static void draw_hints(cairo_t *cr, int W, int H, float y0, float band)
{
	/* light bottom band, aurora style */
	cairo_set_source_rgba(cr, 0.96f, 0.97f, 0.99f, 0.95f);
	cairo_rectangle(cr, 0, y0, W, band);
	cairo_fill(cr);

	/* hairline */
	cairo_set_source_rgba(cr, 1, 1, 1, 0.18f);
	cairo_rectangle(cr, 0, y0 - 1.5f, W, 1.5f);
	cairo_fill(cr);

	float cy = y0 + band * 0.52f;
	float r = band * 0.26f;
	float label_size = band * 0.30f;

	struct { const char *glyph; const char *label; float r, g, b; } hints[] = {
		{ "X", "Launch",   0.20f, 0.55f, 0.95f },
		{ "O", "About",    0.92f, 0.25f, 0.25f },
		{ "S", "Details",  0.90f, 0.35f, 0.80f },
		{ "T", "Refresh",  0.25f, 0.78f, 0.45f },
		{ "\xB5", "Exit (START)", 0.45f, 0.45f, 0.50f },
	};

	/* START gets a pill instead of a circle; keep spacing uniform */
	float xs[5];
	float total = 0;
	for (int i = 0; i < 5; i++) {
		cairo_text_extents_t te;
		if (ui_font) cairo_set_font_face(cr, ui_font);
		else cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, label_size);
		cairo_text_extents(cr, hints[i].label, &te);
		xs[i] = total + r * 2 + te.width;
		total = xs[i] + W * 0.055f;
	}
	float x = (W - total) * 0.5f;

	for (int i = 0; i < 5; i++) {
		if (i == 4) {
			/* START pill */
			float pw = r * 3.6f, ph = r * 1.6f;
			cairo_set_source_rgba(cr, 0.25f, 0.27f, 0.32f, 0.95f);
			round_rect(cr, x, cy - ph / 2, pw, ph, ph / 2);
			cairo_fill(cr);
			text(cr, "START", x + pw / 2, cy + r * 0.35f, r * 0.9f, 0, 1, 1, 1, 1);
			x += pw;
		} else {
			hint_glyph(cr, x + r, cy, r, hints[i].glyph, hints[i].r, hints[i].g, hints[i].b);
			x += r * 2;
		}
		text(cr, hints[i].label, x + band * 0.12f, cy + label_size * 0.36f,
		     label_size, 1, 0.15f, 0.17f, 0.22f, 1.0f);
		x += W * 0.055f;
	}
}

/* ------------------------------------------------------------------ */
/* coverflow                                                           */
/* ------------------------------------------------------------------ */

static void draw_cover(cairo_t *cr, int W, int H, int i, float rel,
                       float box_w, float box_h, float cx, float bottom,
                       float bar_top, int is_center)
{
	(void)W;
	(void)H;
	float k = fabsf(rel);
	float sgn = (rel == 0.0f) ? 0.0f : (rel > 0.0f ? 1.0f : -1.0f);

	float s1 = 0.66f;
	float b, s;
	if (k < 1.0f) {
		b = box_w * 0.62f * k;
		s = LERPF(1.0f, s1, k);
	} else {
		b = box_w * (0.62f + 0.48f * (k - 1.0f) / (1.0f + 0.24f * (k - 1.0f)));
		s = s1 / (1.0f + (k - 1.0f) * 0.18f);
	}
	float alpha = (k <= 1.0f) ? 1.0f : CLAMPF(1.0f - (k - 1.0f) * 0.055f, 0.25f, 1.0f);

	float x = cx + sgn * b;
	float w = box_w * s;
	float h = box_h * s;
	float top = bottom - h;

	if (x + w * 0.5f < -20 || x - w * 0.5f > W + 20)
		return;

	/* shadow */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.45f * alpha);
	round_rect(cr, x - w / 2 + w * 0.02f, top + w * 0.045f + 8, w, h, 12);
	cairo_fill(cr);

	cairo_surface_t *cov = game_cover_get(i);

	cairo_save(cr);
	round_rect(cr, x - w / 2, top, w, h, 10);
	cairo_clip(cr);

	if (cov) {
		int iw = cairo_image_surface_get_width(cov);
		int ih = cairo_image_surface_get_height(cov);
		float fs = (iw && ih) ? (w / (float)iw < h / (float)ih ? w / (float)iw : h / (float)ih) : 1.0f;
		float dw = iw * fs, dh = ih * fs;
		cairo_set_source_surface(cr, cov, x - dw / 2, top + (h - dh) / 2);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint_with_alpha(cr, alpha);
	} else {
		/* fallback panel */
		cairo_pattern_t *g = cairo_pattern_create_linear(x - w / 2, top, x - w / 2, top + h);
		cairo_pattern_add_color_stop_rgb(g, 0.0, 0.20, 0.16, 0.42);
		cairo_pattern_add_color_stop_rgb(g, 1.0, 0.05, 0.06, 0.16);
		cairo_set_source(cr, g);
		cairo_paint_with_alpha(cr, alpha);
		cairo_pattern_destroy(g);
		text(cr, g_games[i].id, x, top + h * 0.52f, w * 0.14f, 0, 0.9, 0.9, 0.95, alpha * 0.9f);
	}
	cairo_restore(cr);

	/* border */
	cairo_set_source_rgba(cr, is_center ? 1.0f : 0.05f,
	                          is_center ? 1.0f : 0.05f,
	                          is_center ? 1.0f : 0.08f, 0.9f);
	cairo_set_line_width(cr, is_center ? 3.0f : 1.5f);
	round_rect(cr, x - w / 2, top, w, h, 10);
	cairo_stroke(cr);

	/* white glow around the selected cover */
	if (is_center) {
		float lw[3] = { 7, 15, 26 };
		float la[3] = { 0.30f, 0.14f, 0.06f };
		for (int j = 0; j < 3; j++) {
			cairo_set_source_rgba(cr, 1, 1, 1, la[j]);
			cairo_set_line_width(cr, lw[j]);
			round_rect(cr, x - w / 2, top, w, h, 10);
			cairo_stroke(cr);
		}
	}

	/* reflection on the floor */
	float rh = CLAMPF(box_h * 0.20f, 10, bar_top - bottom - 4);
	if (rh > 6 && cov) {
		cairo_save(cr);
		cairo_rectangle(cr, x - w / 2, bottom, w, rh);
		cairo_clip(cr);
		cairo_translate(cr, 0, 2 * bottom);
		cairo_scale(cr, 1, -1);

		int iw = cairo_image_surface_get_width(cov);
		int ih = cairo_image_surface_get_height(cov);
		float fs = (iw && ih) ? (w / (float)iw < h / (float)ih ? w / (float)iw : h / (float)ih) : 1.0f;
		float dw = iw * fs, dh = ih * fs;
		cairo_set_source_surface(cr, cov, x - dw / 2, top + (h - dh) / 2);
		cairo_paint_with_alpha(cr, 0.16f * alpha);
		cairo_restore(cr);

		/* fade the reflection into the background */
		cairo_pattern_t *fg = cairo_pattern_create_linear(0, bottom, 0, bottom + rh);
		cairo_pattern_add_color_stop_rgba(fg, 0, 0, 0, 0, 0.10f);
		cairo_pattern_add_color_stop_rgba(fg, 1, 0.01f, 0.01f, 0.03f, 0.92f);
		cairo_set_source(cr, fg);
		cairo_rectangle(cr, x - w / 2, bottom, w, rh);
		cairo_fill(cr);
		cairo_pattern_destroy(fg);
	}
}

/* ------------------------------------------------------------------ */
/* main frame                                                          */
/* ------------------------------------------------------------------ */

void ui_frame(cairo_t *cr, int W, int H, float pos, double t)
{
	(void)t;

	/* background */
	if (bg_surface) {
		cairo_set_source_surface(cr, bg_surface, 0, 0);
		cairo_paint(cr);
	} else {
		cairo_set_source_rgb(cr, 0.07f, 0.06f, 0.18f);
		cairo_paint(cr);
	}

	int N = g_game_count;
	int c = (int)floorf(pos + 0.5f);
	if (c < 0) c = 0;
	if (c > N - 1) c = N - 1;

	float box_w = H * 0.31f;
	float box_h = H * 0.46f;
	float cx = W * 0.5f;
	float bar_top = H - H * 0.155f;
	float light_top = H - H * 0.068f;
	float bottom = bar_top - H * 0.012f;

	/* far covers first, center last */
	int maxd = 14;
	for (int d = maxd; d >= 1; d--) {
		for (int sgn = -1; sgn <= 1; sgn += 2) {
			int i = c + sgn * d;
			if (i < 0 || i >= N)
				continue;
			draw_cover(cr, W, H, i, (float)i - pos, box_w, box_h,
			           cx, bottom, bar_top, 0);
		}
	}
	if (N > 0)
		draw_cover(cr, W, H, c, (float)c - pos, box_w, box_h,
		           cx, bottom, bar_top, 1);

	/* top labels */
	text(cr, "AURORA", 14, H * 0.055f, H * 0.030f, 1, 1, 1, 1, 0.85f);
	text(cr, "PlayStation 3", W - 14, H * 0.055f, H * 0.030f, -1, 1, 1, 1, 0.85f);

	if (N == 0) {
		text(cr, "No games found", W / 2.0f, H * 0.45f, H * 0.045f, 0, 1, 1, 1, 0.95f);
		text(cr, "Put backups in /GAMES or /GAMEZ (folders with PS3_GAME inside),",
		     W / 2.0f, H * 0.52f, H * 0.024f, 0, 0.8, 0.8, 0.85, 1);
		text(cr, "or install game PKGs on the internal HDD.",
		     W / 2.0f, H * 0.555f, H * 0.024f, 0, 0.8, 0.8, 0.85, 1);
		return;
	}

	/* dark title band */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.62f);
	cairo_rectangle(cr, 0, bar_top, W, light_top - bar_top);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.10f);
	cairo_rectangle(cr, 0, bar_top, W, 1.5f);
	cairo_fill(cr);

	/* title + counter */
	GameEntry *g = &g_games[c];
	text(cr, g->title, cx, bar_top + (light_top - bar_top) * 0.42f,
	     H * 0.038f, 0, 1, 1, 1, 1);
	char sub[64];
	snprintf(sub, sizeof(sub), "%d of %d", c + 1, N);
	text(cr, sub, cx, bar_top + (light_top - bar_top) * 0.80f,
	     H * 0.026f, 0, 0.72f, 0.72f, 0.76f, 1);

	/* hints band */
	draw_hints(cr, W, H, light_top, H - light_top);
}

/* ------------------------------------------------------------------ */

int ui_init(int w, int h)
{
	bg_surface = make_background(w, h);
	return bg_surface ? 1 : 0;
}

void ui_set_font(cairo_font_face_t *face)
{
	ui_font = face;
}

void ui_shutdown(void)
{
	if (bg_surface) {
		cairo_surface_destroy(bg_surface);
		bg_surface = NULL;
	}
}