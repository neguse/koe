#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void koe_config_default(koe_config *c, const char *data_home) {
    snprintf(c->core, sizeof c->core, "%s/koe/voicevox_core", data_home);
    c->style = 3;
    c->device[0] = '\0';
    c->volume = 1.0;
}

/* 前後の空白を落とした範囲を [*begin, *end) で返す。 */
static void trim(const char *s, size_t n, const char **begin, const char **end) {
    while (n > 0 && isspace((unsigned char)s[0])) { s++; n--; }
    while (n > 0 && isspace((unsigned char)s[n - 1])) { n--; }
    *begin = s;
    *end = s + n;
}

static int set_string(char *dst, size_t cap, const char *b, const char *e) {
    size_t n = (size_t)(e - b);
    if (n >= cap) return -1;
    memcpy(dst, b, n);
    dst[n] = '\0';
    return 0;
}

int koe_config_parse_line(koe_config *c, const char *line) {
    const char *lb, *le, *kb, *ke, *vb, *ve;
    trim(line, strlen(line), &lb, &le);
    if (lb == le || *lb == '#') return 0;

    const char *eq = memchr(lb, '=', (size_t)(le - lb));
    if (!eq) return -1;
    trim(lb, (size_t)(eq - lb), &kb, &ke);
    trim(eq + 1, (size_t)(le - (eq + 1)), &vb, &ve);

    size_t klen = (size_t)(ke - kb);
    char val[KOE_PATH_MAX];
    if (set_string(val, sizeof val, vb, ve) != 0) return -1;

    if (klen == 4 && strncmp(kb, "core", 4) == 0) {
        return set_string(c->core, sizeof c->core, vb, ve);
    }
    if (klen == 6 && strncmp(kb, "device", 6) == 0) {
        return set_string(c->device, sizeof c->device, vb, ve);
    }
    if (klen == 5 && strncmp(kb, "style", 5) == 0) {
        char *end;
        errno = 0;
        unsigned long v = strtoul(val, &end, 10);
        if (val[0] == '\0' || *end != '\0' || errno != 0) return -1;
        c->style = (unsigned)v;
        return 0;
    }
    if (klen == 6 && strncmp(kb, "volume", 6) == 0) {
        char *end;
        errno = 0;
        double v = strtod(val, &end);
        if (val[0] == '\0' || *end != '\0' || errno != 0 || v < 0) return -1;
        c->volume = v;
        return 0;
    }
    return 0;
}

int koe_config_parse(koe_config *c, FILE *fp) {
    char line[KOE_PATH_MAX + 64];
    int rc = 0;
    while (fgets(line, sizeof line, fp)) {
        if (koe_config_parse_line(c, line) != 0) rc = -1;
    }
    return rc;
}
