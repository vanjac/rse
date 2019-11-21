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
    { 4*FUNIT,  4*FUNIT, 0x0101, &sectors[1]},
    { 0*FUNIT,  4*FUNIT, 0x0404, 0},
    {-3*FUNIT,  2*FUNIT, 0x0505, 0},
    {-3*FUNIT, -4*FUNIT, 0x0404, 0},
    { 4*FUNIT, -4*FUNIT, 0x0606, 0},
    // sector 1 walls
    { 4*FUNIT,  4*FUNIT, 0x0101, 0},
    { 4*FUNIT,  7*FUNIT, 0x0606, 0},
    { 0*FUNIT,  7*FUNIT, 0x0505, 0},
    { 0*FUNIT,  4*FUNIT, 0x0101, &sectors[0]}
};

const Sector sectors[2] = {
    {-256, 512, &walls[0], 5, 0x0202, 0x0303},
    {-256, 256, &walls[5], 4, 0x0303, 0x0202}
};

void intersect(fixed x1, fixed y1, fixed x2, fixed y2,
               fixed x3, fixed y3, fixed x4, fixed y4,
               fixed * xint, fixed * yint);
void rotatePoint(fixed x, fixed y, fixed sint, fixed cost,
                 fixed * xout, fixed * yout);

void drawSector(Sector sector, fixed sint, fixed cost, int xClipMin, int xClipMax);
// return if drawn
int drawWall(fixed x1, fixed y1, fixed z1,
              fixed x2, fixed y2, fixed z2,
              int wallColor, int floorColor, int ceilColor,
              int * xClipMin, int * xClipMax, int drawToClip);
int trapezoid(int x1, int ymin1, int ymax1,
               int x2, int ymin2, int ymax2,
               int wallColor, int floorColor, int ceilColor,
               int * xClipMin, int * xClipMax, int drawToClip);

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
void rotatePoint(fixed x, fixed y, fixed sint, fixed cost,
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
    fixed tX, tY;
    rotatePoint(sector.walls[0].x1 - camX, sector.walls[0].y1 - camY,
        -sint, cost, &tX, &tY);
    for (int i = 0; i < sector.numWalls; i++) {
        Wall nextWall = sector.walls[(i + 1) % sector.numWalls];
        fixed nextX, nextY;
        rotatePoint(nextWall.x1 - camX, nextWall.y1 - camY,
            -sint, cost, &nextX, &nextY);
        if (drawWall(nextX, nextY, sector.zmin - camZ,
            tX, tY, sector.zmax - camZ,
            sector.walls[i].color, sector.floorColor, sector.ceilColor,
            &xClipMin, &xClipMax, 0)) {
            const Sector * portalSector = sector.walls[i].portal;
            if (portalSector) {
                int newXMin = xClipMin, newXMax = xClipMax;
                drawWall(nextX, nextY, portalSector->zmin - camZ,
                        tX, tY, portalSector->zmax - camZ,
                        0, 0, 0,
                        &newXMin, &newXMax, 1);
                    drawSector(*portalSector, sint, cost, newXMin, newXMax);
            }
        }
        tX = nextX;
        tY = nextY;
    }
}

// looking down x axis
// points should be ordered left to right on screen
IWRAM_CODE
__attribute__((target("arm")))
int drawWall(fixed x1, fixed y1, fixed z1,
              fixed x2, fixed y2, fixed z2,
              int wallColor, int floorColor, int ceilColor,
              int * xClipMin, int * xClipMax, int drawToClip) {
#ifdef DEBUG_LINES
    bmp8_line(x1/32 + 120, -y1/32 + 80, x2/32 + 120, -y2/32 + 80,
              8, (void*)MODE4_FB, 240);
#endif

    // clip points using a 90 degree frustum, defined by two lines (a and b)
    // x - y > 0 && x + y > 0

    int p1OutsideA = x1 - y1 < 0;
    int p2OutsideA = x2 - y2 < 0;
    int p1OutsideB = x1 + y1 < 0;
    int p2OutsideB = x2 + y2 < 0;

    // both points are outside frustum on same side
    if ((p1OutsideA && p2OutsideA) || (p1OutsideB && p2OutsideB)
        || (p2OutsideA && p1OutsideB)) // also this (points are rtl)
        return 0;

    if (p1OutsideA)
        intersect(x1, y1, x2, y2, 0, 0, FUNIT,  FUNIT, &x1, &y1);
    if (p2OutsideA)
        intersect(x1, y1, x2, y2, 0, 0, FUNIT,  FUNIT, &x2, &y2);
    // outside B (check again, could have changed with previous clip)
    if (x1 + y1 < 0)
        intersect(x1, y1, x2, y2, 0, 0, FUNIT, -FUNIT, &x1, &y1);
    if (x2 + y2 < 0)
        intersect(x1, y1, x2, y2, 0, 0, FUNIT, -FUNIT, &x2, &y2);

    if (x1 == 0)
        x1 = 1;
    if (x2 == 0)
        x2 = 1;

#ifdef DEBUG_LINES
    bmp8_line(x1/32 + 120, -y1/32 + 80, x2/32 + 120, -y2/32 + 80,
              color&0xff, (void*)MODE4_FB, 240);
#else
    return trapezoid(
        M4WIDTH/2*FUNIT - FDIV(y1*FUNIT, x1)/4,
        HORIZON*FUNIT - FDIV(z2*FUNIT, x1)/2,
        HORIZON*FUNIT - FDIV(z1*FUNIT, x1)/2,
        M4WIDTH/2*FUNIT - FDIV(y2*FUNIT, x2)/4,
        HORIZON*FUNIT - FDIV(z2*FUNIT, x2)/2,
        HORIZON*FUNIT - FDIV(z1*FUNIT, x2)/2,
        wallColor, floorColor, ceilColor,
        xClipMin, xClipMax, drawToClip);
#endif
}

IWRAM_CODE
__attribute__((target("arm")))
// fixed point coordinates in screen space
int trapezoid(fixed x1, fixed ymin1, fixed ymax1,
               fixed x2, fixed ymin2, fixed ymax2,
               int wallColor, int floorColor, int ceilColor,
               int * xClipMin, int * xClipMax, int drawToClip) {
    if (x2 <= x1 || x2 < (*xClipMin)*FUNIT || x1 >= (*xClipMax)*FUNIT)
        return 0;
    fixed xDist = x2 - x1;
    fixed minSlope = FDIV(ymin2 - ymin1, xDist);
    fixed maxSlope = FDIV(ymax2 - ymax1, xDist);

    int xstart = x1 / FUNIT;
    int xend = x2 / FUNIT;

    if (xstart < *xClipMin)
        xstart = *xClipMin;
    else if (drawToClip)
        *xClipMin = xstart;
    if (xend > *xClipMax)
        xend = *xClipMax;
    else if (drawToClip)
        *xClipMax = xend;

    fixed min = ymin1 + FMULT(xstart*FUNIT - x1, minSlope);
    fixed max = ymax1 + FMULT(xstart*FUNIT - x1, maxSlope);

    if (drawToClip) {
        for (int x = xstart; x < xend; x++) {
            int min_i = min / FUNIT;
            if (min_i > yClipMin[x])
                yClipMin[x] = min_i;
            int max_i = max / FUNIT;
            if (max_i < yClipMax[x])
                yClipMax[x] = max_i;
            min += minSlope;
            max += maxSlope;
        }
    } else { // draw to screen
        for (int x = xstart; x < xend; x++) {
            int min_i = min / FUNIT;
            if (min_i < yClipMin[x])
                min_i = yClipMin[x];
            int max_i = max / FUNIT;
            if (max_i > yClipMax[x])
                max_i = yClipMax[x];
            int y = yClipMin[x];
            for (; y < min_i; y++)
                MODE4_FB[y][x] = ceilColor;
            for (; y < max_i; y++)
                MODE4_FB[y][x] = wallColor;
            for (; y < yClipMax[x]; y++)
                MODE4_FB[y][x] = floorColor;
            min += minSlope;
            max += maxSlope;
        }
    }
    return 1;
}