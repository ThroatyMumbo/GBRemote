// Minimal test macros standing in for libgimbal's GBL_TEST_* so libevmu's cases port line for line.
#pragma once
#include "vmu_core.h"
#include "vmu_internal.h"
#include <stdio.h>
#include <string.h>

typedef struct { vmu_t *v; int fails, checks; const char *name; } tctx;

#define T_CASE(name)      static void t_##name(tctx *T)
#define V                 (T->v)
#define T_FAIL(fmt, ...)  do { fprintf(stderr, "  FAIL %s:%d [%s]: " fmt "\n", __FILE__, __LINE__, T->name, ##__VA_ARGS__); T->fails++; return; } while (0)
#define CMP(a, b)         do { long a_ = (long)(a), b_ = (long)(b); T->checks++; if (a_ != b_) T_FAIL("%s == %ld (0x%lx), expected %ld (0x%lx) from %s", #a, a_, a_, b_, b_, #b); } while (0)
#define VFY(x)            do { T->checks++; if (!(x)) T_FAIL("%s is false", #x); } while (0)
#define T_CALL(x)         do { int f_ = T->fails; x; if (T->fails != f_) return; } while (0)
#define EXEC(...)         vmu_execute(V, &(const vmu_instr_t){ __VA_ARGS__ })
#define RD(a)             vmu_read(V, (uint16_t)(a))
#define WR(a, b)          vmu_write(V, (uint16_t)(a), (uint8_t)(b))
#define IND(m)            vmu_indirect_addr(V, (m))
#define PC()              (V->pc)
#define STACK_DEPTH()     vmu_stack_depth(V)
#define STACK_AT(d)       vmu_stack_at(V, (d))
#define T_RUN(fn)         do { T.name = #fn; int f_ = T.fails; t_##fn(&T); ran++; if (T.fails == f_) passed++; } while (0)
