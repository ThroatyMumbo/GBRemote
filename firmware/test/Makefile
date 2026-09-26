CC      ?= cc
GEN     := generated
OBJ     := obj
PIOASM  := ../build/pioasm/pioasm
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O1 -DPAD_HOST -MMD -MP \
           -I.. -I../drivers -I../links -I$(GEN) -fsanitize=address,undefined
LDFLAGS := -fsanitize=address,undefined

# The GB menu's slot parser, compiled against a host array instead of the served page at $0400, so
# the test drives the real parser rather than a second copy of it.
SLOTSRC := ../../rom/slots.c
CFLAGS  += -I../../rom -DSLOT_PAGE_PTR=g_slot_page

# The same trick for the controller picture: the menu's own parser, against the blob the packer
# really emits. __in_flash() is the SDK's section attribute and means nothing on the host.
ARTSRC  := ../../rom/art.c
CFLAGS  += -DART_WIN_PTR=g_art_win -D'__in_flash(g)='

# And for the map-target page at $0500.
MPSRC   := ../../rom/mapinfo.c
CFLAGS  += -DMP_PAGE_PTR=g_mp_page

# What a real controller and a real VMU answered, off the bus (demos/chao/dc/vmudiff.c). The frozen
# reference, never the tool's latest.txt. cwd is this directory under `make -C`.
CFLAGS  += -DMAPLE_CAPTURE='"../../demos/chao/dc/capture/real-vmu.txt"'

TESTS := test_main.c test_pad.c test_n64.c test_maple.c test_vmu.c test_snes.c test_genesis.c \
         test_capenc.c test_tpak.c test_rom.c test_serve.c

SRC := $(TESTS) pio_core.c maple_capture.c \
       wire_model.c jb_model.c maple_model.c snes_model.c gen_model.c capout_model.c \
       mouse_model.c link_model.c tpak_master.c $(SLOTSRC) $(ARTSRC) $(MPSRC) ../slot_page.c ../vmu_store_fmt.c ../vmu_lcd.c \
       ../config_map.c ../cable_id.c ../capenc.c ../pad_res.c ../pad_catalog.c ../pad_mapinfo.c \
       ../drivers/n64_proto.c ../drivers/maple_proto.c ../drivers/snes_proto.c \
       ../drivers/snesmouse_proto.c ../links/sneslink_proto.c \
       ../drivers/gen_proto.c ../drivers/gbcart_emu.c ../drivers/tpak_proto.c ../drivers/vis_proto.c

# Every PIO program the tests run on pio_core, assembled live from its source.
PIOS  := mapledev snesdev snesmouse sneslink gendev capout jbdev serve read_addr
INSNS := $(PIOS:%=$(GEN)/%_insns.h)

vpath %.c   $(sort $(dir $(SRC)))
vpath %.pio .. ../drivers ../links

check: run_tests
	./run_tests

# pioasm comes from the SDK's build tree; run.sh configures it if it is missing.
$(INSNS): $(GEN)/%_insns.h: %.pio gen_pio_insns.py
	@mkdir -p $(GEN)
	python3 gen_pio_insns.py $(PIOASM) $< $@

$(OBJ)/%.o: %.c | $(INSNS) ../generated/padart_blobs.h
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

run_tests: $(addprefix $(OBJ)/,$(notdir $(SRC:.c=.o)))
	$(CC) -o $@ $^ $(LDFLAGS)

../generated/padart_blobs.h:
	@echo "firmware/generated/padart_blobs.h is missing — run ./tools/run.sh art" >&2; exit 1

clean:
	rm -f run_tests
	rm -rf $(GEN) $(OBJ)

-include $(wildcard $(OBJ)/*.d)

.PHONY: check clean
