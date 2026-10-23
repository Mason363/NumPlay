/* A flat world-space road mesh and one perspective camera for the entire scene.
 * All coordinates remain fractional until their final LCD pixel conversion. */
#include <math.h>
#define TR_FOCAL 256.0f
#define TR_HEIGHT 4.5f
#define TR_HORIZON 84
typedef struct {float x,z;} TrVertex;
static TrVertex camera_point(const TrView*v,float x,float z){
 x-=v->eye.x;z-=v->eye.z;return(TrVertex){x*v->eye.rx+z*v->eye.rz,x*v->eye.fx+z*v->eye.fz};
}
static TrVertex road_edge(const TrView*v,TrRoadPoint p,float offset){return camera_point(v,p.x+p.tz*offset,p.z-p.tx*offset);}
bool tr_project(const TrView*v,float at,float offset,float*x,float*y,float*scale){
 TrRoadPoint p=tr_road_sample(&v->road,at);TrVertex a=road_edge(v,p,offset);if(a.z<3.5f)return false;
 *scale=TR_FOCAL/a.z;*x=160+a.x**scale;*y=TR_HORIZON+TR_HEIGHT**scale;return true;
}
static void road_quad(TrView*v,int node){
 TrRoadPoint a=v->road.nodes[node],b=v->road.nodes[node+1];
 TrVertex in[8]={road_edge(v,a,-7),road_edge(v,b,-7),road_edge(v,b,7),road_edge(v,a,7)},poly[8];int n=0;
 /* Clip the actual 3D quad before division, including bends behind the eye. */
 for(int i=0;i<4;i++){
   TrVertex p=in[i],q=in[(i+1)%4];bool pin=p.z>=3.5f,qin=q.z>=3.5f;
   if(pin)poly[n++]=p;
   if(pin!=qin){float t=(3.5f-p.z)/(q.z-p.z);poly[n++]=(TrVertex){p.x+(q.x-p.x)*t,3.5f};}
 }
 if(n<3)return;float low=240,high=0;
 for(int i=0;i<n;i++){poly[i].x=160+poly[i].x*TR_FOCAL/poly[i].z;poly[i].z=TR_HORIZON+TR_HEIGHT*TR_FOCAL/poly[i].z;if(poly[i].z<low)low=poly[i].z;if(poly[i].z>high)high=poly[i].z;}
 int first=NP_MAX(TR_HORIZON+1,(int)ceilf(low-.5f)),last=NP_MIN(239,(int)floorf(high-.5f));
 TrVertex ca=camera_point(v,a.x,a.z),cb=camera_point(v,b.x,b.z);
 for(int y=first;y<=last;y++){
   float scan=y+.5f,l=10000,r=-10000;
   for(int i=0;i<n;i++){TrVertex p=poly[i],q=poly[(i+1)%n];if((p.z<=scan&&q.z>scan)||(q.z<=scan&&p.z>scan)){float x=p.x+(q.x-p.x)*(scan-p.z)/(q.z-p.z);if(x<l)l=x;if(x>r)r=x;}}
   if(r<l)continue;int left=NP_CLAMP((int)floorf(l),-2000,2000),right=NP_CLAMP((int)ceilf(r),-2000,2000);
   TrRow*row=&v->rows[y];float depth=TR_HEIGHT*TR_FOCAL/(scan-TR_HORIZON),t=0;
   if(fabsf(cb.z-ca.z)>.0001f)t=(depth-ca.z)/(cb.z-ca.z);
   bool middle=t>=0&&t<=1;t=fminf(1,fmaxf(0,t));
   if(!row->width){row->left=left;row->right=right;}
   else if(right>=row->left-1&&left<=row->right+1){row->left=NP_MIN(row->left,left);row->right=NP_MAX(row->right,right);}
   else if(!middle)continue;else{row->left=left;row->right=right;}
   row->center=(row->left+row->right)/2;row->width=NP_MAX(1,(row->right-row->left)/2);row->depth=(uint16_t)depth;
   if(middle||!row->world)row->world=(int32_t)((v->road.start+(node+t)*TR_ROAD_STEP)*256);
 }
}
void tr_prepare(TrView*v,const TrRace*r,const TrProfile*p,uint32_t frame){
 v->race=r;v->profile=p;v->frame=frame;v->mood=r->mood;v->land=tr_tracks[r->track].landscape;v->rain=p->effects==2;
 v->progress=r->cars[0].distance/256.0f;v->meters=(int)v->progress;v->horizon=TR_HORIZON;v->focal=(int)TR_FOCAL;v->camera=r->camera_x;v->count=0;
 tr_road_build(&v->road,r->track,r->cars[0].distance);TrRoadPoint hero=tr_road_sample(&v->road,v->progress);
 float yaw=r->heading*.00015f,cs=cosf(yaw),sn=sinf(yaw);
 v->eye.fx=hero.tx*cs+hero.tz*sn;v->eye.fz=hero.tz*cs-hero.tx*sn;v->eye.rx=v->eye.fz;v->eye.rz=-v->eye.fx;
 float offset=r->camera_x*.007f;v->eye.x=hero.x+hero.tz*offset-v->eye.fx*10;v->eye.z=hero.z-hero.tx*offset-v->eye.fz*10;
 float scale;tr_project(v,v->progress,r->cars[0].x*.007f,&v->player_x,&v->player_y,&scale);v->player_w=2.8f*scale;
 float heading=tr_road_heading(r->track,r->distance_base+v->progress);
 v->pan=NP_CLAMP((int)(40+sinf(heading)*32),0,80);
 unsigned key=v->land*8+v->mood*2+v->rain;
 if(!v->palette_ready||v->palette_key!=key){
   for(int i=0;i<64;i++){color_t c=BACKGROUND_PALETTES[v->land][i];if(v->mood==1)c=gfx_mix(c,HEX(0xc37872),9);if(v->mood==2)c=gfx_mix(c,HEX(0x061225),24);if(v->rain)c=gfx_mix(c,HEX(0x74858e),v->mood==2?5:12);v->sky_palette[i]=c;}
   for(int a=0;a<8;a++)for(int i=0;i<64;i++){color_t c=SCENE_PALETTES[a][i];if(v->mood==1)c=gfx_mix(c,HEX(0xb37361),4);if(v->mood==2)c=gfx_mix(c,HEX(0x081b2d),19);v->scene_palette[a][i]=c;}
   v->palette_key=key;v->palette_ready=1;
 }
 memset(v->rows,0,sizeof v->rows);
 for(int i=TR_ROAD_NODES-2;i>=0;i--)road_quad(v,i);
 /* Fixed roadside landmarks. Passing them uses the same world projection as
  * opponents and the player's tire contact point, with no screen-row lookup. */
 int start=(int)floorf(v->progress/28);
 for(int i=start;i<start+18;i++){
   float at=i*28.0f;
   for(int side=-1;side<=1;side+=2){
     float x,y,s;if(v->count>=48||!tr_project(v,at,side*(9.8f+(i&3)*.7f),&x,&y,&s)||y>=240||y<85)continue;
     int w=NP_CLAMP((int)(s*2),1,52);if(x+w<0||x-w>320)continue;
     int kind=v->land;if(v->land==0&&i%6==0)kind=6;if(v->land==2&&(i&1))kind=5;if(v->land==3&&(i&1))kind=7;
     int bend=tr_curve(r->track,(int)at+60);if(i%7==0&&(bend>25||bend< -25))kind=4;
     v->objects[v->count++]=(TrObject){(int16_t)x,(int16_t)y,w,kind,(uint8_t)(i&7),(int32_t)at};
   }
 }
 for(int i=1;i<8;i++){
   if(!tr_car_active(r,i))continue;int32_t gap=tr_gap(r,r->cars[i].distance,r->cars[0].distance);float at=v->progress+gap/256.0f;
   if(gap< -8*256||gap>500*256)continue;float x,y,s;
   if(!tr_project(v,at,r->cars[i].x*.007f,&x,&y,&s)||y>=240||y<85||x<-120||x>440)continue;
   v->objects[v->count++]=(TrObject){(int16_t)x,(int16_t)y,NP_CLAMP((int)(s*2.8f),3,120),8,(uint8_t)i,(int32_t)at};
 }
 v->objects[v->count++]=(TrObject){(int16_t)v->player_x,(int16_t)v->player_y,(int16_t)v->player_w,9,0,(int32_t)v->progress};
 for(int i=1;i<v->count;i++){TrObject a=v->objects[i];int j=i;while(j&&v->objects[j-1].y>a.y){v->objects[j]=v->objects[j-1];j--;}v->objects[j]=a;}
}
