#include <gccore.h>
#include <wiiuse/wpad.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIFO_SIZE (256*1024)
#define PI 3.14159265358979323846f
#define DEG2RAD(x) ((x)*PI/180.0f)
#define RAD2DEG(x) ((x)*180.0f/PI)
#define DT (1.0f/60.0f)
#define G_YD 10.72f
#define RANGE_MAX 365.0f

typedef struct { float x,y,z; } V3;
typedef struct { float x,y,z; float vx,vy,vz; float spin_side,spin_back; int moving,flying; float rest_time; float distance; } Ball;
typedef struct { const char *name; float carry; float launch; float loft_spin; } Club;
static const Club CLUBS[]={
    {"DR",275.0f,12.0f,1.00f},{"3W",235.0f,14.5f,1.08f},{"5I",185.0f,19.0f,1.22f},
    {"7I",155.0f,24.0f,1.35f},{"PW",115.0f,34.0f,1.55f}
};
#define CLUB_COUNT ((int)(sizeof(CLUBS)/sizeof(CLUBS[0])))

typedef struct {
    float base_pitch,base_roll;
    float live_pitch,live_roll;
    float club_angle,face_angle;
    float backswing,max_backswing;
    float last_club_angle,angular_speed;
    float power;
    float meter_face;
    int addressed;
    int had_backswing;
    int impact_lock;
    int motionplus;
    int mp_cal_frames;
    float mp_zero_rx,mp_zero_ry,mp_zero_rz;
    float mp_sum_rx,mp_sum_ry,mp_sum_rz;
    float gyro_yaw,gyro_roll,gyro_pitch;
} Swing;

typedef enum { CAM_TEE=0,CAM_HOLD,CAM_BALL,CAM_RESULT } CamMode;
typedef struct { int club; float aim; int shots; CamMode cam; int cam_frames; float flash; float wind_x,wind_z; } Game;

static GXRModeObj *rmode; static void *xfb[2]; static void *fifo; static u32 fb=0;
static Mtx44 perspective,ortho; static Mtx view;

static float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);} static float lerpf(float a,float b,float t){return a+(b-a)*t;}
static V3 v3(float x,float y,float z){V3 v={x,y,z};return v;} static V3 vadd(V3 a,V3 b){return v3(a.x+b.x,a.y+b.y,a.z+b.z);} static V3 vsub(V3 a,V3 b){return v3(a.x-b.x,a.y-b.y,a.z-b.z);} static V3 vmul(V3 a,float s){return v3(a.x*s,a.y*s,a.z*s);} static float vlen(V3 a){return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z);} static V3 vnorm(V3 a){float l=vlen(a);return l>.0001f?vmul(a,1.0f/l):v3(0,0,0);}
static GXColor mixc(GXColor a,GXColor b,float t){t=clampf(t,0,1);GXColor c={(u8)lerpf(a.r,b.r,t),(u8)lerpf(a.g,b.g,t),(u8)lerpf(a.b,b.b,t),255};return c;}
static void col(GXColor c){GX_Color4u8(c.r,c.g,c.b,c.a);} static void vtx(float x,float y,float z,GXColor c){GX_Position3f32(x,y,z);col(c);}

/* Soft, rolling Wuhu-like range terrain. Original geometry/assets; no Nintendo course data. */
static float terrain_h(float z,float x){
    float broad=1.35f*sinf(z*.018f)+.55f*sinf(z*.043f+.5f);
    float side=.012f*x+.25f*sinf(x*.048f+z*.009f);
    float tee=1.0f*expf(-((z-8)*(z-8))/360.0f);
    float bowl=-.65f*expf(-((z-205)*(z-205))/3600.0f);
    return broad+side+tee+bowl;
}
static float fair_center(float z){return -2.0f*sinf(z*.012f)-1.2f*sinf(z*.028f+.4f);} static float fair_half(float z){return 28.0f+4.2f*sinf(z*.015f+.8f);}
static float soft_edge(float d,float half){return clampf((half+4.0f-d)/8.0f,0,1);} static int in_bunker(float z,float x){float dx=x-31,dz=z-135;if(dx*dx/180.0f+dz*dz/430.0f<1)return 1;dx=x+30;dz=z-224;return dx*dx/155.0f+dz*dz/340.0f<1;}
static GXColor grass_color(float z,float x){
    GXColor rough={48,117,54,255},fair={83,158,72,255},light={122,187,94,255},sand={218,201,153,255};
    if(in_bunker(z,x)) return sand;
    float d=fabsf(x-fair_center(z)),f=soft_edge(d,fair_half(z));
    GXColor g=mixc(rough,fair,f);
    float stripe=.5f+.5f*sinf(z*.26f);g=mixc(g,light,(f*.10f)+(stripe*.08f*f));
    float hx=(terrain_h(z,x+1)-terrain_h(z,x-1))*.5f,hz=(terrain_h(z+1,x)-terrain_h(z-1,x))*.5f;
    float shade=clampf(.60f-.30f*hx-.18f*hz,.2f,.88f);
    return mixc((GXColor){28,74,35,255},g,shade);
}

static void draw_sphere(V3 c,float r,GXColor base,int stacks,int slices){
    for(int j=0;j<stacks;j++){
        float p0=-PI*.5f+PI*(float)j/stacks,p1=-PI*.5f+PI*(float)(j+1)/stacks;
        GX_Begin(GX_TRIANGLESTRIP,GX_VTXFMT0,(u16)((slices+1)*2));
        for(int i=0;i<=slices;i++){
            float a=2*PI*(float)i/slices;
            float x0=cosf(p0)*cosf(a),y0=sinf(p0),z0=cosf(p0)*sinf(a);float l0=clampf(.62f+.25f*y0-.13f*x0,.35f,1);
            float x1=cosf(p1)*cosf(a),y1=sinf(p1),z1=cosf(p1)*sinf(a);float l1=clampf(.62f+.25f*y1-.13f*x1,.35f,1);
            GXColor c0=mixc((GXColor){18,25,20,255},base,l0),c1=mixc((GXColor){18,25,20,255},base,l1);
            vtx(c.x+r*x0,c.y+r*y0,c.z+r*z0,c0);vtx(c.x+r*x1,c.y+r*y1,c.z+r*z1,c1);
        }GX_End();
    }
}
static void draw_beam(V3 a,V3 b,float w,GXColor c){V3 d=vnorm(vsub(b,a));V3 s=vnorm(v3(d.z,.22f,-d.x));if(vlen(s)<.1f)s=v3(1,0,0);s=vmul(s,w);V3 p0=vadd(a,s),p1=vsub(a,s),p2=vsub(b,s),p3=vadd(b,s);GX_Begin(GX_QUADS,GX_VTXFMT0,4);vtx(p0.x,p0.y,p0.z,c);vtx(p1.x,p1.y,p1.z,c);vtx(p2.x,p2.y,p2.z,c);vtx(p3.x,p3.y,p3.z,c);GX_End();}
static void draw_disc(float x,float y,float z,float rx,float rz,GXColor c){GX_Begin(GX_TRIANGLEFAN,GX_VTXFMT0,18);vtx(x,y,z,c);for(int i=0;i<=16;i++){float a=2*PI*i/16.0f;vtx(x+cosf(a)*rx,y,z+sinf(a)*rz,c);}GX_End();}
static void draw_terrain(void){const float xmin=-78,xmax=78,dx=2.5f,dz=2.5f;for(float z=0;z<RANGE_MAX;z+=dz){GX_Begin(GX_TRIANGLESTRIP,GX_VTXFMT0,(u16)(((xmax-xmin)/dx+1)*2));for(float x=xmin;x<=xmax+.1f;x+=dx){vtx(x,terrain_h(z,x),z,grass_color(z,x));vtx(x,terrain_h(z+dz,x),z+dz,grass_color(z+dz,x));}GX_End();}}
static void draw_tree(float x,float z,float s){float y=terrain_h(z,x);draw_disc(x,y+.03f,z,3.8f*s,1.8f*s,(GXColor){24,54,29,100});draw_beam(v3(x,y,z),v3(x,y+6*s,z),.45f*s,(GXColor){103,73,44,255});GXColor dark={33,92,45,255},mid={50,122,57,255},lite={67,143,66,255};draw_sphere(v3(x,y+7.2f*s,z),3.5f*s,dark,4,8);draw_sphere(v3(x-2.1f*s,y+7.0f*s,z+.5f*s),2.8f*s,mid,4,8);draw_sphere(v3(x+2.1f*s,y+7.4f*s,z-.4f*s),2.9f*s,lite,4,8);}
static void draw_scenery(void){for(int i=0;i<26;i++){float z=18+i*13.5f,w=4.2f*sinf(i*1.4f);draw_tree(-54-w,z,.78f+(i%3)*.06f);if(i%2==0)draw_tree(55+w,z+5,.8f+(i%4)*.05f);}for(int i=0;i<7;i++){float z=52+i*45;draw_tree(-69,z,.7f);draw_tree(69,z+14,.72f);}}
static void draw_target(float z,float x,float r){float y=terrain_h(z,x)+.06f;GXColor white={247,246,233,255},red={220,71,58,255};GX_Begin(GX_LINESTRIP,GX_VTXFMT0,33);for(int i=0;i<=32;i++){float a=2*PI*i/32;float xx=x+cosf(a)*r,zz=z+sinf(a)*r;vtx(xx,terrain_h(zz,xx)+.09f,zz,white);}GX_End();draw_beam(v3(x,y,z),v3(x,y+6,z),.05f,white);GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3);vtx(x,y+6,z,red);vtx(x+3.6f,y+5.35f,z,red);vtx(x,y+4.7f,z,red);GX_End();}
static void draw_targets(void){draw_target(100,0,7);draw_target(150,-8,8);draw_target(200,8,9);draw_target(250,-5,10);}
static void draw_ball(const Ball *b){draw_sphere(v3(b->x,b->y,b->z),.34f,(GXColor){253,253,248,255},5,10);}

static V3 golfer_origin(void){float x=3.7f,z=7.0f;return v3(x,terrain_h(z,x),z);}
static void draw_golfer(const Swing *s,int hide){if(hide)return;V3 o=golfer_origin();GXColor skin={227,184,138,255},shirt={74,145,184,255},pants={240,239,224,255},shoe={47,52,54,255},club={190,195,199,255},dark={66,70,72,255};
    draw_disc(o.x,o.y+.02f,o.z,1.9f,1.0f,(GXColor){25,54,29,110});
    draw_beam(v3(o.x-.48f,o.y+.25f,o.z),v3(o.x-.58f,o.y+2.9f,o.z),.34f,pants);draw_beam(v3(o.x+.48f,o.y+.25f,o.z),v3(o.x+.58f,o.y+2.9f,o.z),.34f,pants);
    draw_sphere(v3(o.x-.50f,o.y+.25f,o.z-.15f),.48f,shoe,3,7);draw_sphere(v3(o.x+.50f,o.y+.25f,o.z-.15f),.48f,shoe,3,7);
    draw_sphere(v3(o.x,o.y+3.7f,o.z),1.38f,shirt,5,10);draw_sphere(v3(o.x,o.y+5.55f,o.z),.90f,skin,6,12);
    draw_sphere(v3(o.x,o.y+6.15f,o.z-.03f),.62f,(GXColor){70,49,36,255},4,10);
    /* Mii-like face marks, original geometry */
    draw_sphere(v3(o.x-.29f,o.y+5.72f,o.z-.78f),.08f,dark,3,6);draw_sphere(v3(o.x+.29f,o.y+5.72f,o.z-.78f),.08f,dark,3,6);
    float a=DEG2RAD(clampf(s->club_angle,-110,85));float face=DEG2RAD(clampf(s->face_angle,-28,28));
    V3 shL=v3(o.x-.68f,o.y+4.45f,o.z),shR=v3(o.x+.68f,o.y+4.45f,o.z);
    V3 hands=v3(o.x-.70f+1.20f*sinf(a),o.y+3.45f+1.15f*cosf(a),o.z-.40f-.75f*sinf(a));
    draw_beam(shL,hands,.20f,skin);draw_beam(shR,hands,.20f,skin);draw_sphere(hands,.25f,skin,3,7);
    V3 dir=vnorm(v3(.12f*sinf(face),-cosf(a),sinf(a)));V3 end=vadd(hands,vmul(dir,5.4f));draw_beam(hands,end,.065f,club);
    /* rounded club head */ draw_sphere(v3(end.x-.15f,end.y,end.z),.34f,dark,3,7);draw_beam(v3(end.x-.55f,end.y,end.z),v3(end.x+.18f,end.y,end.z),.16f,dark);
}

static void set_camera(const Ball *b,const Game *g){guVector up={0,1,0},eye,target;if(g->cam==CAM_BALL){float hs=hypotf(b->vx,b->vz);float dx=hs>.2f?b->vx/hs:0,dz=hs>.2f?b->vz/hs:1;eye=(guVector){b->x-dx*18+7,b->y+8.5f,b->z-dz*18};target=(guVector){b->x+dx*35,b->y+1.2f,b->z+dz*35};}else if(g->cam==CAM_RESULT){eye=(guVector){b->x+10,b->y+7,b->z-16};target=(guVector){b->x,b->y,b->z};}else{float sn=sinf(g->aim),cs=cosf(g->aim);eye=(guVector){12.5f-sn*2.5f,8.2f,-8.5f};target=(guVector){sn*82,terrain_h(88,sn*82)+2.0f,88*cs};}guLookAt(view,&eye,&up,&target);GX_LoadPosMtxImm(view,GX_PNMTX0);}

/* Compact Resort-inspired HUD: power is backswing amount, horizontal skew is face angle. */
static void hquad(float x0,float y0,float x1,float y1,GXColor c){GX_Begin(GX_QUADS,GX_VTXFMT0,4);vtx(x0,y0,0,c);vtx(x1,y0,0,c);vtx(x1,y1,0,c);vtx(x0,y1,0,c);GX_End();}
static void hline(float x0,float y0,float x1,float y1,GXColor c){GX_Begin(GX_LINES,GX_VTXFMT0,2);vtx(x0,y0,0,c);vtx(x1,y1,0,c);GX_End();}
static void draw_hud(const Swing *s,const Game *g){GX_LoadProjectionMtx(ortho,GX_ORTHOGRAPHIC);Mtx m;guMtxIdentity(m);GX_LoadPosMtxImm(m,GX_PNMTX0);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GXColor pane={245,246,240,210},ink={56,66,59,255},blue={71,153,202,255},green={77,171,89,255},red={211,79,61,255};
    hquad(20,92,76,395,pane);hquad(32,108,48,375,(GXColor){196,205,197,255});float p=clampf(s->power,0,1.05f);float top=375-255*p;hquad(32,top,48,375,p>1?red:green);for(int i=0;i<=4;i++)hline(51,375-i*64,66,375-i*64,ink);
    float skew=clampf(s->meter_face/25,-1,1);hline(40,375,40+skew*28,top,blue);
    /* simple club indicator dots */for(int i=0;i<CLUB_COUNT;i++){float x=500+i*22;hquad(x-6,26,x+6,38,(i==g->club)?blue:(GXColor){190,196,191,255});}
    /* MotionPlus status */hquad(567,92,620,112,s->motionplus?green:red);
    if(g->flash>0){GXColor f={255,244,165,(u8)(clampf(g->flash,0,1)*200)};hquad(0,0,640,14,f);}GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);GX_LoadProjectionMtx(perspective,GX_PERSPECTIVE);}

static void init_video(void){VIDEO_Init();WPAD_Init();rmode=VIDEO_GetPreferredMode(NULL);xfb[0]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));xfb[1]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));VIDEO_Configure(rmode);VIDEO_SetNextFramebuffer(xfb[0]);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();fifo=memalign(32,FIFO_SIZE);memset(fifo,0,FIFO_SIZE);GX_Init(fifo,FIFO_SIZE);GX_SetCopyClear((GXColor){123,190,226,255},0x00ffffff);GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);f32 ys=GX_GetYScaleFactor(rmode->efbHeight,rmode->xfbHeight);u16 xh=GX_SetDispCopyYScale(ys);GX_SetScissor(0,0,rmode->fbWidth,rmode->efbHeight);GX_SetDispCopySrc(0,0,rmode->fbWidth,rmode->efbHeight);GX_SetDispCopyDst(rmode->fbWidth,xh);GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,GX_TRUE,rmode->vfilter);GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_CLEAR);GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);GX_SetNumChans(1);GX_SetNumTexGens(0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetFog(GX_FOG_LIN,125,410,.5f,700,(GXColor){123,190,226,255});guPerspective(perspective,48.0f,4.0f/3.0f,.35f,700);guOrtho(ortho,0,480,0,640,0,300);WPAD_SetDataFormat(WPAD_CHAN_0,WPAD_FMT_BTNS_ACC_IR);WPAD_SetMotionPlus(WPAD_CHAN_0,1);}

static void reset_ball(Ball *b){memset(b,0,sizeof(*b));b->x=0;b->z=7;b->y=terrain_h(b->z,b->x)+.34f;}
static void reset_stance(Swing *s){struct orient_t o;WPAD_Orientation(0,&o);s->base_pitch=o.pitch;s->base_roll=o.roll;s->gyro_pitch=s->gyro_roll=s->gyro_yaw=0;s->max_backswing=0;s->had_backswing=0;s->addressed=0;s->impact_lock=0;}

static void update_motion(Swing *s,u32 held,u32 down){struct orient_t o;WPAD_Orientation(0,&o);s->live_pitch=o.pitch;s->live_roll=o.roll;WPADData *wd=WPAD_Data(0);s->motionplus=(wd && wd->exp.type==5);if(s->motionplus){float rx=wd->exp.mp.rx,ry=wd->exp.mp.ry,rz=wd->exp.mp.rz;if(s->mp_cal_frames<45){s->mp_sum_rx+=rx;s->mp_sum_ry+=ry;s->mp_sum_rz+=rz;s->mp_cal_frames++;if(s->mp_cal_frames==45){s->mp_zero_rx=s->mp_sum_rx/45;s->mp_zero_ry=s->mp_sum_ry/45;s->mp_zero_rz=s->mp_sum_rz/45;}}else{float gy=(ry-s->mp_zero_ry)/13.77f,gp=(rz-s->mp_zero_rz)/13.77f,gr=(rx-s->mp_zero_rx)/13.77f;s->gyro_yaw+=gy*DT;s->gyro_pitch+=gp*DT;s->gyro_roll+=gr*DT;}}
    if(down&WPAD_BUTTON_B) reset_stance(s);
    float dp=o.pitch-s->base_pitch,dr=o.roll-s->base_roll;while(dp>180)dp-=360;while(dp<-180)dp+=360;while(dr>180)dr-=360;while(dr<-180)dr+=360;
    float ang=clampf(-12.0f-dp*1.8f,-112.0f,80.0f);if(s->motionplus && s->mp_cal_frames>=45)ang=clampf(ang+s->gyro_pitch*.30f,-112,80);
    s->angular_speed=(ang-s->last_club_angle)/DT;s->last_club_angle=ang;s->club_angle=ang;s->face_angle=clampf(dr*.72f+(s->motionplus?s->gyro_roll*.12f:0),-28,28);s->meter_face=s->face_angle;
    float back=clampf((-ang-12.0f)/88.0f,0,1.12f);s->power=back;if(back>s->max_backswing)s->max_backswing=back;if(back>.18f)s->had_backswing=1;
    if(down&WPAD_BUTTON_A){s->addressed=1;s->max_backswing=back;s->had_backswing=0;s->impact_lock=0;}
    if(!(held&WPAD_BUTTON_A)&&s->addressed && !s->impact_lock){s->addressed=0;s->had_backswing=0;s->max_backswing=0;}
}

static void launch_ball(Ball *b,Game *g,Swing *s){float power=clampf(s->max_backswing,.20f,1.05f);float launch=DEG2RAD(CLUBS[g->club].launch);float desired=CLUBS[g->club].carry*power;float speed=sqrtf(fmaxf(desired*G_YD/fmaxf(sinf(2*launch),.16f),1));float face=DEG2RAD(clampf(s->face_angle,-22,22));float dir=g->aim+face*.18f;b->vx=sinf(dir)*speed*cosf(launch);b->vz=cosf(dir)*speed*cosf(launch);b->vy=sinf(launch)*speed;b->spin_side=clampf(s->face_angle/20.0f,-1,1);b->spin_back=CLUBS[g->club].loft_spin;b->moving=1;b->flying=1;b->rest_time=0;g->shots++;g->cam=CAM_HOLD;g->cam_frames=0;g->flash=1.0f;s->impact_lock=1;WPAD_Rumble(0,1);}
static void detect_impact(Ball *b,Game *g,Swing *s,u32 held){if(b->moving||!s->addressed||!s->had_backswing||s->impact_lock)return;/* impact: club returns through address with a genuine downswing */if(s->club_angle>-20.0f && s->angular_speed>85.0f && (held&WPAD_BUTTON_A)){launch_ball(b,g,s);}}

static void update_ball(Ball *b,Game *g){if(g->cam==CAM_HOLD){g->cam_frames++;if(g->cam_frames>20){g->cam=CAM_BALL;WPAD_Rumble(0,0);}}if(!b->moving)return;float sp=vlen(v3(b->vx,b->vy,b->vz));if(b->flying){float drag=.00042f*sp;float ax=-drag*b->vx + g->wind_x*.010f;float az=-drag*b->vz + g->wind_z*.010f;float horiz=hypotf(b->vx,b->vz);float lift=.010f*b->spin_back*horiz;float side=.0065f*b->spin_side*horiz;ax+=side*b->vz/(horiz+.01f);az-=side*b->vx/(horiz+.01f);b->vx+=ax*DT;b->vz+=az*DT;b->vy+=(-G_YD-drag*b->vy+lift)*DT;b->x+=b->vx*DT;b->z+=b->vz*DT;b->y+=b->vy*DT;float ground=terrain_h(b->z,b->x)+.34f;if(b->y<=ground){b->y=ground;if(fabsf(b->vy)>2.0f){b->vy=-b->vy*.22f;b->vx*=.78f;b->vz*=.78f;b->spin_back*=.65f;}else{b->flying=0;b->vy=0;}}}else{float hx=(terrain_h(b->z,b->x+1)-terrain_h(b->z,b->x-1))*.5f,hz=(terrain_h(b->z+1,b->x)-terrain_h(b->z-1,b->x))*.5f;b->vx-=hx*G_YD*DT;b->vz-=hz*G_YD*DT;float fr=in_bunker(b->z,b->x)?.940f:.982f;b->vx*=fr;b->vz*=fr;b->x+=b->vx*DT;b->z+=b->vz*DT;b->y=terrain_h(b->z,b->x)+.34f;if(hypotf(b->vx,b->vz)<.24f){b->moving=0;b->vx=b->vz=0;b->distance=hypotf(b->x,b->z-7);g->cam=CAM_RESULT;g->cam_frames=0;}}if(b->z>RANGE_MAX+40||fabsf(b->x)>115){b->moving=0;b->distance=hypotf(b->x,b->z-7);g->cam=CAM_RESULT;}}

static void render(const Ball *b,const Swing *s,const Game *g){GX_LoadProjectionMtx(perspective,GX_PERSPECTIVE);GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);set_camera(b,g);draw_terrain();draw_targets();draw_scenery();draw_golfer(s,g->cam==CAM_BALL);draw_ball(b);draw_hud(s,g);}

int main(int argc,char **argv){(void)argc;(void)argv;init_video();Ball b;Swing s;Game g;memset(&s,0,sizeof(s));memset(&g,0,sizeof(g));g.club=0;g.cam=CAM_TEE;g.wind_x=.7f;g.wind_z=.2f;reset_ball(&b);for(int i=0;i<30;i++){WPAD_ScanPads();VIDEO_WaitVSync();}reset_stance(&s);
    while(SYS_MainLoop()){WPAD_ScanPads();u32 down=WPAD_ButtonsDown(0),held=WPAD_ButtonsHeld(0);if(down&WPAD_BUTTON_HOME)break;if(down&WPAD_BUTTON_PLUS){reset_ball(&b);g.cam=CAM_TEE;g.cam_frames=0;s.impact_lock=0;s.addressed=0;s.max_backswing=0;}if(!b.moving&&g.cam!=CAM_HOLD){if(held&WPAD_BUTTON_LEFT)g.aim-=.010f;if(held&WPAD_BUTTON_RIGHT)g.aim+=.010f;if((down&WPAD_BUTTON_UP)&&g.club>0)g.club--;if((down&WPAD_BUTTON_DOWN)&&g.club<CLUB_COUNT-1)g.club++;}
        update_motion(&s,held,down);detect_impact(&b,&g,&s,held);update_ball(&b,&g);if(g.flash>0)g.flash*=.84f;if(g.cam==CAM_RESULT){g.cam_frames++;if(g.cam_frames>150){reset_ball(&b);g.cam=CAM_TEE;g.cam_frames=0;s.impact_lock=0;s.addressed=0;s.max_backswing=0;}}
        GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);GX_InvVtxCache();render(&b,&s,&g);GX_DrawDone();fb^=1;GX_CopyDisp(xfb[fb],GX_TRUE);VIDEO_SetNextFramebuffer(xfb[fb]);VIDEO_Flush();VIDEO_WaitVSync();}
    return 0;
}
