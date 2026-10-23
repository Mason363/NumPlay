#include "road.h"
#include <math.h>
static float curvature(unsigned track,float s){
 const TrTrack*t=&tr_tracks[track%TR_TRACKS];float u=s/t->segment;int i=(int)floorf(u);float f=u-i;
 unsigned a=(i%32+32)%32;return (t->curves[a]*(1-f)+t->curves[(a+1)%32]*f)/12000.0f;
}
void tr_road_build(TrRoad*r,unsigned track,int32_t distance){
 /* The anchor advances only at fixed grid boundaries. Rebuilding is a rigid
  * translation/rotation of the same road, never a newly deformed screen curve. */
 r->start=(distance/(256*8)-2)*TR_ROAD_STEP;r->nodes[0]=(TrRoadPoint){0,0,0,1};
 for(int i=1;i<TR_ROAD_NODES;i++){
   TrRoadPoint a=r->nodes[i-1];float d=curvature(track,r->start+(i-.5f)*TR_ROAD_STEP)*TR_ROAD_STEP,d2=d*d;
   float sh=d*.5f-d*d2/48.0f,ch=1-d2/8.0f;
   float sn=d-d*d2/6.0f,cs=1-d2*.5f+d2*d2/24.0f;
   r->nodes[i]=(TrRoadPoint){a.x+TR_ROAD_STEP*(a.tx*ch+a.tz*sh),a.z+TR_ROAD_STEP*(a.tz*ch-a.tx*sh),a.tx*cs+a.tz*sn,a.tz*cs-a.tx*sn};
 }
}
TrRoadPoint tr_road_sample(const TrRoad*r,float meters){
 float u=(meters-r->start)/TR_ROAD_STEP;int i=(int)floorf(u);if(i<0)i=0,u=0;if(i>=TR_ROAD_NODES-1)i=TR_ROAD_NODES-2,u=TR_ROAD_NODES-1;
 float t=u-i,t2=t*t,t3=t2*t;TrRoadPoint a=r->nodes[i],b=r->nodes[i+1];
 float h00=2*t3-3*t2+1,h10=t3-2*t2+t,h01=-2*t3+3*t2,h11=t3-t2;
 float tx=a.tx+(b.tx-a.tx)*t,tz=a.tz+(b.tz-a.tz)*t,n=1.0f/sqrtf(tx*tx+tz*tz);
 return (TrRoadPoint){h00*a.x+h10*TR_ROAD_STEP*a.tx+h01*b.x+h11*TR_ROAD_STEP*b.tx,h00*a.z+h10*TR_ROAD_STEP*a.tz+h01*b.z+h11*TR_ROAD_STEP*b.tz,tx*n,tz*n};
}
float tr_road_heading(unsigned track,float meters){
 const TrTrack*t=&tr_tracks[track%TR_TRACKS];float length=t->segment*32.0f,lap=floorf(meters/length),s=meters-lap*length;
 int index=(int)(s/t->segment);float prefix=0,total=0;
 for(int i=0;i<32;i++){float a=(t->curves[i]+t->curves[(i+1)%32])*.5f*t->segment;total+=a;if(i<index)prefix+=a;}
 float part=s-index*t->segment,a=t->curves[index],b=t->curves[(index+1)%32];
 return (lap*total+prefix+a*part+(b-a)*part*part/(2*t->segment))/12000.0f;
}
