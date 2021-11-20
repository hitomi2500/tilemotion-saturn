#include <yaul.h>
#include <vdp1/vram.h>
#include <svin_background.h>
#include <svin_cd_access.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

vdp1_cmdt_list_t *_svin_cmdt_list;
extern uint8_t _svin_init_done;

void _svin_delay(int milliseconds)
{
    //delay
    volatile int dummy = 0;
    for (dummy = 0; dummy < 3000 * milliseconds; dummy++)
        ;
}

void _svin_clear_palette(int number)
{
    uint16_t *my_vdp2_cram = (uint16_t *)VDP2_VRAM_ADDR(8, 0x200 * number);
    for (int i = 0; i < 256; i++)
    {
        my_vdp2_cram[i] = 0;
    }
}


void _svin_set_palette(int number, uint8_t *pointer)
{
    uint16_t *my_vdp2_cram = (uint16_t *)VDP2_VRAM_ADDR(8, 0x200 * number);
    for (int i = 0; i < 256; i++)
    {
        my_vdp2_cram[i] = (((pointer[i * 3 + 2] & 0xF8) << 7) |
                           ((pointer[i * 3 + 1] & 0xF8) << 2) |
                           ((pointer[i * 3 + 0] & 0xF8) >> 3));
    }
}

void _svin_background_init()
{
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
        .bits.pre_clipping_disable = true};

    assert(_svin_cmdt_list != NULL);

    vdp1_cmdt_t *cmdts;
    cmdts = &_svin_cmdt_list->cmdts[0];

    (void)memset(&cmdts[0], 0x00, sizeof(vdp1_cmdt_t) * _SVIN_VDP1_ORDER_COUNT);

    _svin_cmdt_list->count = _SVIN_VDP1_ORDER_COUNT;

    vdp1_cmdt_normal_sprite_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_A0_INDEX]);
    vdp1_cmdt_param_draw_mode_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_A0_INDEX], sprite_draw_mode);
    vdp1_cmdt_normal_sprite_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_A1_INDEX]);
    vdp1_cmdt_param_draw_mode_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_A1_INDEX], sprite_draw_mode);
    vdp1_cmdt_normal_sprite_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_B0_INDEX]);
    vdp1_cmdt_param_draw_mode_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_B0_INDEX], sprite_draw_mode);
    vdp1_cmdt_normal_sprite_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_B1_INDEX]);
    vdp1_cmdt_param_draw_mode_set(&cmdts[_SVIN_VDP1_ORDER_SPRITE_B1_INDEX], sprite_draw_mode);

    vdp1_cmdt_system_clip_coord_set(&cmdts[_SVIN_VDP1_ORDER_SYSTEM_CLIP_COORDS_INDEX]);
    vdp1_cmdt_jump_assign(&cmdts[_SVIN_VDP1_ORDER_SYSTEM_CLIP_COORDS_INDEX], _SVIN_VDP1_ORDER_LOCAL_COORDS_A_INDEX * 4);

    vdp1_cmdt_local_coord_set(&cmdts[_SVIN_VDP1_ORDER_LOCAL_COORDS_A_INDEX]);
    vdp1_cmdt_local_coord_set(&cmdts[_SVIN_VDP1_ORDER_LOCAL_COORDS_B_INDEX]);
    vdp1_cmdt_param_vertex_set(&cmdts[_SVIN_VDP1_ORDER_LOCAL_COORDS_A_INDEX],
                               CMDT_VTX_LOCAL_COORD, &local_coord_ul);
    vdp1_cmdt_param_vertex_set(&cmdts[_SVIN_VDP1_ORDER_LOCAL_COORDS_B_INDEX],
                               CMDT_VTX_LOCAL_COORD, &local_coord_ul);

    vdp1_cmdt_end_set(&cmdts[_SVIN_VDP1_ORDER_DRAW_END_A_INDEX]);
    vdp1_cmdt_end_set(&cmdts[_SVIN_VDP1_ORDER_DRAW_END_B_INDEX]);
    #define VDP1_FBCR_DIE (0x0008)
    MEMORY_WRITE(16, VDP1(FBCR), VDP1_FBCR_DIE);
    
    vdp1_vram_partitions_set(64,//VDP1_VRAM_DEFAULT_CMDT_COUNT,
                              0x7F000, //  VDP1_VRAM_DEFAULT_TEXTURE_SIZE,
                               0,//  VDP1_VRAM_DEFAULT_GOURAUD_COUNT,
                               0);//  VDP1_VRAM_DEFAULT_CLUT_COUNT);

            static vdp1_env_t vdp1_env = {
                .erase_color = COLOR_RGB1555(1, 0, 0, 0),
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
    vdp1_cmdt_param_color_mode4_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_param_color_bank_set(cmdt_sprite, dummy_bank);
    cmdt_sprite->cmd_pmod |= 0x08C0; //enabling ECD and SPD manually for now
    cmdt_sprite = &cmdts[_SVIN_VDP1_ORDER_SPRITE_A1_INDEX];
    cmdt_sprite->cmd_xa = 352;
    cmdt_sprite->cmd_ya = 0;
    cmdt_sprite->cmd_size = 0x2CE0;
    cmdt_sprite->cmd_srca = ((int)vdp1_vram_partitions.texture_base - VDP1_VRAM(0) + _SVIN_SCREEN_WIDTH*_SVIN_SCREEN_HEIGHT/4 ) / 8;
    vdp1_cmdt_param_color_mode4_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_param_color_bank_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_jump_assign(cmdt_sprite, _SVIN_VDP1_ORDER_DRAW_END_A_INDEX * 4);//skipping A2 and A3
    cmdt_sprite->cmd_pmod |= 0x08C0; //enabling ECD and SPD manually for now
    cmdt_sprite = &cmdts[_SVIN_VDP1_ORDER_SPRITE_B0_INDEX];
    cmdt_sprite->cmd_xa = 0;
    cmdt_sprite->cmd_ya = 0;
    cmdt_sprite->cmd_size = 0x2CE0;
    cmdt_sprite->cmd_srca = ((int)vdp1_vram_partitions.texture_base - VDP1_VRAM(0) + 2*_SVIN_SCREEN_WIDTH*_SVIN_SCREEN_HEIGHT/4 ) / 8;
    vdp1_cmdt_param_color_mode4_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_param_color_bank_set(cmdt_sprite, dummy_bank);
    cmdt_sprite->cmd_pmod |= 0x08C0; //enabling ECD and SPD manually for now
    cmdt_sprite = &cmdts[_SVIN_VDP1_ORDER_SPRITE_B1_INDEX];
    cmdt_sprite->cmd_xa = 352;
    cmdt_sprite->cmd_ya = 0;
    cmdt_sprite->cmd_size = 0x2CE0;
    cmdt_sprite->cmd_srca = ((int)vdp1_vram_partitions.texture_base - VDP1_VRAM(0) + 3*_SVIN_SCREEN_WIDTH*_SVIN_SCREEN_HEIGHT/4 ) / 8;
    vdp1_cmdt_param_color_mode4_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_param_color_bank_set(cmdt_sprite, dummy_bank);
    vdp1_cmdt_jump_assign(cmdt_sprite, _SVIN_VDP1_ORDER_DRAW_END_B_INDEX * 4);//skipping B2 and B3
    cmdt_sprite->cmd_pmod |= 0x08C0; //enabling ECD and SPD manually for now

    vdp1_cmdt_t *cmdt_system_clip_coords;
    cmdt_system_clip_coords = &cmdts[_SVIN_VDP1_ORDER_SYSTEM_CLIP_COORDS_INDEX];

    cmdt_system_clip_coords->cmd_xc = _SVIN_SCREEN_WIDTH - 1;
    cmdt_system_clip_coords->cmd_yc = _SVIN_SCREEN_HEIGHT - 1;

    vdp1_sync_cmdt_list_put(_svin_cmdt_list, 0);
    vdp1_sync();

}


void _svin_background_fade_to_black_step()
{
    uint16_t *my_vdp2_cram = (uint16_t *)VDP2_VRAM_ADDR(8, 0x00000);
    uint8_t r, g, b;
    for (int i = 0; i < 256; i++)
    {
        b = (my_vdp2_cram[i] & 0x7C00) >> 10;
        g = (my_vdp2_cram[i] & 0x03E0) >> 5;
        r = (my_vdp2_cram[i] & 0x001F) >> 0;
        r--;
        b--;
        g--;
        if (r == 0xFF)
            r = 0;
        if (g == 0xFF)
            g = 0;
        if (b == 0xFF)
            b = 0;
        my_vdp2_cram[i] = ((b << 10) |
                           (g << 5) |
                           (r << 0));
    }
}

void _svin_background_fade_to_black()
{
    for (int fade = 0; fade < 32; fade++)
    {
        _svin_background_fade_to_black_step();
        _svin_delay(30);
    }
}

#define VIDEO_FIFO_SIZE 256
extern uint8_t _video_fifo[2048*VIDEO_FIFO_SIZE]; 

void _svin_background_set(char * filename)
{
    //searching for fad
    fad_t _bg_fad;
    //int iSize;
    static iso9660_filelist_t _filelist;
    static iso9660_filelist_entry_t _filelist_entries[ISO9660_FILELIST_ENTRIES_COUNT];
    iso9660_filelist_entry_t *file_entry;

    _filelist.entries = _filelist_entries;
    _filelist.entries_count = 0;
    _filelist.entries_pooled_count = 0;

    iso9660_filelist_root_read(&_filelist, -1);
    _bg_fad = 0;
    for (unsigned int i = 0; i < _filelist.entries_count; i++)
    {
            file_entry = &(_filelist.entries[i]);
            if (strcmp(file_entry->name, filename) == 0)
            {
                    _bg_fad = file_entry->starting_fad;
            }
    }
    assert(_bg_fad > 0);
    
    //checking if found file is the exact size we expect
    //assert(iSize == (704*448 + 2048*2));

    //allocate memory for 77 sectors
    uint8_t *buffer = _video_fifo;
    //allocate memory for cram
    uint8_t *palette = malloc(2048);
    assert((int)(palette) > 0);

    //set zero palette to hide loading
    _svin_clear_palette(0);

    vdp1_vram_partitions_t vdp1_vram_partitions;
    vdp1_vram_partitions_get(&vdp1_vram_partitions);

    //reading first half of the background
    _svin_cd_block_sectors_read(_bg_fad + 1, buffer, 2048 * 77);
    memcpy((uint8_t *)(vdp1_vram_partitions.texture_base + 0 * 2048), buffer, 2048 * 77);

    //reading second half of the background
    _svin_cd_block_sectors_read(_bg_fad + 1 + 77, buffer, 2048 * 77);
    memcpy((uint8_t *)(vdp1_vram_partitions.texture_base + 77 * 2048), buffer, 2048 * 77);

    //read palette
    _svin_cd_block_sector_read(_bg_fad + 1 + 154, palette);
    _svin_set_palette(0, palette);

    free(palette);
}

void _svin_background_clear()
{
    //set zero palette
    _svin_clear_palette(0);

    vdp1_vram_partitions_t vdp1_vram_partitions;
    vdp1_vram_partitions_get(&vdp1_vram_partitions);

    memset((uint8_t *)(vdp1_vram_partitions.texture_base), 0, 2048 * 154);
}
