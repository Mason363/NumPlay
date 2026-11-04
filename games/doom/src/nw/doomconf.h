/* Doom on the NumWorks calculator: how the engine is built (every source file starts with this).
 *
 * The engine is RP2040 Doom's (Graham Sanderson's port of Chocolate Doom to a microcontroller with
 * 264 KB of RAM): the shareware WAD packed into its WHD format, read in place from flash, and the
 * game's state squeezed into a few dozen KB. These are its "tiny" build's switches, less the
 * sound, music, network and RP2040 hardware, which the calculator doesn't have. */
#ifndef DOOMCONF_H
#define DOOMCONF_H

#define NUMWORKS 1
#define PICO_ON_DEVICE 0

/* the small build */
#define SHRINK_MOBJ 1
#define DOOM_ONLY 1
#define DOOM_SMALL 1
#define DOOM_CONST 1
#define DOOM_TINY 1
#define NUM_SOUND_CHANNELS 8
#define NO_USE_CHECKSUM 1
#define NO_USE_RELOAD 1
#define USE_SINGLE_IWAD 1
#define NO_USE_WIPE 1
#define NO_USE_JOYSTICK 1
#define NO_USE_DEH 1
#define NO_USE_MUSIC_PACKS 1
#define USE_FLAT_MAX_256 1
#define USE_MEMMAP_ONLY 1
#define USE_LIGHTMAP_INDEXES 1
#define USE_ERASE_FRAME 1
#define NO_DRAW_MID 1
#define NO_DRAW_TOP 1
#define NO_DRAW_BOTTOM 1
#define NO_DRAW_MASKED 1
#define NO_DRAW_SKY 1
#define NO_DRAW_SPRITES 1
#define NO_DRAW_PSPRITES 1
#define NO_VISPLANE_GUTS 1
#define NO_VISPLANE_CACHES 1
#define NO_DRAWSEGS 1
#define NO_VISSPRITES 1
#define NO_MASKED_FLOOR_CLIP 1
#define PD_DRAW_COLUMNS 1
#define PD_DRAW_MARKERS 1
#define PD_DRAW_PLANES 1
#define PD_SCALE_SORT 1
#define PD_CLIP_WALLS 1
#define PD_QUANTIZE 1
#define PD_SANITY 1
#define PD_COLUMNS 1
#define PICO_DOOM 1
#define NO_USE_DS_COLORMAP 1
#define NO_USE_DC_COLORMAP 1
#define USE_READONLY_MMAP 1
#define NO_USE_TIMIDITY 1
#define NO_USE_GUS 1
#define NO_USE_LIBSAMPLERATE 1
#define NO_USE_STATE_MISC 1
#define USE_RAW_MAPNODE 1
#define USE_RAW_MAPVERTEX 1
#define USE_RAW_MAPSEG 1
#define USE_RAW_MAPLINEDEF 1
#define USE_RAW_MAPTHING 1
#define USE_INDEX_LINEBUFFER 1
#define NO_USE_ZLIGHT 1
#define NO_Z_ZONE_ID 1
#define Z_MALOOC_EXTRA_DATA 1
#define USE_THINKER_POOL 1
#define NO_INTERCEPTS_OVERRUN 1
#define TEMP_IMMUTABLE_DISABLED 1
#define USE_CONST_SFX 1
#define USE_CONST_MUSIC 1
#define NO_DEMO_RECORDING 1
#define PICO_NO_TIMING_DEMO 1
#define NO_USE_EXIT 1
#define NO_RDRAW 1
#define NO_USE_NET 1
#define SAVE_COMPRESSED 1
#define LOAD_COMPRESSED 1
#define NO_USE_ARGS 1
#define NO_FILE_ACCESS 1 /* (saves are Epsilon's files: nw/saves.c) */
#define NO_USE_SAVE_CONFIG 1
#define NO_USE_FLOAT 1
#define USE_VANILLA_KEYBOARD_MAPPING_ONLY 1
#define NO_USE_LOADING_DISK 1
#define USE_WHD 1
#define WHD_SUPER_TINY 1
#define WHD_SEG_ANGLES 1 /* (the segs keep their angles: whd_gen -lossless) */
#define NO_Z_MALLOC_USER_PTR 1
#define FIXED_SCREENWIDTH 1
#define FLOOR_CEILING_CLIP_8BIT 1
#define NO_SCREENSHOT 1
#define NO_USE_BOUND_CONFIG 1
#define USE_MEMORY_WAD 1
#define NO_ZONE_DEBUG 1
#define NO_USE_ENDDOOM 1

/* the renderer (pd_render.cpp), with room for this many runs of pixels in the view each frame */
#define PICODOOM_RENDER_NEWHOPE 1
#define MERGE_DISTSCALE0_INTO_VIEWCOSSINANGLE 1
// the frame's records (nw/nw_render.h): columns of wall and sky, and of floor and ceiling (a frame with more
// is drawn band by band)
#ifndef NW_POOL_KB
#define NW_POOL_KB 32
#endif
#define NW_RENDER_COL_MAX 4096
#define NW_PLANE_MAX 16384

/* the calculator: no speaker, one core, files in Epsilon's file system */
#define NO_USE_SOUND 1
#define NO_USE_MUSIC 1
#define NO_USE_MOUSE 1
#if !defined(HOST) && !defined(NP_PORT) /* (another target, a computer's 64-bit pointers: as the tests') */
#define NW_SHORTPTR 1 /* pointers into RAM in 16 bits, as on the RP2040 (doomtype.h) */
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>
#if !defined(HOST) && !defined(NP_PORT)
/* the calculator has no console and no files but Epsilon's: what the engine prints goes nowhere */
#include <stdio.h>
#define printf(...) ((void)0)
#define puts(s) ((void)0)
#define putchar(c) ((void)0)
#define fprintf(f, ...) ((void)0)
#define vfprintf(f, ...) ((void)0)
#define fputs(s, f) ((void)0)
#define fflush(f) ((void)0)
#endif
typedef unsigned int uint;
#define __fast_mul(a, b) ((a) * (b))
#define panic_unsupported() I_Error("unsupported")
#endif
#if defined(NP_PORT) && defined(__EMSCRIPTEN__) /* (the web's C library has no sys/cdefs.h) */
#define __CONCAT(x, y) x##y
#define __STRING(x) #x
#endif

#endif
