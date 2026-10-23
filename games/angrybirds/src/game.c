#include "game.h"
#include "inflate.h"
#include <math.h>
#include <string.h>
static float clamp(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static const Shape *shape(const Body*b){return &shapes[object_defs[b->type].shape];}
static unsigned byte(const Game*g,unsigned at){const Level*l=&levels[g->level];return g->scratch[(at&3)*(l->unpacked/4)+(at>>2)];}
static unsigned u16(const Game*g,unsigned at){return byte(g,at)|(byte(g,at+1)<<8);}
static float f32(const Game*g,unsigned at){uint32_t v=byte(g,at)|byte(g,at+1)<<8|byte(g,at+2)<<16|byte(g,at+3)<<24;float x;memcpy(&x,&v,4);return x;}
static void body_new(Body*b,unsigned type,float x,float y,float angle,unsigned flags){
    memset(b,0,sizeof(*b));b->type=(uint16_t)type;b->a=angle;b->c=cosf(angle);b->s=sinf(angle);b->flags=ALIVE|flags;b->link=65535;
    const ObjectDef*d=&object_defs[type];const Shape*s=&shapes[d->shape];const Material*m=&materials[d->material];
    b->x=x+s->cx*b->c-s->cy*b->s;b->y=y+s->cx*b->s+s->cy*b->c;b->hp=d->health;
    if(flags&DYNAMIC){float mass=fmaxf(.01f,s->area*m->density);b->im=1/mass;b->ii=1/fmaxf(.01f,mass*s->inertia);}
    body_refresh(b);
}
void game_new(Game*g,uint32_t seed){memset(g,0,sizeof(*g));g->rng=seed?seed:1;for(unsigned i=0;i<GROUP_COUNT;i++)g->p.unlocked[i]=1;}
bool game_load(Game*g,unsigned level){
    if(level>=LEVEL_COUNT)return false;const Level*l=&levels[level];if(l->unpacked>LEVEL_BUFFER)return false;
    /* Progress survives retries. The remaining fixed-size game state is reset. */
    memset((uint8_t*)g+sizeof(Progress),0,sizeof(*g)-sizeof(Progress));g->level=(uint16_t)level;
    if(!inflate_raw(level_bytes+l->offset,l->packed,g->scratch,l->unpacked))return false;
    unsigned count=u16(g,0),joints=u16(g,2);if(count>MAX_ORIGINAL_OBJECTS||joints>JOINT_CAP||8+count*16+joints*44!=l->unpacked)return false;
    g->count=(uint16_t)(count+1);g->joint_count=(uint16_t)joints;g->birds=(uint16_t)byte(g,4);g->slingx=0;g->slingy=-8.8f;g->angle=PI*.25f;g->power=1;g->zoom=3.8f;g->camera_x=30;g->camera_y=-20;g->p.last=(uint16_t)level;g->dirty=1;
    for(unsigned i=0;i<GROUP_COUNT;i++)if(level>=groups[i].first&&level<groups[i].first+groups[i].count)g->group=(uint16_t)i;
    g->bodies[0].type=OBJ_SLINGSHOT;g->bodies[0].flags=ALIVE;g->bodies[0].link=65535;
    for(unsigned i=0;i<count;i++){
        unsigned at=8+i*16,type=u16(g,at);if(type>=TYPE_COUNT)return false;const ObjectDef*d=&object_defs[type];unsigned flags=d->is_static?0:DYNAMIC;
        if(i<g->birds)flags|=WAITING;
        body_new(&g->bodies[i+1],type,f32(g,at+4),f32(g,at+8),-f32(g,at+12)*PI/180,flags);g->sorted[i+1]=(uint16_t)(i+1);if(d->kind==PIG)g->pigs++;
    }
    /* The original wrb/Irb loader derives the sling from bird_1, not (0,0). */
    g->slingx=f32(g,12);g->slingy=f32(g,16)-8.5f+shape(&g->bodies[1])->radius;
    if(g->bodies[1].type==OBJ_BIRD_WHITE)g->slingy+=.2f;
    if(g->bodies[1].type==OBJ_BIRD_GREEN||g->bodies[1].type==OBJ_BIRD_SARDINE)g->slingy+=.45f;
    g->camera_x=g->slingx+30;g->camera_y=g->slingy-11.2f;
    for(unsigned i=0;i<joints;i++){
        unsigned at=8+count*16+i*44;Joint*j=&g->joints[i];unsigned a=u16(g,at),b=u16(g,at+2);j->a=a==65535?0:(uint16_t)(a+1);j->b=b==65535?0:(uint16_t)(b+1);if(j->a>=g->count||j->b>=g->count)return false;j->type=(uint8_t)byte(g,at+4);j->flags=(uint16_t)byte(g,at+5);j->timer=(uint16_t)u16(g,at+40);
        Body*A=&g->bodies[j->a],*B=&g->bodies[j->b];j->ax=f32(g,at+8)-shape(A)->cx;j->ay=f32(g,at+12)-shape(A)->cy;j->bx=f32(g,at+16)-shape(B)->cx;j->by=f32(g,at+20)-shape(B)->cy;j->speed=f32(g,at+24);j->torque=f32(g,at+28);j->low=f32(g,at+32);j->high=f32(g,at+36);j->angle=B->a-A->a;
        float ax=A->x+A->c*j->ax-A->s*j->ay,ay=A->y+A->s*j->ax+A->c*j->ay,bx=B->x+B->c*j->bx-B->s*j->by,by=B->y+B->s*j->bx+B->c*j->by;
        if(j->type==2){float dx=B->x-A->x,dy=B->y-A->y;j->ax=dx*A->c+dy*A->s;j->ay=-dx*A->s+dy*A->c;j->bx=j->by=0;j->length=0;}
        else if(j->type==3){float dx=ax-B->x,dy=ay-B->y;j->bx=dx*B->c+dy*B->s;j->by=-dx*B->s+dy*B->c;j->length=0;}
        else j->length=hypotf(bx-ax,by-ay);
        j->next_a=A->link;A->link=(uint16_t)i;j->next_b=B->link;B->link=(uint16_t)i;A->flags|=LINKED;B->flags|=LINKED;
    }
    g->preparing=12;return true;
}
bool game_prepare(Game*g){
    if(!g->preparing)return true;
    /* The app polls Home and Back between each bounded preparation step. */
    physics_step(g,1.0f/120);if(--g->preparing)return false;
    for(unsigned i=1;i<g->count;i++)if((g->bodies[i].flags&(DYNAMIC|WAITING))==DYNAMIC){Body*b=&g->bodies[i];b->flags|=SLEEPING;b->vx=b->vy=b->w=0;b->quiet=91;}
    for(unsigned i=0;i<g->joint_count;i++)if(g->joints[i].flags&2){g->bodies[g->joints[i].a].flags&=~SLEEPING;g->bodies[g->joints[i].b].flags&=~SLEEPING;}
    return true;
}
bool game_start(Game*g,unsigned level){if(!game_load(g,level))return false;while(!game_prepare(g)){}return true;}
static void remove_body(Game*g,unsigned i,bool score){
    Body*b=&g->bodies[i];if(!(b->flags&ALIVE))return;const ObjectDef*d=&object_defs[b->type];b->flags=0;if(d->kind==PIG&&g->pigs){g->pigs--;if(!g->pigs)g->settle=0;}if(score)g->score+=d->score;
    for(unsigned j=1;j<g->count;j++){Body*n=&g->bodies[j];if(n->minx<=b->maxx+.5f&&n->maxx>=b->minx-.5f&&n->miny<=b->maxy+.5f&&n->maxy>=b->miny-.5f){n->flags&=~SLEEPING;n->quiet=0;}}
}
void game_explode(Game*g,float x,float y,float radius,float strength){
    g->explosion_x=x;g->explosion_y=y;g->explosion_age=24;
    for(unsigned i=1;i<g->count;i++){
        Body*b=&g->bodies[i];if((b->flags&(ALIVE|DYNAMIC|WAITING))!=(ALIVE|DYNAMIC))continue;float dx=b->x-x,dy=b->y-y,d=hypotf(dx,dy);if(d>radius+shape(b)->radius)continue;float factor=fmaxf(.05f,1-d/(radius+shape(b)->radius));
        float lost=fminf(fmaxf(b->hp,0),strength*factor/fmaxf(.5f,materials[object_defs[b->type].material].defense));b->hp-=lost;
        if(object_defs[b->type].kind!=BIRD&&object_defs[b->type].score)g->score+=(uint32_t)lost*10;if(d<.1f)dx=0,dy=-1,d=1;
        b->vx+=dx/d*factor*strength*fminf(2,b->im);b->vy+=dy/d*factor*strength*fminf(2,b->im);b->flags&=~SLEEPING;b->quiet=0;
    }
}
static int extra(Game*g,unsigned type,float x,float y);
bool game_launch(Game*g){
    if(g->active||(g->launched>=g->birds&&!g->eagle_armed)||g->won||g->lost)return false;
    unsigned i;
    if(g->eagle_armed&&g->launched>=g->birds){int n=extra(g,OBJ_BIRD_SARDINE,g->slingx,g->slingy);if(n<0)return false;i=(unsigned)n;}
    else i=++g->launched;
    Body*b=&g->bodies[i];unsigned type=g->eagle_armed?OBJ_BIRD_SARDINE:b->type;if(g->eagle_armed)g->feather=1;g->eagle_armed=0;float speed=type==OBJ_BIRD_REDBIG?50:46.25f;
    float dx=cosf(g->angle)*5*g->power,dy=-sinf(g->angle)*5*g->power;
    body_new(b,type,g->slingx-dx,g->slingy-dy,0,DYNAMIC|FIRED);b->vx=speed*g->power*cosf(g->angle);b->vy=-speed*g->power*sinf(g->angle);g->active=(uint16_t)i;g->settle=g->wait=0;g->overview=0;return true;
}
static int extra(Game*g,unsigned type,float x,float y){
    for(unsigned i=g->birds+1;i<g->count;i++)if(!(g->bodies[i].flags&ALIVE)&&g->bodies[i].link==65535){body_new(&g->bodies[i],type,x,y,0,DYNAMIC|FIRED|USED);return(int)i;}
    if(g->count==BODY_CAP)return -1;unsigned i=g->count++;g->sorted[i]=(uint16_t)i;body_new(&g->bodies[i],type,x,y,0,DYNAMIC|FIRED|USED);return(int)i;
}
bool game_power(Game*g){
    if(!g->active)return false;Body*b=&g->bodies[g->active];if(!(b->flags&ALIVE)||b->flags&USED)return false;b->flags|=USED;
    if(b->type==OBJ_BIRD_YELLOW){b->vx*=2.1f;b->vy*=2.1f;}
    else if(b->type==OBJ_BIRD_BLUE){
        for(int sign=-1;sign<=1;sign+=2){int i=extra(g,OBJ_BIRD_BLUE,b->x,b->y+sign*.9f);if(i>=0){g->bodies[i].vx=b->vx*.98f-sign*b->vy*.16f;g->bodies[i].vy=b->vy*.98f+sign*b->vx*.16f;}}
    }else if(b->type==OBJ_BIRD_BLACK){game_explode(g,b->x,b->y,12,80);remove_body(g,g->active,false);g->active=0;g->wait=70;}
    else if(b->type==OBJ_BIRD_WHITE){int i=extra(g,OBJ_MISC_PROP_WHITE_EGG,b->x,b->y+1.5f);if(i>=0){g->bodies[i].flags|=EGG;g->bodies[i].vy=30;g->bodies[i].vx=b->vx*.2f;}b->vx+=10;b->vy=-35;}
    else if(b->type==OBJ_BIRD_GREEN){b->vx=-fabsf(b->vx)*1.7f;b->vy-=12;b->w=-4;}
    else return false;
    return true;
}
void game_eagle(Game*g){
    if(g->eagle_used||g->won||g->lost)return;g->eagle_used=1;g->eagle_armed=1;g->active=0;g->wait=0;g->overview=0;
}
void game_finish(Game*g){
    if(!g->won)return;Progress*p=&g->p;unsigned l=g->level;unsigned stars=g->score>=levels[l].gold?3:g->score>=levels[l].silver?2:1;
    uint32_t best=BEST_SCORE(p,l),eagle=BEST_EAGLE(p,l);
    if(!g->feather){if(g->score>best)best=g->score>SCORE_MASK?SCORE_MASK:g->score;if(stars>p->stars[l])p->stars[l]=(uint8_t)stars;}
    else {uint32_t target=levels[l].eagle;if(!target){for(unsigned i=1;i<g->count;i++){const ObjectDef*d=&object_defs[g->bodies[i].type];if(d->kind!=BIRD&&!d->is_static&&materials[d->material].defense>=0)target+=d->score+(uint32_t)d->health*10;}}
        unsigned percent=target?(unsigned)((uint64_t)g->score*100/target):100;g->eagle_percent=(uint8_t)(percent>100?100:percent);if(g->eagle_percent>eagle)eagle=g->eagle_percent;if(eagle==100)p->feathers[l>>3]|=1u<<(l&7);
    }
    p->best[l]=best|(eagle<<25);
    unsigned next=l-groups[g->group].first+2;if(next>groups[g->group].count)next=groups[g->group].count;if(next>p->unlocked[g->group])p->unlocked[g->group]=(uint16_t)next;g->dirty=1;
}
void game_step(Game*g){
    if(g->won||g->lost)return;g->tick++;if(g->explosion_age)g->explosion_age--;
    if(g->eagle_timer){g->eagle_timer--;float t=(180-g->eagle_timer)/180.0f;float x=g->eagle_x-90+t*180,y=g->eagle_y-30*sinf(t*PI);if(t>.25f&&t<.8f)game_explode(g,x,y,24,200);}
    if(g->active&&hypotf(g->bodies[g->active].vx,g->bodies[g->active].vy)>20){physics_step(g,1.0f/120);physics_step(g,1.0f/120);}else physics_step(g,1.0f/60);
    for(unsigned i=1;i<g->count;i++){
        Body*b=&g->bodies[i];if(!(b->flags&ALIVE)||b->flags&WAITING)continue;
        if(b->flags&FIRED){b->age++;if(b->type==OBJ_BIRD_BLACK&&!(b->flags&USED)&&(b->flags&HIT)&&b->age>120){game_explode(g,b->x,b->y,12,80);remove_body(g,i,false);}}
        if(b->type==OBJ_BIRD_SARDINE&&(b->flags&FIRED)&&!(b->flags&USED)&&(b->damage>0||b->maxy>=-.1f||b->age>180)){
            b->flags|=USED;g->eagle_x=b->x;g->eagle_y=b->y;g->eagle_timer=180;g->active=0;
        }
        if(b->flags&EGG&&((b->flags&HIT)||b->maxy>=-.1f||b->age>150)){game_explode(g,b->x,b->y,10,70);remove_body(g,i,false);}
        if(b->hp<=0&&object_defs[b->type].kind!=BIRD){if(object_defs[b->type].kind==TNT)game_explode(g,b->x,b->y,10,70);remove_body(g,i,true);}
        if(b->x<levels[g->level].left-80||b->x>levels[g->level].right+100||b->y>60)remove_body(g,i,true);
    }
    for(unsigned i=0;i<g->joint_count;i++){
        Joint*j=&g->joints[i];if(j->type!=5||j->b>=g->count||j->a>=g->count)continue;
        if(!(g->bodies[j->a].flags&ALIVE)&&g->bodies[j->b].flags&ALIVE){if(j->timer>16)j->timer-=16;else remove_body(g,j->b,true);}
    }
    if(!g->pigs){if(++g->settle>=100){if(!g->feather)g->score+=(g->birds-g->launched)*10000;g->won=1;game_finish(g);}return;}
    if(g->active){Body*b=&g->bodies[g->active];g->settle++;bool resting=hypotf(b->vx,b->vy)<1.6f&&fabsf(b->w)<1.2f;if(!(b->flags&ALIVE)||(resting&&g->settle>150)||g->settle>720){g->active=0;g->wait=75;g->settle=0;}}
    else if(g->wait)g->wait--;
    else if(g->launched==g->birds&&!g->eagle_armed&&!g->eagle_timer){if(++g->settle>180)g->lost=1;}
    float targetx=g->slingx+30,targety=g->slingy-11.2f,zoom=g->overview?fminf(300/(levels[g->level].right-levels[g->level].left),190/(4-levels[g->level].top)):3.8f;
    if(g->overview){targetx=(levels[g->level].left+levels[g->level].right)*.5f;targety=(levels[g->level].top+4)*.5f;}
    else if(g->active){Body*b=&g->bodies[g->active];targetx=fmaxf(g->slingx+30,b->x+18);targety=fminf(g->slingy-11.2f,b->y+6);}
    g->camera_x+=(targetx-g->camera_x)*.09f;g->camera_y+=(targety-g->camera_y)*.09f;g->zoom+=(clamp(zoom,.6f,5)-g->zoom)*.09f;
}
bool game_validate(const Game*g){
    if(g->count>BODY_CAP||g->joint_count>JOINT_CAP||g->contact_count>CONTACT_CAP||g->launched>g->birds||g->birds>10)return false;
    for(unsigned i=1;i<g->count;i++){const Body*b=&g->bodies[i];if(b->type>=TYPE_COUNT||!isfinite(b->x)||!isfinite(b->y)||!isfinite(b->vx)||!isfinite(b->vy)||!isfinite(b->w)||fabsf(b->x)>100000||fabsf(b->y)>100000)return false;}return true;
}
