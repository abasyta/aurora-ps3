/*
 * games.c - game discovery + PARAM.SFO parsing + cover loading
 * Aurora Coverflow for PS3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sysmodule/sysmodule.h>
#include <pngdec/pngdec.h>

#include <cairo/cairo.h>

#include "games.h"

#define APP_ID "AURORA001"
#define APP_USRDIR "/dev_hdd0/game/AURORA001/USRDIR"

GameEntry g_games[GAMES_MAX];
int g_game_count = 0;

/* config */
static unsigned int cfg_webman_port = 80;
static char cfg_font_path[600] = "";
static char cfg_extra_paths[1024] = "";


/* ------------------------------------------------------------------ */
/* file helpers                                                        */
/* ------------------------------------------------------------------ */

static int read_file(const char *path, unsigned char **out, unsigned int *outlen)
{
	*out = NULL;
	*outlen = 0;

	int fd = -1;
	sysFSStat st;

	if (sysFsOpen(path, SYS_O_RDONLY, &fd, NULL, 0) != 0)
		return 0;

	if (sysFsFstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > (4*1024*1024)) {
		sysFsClose(fd);
		return 0;
	}

	unsigned char *buf = (unsigned char *)malloc((size_t)st.st_size + 1);
	if (!buf) {
		sysFsClose(fd);
		return 0;
	}

	u64 done = 0;
	while (done < (u64)st.st_size) {
		u64 rd = 0;
		if (sysFsRead(fd, buf + done, (u64)st.st_size - done, &rd) != 0 || rd == 0)
			break;
		done += rd;
	}
	sysFsClose(fd);

	if (done != (u64)st.st_size) {
		free(buf);
		return 0;
	}

	buf[done] = 0;
	*out = buf;
	*outlen = (unsigned int)done;
	return 1;
}

static int file_exists(const char *path)
{
	sysFSStat st;
	return sysFsStat(path, &st) == 0;
}

/* ------------------------------------------------------------------ */
/* PARAM.SFO parsing                                                   */
/* ------------------------------------------------------------------ */

static int sfo_get(const unsigned char *buf, unsigned int size,
                   const char *key, char *out, unsigned int outn)
{
	out[0] = 0;
	if (size < 0x14)
		return 0;

	if (memcmp(buf, "\0PSF", 4) != 0)
		return 0;

	u32 key_off   = *(const u32 *)(buf +  8);
	u32 data_off  = *(const u32 *)(buf + 12);
	u32 n_entries = *(const u32 *)(buf + 16);

	if (key_off > size || data_off > size)
		return 0;

	for (u32 i = 0; i < n_entries; i++) {
		const unsigned char *ent = buf + 0x14 + i * 0x10;
		if (0x14 + (i + 1) * 0x10 > size)
			return 0;

		u16 koff = *(const u16 *)(ent + 0);
		u32 doff = *(const u32 *)(ent + 12);

		if (key_off + koff >= size || data_off + doff >= size)
			continue;

		const char *k = (const char *)(buf + key_off + koff);
		if (strcmp(k, key) == 0) {
			const char *v = (const char *)(buf + data_off + doff);
			unsigned int j = 0;
			while (j + 1 < outn && v[j]) {
				out[j] = v[j];
				j++;
			}
			out[j] = 0;
			return 1;
		}
	}
	return 0;
}

/* trim helper */
static void strtrim(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n-1] == ' ' || s[n-1] == '\r' || s[n-1] == '\n'))
		s[--n] = 0;
	size_t start = 0;
	while (s[start] == ' ') start++;
	if (start) memmove(s, s + start, strlen(s + start) + 1);
}

/* ------------------------------------------------------------------ */
/* scanning                                                            */
/* ------------------------------------------------------------------ */

static void scan_add(const char *base, const char *name, int is_content_dir)
{
	char path[GAMES_PATH_MAX];
	char sfo_path[620];
	char cover_path[640];
	char title[128] = "";
	char id[16] = "";
	char category[4] = "";

	if (g_game_count >= GAMES_MAX)
		return;

	snprintf(path, sizeof(path), "%s/%s", base, name);

	/* layout 1: <path>/PS3_GAME/PARAM.SFO (backup folder) */
	snprintf(sfo_path, sizeof(sfo_path), "%s/PS3_GAME/PARAM.SFO", path);
	if (!file_exists(sfo_path)) {
		/* layout 2: <path>/PARAM.SFO (installed content dir) */
		snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", path);
		if (!file_exists(sfo_path))
			return;
		is_content_dir = 1;
		snprintf(cover_path, sizeof(cover_path), "%s/ICON0.PNG", path);
	} else {
		snprintf(cover_path, sizeof(cover_path), "%s/PS3_GAME/ICON0.PNG", path);
	}

	/* dedup by path */
	for (int i = 0; i < g_game_count; i++)
		if (strcmp(g_games[i].path, path) == 0)
			return;

	unsigned char *buf = NULL;
	unsigned int size = 0;
	if (!read_file(sfo_path, &buf, &size))
		return;

	sfo_get(buf, size, "TITLE",     title,    sizeof(title));
	sfo_get(buf, size, "TITLE_ID",  id,       sizeof(id));
	sfo_get(buf, size, "CATEGORY",  category, sizeof(category));
	free(buf);

	strtrim(title);
	strtrim(id);
	strtrim(category);

	if (strcmp(id, APP_ID) == 0)   /* do not list ourselves */
		return;

	if (is_content_dir) {
		/* installed apps: keep games/minis/demos only */
		if (strcmp(category, "HG") != 0 && strcmp(category, "DG") != 0 &&
		    strcmp(category, "MN") != 0 && strcmp(category, "SD") != 0 &&
		    strcmp(category, "EW") != 0)
			return;
	} else {
		if (category[0] == 0)
			strcpy(category, "DG");
	}
	if (id[0] == 0)
		strcpy(id, "?????????");

	GameEntry *g = &g_games[g_game_count];
	memset(g, 0, sizeof(GameEntry));
	snprintf(g->title, sizeof(g->title), "%s", title[0] ? title : id);
	snprintf(g->id, sizeof(g->id), "%s", id);
	snprintf(g->category, sizeof(g->category), "%s", category);
	snprintf(g->path, sizeof(g->path), "%s", path);
	snprintf(g->cover, sizeof(g->cover), "%s", cover_path);
	g->type = is_content_dir ? GAME_INSTALLED : GAME_BACKUP;
	g->has_cover = 0;
	g_game_count++;
}

static void scan_dir(const char *base, int is_content_dir)
{
	int fd = -1;
	if (sysFsOpendir(base, &fd) != 0)
		return;

	sysFSDirent ent;
	u64 read = 0;

	while (g_game_count < GAMES_MAX) {
		if (sysFsReaddir(fd, &ent, &read) != 0)
			break;
		if (read == 0)
			break;
		const char *name = ent.d_name;
		if (!name[0])
			continue;
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;
		scan_add(base, name, is_content_dir);
	}
	sysFsClosedir(fd);
}

void games_scan(void)
{
	char base[64];

	g_game_count = 0;
	memset(g_games, 0, sizeof(g_games));

	/* internal backup folders */
	scan_dir("/dev_hdd0/GAMES", 0);
	scan_dir("/dev_hdd0/GAMEZ", 0);
	scan_dir("/dev_hdd0/PS3GAME", 0);
	scan_dir("/dev_hdd0/GAMES_ISO", 0);

	/* installed content (PKG games) */
	scan_dir("/dev_hdd0/game", 1);

	/* usb devices */
	for (int i = 0; i <= 7; i++) {
		snprintf(base, sizeof(base), "/dev_usb00%d/GAMES", i);
		scan_dir(base, 0);
		snprintf(base, sizeof(base), "/dev_usb00%d/GAMEZ", i);
		scan_dir(base, 0);
		snprintf(base, sizeof(base), "/dev_usb00%d/PS3/GAMES", i);
		scan_dir(base, 0);
	}

	/* extra paths from aurora.ini */
	if (cfg_extra_paths[0]) {
		char *copy = strdup(cfg_extra_paths);
		if (copy) {
			char *p = copy;
			while (*p) {
				char *semi = strchr(p, ';');
				if (semi) *semi = 0;
				strtrim(p);
				if (p[0]) scan_dir(p, 0);
				if (!semi) break;
				p = semi + 1;
			}
			free(copy);
		}
	}

	/* sort alphabetically by title */
	for (int i = 1; i < g_game_count; i++) {
		GameEntry tmp = g_games[i];
		int j = i - 1;
		while (j >= 0 && strcmp(g_games[j].title, tmp.title) > 0) {
			g_games[j + 1] = g_games[j];
			j--;
		}
		g_games[j + 1] = tmp;
	}
}

void games_free(void)
{
	games_covers_clear();
	g_game_count = 0;
	memset(g_games, 0, sizeof(g_games));
}

void games_covers_clear(void)
{
	for (int i = 0; i < g_game_count; i++) {
		GameEntry *g = &g_games[i];
		if (g->surf) {
			cairo_surface_destroy(g->surf);
			g->surf = NULL;
		}
		g->loadfail = 0;
	}
}

/* ------------------------------------------------------------------ */
/* cover loading via pngdec -> cairo ARGB32 surface                    */
/* ------------------------------------------------------------------ */

static cairo_surface_t *png_to_cairo(const char *path)
{
	static int pngdec_ready = -1;
	if (pngdec_ready < 0) {
		s32 ret = sysModuleLoad(SYSMODULE_PNGDEC);
		pngdec_ready = (ret == 0 || ret == 0x80012001) ? 1 : 0;
	}
	if (!pngdec_ready)
		return NULL;

	pngData png;
	memset(&png, 0, sizeof(png));
	if (pngLoadFromFile(path, &png) != 0 || !png.bmp_out ||
	    png.width == 0 || png.height == 0)
		return NULL;

	u32 w = png.width, h = png.height;
	u32 pitch = png.pitch;

	cairo_surface_t *src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (!src) {
		free(png.bmp_out);
		return NULL;
	}

	u8 *dst = cairo_image_surface_get_data(src);
	int dststride = cairo_image_surface_get_stride(src);
	const unsigned char *sp = (const unsigned char *)png.bmp_out;

	for (u32 y = 0; y < h; y++) {
		const unsigned char *srow = sp + y * pitch;
		u32 *drow = (u32 *)(dst + y * dststride);
		for (u32 x = 0; x < w; x++) {
			/* pngdec outputs RGBA */
			u8 r = srow[0], g = srow[1], b = srow[2], a = srow[3];
			srow += 4;
			u32 aa = (u32)a;
			u32 rr = (r * aa + 127) / 255;
			u32 gg = (g * aa + 127) / 255;
			u32 bb = (b * aa + 127) / 255;
			drow[x] = (aa << 24) | (rr << 16) | (gg << 8) | bb;
		}
	}
	cairo_surface_mark_dirty(src);

	free(png.bmp_out);

	/* downscale big covers to max 256x384 */
	int tw = w, th = h;
	float fit = 1.0f;
	if (w > 256 || h > 384) {
		float sw = 256.0f / (float)w;
		float sh = 384.0f / (float)h;
		fit = sw < sh ? sw : sh;
		tw = (int)(w * fit);
		th = (int)(h * fit);
	}

	cairo_surface_t *out;
	if (tw == (int)w && th == (int)h)
		out = src;
	else {
		out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
		if (out) {
			cairo_t *cr = cairo_create(out);
			cairo_scale(cr, (float)tw / w, (float)th / h);
			cairo_set_source_surface(cr, src, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint(cr);
			cairo_destroy(cr);
			cairo_surface_destroy(src);
		} else
			out = src;
	}
	return out;
}

cairo_surface_t *game_cover_get(int i)
{
	if (i < 0 || i >= g_game_count)
		return NULL;

	GameEntry *g = &g_games[i];
	if (g->surf)
		return g->surf;
	if (g->loadfail)
		return NULL;
	if (!g->cover[0]) {
		g->loadfail = 1;
		return NULL;
	}

	cairo_surface_t *s = png_to_cairo(g->cover);
	if (s) {
		g->surf = s;
		g->surf_w = cairo_image_surface_get_width(s);
		g->surf_h = cairo_image_surface_get_height(s);
		g->has_cover = 1;
	} else
		g->loadfail = 1;

	return g->surf;
}

void games_covers_trim(int center, int keep_dist)
{
	for (int i = 0; i < g_game_count; i++) {
		if (i >= center - keep_dist && i <= center + keep_dist)
			continue;
		GameEntry *g = &g_games[i];
		if (g->surf) {
			cairo_surface_destroy(g->surf);
			g->surf = NULL;
		}
	}
}

/* ------------------------------------------------------------------ */
/* config (aurora.ini)                                                 */
/* ------------------------------------------------------------------ */

static int ini_get(const unsigned char *buf, unsigned int size,
                    const char *key, char *out, unsigned int outn)
{
	out[0] = 0;
	char *copy = malloc(size + 1);
	if (!copy) return 0;
	memcpy(copy, buf, size);
	copy[size] = 0;

	char *p = copy;
	while (*p) {
		char *eol = strchr(p, '\n');
		if (eol) *eol = 0;
		char *eq = strchr(p, '=');
		if (eq) {
			*eq = 0;
			char *k = p, *v = eq + 1;
			/* trim key */
			char *e = k + strlen(k);
			while (e > k && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
			strtrim(v);
			if (strcmp(k, key) == 0) {
				snprintf(out, outn, "%s", v);
				free(copy);
				return;
			}
		}
		if (!eol) break;
		p = eol + 1;
	}
	free(copy);
	return 0;
}

void games_config_load(void)
{
	unsigned char *buf = NULL;
	unsigned int size = 0;
	char tmp[1200];

	const char *candidates[2] = { APP_USRDIR "/aurora.ini", "/app_home/aurora.ini" };

	for (int i = 0; i < 2; i++) {
		if (read_file(candidates[i], &buf, &size)) {
			if (ini_get(buf, size, "webman_port", tmp, sizeof(tmp))) {
				int p = atoi(tmp);
				if (p > 0 && p < 65536)
					cfg_webman_port = (unsigned int)p;
			}
			if (ini_get(buf, size, "extra_paths", tmp, sizeof(tmp)))
				snprintf(cfg_extra_paths, sizeof(cfg_extra_paths), "%s", tmp);
			if (ini_get(buf, size, "font", tmp, sizeof(tmp)))
				snprintf(cfg_font_path, sizeof(cfg_font_path), "%s", tmp);
			free(buf);
			break;
		}
	}
}

unsigned int aurora_webman_port(void)
{
	return cfg_webman_port;
}

const char *aurora_font_path(void)
{
	return cfg_font_path[0] ? cfg_font_path : APP_USRDIR "/font.ttf";
}