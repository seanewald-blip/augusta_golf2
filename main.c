#include <gccore.h>
#include <malloc.h>
#include <wiiuse/wpad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "course.h"

#define FIFO_SIZE (256*1024)
#define PI 3.14159265358979323846f
#define SCREEN_W 640.0f
#define SCREEN_H 480.0f

typedef struct { const char *name; float carry; float launch_deg; float spin; } Club;
static const Club clubs[]={{"Driver",285,12,0.10f},{"3 Wood",240,14,0.12f},{"5 Iron",190,19,0.16f},{"7 Iron",160,23,0.19f},{"9 Iron",130,29,0.23f},{"Wedge",95,38,0.30f},{"Putter",36,0,0}};
#define CLUB_COUNT ((int)(sizeof(clubs)/sizeof(clubs[0])))

typedef struct {
 float along,lateral,z;
 float vx,vy,vz;
 float aim;
 float last_safe_along,last_safe_lateral;
 int flying,moving,strokes,club,hole,total_score;
 Lie lie;
} Game;

typedef struct { int armed; float peak_g; float start_roll; int frames; } Swing;

static void *xfb[2]; static GXRModeObj *rmode; static u32 fb; static Mtx44 proj; static void *fifo;
static float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}
static void draw_quad(float x1,float y1,float x2,float y2,GXColor c){GX_Begin(GX_QUADS,GX_VTXFMT0,4);GX_Position2f32(x1,y1);GX_Color4u8(c.r,c.g,c.b,c.a);GX_Position2f32(x2,y1);GX_Color4u8(c.r,c.g,c.b,c.a);GX_Position2f32(x2,y2);GX_Color4u8(c.r,c.g,c.b,c.a);GX_Position2f32(x1,y2);GX_Color4u8(c.r,c.g,c.b,c.a);GX_End();}
static void draw_line(float x1,float y1,float x2,float y2,GXColor c){GX_Begin(GX_LINES,GX_VTXFMT0,2);GX_Position2f32(x1,y1);GX_Color4u8(c.r,c.g,c.b,c.a);GX_Position2f32(x2,y2);GX_Color4u8(c.r,c.g,c.b,c.a);GX_End();}
static void draw_circle(float cx,float cy,float r,GXColor c){const int n=24;GX_Begin(GX_TRIANGLEFAN,GX_VTXFMT0,n+2);GX_Position2f32(cx,cy);GX_Color4u8(c.r,c.g,c.b,c.a);for(int i=0;i<=n;i++){float a=2*PI*i/n;GX_Position2f32(cx+cosf(a)*r,cy+sinf(a)*r);GX_Color4u8(c.r,c.g,c.b,c.a);}GX_End();}

/* Tiny seven-segment display: enough to show hole/strokes/score without bundling a font. */
static const unsigned char seg[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
static void draw_digit(int d,float x,float y,float s,GXColor c){if(d<0||d>9)return;unsigned m=seg[d];float t=2*s,w=8*s,h=14*s; if(m&1)draw_quad(x+t,y,x+w-t,y+t,c);if(m&2)draw_quad(x+w-t,y+t,x+w,y+h/2-t/2,c);if(m&4)draw_quad(x+w-t,y+h/2+t/2,x+w,y+h-t,c);if(m&8)draw_quad(x+t,y+h-t,x+w-t,y+h,c);if(m&16)draw_quad(x,y+h/2+t/2,x+t,y+h-t,c);if(m&32)draw_quad(x,y+t,x+t,y+h/2-t/2,c);if(m&64)draw_quad(x+t,y+h/2-t/2,x+w-t,y+h/2+t/2,c);}
static void draw_number(int n,float x,float y,float s,GXColor c){if(n<0){draw_quad(x,y+6*s,x+6*s,y+8*s,c);x+=9*s;n=-n;}int a=n/100,b=(n/10)%10,d=n%10;if(a){draw_digit(a,x,y,s,c);x+=10*s;}if(a||b){draw_digit(b,x,y,s,c);x+=10*s;}draw_digit(d,x,y,s,c);}

static void video_init(void){VIDEO_Init();WPAD_Init();rmode=VIDEO_GetPreferredMode(NULL);xfb[0]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));xfb[1]=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));VIDEO_Configure(rmode);VIDEO_SetNextFramebuffer(xfb[0]);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();fifo=memalign(32,FIFO_SIZE);memset(fifo,0,FIFO_SIZE);GX_Init(fifo,FIFO_SIZE);GX_SetCopyClear((GXColor){22,70,32,255},0x00ffffff);GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);f32 ys=GX_GetYScaleFactor(rmode->efbHeight,rmode->xfbHeight);u16 xh=GX_SetDispCopyYScale(ys);GX_SetScissor(0,0,rmode->fbWidth,rmode->efbHeight);GX_SetDispCopySrc(0,0,rmode->fbWidth,rmode->efbHeight);GX_SetDispCopyDst(rmode->fbWidth,xh);GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,GX_TRUE,rmode->vfilter);GX_SetCullMode(GX_CULL_NONE);GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XY,GX_F32,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);GX_SetNumChans(1);GX_SetNumTexGens(0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);guOrtho(proj,0,480,0,640,0,300);GX_LoadProjectionMtx(proj,GX_ORTHOGRAPHIC);Mtx mv;guMtxIdentity(mv);GX_LoadPosMtxImm(mv,GX_PNMTX0);WPAD_SetDataFormat(WPAD_CHAN_0,WPAD_FMT_BTNS_ACC_IR);}

static void reset_hole(Game*g,int h){int score=g->total_score;memset(g,0,sizeof(*g));g->hole=h;g->club=(g_holes[h].par==3)?3:0;g->aim=0;g->lie=LIE_TEE;g->total_score=score;}
static float friction_for(Lie l){switch(l){case LIE_GREEN:return 0.9925f;case LIE_FAIRWAY:return 0.984f;case LIE_ROUGH:return 0.970f;case LIE_BUNKER:return 0.935f;default:return 0.980f;}}
static float power_penalty(Lie l){switch(l){case LIE_ROUGH:return .88f;case LIE_BUNKER:return .62f;default:return 1.0f;}}

static void launch_shot(Game*g,float power,float face_deg){const HoleDef*h=&g_holes[g->hole];float lie_mul=power_penalty(g->lie);float target=clubs[g->club].carry*power*lie_mul;float face=face_deg*PI/180.0f;float dir=g->aim+face*0.65f; if(g->club==CLUB_COUNT-1||g->lie==LIE_GREEN){float speed=target*0.52f;g->vx=cosf(dir)*speed;g->vy=sinf(dir)*speed;g->vz=0;g->flying=0;}else{float a=clubs[g->club].launch_deg*PI/180.0f;float speed=sqrtf(target*10.72f/fmaxf(sinf(2.0f*a),0.18f))*1.05f;g->vx=cosf(dir)*speed*cosf(a);g->vy=sinf(dir)*speed*cosf(a);g->vz=speed*sinf(a);}g->z=course_height_yards(h,g->along/(float)h->yards,g->lateral)+0.03f;g->moving=1;g->flying=(g->vz>0.1f);g->strokes++;}

static int ball_holed(const Game*g){const HoleDef*h=&g_holes[g->hole];float cup_lat=course_center_lateral(h,1);return !g->moving && hypotf(h->yards-g->along,g->lateral-cup_lat)<1.25f;}
static void update_ball(Game*g){if(!g->moving)return;const HoleDef*h=&g_holes[g->hole];float dt=1.0f/60.0f; if(g->flying){g->along+=g->vx*dt;g->lateral+=g->vy*dt;g->z+=g->vz*dt;g->vz-=10.72f*dt;g->vx*=0.9990f;g->vy*=0.9990f;float ground=course_height_yards(h,g->along/(float)h->yards,g->lateral);if(g->z<=ground){g->z=ground;g->lie=course_lie(h,g->along,g->lateral);if(fabsf(g->vz)>2.4f && g->lie!=LIE_BUNKER){g->vz=-g->vz*0.24f;g->vx*=0.76f;g->vy*=0.76f;}else{g->flying=0;g->vz=0;}}}else{g->along+=g->vx*dt;g->lateral+=g->vy*dt;g->lie=course_lie(h,g->along,g->lateral);float s=g->along/(float)h->yards;float eps=0.002f;float dhds=(course_height_yards(h,clampf(s+eps,0,1),g->lateral)-course_height_yards(h,clampf(s-eps,0,1),g->lateral))/(2*eps*h->yards);float center=course_center_lateral(h,s);float cross=h->cross_slope+(g->lie==LIE_GREEN?h->green_slope_x:0);g->vx-=dhds*10.72f*dt;g->vy-=cross*8.5f*dt;float f=friction_for(g->lie);g->vx*=f;g->vy*=f; if(hypotf(g->vx,g->vy)<0.30f){g->vx=g->vy=0;g->moving=0;}}
 if(g->lie==LIE_WATER && !g->flying){g->along=g->last_safe_along;g->lateral=g->last_safe_lateral;g->z=course_height_yards(h,g->along/(float)h->yards,g->lateral);g->moving=0;g->strokes++;g->lie=course_lie(h,g->along,g->lateral);}else if(!g->flying && g->lie!=LIE_WATER){g->last_safe_along=g->along;g->last_safe_lateral=g->lateral;}
 if(g->along<0)g->along=0;if(g->along>h->yards+30)g->along=h->yards+30;}

static void course_to_screen(const HoleDef*h,float along,float lat,float *x,float*y){float scale=390.0f/h->yards;*x=105.0f+along*scale;*y=240.0f+lat*scale;}
static GXColor lie_color(Lie l){switch(l){case LIE_GREEN:return (GXColor){92,170,86,255};case LIE_BUNKER:return (GXColor){210,190,135,255};case LIE_WATER:return (GXColor){50,105,150,255};case LIE_ROUGH:return (GXColor){35,90,42,255};default:return (GXColor){55,130,60,255};}}
static void render(const Game*g,const Swing*sw){const HoleDef*h=&g_holes[g->hole];GXColor rough={28,82,37,255},fair={55,132,61,255},green={92,174,88,255},white={245,245,245,255},black={8,12,9,210},yellow={245,225,80,255};draw_quad(0,0,640,480,rough);
 /* fairway ribbon */ for(int i=0;i<96;i++){float s0=i/96.0f,s1=(i+1)/96.0f;float a0=s0*h->yards,a1=s1*h->yards,c0=course_center_lateral(h,s0),c1=course_center_lateral(h,s1),w0=course_half_width(h,s0),w1=course_half_width(h,s1);float x0,y00,y01,x1,y10,y11;course_to_screen(h,a0,c0-w0,&x0,&y00);course_to_screen(h,a0,c0+w0,&x0,&y01);course_to_screen(h,a1,c1-w1,&x1,&y10);course_to_screen(h,a1,c1+w1,&x1,&y11);GX_Begin(GX_QUADS,GX_VTXFMT0,4);GX_Position2f32(x0,y00);GX_Color4u8(fair.r,fair.g,fair.b,255);GX_Position2f32(x1,y10);GX_Color4u8(fair.r,fair.g,fair.b,255);GX_Position2f32(x1,y11);GX_Color4u8(fair.r,fair.g,fair.b,255);GX_Position2f32(x0,y01);GX_Color4u8(fair.r,fair.g,fair.b,255);GX_End();}
 for(int i=0;i<h->hazard_count;i++){Hazard z=h->hazards[i];float a0=z.s0*h->yards,a1=z.s1*h->yards,x0,y0,x1,y1;course_to_screen(h,a0,z.lateral-z.half_width,&x0,&y0);course_to_screen(h,a1,z.lateral+z.half_width,&x1,&y1);draw_quad(x0,y0,x1,y1,lie_color(z.type==HAZARD_WATER?LIE_WATER:LIE_BUNKER));}
 float gx,gy;course_to_screen(h,h->yards,course_center_lateral(h,1),&gx,&gy);draw_circle(gx,gy,h->green_radius*(390.0f/h->yards),green);draw_line(gx,gy,gx,gy-35,white);draw_quad(gx,gy-35,gx+13,gy-26,yellow);
 float bx,by;course_to_screen(h,g->along,g->lateral,&bx,&by);draw_circle(bx,by,4.2f,white);draw_line(bx,by,bx+cosf(g->aim)*48,by+sinf(g->aim)*48,yellow);
 draw_quad(0,0,640,54,black);draw_number(g->hole+1,12,10,1.4f,white);draw_number(h->yards,80,10,1.4f,white);draw_number(g->strokes,190,10,1.4f,white);draw_number(g->club+1,250,10,1.4f,white);int rem=(int)fmaxf(0,h->yards-g->along);draw_number(rem,330,10,1.4f,white);draw_number(g->total_score,475,10,1.4f,white);
 if(sw->armed){float p=clampf((sw->peak_g-1.05f)/2.8f,0,1);draw_quad(90,442,550,468,black);draw_quad(96,448,96+448*p,462,white);} if(ball_holed(g)){draw_quad(225,195,415,285,black);draw_circle(320,240,25,yellow);}
}

int main(int argc,char**argv){(void)argc;(void)argv;video_init();Game g;memset(&g,0,sizeof(g));reset_hole(&g,0);Swing sw={0};while(SYS_MainLoop()){WPAD_ScanPads();u32 down=WPAD_ButtonsDown(0),held=WPAD_ButtonsHeld(0);if(down&WPAD_BUTTON_HOME)break;if(!g.moving&&!ball_holed(&g)){if(held&WPAD_BUTTON_LEFT)g.aim-=0.018f;if(held&WPAD_BUTTON_RIGHT)g.aim+=0.018f;if(down&WPAD_BUTTON_UP&&g.club>0)g.club--;if(down&WPAD_BUTTON_DOWN&&g.club<CLUB_COUNT-1)g.club++;if(down&WPAD_BUTTON_B){struct orient_t o;WPAD_Orientation(0,&o);sw.armed=1;sw.peak_g=0;sw.start_roll=o.roll;sw.frames=0;}if(sw.armed){struct gforce_t gf;struct orient_t o;WPAD_GForce(0,&gf);WPAD_Orientation(0,&o);float mag=sqrtf(gf.x*gf.x+gf.y*gf.y+gf.z*gf.z);if(mag>sw.peak_g)sw.peak_g=mag;sw.frames++;if(!(held&WPAD_BUTTON_B)&&sw.frames>3){float p=clampf((sw.peak_g-1.05f)/2.8f,0.08f,1.0f);float face=clampf((o.roll-sw.start_roll)*0.25f,-14,14);launch_shot(&g,p,face);sw.armed=0;WPAD_Rumble(0,1);}}}else if(ball_holed(&g)&&down&WPAD_BUTTON_PLUS){g.total_score+=g.strokes-g_holes[g.hole].par;int nh=g.hole+1;if(nh>=HOLE_COUNT)nh=0;reset_hole(&g,nh);}update_ball(&g);if(g.moving)WPAD_Rumble(0,0);GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);GX_InvVtxCache();render(&g,&sw);GX_DrawDone();fb^=1;GX_CopyDisp(xfb[fb],GX_TRUE);VIDEO_SetNextFramebuffer(xfb[fb]);VIDEO_Flush();VIDEO_WaitVSync();}return 0;}
