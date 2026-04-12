#include "scr1.h"

#include <arch/zxn.h>

#include <print_tile.h>
#include <tilemap_manager.h>
#include <keyb.h>
#include <screen_list.h>
#include <palette_manager.h>
#include "palette_data.h"
#include "dma.h"
#include <sprite_manager.h>
#include <util_next.h>

#include <string.h>

typedef struct _Boy {
    u8 u;
    u8 v;
    u8 dir;
    u8 frame;
    SpriteDef sdef;
} Boy;

static Boy boy;

#define DIRN 0
#define DIRE 1
#define DIRS 2
#define DIRW 3

#define STATE_READY 0
#define STATE_MOVING 1

u8 state = STATE_READY;

void boy_transform_uv_to_xy(void);

void s1_init(void)
{
    palette_load_sprites(spriteset_palette);

    ZXN_NEXTREG(0x50, 30);
    ZXN_NEXTREG(0x51, 31);
    dma_transfer_sprite(0x0000, 0x4000);

    sprite_setup();

    tilemap_set_40col();

    palette_load_tilemap(screen01_palette);

    // copy tileset
    ZXN_NEXTREG(0x52, 41);
    ZXN_NEXTREG(0x53, 11);
    memcpy(0x6000, 0x4000, 0x2000);

    // copy tilemap
    ZXN_NEXTREG(0x52, 42);
    ZXN_NEXTREG(0x53, 10);
    memcpy(0x6000, 0x4000, 0x2000);

    ZXN_NEXTREG(0x52, 10);
    ZXN_NEXTREG(0x53, 11);

    boy.u = 40;
    boy.v = 40;
    boy.dir = DIRS;
    boy.frame = 0;

    boy.sdef.slot = 0;
    boy.sdef.pal = 0;
    boy.sdef.pat = 16;
    boy.sdef.mirrot = ATTR_X___;
    boy.sdef.scale = 0;
    boy.sdef.x = 144;
    boy.sdef.y = 128;

    boy_transform_uv_to_xy();

    sprite_init_quad(&boy.sdef);

    sprite_update_quad(&boy.sdef);

}

s8 xdir = 0;
s8 ydir = 0;

void read_input(void)
{
    xdir = 0;
    ydir = 0;
    if (keyb_is_pressed(KEY_ZX_5) || keyb_is_pressed(KEY_NX_LEFT) ) xdir -= 1;
    if (keyb_is_pressed(KEY_ZX_8) || keyb_is_pressed(KEY_NX_RIGHT)) xdir += 1;
    if (keyb_is_pressed(KEY_ZX_7) || keyb_is_pressed(KEY_NX_UP)   ) ydir -= 1;
    if (keyb_is_pressed(KEY_ZX_6) || keyb_is_pressed(KEY_NX_DOWN) ) ydir += 1;
    // no diagonals
    if (xdir != 0 && ydir != 0) {
        xdir = 0;
        ydir = 0;
    }
}

// translate this gdscript to C
/*
func update_dbgtext():
	$dbgtext.text = 'x=%d\ny=%d\ngx=%d\ngy=%d'%[coord.x, coord.y, gpos.x,gpos.y]
	
func update_anim():
	match state:
		State.READY:
			$player.state = $player.State.IDLE
		State.MOVING:
			$player.state = $player.State.WALK
			if xdir != 0:
				$player.frame = (gpos.x & 15) >> 2
			if ydir != 0:
				$player.frame = (gpos.y & 15) >> 2
	$player.update_pose()

func on_landed():
	coord.x = gpos.x >> 4
	coord.y = gpos.y >> 4
	
	var block = board[coord.y][coord.x]
	if coord.x == 0 or coord.x == 9 or coord.y == 0 or coord.y == 9:
		current_color = block.color
		$player.color = current_color
		$player.update_pose()
	else:
		block.color = current_color
		block.update_block()*/

void process_ready(void)
{
    read_input();

    if (xdir != 0 || ydir != 0) {
        boy.frame = 0;
        state = STATE_MOVING;
        if (xdir < 0) boy.dir = DIRW;
        if (xdir > 0) boy.dir = DIRE;
        if (ydir < 0) boy.dir = DIRN;
        if (ydir > 0) boy.dir = DIRS;
    }
}

void on_landed(void);

/*
    const u = uv.x;
    const v = uv.y;
    const x = (u - v);
    const y = (u + v) / 2;
    return new THREE.Vector2(x + ox, y + oy);
*/

void boy_transform_uv_to_xy(void)
{
    // isometric transform
    u8 u = boy.u;
    u8 v = boy.v;
    s16 x = (u - v);
    s16 y = (u + v) >> 1;
    boy.sdef.x = x + 144;
    boy.sdef.y = y - 24;
}

void process_moving(void)
{
    boy.u += xdir;
    boy.v += ydir;

    boy_transform_uv_to_xy();

    bool xlanded = (boy.u & 15) == 8;
    bool ylanded = (boy.v & 15) == 8;
    if (xlanded && ylanded) {
        state = STATE_READY;
        on_landed();
        process_ready();
    }    
}

void process_state(void)
{
    switch(state) {
        case STATE_READY:
            process_ready();
            break;
        case STATE_MOVING:
            process_moving();
            break;
    }
}

void on_landed(void)
{
    boy.frame = 0;
}

// func update_pose():
// 	var front : bool = false if dir == Dir.N or dir == Dir.W else true
// 	var left  : bool = false if dir == Dir.N or dir == Dir.E else true
	
// 	$base.frame = 0
// 	$dress.frame = 0
// 	if front: $base.frame += 6
// 	if front: $dress.frame += 6
// 	$base.flip_h = left
// 	$dress.flip_h = left
	
// 	match state:
// 		State.IDLE:
// 			$base.frame += (frame & 1)
// 			$dress.frame += (frame & 1)
// 		State.WALK:
// 			$base.frame += 2 + (frame & 3)
// 			$dress.frame += 2 + (frame & 3)


void process_player_anim(void)
{
    u8 pat = 0;
    u8 mirrot = 0;
    u8 dir = boy.dir;

    bool front = (dir == DIRN || dir == DIRW) ? false : true;
    bool left  = (dir == DIRN || dir == DIRE) ? false : true;

    if (front) pat += 16;
    if (left)  mirrot |= ATTR_X___;

    switch(state) {
        case STATE_READY:
            if (boy.frame & 32)
                pat += 4;
            break;
        case STATE_MOVING:
            u8 f0123 = (boy.frame >> 2) & 3;
            // 0 -> 8, 1 -> 0, 2 -> 12, 3 -> 0
            if (f0123 == 0) pat += 8;
            else if (f0123 == 2) pat += 12;
    }

    boy.sdef.pat = pat;
    boy.sdef.mirrot = mirrot;

    boy.frame++;

    sprite_update_quad(&boy.sdef);
}

/*

static var slowValue = 30 # number of frames to skip during slow motion
static var slowCount = 0
static var slowFrame = slowCount
static var slowSkip = false

static func mustSkipDueToSlowMotion():
	if not DUSE_DEBUG_KEYS:
		return false
	return slowSkip
		
static func processSkipDueToSlowMotion():
	slowCount = slowValue if Input.is_action_pressed("debug_slomo") else 0
			
	if slowCount != 0:
		if slowFrame > 0:
			slowFrame -= 1
			slowSkip = true
			return
		else:
			slowFrame = slowCount
	slowSkip = false
	

*/

u8 slowValue = 30; // number of frames to skip during slow motion
u8 slowCount = 0;
u8 slowFrame = 0;
bool slowSkip = false;

bool mustSkipDueToSlowMotion(void)
{
    // if (!DUSE_DEBUG_KEYS) return false;
    return slowSkip;
}

void processSkipDueToSlowMotion(void)
{
    // if (!DUSE_DEBUG_KEYS) return;
    slowCount = keyb_is_pressed(KEY_ZX_SYM) ? slowValue : 0;

    if (slowCount != 0) {
        if (slowFrame > 0) {
            slowFrame -= 1;
            slowSkip = true;
            return;
        } else {
            slowFrame = slowCount;
        }
    }
    slowSkip = false;
}

void s1_update(void)
{
    processSkipDueToSlowMotion();
    if (mustSkipDueToSlowMotion()) return;
    
    process_state();

    process_player_anim();

    // if (keyb_is_just_pressed_any())
    // {
    //     palette_load_tilemap(tileset_palette);

    //     // copy text tileset
    //     ZXN_NEXTREG(0x52, 40);
    //     ZXN_NEXTREG(0x53, 11);
    //     memcpy(0x6000, 0x4000, 0x2000);
    //     ZXN_NEXTREG(0x52, 10);
    //     ZXN_NEXTREG(0x53, 11);

    //     sc_switch_screen(sm_init, sm_update, NULL);
    // }
}

