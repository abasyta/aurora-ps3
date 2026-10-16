/*
 * games.h - game discovery for Aurora Coverflow PS3
 */

#ifndef __GAMES_H__
#define __GAMES_H__

#include <cairo/cairo.h>
#include <ppu-types.h>

#define GAMES_MAX			256
#define GAMES_PATH_MAX		512

enum game_type {
	GAME_BACKUP = 0,	/* folder backup (PS3_GAME layout) or disc */
	GAME_INSTALLED		/* PKG installed under /dev_hdd0/game */
};

typedef struct
{
	char		title[128];
	char		id[16];			/* TITLE_ID e.g. BLUS31204 */
	char		category[4];	/* HG / DG / MN / EW ... */
	int			type;			/* game_type */
	char		path[GAMES_PATH_MAX];	/* game base dir (mountable) */
	char		cover[600];		/* full path of ICON0.PNG (may be empty) */
	int			has_cover;

	/* lazily loaded cover surface */
	cairo_surface_t	*surf;
	int				surf_w, surf_h;
	s32				loadfail;		/* failed to load, do not retry */
} GameEntry;

extern GameEntry g_games[GAMES_MAX];
extern int g_game_count;

/* scan all configured locations; rescans games */
void games_scan(void);

/* reload the aurora.ini config (also rescanned by scan) */
void games_config_load(void);

/* lazily load a cover into g_games[i].surf; returns the surface or NULL */
cairo_surface_t *game_cover_get(int i);

/* release cover surfaces */
void games_covers_clear(void);

/* free cover surfaces of games far away from the selection (memory saver) */
void games_covers_trim(int center, int keep_dist);

/* free everything */
void games_free(void);

/* helpers */
unsigned int aurora_webman_port(void);
const char *aurora_font_path(void);

#endif /* __GAMES_H__ */