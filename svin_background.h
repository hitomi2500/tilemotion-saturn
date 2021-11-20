#ifndef _SVIN_BACKGROUND_H_
#define _SVIN_BACKGROUND_H_

#include <yaul.h>
//#include <svin.h>

#define _SVIN_SCREEN_WIDTH    704
#define _SVIN_SCREEN_HEIGHT   448

//VDP1 command list order
#define _SVIN_VDP1_ORDER_SYSTEM_CLIP_COORDS_INDEX  0
#define _SVIN_VDP1_ORDER_LOCAL_COORDS_A_INDEX      1
#define _SVIN_VDP1_ORDER_SPRITE_A0_INDEX           2
#define _SVIN_VDP1_ORDER_SPRITE_A1_INDEX           3
#define _SVIN_VDP1_ORDER_SPRITE_A2_INDEX           4
#define _SVIN_VDP1_ORDER_SPRITE_A3_INDEX           5
#define _SVIN_VDP1_ORDER_DRAW_END_A_INDEX          6
#define _SVIN_VDP1_ORDER_LOCAL_COORDS_B_INDEX      7
#define _SVIN_VDP1_ORDER_SPRITE_B0_INDEX           8
#define _SVIN_VDP1_ORDER_SPRITE_B1_INDEX           9
#define _SVIN_VDP1_ORDER_SPRITE_B2_INDEX           10
#define _SVIN_VDP1_ORDER_SPRITE_B3_INDEX           11
#define _SVIN_VDP1_ORDER_DRAW_END_B_INDEX          12
#define _SVIN_VDP1_ORDER_COUNT                     13

void _svin_delay(int milliseconds);

void _svin_background_init();
void _svin_background_fade_to_black_step();
void _svin_background_fade_to_black();
void _svin_background_set_by_index(int index);
void _svin_background_set(char *name);
void _svin_background_clear();
void _svin_background_fade_to_black();
//void _svin_background_load_index(char * filename);

#endif
