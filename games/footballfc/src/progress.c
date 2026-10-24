#include "football.h"
#include <string.h>

void fc_defaults(FCProfile*p){
 memset(p,0,sizeof *p);p->money=1800;p->seed=0x4971a12b;p->opponent=1;p->difficulty=1;p->season=1;
 memset(p->group_goals,255,sizeof p->group_goals);memset(p->knock_goals,255,sizeof p->knock_goals);
 for(int i=0;i<11;i++){p->roster[i]=74+i%6;p->contracts[i]=7;}
}
void fc_career_new(FCProfile*p,unsigned team){
 p->career_active=1;p->career_team=team%32;p->career_round=0;p->season=1;p->money=1800;p->coach=0;p->training=0;
 memset(p->pts,0,8);memset(p->gf,0,8);memset(p->ga,0,8);p->league[0]=team%32;
 for(int i=1;i<8;i++)p->league[i]=(team+i*3)%32;
 for(int i=0;i<11;i++){p->roster[i]=72+fc_random(&p->seed)%9;p->contracts[i]=7;}
}
void fc_league_pair(unsigned round,unsigned pair,unsigned*a,unsigned*b){
 unsigned ring[8]={0,1,2,3,4,5,6,7};for(unsigned r=0;r<round%7;r++){unsigned last=ring[7];for(int i=7;i>1;i--)ring[i]=ring[i-1];ring[1]=last;}
 *a=ring[pair%4];*b=ring[7-pair%4];
}
unsigned fc_career_opponent(const FCProfile*p){
 for(unsigned i=0;i<4;i++){unsigned a,b;fc_league_pair(p->career_round,i,&a,&b);if(!a||!b)return p->league[a?a:b];}return p->league[1];
}
static void table_score(uint8_t*pts,uint8_t*gf,uint8_t*ga,unsigned a,unsigned b,unsigned x,unsigned y){
 gf[a]=(uint8_t)FC_MIN(250,gf[a]+x);ga[a]=(uint8_t)FC_MIN(250,ga[a]+y);gf[b]=(uint8_t)FC_MIN(250,gf[b]+y);ga[b]=(uint8_t)FC_MIN(250,ga[b]+x);
 pts[a]+=(uint8_t)(x>y?3:x==y?1:0);pts[b]+=(uint8_t)(y>x?3:x==y?1:0);
}
unsigned fc_rank(const FCProfile*p,unsigned slot){
 unsigned rank=1;int gd=p->gf[slot]-p->ga[slot];for(unsigned i=0;i<8;i++)if(i!=slot){int d=p->gf[i]-p->ga[i];
 if(p->pts[i]>p->pts[slot]||(p->pts[i]==p->pts[slot]&&(d>gd||(d==gd&&(p->gf[i]>p->gf[slot]||(p->gf[i]==p->gf[slot]&&i<slot))))))rank++;}return rank;
}
static unsigned sim_goal(FCProfile*p,unsigned team,unsigned other){
 int strength=fc_teams[team].attack-fc_teams[other].defense;
 unsigned r=fc_random(&p->seed)%1000;return r<(unsigned)(FC_CLAMP(270-strength*6,100,500))?0:r<650?1:r<880?2:r<975?3:4;
}
unsigned fc_train_price(const FCProfile*p,unsigned player){return 220+(p->roster[player%11]-60)*22;}
bool fc_train(FCProfile*p,unsigned i){if(i>=11||p->roster[i]>=96)return false;unsigned price=fc_train_price(p,i);if(p->money<price)return false;p->money-=price;p->roster[i]+=2;return true;}
bool fc_contract(FCProfile*p,unsigned i,unsigned games){if(i>=11||(games!=3&&games!=5&&games!=7)||p->contracts[i]>20-games)return false;unsigned cost=games*(25+p->roster[i]/2);if(p->money<cost)return false;p->money-=cost;p->contracts[i]+=games;return true;}
bool fc_coach(FCProfile*p){unsigned cost=800+p->coach*700;if(p->coach>=4||p->money<cost)return false;p->money-=cost;p->coach++;return true;}
bool fc_recruit(FCProfile*p,unsigned offer){
 if(offer>2)return false;unsigned skill=80+offer*5,cost=1000+offer*1000;if(p->money<cost)return false;
 int low=0;for(int i=1;i<11;i++)if(p->roster[i]<p->roster[low])low=i;if(p->roster[low]>=skill)return false;
 p->money-=cost;p->roster[low]=(uint8_t)skill;p->contracts[low]=7;return true;
}
void fc_career_next(FCProfile*p){if(p->career_round<7)return;p->season=FC_MIN(999,p->season+1);p->career_round=0;memset(p->pts,0,8);memset(p->gf,0,8);memset(p->ga,0,8);}
static const uint8_t group_pair[6][2]={{0,1},{2,3},{0,2},{1,3},{0,3},{1,2}};
/* Eight seeded groups, one top-seeded side in each group. */
static unsigned group_team(unsigned group,unsigned position){return group+8*position;}
void fc_group_table(const FCProfile*p,unsigned group,uint8_t pts[4],uint8_t gf[4],uint8_t ga[4]){
 memset(pts,0,4);memset(gf,0,4);memset(ga,0,4);for(unsigned j=0;j<6;j++)if(p->group_goals[group*6+j][0]!=255)
 table_score(pts,gf,ga,group_pair[j][0],group_pair[j][1],p->group_goals[group*6+j][0],p->group_goals[group*6+j][1]);
}
static void qualifiers(const FCProfile*p,unsigned group,uint8_t out[2]){
 uint8_t pts[4],gf[4],ga[4],order[4]={0,1,2,3};fc_group_table(p,group,pts,gf,ga);
 for(int i=0;i<4;i++)for(int j=i+1;j<4;j++){int a=order[i],b=order[j],d1=gf[a]-ga[a],d2=gf[b]-ga[b];
 if(pts[b]>pts[a]||(pts[a]==pts[b]&&(d2>d1||(d1==d2&&gf[b]>gf[a])))){order[i]=(uint8_t)b;order[j]=(uint8_t)a;}}
 out[0]=(uint8_t)group_team(group,order[0]);out[1]=(uint8_t)group_team(group,order[1]);
}
void fc_cup_new(FCProfile*p,unsigned team){
 p->cup_active=1;p->cup_team=team%32;p->cup_round=0;p->cup_stage=0;p->cup_alive=1;
 memset(p->group_goals,255,sizeof p->group_goals);memset(p->knock_goals,255,sizeof p->knock_goals);memset(p->bracket,255,16);
}
unsigned fc_cup_opponent(const FCProfile*p){
 if(!p->cup_alive)return p->cup_team;
 if(!p->cup_stage){unsigned g=p->cup_team%8,pos=p->cup_team/8;for(unsigned j=p->cup_round*2;j<p->cup_round*2u+2u;j++){
  unsigned a=group_pair[j][0],b=group_pair[j][1];if(a==pos||b==pos)return group_team(g,a==pos?b:a);}}
 else{unsigned n=16u>>(p->cup_stage-1);for(unsigned i=0;i<n;i+=2)if(p->bracket[i]==p->cup_team||p->bracket[i+1]==p->cup_team)return p->bracket[i]==p->cup_team?p->bracket[i+1]:p->bracket[i];}
 return p->cup_team;
}
static void cup_finish(FCProfile*p,const FCMatch*m){
 if(!p->cup_stage){
  for(unsigned g=0;g<8;g++)for(unsigned j=p->cup_round*2;j<p->cup_round*2u+2u;j++){
   unsigned a=group_team(g,group_pair[j][0]),b=group_team(g,group_pair[j][1]);uint8_t*x=p->group_goals[g*6+j];
   if(a==p->cup_team){x[0]=m->score[0];x[1]=m->score[1];}else if(b==p->cup_team){x[1]=m->score[0];x[0]=m->score[1];}
   else{x[0]=(uint8_t)sim_goal(p,a,b);x[1]=(uint8_t)sim_goal(p,b,a);}
  }
  if(++p->cup_round==3){uint8_t qual[8][2];for(unsigned g=0;g<8;g++)qualifiers(p,g,qual[g]);
   for(int g=0;g<8;g++){p->bracket[g*2]=qual[g][0];p->bracket[g*2+1]=qual[g^1][1];}
   p->cup_stage=1;p->cup_round=0;p->cup_alive=0;for(int i=0;i<16;i++)if(p->bracket[i]==p->cup_team)p->cup_alive=1;
  }
 }else{
  unsigned n=16u>>(p->cup_stage-1),offset=16-n;uint8_t winners[8];
  for(unsigned i=0;i<n;i+=2){unsigned a=p->bracket[i],b=p->bracket[i+1],x,y;bool win;
   if(a==p->cup_team||b==p->cup_team){x=a==p->cup_team?m->score[0]:m->score[1];y=a==p->cup_team?m->score[1]:m->score[0];win=x>y;
    if(x==y){bool human=m->shoot_score[0]>m->shoot_score[1];win=a==p->cup_team?human:!human;}}
   else{x=sim_goal(p,a,b);y=sim_goal(p,b,a);win=x==y?(fc_random(&p->seed)&1):x>y;}
   p->knock_goals[offset+i/2][0]=(uint8_t)x;p->knock_goals[offset+i/2][1]=(uint8_t)y;winners[i/2]=(uint8_t)(win?a:b);
  }
  memcpy(p->bracket,winners,n/2);memset(p->bracket+n/2,255,16-n/2);p->cup_stage++;p->cup_alive=0;
  for(unsigned i=0;i<n/2;i++)if(p->bracket[i]==p->cup_team)p->cup_alive=1;
  if(n==2){p->cup_active=0;if(p->cup_alive){p->trophies=FC_MIN(999,p->trophies+1);p->money=FC_MIN(999999,p->money+2500);}}
 }
 if(!p->cup_alive)p->cup_active=0;
}
void fc_finish(FCProfile*p,const FCMatch*m){
 if(m->mode==FC_TRAINING||m->mode==FC_FREEPLAY)return;
 p->played=FC_MIN(999999,p->played+1);bool win=m->score[0]>m->score[1]||(m->score[0]==m->score[1]&&m->shoot_state&&m->shoot_score[0]>m->shoot_score[1]);
 if(win)p->wins=FC_MIN(p->played,p->wins+1);
 if(m->mode==FC_CAREER){
  unsigned x=m->score[0],y=m->score[1];
  for(unsigned i=0;i<4;i++){unsigned a,b;fc_league_pair(p->career_round,i,&a,&b);
   if(a&&b)table_score(p->pts,p->gf,p->ga,a,b,sim_goal(p,p->league[a],p->league[b]),sim_goal(p,p->league[b],p->league[a]));
   else table_score(p->pts,p->gf,p->ga,a,b,a?y:x,a?x:y);}
  unsigned income=win?950:m->score[0]==m->score[1]?650:450;p->money=FC_MIN(999999,p->money+income);
  for(int i=0;i<11;i++){if(p->contracts[i])p->contracts[i]--;if(win&&p->roster[i]<90&&fc_random(&p->seed)%100<15u+p->coach*5u)p->roster[i]++;}
  p->career_round++;p->last_rank=(uint8_t)fc_rank(p,0);
  if(p->career_round==7){p->money=FC_MIN(999999,p->money+(9-p->last_rank)*250);if(p->last_rank==1)p->trophies=FC_MIN(999,p->trophies+1);}
 }else if(m->mode==FC_CUP){p->money=FC_MIN(999999,p->money+(win?450:200));cup_finish(p,m);}
}
static void put(uint8_t*p,uint32_t n){for(int i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t crc(const uint8_t*p,unsigned n){uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=c>>1^(0xedb88320u&-(c&1));}return ~c;}
void fc_encode(const FCProfile*p,uint8_t b[FC_SAVE_BYTES]){
 memset(b,0,FC_SAVE_BYTES);memcpy(b,"FFCP",4);b[4]=1;b[5]=p->venue;put(b+8,p->money);put(b+12,p->wins);put(b+16,p->played);put(b+20,p->seed);put(b+24,p->season);
 unsigned at=28;
#define OUT(v) b[at++]=(uint8_t)p->v
 OUT(team);OUT(opponent);OUT(difficulty);OUT(length);OUT(formation);OUT(kit);OUT(career_active);OUT(career_team);OUT(career_round);OUT(coach);OUT(training);
 memcpy(b+at,p->roster,11);at+=11;memcpy(b+at,p->contracts,11);at+=11;memcpy(b+at,p->league,8);at+=8;
 memcpy(b+at,p->pts,8);at+=8;memcpy(b+at,p->gf,8);at+=8;memcpy(b+at,p->ga,8);at+=8;
 OUT(cup_active);OUT(cup_team);OUT(cup_round);OUT(cup_stage);OUT(cup_alive);
 memcpy(b+at,p->group_goals,96);at+=96;memcpy(b+at,p->bracket,16);at+=16;memcpy(b+at,p->knock_goals,30);at+=30;
 put(b+at,p->trophies);at+=4;OUT(last_rank);put(b+FC_SAVE_BYTES-4,crc(b,FC_SAVE_BYTES-4));
#undef OUT
}
bool fc_decode(FCProfile*p,const uint8_t*b,unsigned size){
 if(!b||size!=FC_SAVE_BYTES||memcmp(b,"FFCP",4)||b[4]!=1||get(b+size-4)!=crc(b,size-4))return false;
 FCProfile n;fc_defaults(&n);n.venue=b[5];n.money=get(b+8);n.wins=get(b+12);n.played=get(b+16);n.seed=get(b+20);uint32_t season=get(b+24);n.season=(uint16_t)season;
 unsigned at=28;
#define IN(v) n.v=b[at++]
 IN(team);IN(opponent);IN(difficulty);IN(length);IN(formation);IN(kit);IN(career_active);IN(career_team);IN(career_round);IN(coach);IN(training);
 memcpy(n.roster,b+at,11);at+=11;memcpy(n.contracts,b+at,11);at+=11;memcpy(n.league,b+at,8);at+=8;
 memcpy(n.pts,b+at,8);at+=8;memcpy(n.gf,b+at,8);at+=8;memcpy(n.ga,b+at,8);at+=8;
 IN(cup_active);IN(cup_team);IN(cup_round);IN(cup_stage);IN(cup_alive);
 memcpy(n.group_goals,b+at,96);at+=96;memcpy(n.bracket,b+at,16);at+=16;memcpy(n.knock_goals,b+at,30);at+=30;
 uint32_t trophies=get(b+at);at+=4;n.trophies=(uint16_t)trophies;IN(last_rank);
#undef IN
 if(n.venue>2||n.money>999999||n.wins>n.played||n.played>999999||season<1||season>999||trophies>999||n.team>=32||n.opponent>=32||n.difficulty>2||n.length>2||n.formation>2||n.kit>1||n.career_active>1||n.career_team>=32||n.career_round>7||n.coach>4||n.training>4||n.cup_active>1||n.cup_team>=32||n.cup_round>3||n.cup_stage>5||n.cup_alive>1||n.last_rank>8)return false;
 for(int i=0;i<11;i++)if(n.roster[i]<60||n.roster[i]>99||n.contracts[i]>20)return false;
 for(int i=0;i<8;i++){if(n.league[i]>=32||n.pts[i]>21)return false;if(n.career_active)for(int j=0;j<i;j++)if(n.league[i]==n.league[j])return false;}
 for(int i=0;i<48;i++)for(int j=0;j<2;j++)if(n.group_goals[i][j]>99&&n.group_goals[i][j]!=255)return false;
 for(int i=0;i<16;i++)if(n.bracket[i]>=32&&n.bracket[i]!=255)return false;
 for(int i=0;i<15;i++)for(int j=0;j<2;j++)if(n.knock_goals[i][j]>99&&n.knock_goals[i][j]!=255)return false;
 if(n.cup_active){if(!n.cup_alive||(!n.cup_stage&&n.cup_round>2)||n.cup_stage>4)return false;if(n.cup_stage){unsigned count=16u>>(n.cup_stage-1);bool found=false;for(unsigned i=0;i<count;i++){if(n.bracket[i]>=32)return false;found|=n.bracket[i]==n.cup_team;for(unsigned j=0;j<i;j++)if(n.bracket[i]==n.bracket[j])return false;}if(!found)return false;}}
 *p=n;return true;
}
