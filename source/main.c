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
#include <sys/process.h>	/* SYS_PROCESS_PARAM */

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
	(void)button;
	dialog_done = 1;
}

/* close a currently visible diag dialog */
static void diag_close(void)
{
	msgDialogAbort();
	usleep(120000);
	sysUtilCheckCallback();
}

/* diagnostic trace dialog: waits for X, then STAYS on screen so that a
 * later freeze/crash leaves the last message visible. */
static void diag_stage(const char *msg)
{
	if (!running)
		return;
	diag_close();
	dialog_done = 0;
	msgType type = (msgType)(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_OK |
	                         MSG_DIALOG_DISABLE_CANCEL_ON);
	if (msgDialogOpen2(type, msg, dialog_handler, NULL, NULL) != 0)
		return;
	while (!dialog_done && running) {
		sysUtilCheckCallback();
		usleep(16000);
	}
}

/* show a fatal message and stop (dialog stays forever) */
static void diag_fatal(const char *msg)
{
	diag_close();
	dialog_done = 0;
	msgType type = (msgType)(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_OK |
	                         MSG_DIALOG_DISABLE_CANCEL_ON);
	if (msgDialogOpen2(type, msg, dialog_handler, NULL, NULL) == 0) {
		while (running) {
			sysUtilCheckCallback();
			usleep(16000);
		}
	}
	for (;;)
		usleep(500000);
}

/* show an informational dialog (about/details/mount hints) */
static void show_dialog(const char *text)
{
	diag_close();
	dialog_done = 0;
	msgType type = (msgType)(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_OK |
	                         MSG_DIALOG_DISABLE_CANCEL_ON);
	if (msgDialogOpen2(type, text, dialog_handler, NULL, NULL) != 0)
		return;
	while (!dialog_done && running) {
		sysUtilCheckCallback();
		usleep(16000);
	}
	msgDialogAbort();
	usleep(60000);
	sysUtilCheckCallback();
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

#define K_LEFT			(1 << 0)
#define K_RIGHT			(1 << 1)
#define K_UP			(1 << 2)
#define K_DOWN			(1 << 3)
#define K_CROSS			(1 << 4)
#define K_CIRCLE		(1 << 5)
#define K_SQUARE		(1 << 6)
#define K_TRIANGLE		(1 << 7)
#define K_START			(1 << 8)
#define K_L1			(1 << 9)
#define K_R1			(1 << 10)

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

		if (pd.BTN_LEFT)     b |= K_LEFT;
		if (pd.BTN_RIGHT)    b |= K_RIGHT;
		if (pd.BTN_UP)       b |= K_UP;
		if (pd.BTN_DOWN)     b |= K_DOWN;
		if (pd.BTN_CROSS)    b |= K_CROSS;
		if (pd.BTN_CIRCLE)   b |= K_CIRCLE;
		if (pd.BTN_SQUARE)   b |= K_SQUARE;
		if (pd.BTN_TRIANGLE) b |= K_TRIANGLE;
		if (pd.BTN_START)    b |= K_START;
		if (pd.BTN_L1)       b |= K_L1;
		if (pd.BTN_R1)       b |= K_R1;
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

	char tmpbuf[640];

	diag_stage("[Aurora v1.1-DIAG]\nSTAGE 1: process alive. Press X to continue.");

	/* RSX + video */
	void *host_addr = memalign(1024 * 1024, HOST_SIZE);
	if (!host_addr) {
		printf("memalign failed\n");
		diag_fatal("FATAL: memalign failed.");
	}

	context = initScreen(host_addr, HOST_SIZE);
	if (!context) {
		printf("initScreen failed\n");
		diag_fatal("FATAL: RSX/video init failed.");
	}

	{
		u16 sw = 0, sh = 0;
		if (getResolution(&sw, &sh))
			snprintf(tmpbuf, sizeof(tmpbuf),
			         "STAGE 1 OK: RSX + video ready (%ux%u).\nPress X to continue.",
			         (unsigned)sw, (unsigned)sh);
		else
			snprintf(tmpbuf, sizeof(tmpbuf),
			         "STAGE 1 OK: RSX + video ready (resolution unknown).\nPress X to continue.");
		diag_stage(tmpbuf);
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
			diag_fatal("FATAL: display buffer allocation failed.");
		}
	}
	flip(context, MAX_BUFFERS - 1);

	/* font (freetype via cairo) */
	FT_Library ft = NULL;
	FT_Face face = NULL;
	cairo_font_face_t *fontface = NULL;
	int font_ok = 0;

	if (FT_Init_FreeType(&ft) == 0) {
		const char *font = aurora_font_path();
		printf("font path: %s\n", font);
		if (FT_New_Face(ft, font, 0, &face) == 0)
			{ fontface = cairo_ft_font_face_create_for_ft_face(face, 0); font_ok = 1; }
		else
			printf("FT_New_Face failed, using default fonts\n");
	}

	ui_init(width, height);
	ui_set_font(fontface);

	snprintf(tmpbuf, sizeof(tmpbuf),
	         "STAGE 2 OK: buffers + font + UI ready.\nfont.ttf: %s\nPress X to continue.",
	         font_ok ? "loaded" : "missing (fallback font)");
	diag_stage(tmpbuf);

	/* load config + scan games */
	games_config_load();
	games_scan();
	printf("found %d games\n", g_game_count);

	snprintf(tmpbuf, sizeof(tmpbuf),
	         "STAGE 3 OK: scanning done - %d games found.\nPress X to continue.",
	         g_game_count);
	diag_stage(tmpbuf);

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
			if (pressed & (K_LEFT | K_UP))
				step = -1;
			if (pressed & (K_RIGHT | K_DOWN))
				step = 1;
			if (pressed & K_L1)
				step = -5;
			if (pressed & K_R1)
				step = 5;

			/* hold-to-repeat for left/right */
			if (!step && (cur & (K_LEFT | K_RIGHT))) {
				repeat_timer++;
				unsigned period = (repeat_timer > 30) ? 7 : 22;
				if (repeat_timer > 16 && (repeat_timer % period) == 0)
					step = (cur & K_LEFT) ? -1 : 1;
			} else if (step)
				repeat_timer = 0;

			if (step) {
				sel = (sel + step + count) % count;
			}
		}

		if (pressed & K_CROSS)
			do_launch(sel);
		if (pressed & K_SQUARE)
			do_details(sel);
		if (pressed & K_CIRCLE)
			do_about();
		if (pressed & K_TRIANGLE) {
			games_free();
			games_scan();
			sel = 0;
			pos = 0.0f;
		}
		if (pressed & K_START)
			running = 0;

		prev = cur;

		/* ---- animation ---- */
		pos += ((float)sel - pos) * 0.20f;
		if (pos > sel - 0.002f && pos < sel + 0.002f)
			pos = (float)sel;

		/* ---- covers around selection (deferred until after frame 1) ---- */
		int c = (int)floorf(pos + 0.5f);
		if (frame > 0) {
			int loaded = 0;
			for (int i = c - 10; i <= c + 10 && loaded < 2; i++)
				if (i >= 0 && i < g_game_count) {
					GameEntry *gg = &g_games[i];
					if (!gg->surf && !gg->loadfail) {
						game_cover_get(i);
						loaded++;
					}
				}
		}
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

		if (frame == 1) {
			diag_stage("STAGE 4 OK: first frame drawn.\nPress X to continue (cover loading now enabled).");
			diag_close();
		}
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