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

typedef enum {
    FILL_SOLID, FILL_TEXTURE, FILL_PARALLAX
} FillType;

typedef struct Sector {
    fixed zmin, zmax;
    const struct Wall * walls;
    int numWalls;
    int floorColor, ceilColor;
} Sector;

typedef struct Wall {
    fixed x1, y1; // x2 y2 defined by next wall
    FillType fillType;
    unsigned int fillNum;
    const struct Sector * portal;
} Wall;

typedef struct {
    int widthPwr, heightPwr;
    const u16 * data;
} Texture;

extern const Sector sectors[2];
const Wall walls[9] = {
    // sector 0 walls
    { 4*FUNIT,  4*FUNIT, FILL_TEXTURE, 0, 0},
    { 0*FUNIT,  4*FUNIT, FILL_SOLID, 0x0404, &sectors[1]},
    {-3*FUNIT,  2*FUNIT, FILL_SOLID, 0x0505, 0},
    {-3*FUNIT, -4*FUNIT, FILL_SOLID, 0x0404, 0},
    { 4*FUNIT, -4*FUNIT, FILL_SOLID, 0x0606, 0},
    // sector 1 walls
    { 4*FUNIT,  4*FUNIT, FILL_SOLID, 0x0101, &sectors[0]},
    { 4*FUNIT,  7*FUNIT, FILL_SOLID, 0x0606, 0},
    { 0*FUNIT,  7*FUNIT, FILL_SOLID, 0x0505, 0},
    { 0*FUNIT,  4*FUNIT, FILL_SOLID, 0x0101, 0}
};

const Sector sectors[2] = {
    {-256, 512, &walls[0], 5, 0x0202, 0x0303},
    {-256, 256, &walls[5], 4, 0x0303, 0x0202}
};

const Texture textures[3] = {
    {5, 5, texturesBitmap},
    {5, 5, texturesBitmap + 1024},
    {5, 5, texturesBitmap + 2048}
};

static inline void rotatePoint(fixed x, fixed y, fixed sint, fixed cost,
    fixed * xout, fixed * yout);

static void drawSector(const Sector * sector, fixed sint, fixed cost,
    int xClipMin, int xClipMax, YCB minYCB, YCB maxYCB, int depth);
// looking down x axis
// points should be ordered left to right on screen
// return if on screen
static inline int clipFrustum(fixed * x1, fixed * y1, fixed * x2, fixed * y2);
static inline void projectXY(fixed x1recip, fixed y1, fixed x2recip, fixed y2,
    int * outScrX1, int * outScrX2);
static inline void projectZ(fixed x1recip, fixed x2recip, fixed z,
    int * outScrY1, int * outScrY2);
static inline void calculateSlope(fixed x1, fixed y1, fixed x2, fixed y2,
    int xDrawMin, fixed * yStartOut, fixed * slopeOut);
static inline void ycbLine(int xDrawMin, int xDrawMax, fixed yStart, fixed slope,
    YCB minYCB, YCB maxYCB, YCB outYCB);
static void solidFill(int xDrawMin, int xDrawMax,
    fixed yStart1, fixed slope1, fixed yStart2, fixed slope2,
    YCB minYCB, YCB maxYCB, int color);
static void textureFill(int xDrawMin, int xDrawMax,
    fixed yStart1, fixed slope1, fixed yStart2, fixed slope2,
    YCB minYCB, YCB maxYCB, Texture texture);

static inline fixed cross(fixed x1, fixed y1, fixed x2, fixed y2) {
    return FMULT(x1, y2) - FMULT(y1, x2);
}

// intersect with frustum lines
static inline void intersectA(fixed crossX, fixed x1, fixed y1, fixed x2, fixed y2,
        fixed * xint) {
    fixed det = -(x1-x2) + y1-y2;
    if (det == 0)
        det = 1;
    *xint = FDIV(-crossX, det);
}
static inline void intersectB(fixed crossX, fixed x1, fixed y1, fixed x2, fixed y2,
        fixed * xint, fixed * yint) {
    fixed det = (x1-x2) + y1-y2;
    if (det == 0)
        det = 1;
    *xint = FDIV(-crossX, det);
    *yint = -(*xint);
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

        drawSector(currentSector, sint, cost, 0, M4WIDTH, screenMin, screenMax, 1);

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
static void drawSector(const Sector * sector, fixed sint, fixed cost,
        int xClipMin, int xClipMax, YCB minYCB, YCB maxYCB, int depth) {
    YCB newYCB1 = ycbs + depth * 2 * YCB_SIZE;
    YCB newYCB2 = newYCB1 + YCB_SIZE;

    // transformed vertices
    fixed tX, tY, prevTX, prevTY;
    int numWalls = sector->numWalls;
    rotatePoint(sector->walls[numWalls-1].x1 - camX,
                sector->walls[numWalls-1].y1 - camY,
                -sint, cost, &prevTX, &prevTY);
    for (int i = 0; i < numWalls; i++, prevTX=tX, prevTY=tY) {
        const Wall * wall = sector->walls + i;
        rotatePoint(wall->x1 - camX, wall->y1 - camY, -sint, cost, &tX, &tY);

        fixed x1 = tX, y1 = tY, x2 = prevTX, y2 = prevTY;
        if (!clipFrustum(&x1, &y1, &x2, &y2))
            continue;

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
        projectZ(x1recip, x2recip, sector->zmax-camZ, &scrYMin1, &scrYMin2);
        projectZ(x1recip, x2recip, sector->zmin-camZ, &scrYMax1, &scrYMax2);

        fixed yStart1, slope1, yStart2, slope2;
        calculateSlope(scrX1, scrYMin1, scrX2, scrYMin2, xDrawMin, &yStart1, &slope1);
        calculateSlope(scrX1, scrYMax1, scrX2, scrYMax2, xDrawMin, &yStart2, &slope2);
        solidFill(xDrawMin, xDrawMax, 0, 0, yStart1, slope1, minYCB, maxYCB, sector->ceilColor);
        solidFill(xDrawMin, xDrawMax, yStart2, slope2, SCREEN_HEIGHT*FUNIT, 0, minYCB, maxYCB, sector->floorColor);

        const Sector * portalSector = wall->portal;
        if (portalSector) {
            if (portalSector->zmax < sector->zmax) {
                // top wall
                fixed portalScrYMin1, portalScrYMin2;
                projectZ(x1recip, x2recip, portalSector->zmax-camZ, &portalScrYMin1, &portalScrYMin2);
                fixed portalYStart1, portalSlope1;
                calculateSlope(scrX1, portalScrYMin1, scrX2, portalScrYMin2, xDrawMin, &portalYStart1, &portalSlope1);
                solidFill(xDrawMin, xDrawMax, yStart1, slope1, portalYStart1, portalSlope1,
                    minYCB, maxYCB, wall->fillNum);
                yStart1 = portalYStart1; slope1 = portalSlope1;
            }
            if (portalSector->zmin > sector->zmin) {
                // bottom wall
                fixed portalScrYMax1, portalScrYMax2;
                projectZ(x1recip, x2recip, portalSector->zmin-camZ, &portalScrYMax1, &portalScrYMax2);
                fixed portalYStart2, portalSlope2;
                calculateSlope(scrX1, portalScrYMax1, scrX2, portalScrYMax2, xDrawMin, &portalYStart2, &portalSlope2);
                solidFill(xDrawMin, xDrawMax, portalYStart2, portalSlope2, yStart2, slope2,
                    minYCB, maxYCB, wall->fillNum);
                yStart2 = portalYStart2; slope2 = portalSlope2;
            }
            ycbLine(xDrawMin, xDrawMax, yStart1, slope1, minYCB, maxYCB, newYCB1);
            ycbLine(xDrawMin, xDrawMax, yStart2, slope2, minYCB, maxYCB, newYCB2);
            drawSector(portalSector, sint, cost, xDrawMin, xDrawMax, newYCB1, newYCB2, depth + 1);
        } else {
            switch(wall->fillType) {
                case FILL_SOLID:
                    solidFill(xDrawMin, xDrawMax, yStart1, slope1, yStart2, slope2, minYCB, maxYCB, wall->fillNum);
                    break;
                case FILL_TEXTURE:
                    textureFill(xDrawMin, xDrawMax, yStart1, slope1, yStart2, slope2, minYCB, maxYCB, textures[wall->fillNum]);
                    break;
            }
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

    // both points outside frustum on same side
    // or points are backwards
    if ((p1OutsideA && p2OutsideA) || (p1OutsideB && p2OutsideB)
            || (p2OutsideA && !p2OutsideB) || (p1OutsideB && !p1OutsideA))
        return 0;

    if (p1OutsideA || p2OutsideB) {
        fixed crossX = cross(*x1, *y1, *x2, *y2);
        // TODO: why do I have to do this?? also why do lines shake more
        fixed newX1 = 0;
        if (p1OutsideA)
            intersectA(crossX, *x1, *y1, *x2, *y2, &newX1);
        if (p2OutsideB)
            intersectB(crossX, *x1, *y1, *x2, *y2, x2, y2);
        if (p1OutsideA)
            *x1 = *y1 = newX1;
    }

#ifdef DEBUG_LINES
    bmp8_line(*x1/32 + 120, -*y1/32 + 80, *x2/32 + 120, -*y2/32 + 80,
              7, (void*)MODE4_FB, 240);
    return 0; // will prevent drawing line
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
static inline void projectZ(fixed x1recip, fixed x2recip, fixed z,
        int * outScrY1, int * outScrY2) {
    z *= FUNIT;
    *outScrY1 = HORIZON*FUNIT - FMULT(z, x1recip)/2;
    *outScrY2 = HORIZON*FUNIT - FMULT(z, x2recip)/2;
}

IWRAM_CODE
__attribute__((target("arm")))
static inline void calculateSlope(fixed x1, fixed y1, fixed x2, fixed y2,
        int xDrawMin, fixed * yStartOut, fixed * slopeOut) {
    *slopeOut = FDIV(y2 - y1, x2 - x1); // TODO: store reciprocal to reduce divisions
    *yStartOut = y1 + FMULT(xDrawMin*FUNIT - x1, *slopeOut);
}

IWRAM_CODE
__attribute__((target("arm")))
static inline void ycbLine(int xDrawMin, int xDrawMax, fixed yStart, fixed slope,
        YCB minYCB, YCB maxYCB, YCB outYCB) {
    fixed y = yStart;
    for (int x = xDrawMin; x < xDrawMax; x++) {
        int min = minYCB[x], max = maxYCB[x];
        int curY = y/FUNIT;
        if (curY < min)
            curY = min;
        if (curY > max)
            curY = max;
        outYCB[x] = curY;
        y += slope;
    }
}

IWRAM_CODE
__attribute__((target("arm")))
static void solidFill(int xDrawMin, int xDrawMax,
        fixed yStart1, fixed slope1, fixed yStart2, fixed slope2,
        YCB minYCB, YCB maxYCB, int color) {
    fixed y1 = yStart1, y2 = yStart2;
    for (int x = xDrawMin; x < xDrawMax; x++) {
        int y = minYCB[x], max = maxYCB[x];
        int curY1 = y1/FUNIT, curY2 = y2/FUNIT;
        if (curY1 > y)
            y = curY1;
        if (curY2 < max)
            max = curY2;
        for (; y < max; y++)
            MODE4_FB[y][x] = color;
        y1 += slope1; y2 += slope2;
    }
}

IWRAM_CODE
__attribute__((target("arm")))
static void textureFill(int xDrawMin, int xDrawMax,
        fixed yStart1, fixed slope1, fixed yStart2, fixed slope2,
        YCB minYCB, YCB maxYCB, Texture texture) {
    int texWidth = 1 << texture.widthPwr;
    fixed y1 = yStart1, y2 = yStart2;
    for (int x = xDrawMin; x < xDrawMax; x++) {
        int y = minYCB[x], max = maxYCB[x];
        int curY1 = y1/FUNIT, curY2 = y2/FUNIT;
        int lHeight = curY2 - curY1;
        if (curY1 > y)
            y = curY1;
        if (curY2 < max)
            max = curY2;
        if (y >= max)
            continue;

        int texU = 0;
        int yyy = (curY1 << texture.widthPwr) + lHeight;
        for (; (yyy >> texture.widthPwr) < y; yyy += lHeight) {
            texU++;
        }
        int maxYYY = max << texture.widthPwr;
        for (; yyy < maxYYY; yyy += lHeight) {
            int color = texture.data[texU];
            int texelMax = yyy >> texture.widthPwr;
            for (; y < texelMax; y++)
                MODE4_FB[y][x] = color;
            texU++;
        }
        // fill in the last texel separately
        int finalColor = texture.data[texU];
        for (; y < max; y++)
            MODE4_FB[y][x] = finalColor;
        y1 += slope1; y2 += slope2;
    }
}
