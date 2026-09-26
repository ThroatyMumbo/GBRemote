# libevmu's instruction tests, ported onto the flat core; `make check` also runs them under ASan/UBSan.
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=gnu11
CORE    := $(wildcard ../vmu_*.c)
HDRS    := $(wildcard ../*.h)
DATE    := -DVMU_BUILD_YEAR=$(shell date +%Y) -DVMU_BUILD_MONTH=$(shell date +%-m) -DVMU_BUILD_DAY=$(shell date +%-d)
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer

all: test_cpu

test_cpu: test_cpu.c t.h $(CORE) $(HDRS)
	$(CC) $(CFLAGS) $(DATE) -I.. -o $@ test_cpu.c $(CORE)

test_cpu_san: test_cpu.c t.h $(CORE) $(HDRS)
	$(CC) $(CFLAGS) $(SAN) $(DATE) -I.. -o $@ test_cpu.c $(CORE)

check: test_cpu test_cpu_san
	./test_cpu
	./test_cpu_san

clean:
	rm -f test_cpu test_cpu_san

.PHONY: all check clean
