#ifndef AUGUSTA_COURSE_H
#define AUGUSTA_COURSE_H

#define HOLE_COUNT 18
#define MAX_ROUTE_POINTS 6
#define MAX_HAZARDS 6

typedef enum { LIE_TEE, LIE_FAIRWAY, LIE_ROUGH, LIE_GREEN, LIE_BUNKER, LIE_WATER } Lie;
typedef enum { HAZARD_NONE, HAZARD_BUNKER, HAZARD_WATER } HazardType;

typedef struct { float s; float lateral; float width; } RoutePoint;
typedef struct { HazardType type; float s0, s1, lateral, half_width; } Hazard;
typedef struct {
    const char *name;
    int par;
    int yards;
    float elevation_change_ft;
    float cross_slope;
    int route_count;
    RoutePoint route[MAX_ROUTE_POINTS];
    int hazard_count;
    Hazard hazards[MAX_HAZARDS];
    float green_radius;
    float green_slope_x;
    float green_slope_y;
} HoleDef;

extern const HoleDef g_holes[HOLE_COUNT];
float course_center_lateral(const HoleDef *h, float s);
float course_half_width(const HoleDef *h, float s);
float course_height_yards(const HoleDef *h, float s, float lateral);
Lie course_lie(const HoleDef *h, float s, float lateral);
const char *lie_name(Lie lie);

#endif
