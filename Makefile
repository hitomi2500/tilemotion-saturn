ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT (install root directory))
endif

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

# Required for library usage
include $(YAUL_INSTALL_ROOT)/share/build.tga.mk
include $(YAUL_INSTALL_ROOT)/share/build.bcl.mk

SH_PROGRAM:= tilemotion
SH_SRCS:= \
	tilemotion.c \
	cd-block_multiread.c \
	libsvin\svin_background.c \
	libsvin\svin_cd_access.c \
	libsvin\svin_filelist.c \
	libsvin\svin_textbox.c \
	libsvin\svin_text.c \
	libsvin\mcufont\mf_bwfont.c \
	libsvin\mcufont\mf_encoding.c \
	libsvin\mcufont\mf_font.c \
	libsvin\mcufont\mf_justify.c \
	libsvin\mcufont\mf_kerning.c \
	libsvin\mcufont\mf_rlefont.c \
	libsvin\mcufont\mf_scaledfont.c \
	libsvin\mcufont\mf_wordwrap.c \
	libsvin\svin.c

SH_CFLAGS+= -O2 -I. $(TGA_CFLAGS) $(BCL_CFLAGS) -Ilibsvin -Ilibsvin/mcufont -save-temps=obj
SH_LDFLAGS+= $(TGA_LDFLAGS) $(BCL_LDFLAGS)

IP_VERSION:= V0.001
IP_RELEASE_DATE:= 20180214
IP_AREAS:= JTUBKAEL
IP_PERIPHERALS:= JAMKST
IP_TITLE:= Tilemotion
IP_MASTER_STACK_ADDR:= 0x06004000
IP_SLAVE_STACK_ADDR:= 0x06001000
IP_1ST_READ_ADDR:= 0x06004000
IP_1ST_READ_SIZE:= 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
