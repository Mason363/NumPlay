/* The Sega Master System, Game Gear and SG-1000 (sms.c and superzazu's Z80), as Retro-Go's
 * screens drive them. */
#ifndef SMS_H
#define SMS_H
#include "../rg.h"

#define SMS_WIDTH 256
#define SMS_HEIGHT 240 /* (the tallest picture; most games show 192 lines) */

/* Loads a ROM read in place, for a Master System, a Game Gear or an SG-1000 (machine); crc is its
   CRC32, for the few games that need something of their own (the mapper, 50 Hz, Master System
   mode on the Game Gear). RG_OK or an RG_ERR_ code. */
enum { SMS_SMS, SMS_GG, SMS_SG };
int sms_load(const uint8_t *data, uint32_t size, uint32_t crc, int machine);
void sms_reset(bool hard);
void sms_run_frame(bool draw);
void sms_set_pad(int rg_pad);
/* the picture shown: its first line and column and its size (the Game Gear's screen is a part of
   its video chip's) */
void sms_view(int *x, int *y, int *w, int *h);
/* a frame's length in microseconds: 60 frames a second, 50 for the European games that need it */
uint32_t sms_frame_us(void);
bool sms_is_gg(void);
bool sms_is_sg(void);
/* the last frame drawn, line by line through rg_line again (under the menus) */
void sms_redraw(void);

/* the cartridge's RAM (32 KB), and whether the game has turned it on (only then is it saved) */
uint8_t *sms_sram(uint32_t *size);
bool sms_sram_used(void);
bool sms_sram_dirty(void);
void sms_sram_saved(void);
/* it was read back from a save: the game had it on */
void sms_sram_loaded(void);

int sms_state_chunks(rg_chunk_t *out);
void sms_state_loaded(void);
#endif
