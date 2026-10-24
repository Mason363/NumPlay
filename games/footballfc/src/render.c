#include "app.h"
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#define C(n) HEX(n)
#define INK C(0x0b1725)
#define PANEL C(0x152a3c)
#define EDGE C(0x2a4356)
#define MINT C(0x85e4c4)
#define GOLD C(0xffd349)
#define WHITE C(0xf2f5ef)
#define MUTED C(0xa5b8c2)
#define PAPER C(0x161d19)
#define TEXT C(0xe9e7d8)
#define NOTE C(0x9ea99c)
#define RED C(0xe39a7c)
#define GREEN C(0x466c4d)
#define FOCAL 640
#define HORIZON 124
#define RISE_Q (35*FC_Q*11/20)
static char*number(char*s,unsigned n){char b[12];int i=0;do{b[i++]='0'+n%10;n/=10;}while(n);while(i)*s++=b[--i];*s=0;return s;}
static char*text(char*s,const char*t){while(*t)*s++=*t++;*s=0;return s;}
static void small(int x,int y,const char*s,color_t c){gfx_text(&np_font_small,x,y,s,c,256);}
static void body(int x,int y,const char*s,color_t c){gfx_text(&np_font_body,x,y,s,c,256);}
static void center(int x,int y,const char*s,color_t c){gfx_text_center(&np_font_body,x,y,s,c,256);}
static void title(int x,int y,const char*s,color_t c){gfx_text(&np_font_title,x,y,s,c,256);}
static void line(int x0,int y0,int x1,int y1,color_t c){
 if(FC_MAX(y0,y1)<gfx_y0||FC_MIN(y0,y1)>=gfx_y1||FC_MAX(x0,x1)<0||FC_MIN(x0,x1)>=320)return;
 int n=FC_MAX(abs(x1-x0),abs(y1-y0));if(n>2000)return;if(!n){gfx_fill(x0,y0,1,1,c);return;}
 int start=0,end=n;if(y0!=y1){int a=(gfx_y0-y0)*n/(y1-y0),b=(gfx_y1-y0)*n/(y1-y0);start=FC_MAX(0,FC_MIN(a,b)-2);end=FC_MIN(n,FC_MAX(a,b)+2);}
 for(int i=start;i<=end;i++)gfx_fill(x0+(x1-x0)*i/n,y0+(y1-y0)*i/n,1,1,c);
}
static void tri(int ax,int ay,int bx,int by,int cx,int cy,color_t c){
 int low=FC_MAX(gfx_y0,FC_MIN(ay,FC_MIN(by,cy))),high=FC_MIN(gfx_y1-1,FC_MAX(ay,FC_MAX(by,cy)));
 for(int y=low;y<=high;y++){int xs[3],n=0;int xx[]={ax,bx,cx,ax},yy[]={ay,by,cy,ay};
  for(int i=0;i<3;i++)if((yy[i]<=y&&yy[i+1]>y)||(yy[i+1]<=y&&yy[i]>y))xs[n++]=xx[i]+(xx[i+1]-xx[i])*(y-yy[i])/(yy[i+1]-yy[i]);
  if(n>=2)gfx_fill(FC_MIN(xs[0],xs[1]),y,abs(xs[0]-xs[1])+1,1,c);
 }
}
static void rect(int x,int y,int w,int h,color_t c){gfx_fill(x,y,w,h,c);}
const char*fc_player_name(unsigned team,unsigned player){
 static const char*const names[]={"Martin","Silva","Costa","Reed","Santos","Weber","Rossi","Sato","Diallo","Meyer","Garcia","Park","Baker","Moreau","Lima","Marin","Roux","Ibrahim","Lewis","Kovac","Torres","Novak","Alves","Tanaka","Benali","Cruz","Walker","Sow","Diaz","Nordin","Hassan","King"};
 return names[(team*7+player*3)%32];
}
static void flag(unsigned team,int x,int y,int w,int h){
 color_t red=C(0xd84148),blue=C(0x234db1),green=C(0x258250),yellow=C(0xffd34b),white=WHITE,black=C(0x20282c);
 color_t a=red,b=white,c=blue;bool vertical=false;unsigned id=team%32;
 switch(id){case 0:a=blue;b=white;c=red;vertical=true;break;case 2:a=C(0x82c5ec);b=white;c=a;break;case 4:a=red;b=yellow;c=red;break;case 6:a=black;b=red;c=yellow;break;case 7:a=red;b=white;c=blue;break;case 8:a=green;b=white;c=red;vertical=true;break;case 9:a=black;b=yellow;c=red;vertical=true;break;case 10:a=red;b=white;c=blue;break;case 12:a=b=c=red;break;case 13:case 21:a=yellow;b=blue;c=red;break;case 14:case 20:a=b=c=white;break;case 16:a=green;b=white;c=red;vertical=true;break;case 17:case 18:a=b=c=red;break;case 19:a=green;b=yellow;c=red;vertical=true;break;case 22:a=red;b=white;c=red;break;case 23:a=red;b=blue;c=white;break;case 24:case 31:a=b=c=blue;break;case 25:a=red;b=white;c=red;vertical=true;break;case 26:a=red;b=white;c=black;break;case 27:a=green;b=white;c=green;vertical=true;break;case 28:a=white;b=white;c=red;break;case 29:a=red;b=yellow;c=green;break;case 30:a=white;b=white;c=red;break;default:break;}
 if(id==1){gfx_fill(x,y,w,h,green);tri(x+w/2,y+1,x+1,y+h/2,x+w/2,y+h-1,yellow);tri(x+w/2,y+1,x+w-1,y+h/2,x+w/2,y+h-1,yellow);gfx_circle(x+w/2,y+h/2,FC_MAX(2,h/4),blue,256);}
 else if(id==3){gfx_fill(x,y,w,h,white);gfx_fill(x+w*2/5,y,FC_MAX(2,w/6),h,red);gfx_fill(x,y+h*2/5,w,FC_MAX(2,h/5),red);}
 else if(id==5){gfx_fill(x,y,w,h,red);gfx_fill(x,y,w*2/5,h,green);gfx_circle(x+w*2/5,y+h/2,FC_MAX(1,h/5),yellow,256);}
 else if(id==11){gfx_fill(x,y,w,h,white);for(int i=0;i<4;i++)gfx_fill(x,y+i*h/4,w,FC_MAX(1,h/9),blue);gfx_fill(x,y,w/3,h/2,white);gfx_circle(x+w/6,y+h/4,FC_MAX(1,h/8),yellow,256);}
 else if(id==15){gfx_fill(x,y,w,h,white);for(int i=0;i<7;i++)gfx_fill(x,y+i*h/7,w,FC_MAX(1,h/14),red);gfx_fill(x,y,w*2/5,h*3/5,blue);}
 else if(vertical){gfx_fill(x,y,w/3,h,a);gfx_fill(x+w/3,y,w/3,h,b);gfx_fill(x+w*2/3,y,w-w*2/3,h,c);}
 else{gfx_fill(x,y,w,h,a);gfx_fill(x,y+h/3,w,h/3,b);gfx_fill(x,y+h*2/3,w,h-h*2/3,c);}
 if(id==4||id==13||id==21)gfx_fill(x,y+h/4,w,h/2,yellow);
 if(id==14)gfx_circle(x+w/2,y+h/2,FC_MAX(2,h/4),red,256);
 if(id==17){gfx_fill(x+w/2-1,y+h/5,3,h*3/5,white);gfx_fill(x+w/3,y+h/2-1,w/3,3,white);}
 if(id==18){gfx_fill(x+w/3,y,FC_MAX(1,w/12),h,white);gfx_fill(x,y+h/2,w,FC_MAX(1,h/8),white);}
 if(id==24||id==31){gfx_fill(x,y,w/2,h/2,white);gfx_fill(x+w/5,y,w/10,h/2,red);gfx_fill(x,y+h/5,w/2,h/10,red);gfx_circle(x+w*3/4,y+h/2,1,white,256);gfx_circle(x+w*2/3,y+h*3/4,1,white,256);}
 if(id==28||id==30)gfx_fill(x,y+h/2,w,h-h/2,red);
 if(id==30){gfx_fill(x,y,w/3,h/2,blue);gfx_fill(x+w/6,y+h/4,1,1,white);}
 if(id==20){gfx_circle(x+w/2,y+h/2,FC_MAX(2,h/4),red,256);gfx_fill(x+w/2-h/4,y+h/2,h/2,h/5,blue);}
 if(id==25){tri(x+w/2,y+h/5,x+w*2/5,y+h*3/4,x+w*3/5,y+h*3/4,red);}
 gfx_rrect_outline(x,y,w,h,2,1,WHITE,160);
}
/* Dark match-programme layout: ruled lists and restrained team colours. */
static void menu_surface(void){gfx_fill(0,0,320,240,PAPER);gfx_fill(0,0,320,6,GREEN);gfx_fill(18,218,284,1,C(0x3e4b40));}
static void header(const char*s,const char*sub){menu_surface();title(18,35,s,TEXT);small(19,54,sub,NOTE);gfx_fill(18,62,284,1,TEXT);}
static void footer(const char*s){small(18,234,s,NOTE);}
static void item(int x,int y,int w,const char*s,bool active){
 gfx_fill(x,y,w,21,active?GREEN:PAPER);gfx_fill(x,y+20,w,1,C(0x3e4b40));
 body(x+9,y+15,s,TEXT);if(active)small(x+w-17,y+15,">",TEXT);
}
static void value_row(int y,const char*label,const char*value,bool active){
 gfx_fill(18,y+26,284,1,C(0x3e4b40));if(active)gfx_fill(18,y+5,3,15,RED);
 small(28,y+18,label,NOTE);gfx_text_right(&np_font_body,280,y+19,value,active?RED:TEXT,256);if(active)small(288,y+18,">",RED);
}
int fc_project(const FCApp*a,int x,int y,int height,int*sx,int*sy){
 const FCMatch*m=fc_view(a);
 if(fc_free_view(m)){
  int ox,oy,fx,fy;fc_free_basis(m,&ox,&oy,&fx,&fy);
  int depth=10*FC_Q+((x-ox)*fx+(y-oy)*fy)/FC_Q;if(depth<2*FC_Q)return 0;
  *sx=160+((x-ox)*fy-(y-oy)*fx)/FC_Q*340/depth;*sy=52+(1205-height)*340/depth;return depth;
 }
 int transverse=x-a->cam_x+35*FC_Q,depth=56*FC_Q+transverse*7/20;
 if(depth<6*FC_Q)return 0;*sx=160+(y-a->cam_y)*FOCAL/depth;*sy=HORIZON+(RISE_Q-transverse*11/20-height)*FOCAL/depth;return depth;
}
/* Read packed materials directly from flash and paint only visible strip rows.
 * There is no full atlas, decompression buffer or per-frame allocation in RAM. */
static void sprite(const uint8_t*data,int sw,int sh,int x,int y,int w,int h,bool mirror,const color_t*pal){
 if(w<=0||h<=0||x+w<=0||x>=320||y+h<=gfx_y0||y>=gfx_y1)return;
 for(int row=FC_MAX(gfx_y0,y);row<FC_MIN(gfx_y1,y+h);row++){
  int sy=(row-y)*sh/h,start=0,previous=0;
  for(int col=0;col<=w;col++){
   int material=0;if(col<w){int sx=col*sw/w;if(mirror)sx=sw-1-sx;int n=sy*sw+sx;material=(data[n/2]>>(n&1?0:4))&15;}
   if(col==0){previous=material;continue;}
   if(material!=previous||col==w){if(previous)gfx_fill(x+start,row,col-start,1,pal[previous]);start=col;previous=material;}
  }
 }
}
static void worldline(const FCApp*a,int x0,int y0,int x1,int y1,color_t c){
 int wx0=x0*FC_Q,wy0=y0*FC_Q,wx1=x1*FC_Q,wy1=y1*FC_Q;const FCMatch*m=fc_view(a);
 if(fc_free_view(m)){
  int ox,oy,fx,fy;fc_free_basis(m,&ox,&oy,&fx,&fy);int f0=((wx0-ox)*fx+(wy0-oy)*fy)/FC_Q,f1=((wx1-ox)*fx+(wy1-oy)*fy)/FC_Q,limit=-7*FC_Q;
  if(f0<limit&&f1<limit)return;
  if(f0<limit){wx0+=(wx1-wx0)*(limit-f0)/(f1-f0);wy0+=(wy1-wy0)*(limit-f0)/(f1-f0);}
  else if(f1<limit){wx1+=(wx0-wx1)*(limit-f1)/(f0-f1);wy1+=(wy0-wy1)*(limit-f1)/(f0-f1);}
 }
 int ax,ay,bx,by;if(!fc_project(a,wx0,wy0,0,&ax,&ay)||!fc_project(a,wx1,wy1,0,&bx,&by))return;line(ax,ay,bx,by,c);
}
static void worldrect(const FCApp*a,int x,int y,int w,int h){worldline(a,x,y,x+w,y,WHITE);worldline(a,x+w,y,x+w,y+h,WHITE);worldline(a,x+w,y+h,x,y+h,WHITE);worldline(a,x,y+h,x,y,WHITE);}
static void circle_world(const FCApp*a,int x,int y,int radius){
 static const int16_t circle[25][2]={{256,0},{247,66},{222,128},{181,181},{128,222},{66,247},{0,256},{-66,247},{-128,222},{-181,181},{-222,128},{-247,66},{-256,0},{-247,-66},{-222,-128},{-181,-181},{-128,-222},{-66,-247},{0,-256},{66,-247},{128,-222},{181,-181},{222,-128},{247,-66},{256,0}};
 int px=0,py=0;bool prev=false;for(int i=0;i<=24;i++){int sx,sy;bool ok=fc_project(a,x*256+circle[i][0]*radius,y*256+circle[i][1]*radius,0,&sx,&sy)!=0;if(ok&&prev)line(px,py,sx,sy,WHITE);px=sx;py=sy;prev=ok;}
}
static void stadium(unsigned venue,unsigned time,int edge,int pan){
 static const uint32_t roof[]={0x536557,0x5d4e4b,0x223b42};
 gfx_fill(0,0,320,240,C(0x263b34));
 color_t pal[16]={0,INK,C(0x476b90),C(0x914b3c),C(0xb6a36c),WHITE,C(0xbbbba3),C(0xdfbb8e),C(0xb78663),INK,C(0x668c64),INK,GOLD,C(0x92a17c),C(0x65765e),C(0x394e40)};
 /* Original animated crowd tiles follow the far touchline, so the seating
  * never floats over the grass as the close camera pans across the pitch. */
 for(int row=0;row<5;row++){
  int y=edge-20-row*18;if(y+16<=gfx_y0||y>=gfx_y1)continue;
  for(int x=-32;x<352;x+=32)sprite(fc_crowd_pixels+((time/330+row+x/32+30)%3)*256,32,16,x-pan%32,y,32,16,false,pal);
  gfx_fill(0,y+16,320,2,C(0x71816e));
  for(int x=91-pan%98;x<320;x+=98){gfx_fill(x,y,5,18,C(0x828e7b));for(int h=3;h<18;h+=4)gfx_fill(x,y+h,5,1,C(0x42564a));}
 }
 /* Banner bays break up seating tiers without baking text into a texture. */
 for(int x=-64;x<384;x+=160)sprite(fc_banner_pixels,64,12,x-pan%160,edge-41,64,8,false,pal);
 gfx_fill(0,edge-4,320,3,C(0xc6cbb8));gfx_fill(0,edge-1,320,1,C(0x5f7160));
 if(edge>85){gfx_fill(0,0,320,13,C(roof[venue%3]));line(0,12,319,12,C(0xd1d4c2));
  for(int x=0;x<320;x+=40){line(x,0,x+20,12,C(0x9caa96));line(x+20,12,x+40,0,C(0x9caa96));}}
}
static void touchline_props(const FCApp*a){
 color_t pal[16]={0,INK,C(0x3e607e),C(0x304a53),C(0x6a91a0),WHITE,C(0xbfcac5),C(0xdfbb8e),C(0xb78663),C(0x352d2b),C(0x34683c),INK,C(0xf0ba3f),C(0x92a17c),C(0x65765e),C(0x394e40)};
 for(int end=0;end<2;end++){
  int x=0,y=0,depth=fc_project(a,FC_W/2+FC_Q,FC_H/2+(end?12:-12)*FC_Q,0,&x,&y);
  if(depth)sprite(fc_bench_pixels,64,24,x-27,y-16,54,20,end!=0,pal);
 }
 for(int n=0;n<5;n++){
  int x=0,y=0,depth=fc_project(a,FC_W/2+FC_Q,(10+n*23)*FC_Q,0,&x,&y);
  if(depth){int h=FC_CLAMP(FOCAL*7*FC_Q/4/depth,10,19);sprite(fc_steward_pixels,12,24,x-h/4,y-h,h/2,h,n&1,pal);}
 }
 for(int end=0;end<2;end++){
  int x=0,y=0,depth=fc_project(a,FC_W/2+FC_Q,(end?FC_LENGTH-3:3)*FC_Q,0,&x,&y);
  if(depth)sprite(fc_camera_pixels,24,24,x-9,y-17,18,18,end!=0,pal);
 }
}
static void goal(const FCApp*a,int end){
 int gy=end?FC_H:0,back=gy+(end?640:-640),x[8],y[8];
 for(int i=0;i<8;i++)if(!fc_project(a,(i&1)?FC_GOAL_HALF:-FC_GOAL_HALF,(i&2)?back:gy,(i&4)?FC_GOAL_HEIGHT:0,&x[i],&y[i]))return;
 color_t net=C(0x9eb4a7),shade=C(0x547b67);
 /* Roof, rear mesh and side panels retain perspective at either end. */
 for(int j=0;j<=6;j++){
  int bx=x[2]+(x[3]-x[2])*j/6,by=y[2]+(y[3]-y[2])*j/6;
  int tx=x[6]+(x[7]-x[6])*j/6,ty=y[6]+(y[7]-y[6])*j/6;
  line(bx,by,tx,ty,net);line(x[4]+(x[5]-x[4])*j/6,y[4]+(y[5]-y[4])*j/6,tx,ty,net);
 }
 for(int j=0;j<=3;j++){
  line(x[2]+(x[6]-x[2])*j/3,y[2]+(y[6]-y[2])*j/3,x[3]+(x[7]-x[3])*j/3,y[3]+(y[7]-y[3])*j/3,net);
  for(int side=0;side<2;side++)line(x[side]+(x[side+4]-x[side])*j/3,y[side]+(y[side+4]-y[side])*j/3,x[side+2]+(x[side+6]-x[side+2])*j/3,y[side+2]+(y[side+6]-y[side+2])*j/3,shade);
 }
 line(x[0]+1,y[0],x[4]+1,y[4],shade);line(x[1]+1,y[1],x[5]+1,y[5],shade);
 line(x[0],y[0],x[4],y[4],WHITE);line(x[1],y[1],x[5],y[5],WHITE);line(x[4],y[4],x[5],y[5],WHITE);
}
static int floor_div(int a,int b){int q=a/b;return q-(a%b<0);}
/* Intersect a ground scanline with world bounds, then draw complete spans.
 * Avoid per-pixel perspective/classification work on the older calculators. */
static bool ground_clip(int value,int step,int low,int high,int*left,int*right){
 if(!step)return value>=low&&value<=high;
 if(step<0){value=-value;step=-step;int old=low;low=-high;high=-old;}
 *left=FC_MAX(*left,-floor_div(value-low,step));*right=FC_MIN(*right,floor_div(high-value,step));return *left<=*right;
}
static void pitch(const FCApp*a){
 const FCMatch*m=fc_view(a);bool front=fc_free_view(m);
 if(front){
  int ox,oy,fx,fy;fc_free_basis(m,&ox,&oy,&fx,&fy);int norm=FC_MAX(1,fx*fx+fy*fy),sx=0,sy=90;
  fc_project(a,0,fc_attack(m,m->free_team)>0?FC_H+5*FC_Q:-5*FC_Q,0,&sx,&sy);stadium(a->profile.venue,a->ui_time,sy,0);
  color_t grass[4]={0,a->profile.venue==2?C(0x185746):C(0x357c3f),a->profile.venue==2?C(0x286e50):C(0x458f46),C(0x497650)};
  for(int y=FC_MAX(gfx_y0,53);y<gfx_y1;y++){
   int d=1205*340/(y-52),f=d-10*FC_Q,side=-160*d/340;
   int wx=(int)((int64_t)(f*fx+side*fy)*65536/norm)+ox*256,wy=(int)((int64_t)(f*fy-side*fx)*65536/norm)+oy*256;
   int dx=(int)((int64_t)d*fy*65536/340/norm),dy=(int)(-(int64_t)d*fx*65536/340/norm),l=0,r=319;
   if(!ground_clip(wx,dx,-(FC_W/2+4*FC_Q)*256,(FC_W/2+4*FC_Q)*256,&l,&r)||!ground_clip(wy,dy,-4*FC_Q*256,(FC_H+4*FC_Q)*256,&l,&r))continue;
   gfx_fill(l,y,r-l+1,1,grass[3]);
   if(!ground_clip(wx,dx,-FC_W/2*256,FC_W/2*256,&l,&r)||!ground_clip(wy,dy,0,FC_H*256,&l,&r))continue;
   while(l<=r){
    int at=wy+dy*l,band=at/(8*FC_Q*256),run=r-l+1;
    if(dy>0)run=FC_MIN(run,FC_MAX(1,-floor_div(at-(band+1)*(8*FC_Q*256),dy)));
    else if(dy<0)run=FC_MIN(run,(at-band*(8*FC_Q*256))/(-dy)+1);
    gfx_fill(l,y,run,1,grass[1+(band&1)]);l+=run;
   }
  }
 }else{
 int farx=0,fary=0;fc_project(a,FC_W/2+2*FC_Q,a->cam_y,0,&farx,&fary);stadium(a->profile.venue,a->ui_time,fary,a->cam_y*6/FC_Q);
 for(int y=gfx_y0;y<gfx_y1;y++){
  int transverse=(RISE_Q*FOCAL*20-56*FC_Q*(y-HORIZON)*20)/(11*FOCAL+7*(y-HORIZON));int depth=56*FC_Q+transverse*7/20,wx=a->cam_x-35*FC_Q+transverse;
  if(wx>FC_W/2){
   if(wx<FC_W/2+FC_Q)gfx_fill(0,y,320,1,C(0x82967c));
   else if(wx<FC_W/2+2*FC_Q){gfx_fill(0,y,320,1,a->profile.venue==2?C(0x173f36):C(0xe4dfc5));for(int x=0;x<320;x+=78)gfx_fill(x,y,2,1,INK);}
   continue;
  }
  int lightness=FC_CLAMP((y-38)/22,0,8);color_t dark=gfx_mix(a->profile.venue==2?C(0x175b4a):C(0x337d3c),C(0x68a452),lightness),light=gfx_mix(a->profile.venue==2?C(0x267958):C(0x408e45),C(0x76b05b),lightness);
  if(a->profile.venue==1){dark=gfx_mix(dark,GOLD,2);light=gfx_mix(light,GOLD,2);}
  gfx_fill(0,y,320,1,C(0x235e44));
  /* Run-off grass, then a service track behind each goal. */
  int left=160+(-5*FC_Q-a->cam_y)*FOCAL/depth,right=160+(FC_H+5*FC_Q-a->cam_y)*FOCAL/depth;
  color_t track=(wx/(2*FC_Q)&1)?C(0x344a50):C(0x30464c);
  gfx_fill(0,y,FC_MAX(0,left),1,track);gfx_fill(right,y,320-right,1,track);
  if(y%8==0){gfx_fill(0,y,FC_MAX(0,left),1,C(0x4b6064));gfx_fill(right,y,320-right,1,C(0x4b6064));}
  if(wx>=-FC_W/2){
   int first=FC_CLAMP((a->cam_y-160*depth/FOCAL)/(8*FC_Q),0,13),last=FC_CLAMP((a->cam_y+160*depth/FOCAL)/(8*FC_Q),0,13);
   for(int band=first;band<=last;band++){
    int start=band*8*FC_Q,end=FC_MIN(FC_H,start+8*FC_Q);
    int l=160+(start-a->cam_y)*FOCAL/depth,r=160+(end-a->cam_y)*FOCAL/depth;
    color_t grass=band&1?light:dark;gfx_fill(l,y,r-l,1,grass);
    /* Mowing bands carry the texture; avoid crawling screen-space grain. */
   }
  }
 }
 }
 touchline_props(a);worldrect(a,-FC_HALF_WIDTH,0,2*FC_HALF_WIDTH,FC_LENGTH);worldline(a,-FC_HALF_WIDTH,FC_LENGTH/2,FC_HALF_WIDTH,FC_LENGTH/2,WHITE);circle_world(a,0,FC_LENGTH/2,9);
 worldrect(a,-20,0,40,16);worldrect(a,-20,FC_LENGTH-16,40,16);worldrect(a,-9,0,18,6);worldrect(a,-9,FC_LENGTH-6,18,6);
 circle_world(a,0,11,1);circle_world(a,0,FC_LENGTH-11,1);
 int cx,cy;if(fc_project(a,0,FC_H/2,0,&cx,&cy))gfx_fill(cx-1,cy,3,1,WHITE);
 for(int end=0;end<2;end++)for(int side=0;side<2;side++){
  int x,y,tx,ty;if(fc_project(a,(side?FC_W/2:-FC_W/2),end?FC_H:0,0,&x,&y)&&fc_project(a,(side?FC_W/2:-FC_W/2),end?FC_H:0,360,&tx,&ty)){
   line(x,y,tx,ty,C(0xe5e4cb));tri(tx,ty,tx+4,ty+1,tx,ty+4,GOLD);
  }
 }
 goal(a,0);goal(a,1);
}
static void limb(int ax,int ay,int bx,int by,int width,color_t c){
 int dx=bx-ax,dy=by-ay,d=FC_MAX(abs(dx),abs(dy));if(!d)return;int px=-dy*width/d,py=dx*width/d;
 tri(ax+px,ay+py,ax-px,ay-py,bx+px,by+py,c);tri(ax-px,ay-py,bx-px,by-py,bx+px,by+py,c);
}
static void ellipse(int x,int y,int rx,int ry,color_t c,int alpha){
 if(!ry||!rx)return;
 for(int row=FC_MAX(-ry,gfx_y0-y);row<=FC_MIN(ry,gfx_y1-1-y);row++){
  int width=rx*(ry*ry-row*row)/(ry*ry);gfx_fill_alpha(x-width,y+row,width*2+1,1,c,alpha);
 }
}
static color_t timing_color(unsigned quality){return quality>=75?C(0x72df6a):quality>=40?C(0xf0b44c):C(0xee655c);}
static void timing_ring(int x,int y,unsigned clock){
 static const int8_t v[17][2]={{64,0},{59,24},{45,45},{24,59},{0,64},{-24,59},{-45,45},{-59,24},{-64,0},{-59,-24},{-45,-45},{-24,-59},{0,-64},{24,-59},{45,-45},{59,-24},{64,0}};
 unsigned quality=fc_timing(clock);int radius=6+(100-(int)quality)/7;color_t c=timing_color(quality);
 for(int i=0;i<16;i++)line(x+v[i][0]*radius/64,y+v[i][1]*radius/64,x+v[i+1][0]*radius/64,y+v[i+1][1]*radius/64,c);
}
static void football(int x,int y,int r){
 gfx_circle(x+1,y+1,r,INK,80);gfx_circle(x,y,r,WHITE,256);
 gfx_circle(x-r/3,y-r/3,FC_MAX(1,r/3),C(0x263c49),256);
 if(r>5){line(x,y-r,x+1,y-r/2,C(0x5c6c72));line(x+r/2,y,x+r,y-1,C(0x5c6c72));line(x,y+r/2,x-1,y+r,C(0x5c6c72));gfx_circle(x-r/2,y-r/2,1,WHITE,256);}
}
static void bone(int ax,int ay,int bx,int by,int radius,color_t color){
 limb(ax,ay,bx,by,radius,color);if(radius>1){gfx_circle(ax,ay,radius,color,256);gfx_circle(bx,by,radius,color,256);}
}
static void footballer(int x,int y,int h,color_t shirt,color_t shorts,int pose,bool keeper,int facing,unsigned look){
 int w=FC_MAX(4,h*19/50),head=FC_MAX(2,h/9),thick=FC_MAX(1,h/25);bool back=look&256;look&=255;
 if(y-h>=gfx_y1||y+4<gfx_y0||x+w<0||x-w>=320)return;
 static const uint32_t skins[]={0xe0b185,0xc98f68,0x9b644b,0x704936},hairs[]={0x352d2b,0x252725,0x785539,0xb68f51};
 color_t skin=C(skins[look%4]),shade=gfx_mix(skin,INK,8),dark=gfx_mix(shirt,INK,12),shine=gfx_mix(shirt,WHITE,6);
 bool kick=pose&32;pose&=31;int stride=(pose<12?pose:24-pose)-6,step=stride*h/65,bob=abs(stride)>3?1:0;
 int hip=y-h*7/20,shoulder=y-h*3/4-bob,lx=x-w/4-step,rx=x+w/4+step,ly=y-FC_MAX(0,step/2),ry=y-FC_MAX(0,-step/2);
 if(kick){lx+=(facing?facing:1)*h/5;ly-=h/7;}
 ellipse(x+2,y+1,w,FC_MAX(1,h/20),C(0x102f25),100);
 int knee=y-h/6;bone(x+w/5,hip,rx+step/3,knee,thick,shade);bone(rx+step/3,knee,rx,ry,thick,shade);
 bone(x-w/5,hip,lx-step/3,knee,thick,skin);bone(lx-step/3,knee,lx,ly,thick,skin);
 bone(lx,ly-h/7,lx,ly,thick,WHITE);bone(rx,ry-h/7,rx,ry,thick,C(0xcad6c4));
 gfx_fill(lx-thick,ly-h/7,thick*2+1,1,shirt);gfx_fill(rx-thick,ry-h/7,thick*2+1,1,dark);
 gfx_fill(lx-thick,ly-1,thick*3+1,2,C(0xf1bd42));gfx_fill(rx-thick,ry-1,thick*3+1,2,INK);
 gfx_fill(x-w/2,hip-h/7,w,h/7+2,shorts);gfx_fill(x,hip-h/7,FC_MAX(1,w/2),h/7+2,gfx_mix(shorts,INK,9));
 int arm=step/2,elbow=y-h/2;bone(x-w/2,shoulder+2,x-w+arm,elbow,thick,shade);bone(x+w/2,shoulder+2,x+w-arm,elbow-abs(step)/2,thick,skin);
 tri(x-w/2,shoulder,x+w/2,shoulder,x+w/2-1,hip-h/9,shirt);tri(x-w/2,shoulder,x-w/2+1,hip-h/9,x+w/2-1,hip-h/9,shirt);
 tri(x+1,shoulder,x+w/2,shoulder,x+w/2-1,hip-h/9,dark);gfx_fill(x-w/2+1,shoulder+2,FC_MAX(1,w/5),FC_MAX(2,h/5),shine);
 bone(x-w/2,shoulder+1,x-w*3/4,shoulder+h/9,thick+1,shirt);bone(x+w/2,shoulder+1,x+w*3/4,shoulder+h/9,thick+1,dark);
 gfx_fill(x-1,shoulder-2,2,3,shade);int hx=x+facing*head/3,hy=y-h+head-bob;
 gfx_circle(hx,hy,head,skin,256);gfx_circle(hx,hy-head/2,head,C(hairs[(look/3)%4]),256);
 if(!back){gfx_fill(hx-head+1,hy,head*2-1,head,skin);if(h>24){gfx_fill(hx+(facing?facing:1),hy,1,1,INK);if(!facing)gfx_fill(hx-2,hy,1,1,INK);}}
 if(h>28){gfx_fill(x-w/4,shoulder+1,w/2,1,WHITE);gfx_fill(x-1,shoulder+h/7,3,1,WHITE);gfx_fill(x+1,shoulder+h/7,1,FC_MAX(2,h/10),WHITE);}
 if(keeper){gfx_circle(x-w+arm,elbow,FC_MAX(1,h/24),GOLD,256);gfx_circle(x+w-arm,elbow-abs(step)/2,FC_MAX(1,h/24),GOLD,256);}
}
static void diving_keeper(int x,int y,int h,color_t shirt,int dir,unsigned look){
 int spread=h/2,hipx=x-dir*spread/3,headx=x+dir*spread/2,mid=y-h/3;int thick=FC_MAX(1,h/14);
 ellipse(x+2,y+1,h/2,FC_MAX(1,h/16),INK,90);limb(hipx,mid,x+dir*spread/4,mid-h/8,thick+1,shirt);
 limb(hipx,mid,x-dir*spread,mid+h/6,thick,WHITE);limb(hipx,mid,x-dir*spread/2,mid+h/3,thick,WHITE);
 limb(x,mid-h/8,x+dir*spread,mid-h/5,thick,C(0xddb185));gfx_circle(x+dir*spread,mid-h/5,thick,GOLD,256);
 static const uint32_t skins[]={0xe0b185,0xc98f68,0x9b644b,0x704936};gfx_circle(headx,mid-h/5,FC_MAX(2,h/9),C(skins[look%4]),256);
}
static color_t kit(const FCApp*a,int team){
 unsigned id=a->match.teams[team];const FCTeam*t=&fc_teams[id];uint32_t c=t->shirt;
 if(!team&&a->profile.kit)c=t->away;
 if(team){uint32_t home=a->profile.kit?fc_teams[a->match.teams[0]].away:fc_teams[a->match.teams[0]].shirt;
  int dr=(int)(home>>16)-(int)(c>>16),dg=(int)(home>>8&255)-(int)(c>>8&255),db=(int)(home&255)-(int)(c&255);
  if(abs(dr)+abs(dg)+abs(db)<150)c=t->away;
  if(c==home)c=0xec55a3;
 }return C(c);
}
static void players(const FCApp*a){
 const FCMatch*m=fc_view(a);bool front=fc_free_view(m);int focal=front?340:FOCAL,ox,oy,fx,fy,order[23];fc_free_basis(m,&ox,&oy,&fx,&fy);for(int i=0;i<23;i++)order[i]=i;
 for(int i=0;i<23;i++)for(int j=i+1;j<23;j++){int u=order[i],v=order[j];int d1=front?(u==22?m->ball.x*fx+m->ball.y*fy:m->p[u].x*fx+m->p[u].y*fy):u==22?m->ball.x:m->p[u].x,d2=front?(v==22?m->ball.x*fx+m->ball.y*fy:m->p[v].x*fx+m->p[v].y*fy):v==22?m->ball.x:m->p[v].x;if(d2>d1){int t=order[i];order[i]=order[j];order[j]=t;}}
 for(int n=0;n<23;n++){int i=order[n],x,y,depth;if(i<22&&!fc_active(m,i))continue;
  if(i==22){int sx=0,sy=0;bool ground=fc_project(a,m->ball.x,m->ball.y,0,&sx,&sy)!=0;if(ground)ellipse(sx+1,sy+1,3,1,INK,110);
   depth=fc_project(a,m->ball.x,m->ball.y,m->ball.z,&x,&y);if(depth){int r=FC_CLAMP(focal*FC_Q/8/depth,2,front?5:3);if(ground&&m->ball.z>3*FC_Q)for(int row=sy-2;row>y;row-=4)gfx_fill(sx,row,1,1,C(0xa9c0a4));color_t pal[16]={0,INK,0,0,0,WHITE,C(0xcad3bb)};unsigned rotation=(abs(m->ball.x)+abs(m->ball.y)+abs(m->ball.z))/64%4;sprite(fc_ball_pixels+rotation*32,8,8,x-r,y-r*2,r*2+1,r*2+1,false,pal);}continue;}
  const FCPlayer*p=&m->p[i];depth=fc_project(a,p->x,p->y,fc_wall_lift(m,i),&x,&y);if(!depth)continue;int h=FC_CLAMP(focal*7*FC_Q/4/depth,11,front?68:28),t=i/11;
  if(front&&i==m->taker){x-=9;y+=8;}
  if(x< -h||x>320+h||y<gfx_y0-4||y-h>gfx_y1)continue;
  if(i==m->selected){line(x-3,y+2,x,y+5,GOLD);line(x,y+5,x+3,y+2,GOLD);gfx_fill(x-2,y-h-5,5,2,GOLD);}
  if((m->mode==FC_TRAINING&&m->drill==1&&i==m->drill_target)||i==m->ball.receiver){tri(x-4,y-h-9,x+4,y-h-9,x,y-h-5,MINT);}
  if(i==a->preview_target&&m->pass_charge){line(x-4,y+3,x,y+6,C(0x64e0dd));line(x,y+6,x+4,y+3,C(0x64e0dd));}
  if(i==m->selected&&(m->input_latch&FC_CONTROL)){line(x-4,y+1,x+4,y+1,C(0x64e0dd));}
  if(i==fc_keeper(m,t)&&m->keeper_dive[t]>6)diving_keeper(x,y,h,C(t?0xef9a38:0x65cb94),m->p[i].vx<0?-1:1,(i+m->teams[t]*3)|(m->motion[i].face_x>100?256:0));
  else if(front&&h>32){const FCMotion*f=&m->motion[i];int pose=fc_distance(p->vx,p->vy)<150?6:p->pose;if(f->action&&m->ball.last==i&&m->ball.owner<0)pose|=32;
   footballer(x,y,h,kit(a,t),C(fc_teams[m->teams[t]].shorts),pose,i==fc_keeper(m,t),FC_CLAMP((f->face_x*fy-f->face_y*fx)/256/100,-1,1),(i+m->teams[t]*3)|(f->face_x*fx+f->face_y*fy>0?256:0));
  }else{
   const FCMotion*f=&m->motion[i];int ax=abs(f->face_y),ay=f->face_x;
   int angle=abs(ay)<ax/2?2:abs(ay)>ax*2?(ay>0?4:0):(ay>0?3:1);
   int frame=fc_distance(p->vx,p->vy)<150?0:1+p->pose/6;
   if(f->action&&m->ball.last==i&&m->ball.owner<0)frame=5;
   color_t shirt=i!=fc_keeper(m,t)?kit(a,t):C(t?0xef9a38:0x65cb94),shorts=C(fc_teams[m->teams[t]].shorts);
   static const uint32_t skins[]={0xe0b185,0xc98f68,0x9b644b,0x704936},hairs[]={0x352d2b,0x252725,0x785539,0xb68f51};
   color_t skin=C(skins[(i+m->teams[t])%4]);color_t pal[16]={0,INK,shirt,gfx_mix(shirt,INK,10),gfx_mix(shirt,WHITE,9),WHITE,C(0xcad3bb),skin,gfx_mix(skin,INK,7),C(hairs[(i/3+m->teams[t])%4]),shorts,gfx_mix(shorts,INK,10),GOLD};
   int w=h*2/3;ellipse(x+1,y+1,FC_MAX(2,w/3),1,C(0x102f25),105);
   sprite(fc_player_pixels+(angle*6+frame)*192,16,24,x-w/2,y-h,w,h,f->face_y<0,pal);
  }
 }
}
static void minimap(const FCMatch*m){
 gfx_rrect(258,195,55,38,3,INK,180);color_t marking=C(0x87b19d);
 line(261,198,309,198,marking);line(261,230,309,230,marking);line(261,198,261,230,marking);line(309,198,309,230,marking);line(285,198,285,230,marking);
 gfx_fill(fc_attack(m,0)>0?310:259,211,2,6,GOLD);
 for(int i=0;i<22;i++)if(fc_active(m,i)){int x=262+m->p[i].y*46/FC_H,y=228-(m->p[i].x+FC_W/2)*29/(FC_W);gfx_fill(x,y,i==m->selected?2:1,2,i<11?GOLD:WHITE);}
 int x=262+FC_CLAMP(m->ball.y,0,FC_H)*46/FC_H,y=228-FC_CLAMP(m->ball.x+FC_W/2,0,FC_W)*29/(FC_W);gfx_fill(x,y,2,2,C(0xff8364));
}
static void free_preview(const FCApp*a){
 const FCMatch*m=fc_view(a);if(m->phase!=FC_FREE||m->set_team)return;
 int vx,vy,vz;fc_free_velocity(m,&vx,&vy,&vz);int wx=m->ball.x,wy=m->ball.y,wz=FC_Q/5,x=0,y=0;
 if(fc_project(a,wx,wy,0,&x,&y))timing_ring(x,y-2,m->kick_clock);
 for(int tick=0;tick<100;tick++){
  wx+=vx/40;wy+=vy/40;wz+=vz/40;vz-=64;int old=vx;vx+=vy*m->curve*fc_attack(m,0)/24000;vy-=old*m->curve*fc_attack(m,0)/24000;
  if(wz<0)break;vx=vx*999/1000;vy=vy*999/1000;
  if(abs(wx)>FC_W/2||wy<0||wy>FC_H)break;
  if(tick%4==0&&fc_project(a,wx,wy,wz,&x,&y))gfx_fill(x-1,y-1,2,2,C(0xaff0d0));
 }
 if(fc_free_view(m))return;
 char b[60],*q=text(b,"Power ");q=number(q,m->power/10);q=text(q,"%   ");q=text(q,m->kick_height<200?"Low":m->kick_height<450?"Mid":"High");q=text(q,"   Curl ");q=text(q,m->curve<0?"L ":m->curve>0?"R ":"0");if(m->curve)number(q,abs(m->curve));
 gfx_fill(38,149,244,17,INK);gfx_text_center(&np_font_small,160,161,b,WHITE,256);
}
static void free_hud(const FCApp*a){
 const FCMatch*m=fc_view(a);char b[64],*q;
 gfx_fill(8,7,128,20,INK);small(16,21,"FREE KICKS",WHITE);gfx_fill(144,7,168,20,INK);
 if(m->mode==FC_FREEPLAY){q=text(b,"Goals ");q=number(q,m->free_goals);q=text(q," / ");number(q,m->free_attempts);gfx_text_center(&np_font_small,228,21,b,GOLD,256);}
 else{q=text(b,fc_teams[m->teams[0]].code);q=text(q,"  ");q=number(q,m->score[0]);q=text(q," - ");q=number(q,m->score[1]);q=text(q,"  ");text(q,fc_teams[m->teams[1]].code);gfx_text_center(&np_font_small,228,21,b,WHITE,256);}
 if(m->phase==FC_FREE&&m->set_team){gfx_fill(69,31,182,18,INK);gfx_text_center(&np_font_small,160,44,"Opponent free kick",WHITE,256);return;}
 if(m->card_timer&&m->card_kind){gfx_fill(8,53,102,17,INK);small(15,65,m->card_kind==1?"YELLOW CARD":"RED CARD",m->card_kind==1?GOLD:C(0xe45348));}
 if(m->phase==FC_FREE){
  gfx_fill(40,31,240,17,INK);gfx_text_center(&np_font_small,160,43,"Left/Right Aim   Up/Down Power",WHITE,256);
  gfx_fill(8,168,86,57,INK);q=text(b,"Power ");q=number(q,m->power/10);text(q,"%");small(15,181,b,WHITE);
  small(15,195,m->kick_height<200?"Low shot":m->kick_height<450?"Mid shot":"High shot",WHITE);
  q=text(b,"Curl ");q=text(q,m->curve<0?"L ":m->curve>0?"R ":"0");if(m->curve)number(q,abs(m->curve));small(15,209,b,MINT);
  gfx_fill(216,168,96,m->mode==FC_FREEPLAY?68:57,INK);small(223,181,"8/9 Curl",WHITE);small(223,195,"5 Height",WHITE);small(223,209,"EXE: release",GOLD);small(223,222,"on green",GOLD);
  if(m->mode==FC_FREEPLAY)small(223,233,"6 New spot",MUTED);
 }else{
  const char*label=m->phase==FC_GOAL?"GOAL!":m->mode==FC_FREEPLAY&&m->phase!=FC_LIVE?(m->free_result==FC_PEN_GOAL?"GOAL!":m->free_result==FC_PEN_SAVE?"SAVED / BLOCKED":"WIDE / HIGH"):m->kick_quality>=75?"GOOD TIMING":m->kick_quality>=40?"OK TIMING":"POOR TIMING";
  gfx_fill(92,33,136,18,INK);gfx_text_center(&np_font_small,160,46,label,timing_color(m->kick_quality),256);
  if(m->mode==FC_FREEPLAY&&m->phase!=FC_LIVE){gfx_fill(224,212,88,20,INK);small(232,226,"OK Next kick",WHITE);}
 }
}
static void hud(const FCApp*a){
 const FCMatch*m=fc_view(a);char b[48],*s;
 gfx_rrect(8,7,182,20,3,INK,245);gfx_fill(8,7,3,20,GOLD);
 if(m->mode==FC_TRAINING){small(18,21,fc_drill_name(m->drill),WHITE);s=number(b,m->drill_hits);s=text(s,"/");number(s,m->drill==1?10:6);small(105,21,b,GOLD);}
 else{small(18,21,fc_teams[m->teams[0]].code,WHITE);s=number(b,m->score[0]);s=text(s," - ");number(s,m->score[1]);gfx_text_center(&np_font_small,78,21,b,GOLD,256);small(105,21,fc_teams[m->teams[1]].code,WHITE);}
 gfx_fill(137,11,1,12,EDGE);unsigned seconds=m->mode==FC_TRAINING?(m->duration-m->clock)/40:m->duration?m->clock*90*60/m->duration:0;s=number(b,seconds/60);*s++=':';*s++='0'+seconds%60/10;*s++='0'+seconds%10;*s=0;small(147,21,b,WHITE);
 if(m->tactic_notice&&m->mode!=FC_TRAINING){gfx_rrect(207,29,105,17,3,INK,235);gfx_text_center(&np_font_small,260,41,fc_tactic_name(m->tactics[0]),GOLD,256);}
 gfx_rrect(251,7,61,17,3,INK,215);small(259,19,fc_attack(m,0)>0?"ATTACK >":"< ATTACK",GOLD);
 minimap(m);
 gfx_rrect(8,211,112,22,3,INK,210);small(15,223,fc_player_name(m->teams[0],m->selected%11),WHITE);
 gfx_fill(15,227,96,2,EDGE);gfx_fill(15,227,m->p[m->selected].stamina*96/1000,2,MINT);
 if(m->yellow[m->selected])gfx_fill(111,215,4,6,GOLD);
 for(int t=0;t<2;t++){unsigned reds=0;for(int i=t*11;i<(t+1)*11;i++)reds+=m->red[i]!=0;if(reds){gfx_fill(197+t*24,9,5,8,C(0xe45348));number(b,reds);small(205+t*24,18,b,WHITE);}}
 if(m->card_timer&&m->card_kind){gfx_rrect(86,59,148,25,3,INK,240);gfx_fill(94,65,7,12,m->card_kind==1?GOLD:C(0xe45348));small(108,70,m->card_kind==1?"YELLOW CARD":m->card_kind==3?"SECOND YELLOW":"RED CARD",WHITE);s=text(b,fc_teams[m->teams[m->card_player/11]].code);s=text(s,"  #");number(s,m->card_player%11+1);small(108,80,b,GOLD);}
 if(m->phase!=FC_FREE&&(m->charge||m->pass_charge)){
  unsigned power=m->charge?m->charge:m->pass_charge;gfx_rrect(127,215,92,18,3,INK,230);
  small(134,226,m->charge?((m->input_latch&FC_CONTROL)?"FINESSE":"SHOT"):m->pass_kind==FC_HIGH_PASS?"LOB":m->pass_kind==FC_RUN_PASS?"THROUGH":"PASS",WHITE);
  gfx_fill(134,229,77,2,EDGE);gfx_fill(134,229,FC_MIN(power,1000u)*77/1000,2,m->charge?GOLD:MINT);
 }
 /* Controls live in the pause/help pages; no permanent bottom banner. */
 if(m->phase!=FC_LIVE&&m->phase!=FC_FULL){
  const char*name=m->mode==FC_TRAINING?(m->phase==FC_GOAL?"GOAL!":"TRY AGAIN"):m->phase==FC_KICKOFF?"KICK OFF":m->phase==FC_GOAL?"GOAL!":m->phase==FC_HALF?"HALF TIME":m->phase==FC_THROW?"THROW IN":m->phase==FC_CORNER?"CORNER":m->phase==FC_GOALKICK?"GOAL KICK":m->phase==FC_PENALTY?"PENALTY":"FREE KICK";
  gfx_rrect(102,34,116,22,3,INK,240);gfx_text_center(&np_font_body,160,50,name,GOLD,256);
  if(m->notice&&!m->card_timer){gfx_rrect(108,58,104,16,3,INK,230);gfx_text_center(&np_font_small,160,70,m->notice==2?"Offside":"Foul",WHITE,256);}
  if(m->mode!=FC_TRAINING&&m->phase>=FC_THROW&&m->set_team==0){
   if(m->phase==FC_FREE){gfx_rrect(18,171,284,34,3,INK,235);small(25,183,"Up/Down Aim   Left/Right Power   8/9 Curl",WHITE);small(25,198,"5 Height   EXE Hold/release   OK Pass",GOLD);}
   else{gfx_rrect(24,177,221,27,3,INK,225);small(32,188,"Up/Down Aim   Left/Right Power",WHITE);small(32,200,"OK / EXE  Take restart",GOLD);}
  }
  else if(m->phase==FC_KICKOFF){gfx_rrect(113,182,94,16,3,INK,210);gfx_text_center(&np_font_small,160,194,"Back: controls",MUTED,256);}
 }
}
static int blend(int a,int b,int t){return a+(b-a)*t/256;}
static void penalty_keeper(const FCMatch*m){
 int x=160+m->shoot_keeper*112/FC_GOAL_HALF,age=(int)m->shoot_timer-FC_PEN_RUNUP-(m->shoot_turn?2:10-m->difficulty*2);
 if(m->shoot_state==1||age<=0||abs(m->shoot_goal-m->shoot_start)<130){
  footballer(x,146,51,C(0xdf863d),C(0x294c45),6,true,0,m->teams[1-m->shoot_turn]*3);
  if(m->shoot_state==2&&age>0){int gy=146-m->shoot_guard_z*78/FC_GOAL_HEIGHT;bone(x-7,118,x-4,gy,2,C(0xdf863d));bone(x+7,118,x+4,gy,2,C(0xdf863d));gfx_circle(x-4,gy,3,GOLD,256);gfx_circle(x+4,gy,3,GOLD,256);}return;
 }
 int t=FC_CLAMP(age*24,0,256),dir=m->shoot_goal>m->shoot_start?1:-1;
 int fall=m->shoot_resolved?FC_CLAMP(((int)m->shoot_timer-FC_PEN_RUNUP-m->shoot_flight-8)*7,0,150):0;
 int glove=blend(146-m->shoot_guard_z*78/FC_GOAL_HEIGHT,143,fall),cx=x-dir*blend(0,13,t),cy=blend(118,glove+8,t),hipx=x-dir*blend(0,28,t),hipy=blend(132,glove+16,t);
 ellipse(hipx,148,27,3,INK,100);
 bone(hipx,hipy,x-dir*blend(5,45,t),FC_MIN(146,hipy+13),3,WHITE);bone(hipx+dir*3,hipy,x-dir*blend(-5,34,t),FC_MIN(146,hipy+23),3,C(0xd7dfd0));
 bone(hipx,hipy,cx,cy,5,C(0xdf863d));bone(cx,cy,cx+dir*2,cy-4,4,C(0xf0ac51));
 bone(cx,cy-2,x,glove,2,C(0xdf863d));bone(cx,cy+3,x-dir*2,glove+4,2,C(0xdf863d));
 gfx_circle(cx+dir*5,cy-7,5,C(0xc98f68),256);gfx_fill(cx+dir*5-3,cy-12,6,2,C(0x352d2b));
 gfx_circle(x,glove,3,GOLD,256);gfx_circle(x-dir*2,glove+4,3,GOLD,256);
}
static void shootout(const FCApp*a){
 const FCMatch*m=fc_view(a);stadium(a->profile.venue,a->ui_time,83,0);
 gfx_vgrad(0,82,320,158,0x457c49,0x255c3c);
 /* Goal roof and side netting sit behind the full-width front frame. */
 tri(48,70,272,70,263,63,C(0xa0ad94));tri(48,70,57,63,263,63,C(0x899b85));
 for(int x=57;x<=263;x+=10)line(x,65,x,139,C(0x9bae94));for(int y=65;y<=139;y+=8)line(57,y,263,y,C(0x8da68b));
 for(int y=76;y<146;y+=8){line(48,y,57,y-7,C(0xc1cfb7));line(272,y,263,y-7,C(0xc1cfb7));}
 gfx_fill(46,68,228,4,WHITE);gfx_fill(46,68,4,81,WHITE);gfx_fill(270,68,4,81,WHITE);line(49,149,271,149,C(0xdef0d2));
 /* Penalty-area lines converge to the goal, leaving the bottom free of HUD. */
 line(0,240,57,149,C(0xa7c293));line(319,240,263,149,C(0xa7c293));line(94,225,226,225,C(0xa7c293));ellipse(160,219,3,1,WHITE,256);
 penalty_keeper(m);
 unsigned run=m->shoot_state==2?FC_MIN(m->shoot_timer,FC_PEN_RUNUP):0;
 int kx=144+(int)run*11/FC_PEN_RUNUP,ky=237-(int)run*19/FC_PEN_RUNUP;
 int pose=m->shoot_state==2&&m->shoot_timer>=FC_PEN_RUNUP-2&&m->shoot_timer<FC_PEN_RUNUP+8?38:m->shoot_state==2&&m->shoot_timer<FC_PEN_RUNUP?6+(int)run:6;
 footballer(kx,ky,58,kit(a,m->shoot_turn),C(fc_teams[m->teams[m->shoot_turn]].shorts),pose,false,1,256+m->teams[m->shoot_turn]*3);
 int distance=FC_CLAMP(m->ball.y,-3*FC_Q,11*FC_Q),scale=256+distance*128/(11*FC_Q);
 int bx=160+m->ball.x*112*scale/FC_GOAL_HALF/256,ground=148+distance*71/(11*FC_Q),by=ground-m->ball.z*78/FC_GOAL_HEIGHT;
 ellipse(bx,ground,FC_CLAMP(2+distance/(4*FC_Q),2,5),1,INK,100);football(bx,by-2,FC_CLAMP(2+distance/(4*FC_Q),2,5));
 if(m->shoot_state==1&&!m->shoot_turn)timing_ring(bx,by-2,m->kick_clock);
 if(m->shoot_state==1&&!m->shoot_turn){int tx=160+m->shoot_target*112/FC_GOAL_HALF,ty=148-m->shoot_height*78/FC_GOAL_HEIGHT;
  line(tx-5,ty,tx+5,ty,GOLD);line(tx,ty-5,tx,ty+5,GOLD);gfx_circle(tx,ty,2,GOLD,100);
 }
 char b[48],*q;gfx_fill(8,6,191,20,INK);small(16,20,fc_teams[m->teams[0]].code,WHITE);q=number(b,m->single_penalty?m->score[0]:m->shoot_score[0]);q=text(q," - ");number(q,m->single_penalty?m->score[1]:m->shoot_score[1]);gfx_text_center(&np_font_small,92,20,b,GOLD,256);small(151,20,fc_teams[m->teams[1]].code,WHITE);
 q=text(b,m->single_penalty?"PENALTY":"ROUND ");if(!m->single_penalty)number(q,m->shoot_round+1);gfx_fill(239,6,73,20,INK);small(247,20,b,WHITE);
 if(m->shoot_state==1){gfx_fill(12,31,296,18,INK);gfx_text_center(&np_font_small,160,44,m->shoot_turn?"KEEPER: arrows move / height. OK ready":"Aim: arrows   Hold EXE / release on green",WHITE,256);}
 else if(m->shoot_resolved){const char*result=m->shoot_result==FC_PEN_GOAL?"GOAL":m->shoot_result==FC_PEN_SAVE?"SAVED":m->shoot_result==FC_PEN_POST?"POST":"WIDE / HIGH";gfx_fill(107,31,106,19,INK);center(160,45,result,GOLD);}
 if(m->charge&&m->shoot_state==1){gfx_fill(15,175,57,29,INK);small(21,187,"POWER",WHITE);gfx_fill(21,193,45,3,C(0x5a7260));gfx_fill(21,193,m->charge*45/1000,3,m->charge>900?C(0xf07d54):GOLD);}
 if(m->shoot_state==2&&!m->shoot_turn&&!m->shoot_resolved){gfx_fill(115,53,90,15,INK);gfx_text_center(&np_font_small,160,64,m->kick_quality>=75?"GOOD TIMING":m->kick_quality>=40?"OK TIMING":"POOR TIMING",timing_color(m->kick_quality),256);}
 if(m->single_penalty&&m->card_timer&&m->card_kind){gfx_fill(12,53,97,16,INK);gfx_fill(17,56,5,9,m->card_kind==1?GOLD:C(0xe45348));small(27,65,m->card_kind==1?"YELLOW CARD":"RED CARD",WHITE);}
 /* A small five-kick tally replaces the former large instruction footer. */
 if(!m->single_penalty)for(int team=0;team<2;team++)for(int i=0;i<5;i++)gfx_fill(12+i*8,53+team*5,5,2,i<m->shoot_taken[team]?((m->shoot_marks[team]&(1u<<i))?GOLD:C(0x9ca99b)):C(0x597264));
}
static void home(const FCApp*a){
 menu_surface();title(18,32,"FOOTBALL FC",TEXT);small(19,49,"By Bobydeluxe",NOTE);flag(a->profile.team,270,19,31,20);gfx_fill(18,57,284,2,TEXT);
 static const char*const options[]={"Kick off","Career","World Cup","Penalties","Free kicks","Training","Settings","How to play"};
 for(int i=0;i<8;i++)item(18,64+i*19,163,options[i],a->selected==i);
 /* Stadium plan printed alongside the fixture list. */
 gfx_fill(198,68,104,135,GREEN);gfx_fill(203,74,94,122,C(0x7b9272));gfx_fill(209,81,82,108,C(0xbac3a0));gfx_fill(216,89,68,93,C(0x47814a));
 for(int row=0;row<10;row++)for(int col=0;col<12;col++){int x=204+col*8,y=76+row*12;if(col>1&&col<10&&row>0&&row<9)continue;gfx_fill(x,y,2,3,(row+col)&1?TEXT:C(0x506b4c));}
 line(220,94,279,94,TEXT);line(220,178,279,178,TEXT);line(220,94,220,178,TEXT);line(279,94,279,178,TEXT);line(220,136,279,136,TEXT);gfx_circle(250,136,9,TEXT,160);gfx_circle(250,136,8,C(0x47814a),256);
 gfx_fill(238,92,24,3,TEXT);gfx_fill(238,178,24,3,TEXT);footer("Up/Down Choose   OK Open   Back Exit");
}
static void team_card(unsigned id,int x,int y,bool selected,const char*role){
 const FCTeam*t=&fc_teams[id];rect(x,y,137,85,selected?C(0x2d4131):C(0x202c23));
 gfx_fill(x,y+5,3,75,selected?RED:EDGE);small(x+10,y+15,role,selected?RED:NOTE);flag(id,x+100,y+8,26,16);
 footballer(x+27,y+65,37,C(t->shirt),C(t->shorts),6,false,0,id*3);
 small(x+50,y+38,"ATK  MID  DEF",NOTE);char b[24],*s=number(b,t->attack);s=text(s,"   ");s=number(s,t->midfield);s=text(s,"   ");number(s,t->defense);small(x+50,y+53,b,TEXT);
 gfx_text_center(&np_font_body,x+68,y+79,t->name,TEXT,256);
 if(selected){small(x+5,y+78,"<",RED);small(x+125,y+78,">",RED);}
}
static void setup(const FCApp*a){
 header(a->mode==FC_FRIENDLY?"KICK OFF":a->mode==FC_CAREER?"NEW CAREER":a->mode==FC_CUP?"WORLD CUP":a->mode==FC_FREEPLAY?"FREE KICKS":"PENALTIES","Left/Right changes teams and match settings.");
 team_card(a->profile.team,18,65,a->selected==0,"YOUR TEAM");team_card(a->profile.opponent,165,65,a->selected==1,a->mode==FC_FRIENDLY||a->mode==FC_SHOOTOUT||a->mode==FC_FREEPLAY?"OPPONENT":"PREVIEW");
 static const char*const diff[]={"Easy","Normal","Hard"};
 rect(18,155,137,29,a->selected==2?C(0x2d4131):C(0x202c23));small(27,167,"DIFFICULTY",a->selected==2?RED:NOTE);small(27,179,diff[a->profile.difficulty],TEXT);if(a->selected==2)small(141,175,">",RED);
 char b[20];char*s=number(b,2+a->profile.length*2);text(s," minutes");rect(165,155,137,29,a->selected==3?C(0x2d4131):C(0x202c23));small(174,167,a->mode==FC_FREEPLAY?"SESSION":"MATCH LENGTH",a->selected==3?RED:NOTE);small(174,179,a->mode==FC_FREEPLAY?"Unlimited kicks":b,TEXT);if(a->selected==3)small(288,175,">",RED);
 item(18,192,284,a->mode==FC_FREEPLAY?"Start free kicks":"Start match",a->selected==4);footer(a->selected==4?(a->mode==FC_FREEPLAY?"OK Start session    Back Home":"OK Start match    Back Home"):"Up/Down Select   Left/Right Change");
}
static void career(const FCApp*a){
 const FCProfile*p=&a->profile;char b[64],*s;header("CAREER",fc_teams[p->career_team].name);
 rect(18,65,284,54,C(0x202c23));s=text(b,"SEASON ");s=number(s,p->season);s=text(s,"   ");s=number(s,FC_MIN(7,p->career_round+1));text(s," / 7");small(28,79,b,RED);
 s=number(b,p->money);text(s," CR");gfx_text_right(&np_font_small,292,79,b,TEXT,256);
 flag(p->career_team,28,89,25,15);
 s=text(b,p->career_round==7?"League position: ":"vs ");if(p->career_round==7)number(s,fc_rank(p,0));else text(s,fc_teams[fc_career_opponent(p)].name);body(65,102,b,TEXT);
 for(int i=0;i<7;i++)gfx_fill(28+i*37,111,32,2,i<p->career_round?GREEN:EDGE);
 const char*options[]={p->career_round==7?"Start next season":"Play next match","Squad & contracts","League table","Transfers & coach"};
 for(int i=0;i<4;i++)item(18,125+i*23,284,options[i],a->selected==i);footer("OK Select    EXE New career    Back Home");
}
static void squad(const FCApp*a){
 const FCProfile*p=&a->profile;char b[64],*s;header("SQUAD & CONTRACTS","Select a player to train or renew their contract.");
 small(28,71,"PLAYER",NOTE);small(207,71,"OVR",NOTE);small(249,71,"GAMES",NOTE);
 int first=(a->selected/5)*5;for(int row=0;row<5&&first+row<11;row++){
  int i=first+row,y=77+row*21;bool active=i==a->selected;rect(18,y,284,20,active?C(0x2d4131):C(0x202c23));if(active)gfx_fill(18,y+4,3,12,RED);
  s=number(b,i+1);small(28,y+14,b,NOTE);small(51,y+14,fc_player_name(p->career_team,i),TEXT);
  number(b,p->roster[i]);small(211,y+14,b,active?RED:TEXT);number(b,p->contracts[i]);small(261,y+14,b,p->contracts[i]?GREEN:C(0xff877b));
 }
 int i=FC_CLAMP(a->selected,0,10);s=text(b,"OK  Train +2  /  ");s=number(s,fc_train_price(p,i));text(s," CR");small(20,194,b,TEXT);
 s=text(b,"EXE Renew ");s=number(s,a->contract_games);s=text(s," games / ");s=number(s,a->contract_games*(25+p->roster[i]/2));text(s," CR");small(20,208,b,TEXT);
 footer("Up/Down Player  Left/Right Term  Back");
}
static void table(const FCApp*a){
 const FCProfile*p=&a->profile;header("LEAGUE TABLE","Pos   Team                         Pts   GF-GA");char b[40],*s;
 for(unsigned rank=1;rank<=8;rank++)for(int i=0;i<8;i++)if(fc_rank(p,i)==rank){int y=62+(rank-1)*18;rect(18,y,284,17,i?PAPER:C(0x2d4131));s=number(b,rank);small(24,y+13,b,i?NOTE:RED);small(45,y+13,fc_teams[p->league[i]].name,TEXT);s=number(b,p->pts[i]);s=text(s,"   ");s=number(s,p->gf[i]);*s++='-';number(s,p->ga[i]);gfx_text_right(&np_font_small,292,y+13,b,TEXT,256);}
 footer("Back Career");
}
static void market(const FCApp*a){
 const FCProfile*p=&a->profile;header("TRANSFERS & COACH","Recruits replace your lowest-rated player.");char b[60],*s;
 small(20,73,"YOUR BUDGET",NOTE);s=number(b,p->money);text(s," CR");gfx_text_right(&np_font_body,300,73,b,TEXT,256);
 static const char*const label[]={"Prospect","First team starter","Star signing","Upgrade coach"};
 for(int i=0;i<4;i++){
  int y=82+i*31;bool active=a->selected==i;rect(18,y,284,27,active?C(0x2d4131):C(0x202c23));gfx_fill(18,y+4,3,19,active?RED:EDGE);small(28,y+12,label[i],active?RED:TEXT);
  if(i<3){s=number(b,80+i*5);text(s," OVR  /  7-game contract");}else{s=text(b,"LEVEL ");s=number(s,p->coach);text(s," / 4");}small(28,y+23,b,NOTE);
  s=number(b,i<3?1000+i*1000:800+p->coach*700);text(s," CR");gfx_text_right(&np_font_small,292,y+17,i==3&&p->coach==4?"MAX":b,TEXT,256);
 }
 footer("Up/Down Select    OK Buy    Back Career");
}
static void cup(const FCApp*a){
 const FCProfile*p=&a->profile;header("WORLD CUP",fc_teams[p->cup_team].name);static const char*const stage[]={"Group stage","Round of 16","Quarter-final","Semi-final","Final","CHAMPIONS"};
 center(160,80,stage[FC_MIN(p->cup_stage,5)],RED);char b[60],*s;
 if(p->cup_active){s=text(b,"Next: ");text(s,fc_teams[fc_cup_opponent(p)].name);center(160,103,b,TEXT);item(36,200,248,"Play next match",true);}
 else{center(160,108,p->cup_alive?"You lifted the trophy!":"Your World Cup run is over",TEXT);item(36,200,248,"New World Cup",true);}
 if(!p->cup_stage){unsigned g=p->cup_team%8;uint8_t pts[4],gf[4],ga[4];fc_group_table(p,g,pts,gf,ga);small(30,122,"GROUP",NOTE);small(226,122,"PTS  GF-GA",NOTE);
  for(unsigned i=0;i<4;i++){unsigned team=g+i*8;int y=128+i*16;rect(27,y,266,15,team==p->cup_team?C(0x2d4131):PAPER);small(34,y+12,fc_teams[team].name,team==p->cup_team?RED:TEXT);s=number(b,pts[i]);s=text(s,"    ");s=number(s,gf[i]);*s++='-';number(s,ga[i]);gfx_text_right(&np_font_small,285,y+12,b,TEXT,256);}
 }else if(p->cup_active){unsigned n=16u>>(p->cup_stage-1);for(unsigned i=0;i<n;i+=2){int col=(i/2)%2,row=i/4;int x=26+col*139,y=120+row*18;unsigned home=p->bracket[i],away=p->bracket[i+1];if(home<32&&away<32){s=text(b,fc_teams[home].code);s=text(s," - ");text(s,fc_teams[away].code);rect(x,y,129,16,PAPER);small(x+9,y+12,b,home==p->cup_team||away==p->cup_team?RED:TEXT);}}}
 footer("OK Play    EXE New cup    Back Home");
}
static void training(const FCApp*a){
 header("TRAINING GROUND","Choose a drill. Sessions last 60 seconds.");
 static const char*const info[]={"Score 6 goals against the keeper.","Complete 10 passes to the marked player.","Dribble through 6 gates in order."};
 for(int i=0;i<3;i++){int y=68+i*47;bool active=a->selected==i;rect(18,y,284,40,active?C(0x2d4131):C(0x202c23));gfx_fill(18,y+5,3,30,active?RED:EDGE);body(30,y+17,fc_drill_name(i),active?RED:TEXT);small(30,y+32,info[i],NOTE);}
 footer("Up/Down Select   OK Start   60 seconds");
}
static void training_result(const FCApp*a){
 const FCMatch*m=&a->match;header("SESSION COMPLETE",fc_drill_name(m->drill));unsigned goal=m->drill==1?10:6;char b[40],*s;
 rect(18,70,284,91,C(0x202c23));gfx_text_center(&np_font_small,160,87,m->drill_hits>=goal?"TARGET REACHED":"KEEP PRACTISING",RED,256);
 s=number(b,m->drill_hits);s=text(s," / ");number(s,goal);gfx_text_center(&np_font_title,160,117,b,TEXT,256);
 s=text(b,"Time: ");s=number(s,m->clock/40);text(s," seconds");gfx_text_center(&np_font_small,160,143,b,NOTE,256);
 item(18,174,284,"Try again",a->selected==0);item(18,198,284,"Home",a->selected==1);footer("OK Continue    Back Home");
}
static void training_markers(const FCApp*a){
 const FCMatch*m=fc_view(a);if(m->mode!=FC_TRAINING||m->drill!=2)return;
 int wx,wy;fc_training_target(m,&wx,&wy);
 for(int side=-1;side<=1;side+=2){int x,y,tx,ty;if(fc_project(a,wx+side*3*FC_Q,wy,0,&x,&y)&&fc_project(a,wx+side*3*FC_Q,wy,FC_Q,&tx,&ty)){
  line(x,y,tx,ty,RED);tri(tx-2,ty,tx+3,ty,tx,ty-4,RED);
 }}
 int x,y,tx,ty;if(fc_project(a,wx-3*FC_Q,wy,0,&x,&y)&&fc_project(a,wx+3*FC_Q,wy,0,&tx,&ty))line(x,y,tx,ty,GREEN);
}
static void result(const FCApp*a){
 const FCMatch*m=fc_view(a);header("FULL TIME",m->mode==FC_CAREER?"Career updated and saved":m->mode==FC_CUP?"World Cup updated and saved":"Match report");
 rect(18,65,284,68,C(0x202c23));flag(m->teams[0],38,76,35,22);flag(m->teams[1],247,76,35,22);center(55,117,fc_teams[m->teams[0]].code,TEXT);center(265,117,fc_teams[m->teams[1]].code,TEXT);
 char b[60],*s=number(b,m->score[0]);s=text(s," - ");number(s,m->score[1]);gfx_text_center(&np_font_title,160,96,b,RED,256);
 if(m->shoot_state){s=text(b,"PENS ");s=number(s,m->shoot_score[0]);s=text(s," - ");number(s,m->shoot_score[1]);gfx_text_center(&np_font_small,160,119,b,RED,256);}else{gfx_text_center(&np_font_small,160,119,m->score[0]>m->score[1]?"VICTORY":m->score[0]<m->score[1]?"DEFEAT":"DRAW",RED,256);}
 number(b,m->shots[0]);small(30,151,b,TEXT);gfx_text_center(&np_font_small,160,151,"SHOTS",NOTE,256);number(b,m->shots[1]);gfx_text_right(&np_font_small,291,151,b,TEXT,256);
 int total=m->shots[0]+m->shots[1],share=total?m->shots[0]*132/total:66;gfx_fill(28,158,132,3,EDGE);gfx_fill(160,158,132,3,EDGE);gfx_fill(160-share,158,share,3,RED);gfx_fill(160,158,132-share,3,GREEN);
 item(18,174,284,m->mode==FC_CAREER?"Back to career":m->mode==FC_CUP?"Continue World Cup":"Play again",a->selected==0);item(18,198,284,"Home",a->selected==1);footer("OK Continue    Back Home");
}
static void help(const FCApp*a){
 static const char*const names[]={"MOVE & CONTROL","PASSES","SHOOTING","DEFENDING","PENALTIES","MATCH RULES","FREE KICKS","FOULS & CARDS"};
 static const char*const keys[8][3]={{"ARROWS","XNT","ALPHA"},{"OK","8","9"},{"EXE","UP / DOWN","ALPHA"},{"OK","7 / 5","6"},{"ARROWS","EXE / OK","L / R"},{"ARROWS","OK / EXE","BACK"},{"ARROWS","8 / 9 / 5","EXE / OK"},{"OK","YELLOW","RED"}};
 static const char*const label[8][3]={{"Move your player","Sprint","Close control"},{"Ground pass","Through ball","Lob / cross"},{"Charge, then release","Aim for a corner","Placed shot"},{"Standing tackle","Switch / teammate press","Change your tactics"},{"Aim across and high / low","Red timing misses the goal","Move and dive as keeper"},{"Set-piece aim and power","Take the restart","Pause or view controls"},{"Aim and set power","Curl left / right / height","Time shot / short pass"},{"Tackle from the front","Reckless challenge","Player sent off"}};
 static const char*const detail[8][3]={{"Gold marker = you.","Faster, but uses stamina.","Slow down and protect the ball."},{"Hold, aim, then release.","Pass into a teammate's run.","Lift the ball over defenders."},{"EXE or Shift. Avoid full power.","In a match: Up / Down aims.","Hold ALPHA while shooting."},{"Close enough to win the ball.","7 switches. Hold 5 for support.","Also: Left/Right while paused."},{"Up/Down sets height in shootouts.","EXE: hold/release. OK: quick kick.","Up/Down sets glove height."},{"Up/Down aim. Left/Right power.","Free kick: EXE shoots, OK passes.","Home / Power saves and exits."},{"Left/Right aim. Up/Down power.","7 resets curl. Dots show flight.","Hold EXE, release on green."},{"Late or rear contact risks a foul.","Two yellows become a red card.","Team plays on with fewer players."}};
 unsigned page=(unsigned)a->page%FC_HELP_PAGES;char b[48],*q=number(b,page+1);q=text(q," / 8   Left / Right: page");header(names[page],b);
 for(int i=0;i<3;i++){int y=78+i*44;gfx_fill(18,y,78,29,C(0x29372c));gfx_text_center(&np_font_small,57,y+18,keys[page][i],TEXT,256);body(108,y+13,label[page][i],TEXT);small(108,y+28,detail[page][i],NOTE);gfx_fill(108,y+37,194,1,C(0x3e4b40));}
 footer("Left/Right Page    Back Return");
}
void fc_camera(FCApp*a,unsigned elapsed){
 if(a->match.shoot_state||fc_free_view(&a->match))return;/* Front-on penalties use a separate coordinate system. */
 const FCMatch*m=&a->match;int owner=m->ball.owner,lead=owner>=0?fc_attack(m,owner/11)*(owner<11?6:4)*FC_Q:0;
 int tx=FC_CLAMP(m->ball.x+m->ball.vx/8,-26*FC_Q,26*FC_Q),ty=FC_CLAMP(m->ball.y+lead+m->ball.vy/5,8*FC_Q,FC_H-8*FC_Q);
 /* Camera follows the ball, never the newly selected receiving player. */
 if(a->match.free_view==2){a->match.free_view=0;a->cam_x=tx;a->cam_y=ty;return;}
 if(m->phase!=FC_LIVE&&abs(ty-a->cam_y)>32*FC_Q){a->cam_x=tx;a->cam_y=ty;return;}
 unsigned n=FC_MIN(elapsed,80);a->cam_x+=(tx-a->cam_x)*(int)n/180;a->cam_y+=(ty-a->cam_y)*(int)n/180;
}
static void scene(void*ctx){
 const FCApp*a=ctx;
 if(a->screen==UI_HOME){home(a);return;}if(a->screen==UI_TRAINING){training(a);return;}if(a->screen==UI_SETUP){setup(a);return;}if(a->screen==UI_CAREER){career(a);return;}if(a->screen==UI_SQUAD){squad(a);return;}if(a->screen==UI_TABLE){table(a);return;}if(a->screen==UI_MARKET){market(a);return;}if(a->screen==UI_CUP){cup(a);return;}if(a->screen==UI_RESULT){if(a->match.mode==FC_TRAINING)training_result(a);else result(a);return;}if(a->screen==UI_HELP){help(a);return;}
 if(a->screen==UI_SETTINGS){header("SETTINGS","Gameplay settings apply to your next match.");static const char*const d[]={"Easy","Normal","Hard"},*const f[]={"4-3-3","4-4-2","3-4-3"},*const v[]={"Daylight","Sunset","Floodlights"};char b[30];char*s=number(b,2+a->profile.length*2);text(s," minutes");value_row(64,"Difficulty",d[a->profile.difficulty],a->selected==0);value_row(94,"Match length",b,a->selected==1);value_row(124,"Formation",f[a->profile.formation],a->selected==2);value_row(154,"Your kit",a->profile.kit?"Away":"Home",a->selected==3);value_row(184,"Stadium",v[a->profile.venue%3],a->selected==4);footer("Up/Down Select   Left/Right Change");return;}
 if(a->screen==UI_MATCH||a->screen==UI_PAUSE){if(a->match.shoot_state)shootout(a);else{pitch(a);training_markers(a);players(a);free_preview(a);
  if(!fc_free_view(fc_view(a))&&a->match.mode!=FC_TRAINING&&fc_view(a)->phase>=FC_THROW&&fc_view(a)->phase!=FC_FREE&&fc_view(a)->set_team==0){int x,y,tx,ty,wx,wy;const FCMatch*m=fc_view(a);fc_set_target(m,&wx,&wy);if(fc_project(a,m->ball.x,m->ball.y,0,&x,&y)&&fc_project(a,wx,wy,0,&tx,&ty)){line(x,y,tx,ty,GOLD);int d=FC_MAX(abs(tx-x),abs(ty-y));if(d){int hx=(tx-x)*5/d,hy=(ty-y)*5/d;line(tx-hx-hy/2,ty-hy+hx/2,tx,ty,GOLD);line(tx-hx+hy/2,ty-hy-hx/2,tx,ty,GOLD);}}}
  if(fc_free_view(fc_view(a)))free_hud(a);else hud(a);}if(a->screen==UI_MATCH)return;
  gfx_fill_alpha(0,0,320,240,INK,175);rect(48,43,224,158,PAPER);title(66,71,"PAUSED",TEXT);const char*opts[]={"Resume","How to play",a->match.mode==FC_FREEPLAY?"Leave practice":"Leave match"};for(int i=0;i<3;i++)item(63,88+i*31,194,opts[i],a->selected==i);if(a->match.mode!=FC_TRAINING&&a->match.mode!=FC_FREEPLAY){gfx_text_center(&np_font_small,160,215,fc_tactic_name(a->match.tactics[0]),PAPER,256);gfx_text_center(&np_font_small,160,230,"Left/Right Tactic   6 during match",MUTED,256);}return;}
 header(a->screen==UI_SAVE_ERROR?"SAVE FAILED":"PLEASE CONFIRM",a->screen==UI_SAVE_ERROR?"Your profile is still held in memory.":a->confirm==4?"Your profile will be saved.":a->confirm==3?"Leave the current session.":"This action replaces existing progress.");
 gfx_paragraph(&np_font_body,160,88,270,19,a->screen==UI_SAVE_ERROR?"Storage may be full. Free space on the calculator and retry, or return to the game.":a->confirm==4?"Leave Football FC and return to the calculator?":a->confirm==3?(a->match.mode==FC_FREEPLAY?"Leave free-kick practice and return to the menu?":"Leave this match? Career and cup progress from this unfinished match will not be recorded."):"Start a new competition? The current competition will be replaced.",TEXT,256);
 item(36,154,248,a->screen==UI_SAVE_ERROR?"Retry save":"Confirm",a->selected==0);item(36,180,248,a->screen==UI_SAVE_ERROR?"Return to game":"Cancel",a->selected==1);
 if(a->screen==UI_SAVE_ERROR)item(36,206,248,"Exit without saving",a->selected==2);
}
void fc_draw(void*ctx){
 const FCApp*a=ctx;scene(ctx);
 if(a->message[0]&&a->screen!=UI_MATCH){gfx_fill(8,217,312,23,PAPER);gfx_text_center(&np_font_small,160,232,a->message,RED,256);}
}
