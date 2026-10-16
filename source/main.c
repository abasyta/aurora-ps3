/*
 * main.c - Aurora Coverflow for PS3
 *
 * A game coverflow interface in the style of the Xbox 360 Aurora dashboard.
 * Lists installed PKG games and folder backups (PS3_GAME layout), shows
 * ICON0.PNG covers, and launches games through webMAN MOD.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <time.h>

#include <ppu-lv2.h>

#include <io/pad.h>
#include <sysutil/sysutil.h>
#include <sysutil/msg.h>
#include <sysmodule/sysmodule.h>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "rsxutil.h"
#include "ui.h"
#include "games.h"
#include "webman.h"

#define APP_ID_STR	"AURORA001"
#define APP_USRDIR	"/dev_hdd0/game/" APP_ID_STR "/USRDIR"

#define MAX_BUFFERS 2

static u32 running = 1;
static u32 exit_requested = 0;
static gcmContextData *context = NULL;
static rsxBuffer buffers[MAX_BUFFERS];
static int currentBuffer = 0;

/* ------------------------------------------------------------------ */
/* system callbacks                                                    */
/* ------------------------------------------------------------------ */

static void program_exit_callback(void)
{
	gcmSetWaitFlip(context);
	rsxFinish(context, 1);
}

static void sysutil_exit_callback(u64 status, u64 param, void *usrdata)
{
	(void)param; (void)usrdata;
	switch (status) {
	case SYSUTIL_EXIT_GAME:
		running = 0;
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* message dialogs                                                     */
/* ------------------------------------------------------------------ */

static volatile s32 dialog_done = 0;

static void dialog_handler(msgButton button, void *usrData)
{
	(void)usrData;
	switch (button) {
	case MSG_DIALOG_BTN_NONE:
		dialog_done = 1;   /* auto-close */
		break;
	default:
		dialog_done = 1;
		break;
	}
}

static void show_dialog(const char *text)
{
	dialog_done = 0;
	msgType type = (msgType)(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_OK |
	                         MSG_DIALOG_DISABLE_CANCEL_ON);
	if (msgDialogOpen2(type, text, dialog_handler, NULL, NULL) != 0)
		return;

	while (!dialog_done && running) {
		sysUtilCheckCallback();
		flip(context, buffers[currentBuffer].id);
	}
	msgDialogAbort();
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

#define BTN_LEFT		PAD_CTRL_LEFT
#define BTN_RIGHT		PAD_CTRL_RIGHT
#define BTN_UP			PAD_CTRL_UP
#define BTN_DOWN		PAD_CTRL_DOWN
#define BTN_CROSS		PAD_CTRL_CROSS
#define BTN_CIRCLE		PAD_CTRL_CIRCLE
#define BTN_SQUARE		PAD_CTRL_SQUARE
#define BTN_TRIANGLE	PAD_CTRL_TRIANGLE
#define BTN_START		PAD_CTRL_START
#define BTN_L1			PAD_CTRL_L1
#define BTN_R1			PAD_CTRL_R1

static u16 read_buttons(void)
{
	padInfo pinfo;
	padData pd;
	u16 b = 0;

	if (ioPadGetInfo(&pinfo) != 0)
		return 0;

	for (u32 i = 0; i < MAX_PORT_NUM; i++) {
		if (!pinfo.status[i])
			continue;
		memset(&pd, 0, sizeof(pd));
		if (ioPadGetData(i, &pd) != 0)
			continue;

		if (pd.BTN_LEFT)     b |= BTN_LEFT;
		if (pd.BTN_RIGHT)    b |= BTN_RIGHT;
		if (pd.BTN_UP)       b |= BTN_UP;
		if (pd.BTN_DOWN)     b |= BTN_DOWN;
		if (pd.BTN_CROSS)    b |= BTN_CROSS;
		if (pd.BTN_CIRCLE)   b |= BTN_CIRCLE;
		if (pd.BTN_SQUARE)   b |= BTN_SQUARE;
		if (pd.BTN_TRIANGLE) b |= BTN_TRIANGLE;
		if (pd.BTN_START)    b |= BTN_START;
		if (pd.BTN_L1)       b |= BTN_L1;
		if (pd.BTN_R1)       b |= BTN_R1;
		break;
	}
	return b;
}

/* ------------------------------------------------------------------ */
/* actions                                                             */
/* ------------------------------------------------------------------ */

static void do_launch(int idx)
{
	if (idx < 0 || idx >= g_game_count)
		return;

	GameEntry *g = &g_games[idx];

	unsigned short ports[3];
	int n = 0;
	unsigned int cfgport = aurora_webman_port();
	ports[n++] = (unsigned short)cfgport;
	if (cfgport != 8080) ports[n++] = 8080;
	if (cfgport != 80)   ports[n++] = 80;

	int r = webman_launch(g->path, ports, n);
	switch (r) {
	case 1:
		/* webman is booting the game; exit so the game takes over */
		exit_requested = 1;
		running = 0;
		break;
	case 2:
		show_dialog("Game mounted.\nExit this app (START), then press the disc icon on the XMB to play.");
		break;
	default:
		show_dialog("webMAN MOD was not detected.\n\n"
		            "To launch games directly from Aurora, install webMAN MOD.\n\n"
		            "For now, launch this game with multiMAN, IrisMAN,\n"
		            "or by pressing the game icon in the XMB.");
		break;
	}
}

static void do_details(int idx)
{
	if (idx < 0 || idx >= g_game_count)
		return;

	GameEntry *g = &g_games[idx];
	char info[1400];
	snprintf(info, sizeof(info),
	         "%s\n\n"
	         "ID: %s\n"
	         "Type: %s\n"
	         "Path: %s\n\n"
	         "%d of %d",
	         g->title, g->id,
	         g->type == GAME_INSTALLED ? "Installed (PKG)" : "Backup",
	         g->path, idx + 1, g_game_count);
	show_dialog(info);
}

static void do_about(void)
{
	show_dialog("Aurora Coverflow for PS3  v1.0\n\n"
	            "A game browser inspired by the Aurora dashboard.\n\n"
	            "Direct launching requires webMAN MOD.\n"
	            "Keep backups in /GAMES or /GAMEZ folders\n"
	            "with PS3_GAME inside.\n\n"
	            "Have fun!");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

SYS_PROCESS_PARAM(1001, 0x400000);

int main(int argc, const char *argv[])
{
	(void)argc; (void)argv;

	printf("Aurora Coverflow starting...\n");

	/* RSX + video */
	void *host_addr = memalign(1024 * 1024, HOST_SIZE);
	if (!host_addr) {
		printf("memalign failed\n");
		return 1;
	}

	context = initScreen(host_addr, HOST_SIZE);
	if (!context) {
		printf("initScreen failed\n");
		free(host_addr);
		return 1;
	}

	ioPadInit(7);
	sysModuleLoad(SYSMODULE_PNGDEC);
	sysModuleLoad(SYSMODULE_NET);

	atexit(program_exit_callback);
	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_exit_callback, NULL);

	/* resolution + buffers */
	u16 width = 1280, height = 720;
	getResolution(&width, &height);

	for (int i = 0; i < MAX_BUFFERS; i++) {
		if (!makeBuffer(&buffers[i], width, height, i)) {
			printf("makeBuffer failed\n");
			return 1;
		}
	}
	flip(context, MAX_BUFFERS - 1);

	/* font (freetype via cairo) */
	FT_Library ft = NULL;
	FT_Face face = NULL;
	cairo_font_face_t *fontface = NULL;

	if (FT_Init_FreeType(&ft) == 0) {
		const char *font = aurora_font_path();
		printf("font path: %s\n", font);
		if (FT_New_Face(ft, font, 0, &face) == 0)
			fontface = cairo_ft_font_face_create_for_ft_face(face, 0);
		else
			printf("FT_New_Face failed, using default fonts\n");
	}

	ui_init(width, height);
	ui_set_font(fontface);

	/* load config + scan games */
	games_config_load();
	games_scan();
	printf("found %d games\n", g_game_count);

	int sel = 0;
	float pos = 0.0f;
	unsigned frame = 0;
	u16 prev = 0;
	unsigned repeat_timer = 0;

	clock_t t0 = clock();

	while (running) {
		sysUtilCheckCallback();
		if (exit_requested)
			break;

		/* ---- input ---- */
		u16 cur = read_buttons();
		u16 pressed = cur & ~prev;
		int count = g_game_count;

		if (count > 0) {
			int step = 0;
			if (pressed & (BTN_LEFT | BTN_UP))
				step = -1;
			if (pressed & (BTN_RIGHT | BTN_DOWN))
				step = 1;
			if (pressed & BTN_L1)
				step = -5;
			if (pressed & BTN_R1)
				step = 5;

			/* hold-to-repeat for left/right */
			if (!step && (cur & (BTN_LEFT | BTN_RIGHT))) {
				repeat_timer++;
				unsigned period = (repeat_timer > 30) ? 7 : 22;
				if (repeat_timer > 16 && (repeat_timer % period) == 0)
					step = (cur & BTN_LEFT) ? -1 : 1;
			} else if (step)
				repeat_timer = 0;

			if (step) {
				sel = (sel + step + count) % count;
			}
		}

		if (pressed & BTN_CROSS)
			do_launch(sel);
		if (pressed & BTN_SQUARE)
			do_details(sel);
		if (pressed & BTN_CIRCLE)
			do_about();
		if (pressed & BTN_TRIANGLE) {
			games_free();
			games_scan();
			sel = 0;
			pos = 0.0f;
		}
		if (pressed & BTN_START)
			running = 0;

		prev = cur;

		/* ---- animation ---- */
		pos += ((float)sel - pos) * 0.20f;
		if (pos > sel - 0.002f && pos < sel + 0.002f)
			pos = (float)sel;

		/* ---- covers around selection ---- */
		int c = (int)floorf(pos + 0.5f);
		for (int i = c - 10; i <= c + 10; i++)
			if (i >= 0 && i < g_game_count)
				game_cover_get(i);
		games_covers_trim(c, 12);

		/* ---- render ---- */
		setRenderTarget(context, &buffers[currentBuffer]);
		waitFlip();

		cairo_surface_t *surf = cairo_image_surface_create_for_data(
			(unsigned char *)buffers[currentBuffer].ptr,
			CAIRO_FORMAT_RGB24, width, height, width * 4);
		if (surf) {
			cairo_t *cr = cairo_create(surf);
			ui_frame(cr, width, height, pos, (double)(clock() - t0) / CLOCKS_PER_SEC);
			cairo_destroy(cr);
			cairo_surface_finish(surf);
			cairo_surface_destroy(surf);
		}

		flip(context, buffers[currentBuffer].id);
		currentBuffer = (currentBuffer + 1) % MAX_BUFFERS;
		frame++;
	}

	/* ---- cleanup ---- */
	ui_shutdown();
	games_free();
	if (fontface)
		cairo_font_face_destroy(fontface);
	if (face)
		FT_Done_Face(face);
	if (ft)
		FT_Done_FreeType(ft);

	gcmSetWaitFlip(context);
	rsxFinish(context, 1);
	ioPadEnd();
	sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
	free(host_addr);

	printf("Aurora Coverflow done.\n");
	return 0;
}