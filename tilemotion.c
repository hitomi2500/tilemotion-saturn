/* 
 * 240p Test Suite for Nintendo 64
 * Copyright (C)2018 Artemio Urbina
 *
 * This file is part of the 240p Test Suite
 *
 * The 240p Test Suite is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The 240p Test Suite is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 240p Test Suite; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA	02111-1307	USA

 * 240p Test Suite for Sega Saturn
 * Copyright (c) 2018 Artemio Urbina
 * See LICENSE for details.
 * Uses libyaul
 */

#include <yaul.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "fs.h"
#include "font.h"
#include "input.h"
#include "video.h"
#include "control.h"
#include "ire.h"
#include "svin.h"
#include "background.h"

#define MENU_MAIN 0
#define MENU_PATTERNS 1
#define MENU_PATTERNS_COLOR_LEVELS 2
#define MENU_PATTERNS_GEOMETRY 3
#define MENU_VIDEO_TESTS 4
#define MENU_AUDIO_TESTS 5
#define MENU_HARDWARE_TESTS 6
#define MENU_CONFIGURATION 7
#define MENU_VIDEO_TEST_SCROLL_HV_SELECT 8

extern uint8_t asset_bootlogo_bg[];
extern uint8_t asset_bootlogo_bg_end[];

static cdfs_filelist_t _filelist;

void update_screen_mode(_svin_screen_mode_t screenmode)
{
	//ClearTextLayer();
	_svin_deinit();
	_svin_init(screenmode);
	_svin_clear_palette(0);
	LoadFont();
}

int get_fifo_fill(fifo_write_ptr,fifo_read_ptr,fifo_max)
{
	int fill = fifo_write_ptr - fifo_read_ptr;
	fill = (fill<0) ? fill+fifo_max : fill;
	fill = fill*100/fifo_max;
	return fill;
}

#define MEMCPY_32_FAST(x,y) {x[0]=y[0];x[1]=y[1];x[2]=y[2];x[3]=y[3];x[4]=y[4];x[5]=y[5];x[6]=y[6];x[7]=y[7];\
							 x[8]=y[8];x[9]=y[9];x[10]=y[10];x[11]=y[11];x[12]=y[12];x[13]=y[13];x[14]=y[14];x[15]=y[15];}

/*void _dma_transfer(int ch, void * src, void * dst, int size)
{
	cpu_dmac_channel_wait(ch);

    cpu_dmac_cfg_t cfg = {
        .channel = ch,
        .src_mode = CPU_DMAC_SOURCE_INCREMENT,
        .src = src,
        .dst_mode = CPU_DMAC_DESTINATION_INCREMENT,
        .dst = (uint32_t) dst,
        .len = size,
        .stride = CPU_DMAC_STRIDE_2_BYTES,
        .bus_mode = CPU_DMAC_BUS_MODE_BURST,
        .ihr = NULL,
        .ihr_work = NULL
    };

    cpu_dmac_channel_config_set(&cfg);
    cpu_dmac_channel_start(ch);
    cpu_dmac_channel_wait(ch);
}*/

int main(void)
{
	int sel = 0;
	bool redrawMenu = true, redrawBG = true, key_pressed = false;
	int menu_id=MENU_MAIN;
	int menu_size=0;
	char string_buf[128];
	
	int * p32;
	int x_size_tiles;
	int y_size_tiles;
	int x_shift_tiles;
	int y_shift_tiles;
	int frames_number;
	int fifo_read_index;
	int current_frame;

	int frame_tiles;
	int frame_palettes;
	int frame_commands;

	uint16_t * p16;
	uint16_t * p16_tile;
	uint32_t * p32_tile;
	uint16_t * p16_src;
	uint32_t * p32_src;
	uint16_t * p16_palette;
	uint32_t * p32_palette;
	uint8_t * p8;
	uint8_t * tmp_buf;
	uint8_t * tmp_buf_unaligned;
	uint16_t tile_index;
	uint16_t palette_index;
	int current_x;
	int current_y;
	int skip;
	int stream_frame;
	int stream_offset;
	int last_rendered_frame;
	int fifo_write_ptr;
	int fifo_read_ptr;
	int fifo_write_ptr_new;
	int fifo_read_ptr_new;
	int fifo_max;
	int sectors_ready;
	uint8_t * pHugeBuffer;
	int fifo_fill;

	tmp_buf_unaligned = malloc(2048+16);
	tmp_buf = (uint8_t *)((((int)tmp_buf_unaligned - 1)/16 + 1)*16);

	_svin_screen_mode_t screenMode =
	{
		.scanmode = _SVIN_SCANMODE_240P,
		.x_res = _SVIN_X_RESOLUTION_320,
		.y_res = VDP2_TVMD_VERT_240,
		.x_res_doubled = false,
		.colorsystem = VDP2_TVMD_TV_STANDARD_NTSC,
	};

	//show yaul logo in 240p
	_svin_init(screenMode);
	_svin_background_set_from_assets(asset_bootlogo_bg,(int)(asset_bootlogo_bg_end-asset_bootlogo_bg));

	//wait for 60 frames, either 1s or 1.2s
	for (int i=0;i<60;i++)
		vdp2_tvmd_vblank_in_next_wait(1);

	_svin_background_fade_to_black();

	_svin_clear_palette(0);
	LoadFont();

	//detect color system
	screenMode.colorsystem = vdp2_tvmd_tv_standard_get();

	//measure frame clock
	volatile int frame_counter=0;
	while (vdp2_tvmd_vblank_out())
		;
	while (vdp2_tvmd_vblank_in())
		frame_counter++;
	while (vdp2_tvmd_vblank_out())
		frame_counter++;

	//if(!fs_init())
	//	DrawString("FS INIT FAILED!\n", 120, 20, 1);
	redrawMenu = true;
	redrawBG = true;

	screenMode.x_res = _SVIN_X_RESOLUTION_352;
	_svin_init(screenMode);

	ClearTextLayer();
	DrawString("Loading data", 10, 10, FONT_GREEN);

	//using huge buffer without allocation
	pHugeBuffer = (uint8_t *)0x06030000;
	
	//finding videofile
	//Load the maximum number. We have to free the allocated filelist entries, but since we never exit, we don't have to
    cdfs_filelist_entry_t * const filelist_entries = cdfs_entries_alloc(-1);
    assert(filelist_entries != NULL);
    cdfs_config_default_set();
    cdfs_filelist_init(&_filelist, filelist_entries, -1);
    cdfs_filelist_root_read(&_filelist);

	cdfs_filelist_entry_t videofile;
	videofile.starting_fad = -2;
	for (int i=0;i<_filelist.entries_count;i++)
	{
		if (0 == memcmp(_filelist.entries[i].name,"YUI.GTS",7))
			videofile = _filelist.entries[i];
	}

	sprintf(string_buf,"Videofile fad = %i size = %i",videofile.starting_fad,videofile.size);
	DrawString(string_buf, 10, 20, FONT_GREEN);

	//starting the file playback command
    const uint32_t sector_count = (videofile.size + (CDFS_SECTOR_SIZE - 1)) / CDFS_SECTOR_SIZE;

    int ret;

    if ((ret = cd_block_cmd_selector_reset(0, 0)) != 0) {
		DrawString("cd_block_cmd_selector_reset error", 10, 30, FONT_GREEN);
        return;
    }

    if ((ret = cd_block_cmd_cd_dev_connection_set(0)) != 0) {
		DrawString("cd_block_cmd_cd_dev_connection_set error", 10, 30, FONT_GREEN);
        return;
    }

    if ((ret = cd_block_cmd_disk_play(0, videofile.starting_fad, sector_count)) != 0) {
		DrawString("cd_block_cmd_disk_play error", 10, 30, FONT_GREEN);
        return;
    }

	//pre-loading the buffer
	stream_frame=0;
	stream_offset=256;
	fifo_write_ptr = 0;
	fifo_read_ptr = 0;
	fifo_write_ptr_new = 0;
	fifo_read_ptr_new = 0;
	fifo_max = 0xC0000 / CDFS_SECTOR_SIZE;
	cpu_dmac_enable();
	while (get_fifo_fill(fifo_write_ptr,fifo_read_ptr,fifo_max)<90)
	{
		sectors_ready = cd_block_cmd_sector_number_get(0);
		if (sectors_ready > 0)
		{
			//reading only 1 sector at the moment
			if (0 == cd_block_transfer_data_dmac(0, 0, &(pHugeBuffer[fifo_write_ptr*CDFS_SECTOR_SIZE]), CDFS_SECTOR_SIZE, 0))
			{
				fifo_write_ptr++;
			}
			else
			{
				DrawString("cd_block_transfer_data_dmac error", 10, 40, FONT_GREEN);
			}
		}
	}

	fifo_write_ptr--;

	p32 = (uint32_t *)pHugeBuffer;
	x_size_tiles = p32[0]/8;
	y_size_tiles = p32[1]/8;
	x_shift_tiles = (44-x_size_tiles)/2;
	y_shift_tiles = (30-y_size_tiles)/2;
	frames_number = p32[2];
	fifo_read_index = 256;//skipping header
	current_frame = 0;

	ClearTextLayer();
	_svin_frame_count = 0;
	last_rendered_frame = -1;

	//draw black borders on VDP1 layer
	vdp1_cmdt_t * _cmd;
	_cmd = (vdp1_cmdt_t *)VDP1_VRAM(_SVIN_VDP1_ORDER_TEXT_SPRITE_1_INDEX*32);
	_cmd->cmd_xa = 0;
    _cmd->cmd_ya = 0;
    _cmd->cmd_size=((352/8)<<8)|(y_shift_tiles*8);

	_cmd = (vdp1_cmdt_t *)VDP1_VRAM(_SVIN_VDP1_ORDER_TEXT_SPRITE_2_INDEX*32);
	_cmd->cmd_xa = 0;
    _cmd->cmd_ya = (y_size_tiles + y_shift_tiles)*8;
    _cmd->cmd_size=((352/8)<<8)|(240-(y_size_tiles+y_shift_tiles)*8);

	uint16_t *p16_vram;
	vdp1_vram_partitions_t vdp1_vram_partitions;
    vdp1_vram_partitions_get(&vdp1_vram_partitions);
	p16_vram = (uint16_t *)vdp1_vram_partitions.texture_base+0x11800/2;
	for (int i=0; i<352*32; i++)
	{
		p16_vram[i] = 0x8000;
	}

	//draw fill gauge border
	for (int i=10; i<111; i++)
	{
		p16_vram[352*10+i] = 0xFFFF;
		p16_vram[352*13+i] = 0xFFFF;
	}
	p16_vram[352*10+10] = 0xFFFF;
	p16_vram[352*11+10] = 0xFFFF;
	p16_vram[352*12+10] = 0xFFFF;
	p16_vram[352*13+10] = 0xFFFF;
	p16_vram[352*10+111] = 0xFFFF;
	p16_vram[352*11+111] = 0xFFFF;
	p16_vram[352*12+111] = 0xFFFF;
	p16_vram[352*13+111] = 0xFFFF;

	
	while(stream_frame < frames_number)
	{
		//if ( _svin_frame_count - last_frame_count > 2)
		if ( ( _svin_frame_count % 3 == 0) && (_svin_frame_count != last_rendered_frame) )
		{
			last_rendered_frame = _svin_frame_count;
			/*{
				fifo_fill = get_fifo_fill(fifo_write_ptr,fifo_read_ptr,fifo_max);
				LoadFont();
				ClearTextLayer();
				sprintf(string_buf,"read = %x (%x) write = %x",fifo_read_ptr*0x800,fifo_read_index,fifo_write_ptr*0x800);
				DrawString(string_buf, 10, 50, FONT_WHITE);
				sprintf(string_buf,"frame = %i stream=%x fifo_fill = %i",stream_frame,stream_offset,fifo_fill);
				DrawString(string_buf, 10, 60, FONT_WHITE);
				while(1);	
			}*/

			//not parsing until vblank handler
			vdp2_tvmd_vblank_in_next_wait(1);

			//reading the frame info
			p32 = (int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE+fifo_read_index]));
			if (0x46524D45 != p32[0])
			{
				fifo_fill = get_fifo_fill(fifo_write_ptr,fifo_read_ptr,fifo_max);
				LoadFont();
				ClearTextLayer();
				sprintf(string_buf,"read = %x (%x) write = %x",fifo_read_ptr*0x800,fifo_read_index,fifo_write_ptr*0x800);
				DrawString(string_buf, 10, 50, FONT_WHITE);
				sprintf(string_buf,"no magic at frame = %i stream=%x fifo_fill = %i",stream_frame,stream_offset,fifo_fill);
				DrawString(string_buf, 10, 60, FONT_WHITE);
				while(1);	
			}

			frame_tiles = p32[1];
			frame_palettes = p32[2];
			frame_commands = p32[3]/4;

			fifo_read_index+=16;//frame header
			stream_offset+=16;

			//checking if we have enough data in buffer for reading
			int fifo_read_index_test = fifo_read_index + frame_tiles*34 + frame_palettes*34 + frame_commands*4;
			fifo_read_ptr_new = fifo_read_ptr;
			int fits = 1;
			while (fifo_read_index_test >= 0)
			{
				fifo_read_ptr_new = fifo_read_ptr+1;
				fifo_read_ptr_new = (fifo_read_ptr_new == fifo_max) ? 0 : fifo_read_ptr_new;
				if (fifo_read_ptr_new == fifo_write_ptr)
					fits = 0;
				fifo_read_index_test -= CDFS_SECTOR_SIZE;
			}

			if (fits)
			{
				/*if (90 == stream_frame)
				{
					LoadFont();
					ClearTextLayer();
					sprintf(string_buf,"f90, tiles = %i palettes = %i cmds = %i",frame_tiles,frame_palettes,frame_commands);
					DrawString(string_buf, 10, 50, FONT_WHITE);
					sprintf(string_buf,"f90, from %x to %x",(uint32_t)p32,((uint32_t)p32)+frame_tiles*34+frame_palettes*34+frame_commands*4);
					DrawString(string_buf, 10, 60, FONT_WHITE);
					sprintf(string_buf,"read = %x (%x) write = %x",0x06030000+fifo_read_ptr*0x800,fifo_read_index,0x06030000+fifo_write_ptr*0x800);
					DrawString(string_buf, 10, 70, FONT_WHITE);
					while(1);	
				}*/
				//reading tiles
				for (int i=0; i<frame_tiles; i++)
				{
					if (fifo_read_index + 34 < CDFS_SECTOR_SIZE) {
						p16 = (uint16_t *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE+fifo_read_index]));
						fifo_read_index+=34;
						stream_offset+=34;
					}
					else {
						//overwrapped, copying to tmp buffer
						//int size1 = fifo_read_index + 34 - CDFS_SECTOR_SIZE;
						int size1 = CDFS_SECTOR_SIZE - fifo_read_index;
						memcpy(tmp_buf,(int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE+fifo_read_index])),size1);
						//cpu_dmac_transfer_wait(1);
						//cpu_dmac_transfer(1, tmp_buf,(int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE+fifo_read_index])),size1);
						fifo_read_ptr_new = fifo_read_ptr+1;
						fifo_read_ptr_new = (fifo_read_ptr_new == fifo_max) ? 0 : fifo_read_ptr_new;
						fifo_read_ptr = fifo_read_ptr_new;
						fifo_read_index+=34;
						stream_offset+=34;
						fifo_read_index-=CDFS_SECTOR_SIZE;
						memcpy(&(tmp_buf[size1]),(int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE])),34-size1);
						//cpu_dmac_transfer(0, &(tmp_buf[size1]),(int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE])),34-size1);
						p16 = (uint16_t *)tmp_buf;
					}
					tile_index = p16[0];
					p16_tile = (uint16_t *)(_SVIN_NBG0_CHPNDR_START+tile_index*32);
					//memcpy(p16_tile,&(p16[1]),32);
					p16_src = (uint16_t *)&(p16[1]);
					MEMCPY_32_FAST(p16_tile,p16_src);
					//cpu_dmac_transfer(0,p16_tile,&(p16[1]),32);
				}

				//reading palettes
				for (int i=0; i<frame_palettes; i++)
				{
					if (fifo_read_index + 34 < CDFS_SECTOR_SIZE) {
						p16 = (uint16_t *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE+fifo_read_index]));
						fifo_read_index+=34;
						stream_offset+=34;
					}
					else {
						//overwrapped, copying to tmp buffer
						int size1 = CDFS_SECTOR_SIZE - fifo_read_index;
						memcpy(tmp_buf,(int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE+fifo_read_index])),size1);
						//cpu_dmac_transfer_wait(1);
						//cpu_dmac_transfer(1,tmp_buf,(int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE+fifo_read_index])),size1);
						fifo_read_ptr_new = fifo_read_ptr+1;
						fifo_read_ptr_new = (fifo_read_ptr_new == fifo_max) ? 0 : fifo_read_ptr_new;
						fifo_read_ptr = fifo_read_ptr_new;
						fifo_read_index+=34;
						stream_offset+=34;
						fifo_read_index-=CDFS_SECTOR_SIZE;
						memcpy(&(tmp_buf[size1]),(int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE])),34-size1);
						//cpu_dmac_transfer_wait(0);
						//cpu_dmac_transfer(0, &(tmp_buf[size1]),(int *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE])),34-size1);
						p16 = (uint16_t *)tmp_buf;
					}
					palette_index = p16[0]&0xFF;
					p16_palette = (uint16_t *)((VDP2_CRAM_ADDR(palette_index*16)));
					//memcpy(p16_palette,&(p16[1]),32);
					p16_src = (uint16_t *)&(p16[1]);
					MEMCPY_32_FAST(p16_palette,p16_src);
					//cpu_dmac_transfer(0, p16_palette,&(p16[1]),32);
				}

				//reading commands
				current_x = x_shift_tiles;
				current_y = y_shift_tiles;
				for (int i=0; i<frame_commands; i++)
				{
					p8 = (uint8_t *)(&(pHugeBuffer[fifo_read_ptr*CDFS_SECTOR_SIZE+fifo_read_index]));
					switch (p8[0])
					{
						case 0x01:
							//skip block
							p16 = (uint16_t *)p8;
							skip = p16[1]+1;
							while (skip)
							{
								current_x++;
								if (current_x == x_size_tiles)
								{
									current_y++;
									current_x = x_shift_tiles;
								}
								skip--;
							}
							fifo_read_index+=4;
							stream_offset+=4;
							break;
						case 0x00:
							//ShortTileIdx, no rotation
							p16 = (uint16_t *)p8;
							palette_index = p8[1];
							tile_index = p16[1];
							p32 = (uint32_t *)_SVIN_NBG0_PNDR_START;
							p32[current_y*64+current_x] = (palette_index << 16) | tile_index;
							current_x++;
							if (current_x == x_size_tiles)
							{
								current_y++;
								current_x = x_shift_tiles;
							}
							fifo_read_index+=4;
							stream_offset+=4;
							break;
						case 0x40:
							//ShortTileIdx, rotation
							p16 = (uint16_t *)p8;
							palette_index = p8[1];
							tile_index = p16[1];
							p32 = (uint32_t *)_SVIN_NBG0_PNDR_START;
							p32[current_y*64+current_x] = 0x40000000 | (palette_index << 16) | tile_index;
							current_x++;
							if (current_x == x_size_tiles)
							{
								current_y++;
								current_x = x_shift_tiles;
							}
							fifo_read_index+=4;
							stream_offset+=4;
							break;
						case 0x80:
							//ShortTileIdx, rotation
							p16 = (uint16_t *)p8;
							palette_index = p8[1];
							tile_index = p16[1];
							p32 = (uint32_t *)_SVIN_NBG0_PNDR_START;
							p32[current_y*64+current_x] = 0x80000000 | (palette_index << 16) | tile_index;
							current_x++;
							if (current_x == x_size_tiles)
							{
								current_y++;
								current_x = x_shift_tiles;
							}
							fifo_read_index+=4;
							stream_offset+=4;
							break;
						case 0xC0:
							//ShortTileIdx, rotation
							p16 = (uint16_t *)p8;
							palette_index = p8[1];
							tile_index = p16[1];
							p32 = (uint32_t *)_SVIN_NBG0_PNDR_START;
							p32[current_y*64+current_x] = 0xC0000000 | (palette_index << 16) | tile_index;
							current_x++;
							if (current_x == x_size_tiles)
							{
								current_y++;
								current_x = x_shift_tiles;
							}
							fifo_read_index+=4;
							stream_offset+=4;
							break;
						case 0x02:
							//frame end, advance to 256 bytes block edge
							//while (fifo_read_index%256 != 0)
							//	fifo_read_index++;
							fifo_read_index+=4;
							stream_offset+=4;
							break;
					}
					if (fifo_read_index >= CDFS_SECTOR_SIZE) {
						fifo_read_ptr_new = fifo_read_ptr+1;
						fifo_read_ptr_new = (fifo_read_ptr_new == fifo_max) ? 0 : fifo_read_ptr_new;
						fifo_read_ptr = fifo_read_ptr_new;
						fifo_read_index -= CDFS_SECTOR_SIZE;
					}
				}

				while (fifo_read_index%256 != 0)
				{
					fifo_read_index++;
					stream_offset++;
				}
			
				if (fifo_read_index >= CDFS_SECTOR_SIZE) {
					fifo_read_ptr_new = fifo_read_ptr+1;
					fifo_read_ptr_new = (fifo_read_ptr_new == fifo_max) ? 0 : fifo_read_ptr_new;
					fifo_read_ptr = fifo_read_ptr_new;
					fifo_read_index -= CDFS_SECTOR_SIZE;
				}
			}
			else
			{
				//can't read, not enough data in buffer, reverting index back to header start
				fifo_read_index -= 16;
			}

			//if (30 == stream_frame)
			//	while(1);

			stream_frame++;
		}

		/*ClearTextLayer();
		sprintf(string_buf,"read = %i (%i) write = %i",fifo_read_ptr,fifo_read_index,fifo_write_ptr);
		DrawString(string_buf, 10, 50, FONT_GREEN);
		while(1);*/

		//now filling fifo, but only if there is a place in it, and only on frame 0 and 1

		fifo_fill = get_fifo_fill(fifo_write_ptr,fifo_read_ptr,fifo_max);
			
		//while (abs(_svin_frame_count - last_frame_count) < 2)
		{
			if ( (fifo_fill < 2) && ( frames_number - stream_frame > 60 ) )
			{
				LoadFont();
				ClearTextLayer();
				sprintf(string_buf,"fill = %i read = %x (%x) write = %x",fifo_fill,fifo_read_ptr*0x800,fifo_read_index,fifo_write_ptr*0x800);
				DrawString(string_buf, 10, 50, FONT_WHITE);
				sprintf(string_buf,"low fill at frame = %i stream=%x _sfc=%i",stream_frame,stream_offset,_svin_frame_count);
				DrawString(string_buf, 10, 60, FONT_WHITE);
				sprintf(string_buf,"_svin_frame_count = %i last_rend_frame=%i",_svin_frame_count,last_rendered_frame);
				DrawString(string_buf, 10, 70, FONT_WHITE);
				while(1);	
			}
			if (fifo_fill > 98)
			{
				LoadFont();
				ClearTextLayer();
				sprintf(string_buf,"fill = %i read = %x (%x) write = %x",fifo_fill,fifo_read_ptr*0x800,fifo_read_index,fifo_write_ptr*0x800);
				DrawString(string_buf, 10, 50, FONT_WHITE);
				sprintf(string_buf,"high fill at frame = %i stream=%x _sfc=%i",stream_frame,stream_offset,_svin_frame_count);
				DrawString(string_buf, 10, 60, FONT_WHITE);
				sprintf(string_buf,"_svin_frame_count = %i last_rend_frame=%i",_svin_frame_count,last_rendered_frame);
				DrawString(string_buf, 10, 70, FONT_WHITE);
				while(1);	
			}
			if (fifo_fill < 98)
			{
				fifo_write_ptr_new = fifo_write_ptr + 1;
				fifo_write_ptr_new = (fifo_write_ptr_new >= fifo_max) ? fifo_write_ptr_new-fifo_max : fifo_write_ptr_new;
				if (fifo_write_ptr_new != fifo_read_ptr)
				{
					sectors_ready = cd_block_cmd_sector_number_get(0);
					if (sectors_ready > 0)
					{
						//reading only 1 sector at the moment
						if (0 == cd_block_transfer_data_dmac(0, 0, &(pHugeBuffer[fifo_write_ptr_new*CDFS_SECTOR_SIZE]), CDFS_SECTOR_SIZE, 0))
						{
							fifo_write_ptr = fifo_write_ptr_new;
						}
						else
						{
							LoadFont();
							ClearTextLayer();
							DrawString("cd_block_transfer_data_dmac error", 10, 40, FONT_GREEN);
							while(1);
						}				
					}
				}
			}
		}

		//ClearText(50,180,20,10);
		//sprintf(string_buf,"fill = %i",fifo_fill);
		//DrawString(string_buf, 10, 180, FONT_WHITE);

		//redraw fill gauge
		for (int i=11; i<11+fifo_fill; i++)
		{
			p16_vram[352*11+i] = 0xFFFF;
			p16_vram[352*12+i] = 0xFFFF;
		}
		for (int i=11+fifo_fill; i<111; i++)
		{
			p16_vram[352*11+i] = 0x8000;
			p16_vram[352*12+i] = 0x8000;
		}

		//if (20==stream_frame)
			//while(1);	

		//waiting for frame 2, to achieve 20 fps
		//while (abs(_svin_frame_count - last_frame_count) < 5)
		//	;

		//last_frame_count = _svin_frame_count;
	}

	fifo_fill = get_fifo_fill(fifo_write_ptr,fifo_read_ptr,fifo_max);
	LoadFont();
	ClearTextLayer();
	DrawString("Playback finished", 10, 50, FONT_GREEN);
	while(1);	
}
