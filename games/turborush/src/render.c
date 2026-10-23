#include "render.h"
#include <string.h>
#define WHITE 0xffff
#define DARK HEX(0x0b1520)
#include "art.h"
static const uint8_t*const BACKGROUNDS[]={bg0_pixels,bg1_pixels,bg2_pixels,bg3_pixels};
static const color_t*const BACKGROUND_PALETTES[]={bg0_palette,bg1_palette,bg2_palette,bg3_palette};
static const uint8_t*const SCENERY[]={scene0_pixels,scene1_pixels,scene2_pixels,scene3_pixels,scene4_pixels,scene5_pixels,scene6_pixels,scene7_pixels};
static const color_t*const SCENE_PALETTES[]={scene0_palette,scene1_palette,scene2_palette,scene3_palette,scene4_palette,scene5_palette,scene6_palette,scene7_palette};
const color_t tr_paints[6]={HEX(0xdf202e),HEX(0xffca3b),HEX(0x2479d5),HEX(0x30ba80),HEX(0xf2f0de),HEX(0xa66ee0)};
static void line(int x,int y,int xx,int yy,color_t c){
 if(NP_MAX(y,yy)<gfx_y0||NP_MIN(y,yy)>=gfx_y1)return;
 int dx=xx-x,dy=yy-y,n=NP_MAX(dx<0?-dx:dx,dy<0?-dy:dy);if(!n){gfx_fill(x,y,1,1,c);return;}
 for(int i=0;i<=n;i++)gfx_fill(x+dx*i/n,y+dy*i/n,1,1,c);
}
static const uint8_t*const CARS[]={car0_pixels,car1_pixels,car2_pixels,car3_pixels,car4_pixels,car5_pixels};
static const color_t*const CAR_PALETTES[]={car_palette0,car_palette1,car_palette2,car_palette3,car_palette4,car_palette5};
static void vehicle(int x,int y,int w,color_t c,int steer,bool lights,bool boost,unsigned style){
 if(w<3||w>150)return;int h=w*64/96,l=x-w/2,top=y-h;
 if(y<gfx_y0||top>=gfx_y1||l>=320||l+w<=0)return;
 unsigned paint=0;for(int i=0;i<6;i++)if(c==tr_paints[i])paint=i;
 unsigned angle=steer< -1?0:steer>1?2:1;const uint8_t*src=CARS[(style%2)*3+angle];const color_t*pal=CAR_PALETTES[paint];
 gfx_rrect(l+w/12,y-w/14,w*5/6,NP_MAX(2,w/10),w/15,HEX(0x182429),180);
 for(int yy=NP_MAX(top,gfx_y0);yy<NP_MIN(y,gfx_y1);yy++){
   int sy=(yy-top)*64/h;
   for(int xx=NP_MAX(l,0);xx<NP_MIN(320,l+w);xx++){
     unsigned bit=(sy*96+(xx-l)*96/w)*6,at=bit/8;unsigned pair=src[at]|(unsigned)src[at+1]<<8,id=(pair>>(bit&7))&63;
     if(id!=63)gfx_buf[(yy-gfx_y0)*320+xx]=lights?gfx_mix(pal[id],HEX(0x142136),5):pal[id];
   }
 }
 if(boost){int h1=NP_MAX(2,w/8);gfx_fill(x-w/3,y-2,NP_MAX(1,w/20),h1,HEX(0x30bfff));gfx_fill(x+w/5,y-2,NP_MAX(1,w/20),h1,HEX(0x30bfff));}
}
void tr_car(int x,int y,int w,color_t c,int steer,bool lights,bool boost){vehicle(x,y,w,c,steer,lights,boost,0);}
#include "projection.h"
static void tree(const TrObject*o,const TrView*v){
 int w=o->w*2,h=w*112/64,left=o->x-w/2,top=o->y-h;
 if(o->y<gfx_y0||top>=gfx_y1||left>=320||left+w<=0)return;
 const uint8_t*src=SCENERY[o->kind];const color_t*pal=v->scene_palette[o->kind];
 bool flip=o->kind==4&&tr_curve(v->race->track,o->at+60)<0;
 for(int y=NP_MAX(top,gfx_y0);y<NP_MIN(o->y,gfx_y1);y++){
   int sy=(y-top)*112/h;
   for(int x=NP_MAX(0,left);x<NP_MIN(320,left+w);x++){
     int sx=(x-left)*64/w;if(flip)sx=63-sx;unsigned bit=(sy*64+sx)*6,at=bit/8,pair=src[at]|(unsigned)src[at+1]<<8,id=(pair>>(bit&7))&63;
     if(id!=63)gfx_buf[(y-gfx_y0)*320+x]=pal[id];
   }
 }
}
void tr_world(const TrView*v){
 bool night=v->mood==2,sunset=v->mood==1;int hz=v->horizon;
 const uint8_t*art=BACKGROUNDS[v->land];
 for(int y=gfx_y0;y<NP_MIN(gfx_y1,hz+1);y++){
   unsigned row=(unsigned)y*100/(hz+1);color_t*dest=gfx_buf+(y-gfx_y0)*320;
   for(int x=0;x<320;x++){unsigned bit=(row*400+x+v->pan)*6,at=bit/8;unsigned pair=art[at]|(unsigned)art[at+1]<<8;dest[x]=v->sky_palette[(pair>>(bit&7))&63];}
 }
 if(night&&!v->rain){for(int i=0;i<16;i++)gfx_fill((i*97+17)%320,(i*31+9)%30,1,1,HEX(0xb8ccd8));}
 color_t grass=night?HEX(0x172e25):HEX(0x4b8750);
 if(v->land==2)grass=night?HEX(0x49332e):HEX(0xbb8958);
 if(v->land==3)grass=night?HEX(0x27353a):HEX(0x758581);
 color_t asphalt=night?HEX(0x29343c):HEX(0x515b5f),fog=night?HEX(0x1e3046):sunset?HEX(0x9d8987):HEX(0x95b5b6);
 for(int y=NP_MAX(gfx_y0,hz+1);y<gfx_y1;y++){
   TrRow row=v->rows[y];int mist=NP_CLAMP((row.depth-90)/12,0,26);
   gfx_fill(0,y,320,1,gfx_mix(grass,fog,mist));if(!row.width)continue;
   int z=row.world;int phase=((z%(16*256))+16*256)%(16*256),curb=NP_MAX(1,row.width/22);
   color_t edge=((z/(6*256))&1)?HEX(0xd5d8c9):HEX(0xbd5145);
   gfx_fill(row.left-curb,y,row.right-row.left+2*curb,1,gfx_mix(edge,fog,mist));
   gfx_fill(row.left,y,row.right-row.left,1,gfx_mix(asphalt,fog,mist));
   color_t stripe=gfx_mix(night?HEX(0x9aaba8):HEX(0xd5dbcf),fog,mist);int sw=NP_MAX(1,row.width/50);
   gfx_fill(row.left+1,y,sw,1,stripe);gfx_fill(row.right-sw-1,y,sw,1,stripe);
   if(phase<8*256&&row.depth<240){gfx_fill(row.left+(row.right-row.left)/3,y,sw,1,stripe);gfx_fill(row.left+(row.right-row.left)*2/3,y,sw,1,stripe);}
   int length=tr_length(v->race->track)*256,linez=(z%length+length)%length;
   if(v->race->mode!=TR_FREE_RIDE&&linez<3*256){for(int k=0;k<16;k++){int x=row.left+(row.right-row.left)*k/16;gfx_fill(x,y,(row.right-row.left)/16+1,1,((k+linez/128)&1)?DARK:WHITE);}}
 }
 for(int i=0;i<v->count;i++){
   const TrObject*o=&v->objects[i];
   if(o->kind==9)vehicle(o->x,o->y,o->w,tr_paints[v->profile->paint],v->race->heading/50,night,v->race->boost,0);
   else if(o->kind==8){
     TrRoadPoint p=tr_road_sample(&v->road,o->at);float turn=p.tx*v->eye.rx+p.tz*v->eye.rz;
     vehicle(o->x,o->y,o->w,tr_paints[o->id%6],turn>.07f?4:turn<-.07f?-4:0,night,false,v->race->cars[o->id].style);
   }else tree(o,v);
 }
 if(v->rain){for(int i=0;i<45;i++){int x=(i*79+(v->race->time/11))%350-15,y=(i*43+v->race->time/4)%250-5;line(x,y,x-3,y+8,HEX(0x9eafb8));}}
 if(v->race->boost&&v->profile->effects){for(int i=0;i<5;i++){line(10+i*8,110+i*17,2+i*6,127+i*17,HEX(0x90c2cf));line(309-i*8,110+i*17,317-i*6,127+i*17,HEX(0x90c2cf));}}
}
static void num(char*s,unsigned n){char a[10];int i=0;do{a[i++]='0'+n%10;n/=10;}while(n);while(i)*s++=a[--i];*s=0;}
void tr_map(int x,int y,int w,int h,unsigned track,const TrRace*r){
 /* Compact closed course silhouettes, deliberately redrawn from scratch. */
 static const int8_t maps[6][24]={
 {4,8,15,7,19,25,31,32,43,29,47,18,59,20,67,10,72,15,68,37,44,48,19,39},
 {4,16,13,7,25,18,31,5,43,10,40,24,55,17,72,25,61,46,46,42,31,48,14,33},
 {5,5,28,7,20,20,40,18,38,5,66,9,58,25,73,39,49,46,36,33,22,45,7,31},
 {8,6,64,6,72,17,72,37,60,46,41,46,41,29,25,29,25,46,6,46,6,25,8,6},
 {4,7,20,5,34,21,49,5,70,13,56,28,73,40,52,48,38,30,27,46,6,34,18,20},
 {5,6,32,6,32,19,51,19,51,5,72,6,72,44,49,46,36,31,25,44,5,44,5,6}};
 const int8_t*m=maps[track%6];for(int i=0;i<12;i++){int j=(i+1)%12;line(x+m[i*2]*w/78,y+m[i*2+1]*h/52,x+m[j*2]*w/78,y+m[j*2+1]*h/52,HEX(0xe4e4d3));}
 if(r){int length=tr_length(track)*256,progress=r->cars[0].distance%length;int seg=(int)((int64_t)progress*12*256/length),i=seg/256,j=(i+1)%12,f=seg%256;
   int xx=x+(m[i*2]*(256-f)+m[j*2]*f)*w/(78*256),yy=y+(m[i*2+1]*(256-f)+m[j*2+1]*f)*h/(52*256);gfx_circle(xx,yy,2,HEX(0xffd34c),256);}
}
void tr_hud(const TrView*v){
 const TrRace*r=v->race;char s[40];
 bool free=r->mode==TR_FREE_RIDE;
 gfx_rrect(6,5,81,36,3,DARK,245);memcpy(s,"P0 / 8",7);s[1]='0'+r->rank;
 if(free){gfx_text(&np_font_small,13,19,"DISTANCE",WHITE,256);uint32_t meters=tr_distance(r);num(s,meters/1000);unsigned n=strlen(s);s[n++]='.';s[n++]='0'+meters/100%10;memcpy(s+n," KM",4);gfx_text(&np_font_small,13,35,s,HEX(0xffd052),256);}
 else{gfx_text(&np_font_body,13,21,s,WHITE,256);memcpy(s,"LAP 0 / 3",10);s[4]='0'+r->lap;gfx_text(&np_font_small,13,35,s,HEX(0xffd052),256);}
 gfx_rrect(112,5,96,25,3,DARK,245);
 unsigned t=r->time;num(s,t/60000);int n=strlen(s);s[n++]=':';s[n++]='0'+t/1000%60/10;s[n++]='0'+t/1000%10;s[n++]='.';s[n++]='0'+t/100%10;s[n]=0;
 gfx_text_center(&np_font_body,160,23,free?"FREE RIDE":s,WHITE,256);
 gfx_rrect(229,5,85,36,3,DARK,245);gfx_text_center(&np_font_small,271,19,free?"OVERTAKES":"NEXT BEND",HEX(0xbbced5),256);
 int bend=tr_curve(r->track,r->cars[0].distance/256+90);
 if(free)num(s,r->overtakes);gfx_text_center(&np_font_body,271,35,free?s:bend>25?"RIGHT >":bend< -25?"< LEFT":"STRAIGHT",WHITE,256);
 gfx_fill(0,204,320,36,DARK);gfx_fill(0,204,320,1,HEX(0x56717b));
 num(s,tr_speed_kmh(r));gfx_text(&np_font_title,8,232,s,WHITE,256);gfx_text(&np_font_small,68,217,"KM/H",HEX(0xbbced5),256);
 memcpy(s,"GEAR 0",7);s[5]='0'+r->gear;gfx_text(&np_font_small,68,232,s,HEX(0xffd052),256);
 gfx_text(&np_font_small,119,217,r->boost_empty?"CHARGING":"NITRO",HEX(0xbbced5),256);
 gfx_fill(119,224,87,8,HEX(0x2b404b));gfx_fill(119,224,r->nitro*87/1000,8,r->boost_empty?HEX(0x577b98):HEX(0x40b8ef));
 if(free){gfx_text(&np_font_small,223,217,"ENDLESS DRIVE",HEX(0xbbced5),256);gfx_text(&np_font_small,223,232,"Back: pause",WHITE,256);}
 else{gfx_text(&np_font_small,223,217,"DAMAGE",HEX(0xbbced5),256);num(s,r->damage/10);n=strlen(s);s[n++]='%';s[n]=0;
 gfx_text_right(&np_font_small,311,217,s,WHITE,256);gfx_fill(223,224,88,8,HEX(0x2b404b));gfx_fill(223,224,r->damage*88/1000,8,r->damage>700?HEX(0xff704f):HEX(0xe5ac5b));}
 if(r->phase==TR_READY){
   gfx_rrect(30,73,260,88,5,DARK,235);gfx_text_center(&np_font_body,160,96,tr_tracks[r->track].name,WHITE,256);
   s[0]='0'+(r->countdown+49)/50;s[1]=0;gfx_text_center(&np_font_title,160,132,s,HEX(0xffd052),256);
   gfx_text_center(&np_font_small,160,151,"Hold Up to accelerate",HEX(0xbbced5),256);
 }else if(!free&&r->ticks<195)gfx_text_center(&np_font_title,160,108,"GO!",HEX(0xffd052),256);
 else if(r->impact)gfx_text_center(&np_font_body,160,54,"CONTACT",HEX(0xffbc6d),256);
 else if(r->cars[0].x>1050||r->cars[0].x< -1050)gfx_text_center(&np_font_small,160,49,"OFF ROAD",HEX(0xffbc6d),256);
 else if(r->damage>800)gfx_text_center(&np_font_small,160,49,"CRITICAL DAMAGE",HEX(0xffbc6d),256);
}
