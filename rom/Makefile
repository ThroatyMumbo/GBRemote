GBDK_HOME ?= /opt/gbdk
LCC        = $(GBDK_HOME)/bin/lcc

# -Wm-yc  CGB compatible, not CGB-only: nothing here needs CGB hardware except the palette, which
#         main.c branches on, so a DMG should boot and work rather than refuse.
# -Wm-yt0x19 MBC5, served by firmware/cart_mbc.c: bank_sel and a DMA mount the bank ~200 ns after
#         the $2000 write, so GBDK's banked calls work as they are. Never write $3000 (bank_sel
#         ignores A12), which is why nothing here uses SWITCH_ROM_MBC5.
# -autobank -Wb-min=2 sizes the ROM and packs `#pragma bank 255` files from bank 2 up. Bank 1 is
#         the firmware's window (VMU frame $4000, controller art $4600) and must stay $FF.
# -Wl-b_CODE=0x0700 moves code above every page the firmware rewrites under the running console:
#         $0010-$001F status, $0200-$02FF config mirror, $0300-$03FF driver catalog,
#         $0400-$04FF Game Boy ROM slots, $0500-$06FF map targets. GBDK's default is 0x0200,
#         squarely on the mirror.
#         tools/run.sh asserts this and checks the built image byte for byte, because an sdld map
#         cannot see ABS areas at all.
#
# -I../firmware: art.c includes firmware/padart_fmt.h verbatim rather than mirroring it the way
#         mbox.h mirrors the mailbox. Fourteen published offsets are too many to keep in step by
#         hand, and the file is nothing but #define and enum over <stdint.h>.
LCCFLAGS = -Wl-m -Wm-yc -Wm-yt0x19 -Wm-yn"GBCONTROL" -Wl-b_CODE=0x0700 \
           -autobank -Wb-ext=.rel -Wb-min=2 -I../firmware

PY  ?= ../.venv/bin/python
GEN  = generated
LCCFLAGS += -I$(GEN)

SRCS = main.c mbox.c ui.c menu.c input.c catalog.c slots.c art.c mapinfo.c canvas.c gamedb.c \
       theme.c $(GEN)/cv_tiles.c $(GEN)/ui_tiles.c \
       scr_root.c scr_ctrl.c scr_pad.c scr_map.c scr_emu.c scr_settings.c scr_diag.c scr_roms.c
HDRS = mbox.h ui.h menu.h input.h catalog.h slots.h screens.h art.h mapinfo.h canvas.h gamedb.h \
       theme.h ../firmware/padart_fmt.h $(GEN)/cv_tiles.h $(GEN)/ui_tiles.h \
       $(GEN)/cart_gfx.h $(GEN)/cart_gfx.c

controller.gb: $(SRCS) $(HDRS)
	$(LCC) $(LCCFLAGS) -o $@ $(SRCS)

$(GEN)/cv_tiles.c $(GEN)/cv_tiles.h: ../tools/mktiles.py
	$(PY) ../tools/mktiles.py $(GEN)

$(GEN)/ui_tiles.c $(GEN)/ui_tiles.h: ../tools/mkuitiles.py ../tools/mktiles.py
	$(PY) ../tools/mkuitiles.py $(GEN)

# Not in SRCS: scr_roms.c includes cart_gfx.c, so the art is linked into that screen's own bank.
$(GEN)/cart_gfx.c $(GEN)/cart_gfx.h: ../tools/mkcart.py ../demos/chao/tools/cgbpal.py
	$(PY) ../tools/mkcart.py $(GEN)

clean:
	rm -f *.o *.lst *.map *.gb *.ihx *.sym *.cdb *.adb *.asm *.noi *.rst *.rel *.lk
	rm -rf $(GEN)

.PHONY: clean
