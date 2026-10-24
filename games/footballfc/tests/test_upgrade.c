#include "../src/football.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define Q FC_Q
static void empty(FCMatch*m){FCProfile p;fc_defaults(&p);fc_new(m,&p,FC_FRIENDLY,1,123);m->phase=FC_LIVE;
 for(int i=0;i<22;i++){m->p[i].x=(i&1?30:-30)*Q;m->p[i].y=50*Q;m->p[i].vx=m->p[i].vy=0;m->p[i].cooldown=0;}
 m->selected=9;m->ball.owner=-1;m->ball.receiver=-1;m->ball.grace=0;m->ball_kind=FC_DRIVE;
}
int main(void){FCProfile p,q,before;FCMatch m;fc_defaults(&p);
 /* Both sides remain in their own half until either side takes the kickoff. */
 for(int half=0;half<2;half++)for(int team=0;team<2;team++){
  fc_new(&m,&p,FC_FRIENDLY,1,1);m.half=half;fc_restart(&m,FC_KICKOFF,team,0,FC_H/2);
  for(int i=0;i<22;i++)if(i!=m.taker){assert((m.p[i].y-FC_H/2)*fc_attack(&m,i/11)<=0);if(i/11!=team)assert(fc_distance(m.p[i].x-m.ball.x,m.p[i].y-m.ball.y)>=9*Q-2);}
 }
 /* The extra goal opening scores inside the enlarged post, not beyond it. */
 empty(&m);m.ball.x=960;m.ball.y=FC_H-40;m.ball.z=100;m.ball.vy=30*Q;m.ball.grace=10;
 fc_step(&m,0);assert(m.score[0]==1);
 /* A save on the swept path happens before the goal is awarded. */
 empty(&m);m.p[11].x=0;m.p[11].y=FC_H-100;m.ball.x=0;m.ball.y=FC_H-150;m.ball.z=100;m.ball.vy=32*Q;
 fc_step(&m,0);assert(!m.score[0]&&m.ball.last==11&&m.phase==FC_LIVE);
 /* An outfielder unable to reach a higher ball does not mask the keeper. */
 empty(&m);m.p[10].x=0;m.p[10].y=FC_H-220;m.p[11].x=0;m.p[11].y=FC_H-110;m.ball.x=0;m.ball.y=FC_H-300;m.ball.z=430;m.ball.vy=14*Q;
 fc_step(&m,0);assert(m.ball.owner==11&&!m.score[0]);
 /* Keepers cannot use their larger aerial catching reach outside their box. */
 empty(&m);m.p[11].x=0;m.p[11].y=50*Q;m.ball.x=0;m.ball.y=50*Q;m.ball.z=430;m.ball.vy=10*Q;
 fc_step(&m,0);assert(m.ball.owner<0&&m.ball.last!=11);
 /* A held tactic key changes once, then re-arms after release. */
 empty(&m);for(int i=0;i<8;i++)fc_step(&m,FC_TACTIC);assert(m.tactics[0]==1);fc_step(&m,0);fc_step(&m,FC_TACTIC);assert(m.tactics[0]==2);
 for(int drill=0;drill<3;drill++){
  fc_training_new(&m,&p,drill,123);assert(m.mode==FC_TRAINING&&m.selected==9);
  for(int tick=0;tick<2600&&m.phase!=FC_FULL;tick++)fc_step(&m,0);
  assert(m.phase==FC_FULL&&m.clock==m.duration&&!m.half);
  before=p;fc_finish(&p,&m);assert(!memcmp(&p,&before,sizeof p));
 }
 /* Pass drill: finish all ten receptions with the actual pass/trap physics. */
 fc_training_new(&m,&p,1,123);
 for(int n=0;n<10;n++){
  int owner=m.ball.owner,target=m.drill_target;assert(owner>=0&&owner!=target);int dx=m.p[target].x-m.p[owner].x,dy=m.p[target].y-m.p[owner].y;
  m.input_latch=(dx>Q?FC_RIGHT:dx<-Q?FC_LEFT:0)|(dy>Q?FC_UP:dy<-Q?FC_DOWN:0);
  if(fc_pass_target(&m,owner,FC_GROUND)!=target)fprintf(stderr,"Target n=%d owner=%d at %d,%d intended=%d at %d,%d chosen=%d input=%u dist=%d\n",n,owner,m.p[owner].x,m.p[owner].y,target,m.p[target].x,m.p[target].y,fc_pass_target(&m,owner,FC_GROUND),m.input_latch,fc_distance(dx,dy));assert(fc_pass_target(&m,owner,FC_GROUND)==target);fc_pass_type(&m,owner,FC_GROUND,500);
  for(int tick=0;tick<180&&m.ball.owner<0;tick++)fc_step(&m,0);
  if(m.ball.owner!=target||m.drill_hits!=n+1)fprintf(stderr,"Pass drill n=%d owner=%d target=%d hits=%d phase=%d ball=%d,%d receiver=%d last=%d player=%d,%d clock=%u\n",n,m.ball.owner,target,m.drill_hits,m.phase,m.ball.x,m.ball.y,m.ball.receiver,m.ball.last,m.p[target].x,m.p[target].y,m.clock);
  assert(m.ball.owner==target&&m.drill_hits==n+1);
 }assert(m.phase==FC_FULL);
 /* Slalom is completable by steering, without teleporting the player or ball. */
 fc_training_new(&m,&p,2,123);
 for(int tick=0;tick<2400&&m.phase!=FC_FULL;tick++){
  int x,y;fc_training_target(&m,&x,&y);int dx=x-m.p[9].x,dy=y-m.p[9].y;
  unsigned in=(dx>Q/3?FC_RIGHT:dx<-Q/3?FC_LEFT:0)|(dy>Q/3?FC_UP:dy<-Q/3?FC_DOWN:0);
  fc_step(&m,in);
 }assert(m.phase==FC_FULL&&m.drill_hits==6&&m.clock<m.duration);
 fc_training_new(&m,&p,0,123);
 for(int n=0;n<6;n++){
  fc_training_reset(&m);m.p[11].x=10*Q;m.ball.owner=-1;m.ball.grace=10;m.ball.x=0;m.ball.y=FC_H-40;m.ball.vy=30*Q;m.ball.z=100;
  fc_step(&m,0);assert(m.drill_hits==n+1);
 }assert(m.phase==FC_FULL);
 /* Finishing can be completed with movement and charged-shot inputs. */
 fc_training_new(&m,&p,0,123);
 for(int tick=0;tick<2400&&m.phase!=FC_FULL;tick++){
  unsigned in=0;if(m.phase==FC_LIVE&&m.ball.owner==9){
   if(m.p[9].y<90*Q){in=FC_UP|FC_SPRINT;if(m.p[9].x>3*Q)in|=FC_LEFT;else if(m.p[9].x<-3*Q)in|=FC_RIGHT;}
   else if(!m.p[9].cooldown){in=m.p[9].x>0?FC_LEFT:FC_RIGHT;if(m.charge<600)in|=FC_SHOOT;}
  }fc_step(&m,in);
 }
 fprintf(stderr,"Training finishing controller: %u goals, %u shots, %u ticks\n",m.drill_hits,m.shots[0],m.clock);
 assert(m.phase==FC_FULL&&m.drill_hits==6&&m.clock<m.duration);
 /* Save compatibility: the v4 profile has no stadium field, and defaults to day. */
 unsigned char data[FC_SAVE_BYTES];FILE*f=fopen("../tests/legacy-v4-save.bin","rb");assert(f&&fread(data,1,sizeof data,f)==sizeof data);fclose(f);assert(fc_decode(&q,data,sizeof data)&&q.venue==0);
 for(int venue=0;venue<3;venue++){p.venue=venue;fc_encode(&p,data);assert(fc_decode(&q,data,sizeof data)&&q.venue==venue);}
 p.venue=3;fc_encode(&p,data);assert(!fc_decode(&q,data,sizeof data));
 /* Each approach completes a match with all three formations and both attack directions. */
 for(int tactic=0;tactic<4;tactic++)for(int form=0;form<3;form++){
  fc_defaults(&p);p.formation=form;fc_new(&m,&p,FC_FRIENDLY,1,100+tactic*3+form);m.tactics[0]=tactic;
  for(int tick=0;tick<9000&&m.phase!=FC_FULL;tick++)fc_step(&m,0);
  assert(m.phase==FC_FULL&&m.half==1);for(int i=0;i<22;i++)assert(abs(m.p[i].x)<=FC_W/2&&m.p[i].stamina<=1000);
 }
 puts("Upgrade: kickoff halves, swept saves, reachable catches, tactics, three completable drills, no training rewards, legacy saves passed");return 0;
}
