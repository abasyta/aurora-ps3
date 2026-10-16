/*
 * webman.c - minimal HTTP client to launch games via webMAN MOD
 *
 * webMAN MOD (https://github.com/aldostools/webMAN-MOD) runs a local web
 * server on the console. Homebrew can call it on 127.0.0.1:
 *    GET /play.ps3?path=<game path>   -> mount + launch
 *    GET /mount.ps3<path>             -> mount only (boot from XMB disc icon)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#include <ppu-lv2.h>
#include <sysmodule/sysmodule.h>

#include "webman.h"

static int tcp_get(int port, const char *request, char *out, int outsz)
{
	int s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -1;

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(0x7F000001); /* 127.0.0.1 */

	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(s);
		return -1;
	}

	if (send(s, request, strlen(request), 0) < 0) {
		close(s);
		return -1;
	}

	int total = 0;
	while (total < outsz - 1) {
		int n = recv(s, out + total, outsz - 1 - total, 0);
		if (n <= 0)
			break;
		total += n;
	}
	out[total] = 0;
	close(s);
	return total;
}

static void url_encode(const char *in, char *out, int outn)
{
	static const char hex[] = "0123456789ABCDEF";
	int o = 0;
	for (int i = 0; in[i] && o < outn - 4; i++) {
		unsigned char c = (unsigned char)in[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
		    c == '~' || c == '/') {
			out[o++] = (char)c;
		} else if (c == ' ') {
			out[o++] = '%';
			out[o++] = '2';
			out[o++] = '0';
		} else {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 0xF];
		}
	}
	out[o] = 0;
}

/* parse "HTTP/1.x <code>" from the response start */
static int response_code(const char *resp)
{
	if (strncmp(resp, "HTTP/", 5) != 0)
		return 0;
	const char *sp = strchr(resp, ' ');
	if (!sp)
		return 0;
	return atoi(sp + 1);
}

static int try_cmd(int port, const char *cmd, const char *path)
{
	char enc[1200];
	char req[1500];
	char resp[2048];

	url_encode(path, enc, sizeof(enc));
	snprintf(req, sizeof(req),
	         "GET %s%s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nUser-Agent: AuroraPS3/1.0\r\n\r\n",
	         cmd, enc);
	memset(resp, 0, sizeof(resp));
	if (tcp_get(port, req, resp, sizeof(resp)) > 0) {
		int code = response_code(resp);
		if (code >= 200 && code < 300)
			return 1;
	}
	return 0;
}

int webman_launch(const char *path, const unsigned short *ports, int nports)
{
	static int net_ready = -1;
	if (net_ready < 0) {
		s32 ret = sysModuleLoad(SYSMODULE_NET);
		net_ready = (ret == 0 || ret == 0x80012001) ? 1 : 0;
	}

	for (int i = 0; i < nports; i++) {
		if (try_cmd(ports[i], "/play.ps3?path=", path))
			return 1;
	}
	for (int i = 0; i < nports; i++) {
		if (try_cmd(ports[i], "/mount.ps3", path))
			return 2;
	}
	return 0;
}