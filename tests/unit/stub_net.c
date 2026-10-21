// Red falsa: guarda la última URL pedida
#include "net.h"
#include <stdlib.h>
#include <string.h>
char last_url[8192];
static Req dummy;
Req *net_get(const char *url, NetLane lane) { strncpy(last_url, url, sizeof last_url - 1); return &dummy; }
Req *net_get_jpeg(const char *url, int w) { return net_get(url, LANE_IMG); }
Req *net_head(const char *url) { return net_get(url, LANE_API); }
void net_release(Req *r) {}
void netlog(const char *f, ...) {}
char *url_escape(const char *s) {
	static const char hex[] = "0123456789ABCDEF"; char *o = malloc(strlen(s) * 3 + 1), *p = o;
	for (; *s; s++) { unsigned char c = *s;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.~", c)) *p++ = c;
		else { *p++ = '%'; *p++ = hex[c >> 4]; *p++ = hex[c & 15]; } }
	*p = 0; return o; }
