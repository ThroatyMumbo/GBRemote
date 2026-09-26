// See maple_capture.h. No strtok_r: -std=c11 defines __STRICT_ANSI__, which hides it, and -Werror
// turns the implicit declaration into a build failure.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "maple_capture.h"

#define LINE_MAX 4096           // the longest line in the capture is a 512-byte block read, 1092 chars

static char g_err[160];

static int fail(const char **err, unsigned line, const char *what) {
    snprintf(g_err, sizeof g_err, "line %u: %s", line, what);
    *err = g_err;
    return -2;
}

static int hex2(const char *p, unsigned *out) {
    char *end;
    unsigned long v;
    char buf[3] = { p[0], p[1], 0 };
    if (!buf[0] || !buf[1]) return -1;
    v = strtoul(buf, &end, 16);
    if (*end) return -1;
    *out = (unsigned)v;
    return 0;
}

// "name=" then two hex digits, wherever it sits on the line.
static int field_hex8(const char *line, const char *name, uint8_t *out) {
    const char *p = strstr(line, name);
    unsigned v;
    if (!p) return -1;
    p += strlen(name);
    if (hex2(p, &v) < 0) return -1;
    *out = (uint8_t)v;
    return 0;
}

static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool mcap_key_parse(const char *key, mcap_key_t *out) {
    char base[MCAP_KEY_LEN];
    unsigned long func, block, phase;
    char *end;
    size_t n;

    memset(out, 0, sizeof *out);

    // A .head record carries the same request as its parent; only what was recorded differs.
    n = strlen(key);
    if (n > 5 && strcmp(key + n - 5, ".head") == 0) { out->head = true; n -= 5; }
    if (n >= sizeof base) return false;
    memcpy(base, key, n);
    base[n] = 0;

    if (strcmp(base, "addr")    == 0) { out->kind = MK_ADDR;    return true; }
    if (strcmp(base, "devinfo") == 0) { out->kind = MK_DEVINFO; return true; }
    if (strcmp(base, "allinfo") == 0) { out->kind = MK_ALLINFO; return true; }
    if (strcmp(base, "badcmd")  == 0) { out->kind = MK_BADCMD;  return true; }

    if (strncmp(base, "cond.f", 6) == 0) {
        func = strtoul(base + 6, &end, 16);
        if (*end) return false;
        out->kind = MK_COND;
        out->func = (uint32_t)func;
        return true;
    }

    if (strncmp(base, "minfo.f", 7) == 0) {
        func = strtoul(base + 7, &end, 16);
        if (*end != '.') return false;
        // The three partition probes vmudiff.c sends: none, low byte, high byte.
        if      (strcmp(end, ".p0")   == 0) out->arg = 0x00000000u;
        else if (strcmp(end, ".plo1") == 0) out->arg = 0x00000001u;
        else if (strcmp(end, ".phi1") == 0) out->arg = 0x01000000u;
        else return false;
        out->kind = MK_MINFO;
        out->func = (uint32_t)func;
        return true;
    }

    if (strncmp(base, "bread.f", 7) == 0) {
        func = strtoul(base + 7, &end, 16);
        if (strncmp(end, ".b", 2) != 0) return false;
        block = strtoul(end + 2, &end, 10);
        if (strncmp(end, ".ph", 3) != 0) return false;
        phase = strtoul(end + 3, &end, 10);
        if (*end) return false;
        out->kind = MK_BREAD;
        out->func = (uint32_t)func;
        // vmudiff.c's blkid(): block low byte at 31..24, high byte at 23..16, phase at 15..8.
        out->arg = (uint32_t)(((block & 0xff) << 24) | ((block >> 8) << 16) | ((phase & 0xff) << 8));
        return true;
    }

    return false;
}

static int parse_section(const char *p, mcap_t *out, unsigned line, const char **err) {
    mcap_sect_t *s;
    const char *name;
    size_t n;

    if (strncmp(p, "[dev ", 5) != 0) return fail(err, line, "section header is not [dev ...]");
    name = p + 5;
    n = strcspn(name, "]");
    if (name[n] != ']') return fail(err, line, "section header has no closing ]");
    if (n != 2 || name[0] < 'A' || name[0] > 'D' || name[1] < '0' || name[1] > '5')
        return fail(err, line, "section name is not a port letter and a unit digit");
    if (out->nsect == MCAP_MAX_SECTS) return fail(err, line, "too many sections");

    s = &out->sect[out->nsect++];
    memcpy(s->name, name, 2);
    s->name[2] = 0;
    s->port = (uint8_t)((name[0] - 'A') << 6);
    s->unit = (uint8_t)(name[1] - '0');
    s->first = out->nrec;
    s->count = 0;
    return 0;
}

static int parse_record(const char *p, mcap_t *out, unsigned line, const char **err) {
    mcap_rec_t *r;
    mcap_key_t k;
    const char *q;
    char *end;
    size_t n;

    if (out->nsect == 0) return fail(err, line, "record before any [dev] section");
    if (out->nrec == MCAP_MAX_RECS) return fail(err, line, "too many records");

    r = &out->rec[out->nrec];
    memset(r, 0, sizeof *r);
    r->line = line;
    r->sect = out->nsect - 1;
    r->stable = (*p == '=');
    p++;

    p += strspn(p, " \t");
    n = strcspn(p, " \t");
    if (n == 0 || n >= MCAP_KEY_LEN) return fail(err, line, "missing or over-long key");
    memcpy(r->key, p, n);
    r->key[n] = 0;
    if (!mcap_key_parse(r->key, &k)) return fail(err, line, "unrecognized key");

    if (field_hex8(p, "dst=", &r->dst) < 0) return fail(err, line, "no dst= field");
    if (field_hex8(p, "src=", &r->src) < 0) return fail(err, line, "no src= field");

    // The addr record is the raw address pair and carries no reply.
    if (k.kind == MK_ADDR) {
        out->nrec++;
        out->sect[r->sect].count++;
        return 0;
    }

    // The response name beside the byte is decoration: the byte is what is compared, so an
    // unfamiliar name cannot silently become a wrong expectation.
    q = strstr(p, "resp=");
    if (!q) return fail(err, line, "no resp= field");
    q = strchr(q, '(');
    if (!q) return fail(err, line, "resp= has no (hh) byte");
    {
        unsigned v;
        if (hex2(q + 1, &v) < 0) return fail(err, line, "resp= byte is not hex");
        r->resp = (uint8_t)v;
    }

    q = strstr(p, "len=");
    if (!q) return fail(err, line, "no len= field");
    {
        unsigned long words = strtoul(q + 4, &end, 10);
        if (end == q + 4 || words > 255) return fail(err, line, "len= is not a word count");
        r->nwords = (uint8_t)words;
    }

    end += strspn(end, " \t");
    while (is_hex(end[0]) && is_hex(end[1])) {
        unsigned v;
        if (r->nbytes == MCAP_MAX_DATA) return fail(err, line, "payload longer than a block read");
        if (hex2(end, &v) < 0) return fail(err, line, "bad hex pair");
        r->data[r->nbytes++] = (uint8_t)v;
        end += 2;
    }
    end += strspn(end, " \t");
    if (*end) return fail(err, line, "trailing junk after the payload (a TIMEOUT or BUSY record?)");

    // Every record self-checks, pending ones included: a truncated capture must not read as a
    // shorter reply that happens to compare equal.
    if (k.head) {
        if (r->nbytes > (unsigned)r->nwords * 4)
            return fail(err, line, ".head payload is longer than the reply it heads");
    } else if (r->nbytes != (unsigned)r->nwords * 4) {
        return fail(err, line, "payload length does not match len=");
    }

    out->nrec++;
    out->sect[r->sect].count++;
    return 0;
}

int mcap_load(const char *path, mcap_t *out, const char **err) {
    static char buf[LINE_MAX];
    FILE *f = fopen(path, "r");
    unsigned line = 0;

    *err = NULL;
    memset(out, 0, sizeof *out);
    if (!f) return -1;

    while (fgets(buf, sizeof buf, f)) {
        char *p;
        size_t n;
        int rc;

        line++;
        n = strlen(buf);
        if (n == sizeof buf - 1 && buf[n - 1] != '\n') { fclose(f); return fail(err, line, "line too long"); }
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;

        p = buf + strspn(buf, " \t");
        if (*p == 0 || *p == '#') continue;

        if (*p == '[')                  rc = parse_section(p, out, line, err);
        else if (*p == '=' || *p == '~') rc = parse_record(p, out, line, err);
        else                             rc = fail(err, line, "line starts with neither = nor ~");

        if (rc) { fclose(f); return rc; }
    }

    fclose(f);
    if (out->nsect == 0) return fail(err, line, "no [dev] section in the file");
    return 0;
}
