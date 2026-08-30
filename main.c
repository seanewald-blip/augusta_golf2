#include <gccore.h>
#include <wiiuse/wpad.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIFO_SIZE (256*1024)
#define PI 3.14159265358979323846f
#define DEG2RAD(x) ((x)*PI/180.0f)
#define HOLE_YARDS 510.0f
#define GRAVITY 10.72f

typedef enum { LIE_TEE, LIE_FAIRWAY, LIE_ROUGH, LIE_GREEN, LIE_BUNKER, LIE_WATER } Lie;
typedef struct { const char *name; float carry, launch; } Club;
static const Club CLUBS[] = {
    {"Driver",285,12},{"3 Wood",240,14},{"5 Iron",190,19},{"7 Iron",160,23},
    {"9 Iron",130,29},{"Wedge",95,38},{"Putter",36,0}
};
#define CLUB_COUNT ((int)(sizeof(CLUBS)/sizeof(CLUBS[0])))

typedef struct {
    float z, x, y;          /* z = yards toward green, x = lateral yards, y = elevation yards */
    float vz, vx, vy;
    float aim;
    float safe_z, safe_x;
    int club, strokes, moving, flying, holed;
    Lie lie;
} Game;

typedef struct { int armed, frames; float peak_g, start_roll; } Swing;

typedef enum { CAM_PLAYER, CAM_BALL, CAM_PUTT } CameraMode;

static GXRModeObj *rmode;
static void *xfb[2];
static void *fifo;
static u32 fb = 0;
static Mtx44 perspective, ortho;
static Mtx view;

static float clampf(float v,float a,float b){ return v<a?a:(v>b?b:v); }
static float lerpf(float a,float b,float t){ return a+(b-a)*t; }

/* Approximate, hand-authored Hole 13 geometry. This is intentionally original data, not copied map artwork. */
static float center_x(float z){
    float s=clampf(z/HOLE_YARDS,0,1);
    if(s<0.22f) return lerpf(0,-7,s/0.22f);
    if(s<0.52f) return lerpf(-7,-41,(s-0.22f)/0.30f);
    if(s<0.78f) return lerpf(-41,-67,(s-0.52f)/0.26f);
    return lerpf(-67,-61,(s-0.78f)/0.22f);
}
static float fair_half(float z){
    float s=clampf(z/HOLE_YARDS,0,1);
    if(s<0.10f) return 15;
    if(s<0.50f) return 21;
    if(s<0.78f) return 18;
    return 13;
}
static float terrain_h(float z,float x){
    float s=clampf(z/HOLE_YARDS,0,1);
    /* descent toward creek, then rise into green complex; ~55 ft total relief impression */
    float base = -2.0f*s - 7.0f*expf(-powf((s-0.71f)/0.17f,2.0f)) + 2.1f*expf(-powf((s-0.98f)/0.09f,2.0f));
    float cross = 0.015f*(x-center_x(z));
    float rolls = 0.55f*sinf(z*0.031f) + 0.30f*sinf(z*0.071f + x*0.04f);
    return base + cross + rolls;
}
static int in_creek(float z,float x){
    /* diagonal Rae's-Creek-like crossing/frontage near green, stylized */
    float cz = 414.0f + (x+55.0f)*0.34f;
    return fabsf(z-cz)<5.5f && x>-105 && x<-25;
}
static int in_bunker(float z,float x){
    float dx=x+48, dz=z-468;
    if((dx*dx)/(12*12)+(dz*dz)/(18*18)<1) return 1;
    dx=x+78; dz=z-487;
    if((dx*dx)/(10*10)+(dz*dz)/(13*13)<1) return 1;
    return 0;
}
static int on_green(float z,float x){
    float dx=x+61, dz=z-506;
    return (dx*dx)/(24*24)+(dz*dz)/(18*18)<1;
}
static Lie lie_at(float z,float x){
    if(in_creek(z,x)) return LIE_WATER;
    if(in_bunker(z,x)) return LIE_BUNKER;
    if(on_green(z,x)) return LIE_GREEN;
    if(z<18 && fabsf(x-center_x(z))<10) return LIE_TEE;
    if(fabsf(x-center_x(z))<fair_half(z)) return LIE_FAIRWAY;
    return LIE_ROUGH;
}

static GXColor grass_color(float z,float x){
    Lie l=lie_at(z,x);
    if(l==LIE_WATER) return (GXColor){48,112,155,255};
    if(l==LIE_BUNKER) return (GXColor){207,188,133,255};
    if(l==LIE_GREEN) return (GXColor){81,156,73,255};
    if(l==LIE_FAIRWAY){
        int stripe=((int)(z/12.0f))&1;
        return stripe ? (GXColor){61,132,55,255} : (GXColor){70,145,62,255};
    }
    int mottled=(((int)(z/18))^((int)((x+140)/18)))&1;
    return mottled ? (GXColor){32,91,39,255} : (GXColor){37,101,43,255};
}

static void gx_color(GXColor c){ GX_Color4u8(c.r,c.g,c.b,c.a); }
static void vtx(float x,float y,float z,GXColor c){ GX_Position3f32(x,y,z); gx_color(c); }

static void draw_world_quad(float x0,float z0,float x1,float z1,GXColor c){
    GX_Begin(GX_QUADS,GX_VTXFMT0,4);
    vtx(x0,terrain_h(z0,x0),z0,c); vtx(x1,terrain_h(z0,x1),z0,c);
    vtx(x1,terrain_h(z1,x1),z1,c); vtx(x0,terrain_h(z1,x0),z1,c);
    GX_End();
}
static void draw_terrain(void){
    const float xmin=-145, xmax=70, dz=10, dx=10;
    for(float z=0; z<545; z+=dz){
        for(float x=xmin; x<xmax; x+=dx){
            GXColor c=grass_color(z+dz*.5f,x+dx*.5f);
            GX_Begin(GX_QUADS,GX_VTXFMT0,4);
            vtx(x,terrain_h(z,x),z,c); vtx(x+dx,terrain_h(z,x+dx),z,c);
            vtx(x+dx,terrain_h(z+dz,x+dx),z+dz,c); vtx(x,terrain_h(z+dz,x),z+dz,c);
            GX_End();
        }
    }
}
static void draw_tree(float x,float z,float scale){
    float y=terrain_h(z,x);
    GXColor trunk={79,56,34,255}, dark={23,69,31,255}, mid={33,91,38,255};
    /* low-poly crossed tree, readable from moving camera */
    GX_Begin(GX_QUADS,GX_VTXFMT0,4);
    vtx(x-.8f,y,z,x<0?trunk:trunk); vtx(x+.8f,y,z,trunk); vtx(x+.8f,y+7*scale,z,trunk); vtx(x-.8f,y+7*scale,z,trunk);
    GX_End();
    float w=5.5f*scale,h=12*scale;
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);
    vtx(x-w,y+3,z,dark); vtx(x+w,y+3,z,dark); vtx(x,y+h,z,mid);
    vtx(x,y+3,z-w,dark); vtx(x,y+3,z+w,dark); vtx(x,y+h,z,mid);
    GX_End();
}
static void draw_trees(void){
    for(int i=0;i<38;i++){
        float z=22+i*13.2f;
        float c=center_x(z), w=fair_half(z);
        float wig=5.0f*sinf(i*1.7f);
        draw_tree(c-w-18-wig,z,0.85f+(i%4)*.08f);
        if(i%2==0) draw_tree(c+w+21+wig,z+5,0.9f+(i%3)*.09f);
    }
}
static void draw_flag(void){
    float x=-61,z=506,y=terrain_h(z,x);
    GXColor white={245,245,240,255}, flag={238,224,80,255};
    GX_Begin(GX_LINES,GX_VTXFMT0,2); vtx(x,y,z,white); vtx(x,y+6.8f,z,white); GX_End();
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3); vtx(x,y+6.8f,z,flag); vtx(x+5,y+5.8f,z,flag); vtx(x,y+4.9f,z,flag); GX_End();
}
static void draw_ball(const Game *g){
    GXColor white={250,250,246,255}; float r=.55f;
    /* tiny octahedron */
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,24);
    vtx(g->x,g->y+r,g->z,white);vtx(g->x-r,g->y,g->z,white);vtx(g->x,g->y,g->z+r,white);
    vtx(g->x,g->y+r,g->z,white);vtx(g->x,g->y,g->z+r,white);vtx(g->x+r,g->y,g->z,white);
    vtx(g->x,g->y+r,g->z,white);vtx(g->x+r,g->y,g->z,white);vtx(g->x,g->y,g->z-r,white);
    vtx(g->x,g->y+r,g->z,white);vtx(g->x,g->y,g->z-r,white);vtx(g->x-r,g->y,g->z,white);
    vtx(g->x,g->y-r,g->z,white);vtx(g->x,g->y,g->z+r,white);vtx(g->x-r,g->y,g->z,white);
    vtx(g->x,g->y-r,g->z,white);vtx(g->x+r,g->y,g->z,white);vtx(g->x,g->y,g->z+r,white);
    vtx(g->x,g->y-r,g->z,white);vtx(g->x,g->y,g->z-r,white);vtx(g->x+r,g->y,g->z,white);
    vtx(g->x,g->y-r,g->z,white);vtx(g->x-r,g->y,g->z,white);vtx(g->x,g->y,g->z-r,white);
    GX_End();
}
static void draw_golfer(const Game *g){
    if(g->moving) return;
    float x=g->x+4.0f, z=g->z-2.0f, y=terrain_h(z,x);
    GXColor pants={236,237,230,255}, shirt={42,102,62,255}, skin={205,164,119,255}, club={170,174,175,255};
    draw_world_quad(x-1,z-1,x+1,z+1,pants);
    GX_Begin(GX_QUADS,GX_VTXFMT0,4); vtx(x-1.4f,y+2,z,shirt);vtx(x+1.4f,y+2,z,shirt);vtx(x+1.1f,y+6,z,shirt);vtx(x-1.1f,y+6,z,shirt);GX_End();
    GX_Begin(GX_QUADS,GX_VTXFMT0,4);vtx(x-.8f,y+6,z,skin);vtx(x+.8f,y+6,z,skin);vtx(x+.7f,y+7.6f,z,skin);vtx(x-.7f,y+7.6f,z,skin);GX_End();
    GX_Begin(GX_LINES,GX_VTXFMT0,2);vtx(x+1,y+4.4f,z,club);vtx(g->x+.4f,g->y+.2f,g->z+.2f,club);GX_End();
}

static void set_camera(const Game *g, CameraMode mode){
    guVector eye, up={0,1,0}, target;
    if(mode==CAM_BALL){
        eye=(guVector){g->x+18.0f, g->y+14.0f, g->z-32.0f};
        target=(guVector){g->x,g->y+1.0f,g->z+32.0f};
    }else if(mode==CAM_PUTT){
        eye=(guVector){g->x+8.0f,g->y+5.0f,g->z-15.0f};
        target=(guVector){-61,terrain_h(506,-61)+.4f,506};
    }else{
        float sx=sinf(g->aim), cz=cosf(g->aim);
        eye=(guVector){g->x-10.0f*sx+5.0f,g->y+8.0f,g->z-16.0f*cz};
        target=(guVector){g->x+65.0f*sx,g->y+3.0f,g->z+65.0f*cz};
    }
    guLookAt(view,&eye,&up,&target);
    GX_LoadPosMtxImm(view,GX_PNMTX0);
}

static void init_video(void){
    VIDEO_Init(); WPAD_Init();
    rmode=VIDEO_GetPreferredMode(NULL);
    xfb[0]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode); VIDEO_SetNextFramebuffer(xfb[0]); VIDEO_SetBlack(FALSE); VIDEO_Flush(); VIDEO_WaitVSync();
    fifo=memalign(32,FIFO_SIZE); memset(fifo,0,FIFO_SIZE); GX_Init(fifo,FIFO_SIZE);
    GX_SetCopyClear((GXColor){111,174,211,255},0x00ffffff);
    GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);
    f32 ys=GX_GetYScaleFactor(rmode->efbHeight,rmode->xfbHeight); u16 xh=GX_SetDispCopyYScale(ys);
    GX_SetScissor(0,0,rmode->fbWidth,rmode->efbHeight); GX_SetDispCopySrc(0,0,rmode->fbWidth,rmode->efbHeight); GX_SetDispCopyDst(rmode->fbWidth,xh);
    GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,GX_TRUE,rmode->vfilter);
    GX_SetCullMode(GX_CULL_NONE); GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);
    GX_SetVtxDesc(GX_VA_POS,GX_DIRECT); GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0); GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GX_SetNumChans(1); GX_SetNumTexGens(0); GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR); GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    guPerspective(perspective,52.0f,4.0f/3.0f,0.5f,900.0f); GX_LoadProjectionMtx(perspective,GX_PERSPECTIVE);
    guOrtho(ortho,0,480,0,640,0,300);
    WPAD_SetDataFormat(WPAD_CHAN_0,WPAD_FMT_BTNS_ACC_IR);
}

static void reset(Game *g){ memset(g,0,sizeof(*g)); g->z=4; g->x=center_x(g->z); g->y=terrain_h(g->z,g->x)+.55f; g->club=0; g->lie=LIE_TEE; g->safe_z=g->z; g->safe_x=g->x; }
static float lie_power(Lie l){ return l==LIE_ROUGH?.87f:l==LIE_BUNKER?.62f:1.0f; }
static float friction(Lie l){ return l==LIE_GREEN?.9925f:l==LIE_FAIRWAY?.985f:l==LIE_BUNKER?.94f:.972f; }
static void launch(Game *g,float power,float face){
    float target=CLUBS[g->club].carry*power*lie_power(g->lie), dir=g->aim+DEG2RAD(face)*.65f;
    if(g->club==CLUB_COUNT-1 || g->lie==LIE_GREEN){ float sp=target*.52f; g->vz=cosf(dir)*sp; g->vx=sinf(dir)*sp; g->vy=0; g->flying=0; }
    else { float a=DEG2RAD(CLUBS[g->club].launch); float sp=sqrtf(target*GRAVITY/fmaxf(sinf(2*a),.18f))*1.05f; g->vz=cosf(dir)*sp*cosf(a); g->vx=sinf(dir)*sp*cosf(a); g->vy=sp*sinf(a); g->flying=1; }
    g->moving=1; g->strokes++;
}
static void update(Game *g){
    if(!g->moving) return; float dt=1.0f/60.0f;
    if(g->flying){
        g->z+=g->vz*dt; g->x+=g->vx*dt; g->y+=g->vy*dt; g->vy-=GRAVITY*dt; g->vz*=.999f; g->vx*=.999f;
        float ground=terrain_h(g->z,g->x)+.55f;
        if(g->y<=ground){ g->y=ground; g->lie=lie_at(g->z,g->x); if(fabsf(g->vy)>2.4f && g->lie!=LIE_BUNKER){g->vy=-g->vy*.23f;g->vz*=.76f;g->vx*=.76f;}else{g->flying=0;g->vy=0;} }
    }else{
        g->z+=g->vz*dt; g->x+=g->vx*dt; g->lie=lie_at(g->z,g->x); g->y=terrain_h(g->z,g->x)+.55f;
        float f=friction(g->lie); g->vz*=f; g->vx*=f;
        if(g->lie==LIE_GREEN){ g->vx += .012f*dt*60; g->vz -= .006f*dt*60; }
        if(hypotf(g->vz,g->vx)<.28f){g->vz=g->vx=0;g->moving=0;}
    }
    if(g->lie==LIE_WATER && !g->flying){ g->z=g->safe_z;g->x=g->safe_x;g->y=terrain_h(g->z,g->x)+.55f;g->moving=0;g->strokes++;g->lie=lie_at(g->z,g->x); }
    else if(!g->flying && g->lie!=LIE_WATER){g->safe_z=g->z;g->safe_x=g->x;}
    if(!g->moving && on_green(g->z,g->x) && hypotf(g->z-506,g->x+61)<1.2f){g->holed=1;}
}

/* Seven-segment HUD; no font or Nintendo assets required. */
static const unsigned char SEG[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
static void hud_quad(float x1,float y1,float x2,float y2,GXColor c){GX_Begin(GX_QUADS,GX_VTXFMT0,4);vtx(x1,y1,0,c);vtx(x2,y1,0,c);vtx(x2,y2,0,c);vtx(x1,y2,0,c);GX_End();}
static void digit(int d,float x,float y,float s,GXColor c){if(d<0||d>9)return;unsigned m=SEG[d];float t=2*s,w=8*s,h=14*s;if(m&1)hud_quad(x+t,y,x+w-t,y+t,c);if(m&2)hud_quad(x+w-t,y+t,x+w,y+h/2-t/2,c);if(m&4)hud_quad(x+w-t,y+h/2+t/2,x+w,y+h-t,c);if(m&8)hud_quad(x+t,y+h-t,x+w-t,y+h,c);if(m&16)hud_quad(x,y+h/2+t/2,x+t,y+h-t,c);if(m&32)hud_quad(x,y+t,x+t,y+h/2-t/2,c);if(m&64)hud_quad(x+t,y+h/2-t/2,x+w-t,y+h/2+t/2,c);}
static void number(int n,float x,float y,float s,GXColor c){if(n<0){hud_quad(x,y+6*s,x+6*s,y+8*s,c);x+=9*s;n=-n;}int a=n/100,b=(n/10)%10,d=n%10;if(a){digit(a,x,y,s,c);x+=10*s;}if(a||b){digit(b,x,y,s,c);x+=10*s;}digit(d,x,y,s,c);}
static void draw_hud(const Game*g,const Swing*sw){
    GX_LoadProjectionMtx(ortho,GX_ORTHOGRAPHIC); Mtx m; guMtxIdentity(m); GX_LoadPosMtxImm(m,GX_PNMTX0); GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
    GXColor panel={8,22,12,205},white={245,245,240,255},gold={240,220,80,255};
    hud_quad(0,0,640,58,panel); number(13,15,12,1.5f,gold); number(5,76,12,1.5f,white); number(510,125,12,1.5f,white); number(g->strokes,230,12,1.5f,white); number(g->club+1,285,12,1.5f,white); number((int)fmaxf(0,506-g->z),370,12,1.5f,white);
    if(sw->armed){float p=clampf((sw->peak_g-1.05f)/2.8f,0,1);hud_quad(90,438,550,469,panel);hud_quad(98,447,98+444*p,460,gold);}
    if(g->holed){hud_quad(220,185,420,295,panel);number(g->strokes,292,213,3.0f,gold);}
    GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE); GX_LoadProjectionMtx(perspective,GX_PERSPECTIVE);
}

static void render(const Game*g,const Swing*sw){
    CameraMode cm = g->moving?CAM_BALL:(g->lie==LIE_GREEN?CAM_PUTT:CAM_PLAYER);
    GX_LoadProjectionMtx(perspective,GX_PERSPECTIVE); GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE); set_camera(g,cm);
    draw_terrain(); draw_trees(); draw_flag(); draw_golfer(g); draw_ball(g); draw_hud(g,sw);
}

int main(int argc,char **argv){
    (void)argc;(void)argv; init_video(); Game g; Swing sw={0}; reset(&g);
    while(SYS_MainLoop()){
        WPAD_ScanPads(); u32 down=WPAD_ButtonsDown(0), held=WPAD_ButtonsHeld(0);
        if(down&WPAD_BUTTON_HOME) break;
        if(down&WPAD_BUTTON_PLUS) reset(&g);
        if(!g.moving && !g.holed){
            if(held&WPAD_BUTTON_LEFT) g.aim-=.015f;
            if(held&WPAD_BUTTON_RIGHT) g.aim+=.015f;
            if(down&WPAD_BUTTON_UP && g.club>0) g.club--;
            if(down&WPAD_BUTTON_DOWN && g.club<CLUB_COUNT-1) g.club++;
            if(g.lie==LIE_GREEN) g.club=CLUB_COUNT-1;
            if(down&WPAD_BUTTON_B){struct orient_t o;WPAD_Orientation(0,&o);sw.armed=1;sw.peak_g=0;sw.start_roll=o.roll;sw.frames=0;}
            if(sw.armed){struct gforce_t gf;struct orient_t o;WPAD_GForce(0,&gf);WPAD_Orientation(0,&o);float mag=sqrtf(gf.x*gf.x+gf.y*gf.y+gf.z*gf.z);if(mag>sw.peak_g)sw.peak_g=mag;sw.frames++;if(!(held&WPAD_BUTTON_B)&&sw.frames>3){float p=clampf((sw.peak_g-1.05f)/2.8f,.08f,1);float face=clampf((o.roll-sw.start_roll)*.25f,-14,14);launch(&g,p,face);sw.armed=0;WPAD_Rumble(0,1);}}
        }
        update(&g); if(g.moving) WPAD_Rumble(0,0);
        GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1); GX_InvVtxCache(); render(&g,&sw); GX_DrawDone();
        fb^=1; GX_CopyDisp(xfb[fb],GX_TRUE); VIDEO_SetNextFramebuffer(xfb[fb]); VIDEO_Flush(); VIDEO_WaitVSync();
    }
    return 0;
}
