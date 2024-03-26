/*
 * Copyright (c) 2012-2016 Israel Jacquez
 * See LICENSE for details.
 *
 * Israel Jacquez <mrkotfw@gmail.com>
 */

#include <yaul.h>
#include <stdlib.h>
#include <svin_cd_access.h>
#include <svin_background.h>

#define VDP2_VRAM_NAMES_START_0  VDP2_VRAM_ADDR(0, 0x00000)
#define VDP2_VRAM_NAMES_START_1  VDP2_VRAM_ADDR(2, 0x00000)
#define VDP2_VRAM_TILES_START_0  VDP2_VRAM_ADDR(0, 0x08000)
#define VDP2_VRAM_TILES_START_1  VDP2_VRAM_ADDR(2, 0x08000)

#define VIDEO_X_SIZE 88
#define VIDEO_Y_SIZE 56

static cdfs_filelist_t _filelist;
static cdfs_filelist_entry_t _filelist_entries[CDFS_FILELIST_ENTRIES_COUNT];
cdfs_filelist_entry_t *file_entry;
static int _video_file_fad;
static int _video_file_size;
static int _last_used_fad;
static int _last_used_offset;

static void _copy_character_pattern_data(const vdp2_scrn_cell_format_t *);
static void _copy_color_palette(const vdp2_scrn_cell_format_t *);
static void _copy_map(const vdp2_scrn_cell_format_t *, const vdp2_scrn_normal_map_t *);
static void _prepare_video();
static void _play_video_frame();
static void _fill_video_fifo();
static uint8_t tmp_sector[2048];  

#define VIDEO_FIFO_SIZE 256
static int _video_fifo_read;
static int _video_fifo_write;
static int _video_fifo_number_of_entries;
//static int  _cd_read_pending;
uint8_t _video_fifo[2048*VIDEO_FIFO_SIZE];  
static uint32_t _names_cache[7680];  

static int _vsync_counter = 0;
static bool _play_video_enable_flag;

static void _vblank_in_handler(void *);
static void _vblank_out_handler(void *);

static uint8_t * _video_sectors_map;  
static int _last_map_index;
static int _last_vram_address;
static uint8_t current_framebuffer;  
static uint8_t initial_framebuffer_fill;  

static int _fifo_buffers_in_file = 0;
static int _total_fifo_buffers_loaded = 0;
static int _total_frames_played = 0;

static int _update_vdp1_flag = 0;

#define _SVIN_SCREEN_WIDTH    704
#define _SVIN_SCREEN_HEIGHT   448

void _set_cycle_patterns_cpu()
{
        uint16_t * pCYCP = (uint16_t*)0x25F80010;
        pCYCP[0] = 0xEEEE;
        pCYCP[1] = 0xFFFF;
        pCYCP[2] = 0xEEEE;
        pCYCP[3] = 0xFFFF;
        pCYCP[4] = 0xEEEE;
        pCYCP[5] = 0xFFFF;
        pCYCP[6] = 0xEEEE;
        pCYCP[7] = 0xFFFF;
}

void _set_cycle_patterns_nbg()
{
        uint16_t * pCYCP = (uint16_t*)0x25F80010;
        pCYCP[0] = 0x4000;
        pCYCP[1] = 0xFFFF;
        pCYCP[2] = 0x4000;
        pCYCP[3] = 0xFFFF;
        pCYCP[4] = 0x4000;
        pCYCP[5] = 0xFFFF;
        pCYCP[6] = 0x4000;
        pCYCP[7] = 0xFFFF;
}

void _svin_background_init()
{
    vdp1_cmdt_list_t *_svin_cmdt_list;
    vdp2_sprite_priority_set(0, 4);
    vdp2_sprite_priority_set(1, 4);
    vdp2_sprite_priority_set(2, 4);
    vdp2_sprite_priority_set(3, 4);
    vdp2_sprite_priority_set(4, 4);
    vdp2_sprite_priority_set(5, 4);
    vdp2_sprite_priority_set(6, 4);
    vdp2_sprite_priority_set(7, 4);

    vdp2_tvmd_display_set();
    vdp2_sync();

    //-------------- setup VDP1 -------------------
    _svin_cmdt_list = vdp1_cmdt_list_alloc(_SVIN_VDP1_ORDER_COUNT);

    static const int16_vec2_t local_coord_ul =
        INT16_VEC2_INITIALIZER(0,
                               0);

    static const vdp1_cmdt_draw_mode_t sprite_draw_mode = {
        .raw = 0x0000,
        .pre_clipping_disable = true};

    assert(_svin_cmdt_list != NULL);

    vdp1_cmdt_t *cmdts;
    cmdts = &_svin_cmdt_list->cmdts[0];

    (void)memset(&cmdts[0], 0x00, sizeof(vdp1_cmdt_t) * _SVIN_VDP1_ORDER_COUNT);

    _svin_cmdt_list->count = _SVIN_VDP1_ORDER_COUNT;

    vdp1_cmdt_normal_sprite_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_A0_INDEX]);
    vdp1_cmdt_draw_mode_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_A0_INDEX], sprite_draw_mode);
    vdp1_cmdt_normal_sprite_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_A1_INDEX]);
    vdp1_cmdt_draw_mode_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_A1_INDEX], sprite_draw_mode);
    vdp1_cmdt_normal_sprite_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_B0_INDEX]);
    vdp1_cmdt_draw_mode_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_B0_INDEX], sprite_draw_mode);
    vdp1_cmdt_normal_sprite_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_B1_INDEX]);
    vdp1_cmdt_draw_mode_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_B1_INDEX], sprite_draw_mode);

    vdp1_cmdt_system_clip_coord_set(&cmdts[_SVIN_VDP1_ORDER_SYSTEM_CLIP_COORDS_INDEX]);
    vdp1_cmdt_jump_assign(&cmdts[_SVIN_VDP1_ORDER_SYSTEM_CLIP_COORDS_INDEX], _SVIN_VDP1_ORDER_LOCAL_COORDS_A_INDEX * 4);

    vdp1_cmdt_local_coord_set(&cmdts[_SVIN_VDP1_ORDER_LOCAL_COORDS_A_INDEX]);
    vdp1_cmdt_local_coord_set(&cmdts[_SVIN_VDP1_ORDER_LOCAL_COORDS_B_INDEX]);
    vdp1_cmdt_vtx_local_coord_set(&cmdts[_SVIN_VDP1_ORDER_LOCAL_COORDS_A_INDEX], local_coord_ul);
    vdp1_cmdt_vtx_local_coord_set(&cmdts[_SVIN_VDP1_ORDER_LOCAL_COORDS_B_INDEX], local_coord_ul);

    vdp1_cmdt_end_set(&cmdts[_SVIN_VDP1_ORDER_DRAW_END_A_INDEX]);
    vdp1_cmdt_end_set(&cmdts[_SVIN_VDP1_ORDER_DRAW_END_B_INDEX]);
    #define VDP1_FBCR_DIE (0x0008)
    MEMORY_WRITE(16, VDP1(FBCR), VDP1_FBCR_DIE);
    
    vdp1_vram_partitions_set(64,//VDP1_VRAM_DEFAULT_CMDT_COUNT,
                              0x7F000, //  VDP1_VRAM_DEFAULT_TEXTURE_SIZE,
                               0,//  VDP1_VRAM_DEFAULT_GOURAUD_COUNT,
                               0);//  VDP1_VRAM_DEFAULT_CLUT_COUNT);

            static vdp1_env_t vdp1_env = {
                .erase_color = RGB1555(1, 0, 0, 0),
                .erase_points[0] = {
                        .x = 0,
                        .y = 0
                },
                .erase_points[1] = {
                        .x = _SVIN_SCREEN_WIDTH - 1,
                        .y = _SVIN_SCREEN_HEIGHT -1
                },
                .bpp = VDP1_ENV_BPP_8,
                .rotation = VDP1_ENV_ROTATION_0,
                .color_mode = VDP1_ENV_COLOR_MODE_PALETTE,
                .sprite_type = 0xC
        };

        vdp1_env_set(&vdp1_env);

    vdp1_vram_partitions_t vdp1_vram_partitions;
    vdp1_vram_partitions_get(&vdp1_vram_partitions);

    vdp1_cmdt_color_bank_t dummy_bank;
    dummy_bank.raw = 0;

    vdp1_cmdt_t *cmdt_sprite;
    cmdt_sprite = &cmdts[_SVIN_VDP1_ORDER_SPRITE_A0_INDEX];
    cmdt_sprite->cmd_xa = 0;
    cmdt_sprite->cmd_ya = 0;
    cmdt_sprite->cmd_size = 0x2CE0;
    cmdt_sprite->cmd_srca = ((int)vdp1_vram_partitions.texture_base-VDP1_VRAM(0) ) / 8;
    vdp1_cmdt_color_mode4_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_color_bank_set(cmdt_sprite, dummy_bank);
    cmdt_sprite->cmd_pmod |= 0x08C0; //enabling ECD and SPD manually for now
    cmdt_sprite = &cmdts[_SVIN_VDP1_ORDER_SPRITE_A1_INDEX];
    cmdt_sprite->cmd_xa = 352;
    cmdt_sprite->cmd_ya = 0;
    cmdt_sprite->cmd_size = 0x2CE0;
    cmdt_sprite->cmd_srca = ((int)vdp1_vram_partitions.texture_base - VDP1_VRAM(0) + _SVIN_SCREEN_WIDTH*_SVIN_SCREEN_HEIGHT/4 ) / 8;
    vdp1_cmdt_color_mode4_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_color_bank_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_jump_assign(cmdt_sprite, _SVIN_VDP1_ORDER_DRAW_END_A_INDEX * 4);//skipping A2 and A3
    cmdt_sprite->cmd_pmod |= 0x08C0; //enabling ECD and SPD manually for now
    cmdt_sprite = &cmdts[_SVIN_VDP1_ORDER_SPRITE_B0_INDEX];
    cmdt_sprite->cmd_xa = 0;
    cmdt_sprite->cmd_ya = 0;
    cmdt_sprite->cmd_size = 0x2CE0;
    cmdt_sprite->cmd_srca = ((int)vdp1_vram_partitions.texture_base - VDP1_VRAM(0) + 2*_SVIN_SCREEN_WIDTH*_SVIN_SCREEN_HEIGHT/4 ) / 8;
    vdp1_cmdt_color_mode4_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_color_bank_set(cmdt_sprite, dummy_bank);
    cmdt_sprite->cmd_pmod |= 0x08C0; //enabling ECD and SPD manually for now
    cmdt_sprite = &cmdts[_SVIN_VDP1_ORDER_SPRITE_B1_INDEX];
    cmdt_sprite->cmd_xa = 352;
    cmdt_sprite->cmd_ya = 0;
    cmdt_sprite->cmd_size = 0x2CE0;
    cmdt_sprite->cmd_srca = ((int)vdp1_vram_partitions.texture_base - VDP1_VRAM(0) + 3*_SVIN_SCREEN_WIDTH*_SVIN_SCREEN_HEIGHT/4 ) / 8;
    vdp1_cmdt_color_mode4_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_color_bank_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_jump_assign(cmdt_sprite, _SVIN_VDP1_ORDER_DRAW_END_B_INDEX * 4);//skipping B2 and B3
    cmdt_sprite->cmd_pmod |= 0x08C0; //enabling ECD and SPD manually for now

    vdp1_cmdt_t *cmdt_system_clip_coords;
    cmdt_system_clip_coords = &cmdts[_SVIN_VDP1_ORDER_SYSTEM_CLIP_COORDS_INDEX];

    cmdt_system_clip_coords->cmd_xc = _SVIN_SCREEN_WIDTH - 1;
    cmdt_system_clip_coords->cmd_yc = _SVIN_SCREEN_HEIGHT - 1;

    vdp1_sync_cmdt_list_put(_svin_cmdt_list, 0);
    vdp1_sync();
}

int
main(void)
{
        //_video_fifo = malloc(2048*VIDEO_FIFO_SIZE); 

        vdp2_scrn_cell_format_t format;
        memset(&format, 0x00, sizeof(format));
        vdp2_scrn_normal_map_t normal_map;
        memset(&normal_map, 0x00, sizeof(normal_map));

        format.scroll_screen = VDP2_SCRN_NBG0;
        format.ccc = VDP2_SCRN_CCC_PALETTE_16;
        format.char_size = VDP2_SCRN_CHAR_SIZE_1X1;
        format.pnd_size = 2;
        format.aux_mode = 0;
        format.cpd_base = (uint32_t)VDP2_VRAM_TILES_START_0;
        format.palette_base = (uint32_t)VDP2_CRAM_MODE_1_OFFSET(0, 0, 0);
        format.plane_size = VDP2_SCRN_PLANE_SIZE_2X1;
        normal_map.plane_a = (uint32_t)VDP2_VRAM_NAMES_START_0;
        normal_map.plane_b = (uint32_t)VDP2_VRAM_NAMES_START_0;
        normal_map.plane_c = (uint32_t)VDP2_VRAM_NAMES_START_0;
        normal_map.plane_d = (uint32_t)VDP2_VRAM_NAMES_START_0;

        vdp2_vram_cycp_t vram_cycp;

        vram_cycp.pt[0].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG0;
        vram_cycp.pt[0].t1 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[0].t2 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[0].t3 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[0].t4 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[0].t5 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[0].t6 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[0].t7 = VDP2_VRAM_CYCP_NO_ACCESS;

        vram_cycp.pt[1].t0 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[1].t1 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[1].t2 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[1].t3 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[1].t4 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[1].t5 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[1].t6 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[1].t7 = VDP2_VRAM_CYCP_NO_ACCESS;

        vram_cycp.pt[2].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG0;
        vram_cycp.pt[2].t1 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[2].t2 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[2].t3 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[2].t4 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[2].t5 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[2].t6 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[2].t7 = VDP2_VRAM_CYCP_NO_ACCESS;

        vram_cycp.pt[3].t0 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[3].t1 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[3].t2 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[3].t3 = VDP2_VRAM_CYCP_PNDR_NBG0;
        vram_cycp.pt[3].t4 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[3].t5 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[3].t6 = VDP2_VRAM_CYCP_NO_ACCESS;
        vram_cycp.pt[3].t7 = VDP2_VRAM_CYCP_NO_ACCESS;

        vdp2_vram_cycp_set(&vram_cycp);

        _copy_character_pattern_data(&format);
        _copy_color_palette(&format);
        _copy_map(&format,&normal_map);

        vdp2_scrn_cell_format_set(&format,&normal_map);
        vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 2);
        vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG0);
        vdp2_cram_mode_set(1);

        //vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_B,
          //  VDP2_TVMD_VERT_224);
        vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_DOUBLE, VDP2_TVMD_HORZ_HIRESO_B, VDP2_TVMD_VERT_224); //704x448
        vdp2_tvmd_display_set();

        vdp2_sync();

        _svin_background_init();
        _update_vdp1_flag = 1;
        _svin_background_set_no_filelist("LOGO.BG");
        _svin_delay(1000);
        //while (1);
        
        _svin_background_fade_to_black();
        _svin_background_clear();



        _prepare_video();

        _video_fifo_read = -1;
        _video_fifo_write = -1;
        _video_fifo_number_of_entries = 0;
        //_cd_read_pending = 0;
        //pre-filling fifo
        _fill_video_fifo();
        //_video_fifo_read = 0;
//        while (_video_fifo_number_of_entries < 0.5*VIDEO_FIFO_SIZE)
        {
  //              _fill_video_fifo();
        }

        while (true) {
                _fill_video_fifo();
                /*if (true ==_play_video_frame_flag)
                {
                        _play_video_frame_flag = false;
                        _play_video_frame();
                        //while (1);
                }*/
        }	

        while(1);
}

void
user_init(void)
{
        vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
            RGB1555(1, 0, 0, 7));


        vdp_sync_vblank_in_set(_vblank_in_handler,NULL);
        vdp_sync_vblank_out_set(_vblank_out_handler,NULL);

        cpu_intc_mask_set(0);

        vdp2_tvmd_display_clear();
}

static void
_copy_character_pattern_data(const vdp2_scrn_cell_format_t *format)
{
        uint8_t *cpd;
        cpd = (uint8_t *)format->cpd_base;

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
        color_palette = (uint16_t *)format->palette_base;

		for (int palette = 0;palette < 128; palette++)
		{
			for (int i=0;i<16;i++)
			{
				color_palette[palette*16+i] = (RGB888_RGB1555(1, i*16,   palette*2,  0).raw);
			}
		}
}

static void
_copy_map(const vdp2_scrn_cell_format_t *format,const vdp2_scrn_normal_map_t *normal_map)
{
        uint32_t page_width;
        page_width = VDP2_SCRN_PAGE_WIDTH_CALCULATE(format);
        uint32_t page_height;
        page_height = VDP2_SCRN_PAGE_HEIGHT_CALCULATE(format);
        uint32_t page_size;
        page_size = VDP2_SCRN_PAGE_SIZE_CALCULATE(format);

        uint32_t *planes[4];
        planes[0] = (uint32_t *)normal_map->plane_a;
        planes[1] = (uint32_t *)normal_map->plane_b;
        planes[2] = (uint32_t *)normal_map->plane_c;
        planes[3] = (uint32_t *)normal_map->plane_d;

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
                        pnd = VDP2_SCRN_PND_CONFIG_8(1, (uint32_t)format->cpd_base,
                            (uint32_t)(format->palette_base),
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
        current_framebuffer = 0;
        initial_framebuffer_fill = 1;
        _last_vram_address = 0;

        cdfs_filelist_root_read(&_filelist);
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
        _svin_cd_block_sector_read(_video_file_fad, tmp_sector);
        //getting number of tiles
        _fifo_buffers_in_file = tmp_sector_16[0];
        //allocating array for tiles
        assert(_fifo_buffers_in_file < 20000); //hard limit, do we need it?
        int blocks_for_map = (_fifo_buffers_in_file+1) / 2048 + 1;
        _video_sectors_map = malloc(blocks_for_map * 2048);
        //reading whole
        _svin_cd_block_sectors_read(_video_file_fad,_video_sectors_map, CDFS_SECTOR_SIZE*blocks_for_map);

        _last_used_fad = _video_file_fad + blocks_for_map;

        _last_used_offset = 511;

        //requesting entire video file
        _svin_cd_block_multiple_sector_read_request(_last_used_fad,_video_file_size/CDFS_SECTOR_SIZE-blocks_for_map+1);

        _last_map_index = 1; //0 and 1 are used by sectors number. at first _play_video_frame this will be incremented to 2 
}

void
_play_video_frame(void)
{ 
        uint8_t *fifo_frame_8 = (uint8_t *)&_video_fifo[_video_fifo_read*2048]; 
        uint16_t *fifo_frame_16 = (uint16_t *)fifo_frame_8; 
        uint32_t *fifo_frame_32 = (uint32_t *)fifo_frame_8; 
        //uint32_t * pNames;
        //uint32_t * pNames_alt;
        //vdp2_scrn_cell_format_t format;


        if (_video_fifo_number_of_entries == 0)
                return;

        /*if (0 == current_framebuffer)
        {
                pNames = (uint32_t*)_names_cache;//(uint32_t*)(VDP2_VRAM_NAMES_START_0);
                //pNames_alt = (uint32_t*)(VDP2_VRAM_NAMES_START_1);
        }
        else
        {
                pNames = (uint32_t*)_names_cache_0;//(uint32_t*)(VDP2_VRAM_NAMES_START_1);
                //pNames_alt = (uint32_t*)(VDP2_VRAM_NAMES_START_0);
        }*/

        int iSkip = 0;
        int iPaletteIndex = -1;
        int iPaletteNumber = 0;
	//uint16_t color16;
	uint16_t * pCRAM = (uint16_t*)(VDP2_VRAM_ADDR(8,0));

        int iTileX = 0;
	int iTileY = 0;
        while (iTileY<VIDEO_Y_SIZE)
        {
                _last_used_offset ++;
                                        
                if (_last_used_offset >= 512)
                {
                        _last_used_offset = 0;


                        
                        //move to the next fifo entry 
                        _video_fifo_read++;
                        _video_fifo_number_of_entries--;
                        //assert (_video_fifo_number_of_entries>0);
                        if (_video_fifo_number_of_entries<=0)
                        {
                                //no more data in FIFO. eiter underrun or end.
                                //stopping here
                                //assert(0);
                                while(1);
                        }
                        if (_video_fifo_read == VIDEO_FIFO_SIZE)
                                _video_fifo_read = 0;

                        // while the current fifo entry is a tile data, parse it right away and advance to the next one
                        while ((_video_sectors_map[_last_map_index+1] & 0x0F) == 0)
                        {
                                //it's a tile data. if the tile data is for current framebuffer, it's an error, asserting
                                //int iFBIndex = (_video_sectors_map[_last_map_index+1] & 0xF0)>>4;
                                /*if ( (iFBIndex == current_framebuffer) && (initial_framebuffer_fill == 0))
                                {
                                        assert(0);
                                }
                                else*/
                                {
                                        fifo_frame_8 = (uint8_t *)&_video_fifo[_video_fifo_read*2048]; 
                                        if (_last_vram_address < 0x38000) //losing tiles that don't fit
                                        {
                                                if (initial_framebuffer_fill == 1)
                                                        mempcpy((void*)(VDP2_VRAM_TILES_START_0+_last_vram_address),fifo_frame_8,2048);
                                                else if (0 == current_framebuffer)
                                                        mempcpy((void*)(VDP2_VRAM_TILES_START_1+_last_vram_address),fifo_frame_8,2048);
                                                else
                                                        mempcpy((void*)(VDP2_VRAM_TILES_START_0+_last_vram_address),fifo_frame_8,2048);
                                        }
                                        _last_map_index++;
                                        _last_vram_address += 2048;
                                        
                                        //move to the next fifo entry 
                                        _video_fifo_read++;
                                        _video_fifo_number_of_entries--;
                                        //assert (_video_fifo_number_of_entries>0);
                                        if (_video_fifo_number_of_entries<=0)
                                        {
                                                //no more data in FIFO. eiter underrun or end.
                                                //stopping here
                                                //assert(0);
                                                while(1);
                                        }
                                        if (_video_fifo_read == VIDEO_FIFO_SIZE)
                                                _video_fifo_read = 0;
                                }
                        }
                        
                        //as long as the first command data appeared int the stream, reset the fill flag
                        if (initial_framebuffer_fill == 1)
                        {
                                initial_framebuffer_fill = 0; 
                                //there is no switch command at that point, so resetting tiles vram pointer manually
                                _last_vram_address = 0;
                        }


                        /*if (_total_frames_played  > 408)
                        {
                                //memcpy((uint8_t*)LWRAM(0),_video_sectors_map,2048);
                                        uint32_t * p = (uint32_t*)LWRAM(0);
                                        p[0] = _last_map_index;
                                        p[1] = _last_vram_address;
                                        p[2] = _video_fifo_read;
                                        p[3] = _video_fifo_write;
                                        p[4] = current_framebuffer;
                                        while (1);
                        }*/


                        //check if fifo is not empty
                        assert (_video_fifo_read!=_video_fifo_write);
                        //update pointers
                        fifo_frame_8 = (uint8_t *)&_video_fifo[_video_fifo_read*2048]; 
                        fifo_frame_16 = (uint16_t *)fifo_frame_8; 
                        fifo_frame_32 = (uint32_t *)fifo_frame_8; 

                        _last_map_index++;
                }
                
                if (iSkip > 0)
                {
                        iSkip--;
                        _last_used_offset --;
                        //move to next
                        iTileX++;
                        if (iTileX >= VIDEO_X_SIZE)
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
                        if (iTileX >= VIDEO_X_SIZE)
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
                else if(fifo_frame_8[_last_used_offset*4] == 0x04)
                {
                        //do switch
                        _last_vram_address = 0;
                        uint16_t * pMPABN0 = (uint16_t*)0x25F80040;
                        if (0 == current_framebuffer)
                        {
                                current_framebuffer = 1;
                                pMPABN0[0] = 0x1010;
                                //copying names
                                //memcpy(pNames_alt,pNames,30720);
                        }
                        else
                        {
                                current_framebuffer = 0;
                                pMPABN0[0] = 0x0000;
                                //memcpy(pNames,pNames_alt,30720);
                        }
                        //clearing old framebuffer we switched away from
                        //memset(pNames,0,0x7800);
                        //memset(pNames_alt,0,0x7800);

                }
                else if(fifo_frame_8[_last_used_offset*4] == 0xFA)
                {
                        //nop
                }
                else
                {
                        //copy data
                        if (iTileX < 64)
                        {
                                _names_cache[iTileY*64+iTileX] = fifo_frame_32[_last_used_offset];
                        }
                        else
                        {
                                _names_cache[64*63+iTileY*64+iTileX] = fifo_frame_32[_last_used_offset];
                        }
                        //move to next
                        iTileX++;
                        if (iTileX >= VIDEO_X_SIZE)
                        {
                                iTileX = 0;
                                iTileY++;
                        }
                }
        }

        _total_frames_played++;

        

}

void
_fill_video_fifo(void)
{ 
        uint8_t * fifo_frame_8;

        if (_total_fifo_buffers_loaded >= _fifo_buffers_in_file)
                return; //end of file

        //filling fifo no matter the type

        //trying to advance write to check if fifo is full
        int _video_fifo_write_test = _video_fifo_write + 1;
        if (_video_fifo_write_test == VIDEO_FIFO_SIZE)
                _video_fifo_write_test = 0;

        //if fifo is full, bail out
        if (_video_fifo_write_test == _video_fifo_read) 
                return;
                
        fifo_frame_8 = (uint8_t *)&_video_fifo[_video_fifo_write_test*2048]; 

        if (0 == _svin_cd_block_sector_read_process(fifo_frame_8))
        {
                _video_fifo_write = _video_fifo_write_test;
                _video_fifo_number_of_entries++;
                _total_fifo_buffers_loaded++;
                //_last_map_index++;
                if (initial_framebuffer_fill == 1)
                {
                        if (_video_fifo_number_of_entries > (VIDEO_FIFO_SIZE/2))
                        {
                                //enabling video playback
                                _play_video_enable_flag = true;
                        }
                }
        }


        //checking whether next entry is video data or tile data
        /*if ((_video_sectors_map[_last_map_index+1] & 0x0F) == 0)
        {
                //it's a tile data. if the tile data is for current framebuffer, stalling
                int iFBIndex = (_video_sectors_map[_last_map_index+1] & 0xF0)>>4;
                if ( (iFBIndex == current_framebuffer) && (initial_framebuffer_fill == 0))
                {
                        //cannot update tiles on current framebuffer, stalling
                }
                else
                {
                        //loading into VRAM
                        if (0 == _svin_cd_block_sector_read_process(tmp_sector))
                        {
                                if (initial_framebuffer_fill == 1)
                                        mempcpy((void*)(VDP2_VRAM_TILES_START_0+_last_vram_address),tmp_sector,2048);
                                else if (0 == current_framebuffer)
                                        mempcpy((void*)(VDP2_VRAM_TILES_START_1+_last_vram_address),tmp_sector,2048);
                                else
                                        mempcpy((void*)(VDP2_VRAM_TILES_START_0+_last_vram_address),tmp_sector,2048);
                                _last_map_index++;
                                _last_vram_address += 2048;
                                //debug
                                //uint32_t * p = (uint32_t*)LWRAM(0);
                                //memcpy((uint8_t*)LWRAM(0),tmp_sector,2048);
                                //p[1] = _last_map_index;
                                //p[2] = _video_sectors_map[_last_map_index+1];
                                //assert(0); //debug
                        }
                }
        }
        else if ((_video_sectors_map[_last_map_index+1] & 0x0F) == 1)
        {
                //video data
                //trying to advance write to check if fifo is full
                int _video_fifo_write_test = _video_fifo_write + 1;
                if (_video_fifo_write_test == VIDEO_FIFO_SIZE)
                        _video_fifo_write_test = 0;

                //if fifo is full, bail out
                if (_video_fifo_write_test == _video_fifo_read) 
                        return;
                        
                fifo_frame_8 = (uint8_t *)&_video_fifo[_video_fifo_write_test*2048]; 

                if (0 == _svin_cd_block_sector_read_process(fifo_frame_8))
                {
                        _video_fifo_write = _video_fifo_write_test;
                        _video_fifo_number_of_entries++;
                        //mempcpy((void*)LWRAM(_video_fifo_write_test*2048),fifo_frame_8,2048);
                        _last_map_index++;
                        if (initial_framebuffer_fill == 1)
                        {
                                if (_video_fifo_number_of_entries > 50)
                                {
                                        //enabling video playback
                                        _play_video_enable_flag = true;
                                        initial_framebuffer_fill = 0;
                                }
                        }
                }
        }
        else
        {
                memcpy((uint8_t*)LWRAM(0),_video_sectors_map,20000);
                uint32_t * p = (uint32_t*)LWRAM(0);
                p[1] = _last_map_index;
                p[2] = _video_sectors_map[_last_map_index+1];
                assert(0); //wrong sector type!
        }*/
}

static void
_vblank_in_handler(void *work __unused)
{
        /*uint16_t * p = (uint16_t*)LWRAM(0);
        p[_vsync_counter] = _video_fifo_number_of_entries;
        _vsync_counter++;
        if (_play_video_enable_flag)
                _play_video_frame();*/
        
        _set_cycle_patterns_cpu();
        if (0 == current_framebuffer)
        {
                memcpy((uint32_t*)(VDP2_VRAM_NAMES_START_0),_names_cache,30720);
        }
        else
        {
                memcpy((uint32_t*)(VDP2_VRAM_NAMES_START_1),_names_cache,30720);

        }
        _set_cycle_patterns_nbg();
        
                        /*if (_total_frames_played  > 100)
                        {
                                //memcpy((uint8_t*)LWRAM(0),_video_sectors_map,2048);
                                        uint32_t * p = (uint32_t*)LWRAM(0);
                                        p[0] = _last_map_index;
                                        p[1] = _last_vram_address;
                                        p[2] = _video_fifo_read;
                                        p[3] = _video_fifo_write;
                                        p[4] = current_framebuffer;
                                        while (1);
                        }*/
}

static void
_vblank_out_handler(void *work __unused)
{
        uint16_t * p = (uint16_t*)LWRAM(0);
        p[_vsync_counter] = _video_fifo_number_of_entries;
        _vsync_counter++;
        if (_play_video_enable_flag)
                _play_video_frame();

        //logo at start stuff

        uint8_t * p8 = (uint8_t *)VDP1_VRAM(0); 

        if (_update_vdp1_flag)
        {
                if (VDP2_TVMD_TV_FIELD_SCAN_ODD == vdp2_tvmd_field_scan_get())
                        p8[3]=0x1C;
                else
                        p8[3]=0x04;
        }
}