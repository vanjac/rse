#ifndef FIXED_H
#define FIXED_H

// 24.8 fixed point
#define FPOINT      8
#define FPOINT2     (FPOINT*2)
#define FUNIT       (1<<FPOINT)
#define FUNIT2      (1<<FPOINT2)

typedef int fixed;
typedef unsigned int ufixed;

static inline fixed FMULT(fixed a, fixed b) {
    return (int32_t)(((int64_t)a * b) / FUNIT);
}

static inline fixed FDIV(fixed a, fixed b) {
    return a * FUNIT / b;
}

static inline fixed FRECIP(fixed a) {
    return FUNIT2 / a;
}

static inline int ABS(int a) {
    // https://stackoverflow.com/a/21854586
    return (a + (a >> 31)) ^ (a >> 31);
}

#endif