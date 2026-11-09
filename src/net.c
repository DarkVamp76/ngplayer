/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "net.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int mkdir_p(const char *path) {
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; if (mkdir(tmp, 0755) && errno != EEXIST) return -1; *p = '/'; }
    return (mkdir(tmp, 0755) && errno != EEXIST) ? -1 : 0;
}

int net_cache_dir(char *out, size_t cap) {
    const char *x = getenv("XDG_CACHE_HOME"), *h = getenv("HOME");
    if (x && *x) snprintf(out, cap, "%s/ngplayer", x);
    else if (h && *h) snprintf(out, cap, "%s/.cache/ngplayer", h);
    else snprintf(out, cap, "/tmp/ngplayer-cache");
    return mkdir_p(out);
}

static int run_curl(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { execvp("curl", argv); _exit(127); }
    int st;
    while (waitpid(pid, &st, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int net_have_curl(void) {
    char *argv[] = { "curl", "--version", NULL };
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) { freopen("/dev/null", "w", stdout); freopen("/dev/null", "w", stderr); execvp("curl", argv); _exit(127); }
    int st; waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

int net_get(const char *url, const char *dest, int timeout_s) {
    char part[1100], tmo[16];
    snprintf(part, sizeof part, "%s.part", dest);
    snprintf(tmo, sizeof tmo, "%d", timeout_s > 0 ? timeout_s : 30);
    char *argv[] = { "curl", "-fsSL", "--max-time", tmo, "-A", "ngPlayer/0.1", "-o", part, (char *)url, NULL };
    int r = run_curl(argv);
    if (r) { unlink(part); return r; }
    if (rename(part, dest)) { unlink(part); return -1; }
    return 0;
}

int ma_download(int id, char *out, size_t cap) {
    char dir[900], url[160];
    if (id <= 0 || net_cache_dir(dir, sizeof dir)) return -1;
    char sub[1000]; snprintf(sub, sizeof sub, "%s/modarchive", dir);
    if (mkdir_p(sub)) return -1;
    snprintf(out, cap, "%s/%d.mod", sub, id);
    struct stat sb;
    if (!stat(out, &sb) && sb.st_size > 0) return 0;                 /* Cache-Treffer */
    const char *base = getenv("NGPLAYER_MA_URL");                    /* Testhaken */
    snprintf(url, sizeof url, "%s?moduleid=%d", base ? base : "https://api.modarchive.org/downloads.php", id);
    return net_get(url, out, 60);
}

int ma_parse_id(const char *s) {
    if (!s) return 0;
    while (*s == ' ') s++;
    char *e; long v = strtol(s, &e, 10);
    if (e != s && v > 0 && (*e == 0 || *e == '\n' || *e == ' ')) return (int)v;
    const char *k[] = { "moduleid=", "query=", NULL };
    for (int i = 0; k[i]; i++) { const char *p = strstr(s, k[i]); if (p) { v = strtol(p + strlen(k[i]), &e, 10); if (v > 0) return (int)v; } }
    const char *u = strrchr(s, '_');                                 /* .../module/name.mod_12345 bzw. ..._12345 */
    if (u && strstr(s, "modarchive")) { v = strtol(u + 1, &e, 10); if (v > 0) return (int)v; }
    return 0;
}
