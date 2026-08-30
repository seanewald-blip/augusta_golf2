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
#define RANGE_Z_MAX 340.0f
#define GRAVITY 10.72f
#define DT (1.0f/60.0f)

typedef struct { float x,y,z; } V3;
typedef struct { const char *name; float carry, launch; } Club;
static const Club CLUBS[] = {
    {"Driver",275.0f,12.0f}, {"3 Wood",235.0f,14.0f}, {"5 Iron",185.0f,20.0f},
    {"7 Iron",155.0f,24.0f}, {"Wedge",100.0f,36.0f}
};
#define CLUB_COUNT ((int)(sizeof(CLUBS)/sizeof(CLUBS[0])))

typedef struct {
    float x,y,z;
    float vx,vy,vz;
    float aim;
    int club;
    int moving;
    int flying;
    int shots;
    float last_carry;
} Ball;

typedef struct {
    int armed;
    int frames;
    float start_pitch;
    float start_roll;
    float live_angle;
    float live_face;
    float peak_g;
    float power_preview;
    float impact_flash;
} Swing;

static GXRModeObj *rmode;
static void *xfb[2];
static void *fifo;
static u32 fb=0;
static Mtx44 perspective, ortho;
static Mtx view;

static float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
static float lerpf(float a,float b,float t){return a+(b-a)*t;}
static V3 v3(float x,float y,float z){V3 v={x,y,z};return v;}
static V3 vadd(V3 a,V3 b){return v3(a.x+b.x,a.y+b.y,a.z+b.z);}
static V3 vsub(V3 a,V3 b){return v3(a.x-b.x,a.y-b.y,a.z-b.z);}
static V3 vmul(V3 a,float s){return v3(a.x*s,a.y*s,a.z*s);}
static float vlen(V3 a){return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z);}
static V3 vnorm(V3 a){float l=vlen(a);return l>0.0001f?vmul(a,1.0f/l):v3(0,0,0);}

/* Smooth, original driving-range terrain: deliberately not presented as survey-grade Augusta data. */
static float terrain_h(float z,float x){
    float long_roll = 1.7f*sinf(z*0.021f) + 0.85f*sinf(z*0.047f+0.7f);
    float cross = 0.016f*x + 0.42f*sinf(x*0.055f + z*0.012f);
    float tee = 1.4f*expf(-((z-5.0f)*(z-5.0f))/260.0f);
    float landing = -1.0f*expf(-((z-220.0f)*(z-220.0f))/2200.0f);
    return long_roll + cross + tee + landing;
}

static float fair_center(float z){return -3.5f*sinf(z*0.0105f);}
static float fair_half(float z){return 27.0f + 5.0f*sinf(z*0.017f+1.0f);}
static int in_fairway(float z,float x){return fabsf(x-fair_center(z))<fair_half(z);}
static int in_bunker(float z,float x){
    float dx=x-27.0f,dz=z-118.0f;
    if(dx*dx/(14.0f*14.0f)+dz*dz/(22.0f*22.0f)<1.0f)return 1;
    dx=x+31.0f;dz=z-205.0f;
    if(dx*dx/(12.0f*12.0f)+dz*dz/(19.0f*19.0f)<1.0f)return 1;
    return 0;
}
static int in_water(float z,float x){
    if(z>250.0f && z<288.0f){float edge=-35.0f+0.08f*(z-250.0f);if(x<edge)return 1;}
    return 0;
}

static GXColor mixc(GXColor a,GXColor b,float t){
    t=clampf(t,0,1);GXColor c;
    c.r=(u8)clampf(lerpf(a.r,b.r,t),0,255);c.g=(u8)clampf(lerpf(a.g,b.g,t),0,255);
    c.b=(u8)clampf(lerpf(a.b,b.b,t),0,255);c.a=255;return c;
}
static GXColor ground_color(float z,float x){
    if(in_water(z,x)) return (GXColor){55,116,145,255};
    if(in_bunker(z,x)) return (GXColor){205,187,137,255};
    GXColor fairA={67,135,60,255}, fairB={75,146,66,255};
    GXColor roughA={38,91,44,255}, roughB={45,103,48,255};
    float stripe=0.5f+0.5f*sinf(z*0.34f);
    GXColor base=in_fairway(z,x)?mixc(fairA,fairB,stripe):mixc(roughA,roughB,0.5f+0.5f*sinf(z*.10f+x*.075f));
    float hx=(terrain_h(z,x+1.2f)-terrain_h(z,x-1.2f))/2.4f;
    float hz=(terrain_h(z+1.2f,x)-terrain_h(z-1.2f,x))/2.4f;
    float light=clampf(0.55f-0.36f*hx-0.25f*hz,0.15f,0.9f);
    GXColor shadow={18,45,25,255}, sun={112,174,91,255};
    return mixc(shadow,mixc(base,sun,0.22f),light);
}

static void gx_color(GXColor c){GX_Color4u8(c.r,c.g,c.b,c.a);}
static void vtx(float x,float y,float z,GXColor c){GX_Position3f32(x,y,z);gx_color(c);}

static void draw_box(float cx,float cy,float cz,float sx,float sy,float sz,GXColor c){
    float x0=cx-sx*.5f,x1=cx+sx*.5f,y0=cy,y1=cy+sy,z0=cz-sz*.5f,z1=cz+sz*.5f;
    GX_Begin(GX_QUADS,GX_VTXFMT0,24);
    vtx(x0,y0,z0,c);vtx(x1,y0,z0,c);vtx(x1,y1,z0,c);vtx(x0,y1,z0,c);
    vtx(x1,y0,z1,c);vtx(x0,y0,z1,c);vtx(x0,y1,z1,c);vtx(x1,y1,z1,c);
    vtx(x0,y0,z1,c);vtx(x0,y0,z0,c);vtx(x0,y1,z0,c);vtx(x0,y1,z1,c);
    vtx(x1,y0,z0,c);vtx(x1,y0,z1,c);vtx(x1,y1,z1,c);vtx(x1,y1,z0,c);
    vtx(x0,y1,z0,c);vtx(x1,y1,z0,c);vtx(x1,y1,z1,c);vtx(x0,y1,z1,c);
    vtx(x0,y0,z1,c);vtx(x1,y0,z1,c);vtx(x1,y0,z0,c);vtx(x0,y0,z0,c);
    GX_End();
}

static void draw_beam(V3 a,V3 b,float width,GXColor c){
    V3 d=vnorm(vsub(b,a));
    V3 side=vnorm(v3(d.z*0.9f,0.15f,-d.x*0.9f));
    if(vlen(side)<0.1f) side=v3(1,0,0);
    side=vmul(side,width);
    V3 p0=vadd(a,side),p1=vsub(a,side),p2=vsub(b,side),p3=vadd(b,side);
    GX_Begin(GX_QUADS,GX_VTXFMT0,4);
    vtx(p0.x,p0.y,p0.z,c);vtx(p1.x,p1.y,p1.z,c);vtx(p2.x,p2.y,p2.z,c);vtx(p3.x,p3.y,p3.z,c);GX_End();
}

static void draw_terrain(void){
    const float xmin=-72.0f,xmax=72.0f,dx=4.0f,dz=4.0f;
    for(float z=0;z<RANGE_Z_MAX;z+=dz){
        GX_Begin(GX_TRIANGLESTRIP,GX_VTXFMT0,(u16)(((xmax-xmin)/dx+1)*2));
        for(float x=xmin;x<=xmax+0.1f;x+=dx){
            GXColor c0=ground_color(z,x),c1=ground_color(z+dz,x);
            vtx(x,terrain_h(z,x),z,c0);vtx(x,terrain_h(z+dz,x),z+dz,c1);
        }
        GX_End();
    }
}

static void draw_shadow(float x,float z,float rx,float rz){
    float y=terrain_h(z,x)+0.025f;GXColor s={20,38,21,125};
    GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_CLEAR);
    GX_Begin(GX_TRIANGLEFAN,GX_VTXFMT0,14);vtx(x,y,z,s);
    for(int i=0;i<=12;i++){float a=2*PI*i/12.0f;vtx(x+cosf(a)*rx,y,z+sinf(a)*rz,s);}GX_End();
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_CLEAR);
}

static void draw_tree(float x,float z,float s){
    float y=terrain_h(z,x);GXColor trunk={86,61,37,255};
    GXColor dark={24,67,31,255},mid={38,92,42,255},lite={50,108,47,255};
    draw_shadow(x,z,4.0f*s,2.0f*s);
    draw_box(x,y,z,1.0f*s,6.2f*s,1.0f*s,trunk);
    float r=4.3f*s;
    for(int level=0;level<3;level++){
        float yy=y+4.5f*s+level*2.8f*s, rr=r*(1.0f-level*.14f);GXColor c=level==0?dark:(level==1?mid:lite);
        GX_Begin(GX_TRIANGLES,GX_VTXFMT0,24);
        for(int i=0;i<8;i++){float a0=2*PI*i/8.0f,a1=2*PI*(i+1)/8.0f;
            vtx(x,yy+5.5f*s,z,c);vtx(x+cosf(a0)*rr,yy,z+sinf(a0)*rr,c);vtx(x+cosf(a1)*rr,yy,z+sinf(a1)*rr,c);}
        GX_End();
    }
}

static void draw_scenery(void){
    for(int i=0;i<25;i++){
        float z=20.0f+i*13.0f;float wig=5.0f*sinf(i*1.37f);
        draw_tree(-51.0f-wig,z,(0.82f+(i%4)*.07f));
        if(i%2==0)draw_tree(52.0f+wig,z+5.0f,(0.86f+(i%3)*.08f));
    }
    for(int i=0;i<8;i++){float z=65.0f+i*35.0f;draw_tree(-66.0f,z+8,0.72f);draw_tree(66.0f,z-4,0.76f);}
}

static void draw_target(float z,float x,int yards){
    float y=terrain_h(z,x);GXColor white={242,242,235,255}, red={205,62,49,255}, pole={220,222,219,255};
    GX_Begin(GX_LINES,GX_VTXFMT0,2);vtx(x,y,z,pole);vtx(x,y+7.0f,z,pole);GX_End();
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3);vtx(x,y+7,z,red);vtx(x+4.0f,y+6.0f,z,red);vtx(x,y+5.2f,z,white);GX_End();
    float r=(yards==100?7.0f:(yards==150?8.0f:(yards==200?9.0f:10.0f)));
    GX_Begin(GX_LINESTRIP,GX_VTXFMT0,25);for(int i=0;i<=24;i++){float a=2*PI*i/24.0f;float xx=x+cosf(a)*r,zz=z+sinf(a)*r;vtx(xx,terrain_h(zz,xx)+.08f,zz,white);}GX_End();
}

static void draw_targets(void){draw_target(100,0,100);draw_target(150,-9,150);draw_target(200,8,200);draw_target(250,-5,250);}

static void draw_ball(const Ball *b){
    GXColor w={250,250,247,255};float r=.42f;
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,24);
    vtx(b->x,b->y+r,b->z,w);vtx(b->x-r,b->y,b->z,w);vtx(b->x,b->y,b->z+r,w);
    vtx(b->x,b->y+r,b->z,w);vtx(b->x,b->y,b->z+r,w);vtx(b->x+r,b->y,b->z,w);
    vtx(b->x,b->y+r,b->z,w);vtx(b->x+r,b->y,b->z,w);vtx(b->x,b->y,b->z-r,w);
    vtx(b->x,b->y+r,b->z,w);vtx(b->x,b->y,b->z-r,w);vtx(b->x-r,b->y,b->z,w);
    vtx(b->x,b->y-r,b->z,w);vtx(b->x,b->y,b->z+r,w);vtx(b->x-r,b->y,b->z,w);
    vtx(b->x,b->y-r,b->z,w);vtx(b->x+r,b->y,b->z,w);vtx(b->x,b->y,b->z+r,w);
    vtx(b->x,b->y-r,b->z,w);vtx(b->x,b->y,b->z-r,w);vtx(b->x+r,b->y,b->z,w);
    vtx(b->x,b->y-r,b->z,w);vtx(b->x-r,b->y,b->z,w);vtx(b->x,b->y,b->z-r,w);GX_End();
}

static void draw_golfer(const Ball *b,const Swing *sw){
    if(b->moving)return;
    float gx=b->x+3.7f,gz=b->z-.8f,gy=terrain_h(gz,gx);
    GXColor shoe={35,39,37,255},pants={226,225,214,255},shirt={36,92,62,255},skin={210,170,125,255};
    GXColor club={185,190,192,255},head={94,99,101,255};
    draw_shadow(gx,gz,2.1f,1.1f);
    draw_box(gx-.55f,gy,gz,.75f,2.8f,.72f,pants);draw_box(gx+.55f,gy,gz,.75f,2.8f,.72f,pants);
    draw_box(gx-.55f,gy-.05f,gz-.15f,.9f,.35f,1.2f,shoe);draw_box(gx+.55f,gy-.05f,gz-.15f,.9f,.35f,1.2f,shoe);
    draw_box(gx,gy+2.65f,gz,2.4f,3.05f,1.18f,shirt);draw_box(gx,gy+5.75f,gz,.95f,1.25f,.95f,skin);

    float a=DEG2RAD(sw->armed?sw->live_angle:-14.0f);
    float face=DEG2RAD(sw->armed?sw->live_face:0.0f);
    V3 shoulder=v3(gx-.45f,gy+4.75f,gz+.05f);
    V3 hands=v3(gx-1.0f+0.55f*sinf(a),gy+3.65f+0.70f*cosf(a),gz-.25f-0.45f*sinf(a));
    draw_beam(shoulder,hands,.18f,skin);draw_beam(v3(gx+.55f,gy+4.7f,gz),hands,.18f,skin);
    V3 dir=v3(sinf(face)*.20f,-cosf(a),sinf(a));dir=vnorm(dir);
    V3 end=vadd(hands,vmul(dir,5.8f));
    draw_beam(hands,end,.075f,club);
    draw_box(end.x-.18f,end.y-.12f,end.z,.72f,.30f,.30f,head);
}

static void set_camera(const Ball *b){
    guVector up={0,1,0},eye,target;
    if(b->moving){
        float speed=sqrtf(b->vx*b->vx+b->vz*b->vz);float back=clampf(22.0f+speed*.18f,22.0f,40.0f);
        V3 d=vnorm(v3(b->vx,0,b->vz));
        eye=(guVector){b->x-d.x*back+7.0f,b->y+12.0f,b->z-d.z*back};
        target=(guVector){b->x+d.x*28.0f,b->y+1.0f,b->z+d.z*28.0f};
    }else{
        float s=sinf(b->aim),c=cosf(b->aim);
        eye=(guVector){b->x+8.6f-7.0f*s,terrain_h(b->z-11,b->x+8.6f)+7.4f,b->z-11.5f*c};
        float tz=b->z+72.0f*c,tx=b->x+72.0f*s;
        target=(guVector){tx,terrain_h(tz,tx)+2.4f,tz};
    }
    guLookAt(view,&eye,&up,&target);GX_LoadPosMtxImm(view,GX_PNMTX0);
}

static const unsigned char SEG[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
static void hud_quad(float x1,float y1,float x2,float y2,GXColor c){GX_Begin(GX_QUADS,GX_VTXFMT0,4);vtx(x1,y1,0,c);vtx(x2,y1,0,c);vtx(x2,y2,0,c);vtx(x1,y2,0,c);GX_End();}
static void digit(int d,float x,float y,float s,GXColor c){if(d<0||d>9)return;unsigned m=SEG[d];float t=2*s,w=8*s,h=14*s;if(m&1)hud_quad(x+t,y,x+w-t,y+t,c);if(m&2)hud_quad(x+w-t,y+t,x+w,y+h/2-t/2,c);if(m&4)hud_quad(x+w-t,y+h/2+t/2,x+w,y+h-t,c);if(m&8)hud_quad(x+t,y+h-t,x+w-t,y+h,c);if(m&16)hud_quad(x,y+h/2+t/2,x+t,y+h-t,c);if(m&32)hud_quad(x,y+t,x+t,y+h/2-t/2,c);if(m&64)hud_quad(x+t,y+h/2-t/2,x+w-t,y+h/2+t/2,c);}
static void number(int n,float x,float y,float s,GXColor c){if(n<0){hud_quad(x,y+6*s,x+6*s,y+8*s,c);x+=9*s;n=-n;}int a=n/100,b=(n/10)%10,d=n%10;if(a){digit(a,x,y,s,c);x+=10*s;}if(a||b){digit(b,x,y,s,c);x+=10*s;}digit(d,x,y,s,c);}

static void draw_hud(const Ball *b,const Swing *sw){
    GX_LoadProjectionMtx(ortho,GX_ORTHOGRAPHIC);Mtx m;guMtxIdentity(m);GX_LoadPosMtxImm(m,GX_PNMTX0);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
    GXColor panel={7,18,11,210},white={245,245,240,255},gold={239,216,72,255},green={80,192,91,255},red={220,78,59,255};
    hud_quad(0,0,640,55,panel);
    number(b->club+1,18,12,1.5f,gold);number((int)CLUBS[b->club].carry,70,12,1.5f,white);number(b->shots,170,12,1.5f,white);
    if(b->last_carry>1)number((int)b->last_carry,235,12,1.5f,green);
    if(sw->armed){
        hud_quad(85,425,555,472,panel);
        float p=clampf(sw->power_preview,0,1);hud_quad(96,438,96+300*p,451,gold);
        float t=clampf((sw->live_angle+90.0f)/180.0f,0,1);hud_quad(96+300*t-3,456,96+300*t+3,468,white);
        float f=clampf(sw->live_face/25.0f,-1,1);float mid=475;hud_quad(mid,438,mid+f*65,451,fabsf(f)<.25f?green:red);
    }
    if(sw->impact_flash>0){GXColor flash={255,238,120,(u8)clampf(sw->impact_flash*255,0,255)};hud_quad(0,55,640,65,flash);}
    GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);GX_LoadProjectionMtx(perspective,GX_PERSPECTIVE);
}

static void init_video(void){
    VIDEO_Init();WPAD_Init();rmode=VIDEO_GetPreferredMode(NULL);
    xfb[0]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));xfb[1]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);VIDEO_SetNextFramebuffer(xfb[0]);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
    fifo=memalign(32,FIFO_SIZE);memset(fifo,0,FIFO_SIZE);GX_Init(fifo,FIFO_SIZE);
    GX_SetCopyClear((GXColor){118,177,211,255},0x00ffffff);
    GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);f32 ys=GX_GetYScaleFactor(rmode->efbHeight,rmode->xfbHeight);u16 xh=GX_SetDispCopyYScale(ys);
    GX_SetScissor(0,0,rmode->fbWidth,rmode->efbHeight);GX_SetDispCopySrc(0,0,rmode->fbWidth,rmode->efbHeight);GX_SetDispCopyDst(rmode->fbWidth,xh);
    GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,GX_TRUE,rmode->vfilter);GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_CLEAR);
    GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GX_SetNumChans(1);GX_SetNumTexGens(0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GX_SetFog(GX_FOG_LIN,100.0f,390.0f,0.5f,700.0f,(GXColor){118,177,211,255});
    guPerspective(perspective,55.0f,4.0f/3.0f,0.4f,700.0f);GX_LoadProjectionMtx(perspective,GX_PERSPECTIVE);guOrtho(ortho,0,480,0,640,0,300);
    WPAD_SetDataFormat(WPAD_CHAN_0,WPAD_FMT_BTNS_ACC_IR);
}

static void reset_ball(Ball *b){memset(b,0,sizeof(*b));b->z=7.0f;b->x=0;b->y=terrain_h(b->z,b->x)+.45f;b->club=0;}

static void launch_ball(Ball *b,float power,float face_deg){
    float a=DEG2RAD(CLUBS[b->club].launch);float carry=CLUBS[b->club].carry*power;
    float speed=sqrtf(fmaxf(carry*GRAVITY/fmaxf(sinf(2*a),.16f),1.0f));
    float dir=b->aim+DEG2RAD(face_deg)*.55f;
    b->vx=sinf(dir)*speed*cosf(a);b->vz=cosf(dir)*speed*cosf(a);b->vy=sinf(a)*speed;
    b->moving=1;b->flying=1;b->shots++;b->last_carry=0;
}

static void update_ball(Ball *b){
    if(!b->moving)return;
    if(b->flying){
        b->x+=b->vx*DT;b->z+=b->vz*DT;b->y+=b->vy*DT;b->vy-=GRAVITY*DT;b->vx*=.9992f;b->vz*=.9992f;
        float ground=terrain_h(b->z,b->x)+.45f;
        if(b->y<=ground){b->y=ground;if(fabsf(b->vy)>2.1f){b->vy=-b->vy*.24f;b->vx*=.77f;b->vz*=.77f;}else{b->flying=0;b->vy=0;}}
    }else{
        b->x+=b->vx*DT;b->z+=b->vz*DT;b->y=terrain_h(b->z,b->x)+.45f;
        float f=in_bunker(b->z,b->x)?.945f:(in_fairway(b->z,b->x)?.985f:.972f);b->vx*=f;b->vz*=f;
        if(hypotf(b->vx,b->vz)<.25f){b->moving=0;b->vx=b->vz=0;b->last_carry=hypotf(b->x,b->z-7.0f);}
    }
    if(b->z>RANGE_Z_MAX+35||fabsf(b->x)>110){b->moving=0;b->last_carry=hypotf(b->x,b->z-7.0f);}
}

static void update_swing(Swing *sw,u32 down,u32 held,Ball *b){
    struct orient_t o;struct gforce_t gf;WPAD_Orientation(0,&o);WPAD_GForce(0,&gf);
    if((down&WPAD_BUTTON_B)&&!b->moving){sw->armed=1;sw->frames=0;sw->start_pitch=o.pitch;sw->start_roll=o.roll;sw->live_angle=-14;sw->live_face=0;sw->peak_g=0;sw->power_preview=0;}
    if(sw->armed){
        float dp=o.pitch-sw->start_pitch;float dr=o.roll-sw->start_roll;
        sw->live_angle=clampf(-14.0f-dp*1.85f,-105.0f,85.0f);sw->live_face=clampf(dr*.65f,-25.0f,25.0f);
        float mag=sqrtf(gf.x*gf.x+gf.y*gf.y+gf.z*gf.z);if(mag>sw->peak_g)sw->peak_g=mag;
        sw->power_preview=clampf((sw->peak_g-1.05f)/2.9f,0,1);sw->frames++;
        if(!(held&WPAD_BUTTON_B)&&sw->frames>4){float power=clampf(sw->power_preview,.12f,1.0f);launch_ball(b,power,sw->live_face);sw->armed=0;sw->impact_flash=1.0f;WPAD_Rumble(0,1);}
    }
    if(sw->impact_flash>0)sw->impact_flash*=.84f;
}

static void render(const Ball *b,const Swing *sw){
    GX_LoadProjectionMtx(perspective,GX_PERSPECTIVE);GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);set_camera(b);
    draw_terrain();draw_targets();draw_scenery();draw_golfer(b,sw);draw_ball(b);draw_hud(b,sw);
}

int main(int argc,char **argv){
    (void)argc;(void)argv;init_video();Ball b;Swing sw={0};reset_ball(&b);
    while(SYS_MainLoop()){
        WPAD_ScanPads();u32 down=WPAD_ButtonsDown(0),held=WPAD_ButtonsHeld(0);
        if(down&WPAD_BUTTON_HOME)break;
        if(down&WPAD_BUTTON_PLUS){reset_ball(&b);memset(&sw,0,sizeof(sw));}
        if(!b.moving){
            if(held&WPAD_BUTTON_LEFT)b.aim-=.012f;if(held&WPAD_BUTTON_RIGHT)b.aim+=.012f;
            if((down&WPAD_BUTTON_UP)&&b.club>0)b.club--;if((down&WPAD_BUTTON_DOWN)&&b.club<CLUB_COUNT-1)b.club++;
        }
        update_swing(&sw,down,held,&b);update_ball(&b);if(b.moving&&sw.impact_flash<.1f)WPAD_Rumble(0,0);
        GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);GX_InvVtxCache();render(&b,&sw);GX_DrawDone();
        fb^=1;GX_CopyDisp(xfb[fb],GX_TRUE);VIDEO_SetNextFramebuffer(xfb[fb]);VIDEO_Flush();VIDEO_WaitVSync();
    }
    return 0;
}
