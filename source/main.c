#include <gba.h>
#include "fixed.h"
#include "sinlut.h"
#include "tonc_bmp8.h"
#include "textures.h"

//#define DEBUG_LINES

//https://stackoverflow.com/a/3982397
#define SWAP(x, y) do { typeof(x) SWAP = x; x = y; y = SWAP; } while (0)

#define M4WIDTH 120
typedef u16 MODE4_LINE[M4WIDTH];
#define MODE4_FB ((MODE4_LINE *)0x06000000)

#define HORIZON 80

// Y Clip Buffer
typedef s16 * YCB;
// num hwords
#define YCB_SIZE 128

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

void drawSector(Sector sector, fixed sint, fixed cost,
                int xClipMin, int xClipMax, YCB minYCB, YCB maxYCB, int depth);
// looking down x axis
// points should be ordered left to right on screen
// return if on screen
static inline int clipFrustum(fixed * x1, fixed * y1, fixed * x2, fixed * y2);
static inline void projectXY(fixed x1recip, fixed y1, fixed x2recip, fixed y2,
    int * outScrX1, int * outScrX2);
static inline void projectZ(fixed x1recip, fixed x2recip, fixed z1, fixed z2,
    int * outScrYMin1, int * outScrYMax1,
    int * outScrYMin2, int * outScrYMax2);
void ycbLine(fixed x1, fixed y1, fixed x2, fixed y2,
             int xDrawMin, int xDrawMax,
             YCB minYCB, YCB maxYCB, YCB outYCB);
void solidFill(int x1, int x2, YCB minYCB, YCB maxYCB, int color);

static inline fixed cross(fixed x1, fixed y1, fixed x2, fixed y2) {
    return FMULT(x1, y2) - FMULT(y1, x2);
}

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
    *yout = FMULT(y, cost) + FMULT(x, sint);
    *xout = newx;
}

fixed camX = 0, camY = 0, camZ = 0;
const Sector * currentSector;

// room for 64 YCBs
//YCB ycbs = (YCB)(VRAM + 81920);
s16 ycbs[8192];

int main(void) {
	irqInit();
	irqEnable(IRQ_VBLANK);
	REG_IME = 1;

	REG_DISPCNT = MODE_4 | BG2_ON;

    CpuFastSet(texturesPal, BG_COLORS, texturesPalLen/4);

    YCB screenMin = ycbs;
    YCB screenMax = ycbs + YCB_SIZE;
    const int zero = 0;
    const int yMaxFill = SCREEN_HEIGHT | (SCREEN_HEIGHT << 16);
    // clear ymin/max buffers
    CpuFastSet(&zero, screenMin, 64 | (1<<24));
    CpuFastSet(&yMaxFill, screenMax, 64 | (1<<24));

    int theta = 0;
    currentSector = sectors;

    while (1) {
#ifdef DEBUG_LINES
        CpuFastSet(&zero, (void*)VRAM, 9600 | (1<<24));
#endif

        fixed sint = lu_sin(theta) >> 4;
        fixed cost = lu_cos(theta) >> 4;

        drawSector(*currentSector, sint, cost, 0, M4WIDTH, screenMin, screenMax, 1);

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
        fixed moveX = 0, moveY = 0;
        if (buttons & KEY_UP) {
            moveX += cost / 16;
            moveY += sint / 16;
        }
        if (buttons & KEY_DOWN) {
            moveX -= cost / 16;
            moveY -= sint / 16;
        }
        if (buttons & KEY_RIGHT) {
            moveX += sint / 16;
            moveY -= cost / 16;
        }
        if (buttons & KEY_LEFT) {
            moveX -= sint / 16;
            moveY += cost / 16;
        }
        if (buttons & KEY_A) {
            camZ += 16;
        }
        if (buttons & KEY_B) {
            camZ -= 16;
        }

        const Sector * newSector = currentSector;
        if (moveX != 0 || moveY != 0) {
            int numWalls = currentSector->numWalls;
            for (int i = 0; i < numWalls; i++) {
                const Wall * wall1 = currentSector->walls + i;
                const Wall * wall2 = currentSector->walls + (i+1)%(numWalls);
                fixed wallVX = wall1->x1 - wall2->x1;
                fixed wallVY = wall1->y1 - wall2->y1;
                // https://stackoverflow.com/a/3461533
                if (cross(wallVX, wallVY,
                        camX + moveX - wall2->x1, camY + moveY - wall2->y1) > 0) {
                    // moved out of sector
                    if (wall2->portal) {
                        newSector = wall2->portal;
                    } else {
                        fixed project = FDIV(moveX*wallVX+moveY*wallVY, wallVX*wallVX+wallVY*wallVY);
                        moveX = FMULT(wallVX, project);
                        moveY = FMULT(wallVY, project);
                    }
                }
            }
        }
        currentSector = newSector;
        camX += moveX;
        camY += moveY;
    }
}

IWRAM_CODE
__attribute__((target("arm")))
void drawSector(Sector sector, fixed sint, fixed cost,
                int xClipMin, int xClipMax, YCB minYCB, YCB maxYCB, int depth) {
    YCB newYCB1 = ycbs + depth * 2 * YCB_SIZE;
    YCB newYCB2 = newYCB1 + YCB_SIZE;

    // transformed vertices
    fixed tX, tY, prevTX, prevTY;
    int numWalls = sector.numWalls;
    rotatePoint(sector.walls[numWalls-1].x1 - camX,
                sector.walls[numWalls-1].y1 - camY,
                -sint, cost, &prevTX, &prevTY);
    for (int i = 0; i < numWalls; i++, prevTX=tX, prevTY=tY) {
        const Wall * wall = sector.walls + i;
        rotatePoint(wall->x1 - camX, wall->y1 - camY, -sint, cost, &tX, &tY);

        fixed x1 = tX, y1 = tY, x2 = prevTX, y2 = prevTY;
        if (!clipFrustum(&x1, &y1, &x2, &y2))
            continue;
#ifdef DEBUG_LINES
        continue;
#endif
        fixed x1recip = FRECIP(x1), x2recip = FRECIP(x2);
        fixed scrX1, scrX2;
        projectXY(x1recip, y1, x2recip, y2, &scrX1, &scrX2);
        int xDrawMin = scrX1 / FUNIT;
        int xDrawMax = scrX2 / FUNIT;
        if (xDrawMax <= xDrawMin || xDrawMax < xClipMin || xDrawMin >= xClipMax)
            continue;
        if (xDrawMin < xClipMin)
            xDrawMin = xClipMin;
        if (xDrawMax > xClipMax)
            xDrawMax = xClipMax;

        fixed scrYMin1, scrYMax1, scrYMin2, scrYMax2;
        projectZ(x1recip, x2recip, sector.zmin-camZ, sector.zmax-camZ,
                 &scrYMin1, &scrYMax1, &scrYMin2, &scrYMax2);

        const Sector * portalSector = wall->portal;
        if (portalSector) {
            fixed portalScrYMin1, portalScrYMax1, portalScrYMin2, portalScrYMax2;
            projectZ(x1recip, x2recip, portalSector->zmin-camZ, portalSector->zmax-camZ,
                    &portalScrYMin1, &portalScrYMax1, &portalScrYMin2, &portalScrYMax2);

            // top edge
            ycbLine(scrX1, scrYMin1, scrX2, scrYMin2, xDrawMin, xDrawMax,
                    minYCB, maxYCB, newYCB1);
            solidFill(xDrawMin, xDrawMax, minYCB, newYCB1, sector.ceilColor);
            if (portalSector->zmax < sector.zmax) {
                SWAP(newYCB1, newYCB2);
                // top wall
                ycbLine(scrX1, portalScrYMin1, scrX2, portalScrYMin2, xDrawMin, xDrawMax,
                        minYCB, maxYCB, newYCB1);
                solidFill(xDrawMin, xDrawMax, newYCB2, newYCB1, wall->color);
            }
            // bottom of portal
            if (portalSector->zmin > sector.zmin) {
                ycbLine(scrX1, portalScrYMax1, scrX2, portalScrYMax2, xDrawMin, xDrawMax,
                        minYCB, maxYCB, newYCB2);
            } else {
                ycbLine(scrX1, scrYMax1, scrX2, scrYMax2, xDrawMin, xDrawMax,
                        minYCB, maxYCB, newYCB2);
            }
            drawSector(*portalSector, sint, cost, xDrawMin, xDrawMax, newYCB1, newYCB2, depth + 1);
            if (portalSector->zmin > sector.zmin) {
                SWAP(newYCB1, newYCB2);
                ycbLine(scrX1, scrYMax1, scrX2, scrYMax2, xDrawMin, xDrawMax,
                        minYCB, maxYCB, newYCB2);
                solidFill(xDrawMin, xDrawMax, newYCB1, newYCB2, wall->color);
            }
            solidFill(xDrawMin, xDrawMax, newYCB2, maxYCB, sector.floorColor);
        } else {
            // top edge of wall
            ycbLine(scrX1, scrYMin1, scrX2, scrYMin2, xDrawMin, xDrawMax,
                    minYCB, maxYCB, newYCB1);
            solidFill(xDrawMin, xDrawMax, minYCB, newYCB1, sector.ceilColor);
            // bottom edge
            ycbLine(scrX1, scrYMax1, scrX2, scrYMax2, xDrawMin, xDrawMax,
                    minYCB, maxYCB, newYCB2);
            solidFill(xDrawMin, xDrawMax, newYCB1, newYCB2, wall->color);
            solidFill(xDrawMin, xDrawMax, newYCB2, maxYCB, sector.floorColor);
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
static inline void projectXY(fixed x1recip, fixed y1, fixed x2recip, fixed y2,
        int * outScrX1, int * outScrX2) {
    *outScrX1 = M4WIDTH/2*FUNIT - FMULT(y1*FUNIT, x1recip)/4;
    *outScrX2 = M4WIDTH/2*FUNIT - FMULT(y2*FUNIT, x2recip)/4;
}

IWRAM_CODE
__attribute__((target("arm")))
static inline void projectZ(fixed x1recip, fixed x2recip, fixed z1, fixed z2,
        int * outScrYMin1, int * outScrYMax1,
        int * outScrYMin2, int * outScrYMax2){
    *outScrYMin1 = HORIZON*FUNIT - FMULT(z2*FUNIT, x1recip)/2;
    *outScrYMax1 = HORIZON*FUNIT - FMULT(z1*FUNIT, x1recip)/2;
    *outScrYMin2 = HORIZON*FUNIT - FMULT(z2*FUNIT, x2recip)/2;
    *outScrYMax2 = HORIZON*FUNIT - FMULT(z1*FUNIT, x2recip)/2;
}


IWRAM_CODE
__attribute__((target("arm")))
void ycbLine(fixed x1, fixed y1, fixed x2, fixed y2,
             int xDrawMin, int xDrawMax,
             YCB minYCB, YCB maxYCB, YCB outYCB) {
    fixed slope = FDIV(y2 - y1, x2 - x1);
    fixed curY = y1 + FMULT(xDrawMin*FUNIT - x1, slope);
    for (int x = xDrawMin; x < xDrawMax; x++) {
        int curY_i = curY / FUNIT;
        if (curY_i < minYCB[x])
            curY_i = minYCB[x];
        else if (curY_i > maxYCB[x])
            curY_i = maxYCB[x];
        outYCB[x] = curY_i;
        curY += slope;
    }
}

IWRAM_CODE
__attribute__((target("arm")))
void solidFill(int x1, int x2, YCB minYCB, YCB maxYCB, int color) {
    for (int x = x1; x < x2; x++) {
        int max = maxYCB[x];
        for (int y = minYCB[x]; y < max; y++)
            MODE4_FB[y][x] = color;
    }
}
