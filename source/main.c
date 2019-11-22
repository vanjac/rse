#include <gba.h>
#include "fixed.h"
#include "sinlut.h"
#include "tonc_bmp8.h"

//#define DEBUG_LINES

#define M4WIDTH 120
typedef u16 MODE4_LINE[M4WIDTH];
#define MODE4_FB ((MODE4_LINE *)0x06000000)

#define HORIZON 80

fixed camX = 0, camY = 0, camZ = 0;

// for each column
// rounded up to multiple of 32 so CpuFastSet can be used
s16 yClipMin[128];
s16 yClipMax[128];

typedef struct Sector {
    fixed zmin, zmax;
    const struct Wall * walls;
    int numWalls;
    int floorColor, ceilColor;
} Sector;

typedef struct Wall {
    fixed x1, y1; // x2 y2 defined by next wall
    int color;
    const struct Sector * portal;
} Wall;

extern const Sector sectors[2];
const Wall walls[9] = {
    // sector 0 walls
    { 4*FUNIT,  4*FUNIT, 0x0101, 0},
    { 0*FUNIT,  4*FUNIT, 0x0404, &sectors[1]},
    {-3*FUNIT,  2*FUNIT, 0x0505, 0},
    {-3*FUNIT, -4*FUNIT, 0x0404, 0},
    { 4*FUNIT, -4*FUNIT, 0x0606, 0},
    // sector 1 walls
    { 4*FUNIT,  4*FUNIT, 0x0101, &sectors[0]},
    { 4*FUNIT,  7*FUNIT, 0x0606, 0},
    { 0*FUNIT,  7*FUNIT, 0x0505, 0},
    { 0*FUNIT,  4*FUNIT, 0x0101, 0}
};

const Sector sectors[2] = {
    {-256, 512, &walls[0], 5, 0x0202, 0x0303},
    {-256, 256, &walls[5], 4, 0x0303, 0x0202}
};

void intersect(fixed x1, fixed y1, fixed x2, fixed y2,
               fixed x3, fixed y3, fixed x4, fixed y4,
               fixed * xint, fixed * yint);
static inline void rotatePoint(fixed x, fixed y, fixed sint, fixed cost,
    fixed * xout, fixed * yout);

void drawSector(Sector sector, fixed sint, fixed cost, int xClipMin, int xClipMax);
// looking down x axis
// points should be ordered left to right on screen
// return if on screen
static inline int clipFrustum(fixed * x1, fixed * y1, fixed * x2, fixed * y2);
static inline void projectXY(fixed x1, fixed y1, fixed x2, fixed y2,
    int * outScrX1, int * outScrX2);
static inline void projectZ(fixed x1, fixed x2, fixed z1, fixed z2,
    int * outScrYMin1, int * outScrYMax1,
    int * outScrYMin2, int * outScrYMax2);
void trapezoid(int x1, int ymin1, int ymax1,
               int x2, int ymin2, int ymax2,
               int wallColor, int floorColor, int ceilColor,
               int xClipMin, int xClipMax,
               int drawToYClipMin, int drawToYClipMax);

static inline fixed cross(fixed x1, fixed y1, fixed x2, fixed y2) {
    return FMULT(x1, y2) - FMULT(y1, x2);
}

IWRAM_CODE
__attribute__((target("arm")))
void intersect(fixed x1, fixed y1, fixed x2, fixed y2,
               fixed x3, fixed y3, fixed x4, fixed y4,
               fixed * xint, fixed * yint) {
    fixed x = cross(x1, y1, x2, y2);
    fixed y = cross(x3, y3, x4, y4);
    fixed det = cross(x1-x2, y1-y2, x3-x4, y3-y4);
    if (det == 0)
        det = 1;
    *xint = FDIV(cross(x, x1-x2, y, x3-x4), det);
    *yint = FDIV(cross(x, y1-y2, y, y3-y4), det);
}

IWRAM_CODE
__attribute__((target("arm")))
static inline void rotatePoint(fixed x, fixed y, fixed sint, fixed cost,
        fixed * xout, fixed * yout) {
    fixed newx = FMULT(x, cost) - FMULT(y, sint);
    fixed newy = FMULT(y, cost) + FMULT(x, sint);
    *xout = newx;
    *yout = newy;
}

int main(void) {
	irqInit();
	irqEnable(IRQ_VBLANK);
	REG_IME = 1;

	REG_DISPCNT = MODE_4 | BG2_ON;

    ((vu16*)BG_COLORS)[1] = RGB5(31, 0, 14);
    ((vu16*)BG_COLORS)[2] = RGB5(0, 14, 14);
    ((vu16*)BG_COLORS)[3] = RGB5(0, 17, 0);
    ((vu16*)BG_COLORS)[4] = RGB5(31, 31, 14);
    ((vu16*)BG_COLORS)[5] = RGB5(0, 0, 31);
    ((vu16*)BG_COLORS)[6] = RGB5(31, 15, 31);
    ((vu16*)BG_COLORS)[7] = RGB5(31, 31, 31);
    ((vu16*)BG_COLORS)[8] = RGB5(10, 10, 10);

    int theta = 0;

    while (1) {
        const int zero = 0;
        const int yMaxFill = SCREEN_HEIGHT | (SCREEN_HEIGHT << 16);
        // clear ymin/max buffers
        CpuFastSet(&zero, yClipMin, 64 | (1<<24));
        CpuFastSet(&yMaxFill, yClipMax, 64 | (1<<24));
#ifdef DEBUG_LINES
        CpuFastSet(&zero, (void*)VRAM, 9600 | (1<<24));
#endif

        fixed sint = lu_sin(theta) >> 4;
        fixed cost = lu_cos(theta) >> 4;

        drawSector(sectors[0], sint, cost, 0, M4WIDTH);

#ifdef DEBUG_LINES
        bmp8_line(40, 160, 200, 0, 7, (void*)MODE4_FB, 240);
        bmp8_line(40, 0, 200, 160, 7, (void*)MODE4_FB, 240);
#endif

        VBlankIntrWait();

        int buttons = ~REG_KEYINPUT;
        if (buttons & KEY_L)
            theta += 128;
        if (buttons & KEY_R)
            theta -= 128;
        if (buttons & KEY_UP) {
            camX += cost / 16;
            camY += sint / 16;
        }
        if (buttons & KEY_DOWN) {
            camX -= cost / 16;
            camY -= sint / 16;
        }
        if (buttons & KEY_RIGHT) {
            camX += sint / 16;
            camY -= cost / 16;
        }
        if (buttons & KEY_LEFT) {
            camX -= sint / 16;
            camY += cost / 16;
        }
        if (buttons & KEY_A) {
            camZ += 16;
        }
        if (buttons & KEY_B) {
            camZ -= 16;
        }
    }
}

IWRAM_CODE
__attribute__((target("arm")))
void drawSector(Sector sector, fixed sint, fixed cost, int xClipMin, int xClipMax) {
    // transformed vertices
    fixed tX, tY, prevTX, prevTY;
    rotatePoint(sector.walls[sector.numWalls-1].x1 - camX,
                sector.walls[sector.numWalls-1].y1 - camY,
                -sint, cost, &prevTX, &prevTY);
    for (int i = 0; i < sector.numWalls; i++, prevTX=tX, prevTY=tY) {
        const Wall * wall = sector.walls + i;
        rotatePoint(wall->x1 - camX, wall->y1 - camY, -sint, cost, &tX, &tY);

        fixed x1 = tX, y1 = tY, x2 = prevTX, y2 = prevTY;
        if (!clipFrustum(&x1, &y1, &x2, &y2))
            continue;
#ifdef DEBUG_LINES
        continue;
#endif
        fixed scrX1, scrX2;
        projectXY(x1, y1, x2, y2, &scrX1, &scrX2);
        if (scrX2 <= scrX1 || scrX2 < xClipMin*FUNIT || scrX1 >= xClipMax*FUNIT)
            continue;
        fixed scrYMin1, scrYMax1, scrYMin2, scrYMax2;
        projectZ(x1, x2, sector.zmin-camZ, sector.zmax-camZ,
                 &scrYMin1, &scrYMax1, &scrYMin2, &scrYMax2);

        const Sector * portalSector = wall->portal;
        if (portalSector) {
            fixed portalScrYMin1, portalScrYMax1, portalScrYMin2, portalScrYMax2;
            projectZ(x1, x2, portalSector->zmin-camZ, portalSector->zmax-camZ,
                    &portalScrYMin1, &portalScrYMax1, &portalScrYMin2, &portalScrYMax2);

            if (portalSector->zmax < sector.zmax)
            // top wall
            trapezoid(scrX1, scrYMin1, portalScrYMin1, scrX2, scrYMin2, portalScrYMin2,
                wall->color, 0, sector.ceilColor,
                    xClipMin, xClipMax, 1, 0);
            else
                // ceiling only
                trapezoid(scrX1, scrYMin1, scrYMin1, scrX2, scrYMin2, scrYMin2,
                    0, 0, sector.ceilColor,
                    xClipMin, xClipMax, 1, 0);
            if (portalSector->zmin > sector.zmin)
            // bottom wall
            trapezoid(scrX1, portalScrYMax1, scrYMax1, scrX2, portalScrYMax2, scrYMax2,
                wall->color, sector.floorColor, 0,
                xClipMin, xClipMax, 0, 1);
            else
                // floor only
                trapezoid(scrX1, scrYMax1, scrYMax1, scrX2, scrYMax2, scrYMax2,
                    wall->color, sector.floorColor, 0,
                    xClipMin, xClipMax, 0, 1);

            int newXMin = xClipMin, newXMax = xClipMax;
            int xstart = scrX1 / FUNIT;
            int xend = scrX2 / FUNIT;
            if (xstart > newXMin)
                newXMin = xstart;
            if (xend < newXMax)
                newXMax = xend;
            drawSector(*portalSector, sint, cost, newXMin, newXMax);
        } else {
            trapezoid(scrX1, scrYMin1, scrYMax1, scrX2, scrYMin2, scrYMax2,
                wall->color, sector.floorColor, sector.ceilColor,
                xClipMin, xClipMax, 0, 0);
        }
    }
}

IWRAM_CODE
__attribute__((target("arm")))
static inline int clipFrustum(fixed * x1, fixed * y1, fixed * x2, fixed * y2) {
#ifdef DEBUG_LINES
    bmp8_line(*x1/32 + 120, -*y1/32 + 80, *x2/32 + 120, -*y2/32 + 80,
              8, (void*)MODE4_FB, 240);
#endif
    // clip points using a 90 degree frustum, defined by two lines (a and b)
    // x - y > 0 && x + y > 0

    int p1OutsideA = *x1 - *y1 < 0;
    int p2OutsideA = *x2 - *y2 < 0;
    int p1OutsideB = *x1 + *y1 < 0;
    int p2OutsideB = *x2 + *y2 < 0;

    // both points are outside frustum on same side
    if ((p1OutsideA && p2OutsideA) || (p1OutsideB && p2OutsideB)
        || (p2OutsideA && p1OutsideB)) // also this (points are rtl)
        return 0;

    if (p1OutsideA)
        intersect(*x1, *y1, *x2, *y2, 0, 0, FUNIT,  FUNIT, x1, y1);
    if (p2OutsideA)
        intersect(*x1, *y1, *x2, *y2, 0, 0, FUNIT,  FUNIT, x2, y2);
    // outside B (check again, could have changed with previous clip)
    if (*x1 + *y1 < 0)
        intersect(*x1, *y1, *x2, *y2, 0, 0, FUNIT, -FUNIT, x1, y1);
    if (*x2 + *y2 < 0)
        intersect(*x1, *y1, *x2, *y2, 0, 0, FUNIT, -FUNIT, x2, y2);

#ifdef DEBUG_LINES
    bmp8_line(*x1/32 + 120, -*y1/32 + 80, *x2/32 + 120, -*y2/32 + 80,
              7, (void*)MODE4_FB, 240);
#endif
    // prevent future divide by zero with projection
    if (*x1 == 0)
        *x1 = 1;
    if (*x2 == 0)
        *x2 = 1;
    return 1;
}

IWRAM_CODE
__attribute__((target("arm")))
static inline void projectXY(fixed x1, fixed y1, fixed x2, fixed y2,
        int * outScrX1, int * outScrX2) {
    *outScrX1 = M4WIDTH/2*FUNIT - FDIV(y1*FUNIT, x1)/4;
    *outScrX2 = M4WIDTH/2*FUNIT - FDIV(y2*FUNIT, x2)/4;
}

IWRAM_CODE
__attribute__((target("arm")))
static inline void projectZ(fixed x1, fixed x2, fixed z1, fixed z2,
        int * outScrYMin1, int * outScrYMax1,
        int * outScrYMin2, int * outScrYMax2){
    *outScrYMin1 = HORIZON*FUNIT - FDIV(z2*FUNIT, x1)/2;
    *outScrYMax1 = HORIZON*FUNIT - FDIV(z1*FUNIT, x1)/2;
    *outScrYMin2 = HORIZON*FUNIT - FDIV(z2*FUNIT, x2)/2;
    *outScrYMax2 = HORIZON*FUNIT - FDIV(z1*FUNIT, x2)/2;
}

IWRAM_CODE
__attribute__((target("arm")))
// fixed point coordinates in screen space
void trapezoid(fixed x1, fixed ymin1, fixed ymax1,
               fixed x2, fixed ymin2, fixed ymax2,
               int wallColor, int floorColor, int ceilColor,
               int xClipMin, int xClipMax,
               int drawToYClipMin, int drawToYClipMax) {
    fixed xDist = x2 - x1;
    fixed minSlope = FDIV(ymin2 - ymin1, xDist);
    fixed maxSlope = FDIV(ymax2 - ymax1, xDist);

    int xstart = x1 / FUNIT;
    int xend = x2 / FUNIT;

    if (xstart < xClipMin)
        xstart = xClipMin;
    if (xend > xClipMax)
        xend = xClipMax;

    fixed min = ymin1 + FMULT(xstart*FUNIT - x1, minSlope);
    fixed max = ymax1 + FMULT(xstart*FUNIT - x1, maxSlope);

        for (int x = xstart; x < xend; x++) {
            int min_i = min / FUNIT;
            if (min_i < yClipMin[x])
                min_i = yClipMin[x];
            int max_i = max / FUNIT;
            if (max_i > yClipMax[x])
                max_i = yClipMax[x];
            int y = yClipMin[x];
            if (ceilColor)
                for (; y < min_i; y++)
                    MODE4_FB[y][x] = ceilColor;
            else
                y = min_i;
            for (; y < max_i; y++)
                MODE4_FB[y][x] = wallColor;
            if (floorColor)
                for (; y < yClipMax[x]; y++)
                    MODE4_FB[y][x] = floorColor;
        if (drawToYClipMin && max_i > yClipMin[x])
            yClipMin[x] = max_i;
        if (drawToYClipMax && min_i < yClipMax[x])
            yClipMax[x] = min_i;
            min += minSlope;
            max += maxSlope;
        }
    }