/* http.c
 * HTTP protocol client implementation
 * (c) 2002 Mikulas Patocka
 * This file is a part of the Links program, released under GPL.
 */

#include "links.h"

struct http_connection_info {
	int bl_flags;
	int http10;
	int https_forward;
	int close;
	int send_close;
	off_t length;
	int version;
	int chunk_remaining;
};

/* prototypes */
static void http_send_header(struct connection *c);
static void http_get_header(struct connection *c);
static void test_restart(struct connection *c);
static void add_user_agent(unsigned char **hdr, int *l, unsigned char *url);
static void add_referer(unsigned char **hdr, int *l, unsigned char *url, unsigned char *prev_url);
static void add_accept(unsigned char **hdr, int *l, struct connection *c);
static void add_accept_language(unsigned char **hdr, int *l, struct http_connection_info *info);
static void add_accept_encoding(unsigned char **hdr, int *l, unsigned char *url, struct connection *c);
static void add_accept_charset(unsigned char **hdr, int *l, struct http_connection_info *info);
static void add_connection(unsigned char **hdr, int *l, int http10, int proxy, int post);
static void add_upgrade(unsigned char **hdr, int *l);
static void add_dnt(unsigned char **hdr, int *l);
static void add_if_modified(unsigned char **hdr, int *l, struct connection *c);
static void add_range(unsigned char **hdr, int *l, unsigned char *url, struct connection *c);
static void add_pragma_no_cache(unsigned char **hdr, int *l, int no_cache);
static void add_proxy_auth_string(unsigned char **hdr, int *l, unsigned char *url);
static void add_auth_string(unsigned char **hdr, int *l, unsigned char *url);
static void add_post_header(unsigned char **hdr, int *l, unsigned char **post);
static void add_extra_options(unsigned char **hdr, int *l);


/* Returns a string pointer with value of the item.
 * The string must be destroyed after usage with mem_free.
 */
unsigned char *parse_http_header(unsigned char *head, unsigned char *item, unsigned char **ptr)
{
	unsigned char *i, *f, *g, *h;
	if (!head) return NULL;
	for (f = head; *f; f++) {
		if (*f != 10) continue;
		f++;
		for (i = item; *i && *f; i++, f++)
			if (upcase(*i) != upcase(*f)) goto cont;
		if (!*f) break;
		if (f[0] == ':') {
			while (f[1] == ' ') f++;
			for (g = ++f; *g >= ' '; g++)
				;
			while (g > f && g[-1] == ' ') g--;
			h = memacpy(f, g - f);
			if (ptr) {
				*ptr = f;
			}
			return h;
		}
		cont:
		f--;
	}
	return NULL;
}

unsigned char *parse_header_param(unsigned char *x, unsigned char *e, int all)
{
	unsigned char u;
	size_t le = strlen(cast_const_char e);
	int lp;
	unsigned char *y = x;
	if (!all) {
		a:
		if (!(y = cast_uchar strchr(cast_const_char y, ';'))) return NULL;
	}
	while (*y && (*y == ';' || *y <= ' ')) y++;
	if (strlen(cast_const_char y) < le) return NULL;
	if (casecmp(y, e, le)) goto a;
	y += le;
	while (*y && (*y <= ' ' || *y == '=')) y++;
	u = ';';
	if (*y == '\'' || *y == '"') u = *y++;
	lp = 0;
	while (y[lp] >= ' ' && y[lp] != u) {
		lp++;
		if (lp == MAXINT) overalloc();
	}
	return memacpy(y, lp);
}

int get_http_code(unsigned char *head, int *code, int *version)
{
	if (!head) return -1;
	while (head[0] == ' ') head++;
	if (upcase(head[0]) != 'H' || upcase(head[1]) != 'T' || upcase(head[2]) != 'T' ||
	    upcase(head[3]) != 'P') return -1;
	if (head[4] == '/' && head[5] >= '0' && head[5] <= '9'
	 && head[6] == '.' && head[7] >= '0' && head[7] <= '9' && head[8] <= ' ') {
		if (version) *version = (head[5] - '0') * 10 + head[7] - '0';
	} else if (version) *version = 0;
	for (head += 4; *head > ' '; head++)
		;
	if (*head++ != ' ') return -1;
	if (head[0] < '1' || head [0] > '9' || head[1] < '0' || head[1] > '9' ||
	    head[2] < '0' || head [2] > '9') {
		if (code) *code = 200;
		return 0;
	}
	if (code) *code = (head[0]-'0')*100 + (head[1]-'0')*10 + head[2]-'0';
	return 0;
}

static const unsigned char *months[12] = {
	cast_uchar "Jan",
	cast_uchar "Feb",
	cast_uchar "Mar",
	cast_uchar "Apr",
	cast_uchar "May",
	cast_uchar "Jun",
	cast_uchar "Jul",
	cast_uchar "Aug",
	cast_uchar "Sep",
	cast_uchar "Oct",
	cast_uchar "Nov",
	cast_uchar "Dec"
};

#ifdef HAVE_GMTIME
static const unsigned char *days[7] = {
	cast_uchar "Sun",
	cast_uchar "Mon",
	cast_uchar "Tue",
	cast_uchar "Wed",
	cast_uchar "Thu",
	cast_uchar "Fri",
	cast_uchar "Sat",
};
#endif

time_t parse_http_date(unsigned char *date)	/* this functions is bad !!! */
{
#ifdef HAVE_MKTIME
	time_t t = 0;
	time_t offset;
	/* Mon, 03 Jan 2000 21:29:33 GMT */
	int y;
	struct tm tm;
	memset(&tm, 0, sizeof(struct tm));

	date = cast_uchar strchr(cast_const_char date, ' ');
	if (!date) return (time_t)-1;
	date++;
	if (*date >= '0' && *date <= '9') {
			/* Sun, 06 Nov 1994 08:49:37 GMT */
			/* Sunday, 06-Nov-94 08:49:37 GMT */
		y = 0;
		if (date[0] < '0' || date[0] > '9') return (time_t)-1;
		if (date[1] < '0' || date[1] > '9') return (time_t)-1;
		tm.tm_mday = (date[0] - '0') * 10 + date[1] - '0';
		date += 2;
		if (*date != ' ' && *date != '-') return (time_t)-1;
		date += 1;
		for (tm.tm_mon = 0; tm.tm_mon < 12; tm.tm_mon++)
			if (!casecmp(date, months[tm.tm_mon], 3)) goto f1;
		return (time_t)-1;
		f1:
		date += 3;
		if (*date != '-' && *date != ' ') return (time_t)-1;
		date++;
		if (date[0] < '0' || date[0] > '9') return (time_t)-1;
		if (date[1] < '0' || date[1] > '9') return (time_t)-1;
		if (date[2] == ' ') {
				/* Sunday, 06-Nov-94 08:49:37 GMT */
			if (date[0] < '0' || date[0] > '9') return (time_t)-1;
			if (date[1] < '0' || date[1] > '9') return (time_t)-1;
			tm.tm_year = (date[0] >= '7' ? 1900 : 2000) + (date[0] - '0') * 10 + date[1] - '0' - 1900;
			date += 2;
		} else if (date[2] >= '0' && date[2] <= '9' && date[3] >= '0' && date[3] <= '9') {
				/* Sun, 06 Nov 1994 08:49:37 GMT */
			if (date[0] < '0' || date[0] > '9') return (time_t)-1;
			if (date[1] < '0' || date[1] > '9') return (time_t)-1;
			if (date[2] < '0' || date[2] > '9') return (time_t)-1;
			if (date[3] < '0' || date[3] > '9') return (time_t)-1;
			tm.tm_year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + date[3] - '0' - 1900;
			date += 4;
		} else return (time_t)-1;
		if (*date != ' ') return (time_t)-1;
		date++;
	} else {
			/* Sun Nov  6 08:49:37 1994 */
		y = 1;
		for (tm.tm_mon = 0; tm.tm_mon < 12; tm.tm_mon++)
			if (!casecmp(date, months[tm.tm_mon], 3)) goto f2;
		return (time_t)-1;
		f2:
		date += 3;
		while (*date == ' ') date++;
		if (date[0] < '0' || date[0] > '9') return (time_t)-1;
		tm.tm_mday = date[0] - '0';
		date++;
		if (*date != ' ') {
			if (date[0] < '0' || date[0] > '9') return (time_t)-1;
			tm.tm_mday = tm.tm_mday * 10 + date[0] - '0';
			date++;
		}
		if (*date != ' ') return (time_t)-1;
		date++;
	}

	if (date[0] < '0' || date[0] > '9') return (time_t)-1;
	if (date[1] < '0' || date[1] > '9') return (time_t)-1;
	tm.tm_hour = (date[0] - '0') * 10 + date[1] - '0';
	date += 2;
	if (*date != ':') return (time_t)-1;
	date++;
	if (date[0] < '0' || date[0] > '9') return (time_t)-1;
	if (date[1] < '0' || date[1] > '9') return (time_t)-1;
	tm.tm_min = (date[0] - '0') * 10 + date[1] - '0';
	date += 2;
	if (*date != ':') return (time_t)-1;
	date++;
	if (date[0] < '0' || date[0] > '9') return (time_t)-1;
	if (date[1] < '0' || date[1] > '9') return (time_t)-1;
	tm.tm_sec = (date[0] - '0') * 10 + date[1] - '0';
	date += 2;
	if (y) {
		if (*date != ' ') return (time_t)-1;
		date++;
		if (date[0] < '0' || date[0] > '9') return (time_t)-1;
		if (date[1] < '0' || date[1] > '9') return (time_t)-1;
		if (date[2] < '0' || date[2] > '9') return (time_t)-1;
		if (date[3] < '0' || date[3] > '9') return (time_t)-1;
		tm.tm_year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + date[3] - '0' - 1900;
		date += 4;
	}
	if (*date != ' ' && *date) return (time_t)-1;

	/*debug("%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);*/
#if defined(HAVE_TIMEGM) && !defined(HAIKU)
	t = timegm(&tm);
	if (t == (time_t)-1)
		return (time_t)-1;
	offset = 0;
#else
	t = mktime(&tm);
	if (t == (time_t)-1)
		return (time_t)-1;
	memset(&tm, 0, sizeof(struct tm));
	tm.tm_year = 80;
	tm.tm_mday = 1;
	offset = 315532800 - mktime(&tm);
#endif
	return (time_t)((uttime)t + (uttime)offset);
#else
	return (time_t)-1;
#endif
}

unsigned char *print_http_date(time_t date)
{
	static unsigned char str[64];
#ifdef HAVE_GMTIME
	struct tm *tm = gmtime(&date);
	snprintf(cast_char str, sizeof str, "%s %s %2d %02d:%02d:%02d %04d", days[tm->tm_wday], months[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
#else
	snprintf(cast_char str, sizeof str, "%.0f", (double)date);
#endif
	return str;
}

static struct {
	char *name;
	int bugs;
} buggy_servers[] = {
	{ "mod_czech/3.1.0", BL_HTTP10 },
	{ "Purveyor", BL_HTTP10 },
	{ "Netscape-Enterprise", BL_HTTP10 | BL_NO_ACCEPT_LANGUAGE },
	{ "Apache Coyote", BL_HTTP10 },
	{ "lighttpd", BL_HTTP10 },
	{ "FORPSI", BL_NO_RANGE },
	{ "Sausalito", BL_HTTP10 },
	{ NULL, 0 }
};

static int check_http_server_bugs(unsigned char *url, struct http_connection_info *info, unsigned char *head)
{
	unsigned char *server;
	int i, bugs;
	if (!http_options.allow_blacklist || info->http10) return 0;
	if (!(server = parse_http_header(head, cast_uchar "Server", NULL))) return 0;
	bugs = 0;
	for (i = 0; buggy_servers[i].name; i++) if (strstr(cast_const_char server, cast_const_char buggy_servers[i].name)) bugs |= buggy_servers[i].bugs;
	mem_free(server);
	if (bugs && (server = get_host_name(url))) {
		add_blacklist_entry(server, bugs);
		mem_free(server);
		return bugs & ~BL_NO_RANGE;
	}
	return 0;
}

static void http_end_request(struct connection *c, int notrunc, int nokeepalive, int state)
{
	struct http_connection_info *info = c->info;
	if (state == S__OK) {
		if (c->cache) {
			if (!notrunc) truncate_entry(c->cache, c->from, 1);
			c->cache->incomplete = 0;
		}
	}
	setcstate(c, state);
	if (c->info &&
	    !info->close &&
	    !info->send_close &&
	    !nokeepalive) {
		add_keepalive_socket(c, HTTP_KEEPALIVE_TIMEOUT, 0);
	} else {
		abort_connection(c);
	}
}

void http_func(struct connection *c)
{
	/*setcstate(c, S_CONN);*/
	/*set_connection_timeout(c);*/
	if (get_keepalive_socket(c, NULL)) {
		int p;
		if ((p = get_port(c->url)) == -1) {
			setcstate(c, S_BAD_URL);
			abort_connection(c);
			return;
		}
		make_connection(c, p, &c->sock1, http_send_header);
	} else {
		http_send_header(c);
	}
}

void proxy_func(struct connection *c)
{
	http_func(c);
}

static void add_url_to_str(unsigned char **str, int *l, unsigned char *url)
{
	unsigned char *sp;
	for (sp = url; *sp && *sp != POST_CHAR; sp++) {
		if (*sp <= ' ' || *sp >= 127) {
			unsigned char esc[4];
			sprintf(cast_char esc, "%%%02X", (int)*sp);
			add_to_str(str, l, esc);
		} else {
			add_chr_to_str(str, l, *sp);
		}
	}
}

static void http_send_header(struct connection *c)
{
	struct http_connection_info *info;
	int http10 = http_options.http10;
	int proxy;
	unsigned char *hdr;
	unsigned char *h, *u;
	unsigned char *u2;
	int l = 0;
	unsigned char *post = NULL;
	unsigned char *host;

	if (!c->cache) {
		if (!find_in_cache(c->url, &c->cache))
			c->cache->refcount--;
	}

	proxy = is_proxy_url(c->url);
	host = remove_proxy_prefix(c->url);
	set_connection_timeout_keepal(c);
	info = mem_calloc(sizeof(struct http_connection_info));
	c->info = info;
#ifdef HAVE_SSL
	info->https_forward = !c->ssl && proxy && host && !casecmp(host, cast_uchar "https://", 8);
	if (c->ssl) proxy = 0;
#endif
	hdr = init_str();
	if (!host) {
		http_bad_url:
		mem_free(hdr);
		http_end_request(c, 0, 1, S_BAD_URL);
		return;
	}
	if (!info->https_forward && (h = get_host_name(host))) {
		info->bl_flags = get_blacklist_flags(h);
		mem_free(h);
	}
	if (info->bl_flags & BL_HTTP10) http10 = 1;
	info->http10 = http10;
	if (!info->https_forward) {
		post = cast_uchar strchr(cast_const_char host, POST_CHAR);
		if (post) post++;
	}
	info->send_close = info->https_forward || http10;
#ifdef HAVE_SSL
	if (info->https_forward) {
		add_to_str(&hdr, &l, cast_uchar "CONNECT ");
		h = get_host_name(host);
		if (!h) goto http_bad_url;
		add_to_str(&hdr, &l, h);
		mem_free(h);
		h = get_port_str(host);
		if (!h) h = stracpy(cast_uchar "443");
		add_chr_to_str(&hdr, &l, ':');
		add_to_str(&hdr, &l, h);
		mem_free(h);
		goto added_connect;
	} else
#endif
	if (!post) {
		add_to_str(&hdr, &l, cast_uchar "GET ");
	} else {
		add_to_str(&hdr, &l, cast_uchar "POST ");
		c->unrestartable = 2;
	}
	if (!proxy) {
		add_chr_to_str(&hdr, &l, '/');
		u = get_url_data(host);
	} else {
		u = host;
	}
	if (post && post < u) {
		goto http_bad_url;
	}
	u2 = u;
	if (proxy && !*c->socks_proxy && *proxies.dns_append) {
		unsigned char *u_host;
		int u_host_len;
		int u2_len = 0;
		if (parse_url(u, NULL, NULL, NULL, NULL, NULL, &u_host, &u_host_len, NULL, NULL, NULL, NULL, NULL)) goto http_bad_url;
		u2 = init_str();
		add_bytes_to_str(&u2, &u2_len, u, u_host + u_host_len - u);
		add_to_str(&u2, &u2_len, proxies.dns_append);
		add_to_str(&u2, &u2_len, u_host + u_host_len);
	}
	add_url_to_str(&hdr, &l, u2);
	if (u2 != u) mem_free(u2);
#ifdef HAVE_SSL
	added_connect:
#endif
	if (!http10) add_to_str(&hdr, &l, cast_uchar " HTTP/1.1\r\n");
	else add_to_str(&hdr, &l, cast_uchar " HTTP/1.0\r\n");
	if (!info->https_forward && (h = get_host_name(host))) {
		add_to_str(&hdr, &l, cast_uchar "Host: ");
		if (*h && h[strlen(cast_const_char h) - 1] == '.') {
			h[strlen(cast_const_char h) - 1] = 0;
		}
		if (h[0] == '[' && h[strlen(cast_const_char h) - 1] == ']') {
			unsigned char *pc = cast_uchar strchr(cast_const_char h, '%');
			if (pc) {
				pc[0] = ']';
				pc[1] = 0;
			}
		}
		add_to_str(&hdr, &l, h);
		mem_free(h);
		if ((h = get_port_str(host))) {
			unsigned char *default_port = cast_uchar "80";
#ifdef HAVE_SSL
			if (c->ssl) default_port = cast_uchar "443";
#endif
			if (strcmp(cast_const_char h, cast_const_char default_port)) {
				add_chr_to_str(&hdr, &l, ':');
				add_to_str(&hdr, &l, h);
			}
			mem_free(h);
		}
		add_to_str(&hdr, &l, cast_uchar "\r\n");
	}
	add_user_agent(&hdr, &l, info->https_forward ? NULL : host);
	if (proxy) add_proxy_auth_string(&hdr, &l, c->url);
	if (!info->https_forward) {
		test_restart(c);
		if (!c->doh) {
			add_referer(&hdr, &l, host, c->prev_url);
		}
		add_accept(&hdr, &l, c);
		if (!c->doh) {
			add_accept_language(&hdr, &l, info);
			add_accept_encoding(&hdr, &l, host, c);
			add_accept_charset(&hdr, &l, info);
		}
		add_dnt(&hdr, &l);
		add_connection(&hdr, &l, http10, proxy, !info->send_close);
		if (!c->doh) {
			add_upgrade(&hdr, &l);
			add_if_modified(&hdr, &l, c);
			add_range(&hdr, &l, host, c);
			add_pragma_no_cache(&hdr, &l, c->no_cache);
			add_auth_string(&hdr, &l, host);
		}
		add_post_header(&hdr, &l, &post);
		if (!c->doh) {
			add_cookies(&hdr, &l, host);
		}
		add_extra_options(&hdr, &l);
	}
	add_to_str(&hdr, &l, cast_uchar "\r\n");
	if (post) {
		while (post[0] && post[1]) {
			int h1, h2;
			h1 = post[0] <= '9' ? (unsigned)post[0] - '0' : post[0] >= 'A' ? upcase(post[0]) - 'A' + 10 : 0;
			if (h1 < 0 || h1 >= 16) h1 = 0;
			h2 = post[1] <= '9' ? (unsigned)post[1] - '0' : post[1] >= 'A' ? upcase(post[1]) - 'A' + 10 : 0;
			if (h2 < 0 || h2 >= 16) h2 = 0;
			add_chr_to_str(&hdr, &l, h1 * 16 + h2);
			post += 2;
		}
	}
	write_to_socket(c, c->sock1, hdr, l, http_get_header);
	mem_free(hdr);
	setcstate(c, S_SENT);
}

static void test_restart(struct connection *c)
{
/* If the cached entity is compressed, request the whole file and turn off compression */
	if (c->cache && c->from) {
		unsigned char *d;
		if ((d = parse_http_header(c->cache->head, cast_uchar "Content-Encoding", NULL))) {
			mem_free(d);
			c->from = 0;
			c->no_compress = 1;
#ifdef HAVE_ANY_COMPRESSION
			if (c->tries >= 1) {
				unsigned char *h;
				if ((h = get_host_name(c->url))) {
					add_blacklist_entry(h, BL_NO_COMPRESSION);
					mem_free(h);
				}
			}
#endif
		}
	}
}

static void add_user_agent(unsigned char **hdr, int *l, unsigned char *url)
{
	add_to_str(hdr, l, cast_uchar "User-Agent: ");
	if (SCRUB_HEADERS) {
		add_to_str(hdr, l, cast_uchar "Mozilla/5.0 (Windows NT 10.0; rv:102.0) Gecko/20100101 Firefox/102.0\r\n");
	} else if (!(*http_options.header.fake_useragent)) {

		/*
		 * Google started to return css-styled page for searches.
		 * It returns non-css page if the user agent begins with Lynx.
		 */
#if 0
		if (url &&
			(casestrstr(url, cast_uchar "/www.google.") ||
			 casestrstr(url, cast_uchar "/google.")) &&
			strstr(cast_const_char url, "/search?") &&
			(strstr(cast_const_char url, "?q=") ||
			 strstr(cast_const_char url, "&q=")) &&
			!strstr(cast_const_char url, "?tbm=isch") &&
			!strstr(cast_const_char url, "&tbm=isch")
			)
				add_to_str(hdr, l, cast_uchar("Lynx/"));
#endif

		add_to_str(hdr, l, cast_uchar("Links (" VERSION_STRING "; "));
		add_to_str(hdr, l, system_name);
		add_to_str(hdr, l, cast_uchar "; ");
		add_to_str(hdr, l, compiler_name);
		add_to_str(hdr, l, cast_uchar "; ");
		if (!F && !list_empty(terminals)) {
			struct terminal *term;
			struct list_head *lterm;
			unsigned char *t = cast_uchar "text";
			foreach(struct terminal, term, lterm, terminals) if (term->spec->braille) t = cast_uchar "braille";
			add_to_str(hdr, l, t);
		}
#ifdef G
		else if (F && drv) {
			add_to_str(hdr, l, drv->name);
		}
#endif
		else {
			add_to_str(hdr, l, cast_uchar "dump");
		}
		add_to_str(hdr, l, cast_uchar ")\r\n");
	} else {
		add_to_str(hdr, l, http_options.header.fake_useragent);
		add_to_str(hdr, l, cast_uchar "\r\n");
	}
}

static void add_referer(unsigned char **hdr, int *l, unsigned char *url, unsigned char *prev_url)
{
	switch (http_options.header.referer) {
		case REFERER_FAKE: {
			add_to_str(hdr, l, cast_uchar "Referer: ");
			add_to_str(hdr, l, http_options.header.fake_referer);
			add_to_str(hdr, l, cast_uchar "\r\n");
			break;
		}

		case REFERER_SAME_URL: {
			add_to_str(hdr, l, cast_uchar "Referer: ");
			add_url_to_str(hdr, l, url);
			add_to_str(hdr, l, cast_uchar "\r\n");
			break;
		}

		case REFERER_REAL_SAME_SERVER: {
			unsigned char *h, *j;
			int brk = 1;
			if ((h = get_host_name(url))) {
				if ((j = get_host_name(prev_url))) {
					if (!casestrcmp(h, j)) brk = 0;
					else if (!casestrcmp(h, cast_uchar "imageproxy.jxs.cz")) {
						int l = (int)strlen(cast_const_char j);
						int q = (int)strlen(".blog.cz");
						if (l > q && !casestrcmp((j + l - q), cast_uchar ".blog.cz")) brk = 0;
						else if (!casestrcmp(j, cast_uchar "blog.cz")) brk = 0;
					} else if (!casestrcmp(h, cast_uchar "www.google.com")) {
						unsigned char *c = get_url_data(url);
						if (c && !strncmp(cast_const_char c, "recaptcha/api/", 14)) brk = 0;
					}
					mem_free(j);
				}
				mem_free(h);
			}
			if (brk) break;
		}
			/*-fallthrough*/
		case REFERER_REAL: {
			unsigned char *ref;
			unsigned char *user, *ins;
			int ulen;
			if (!prev_url) break;   /* no referrer */

			ref = stracpy(prev_url);
			if (!parse_url(ref, NULL, &user, &ulen, NULL, NULL, &ins, NULL, NULL, NULL, NULL, NULL, NULL) && ulen && ins) {
				memmove(user, ins, strlen(cast_const_char ins) + 1);
			}
			add_to_str(hdr, l, cast_uchar "Referer: ");
			add_url_to_str(hdr, l, ref);
			add_to_str(hdr, l, cast_uchar "\r\n");
			mem_free(ref);
			break;
		}
	}
}

static void add_accept(unsigned char **hdr, int *l, struct connection *c)
{
	if (c->doh) {
		add_to_str(hdr, l, cast_uchar "Accept: application/dns-message\r\n");
	} else if (SCRUB_HEADERS) {
		add_to_str(hdr, l, cast_uchar "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n");
	} else {
		add_to_str(hdr, l, cast_uchar "Accept: */*\r\n");
	}
}

static void add_accept_language(unsigned char **hdr, int *l, struct http_connection_info *info)
{
	if (!(info->bl_flags & BL_NO_ACCEPT_LANGUAGE)) {
		add_to_str(hdr, l, cast_uchar "Accept-Language: ");
		if (SCRUB_HEADERS) {
			add_to_str(hdr, l, cast_uchar "en-US,en;q=0.5\r\n");
		} else {
			int la;
			la = *l;
			add_to_str(hdr, l, get_text_translation(TEXT_(T__ACCEPT_LANGUAGE), NULL));
			add_chr_to_str(hdr, l, ',');
			if (!strstr(cast_const_char(*hdr + la), "en,") &&
			    !strstr(cast_const_char(*hdr + la), "en;")) add_to_str(hdr, l, cast_uchar "en;q=0.2,");
			add_to_str(hdr, l, cast_uchar "*;q=0.1\r\n");
		}
	}
}

#ifdef HAVE_ANY_COMPRESSION
static int advertise_compression(unsigned char *url, struct connection *c)
{
	struct http_connection_info *info = c->info;
	unsigned char *extd;
	if (c->no_compress || http_options.no_compression || info->bl_flags & BL_NO_COMPRESSION)
		return 0;

	/* Fix for bugzilla. The attachment may be compressed and if the server
	   compresses it again, we can't decompress the inner compression */
	if (strstr(cast_const_char url, "/attachment.cgi?"))
		return 0;

	extd = cast_uchar strrchr(cast_const_char url, '.');
	if (extd && get_compress_by_extension(extd + 1, cast_uchar strchr(cast_const_char(extd + 1), 0)))
		return 0;
	return 1;
}
#endif

static void add_accept_encoding(unsigned char **hdr, int *l, unsigned char *url, struct connection *c)
{
#if defined(HAVE_ANY_COMPRESSION)
#define info	((struct http_connection_info *)c->info)
	if (advertise_compression(url, c)) {
		int orig_l = *l;
		int l1;
		add_to_str(hdr, l, cast_uchar "Accept-Encoding: ");
		l1 = *l;
#if defined(HAVE_ZLIB)
		if (*l != l1) add_to_str(hdr, l, cast_uchar ", ");
		add_to_str(hdr, l, cast_uchar "gzip, deflate");
#endif
#if defined(HAVE_BROTLI)
		if ((!SCRUB_HEADERS
#ifdef HAVE_SSL
		    || c->ssl
#endif
		    ) && !(info->bl_flags & BL_NO_BZIP2)) {
			if (*l != l1) add_to_str(hdr, l, cast_uchar ", ");
			add_to_str(hdr, l, cast_uchar "br");
		}
#endif
#if defined(HAVE_ZSTD)
		if (!SCRUB_HEADERS && !(info->bl_flags & BL_NO_BZIP2)) {
			if (*l != l1) add_to_str(hdr, l, cast_uchar ", ");
			add_to_str(hdr, l, cast_uchar "zstd");
		}
#endif
#if defined(HAVE_BZIP2)
		if (!SCRUB_HEADERS && !(info->bl_flags & BL_NO_BZIP2)) {
			if (*l != l1) add_to_str(hdr, l, cast_uchar ", ");
			add_to_str(hdr, l, cast_uchar "bzip2");
		}
#endif
	/* LZMA on DOS often fails with out of memory, don't announce it */
#if defined(HAVE_LZMA) && !defined(DOS)
		if (!SCRUB_HEADERS && !(info->bl_flags & BL_NO_BZIP2)) {
			if (*l != l1) add_to_str(hdr, l, cast_uchar ", ");
			add_to_str(hdr, l, cast_uchar "lzma, lzma2");
		}
#endif
#if defined(HAVE_LZIP)
		if (!SCRUB_HEADERS && !(info->bl_flags & BL_NO_BZIP2)) {
			if (*l != l1) add_to_str(hdr, l, cast_uchar ", ");
			add_to_str(hdr, l, cast_uchar "lzip");
		}
#endif
		if (*l != l1) add_to_str(hdr, l, cast_uchar "\r\n");
		else *l = orig_l;
	}
#undef info
#endif
}

static void add_accept_charset(unsigned char **hdr, int *l, struct http_connection_info *info)
{
	static unsigned char *accept_charset = NULL;

	if (SCRUB_HEADERS ||
	    info->bl_flags & BL_NO_CHARSET ||
	    http_options.no_accept_charset)
		return;

	if (!accept_charset) {
		int i;
		unsigned char *cs, *ac;
		int aclen = 0;
		ac = init_str();
		for (i = 0; (cs = get_cp_mime_name(i)); i++) {
			if (aclen) add_chr_to_str(&ac, &aclen, ',');
			else add_to_str(&ac, &aclen, cast_uchar "Accept-Charset: ");
			add_to_str(&ac, &aclen, cs);
		}
		if (aclen) add_to_str(&ac, &aclen, cast_uchar "\r\n");
		if (!(accept_charset = cast_uchar strdup(cast_const_char ac))) {
			add_to_str(hdr, l, ac);
			mem_free(ac);
			return;
		}
		mem_free(ac);
	}
	add_to_str(hdr, l, accept_charset);
}

static void add_dnt(unsigned char **hdr, int *l)
{
	if (http_options.header.do_not_track)
		add_to_str(hdr, l, cast_uchar "DNT: 1\r\n");
}

static void add_connection(unsigned char **hdr, int *l, int http10, int proxy, int alive)
{
	if (!http10) {
		if (!proxy) add_to_str(hdr, l, cast_uchar "Connection: ");
		else add_to_str(hdr, l, cast_uchar "Proxy-Connection: ");
		if (alive) add_to_str(hdr, l, cast_uchar "keep-alive\r\n");
		else add_to_str(hdr, l, cast_uchar "close\r\n");
	}
}

static void add_upgrade(unsigned char **hdr, int *l)
{
	if (proxies.only_proxies) {
		add_to_str(hdr, l, cast_uchar "Upgrade-Insecure-Requests: 1\r\n");
	}
}

static void add_if_modified(unsigned char **hdr, int *l, struct connection *c)
{
	struct cache_entry *e;
	if ((e = c->cache)) {
		int code = 0;	/* against warning */
		if (get_http_code(e->head, &code, NULL) || code >= 400) goto skip_ifmod;
		if (!e->incomplete && e->head && c->no_cache <= NC_IF_MOD) {
			unsigned char *m;
			if (e->last_modified) m = stracpy(e->last_modified);
			else if ((m = parse_http_header(e->head, cast_uchar "Date", NULL)))
				;
			else if ((m = parse_http_header(e->head, cast_uchar "Expires", NULL)))
				;
			else goto skip_ifmod;
			add_to_str(hdr, l, cast_uchar "If-Modified-Since: ");
			add_to_str(hdr, l, m);
			add_to_str(hdr, l, cast_uchar "\r\n");
			mem_free(m);
		}
		skip_ifmod:;
	}
}

static void add_range(unsigned char **hdr, int *l, unsigned char *url, struct connection *c)
{
	struct cache_entry *e;
	struct http_connection_info *info = c->info;
	if ((e = c->cache)) {
		int code = 0;	/* against warning */
		if (!get_http_code(e->head, &code, NULL) && code >= 300)
			return;
	}
	if (c->from /*&& (c->est_length == -1 || c->from < c->est_length)*/ && c->no_cache < NC_IF_MOD && !(info->bl_flags & BL_NO_RANGE)) {
		add_to_str(hdr, l, cast_uchar "Range: bytes=");
		add_num_to_str(hdr, l, c->from);
		add_to_str(hdr, l, cast_uchar "-\r\n");
	}
}

static void add_pragma_no_cache(unsigned char **hdr, int *l, int no_cache)
{
	if (no_cache >= NC_PR_NO_CACHE) add_to_str(hdr, l, cast_uchar "Pragma: no-cache\r\nCache-Control: no-cache\r\n");
}

static void add_proxy_auth_string(unsigned char **hdr, int *l, unsigned char *url)
{
	unsigned char *h;
	if ((h = get_auth_string(url, 1))) {
		add_to_str(hdr, l, h);
		mem_free(h);
	}
}

static void add_auth_string(unsigned char **hdr, int *l, unsigned char *url)
{
	unsigned char *h;
	if ((h = get_auth_string(url, 0))) {
		add_to_str(hdr, l, h);
		mem_free(h);
	}
}

static void add_post_header(unsigned char **hdr, int *l, unsigned char **post)
{
	if (*post) {
		unsigned char *pd = cast_uchar strchr(cast_const_char *post, '\n');
		if (pd) {
			add_to_str(hdr, l, cast_uchar "Content-Type: ");
			add_bytes_to_str(hdr, l, *post, pd - *post);
			add_to_str(hdr, l, cast_uchar "\r\n");
			*post = pd + 1;
		}
		add_to_str(hdr, l, cast_uchar "Content-Length: ");
		add_num_to_str(hdr, l, strlen(cast_const_char *post) / 2);
		add_to_str(hdr, l, cast_uchar "\r\n");
	}
}

static void add_extra_options(unsigned char **hdr, int *l)
{
	unsigned char *p = http_options.header.extra_header;
	while (1) {
		unsigned char *q = p + strcspn(cast_const_char p, "\\");
		if (p != q) {
			unsigned char *c;
			unsigned char *s = memacpy(p, q - p);
			c = cast_uchar strchr(cast_const_char s, ':');
			if (c && casecmp(s, cast_uchar "Cookie:", 7)) {
				unsigned char *v = NULL; /* against warning */
				unsigned char *cc = memacpy(s, c - s);
				unsigned char *x = parse_http_header(*hdr, cc, &v);
				mem_free(cc);
				if (x) {
					unsigned char *new_hdr;
					int new_l;
					mem_free(x);
					new_hdr = init_str();
					new_l = 0;
					add_bytes_to_str(&new_hdr, &new_l, *hdr, v - *hdr);
					while (*++c == ' ')
						;
					add_to_str(&new_hdr, &new_l, c);
					add_to_str(&new_hdr, &new_l, v + strcspn(cast_const_char v, "\r\n"));
					mem_free(*hdr);
					*hdr = new_hdr;
					*l = new_l;
					goto already_added;
				}
			}
			add_to_str(hdr, l, s);
			add_to_str(hdr, l, cast_uchar "\r\n");
			already_added:
			mem_free(s);
		}
		if (!*q) break;
		p = q + 1;
	}
}

static int is_line_in_buffer(struct read_buffer *rb)
{
	int l;
	for (l = 0; l < rb->len; l++) {
		if (rb->data[l] == 10) return l + 1;
		if (l < rb->len - 1 && rb->data[l] == 13 && rb->data[l + 1] == 10) return l + 2;
		if (l == rb->len - 1 && rb->data[l] == 13) return 0;
		if (rb->data[l] < ' ') return -1;
	}
	return 0;
}

static void read_http_data(struct connection *c, struct read_buffer *rb)
{
	struct http_connection_info *info = c->info;
	int a;
	set_connection_timeout(c);
	if (rb->close == 2) {
		http_end_request(c, 0, 0, S__OK);
		return;
	}
	if (info->length != -2) {
		int l = rb->len;
		if (info->length >= 0 && info->length < l) l = (int)info->length;
		if ((off_t)(0UL + c->from + l) < 0) {
			setcstate(c, S_LARGE_FILE);
			abort_connection(c);
			return;
		}
		c->received += l;
		a = add_fragment(c->cache, c->from, rb->data, l);
		if (a < 0) {
			setcstate(c, a);
			abort_connection(c);
			return;
		}
		if (a == 1) c->tries = 0;
		if (info->length >= 0) info->length -= l;
		c->from += l;
		kill_buffer_data(rb, l);
		if (!info->length) {
			http_end_request(c, 0, 0, S__OK);
			return;
		}
	} else {
		next_chunk:
		if (info->chunk_remaining == -2) {
			int l;
			if ((l = is_line_in_buffer(rb))) {
				if (l == -1) {
					setcstate(c, S_HTTP_ERROR);
					abort_connection(c);
					return;
				}
				kill_buffer_data(rb, l);
				if (l <= 2) {
					http_end_request(c, 0, 0, S__OK);
					return;
				}
				goto next_chunk;
			}
		} else if (info->chunk_remaining == -1) {
			int l;
			if ((l = is_line_in_buffer(rb))) {
				char *end;
				long n = 0;	/* warning, go away */
				if (l != -1) n = strtol(cast_const_char rb->data, &end, 16);
				if (l == -1 || n < 0 || n >= MAXINT || cast_uchar end == rb->data) {
					setcstate(c, S_HTTP_ERROR);
					abort_connection(c);
					return;
				}
				kill_buffer_data(rb, l);
				if (!(info->chunk_remaining = (int)n)) info->chunk_remaining = -2;
				goto next_chunk;
			}
		} else {
			int l = info->chunk_remaining;
			if (l > rb->len) l = rb->len;
			if ((off_t)(0UL + c->from + l) < 0) {
				setcstate(c, S_LARGE_FILE);
				abort_connection(c);
				return;
			}
			c->received += l;
			a = add_fragment(c->cache, c->from, rb->data, l);
			if (a < 0) {
				setcstate(c, a);
				abort_connection(c);
				return;
			}
			if (a == 1) c->tries = 0;
			info->chunk_remaining -= l;
			c->from += l;
			kill_buffer_data(rb, l);
			if (!info->chunk_remaining && rb->len >= 1) {
				if (rb->data[0] == 10) kill_buffer_data(rb, 1);
				else {
					if (rb->data[0] != 13 || (rb->len >= 2 && ((unsigned char *)rb->data)[1] != 10)) {
						setcstate(c, S_HTTP_ERROR);
						abort_connection(c);
						return;
					}
					if (rb->len < 2) goto read_more;
					kill_buffer_data(rb, 2);
				}
				info->chunk_remaining = -1;
				goto next_chunk;
			}
		}
	}
	read_more:
	read_from_socket(c, c->sock1, rb, read_http_data);
	setcstate(c, S_TRANS);
}

static int get_header(struct read_buffer *rb)
{
	int i;
	if (rb->len <= 0) return 0;
	if (rb->data[0] != 'H') return -2;
	if (rb->len <= 1) return 0;
	if (((unsigned char *)rb->data)[1] != 'T') return -2;
	if (rb->len <= 2) return 0;
	if (((unsigned char *)rb->data)[2] != 'T') return -2;
	if (rb->len <= 3) return 0;
	if (((unsigned char *)rb->data)[3] != 'P') return -2;
	for (i = 0; i < rb->len; i++) {
		unsigned char a = rb->data[i];
		if (/*a < ' ' && a != 10 && a != 13*/ !a) return -1;
		if (i < rb->len - 1 && a == 10 && rb->data[i + 1] == 10) return i + 2;
		if (i < rb->len - 3 && a == 13) {
			if (rb->data[i + 1] != 10) return -1;
			if (rb->data[i + 2] == 13) {
				if (rb->data[i + 3] != 10) return -1;
				return i + 4;
			}
		}
	}
	return 0;
}

static void http_got_header(struct connection *c, struct read_buffer *rb)
{
	off_t cf;
	int state = c->state != S_PROC ? S_GETH : S_PROC;
	unsigned char *head;
	int a, h = 0, version = 0;	/* against warning */
	unsigned char *d;
	struct cache_entry *e;
	int previous_http_code;
	struct http_connection_info *info;
	unsigned char *host = remove_proxy_prefix(c->url);
	set_connection_timeout(c);
	info = c->info;
	if (rb->close == 2) {
		unsigned char *hs;
		if (!c->tries && (hs = get_host_name(host))) {
			if (info->bl_flags & BL_NO_CHARSET) {
				del_blacklist_entry(hs, BL_NO_CHARSET);
			} else {
				add_blacklist_entry(hs, BL_NO_CHARSET);
				c->tries = -1;
			}
			mem_free(hs);
		}
		setcstate(c, S_CANT_READ);
		retry_connection(c);
		return;
	}
	rb->close = 0;
	again:
	if ((a = get_header(rb)) == -1) {
		setcstate(c, S_HTTP_ERROR);
		abort_connection(c);
		return;
	}
	if (!a) {
		read_from_socket(c, c->sock1, rb, http_got_header);
		setcstate(c, state);
		return;
	}
	if (a != -2) {
		head = memacpy(rb->data, a);
		kill_buffer_data(rb, a);
	} else {
		head = stracpy(cast_uchar "HTTP/0.9 200 OK\r\nContent-Type: text/html\r\n\r\n");
	}
	if (get_http_code(head, &h, &version) || h == 101) {
		mem_free(head);
		setcstate(c, S_HTTP_ERROR);
		abort_connection(c);
		return;
	}
	if (check_http_server_bugs(host, c->info, head) && is_connection_restartable(c)) {
		mem_free(head);
		setcstate(c, S_RESTART);
		retry_connection(c);
		return;
	}
	if (h == 100) {
		mem_free(head);
		state = S_PROC;
		goto again;
	}
	if (h < 200) {
		mem_free(head);
		setcstate(c, S_HTTP_ERROR);
		abort_connection(c);
		return;
	}
#ifdef HAVE_SSL
	if (info->https_forward && h >= 200 && h < 300) {
		mem_free(head);
		mem_free(c->info);
		c->info = 0;
		c->ssl = DUMMY;
		continue_connection(c, &c->sock1, http_send_header);
		return;
	}
	if (info->https_forward && h != 407) {
		mem_free(head);
		setcstate(c, S_HTTPS_FWD_ERROR);
		abort_connection(c);
		return;
	}
#endif
	if (h != 401 && h != 407 && !c->doh) {
		unsigned char *cookie;
		unsigned char *ch = head;
		while ((cookie = parse_http_header(ch, cast_uchar "Set-Cookie", &ch))) {
			set_cookie(host, cookie);
			mem_free(cookie);
		}
	}
	if (h == 204) {
		mem_free(head);
		http_end_request(c, 0, 0, S_HTTP_204);
		return;
	}
	if (h == 304) {
		mem_free(head);
		http_end_request(c, 1, 0, S__OK);
		return;
	}
	if (h == 416 && c->from) {
		mem_free(head);
		http_end_request(c, 0, 1, S__OK);
		return;
	}
	if (h == 431) {
		unsigned char *hs;
		if (!(info->bl_flags & BL_NO_CHARSET) && (hs = get_host_name(host))) {
			mem_free(head);
			add_blacklist_entry(hs, BL_NO_CHARSET);
			mem_free(hs);
			c->tries = -1;
			setcstate(c, S_RESTART);
			retry_connection(c);
			return;
		}
	}
	if ((h == 500 || h == 502 || h == 503 || h == 504) && http_options.retry_internal_errors && is_connection_restartable(c)) {
			/* !!! FIXME: wait some time ... */
		if (is_last_try(c)) {
			unsigned char *h;
			if ((h = get_host_name(host))) {
				add_blacklist_entry(h, BL_NO_BZIP2);
				mem_free(h);
			}
		}
		mem_free(head);
		setcstate(c, S_RESTART);
		retry_connection(c);
		return;
	}
	if (!c->cache) {
		if (get_connection_cache_entry(c)) {
			mem_free(head);
			setcstate(c, S_OUT_OF_MEM);
			abort_connection(c);
			return;
		}
		c->cache->refcount--;
	}
	e = c->cache;
	previous_http_code = e->http_code;
	e->http_code = h;
	if (e->head) mem_free(e->head);
	e->head = head;
	if (c->doh)
		e->expire_time = 1;
	if ((d = parse_http_header(head, cast_uchar "Expires", NULL))) {
		time_t t = parse_http_date(d);
		if (t != (time_t)-1 && e->expire_time != 1) e->expire_time = t;
		mem_free(d);
	}
	if ((d = parse_http_header(head, cast_uchar "Pragma", NULL))) {
		if (!casecmp(d, cast_uchar "no-cache", 8)) e->expire_time = 1;
		mem_free(d);
	}
	if ((d = parse_http_header(head, cast_uchar "Cache-Control", NULL))) {
		unsigned char *f = d;
		while (1) {
			while (*f && (*f == ' ' || *f == ',')) f++;
			if (!*f) break;
			if (!casecmp(f, cast_uchar "no-cache", 8) || !casecmp(f, cast_uchar "must-revalidate", 15)) {
				e->expire_time = 1;
			}
			if (!casecmp(f, cast_uchar "max-age=", 8)) {
				if (e->expire_time != 1) {
					e->expire_time = get_absolute_seconds();
					e->expire_time = (time_t)((uttime)e->expire_time + (uttime)atoi(cast_const_char(f + 8)));
				}
			}
			while (*f && *f != ',') f++;
		}
		mem_free(d);
	}
#ifdef HAVE_SSL
	if (c->ssl) {
		if (e->ssl_info) mem_free(e->ssl_info);
		e->ssl_info = get_cipher_string(c->ssl);
		if (e->ssl_authority) mem_free(e->ssl_authority);
		e->ssl_authority = stracpy(c->ssl->ca);
	}
#endif
	if (e->redirect) mem_free(e->redirect), e->redirect = NULL;
	if ((h == 302 || h == 303 || h == 307 || h == 511) && !e->expire_time) e->expire_time = 1;
	if (h == 301 || h == 302 || h == 303 || h == 307 || h == 308) {
		if ((d = parse_http_header(e->head, cast_uchar "Location", NULL))) {
			unsigned char *user, *ins;
			unsigned char *newuser, *newpassword;
			if (!parse_url(d, NULL, &user, NULL, NULL, NULL, &ins, NULL, NULL, NULL, NULL, NULL, NULL) && !user && ins && (newuser = get_user_name(host))) {
				if (*newuser) {
					int ins_off = (int)(ins - d);
					newpassword = get_pass(host);
					if (!newpassword) newpassword = stracpy(cast_uchar "");
					add_to_strn(&newuser, cast_uchar ":");
					add_to_strn(&newuser, newpassword);
					add_to_strn(&newuser, cast_uchar "@");
					extend_str(&d, (int)strlen(cast_const_char newuser));
					ins = d + ins_off;
					memmove(ins + strlen(cast_const_char newuser), ins, strlen(cast_const_char ins) + 1);
					memcpy(ins, newuser, strlen(cast_const_char newuser));
					mem_free(newpassword);
				}
				mem_free(newuser);
			}
			if (e->redirect) mem_free(e->redirect);
			e->redirect = d;
			if (h == 307 || h == 308) {
				unsigned char *p;
				if ((p = cast_uchar strchr(cast_const_char host, POST_CHAR)))
					add_to_strn(&e->redirect, p);
			}
		}
	}
	if (!e->expire_time && strchr(cast_const_char c->url, POST_CHAR)) e->expire_time = 1;
	info->close = 0;
	info->length = -1;
	info->version = version;
	if ((d = parse_http_header(e->head, cast_uchar "Connection", NULL)) || (d = parse_http_header(e->head, cast_uchar "Proxy-Connection", NULL))) {
		if (!casestrcmp(d, cast_uchar "close")) info->close = 1;
		mem_free(d);
	} else if (version < 11) info->close = 1;
	cf = c->from;
	c->from = 0;
	if ((d = parse_http_header(e->head, cast_uchar "Content-Range", NULL))) {
		if (strlen(cast_const_char d) > 6) {
			d[5] = 0;
			if (!(casestrcmp(d, cast_uchar "bytes")) && d[6] >= '0' && d[6] <= '9') {
				my_strtoll_t f = my_strtoll(d + 6, NULL);
				if (f >= 0 && (off_t)f >= 0 && (off_t)f == f) c->from = f;
			}
		}
		mem_free(d);
	} else if (h == 206) {
/* Hmm ... some servers send 206 partial but don't send Content-Range */
		c->from = cf;
	}
	if (cf && !c->from && !c->unrestartable) c->unrestartable = 1;
	if (c->from > cf || c->from < 0) {
		setcstate(c, S_HTTP_ERROR);
		abort_connection(c);
		return;
	}
	if ((d = parse_http_header(e->head, cast_uchar "Content-Length", NULL))) {
		unsigned char *ep;
		my_strtoll_t l = my_strtoll(d, &ep);
		if (!*ep && l >= 0 && (off_t)l >= 0 && (off_t)l == l) {
			if (!info->close || version >= 11 || h / 100 == 3) info->length = l;
			if (c->from + l >= 0) c->est_length = c->from + l;
		}
		mem_free(d);
	}
	if ((d = parse_http_header(e->head, cast_uchar "Accept-Ranges", NULL))) {
		if (!casestrcmp(d, cast_uchar "none") && !c->unrestartable) c->unrestartable = 1;
		mem_free(d);
	} else {
		if (!c->unrestartable && !c->from) c->unrestartable = 1;
	}
	if (info->bl_flags & BL_NO_RANGE && !c->unrestartable) c->unrestartable = 1;
	if ((d = parse_http_header(e->head, cast_uchar "Transfer-Encoding", NULL))) {
		if (!casestrcmp(d, cast_uchar "chunked")) {
			info->length = -2;
			info->chunk_remaining = -1;
		}
		mem_free(d);
	}
	if (!info->close && info->length == -1) info->close = 1;
	if ((d = parse_http_header(e->head, cast_uchar "Last-Modified", NULL))) {
		if (e->last_modified && casestrcmp(e->last_modified, d)) {
			delete_entry_content(e);
			if (c->from) {
				c->from = 0;
				mem_free(d);
				setcstate(c, S_MODIFIED);
				retry_connection(c);
				return;
			}
		}
		if (!e->last_modified) e->last_modified = d;
		else mem_free(d);
	}
	if (!e->last_modified && (d = parse_http_header(e->head, cast_uchar "Date", NULL)))
		e->last_modified = d;
	if (info->length == -1 || (version < 11 && info->close)) rb->close = 1;


	/*
	 * Truncate entry if:
	 *	- we are using DNS-over-HTTPS
	 *	- it is compressed (the mix of an old and new document
	 *	  would likely produce decompression error).
	 *	- it was http authentication (the user doesn't need to see the
	 *	  authentication message).
	 */
	if (c->doh) {
		truncate_entry(e, c->from, 0);
	} else if ((d = parse_http_header(e->head, cast_uchar "Content-Encoding", NULL))) {
		mem_free(d);
		truncate_entry(e, c->from, 0);
	} else if (previous_http_code == 401 || previous_http_code == 407) {
		truncate_entry(e, c->from, 0);
	}

	if (info->https_forward && h == 407) {
		http_end_request(c, 0, 1, S__OK);
		return;
	}

	read_http_data(c, rb);
}

static void http_get_header(struct connection *c)
{
	struct read_buffer *rb;
	set_connection_timeout_keepal(c);
	if (!(rb = alloc_read_buffer(c))) return;
	rb->close = 1;
	read_from_socket(c, c->sock1, rb, http_got_header);
}
