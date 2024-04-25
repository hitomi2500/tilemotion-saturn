ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT (install root directory))
endif

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

# Required for library usage
include $(YAUL_INSTALL_ROOT)/share/build.tga.mk
include $(YAUL_INSTALL_ROOT)/share/build.bcl.mk

# Each asset follows the format: <path>;<symbol>. Duplicates are removed
BUILTIN_ASSETS+= \
	assets/BOOTLOGO.BG;asset_bootlogo_bg \
	assets/BACK2.BG;asset_back2_bg \
	assets/KIKI.BG;asset_kiki_bg \
	assets/MASCOT.BG;asset_mascot_bg \

SH_PROGRAM:= tilemotion
SH_SRCS:= \
    background.c \
    bootlogo.c \
    fs.c \
    font.c \
    input.c \
    video.c \
    control.c \
    ire.c \
    help.c \
    hwtest_controller.c \
    hwtest_sysinfo.c \
    image_big_digits.c \
    image_buzzbomber.c \
    image_not_really.c \
	libsvin/svin_background.c \
	libsvin/svin.c \
	tilemotion.c

SH_CFLAGS+= -Os -I. $(TGA_CFLAGS) $(BCL_CFLAGS) -Ilibsvin -save-temps
SH_LDFLAGS+= $(TGA_LDFLAGS) $(BCL_LDFLAGS)

IP_VERSION:= V0.001
IP_RELEASE_DATE:= 20240407
IP_AREAS:= JTUBKAEL
IP_PERIPHERALS:= JAMKST
IP_TITLE:= tilemotion
IP_MASTER_STACK_ADDR:= 0x06100000
IP_SLAVE_STACK_ADDR:= 0x06001000
IP_1ST_READ_ADDR:= 0x06004000
IP_1ST_READ_SIZE:= 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
