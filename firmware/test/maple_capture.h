// Reader for demos/chao/dc/capture/real-vmu.txt, a real controller's and VMU's replies recorded off
// the bus by demos/chao/dc/vmudiff.c. Parsing only; test_maple.c asserts every record.
#ifndef MAPLE_CAPTURE_H
#define MAPLE_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#define MCAP_MAX_RECS  256
#define MCAP_MAX_SECTS 8
#define MCAP_MAX_DATA  520      // the longest payload recorded: func + block id + 512
#define MCAP_KEY_LEN   48

typedef enum {
    MK_ADDR, MK_DEVINFO, MK_ALLINFO, MK_COND, MK_MINFO, MK_BREAD, MK_BADCMD
} mcap_kind_t;

// func and arg are the console-side values the key spells, i.e. KOS's byte order. A request built
// from them needs __builtin_bswap32 — see maple_proto.h on where that transform lives.
typedef struct {
    mcap_kind_t kind;
    uint32_t    func;
    uint32_t    arg;        // minfo: the partition word. bread: the block id.
    bool        head;       // a .head record: only the structural leading bytes were recorded
} mcap_key_t;

typedef struct {
    char     key[MCAP_KEY_LEN];
    bool     stable;        // '=' — an emulation must reproduce these bytes
    uint8_t  resp;          // the response byte as it sat on the wire
    uint8_t  dst, src;      // port-masked, except on the MK_ADDR record, which is raw
    uint8_t  nwords;
    uint16_t nbytes;        // what was recorded; below nwords * 4 only for a .head record
    uint8_t  data[MCAP_MAX_DATA];
    unsigned line, sect;
} mcap_rec_t;

typedef struct {
    char     name[8];       // "B1"
    uint8_t  port;          // (letter - 'A') << 6
    uint8_t  unit;
    unsigned first, count;
} mcap_sect_t;

typedef struct {
    mcap_sect_t sect[MCAP_MAX_SECTS];
    mcap_rec_t  rec[MCAP_MAX_RECS];
    unsigned    nsect, nrec;
} mcap_t;

// 0 on success, -1 if the file is not there, -2 if it is malformed; *err then names what and where.
// mcap_t is ~140 KB, so callers hold it static rather than on the stack.
int  mcap_load(const char *path, mcap_t *out, const char **err);

bool mcap_key_parse(const char *key, mcap_key_t *out);

#endif
