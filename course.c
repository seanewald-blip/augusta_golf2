#include "course.h"
#include <math.h>

const HoleDef g_holes[HOLE_COUNT] = {
  {"Tea Olive", 4, 445, 28.0f, 0.0100f, 4, {{0.000f, 0.00f, 28.00f}, {0.300f, 5.00f, 32.00f}, {0.620f, 22.00f, 31.00f}, {1.000f, 0.00f, 25.00f}, {0,0,0}, {0,0,0}}, 2, {{HAZARD_BUNKER, 0.630f, 0.730f, 18.00f, 12.00f}, {HAZARD_BUNKER, 0.920f, 0.990f, -20.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, -0.0120f, 0.0120f},
  {"Pink Dogwood", 5, 585, -48.0f, -0.0060f, 5, {{0.000f, 0.00f, 30.00f}, {0.250f, -12.00f, 38.00f}, {0.550f, -26.00f, 42.00f}, {0.780f, -8.00f, 34.00f}, {1.000f, 0.00f, 27.00f}, {0,0,0}}, 2, {{HAZARD_BUNKER, 0.820f, 0.900f, 25.00f, 10.00f}, {HAZARD_BUNKER, 0.910f, 0.990f, -18.00f, 8.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0000f, -0.0120f},
  {"Flowering Peach", 4, 350, 6.0f, 0.0040f, 4, {{0.000f, 0.00f, 28.00f}, {0.320f, 8.00f, 34.00f}, {0.700f, -7.00f, 31.00f}, {1.000f, 0.00f, 24.00f}, {0,0,0}, {0,0,0}}, 2, {{HAZARD_BUNKER, 0.680f, 0.800f, -24.00f, 11.00f}, {HAZARD_BUNKER, 0.900f, 0.990f, 19.00f, 8.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0000f, 0.0090f},
  {"Flowering Crab Apple", 3, 240, 18.0f, -0.0060f, 3, {{0.000f, 0.00f, 22.00f}, {0.550f, 0.00f, 25.00f}, {1.000f, 0.00f, 23.00f}, {0,0,0}, {0,0,0}, {0,0,0}}, 2, {{HAZARD_BUNKER, 0.830f, 0.980f, -22.00f, 11.00f}, {HAZARD_BUNKER, 0.830f, 0.980f, 23.00f, 10.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, -0.0030f, -0.0030f},
  {"Magnolia", 4, 495, 34.0f, 0.0040f, 5, {{0.000f, 0.00f, 27.00f}, {0.350f, -7.00f, 31.00f}, {0.620f, -18.00f, 29.00f}, {0.820f, -9.00f, 27.00f}, {1.000f, 0.00f, 23.00f}, {0,0,0}}, 2, {{HAZARD_BUNKER, 0.340f, 0.430f, -24.00f, 11.00f}, {HAZARD_BUNKER, 0.860f, 0.980f, 20.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0030f, -0.0120f},
  {"Juniper", 3, 180, -28.0f, 0.0100f, 3, {{0.000f, 0.00f, 20.00f}, {0.500f, 0.00f, 23.00f}, {1.000f, 0.00f, 22.00f}, {0,0,0}, {0,0,0}, {0,0,0}}, 1, {{HAZARD_BUNKER, 0.780f, 0.980f, -20.00f, 10.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0060f, -0.0120f},
  {"Pampas", 4, 450, 24.0f, -0.0040f, 4, {{0.000f, 0.00f, 26.00f}, {0.350f, -10.00f, 31.00f}, {0.650f, 5.00f, 28.00f}, {1.000f, 0.00f, 22.00f}, {0,0,0}, {0,0,0}}, 2, {{HAZARD_BUNKER, 0.800f, 0.960f, -20.00f, 9.00f}, {HAZARD_BUNKER, 0.800f, 0.960f, 20.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0030f, -0.0120f},
  {"Yellow Jasmine", 5, 570, 42.0f, 0.0060f, 5, {{0.000f, 0.00f, 29.00f}, {0.280f, 8.00f, 37.00f}, {0.580f, 17.00f, 34.00f}, {0.780f, 6.00f, 31.00f}, {1.000f, 0.00f, 25.00f}, {0,0,0}}, 1, {{HAZARD_BUNKER, 0.830f, 0.940f, -20.00f, 10.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0090f, 0.0060f},
  {"Carolina Cherry", 4, 460, -38.0f, -0.0100f, 5, {{0.000f, 0.00f, 29.00f}, {0.320f, 4.00f, 32.00f}, {0.630f, 18.00f, 30.00f}, {0.830f, 12.00f, 26.00f}, {1.000f, 0.00f, 22.00f}, {0,0,0}}, 1, {{HAZARD_BUNKER, 0.880f, 0.980f, -19.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0060f, -0.0060f},
  {"Camellia", 4, 495, -72.0f, 0.0100f, 5, {{0.000f, 0.00f, 28.00f}, {0.280f, -6.00f, 32.00f}, {0.550f, -24.00f, 34.00f}, {0.780f, -35.00f, 29.00f}, {1.000f, 0.00f, 23.00f}, {0,0,0}}, 1, {{HAZARD_BUNKER, 0.820f, 0.940f, 20.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0030f, -0.0120f},
  {"White Dogwood", 4, 520, -36.0f, -0.0080f, 5, {{0.000f, 0.00f, 27.00f}, {0.280f, 7.00f, 31.00f}, {0.550f, -5.00f, 29.00f}, {0.780f, -18.00f, 25.00f}, {1.000f, 0.00f, 21.00f}, {0,0,0}}, 2, {{HAZARD_WATER, 0.780f, 0.980f, 23.00f, 14.00f}, {HAZARD_BUNKER, 0.830f, 0.980f, -19.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, -0.0030f, 0.0030f},
  {"Golden Bell", 3, 155, -8.0f, 0.0120f, 3, {{0.000f, 0.00f, 19.00f}, {0.500f, 0.00f, 20.00f}, {1.000f, 0.00f, 18.00f}, {0,0,0}, {0,0,0}, {0,0,0}}, 2, {{HAZARD_WATER, 0.620f, 0.900f, 0.00f, 38.00f}, {HAZARD_BUNKER, 0.830f, 0.980f, -22.00f, 8.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0120f, 0.0000f},
  {"Azalea", 5, 545, -32.0f, -0.0100f, 5, {{0.000f, 0.00f, 29.00f}, {0.220f, -8.00f, 34.00f}, {0.480f, -24.00f, 35.00f}, {0.720f, -44.00f, 30.00f}, {1.000f, -50.00f, 24.00f}, {0,0,0}}, 2, {{HAZARD_WATER, 0.640f, 0.910f, -15.00f, 40.00f}, {HAZARD_BUNKER, 0.880f, 0.980f, -70.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, -0.0120f, -0.0090f},
  {"Chinese Fir", 4, 440, 9.0f, 0.0080f, 4, {{0.000f, 0.00f, 28.00f}, {0.350f, 6.00f, 31.00f}, {0.680f, 4.00f, 28.00f}, {1.000f, 0.00f, 23.00f}, {0,0,0}, {0,0,0}}, 0, {{HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0000f, 0.0090f},
  {"Firethorn", 5, 550, -18.0f, -0.0060f, 5, {{0.000f, 0.00f, 29.00f}, {0.270f, -6.00f, 35.00f}, {0.550f, -4.00f, 36.00f}, {0.780f, 8.00f, 31.00f}, {1.000f, 0.00f, 25.00f}, {0,0,0}}, 2, {{HAZARD_WATER, 0.720f, 0.930f, 25.00f, 34.00f}, {HAZARD_BUNKER, 0.880f, 0.980f, -19.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0030f, -0.0060f},
  {"Redbud", 3, 170, -12.0f, 0.0140f, 3, {{0.000f, 0.00f, 20.00f}, {0.520f, 0.00f, 21.00f}, {1.000f, 0.00f, 19.00f}, {0,0,0}, {0,0,0}, {0,0,0}}, 2, {{HAZARD_WATER, 0.580f, 0.920f, -10.00f, 40.00f}, {HAZARD_BUNKER, 0.780f, 0.970f, 21.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0030f, 0.0090f},
  {"Nandina", 4, 450, 14.0f, -0.0050f, 4, {{0.000f, 0.00f, 28.00f}, {0.330f, 7.00f, 31.00f}, {0.650f, -3.00f, 29.00f}, {1.000f, 0.00f, 23.00f}, {0,0,0}, {0,0,0}}, 2, {{HAZARD_BUNKER, 0.860f, 0.980f, -18.00f, 9.00f}, {HAZARD_BUNKER, 0.860f, 0.980f, 18.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, 0.0000f, 0.0090f},
  {"Holly", 4, 465, 45.0f, 0.0070f, 5, {{0.000f, 0.00f, 27.00f}, {0.320f, 4.00f, 29.00f}, {0.580f, 20.00f, 27.00f}, {0.780f, 30.00f, 26.00f}, {1.000f, 0.00f, 23.00f}, {0,0,0}}, 4, {{HAZARD_BUNKER, 0.500f, 0.630f, -22.00f, 11.00f}, {HAZARD_BUNKER, 0.500f, 0.630f, 20.00f, 10.00f}, {HAZARD_BUNKER, 0.880f, 0.980f, -20.00f, 9.00f}, {HAZARD_BUNKER, 0.880f, 0.980f, 20.00f, 9.00f}, {HAZARD_NONE,0,0,0,0}, {HAZARD_NONE,0,0,0,0}}, 24.0f, -0.0060f, -0.0030f},
};


static float lerp(float a,float b,float t){return a+(b-a)*t;}
float course_center_lateral(const HoleDef *h, float s){
    if(s<=0) return h->route[0].lateral; if(s>=1) return h->route[h->route_count-1].lateral;
    for(int i=0;i<h->route_count-1;i++){RoutePoint a=h->route[i],b=h->route[i+1]; if(s>=a.s && s<=b.s){float t=(s-a.s)/(b.s-a.s);return lerp(a.lateral,b.lateral,t);}}
    return 0;
}
float course_half_width(const HoleDef *h, float s){
    if(s<=0) return h->route[0].width; if(s>=1) return h->route[h->route_count-1].width;
    for(int i=0;i<h->route_count-1;i++){RoutePoint a=h->route[i],b=h->route[i+1]; if(s>=a.s && s<=b.s){float t=(s-a.s)/(b.s-a.s);return lerp(a.width,b.width,t);}}
    return 25;
}
float course_height_yards(const HoleDef *h, float s, float lateral){
    if(s<0)s=0;if(s>1)s=1;
    /* Smooth macro-profile plus cross slope. If data/terrain.bin is installed, main.c can override this sampler. */
    float rise_yards=(h->elevation_change_ft/3.0f)*(s*s*(3.0f-2.0f*s));
    float center=course_center_lateral(h,s);
    float undulation=1.1f*sinf(s*9.4247779f + h->yards*0.01f) + 0.45f*sinf(s*21.991f + h->par);
    float cross=(lateral-center)*h->cross_slope;
    return rise_yards+undulation+cross;
}
Lie course_lie(const HoleDef *h, float along_yards, float lateral){
    float s=along_yards/(float)h->yards; if(s<0)s=0;if(s>1)s=1;
    float center=course_center_lateral(h,s), d=fabsf(lateral-center);
    for(int i=0;i<h->hazard_count;i++){Hazard z=h->hazards[i]; if(s>=z.s0 && s<=z.s1 && fabsf(lateral-z.lateral)<=z.half_width) return z.type==HAZARD_WATER?LIE_WATER:LIE_BUNKER;}
    float rem=h->yards-along_yards; float cup_lat=course_center_lateral(h,1.0f);
    if(rem<45.0f && hypotf(lateral-cup_lat, rem)<h->green_radius) return LIE_GREEN;
    if(along_yards<12.0f) return LIE_TEE;
    if(d<=course_half_width(h,s)) return LIE_FAIRWAY;
    return LIE_ROUGH;
}
const char *lie_name(Lie lie){static const char*n[]={"TEE","FAIRWAY","ROUGH","GREEN","BUNKER","WATER"};return n[(int)lie];}
