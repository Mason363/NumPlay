/* Compact convex rigid-body solver. Immutable geometry stays in flash.
 * Broad phase uses a persistent sweep order; impulses include rotation/friction.
 * The original game's Java/Box2D runtime is not embedded in this native port. */
#include "game.h"
#include <math.h>
#include <string.h>
static float clamp(float x,float a,float b){return x<a?a:x>b?b:x;}
static const Shape *shape(const Body*b){return &shapes[object_defs[b->type].shape];}
void body_refresh(Body*b){
    b->c=cosf(b->a);b->s=sinf(b->a);const Shape*q=shape(b);
    if(q->kind==0){b->minx=b->x-q->radius;b->maxx=b->x+q->radius;b->miny=b->y-q->radius;b->maxy=b->y+q->radius;}
    else{b->minx=b->miny=1e8f;b->maxx=b->maxy=-1e8f;for(unsigned i=0;i<q->count;i++){float x=q->vertices[i*2],y=q->vertices[i*2+1],u=b->x+x*b->c-y*b->s,v=b->y+x*b->s+y*b->c;if(u<b->minx)b->minx=u;if(u>b->maxx)b->maxx=u;if(v<b->miny)b->miny=v;if(v>b->maxy)b->maxy=v;}}
}
typedef struct {float x,y;} Vec;
static Vec add(Vec a,Vec b){return(Vec){a.x+b.x,a.y+b.y};}
static Vec sub(Vec a,Vec b){return(Vec){a.x-b.x,a.y-b.y};}
static Vec mul(Vec a,float s){return(Vec){a.x*s,a.y*s};}
static float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y;}
static float cross(Vec a,Vec b){return a.x*b.y-a.y*b.x;}
static Vec rotate(const Body*b,Vec p){return(Vec){p.x*b->c-p.y*b->s,p.x*b->s+p.y*b->c};}
static Vec world(const Body*b,Vec p){return add((Vec){b->x,b->y},rotate(b,p));}
static Vec velocity(const Body*b,Vec r){return(Vec){b->vx-b->w*r.y,b->vy+b->w*r.x};}
static void impulse(Body*b,Vec r,Vec p){if(!b->im)return;b->vx+=p.x*b->im;b->vy+=p.y*b->im;b->w+=cross(r,p)*b->ii;}
static void contact(Game*g,unsigned a,unsigned b,Vec n,Vec p,float depth){
    if(g->contact_count==CONTACT_CAP){g->overflows++;return;}
    Contact*c=&g->contacts[g->contact_count++];*c=(Contact){n.x,n.y,p.x,p.y,depth,0,0,0,0,(uint16_t)a,(uint16_t)b};
    Body*A=&g->bodies[a],*B=&g->bodies[b];Vec va=velocity(A,sub(p,(Vec){A->x,A->y})),vb=b?velocity(B,sub(p,(Vec){B->x,B->y})):(Vec){0,0};c->closing=fmaxf(0,-dot(sub(vb,va),n));
    float ma=A->im>0?1/A->im:0,mb=B->im>0?1/B->im:0;c->momentum=hypotf(ma*A->vx-mb*B->vx,ma*A->vy-mb*B->vy)/10;
    if(c->closing>1){if(A->type==OBJ_BIRD_BLACK&&!(A->flags&HIT))A->age=0;if(b&&B->type==OBJ_BIRD_BLACK&&!(B->flags&HIT))B->age=0;A->flags|=HIT;if(b)B->flags|=HIT;}
    if(c->closing>.1f&&b&&((A->im&&!(A->flags&SLEEPING))||(B->im&&!(B->flags&SLEEPING)))){A->flags&=~SLEEPING;B->flags&=~SLEEPING;A->quiet=B->quiet=0;}
}
static unsigned vertices(const Body*b,Vec *v){const Shape*s=shape(b);for(unsigned i=0;i<s->count;i++)v[i]=world(b,(Vec){s->vertices[i*2],s->vertices[i*2+1]});return s->count;}
static Vec normal(Vec a,Vec b){Vec d=sub(b,a);float len=sqrtf(dot(d,d));return len>.00001f?(Vec){d.y/len,-d.x/len}:(Vec){1,0};}
static void circles(Game*g,unsigned ia,unsigned ib){Body*a=&g->bodies[ia],*b=&g->bodies[ib];float r=shape(a)->radius+shape(b)->radius;Vec d={b->x-a->x,b->y-a->y};float length=dot(d,d);if(length>=r*r)return;length=sqrtf(length);Vec n=length>.00001f?mul(d,1/length):(Vec){1,0};contact(g,ia,ib,n,add((Vec){a->x,a->y},mul(n,shape(a)->radius-(r-length)*.5f)),r-length);}
static void circle_poly(Game*g,unsigned ic,unsigned ip,bool reverse){
    Body*c=&g->bodies[ic],*p=&g->bodies[ip];Vec v[8],center={c->x,c->y};unsigned n=vertices(p,v);float separation=-1e9f,r=shape(c)->radius;unsigned face=0;
    for(unsigned i=0;i<n;i++){Vec axis=normal(v[i],v[(i+1)%n]);float d=dot(axis,sub(center,v[i]));if(d>r)return;if(d>separation){separation=d;face=i;}}
    Vec a=v[face],b=v[(face+1)%n],edge=sub(b,a);float t=clamp(dot(sub(center,a),edge)/fmaxf(.000001f,dot(edge,edge)),0,1);Vec closest=add(a,mul(edge,t)),d=sub(center,closest);float length=sqrtf(dot(d,d));Vec out=normal(a,b);float depth=r-separation;
    if(separation>0){if(length>=r)return;if(length>.00001f)out=mul(d,1/length);depth=r-length;}
    Vec point=add(center,mul(out,-r+depth*.5f));if(reverse)contact(g,ip,ic,out,point,depth);else contact(g,ic,ip,mul(out,-1),point,depth);
}
static int clip(Vec *points,Vec n,float offset){
    Vec a=points[0],b=points[1];float da=dot(n,a)-offset,db=dot(n,b)-offset;int count=0;if(da<=0)points[count++]=a;if(db<=0)points[count++]=b;if(da*db<0)points[count++]=add(a,mul(sub(b,a),da/(da-db)));return count;
}
static bool separation(const Vec*a,unsigned na,const Vec*b,unsigned nb,float *best,unsigned *edge){
    *best=-1e8f;
    for(unsigned i=0;i<na;i++){Vec n=normal(a[i],a[(i+1)%na]);float smallest=1e8f;for(unsigned j=0;j<nb;j++)smallest=fminf(smallest,dot(n,sub(b[j],a[i])));if(smallest>0)return false;if(smallest>*best){*best=smallest;*edge=i;}}
    return true;
}
static void polygons(Game*g,unsigned ia,unsigned ib){
    Vec a[8],b[8];unsigned na=vertices(&g->bodies[ia],a),nb=vertices(&g->bodies[ib],b),ea=0,eb=0;float da,db;if(!separation(a,na,b,nb,&da,&ea)||!separation(b,nb,a,na,&db,&eb))return;
    bool swap=db>da+.01f;Vec *ref=swap?b:a,*inc=swap?a:b;unsigned nr=swap?nb:na,ni=swap?na:nb,face=swap?eb:ea;Vec v1=ref[face],v2=ref[(face+1)%nr],n=normal(v1,v2),t={-n.y,n.x};unsigned edge=0;float worst=1e8f;
    for(unsigned i=0;i<ni;i++){float d=dot(n,normal(inc[i],inc[(i+1)%ni]));if(d<worst){worst=d;edge=i;}}
    Vec points[3]={inc[edge],inc[(edge+1)%ni]};if(clip(points,mul(t,-1),-dot(t,v1))<2)return;if(clip(points,t,dot(t,v2))<2)return;
    for(unsigned i=0;i<2;i++){float d=dot(n,sub(points[i],v1));if(d<=.005f)contact(g,ia,ib,swap?mul(n,-1):n,sub(points[i],mul(n,d*.5f)),-d);}
}
static bool connected(const Game*g,unsigned a,unsigned b){
    unsigned at=g->bodies[a].link,guard=0;while(at!=65535&&guard++<JOINT_CAP){const Joint*j=&g->joints[at];if((j->a==b||j->b==b)&&!(j->flags&1))return true;at=j->a==a?j->next_a:j->next_b;}return false;
}
static void ground(Game*g,unsigned a){Body*b=&g->bodies[a];const Shape*s=shape(b);if(b->maxy<=0)return;Vec n={0,1};if(s->kind==0){contact(g,a,0,n,(Vec){b->x,b->y+s->radius},b->maxy);return;}Vec v[8];unsigned count=vertices(b,v),best=0,second=0;for(unsigned i=1;i<count;i++)if(v[i].y>v[best].y)best=i;second=(best+1)%count;unsigned prev=(best+count-1)%count;if(v[prev].y>v[second].y)second=prev;if(v[best].y>0)contact(g,a,0,n,v[best],v[best].y);if(v[second].y>0&&fabsf(v[second].y-v[best].y)<.08f)contact(g,a,0,n,v[second],v[second].y);}
static void collide(Game*g){
    g->contact_count=0;
    for(unsigned i=1;i<g->count;i++){Body*b=&g->bodies[i];if((b->flags&(ALIVE|DYNAMIC|WAITING|SLEEPING))==(ALIVE|DYNAMIC))ground(g,i);}
    for(unsigned i=1;i<g->count;i++){unsigned k=g->sorted[i];float x=g->bodies[k].minx;unsigned j=i;while(j>1&&g->bodies[g->sorted[j-1]].minx>x){g->sorted[j]=g->sorted[j-1];j--;}g->sorted[j]=k;}
    for(unsigned ii=1;ii<g->count;ii++){
        unsigned a=g->sorted[ii];Body*A=&g->bodies[a];if(!(A->flags&ALIVE)||A->flags&WAITING)continue;
        for(unsigned jj=ii+1;jj<g->count;jj++){
            unsigned b=g->sorted[jj];Body*B=&g->bodies[b];if(B->minx>A->maxx)break;
            if(!(B->flags&ALIVE)||B->flags&WAITING||(!A->im&&!B->im)||B->miny>A->maxy||B->maxy<A->miny)continue;
            if((!A->im||(A->flags&SLEEPING))&&(!B->im||(B->flags&SLEEPING)))continue;
            if(connected(g,a,b))continue;
            const Shape*sa=shape(A),*sb=shape(B);
            if(!sa->kind&&!sb->kind)circles(g,a,b);else if(!sa->kind)circle_poly(g,a,b,false);else if(!sb->kind)circle_poly(g,b,a,true);else polygons(g,a,b);
        }
    }
    if(g->contact_count>g->max_contacts)g->max_contacts=g->contact_count;
}
static void solve_contact(Game*g,Contact*c){
    Body*a=&g->bodies[c->a],*b=&g->bodies[c->b];Vec n={c->nx,c->ny},t={-n.y,n.x},ra={c->x-a->x,c->y-a->y},rb=c->b?(Vec){c->x-b->x,c->y-b->y}:(Vec){0,0};
    Vec relative=sub(velocity(b,rb),velocity(a,ra));float rnA=cross(ra,n),rnB=cross(rb,n),mass=a->im+b->im+a->ii*rnA*rnA+b->ii*rnB*rnB;if(mass<.000001f)return;
    float restitution=materials[object_defs[a->type].material].restitution;if(c->b)restitution=fmaxf(restitution,materials[object_defs[b->type].material].restitution);
    float bias=fmaxf(c->depth-.007f,0)*9;if(c->closing>2)bias+=restitution*c->closing;
    float amount=(bias-dot(relative,n))/mass;float before=c->normal;c->normal=fmaxf(before+amount,0);amount=c->normal-before;impulse(a,ra,mul(n,-amount));impulse(b,rb,mul(n,amount));
    relative=sub(velocity(b,rb),velocity(a,ra));float rtA=cross(ra,t),rtB=cross(rb,t),tm=a->im+b->im+a->ii*rtA*rtA+b->ii*rtB*rtB;
    float friction=materials[object_defs[a->type].material].friction;if(c->b)friction=sqrtf(friction*materials[object_defs[b->type].material].friction);else friction=sqrtf(friction*.6f);
    before=c->tangent;if(tm>.000001f)c->tangent=clamp(before-dot(relative,t)/tm,-friction*c->normal,friction*c->normal);amount=c->tangent-before;impulse(a,ra,mul(t,-amount));impulse(b,rb,mul(t,amount));
    if(c->closing>2){a->flags&=~SLEEPING;b->flags&=~SLEEPING;a->quiet=b->quiet=0;}
}
static void solve_joint(Game*g,Joint*j,float dt){
    if(j->a>=g->count||j->b>=g->count)return;Body*a=&g->bodies[j->a],*b=&g->bodies[j->b];if(!(a->flags&ALIVE)||!(b->flags&ALIVE)||j->type==5)return;
    if((!a->im||(a->flags&SLEEPING))&&(!b->im||(b->flags&SLEEPING))&&!(j->flags&2))return;
    if(a->im)a->flags&=~SLEEPING;if(b->im)b->flags&=~SLEEPING;
    Vec ra=rotate(a,(Vec){j->ax,j->ay}),rb=rotate(b,(Vec){j->bx,j->by}),delta=sub(add((Vec){b->x,b->y},rb),add((Vec){a->x,a->y},ra));
    if(j->type==1){float length=sqrtf(dot(delta,delta));if(length<.00001f)return;Vec n=mul(delta,1/length);float ca=cross(ra,n),cb=cross(rb,n),mass=a->im+b->im+a->ii*ca*ca+b->ii*cb*cb;if(mass<=0)return;float speed=dot(sub(velocity(b,rb),velocity(a,ra)),n);float f=-(speed+(length-j->length)*10)/mass;f=clamp(f,-200,200);impulse(a,ra,mul(n,-f));impulse(b,rb,mul(n,f));}
    else{
        Vec relative=sub(velocity(b,rb),velocity(a,ra));float kx=a->im+b->im+a->ii*ra.y*ra.y+b->ii*rb.y*rb.y,ky=a->im+b->im+a->ii*ra.x*ra.x+b->ii*rb.x*rb.x,kxy=-a->ii*ra.x*ra.y-b->ii*rb.x*rb.y,det=kx*ky-kxy*kxy;
        if(det>.000001f){Vec v=add(relative,mul(delta,12)),p={(-ky*v.x+kxy*v.y)/det,(kxy*v.x-kx*v.y)/det};p.x=clamp(p.x,-200,200);p.y=clamp(p.y,-200,200);impulse(a,ra,mul(p,-1));impulse(b,rb,p);}
        float inv=a->ii+b->ii;if(inv>.000001f){float error=b->a-a->a-j->angle,desired=0;
            if(j->type==2)desired=-clamp(error*12,-10,10);
            else if(j->flags&2){desired=j->speed;if((j->flags&8)&&((error<j->low&&desired<0)||(error>j->high&&desired>0))){j->speed=-j->speed;desired=j->speed;}}
            else if(j->flags&4){if(error<j->low)desired=(j->low-error)*12;else if(error>j->high)desired=(j->high-error)*12;else return;}else return;
            float f=(desired-(b->w-a->w))/inv;if(j->type!=2&&j->flags&2)f=clamp(f,-j->torque*dt,j->torque*dt);a->w-=a->ii*f;b->w+=b->ii*f;
        }
    }
}
static float damage_value(const Body*a,const Body*b,float momentum){
    const ObjectDef*oa=&object_defs[a->type],*ob=&object_defs[b->type];const Material*m=&materials[oa->material];if(m->defense<0||!a->im)return 0;
    float mult=damage_matrix[ob->material*MAT_COUNT+oa->material]/16.0f;float raw=momentum*mult,rounded=floorf(raw+.5f);
    if(raw>=m->defense)rounded=fmaxf(1,rounded);if(rounded<m->defense)return 0;
    return floorf(fmaxf(0,rounded-m->defense));
}
void physics_step(Game*g,float dt){
    unsigned awake=0;
    for(unsigned i=1;i<g->count;i++){
        Body*b=&g->bodies[i];b->damage=0;
        if((b->flags&(ALIVE|DYNAMIC|WAITING|SLEEPING))!=(ALIVE|DYNAMIC))continue;
        awake++;
        b->vy+=20*dt;b->vx=clamp(b->vx,-100,100);b->vy=clamp(b->vy,-100,100);b->w=clamp(b->w,-15,15);b->x+=b->vx*dt;b->y+=b->vy*dt;b->a+=b->w*dt;if(fabsf(b->a)>PI*2)b->a=fmodf(b->a,PI*2);body_refresh(b);
    }
    collide(g);
    unsigned iterations=awake>80?6:10;
    for(unsigned it=0;it<iterations;it++){for(unsigned j=0;j<g->joint_count;j++)solve_joint(g,&g->joints[j],dt);for(unsigned i=0;i<g->contact_count;i++)solve_contact(g,&g->contacts[i]);}
    for(unsigned i=0;i<g->contact_count;i++){
        Contact*c=&g->contacts[i];Body*a=&g->bodies[c->a],*b=&g->bodies[c->b];float im=a->im+b->im;
        if(im>0){float correct=fminf(.08f,fmaxf(c->depth-.009f,0)*.22f)/im;a->x-=c->nx*correct*a->im;a->y-=c->ny*correct*a->im;if(c->b){b->x+=c->nx*correct*b->im;b->y+=c->ny*correct*b->im;}}
        if(g->launched&&c->closing>1){a->damage=fmaxf(a->damage,damage_value(a,b,c->momentum));if(c->b)b->damage=fmaxf(b->damage,damage_value(b,a,c->momentum));}
    }
    for(unsigned i=1;i<g->count;i++){
        Body*b=&g->bodies[i];if(!(b->flags&ALIVE)||b->flags&WAITING)continue;bool refresh=b->im&&!(b->flags&SLEEPING);
        if(b->im&&b->vx*b->vx+b->vy*b->vy<.012f&&fabsf(b->w)<.03f){if(b->quiet<65535)b->quiet++;if(b->quiet>90){b->flags|=SLEEPING;b->vx=b->vy=b->w=0;}}else b->quiet=0;
        if(object_defs[b->type].kind!=BIRD){float lost=fminf(fmaxf(b->hp,0),b->damage);if(object_defs[b->type].score)g->score+=(uint32_t)lost*10;b->hp-=lost;}
        if(refresh)body_refresh(b);
    }
}
