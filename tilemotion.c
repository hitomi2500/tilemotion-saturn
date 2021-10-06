/*
 * Copyright (c) 2012-2016 Israel Jacquez
 * See LICENSE for details.
 *
 * Israel Jacquez <mrkotfw@gmail.com>
 */

#include <yaul.h>
#include <stdlib.h>
#include <cd-block_multiread.h>

#define VDP2_VRAM_NAMES_START_0  VDP2_VRAM_ADDR(0, 0x00000)
#define VDP2_VRAM_NAMES_START_1  VDP2_VRAM_ADDR(2, 0x00000)
#define VDP2_VRAM_TILES_START_0  VDP2_VRAM_ADDR(0, 0x02000)
#define VDP2_VRAM_TILES_START_1  VDP2_VRAM_ADDR(2, 0x02000)


static iso9660_filelist_t _filelist;
static iso9660_filelist_entry_t _filelist_entries[ISO9660_FILELIST_ENTRIES_COUNT];
iso9660_filelist_entry_t *file_entry;
static int _video_file_fad;
static int _video_file_size;
static int _last_used_fad;
static int _last_used_offset;

static void _copy_character_pattern_data(const vdp2_scrn_cell_format_t *);
static void _copy_color_palette(const vdp2_scrn_cell_format_t *);
static void _copy_map(const vdp2_scrn_cell_format_t *);
static void _prepare_video();
static void _play_video_frame();
static void _fill_video_fifo();
static uint8_t tmp_sector[2048];  

#define VIDEO_FIFO_SIZE 100
static int _video_fifo_read;
static int _video_fifo_write;
static int _video_fifo_number_of_entries;
static int  _cd_read_pending;
static uint8_t _video_fifo[2048*VIDEO_FIFO_SIZE];  

static int _vsync_counter;
static bool _play_video_frame_flag;

static void _vblank_out_handler(void *);

int
main(void)
{
        vdp2_scrn_cell_format_t format;

        format.scroll_screen = VDP2_SCRN_NBG0;
        format.cc_count = VDP2_SCRN_CCC_PALETTE_16;
        format.character_size = 1 * 1;
        format.pnd_size = 2;
        format.auxiliary_mode = 0;
        format.plane_size = 1 * 1;
        format.cp_table = (uint32_t)VDP2_VRAM_TILES_START_0;
        format.color_palette = (uint32_t)VDP2_CRAM_MODE_0_OFFSET(0, 0, 0);
        format.map_bases.plane_a = (uint32_t)VDP2_VRAM_NAMES_START_0;
        format.map_bases.plane_b = (uint32_t)VDP2_VRAM_NAMES_START_0;
        format.map_bases.plane_c = (uint32_t)VDP2_VRAM_NAMES_START_0;
        format.map_bases.plane_d = (uint32_t)VDP2_VRAM_NAMES_START_0;

        vdp2_vram_cycp_t vram_cycp;

        vram_cycp.pt[0].t0 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[0].t1 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[0].t2 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[0].t3 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[0].t4 = VDP2_VRAM_CYCP_CHPNDR_NBG0;
        vram_cycp.pt[0].t5 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[0].t6 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[0].t7 = VDP2_VRAM_CYCP_NO_ACCESS;

        vram_cycp.pt[1].t0 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[1].t1 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[1].t2 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[1].t3 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[1].t4 = VDP2_VRAM_CYCP_CHPNDR_NBG0;
        vram_cycp.pt[1].t5 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[1].t6 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[1].t7 = VDP2_VRAM_CYCP_NO_ACCESS;

        vram_cycp.pt[2].t0 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[2].t1 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[2].t2 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[2].t3 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[2].t4 = VDP2_VRAM_CYCP_CHPNDR_NBG0;
        vram_cycp.pt[2].t5 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[2].t6 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[2].t7 = VDP2_VRAM_CYCP_NO_ACCESS;

        vram_cycp.pt[3].t0 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[3].t1 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[3].t2 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[3].t3 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[3].t4 = VDP2_VRAM_CYCP_CHPNDR_NBG0;
        vram_cycp.pt[3].t5 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[3].t6 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[3].t7 = VDP2_VRAM_CYCP_NO_ACCESS;

        vdp2_vram_cycp_set(&vram_cycp);

        _copy_character_pattern_data(&format);
        _copy_color_palette(&format);
        _copy_map(&format);

        vdp2_scrn_cell_format_set(&format);
        vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 7);
        vdp2_scrn_display_set(VDP2_SCRN_NBG0, /* transparent = */ false);

        vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_B,
            VDP2_TVMD_VERT_224);
        vdp2_tvmd_display_set();

        vdp_sync();

        _prepare_video();

        _video_fifo_read = 0;
        _video_fifo_write = 0;
        _video_fifo_number_of_entries = 0;
        _cd_read_pending = 0;
        //pre-filling fifo
        while (_video_fifo_number_of_entries < 0.5*VIDEO_FIFO_SIZE)
        {
                _fill_video_fifo();
        }

        while (true) {
                _fill_video_fifo();
                if (true ==_play_video_frame_flag)
                {
                        _play_video_frame_flag = false;
                        _play_video_frame();
                }
        }	

        while(1);
}

void
user_init(void)
{
        vdp2_scrn_back_screen_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
            COLOR_RGB1555(1, 0, 0, 7));


        vdp_sync_vblank_out_set(_vblank_out_handler);

        cpu_intc_mask_set(0);

        vdp2_tvmd_display_clear();
}

static void
_copy_character_pattern_data(const vdp2_scrn_cell_format_t *format)
{
        uint8_t *cpd;
        cpd = (uint8_t *)format->cp_table;

        /*memset(cpd + 0x00, 0x00, 0x20);
        memset(cpd + 0x20, 0x11, 0x20);*/
		for (int i=0;i<32;i++)
		{
			cpd[i] = 0x11*(i%4);
			cpd[i+32] = 0x22*(i/8);
		}
}

static void
_copy_color_palette(const vdp2_scrn_cell_format_t *format)
{
        uint16_t *color_palette;
        color_palette = (uint16_t *)format->color_palette;

		for (int palette = 0;palette < 128; palette++)
		{
			for (int i=0;i<16;i++)
			{
				//color_palette[0] = COLOR_RGB_DATA | COLOR_RGB888_TO_RGB555(255, 255, 255);
				//color_palette[1] = COLOR_RGB_DATA | COLOR_RGB888_TO_RGB555(255,   0,   0);
				color_palette[palette*16+i] = COLOR_RGB_DATA | COLOR_RGB888_TO_RGB555(i*16,   palette*2,  0);
			}
		}
}

static void
_copy_map(const vdp2_scrn_cell_format_t *format)
{
        uint32_t page_width;
        page_width = VDP2_SCRN_CALCULATE_PAGE_WIDTH(format);
        uint32_t page_height;
        page_height = VDP2_SCRN_CALCULATE_PAGE_HEIGHT(format);
        uint32_t page_size;
        page_size = VDP2_SCRN_CALCULATE_PAGE_SIZE(format);

        uint32_t *planes[4];
        planes[0] = (uint32_t *)format->map_bases.plane_a;
        planes[1] = (uint32_t *)format->map_bases.plane_b;
        planes[2] = (uint32_t *)format->map_bases.plane_c;
        planes[3] = (uint32_t *)format->map_bases.plane_d;

        uint32_t *a_pages[4];
        a_pages[0] = &planes[0][0];
        a_pages[1] = &planes[0][1 * (page_size / 2)];
        a_pages[2] = &planes[0][2 * (page_size / 2)];
        a_pages[3] = &planes[0][3 * (page_size / 2)];

        uint16_t num;
        num = 0;

        uint32_t page_x;
        uint32_t page_y;
        for (page_y = 0; page_y < page_height/2; page_y++) {
                for (page_x = 0; page_x < page_width; page_x++) {
                        uint16_t page_idx;
                        page_idx = page_x + (page_width * page_y);

                        uint32_t pnd;
                        pnd = VDP2_SCRN_PND_CONFIG_8(1, (uint32_t)format->cp_table,
                            (uint32_t)(format->color_palette),
							0,page_y%2,0,0);

                        a_pages[0][page_idx] = pnd | 0x3000;// | num;

                        num ^= 1;
                }

                num ^= 1;
        }
}

void
_prepare_video(void)
{
        //open file
        _filelist.entries = _filelist_entries;
        _filelist.entries_count = 0;
        _filelist.entries_pooled_count = 0;
        iso9660_filelist_root_read(&_filelist, -1);
        _video_file_fad = 0;
        for (unsigned int i = 0; i < _filelist.entries_count; i++)
        {
                file_entry = &(_filelist.entries[i]);
                if (strcmp(file_entry->name, "V001.GTY") == 0)
                {
                        _video_file_fad = file_entry->starting_fad;
                        _video_file_size = file_entry->size;
                }
        }
        assert(_video_file_fad > 0);

        //allocating temporary buf for 1 sector
        uint16_t *tmp_sector_16 = (uint16_t *)tmp_sector;  
        //reading 1st block to find out number of blocks
        cd_block_sector_read(_video_file_fad, tmp_sector);
        //getting number of tiles
        int _tiles_number = tmp_sector_16[0];
        //allocating array for background pack index
        assert(_tiles_number < 8100); //hard limit 
        int blocks_for_tiles = (_tiles_number) / 64 + 1;
        uint8_t * _tiles_data = malloc(blocks_for_tiles * 2048);
        //reading
        cd_block_sectors_read(_video_file_fad+1,_tiles_data, ISO9660_SECTOR_SIZE*blocks_for_tiles);
        //copying to VRAM
        memcpy((void*)(VDP2_VRAM_TILES_START_0),_tiles_data,_tiles_number*32);
        _last_used_fad = _video_file_fad + blocks_for_tiles;

        _last_used_offset = 511;

        //releasing
        free(_tiles_data);

        //requesting entire video file
        cd_block_multiple_sector_read_request(_last_used_fad+1,_video_file_size/ISO9660_SECTOR_SIZE-blocks_for_tiles+1);
}

void
_play_video_frame(void)
{ 
        uint8_t *fifo_frame_8 = (uint8_t *)&_video_fifo[_video_fifo_read*2048]; 
        uint16_t *fifo_frame_16 = (uint16_t *)fifo_frame_8; 
        uint32_t *fifo_frame_32 = (uint32_t *)fifo_frame_8; 
        uint32_t * pNames = (uint32_t*)(VDP2_VRAM_NAMES_START_0);
        
        int iSkip = 0;
        int iPaletteIndex = -1;
        int iPaletteNumber = 0;
	//uint16_t color16;
	uint16_t * pCRAM = (uint16_t*)(VDP2_VRAM_ADDR(8,0));
        
        int iTileX = 0;
	int iTileY = 0;
        while (iTileY<25)
        {
                _last_used_offset ++;
                                        
                if (_last_used_offset >= 512)
                {
                        //_last_used_fad++;
                        _last_used_offset = 0;
                        //cd_block_sector_read(_last_used_fad, tmp_sector);
                        //do not read cd block, move to the next fifo entry instead
                        _video_fifo_read++;
                        _video_fifo_number_of_entries--;
                        assert (_video_fifo_number_of_entries>0);
                        if (_video_fifo_read == VIDEO_FIFO_SIZE)
                                _video_fifo_read = 0;
                        //check if fifo is not empty
                        assert (_video_fifo_read!=_video_fifo_write);
                        //update pointers
                        fifo_frame_8 = (uint8_t *)&_video_fifo[_video_fifo_read*2048]; 
                        fifo_frame_16 = (uint16_t *)fifo_frame_8; 
                        fifo_frame_32 = (uint32_t *)fifo_frame_8; 
                }
                
                if (iSkip > 0)
                {
                        iSkip--;
                        _last_used_offset --;
                        //move to next
                        iTileX++;
                        if (iTileX >= 44)
                        {
                                iTileX = 0;
                                iTileY++;
                        }
                }
                else if (iPaletteIndex >= 0)
                {
                        //to CRAM
                        pCRAM[iPaletteNumber*16+iPaletteIndex*2] = fifo_frame_16[_last_used_offset*2];
                        pCRAM[iPaletteNumber*16+iPaletteIndex*2+1] = fifo_frame_16[_last_used_offset*2+1];
                
                        iPaletteIndex++;
                        if (iPaletteIndex == 8)
                        {
                                iPaletteIndex = -1;
                        }
                }
                else if(fifo_frame_8[_last_used_offset*4] == 0x01)
                {
                        //it's a skip
                        iSkip = fifo_frame_16[_last_used_offset*2+1];
                        //move to next
                        iTileX++;
                        if (iTileX >= 44)
                        {
                                iTileX = 0;
                                iTileY++;
                        }
                }
                else if(fifo_frame_8[_last_used_offset*4] == 0x02)
                {
                        //end of frame
                        return;
                }
                else if(fifo_frame_8[_last_used_offset*4] == 0x03)
                {
                        //load palette
                        iPaletteIndex = 0;
                        iPaletteNumber = fifo_frame_8[_last_used_offset*4+3];
                }
                else
                {
                        //copy data
                        pNames[64+iTileY*64+iTileX] = fifo_frame_32[_last_used_offset];
                        //move to next
                        iTileX++;
                        if (iTileX >= 44)
                        {
                                iTileX = 0;
                                iTileY++;
                        }
                }
        }

}

void
_fill_video_fifo(void)
{ 
        uint8_t * fifo_frame_8;
        
        //trying to advance write to check if fifo is full
        int _video_fifo_write_test = _video_fifo_write + 1;
        if (_video_fifo_write_test == VIDEO_FIFO_SIZE)
                _video_fifo_write_test = 0;

        //if fifo is full, bail out
        if (_video_fifo_write_test == _video_fifo_read) 
                return;


        if (_cd_read_pending <= 0)
        {
                //if no access was pending, do a read access
                _last_used_fad++;
                //cd_block_sector_read_request(_last_used_fad);
                _last_used_fad+=19;
                //cd_block_sector_read(_last_used_fad, fifo_frame_8);
                _cd_read_pending = 20;
        }
        else
        {        
                fifo_frame_8 = (uint8_t *)&_video_fifo[_video_fifo_write_test*2048]; 
                //if (true == cd_block_sector_read_check())
                {
                        cd_block_sector_read_process(fifo_frame_8);
                        _video_fifo_write = _video_fifo_write_test;
                        _video_fifo_number_of_entries++;
                        _cd_read_pending--;
                }
        }
}

static void
_vblank_out_handler(void *work __unused)
{
        _vsync_counter++;
        if (_vsync_counter%2 == 0)
        {
                _play_video_frame_flag = true;
        }
}