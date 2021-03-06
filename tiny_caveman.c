#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lib/SMSlib.h"
#include "lib/PSGlib.h"
#include "data.h"

#define SCREEN_W (256)
#define SCREEN_H (192)
#define SCROLL_H (224)

#define MAX_SPAWNERS (2)
#define MAX_ACTORS (2 + MAX_SPAWNERS * 2)
#define FOREACH_ACTOR(act) actor *act = actors; for (char idx_##act = 0; idx_##act != MAX_ACTORS; idx_##act++, act++)
	
#define ANIMATION_SPEED (3)

#define PLAYER_SPEED (3)
#define PLAYER_SHOT_SPEED (6)
#define PLAYER_TOP (32)
#define PLAYER_LEFT (8)
#define PLAYER_BOTTOM (128)

#define GROUP_ENEMY_SUB (1)
#define GROUP_ENEMY_SHOT (2)
#define GROUP_FISH (3)
#define GROUP_DIVER (4)

#define SCORE_DIGITS (6)

#define LEVEL_DIGITS (3)

#define ENERGY_CHARS (12)
#define ENERGY_RESOLUTION (4)
#define ENERGY_SHIFT (4)
#define ENERGY_MAX ((ENERGY_CHARS * ENERGY_RESOLUTION) << ENERGY_SHIFT)
#define ENERGY_MIN (-ENERGY_MAX / 6)

#define LIFE_CHARS (6)

#define STATE_START (1)
#define STATE_GAMEPLAY (2)
#define STATE_GAMEOVER (3)

typedef struct actor {
	char active;
	
	int x, y;
	int spd_x;
	char facing_left;
	char autofire;
	char thrown_away;
	int jump_speed;
	int max_y; // Ugly hack
	
	char char_w, char_h;
	char pixel_w, pixel_h;
	
	unsigned char base_tile, frame_count;
	unsigned char frame, frame_increment, frame_max;
	
	char punching, punch_delay;
	
	char group;
	char col_x, col_y, col_w, col_h;
	
	unsigned int score;
} actor;

actor actors[MAX_ACTORS];

actor *player = actors;
actor *ply_shot = actors + 1;
actor *first_spawner = actors + 2;

int animation_delay;

struct score {
	unsigned int value;
	char dirty;
} score;

struct life {
	int value;
	char dirty;
} life;

struct energy {
	int value;
	unsigned char last_shifted_value;
	char dirty;	
	char playing_sfx;
} energy;

struct level {
	unsigned int number;
	char starting;
	char ending;

	unsigned int submarine_score;
	unsigned int fish_score;
	unsigned int diver_score;
	unsigned int energy_score;
	
	unsigned char submarine_speed;
	unsigned char fish_speed;
	unsigned char diver_speed;
	
	unsigned int diver_chance;
	int boost_chance;
	char enemy_can_fire;
	char show_diver_indicator;
} level;

unsigned char screen_scroll;

void add_score(unsigned int value);
void add_life(int value);
void add_energy_non_negative(int value);

void draw_meta_sprite(int x, int y, int w, int h, unsigned char tile) {
	static char i, j;
	static int sx, sy;
	static unsigned char st;
	
	sy = y;
	st = tile;
	for (i = h; i; i--) {
		tile = st;
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
		st += 64;
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
	sa->thrown_away = 0;
	sa->jump_speed = 0;
	sa->max_y = PLAYER_BOTTOM - (char_h - 1) * 16;
	sa->autofire = 0;
	
	sa->char_w = char_w;
	sa->char_h = char_h;
	sa->pixel_w = char_w << 3;
	sa->pixel_h = char_h << 4;
	
	sa->base_tile = base_tile;
	sa->frame_count = frame_count;
	sa->frame = 0;
	sa->frame_increment = char_w << 1;
	sa->frame_max = sa->frame_increment * frame_count;
	
	sa->group = 0;
	sa->col_w = sa->pixel_w - 4;
	sa->col_h = sa->pixel_h - 4;
	sa->col_x = (sa->pixel_w - sa->col_w) >> 1;
	sa->col_y = (sa->pixel_h - sa->col_h) >> 1;
	
	sa->score = 0;
}

void clear_actors() {
	FOREACH_ACTOR(act) {
		act->active = 0;
	}
}

void wait_frames(int wait_time) {
	for (; wait_time; wait_time--) SMS_waitForVBlank();
}

void fire_shot(actor *shot, actor *shooter, char speed) {	
	static actor *_shot, *_shooter;

	if (shot->active || level.starting) return;
	
	_shot = shot;
	_shooter = shooter;
	
	init_actor(_shot, _shooter->x, _shooter->y, 1, 1, _shooter->base_tile + 36, 3);
	
	_shot->col_x = 0;
	_shot->col_y = 8;
	_shot->col_w = _shot->pixel_w;
	_shot->col_h = 4;
	
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
		
	if (_act->thrown_away) {
		_act->y--;
	} else {
		if (act->jump_speed) {
			_act->y += act->jump_speed >> 2;
		}
		act->jump_speed++;
		if (_act->y > _act->max_y) {
			_act->y = _act->max_y;
			act->jump_speed = 0;
		}
	}
	
	if (_act->autofire && level.enemy_can_fire) {
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
	
	if (_act->punching) {
		frame_tile = _act->base_tile + (_act->frame_max << 1);
		if (!_act->facing_left) {
			frame_tile += 4;
		}		
	} else {
		frame_tile = _act->base_tile + _act->frame;
		if (!_act->facing_left) {
			frame_tile += _act->frame_max;
		}
	}
	
	
	draw_meta_sprite(_act->x, _act->y, _act->char_w, _act->char_h, frame_tile);	

	if (!animation_delay) {
		_act->frame += _act->frame_increment;
		if (_act->frame >= _act->frame_max) _act->frame = 0;		
		if (_act->punching) {
			_act->punching--;
		} else if (_act->punch_delay > 1) {
			_act->punch_delay--;
		}
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
	PSGSFXFrame();
}

void load_standard_palettes() {
	static unsigned char palette[16];
	SMS_loadBGPalette(background_palette_bin);
	
	memcpy(palette, sprites_palette_bin, 16);
	palette[0] = 0;
	SMS_loadSpritePalette(palette);
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
	
	if ((joy & PORT_A_KEY_UP) && player->y == PLAYER_BOTTOM) {
		player->jump_speed = -5 << 2;
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
	
	if ((joy & (PORT_A_KEY_1)) && !player->punching && !player->punch_delay) {
		if (!ply_shot->active && !level.starting) {
			PSGPlayNoRepeat(player_punch_psg);
		}

		player->punching = 2;
		player->punch_delay = 2;
	}
	
	if (!(joy & (PORT_A_KEY_1)) && !player->punching && player->punch_delay == 1) {
		player->punch_delay = 0;
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
	static char i, facing_left, thing_to_spawn, boost;
	static int y;
	
	act = first_spawner;
	for (i = 0, y = PLAYER_BOTTOM - 16; i != MAX_SPAWNERS; i++, act += 2) {
		act2 = act + 1;
		if (!act->active && !act2->active) {
			if (rand() & 3 > 1) {
				facing_left = (rand() >> 4) & 1;
				thing_to_spawn = 0;
				boost = (rand() >> 4) % level.boost_chance ? 0 : 1;
				
				switch (thing_to_spawn) {
				case 0:
					// Spawn a T-Rex
					init_actor(act, 0, y, 3, 2, 66, 3);
					act->spd_x = level.submarine_speed + boost;
					act->autofire = 1;
					act->group = GROUP_ENEMY_SUB;
					act->score = level.submarine_score;
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

void check_collision_against_player_attack() {	
	if (!collider->active || !collider->group) {
		return;
	}

	if (player->punching && !collider->thrown_away && is_touching(collider, ply_shot)) {
		collider->thrown_away = 1;
		collider->spd_x = -3 * collider->spd_x;
		add_score(collider->score);
		PSGSFXPlay(enemy_death_psg, SFX_CHANNELS2AND3);
	}
}

void check_collision_against_player() {	
	if (!collider->active || !collider->group) {
		return;
	}

	if (player->active && !collider->thrown_away && is_touching(collider, player)) {
		add_energy_non_negative(-3);
	}
}

void check_collisions() {
	// Ugly hack for collision checking against the player's punch
	ply_shot->x = player->x + (player->facing_left ? -8 : 8);
	ply_shot->y = player->y;
	ply_shot->col_x = player->col_x;
	ply_shot->col_y = player->col_y;
	ply_shot->col_w = player->col_w;
	ply_shot->col_h = player->col_h;
	
	FOREACH_ACTOR(act) {
		collider = act;
		check_collision_against_player_attack();
		check_collision_against_player();
	}
}

void reset_actors_and_player() {
	clear_actors();
	init_actor(player, 116, PLAYER_BOTTOM, 2, 1, 2, 3);	
	ply_shot->active = 0;
}

void set_score(unsigned int value) {
	score.value = value;
	score.dirty = 1;
}

void add_score(unsigned int value) {
	set_score(score.value + value);
}

void draw_score() {
	static char buffer[SCORE_DIGITS];
	
	memset(buffer, -1, sizeof buffer);
	
	// Last digit is always zero
	char *d = buffer + SCORE_DIGITS - 1;
	*d = 0;
	d--;
	
	// Calculate the digits
	unsigned int remaining = score.value;
	while (remaining) {
		*d = remaining % 10;		
		remaining = remaining / 10;
		d--;
	}
		
	// Draw the digits
	d = buffer;
	SMS_setNextTileatXY(((32 - SCORE_DIGITS) >> 1) + 1, 0);
	for (char i = SCORE_DIGITS; i; i--, d++) {
		SMS_setTile((*d << 1) + 237 + TILE_USE_SPRITE_PALETTE);
	}
}

void draw_score_if_needed() {
	if (score.dirty) draw_score();
}

void draw_level_number() {
	static char buffer[LEVEL_DIGITS];
	
	memset(buffer, -1, sizeof buffer);
	
	// Calculate the digits
	char *d = buffer + LEVEL_DIGITS - 1;
	unsigned int remaining = level.number;
	do {
		*d = remaining % 10;		
		remaining = remaining / 10;
		d--;
	} while (remaining);
		
	// Draw the digits
	d = buffer;
	SMS_setNextTileatXY(2, 0);
	for (char i = LEVEL_DIGITS; i; i--, d++) {
		SMS_setTile((*d << 1) + 237 + TILE_USE_SPRITE_PALETTE);
	}
}

void set_life(int value) {
	if (value < 0) value = 0;
	life.value = value;
	life.dirty = 1;	
}

void add_life(int value) {
	set_life(life.value + value);	
}

void draw_life() {
	SMS_setNextTileatXY(2, 1);
	
	int remaining = life.value;
	for (char i = LIFE_CHARS; i; i--) {
		SMS_setTile((remaining > 0 ? 61 : 60) + TILE_USE_SPRITE_PALETTE);
		remaining --;
	}
}

void draw_life_if_needed() {
	if (life.dirty) draw_life();
}

void set_energy(int value) {
	if (value < ENERGY_MIN) value = ENERGY_MIN;
	if (value > ENERGY_MAX) value = ENERGY_MAX;
	
	energy.value = value;
	
	unsigned char shifted_value = value < 0 ? 0 : value >> ENERGY_SHIFT;
	
	energy.dirty = shifted_value != energy.last_shifted_value;
	energy.last_shifted_value = shifted_value;
}

void add_energy(int value) {
	set_energy(energy.value + value);
}

void add_energy_non_negative(int value) {
	value = energy.value + value;
	if (value < 0) value = 0;
	set_energy(value);
}

void draw_energy() {
	SMS_setNextTileatXY(((32 - ENERGY_CHARS) >> 1) + 1, 1);
	
	int remaining = energy.last_shifted_value;
	if (remaining < 0) remaining = 0;
	
	for (char i = ENERGY_CHARS; i; i--) {
		if (remaining > ENERGY_RESOLUTION) {
			SMS_setTile(127 + TILE_USE_SPRITE_PALETTE);
			remaining -= ENERGY_RESOLUTION;
			if (remaining < 0) remaining = 0;
		} else {
			SMS_setTile(119 + (remaining << 1) + TILE_USE_SPRITE_PALETTE);
			remaining = 0;
		}
	}
}

void draw_energy_if_needed() {
	if (energy.dirty) draw_energy();
}

void handle_energy() {
	if (level.starting) {			
		add_energy(15);
		level.starting = energy.value < ENERGY_MAX;
	}
}

void initialize_level() {
	level.starting = 1;
	level.ending = 0;
	
	clear_actors();
	ply_shot->active = 0;
	set_energy(0);
	
	level.fish_score = 1 + level.number / 3;
	level.submarine_score = level.fish_score << 1;
	level.diver_score = level.fish_score + level.submarine_score;
	level.energy_score = 1 + level.number / 4;
	
	level.fish_speed = 1 + level.number / 3;
	level.submarine_speed = 2 + level.number / 5;
	level.diver_speed = 1 + level.number / 6;
	
	if (level.fish_speed > PLAYER_SPEED) level.fish_speed = PLAYER_SPEED;
	if (level.submarine_speed > PLAYER_SPEED) level.submarine_speed = PLAYER_SPEED;
	if (level.diver_speed > PLAYER_SPEED) level.diver_speed = PLAYER_SPEED;
	
	level.diver_chance = 4 + level.number * 3 / 4;	
	level.enemy_can_fire = level.number > 1;
	level.show_diver_indicator = level.number < 2;
	
	level.boost_chance = 7 - level.number * 2 / 3;
	if (level.boost_chance < 2) level.boost_chance = 2;
}

void flash_player_red(unsigned char delay) {
	static unsigned char counter;
	static unsigned char flag;
	
	if (counter > delay) counter = delay;
	if (counter) {
		counter--;
		return;
	}
	
	counter = delay;
	
	SMS_loadSpritePalette(sprites_palette_bin);
	SMS_setSpritePaletteColor(0, 0);
	
	flag = !flag;
	if (flag) {
		SMS_setSpritePaletteColor(5, 0x1B);
		SMS_setSpritePaletteColor(6, 0x06);
		SMS_setSpritePaletteColor(7, 0x01);
	}
	
}

void perform_death_sequence() {
	PSGSFXPlay(player_death_psg, SFX_CHANNELS2AND3);
	
	for (unsigned char i = 80; i; i--) {
		SMS_waitForVBlank();
		flash_player_red(8);
	}
	
	load_standard_palettes();
}

void perform_level_end_sequence() {
	level.ending = 1;
	PSGSFXStop();
	PSGPlayNoRepeat(level_end_psg);
	
	load_standard_palettes();	
	while (energy.value) {
		if (player->x < 116) player->x++;
		if (player->x > 116) player->x--;
		if (player->y > PLAYER_TOP) player->y--;
		
		if (energy.value) {
			add_score(level.energy_score);
			add_energy_non_negative(-4);
		}
		
		SMS_initSprites();	
		draw_actors();		
		SMS_finalizeSprites();
		SMS_waitForVBlank();
		SMS_copySpritestoSAT();
		
		draw_score_if_needed();
		draw_energy_if_needed();
	}
	
	level.ending = 0;
	PSGSFXPlay(fill_air_psg, SFX_CHANNELS2AND3);			
}

void handle_scrolling() {
	static int delta = 0;
	
	if (player->x < 128) return;
	
	delta = player->x - 128;
	screen_scroll -= delta;	
	SMS_setBGScrollX(screen_scroll);

	player->x = 128;
	/*
	FOREACH_ACTOR(act) {
		if (act->active && act->x > 0 && !act->thrown_away) {
			act->x -= delta;
			if (act->x < -24) {
				act->active = 0;
			}
		}
	}
	*/
}

char gameplay_loop() {
	int frame = 0;
	int fish_frame = 0;
	int torpedo_frame = 0;
	
	animation_delay = 0;
	
	set_score(0);
	set_life(4);
	set_energy(0);	
	energy.dirty = 1;
	energy.playing_sfx = 0;
	screen_scroll = 0;
	
	level.number = 1;
	level.starting = 1;

	reset_actors_and_player();

	SMS_waitForVBlank();
	SMS_displayOff();

	SMS_loadPSGaidencompressedTiles(sprites_tiles_psgcompr, 0);
	SMS_loadPSGaidencompressedTiles(background_tiles_psgcompr, 256);
	
	SMS_setBGScrollX(0);
	draw_background();

	load_standard_palettes();

	clear_sprites();
	
	SMS_setLineInterruptHandler(&interrupt_handler);
	SMS_setLineCounter(180);
	SMS_enableLineInterrupt();

	SMS_displayOn();
		
	initialize_level();
	
	while(1) {	
		if (0) {
			perform_level_end_sequence();
			level.number++;
			initialize_level();
			player->active = 1;
		}

		if (!player->active) {
			add_life(-1);
			reset_actors_and_player();
			set_energy(0);
			level.starting = 1;
		}
		
		if (!life.value) {
			return STATE_GAMEOVER;
		}
	
		handle_player_input();
		handle_energy();
		
		if (!level.starting) {			
			handle_spawners();
			move_actors();
			check_collisions();
		}
		
		if (!player->active) {
			perform_death_sequence();
		}
		
		SMS_initSprites();	

		handle_scrolling();
		draw_actors();		

		SMS_finalizeSprites();		

		SMS_waitForVBlank();
		SMS_copySpritestoSAT();

		load_standard_palettes();
		
		draw_level_number();
		draw_score_if_needed();
		draw_life_if_needed();
		draw_energy_if_needed();
				
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

void print_number(char x, char y, unsigned int number, char extra_zero) {
	unsigned int base = 352 - 32;
	unsigned int remaining = number;
	
	if (extra_zero) {
		SMS_setNextTileatXY(x--, y);	
		SMS_setTile(base + '0');
	}
	
	while (remaining) {
		SMS_setNextTileatXY(x--, y);
		SMS_setTile(base + '0' + remaining % 10);
		remaining /= 10;
	}
}

char handle_gameover() {
	SMS_displayOff();
	
	load_standard_palettes();
	clear_sprites();
	
	SMS_loadPSGaidencompressedTiles(background_tiles_psgcompr, 0);
	SMS_loadTileMap(0, 0,background_tilemap_bin, background_tilemap_bin_size);		
	configure_text();	
	
	/*
	SMS_configureTextRenderer(352 - 32);
	SMS_setNextTileatXY(11, 11);
	puts('Game Over!');
	SMS_setNextTileatXY(11, 13);
//	printf('Score: %d0', score.value);
	*/

	// For some reason, the default text renderer is not working.
	// TODO: Organize this mess
	char *ch;
	unsigned int base = 352 - 32;
	
	SMS_setNextTileatXY(11, 11);
	for (ch = "Game Over!"; *ch; ch++) SMS_setTile(base + *ch);
	
	SMS_setNextTileatXY(11, 13);
	for (ch = "Your score:"; *ch; ch++) SMS_setTile(base + *ch);
	print_number(16, 14, score.value, 1);
	
	
	SMS_setNextTileatXY(11, 16);
	for (ch = "Your level:"; *ch; ch++) SMS_setTile(base + *ch);
	print_number(16, 17, level.number, 0);

	SMS_displayOn();	
	
	wait_frames(180);
	
	return STATE_START;
}

char handle_title() {
	reset_actors_and_player();

	SMS_waitForVBlank();
	SMS_displayOff();
	SMS_disableLineInterrupt();
	clear_sprites();
	SMS_setBGScrollX(0);

	SMS_loadPSGaidencompressedTiles(dinojam_tiles_psgcompr, 0);
	SMS_loadTileMap(0, 0, dinojam_tilemap_bin, dinojam_tilemap_bin_size);
	SMS_loadBGPalette(dinojam_palette_bin);

	SMS_displayOn();	
	wait_frames(90);
	SMS_displayOff();

	SMS_loadPSGaidencompressedTiles(title_tiles_psgcompr, 0);
	SMS_loadTileMap(0, 0, title_tilemap_bin, title_tilemap_bin_size);
	SMS_loadBGPalette(title_palette_bin);
	
	SMS_displayOn();	
	wait_frames(90);

	return STATE_GAMEPLAY;
}

void main() {
	char state = STATE_START;
	
	SMS_useFirstHalfTilesforSprites(1);
	SMS_setSpriteMode(SPRITEMODE_TALL);
	SMS_VDPturnOnFeature(VDPFEATURE_HIDEFIRSTCOL);
	SMS_VDPturnOnFeature(VDPFEATURE_LOCKHSCROLL);
	
	while (1) {
		switch (state) {
			
		case STATE_START:
			state = handle_title();
			break;
			
		case STATE_GAMEPLAY:
			state = gameplay_loop();
			break;
			
		case STATE_GAMEOVER:
			state = handle_gameover();
			break;
		}
	}
}

SMS_EMBED_SEGA_ROM_HEADER(9999,0); // code 9999 hopefully free, here this means 'homebrew'
SMS_EMBED_SDSC_HEADER(0,1, 2021,9,11, "Haroldo-OK\\2021", "Tiny Caveman",
  "A prehistoric beat-em-up.\n"
  "Made for Dino Jam 1.\n"
  "Built using devkitSMS & SMSlib - https://github.com/sverx/devkitSMS");
