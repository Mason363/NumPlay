#ifndef TR_ROAD_H
#define TR_ROAD_H
#include "race.h"
#define TR_ROAD_NODES 72
#define TR_ROAD_STEP 8.0f
typedef struct {float x,z,tx,tz;} TrRoadPoint;
typedef struct {TrRoadPoint nodes[TR_ROAD_NODES];float start;} TrRoad;
/* A fixed world-distance grid, independent of frame rate or player offset. */
void tr_road_build(TrRoad*,unsigned track,int32_t distance);
TrRoadPoint tr_road_sample(const TrRoad*,float meters);
float tr_road_heading(unsigned track,float meters);
#endif
