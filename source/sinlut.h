#ifndef SINLUT_H
#define SINLUT_H

// copied from
// https://www.coranac.com/tonc/text/fixed.htm

extern const short sin_lut[512];

// Sine/cosine lookups. 
// NOTE: theta's range is [0, 0xFFFF] for [0,2π⟩, just like the 
// BIOS functions

//! Look-up a sine value
static inline s32 lu_sin(u32 theta)
{   return sin_lut[(theta>>7)&0x1FF];   }

//! Look-up a cosine value
static inline s32 lu_cos(u32 theta)
{   return sin_lut[((theta>>7)+128)&0x1FF]; }

#endif