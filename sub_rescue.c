#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lib/SMSlib.h"
#include "lib/PSGlib.h"
#include "data.h"

#define SCREEN_W (256)
#define SCREEN_H (192)
#define SCROLL_H (224)

#define MAX_SPAWNERS (5)
#define MAX_ACTORS (2 + MAX_SPAWNERS * 2)
#define FOREACH_ACTOR(act) actor *act = actors; for (char idx_##act = 0; idx_##act != MAX_ACTORS; idx_##act++, act++)
	
#define ANIMATION_SPEED (3)

#define PLAYER_SPEED (2)
#define PLAYER_SHOT_SPEED (4)
#define PLAYER_TOP (32)
#define PLAYER_LEFT (8)
#define PLAYER_BOTTOM (146)

#define GROUP_ENEMY_SUB (1)
#define GROUP_ENEMY_SHOT (2)
#define GROUP_FISH (3)
#define GROUP_DIVER (4)


typedef struct actor {
	char active;
	
	int x, y;
	int spd_x;
	char facing_left;
	char autofire;
	
	char char_w, char_h;
	char pixel_w, pixel_h;
	
	unsigned char base_tile, frame_count;
	unsigned char frame, frame_increment, frame_max;
	
	char group;
	char col_x, col_y, col_w, col_h;
} actor;

actor actors[MAX_ACTORS];

actor *player = actors;
actor *ply_shot = actors + 1;
actor *first_spawner = actors + 2;

int animation_delay;

void draw_meta_sprite(int x, int y, int w, int h, unsigned char tile) {
	static char i, j;
	static int sx, sy;
	static unsigned char st;
	
	sy = y;
	st = tile;
	for (i = h; i; i--) {
		if (y >= 0 && y < SCREEN_H) {
			sx = x;
			for (j = w; j; j--) {
				if (sx >= 0 && sx < SCREEN_W) {
					SMS_addSprite(sx, sy, tile);
				}
				sx += 8;
				tile += 2;
			}
		}
		sy += 16;
	}
}

void init_actor(actor *act, int x, int y, int char_w, int char_h, unsigned char base_tile, unsigned char frame_count) {
	static actor *sa;
	sa = act;
	
	sa->active = 1;
	
	sa->x = x;
	sa->y = y;
	sa->spd_x = 0;
	sa->facing_left = 1;
	sa->autofire = 0;
	
	sa->char_w = char_w;
	sa->char_h = char_h;
	sa->pixel_w = char_w << 3;
	sa->pixel_h = char_h << 4;
	
	sa->base_tile = base_tile;
	sa->frame_count = frame_count;
	sa->frame = 0;
	sa->frame_increment = char_w * (char_h << 1);
	sa->frame_max = sa->frame_increment * frame_count;
	
	sa->group = 0;
	sa->col_w = sa->pixel_w - 4;
	sa->col_h = sa->pixel_h - 4;
	sa->col_x = (sa->pixel_w - sa->col_w) >> 1;
	sa->col_y = (sa->pixel_h - sa->col_h) >> 1;
}

void clear_actors() {
	FOREACH_ACTOR(act) {
		act->active = 0;
	}
}

void fire_shot(actor *shot, actor *shooter, char speed) {	
	static actor *_shot, *_shooter;

	if (shot->active) return;
	
	_shot = shot;
	_shooter = shooter;
	
	init_actor(_shot, _shooter->x, _shooter->y, 1, 1, _shooter->base_tile + 36, 3);
	
	_shot->col_x = 0;
	_shot->col_y = 8;
	_shot->col_w = _shot->pixel_w;
	_shot->col_h = _shot->pixel_h;
	
	_shot->facing_left = _shooter->facing_left;
	_shot->spd_x = _shooter->facing_left ? -speed : speed;
	if (!_shooter->facing_left) {
		_shot->x += _shooter->pixel_w - 8;
	}
}

void move_actor(actor *act) {
	static actor *_act, *_shot;
	
	if (!act->active) return;
	
	_act = act;
	
	if (_act->spd_x) {
		_act->x += _act->spd_x;
		
		if (_act->spd_x < 0) {
			if (_act->x + _act->pixel_w < 0) _act->active = 0;
		} else {
			if (_act->x >= SCREEN_W) _act->active = 0;
		}				
	}
	
	if (_act->autofire) {
		actor *_shot = _act + 1;		
		fire_shot(_shot, _act, abs(_act->spd_x) + 1);
		_shot->group = GROUP_ENEMY_SHOT;
	}
}

void move_actors() {
	FOREACH_ACTOR(act) {
		move_actor(act);
	}
}

void draw_actor(actor *act) {
	static actor *_act;
	static unsigned char frame_tile;
	
	if (!act->active) {
		return;
	}
	
	_act = act;
	
	frame_tile = _act->base_tile + _act->frame;
	if (!_act->facing_left) {
		frame_tile += _act->frame_max;
	}
	
	draw_meta_sprite(_act->x, _act->y, _act->char_w, _act->char_h, frame_tile);	

	if (!animation_delay) {
		_act->frame += _act->frame_increment;
		if (_act->frame >= _act->frame_max) _act->frame = 0;
	}
}

void draw_actors() {
	FOREACH_ACTOR(act) {
		draw_actor(act);
	}
}

void clear_sprites() {
	SMS_initSprites();	
	SMS_finalizeSprites();
	SMS_copySpritestoSAT();
}

void interrupt_handler() {
	PSGFrame();
}

void load_standard_palettes() {
	SMS_loadBGPalette(background_palette_bin);
	SMS_loadSpritePalette(sprites_palette_bin);
	SMS_setSpritePaletteColor(0, 0);
}

void load_tile_zero() {
	SMS_load1bppTiles(font_1bpp, 0, 8, 0, 1);
}

void configure_text() {
	load_tile_zero();
	SMS_load1bppTiles(font_1bpp, 352, font_1bpp_size, 0, 1);
	SMS_configureTextRenderer(352 - 32);
}

void shuffle_random(char times) {
	for (; times; times--) {
		rand();
	}
}

void handle_player_input() {
	unsigned char joy = SMS_getKeysStatus();
	
	if (joy & PORT_A_KEY_UP) {
		if (player->y > PLAYER_TOP) player->y -= PLAYER_SPEED;
		shuffle_random(1);
	} else if (joy & PORT_A_KEY_DOWN) {
		if (player->y < PLAYER_BOTTOM) player->y += PLAYER_SPEED;
		shuffle_random(2);
	}
	
	if (joy & PORT_A_KEY_LEFT) {		
		if (player->x > PLAYER_LEFT) player->x -= PLAYER_SPEED;
		player->facing_left = 1;
		shuffle_random(3);
	} else if (joy & PORT_A_KEY_RIGHT) {
		if (player->x < SCREEN_W - player->pixel_w) player->x += PLAYER_SPEED;
		player->facing_left = 0;
		shuffle_random(4);
	}
	
	if (joy & (PORT_A_KEY_1 | PORT_A_KEY_2)) {
		fire_shot(ply_shot, player, PLAYER_SHOT_SPEED);
	}
}

void adjust_facing(actor *act, char facing_left) {
	static actor *_act;
	_act = act;
	
	_act->facing_left = facing_left;
	if (facing_left) {
		_act->x = SCREEN_W - _act->x;
		_act->spd_x = -_act->spd_x;
	} else {
		_act->x -= _act->pixel_w;
	}
}

void handle_spawners() {
	static actor *act, *act2;
	static char i, facing_left, thing_to_spawn;
	static int y;
	
	act = first_spawner;
	for (i = 0, y = PLAYER_TOP + 16; i != MAX_SPAWNERS; i++, act += 2, y += 24) {
		act2 = act + 1;
		if (!act->active && !act2->active) {
			if (rand() & 3 > 1) {
				facing_left = (rand() >> 4) & 1;
				thing_to_spawn = ((rand() >> 4) & 7) ? ((rand() >> 4) & 1) : 2;
				
				switch (thing_to_spawn) {
				case 0:
					// Spawn a submarine
					init_actor(act, 0, y, 3, 1, 66, 3);
					act->spd_x = 2;
					act->autofire = 1;
					act->group = GROUP_ENEMY_SUB;
					break;
					
				case 1:
					// Spawn a pair of fishes
					init_actor(act, 0, y, 2, 1, 128, 4);
					init_actor(act2, -64, y, 2, 1, 128, 4);
					act->spd_x = 2;
					act->group = GROUP_FISH;

					act2->spd_x = act->spd_x;
					act2->group = act->group;
					break;
					
				case 2:
					// Spawn a diver
					init_actor(act, 0, y, 2, 1, 192, 4);
					act->spd_x = 2;
					act->group = GROUP_DIVER;
					break;
				}
				
				adjust_facing(act, facing_left);
				adjust_facing(act2, facing_left);
			}	
		}
	}
}

void draw_background() {
	unsigned int *ch = background_tilemap_bin;
	
	SMS_setNextTileatXY(0, 0);
	for (char y = 0; y != 24; y++) {
		for (char x = 0; x != 32; x++) {
			unsigned int tile_number = *ch + 256;
			if (y == 5) {
				tile_number |= TILE_PRIORITY;
			}
			
			SMS_setTile(tile_number);
			ch++;
		}
	}
}

char is_touching(actor *act1, actor *act2) {
	static actor *collider1, *collider2;
	static int r1_tlx, r1_tly, r1_brx, r1_bry;
	static int r2_tlx, r2_tly, r2_bry;

	// Use global variables for speed
	collider1 = act1;
	collider2 = act2;

/*
	// Rough collision: check if their base vertical coordinates are on the same row
	if (abs(collider1->y - collider2->y) > 16) {
		return 0;
	}
	
	// Rough collision: check if their base horizontal coordinates are not too distant
	if (abs(collider1->x - collider2->x) > 24) {
		return 0;
	}
	*/
	
	// Less rough collision on the Y axis
	
	r1_tly = collider1->y + collider1->col_y;
	r1_bry = r1_tly + collider1->col_h;
	r2_tly = collider2->y + collider2->col_y;
	
	// act1 is too far above
	if (r1_bry < r2_tly) {
		return 0;
	}
	
	r2_bry = r2_tly + collider2->col_h;
	
	// act1 is too far below
	if (r1_tly > r2_bry) {
		return 0;
	}
	
	// Less rough collision on the X axis
	
	r1_tlx = collider1->x + collider1->col_x;
	r1_brx = r1_tlx + collider1->col_w;
	r2_tlx = collider2->x + collider2->col_x;
	
	// act1 is too far to the left
	if (r1_brx < r2_tlx) {
		return 0;
	}
	
	int r2_brx = r2_tlx + collider2->col_w;
	
	// act1 is too far to the left
	if (r1_tlx > r2_brx) {
		return 0;
	}
	
	return 1;
}

// Made global for performance
actor *collider;

void check_collision_against_player_shot() {	
	if (!collider->active || !collider->group) {
		return;
	}

	if (ply_shot->active && is_touching(collider, ply_shot)) {
		if (collider->group != GROUP_DIVER) collider->active = 0;
		
		if (collider->group != GROUP_DIVER && collider->group != GROUP_ENEMY_SHOT) {
			ply_shot->active = 0;
		}
	}
}

void check_collision_against_player() {	
	if (!collider->active || !collider->group) {
		return;
	}

	if (player->active && is_touching(collider, player)) {
		collider->active = 0;		
		if (collider->group != GROUP_DIVER) {
			player->active = 0;
		}
	}
}

void check_collisions() {
	FOREACH_ACTOR(act) {
		collider = act;
		check_collision_against_player_shot();
		check_collision_against_player();
	}
}

void reset_actors_and_player() {
	clear_actors();
	init_actor(player, 116, 88, 3, 1, 2, 3);	
	ply_shot->active = 0;
}

char gameplay_loop() {
	int frame = 0;
	int fish_frame = 0;
	int torpedo_frame = 0;
	
	animation_delay = 0;

	reset_actors_and_player();

	SMS_waitForVBlank();
	SMS_displayOff();

	SMS_loadPSGaidencompressedTiles(sprites_tiles_psgcompr, 0);
	SMS_loadPSGaidencompressedTiles(background_tiles_psgcompr, 256);
	
	draw_background();

	load_standard_palettes();

	clear_sprites();

	//configure_text();
	
	SMS_displayOn();
	
	while(1) {		
		if (!player->active) {
			reset_actors_and_player();
		}
	
		handle_player_input();
		handle_spawners();
		move_actors();
		check_collisions();
		
		SMS_initSprites();	

		draw_actors();

		SMS_finalizeSprites();		

		SMS_waitForVBlank();
		SMS_copySpritestoSAT();
		
		frame += 6;
		if (frame > 12) frame = 0;
		
		fish_frame += 4;
		if (fish_frame > 12) fish_frame = 0;
				
		torpedo_frame += 2;
		if (torpedo_frame > 4) torpedo_frame = 0;
		
		animation_delay--;
		if (animation_delay < 0) animation_delay = ANIMATION_SPEED;
	}
}

void main() {
	SMS_useFirstHalfTilesforSprites(1);
	SMS_setSpriteMode(SPRITEMODE_TALL);
	SMS_VDPturnOnFeature(VDPFEATURE_HIDEFIRSTCOL);
	
	while (1) {			
		gameplay_loop();
	}
}

SMS_EMBED_SEGA_ROM_HEADER(9999,0); // code 9999 hopefully free, here this means 'homebrew'
SMS_EMBED_SDSC_HEADER(0,1, 2021,3,15, "Haroldo-OK\\2021", "Sub Rescue",
  "A subaquatic shoot-em-up.\n"
  "Built using devkitSMS & SMSlib - https://github.com/sverx/devkitSMS");