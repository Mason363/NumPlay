#ifndef ANGRY_DATA_H
#define ANGRY_DATA_H
#include <stdint.h>
#include "ids.h"
typedef struct { uint32_t offset; uint16_t w,h,ow,oh; int16_t px,py; uint16_t palette[16]; } Sprite;
typedef struct { uint8_t kind,count; float radius,cx,cy,area,inertia,vertices[16]; } Shape;
typedef struct { float density,friction,restitution,strength,defense; } Material;
enum { BIRD=1,PIG,BLOCK,STATIC,DECOR,TNT };
typedef struct { uint8_t shape,material,kind,frames,is_static; float health; uint16_t score,sprite[8]; } ObjectDef;
typedef struct { uint32_t offset; uint16_t packed,unpacked; uint32_t gold,silver,eagle; uint8_t theme; float left,top,right,bottom; const char *name; } Level;
typedef struct { uint16_t first,count,image; const char *name; } Group;
extern const uint8_t art_pixels[],level_bytes[],damage_matrix[],velocity_matrix[];
extern const Sprite sprites[];
extern const Shape shapes[];
extern const Material materials[];
extern const ObjectDef object_defs[];
extern const Level levels[];
extern const Group groups[];
extern const uint16_t background_ids[],font_ids[];
extern const uint16_t story_intro[],story_end[];
#endif
