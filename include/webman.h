/*
 * webman.h - launch games via webMAN MOD's local HTTP API
 */

#ifndef __WEBMAN_H__
#define __WEBMAN_H__

#include <ppu-types.h>

/* Try to launch/mount a game path via webMAN MOD (http://127.0.0.1).
 * ports: list of ports to try (e.g. {80, 8080}), nports = list length.
 * Returns:  1 = play request OK (game launching)
 *           2 = mount request OK (game mounted, boot it from XMB disc icon)
 *           0 = webMAN not reachable
 */
int webman_launch(const char *path, const unsigned short *ports, int nports);

#endif