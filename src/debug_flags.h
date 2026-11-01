/* Character debug flag block (no stamina use +4, no FP use +5, no Ashes of War
   FP use +0x11), found by three code signatures that compare those bytes with
   zero. Each signature must match exactly once in the game's .text and all
   three must name the same .data block: 0x3D6A210 in 2.7.0.0 / 2.7.1.0 /
   2.7.1.1, 0x3D6D230 in 2.7.2.0. */
#ifndef TV_DEBUG_FLAGS_H
#define TV_DEBUG_FLAGS_H

#include <stdint.h>
#include <string.h>

typedef struct { const unsigned char *tail; size_t tail_size; uint32_t field; } DebugFlagSignature;

/* Every signature is "cmp byte ptr [rip+X],0" (80 3D X 00) followed by tail. */
static const unsigned char debug_tail_stamina[]={0x75,0x04,0x32,0xC0,0xEB,0x02,0xB0,0x01,0xF6,0xC3,0x01,0x74,0x07,0x83,0xE3,0xFE,0x89,0x5C,0x24,0x20,0x84,0xC0,0x75,0x2E};
static const unsigned char debug_tail_fp[]={0x74,0x04,0xB0,0x01,0xEB,0x02,0x32,0xC0,0xF6,0xC3,0x01,0x74,0x07,0x83,0xE3,0xFE,0x89,0x5C,0x24,0x20,0x84,0xC0,0x75,0x2D};
static const unsigned char debug_tail_art_fp[]={0x75,0x3C,0x80,0x7B,0x14,0x00};
static const DebugFlagSignature debug_flag_signatures[3]={
    {debug_tail_stamina,sizeof(debug_tail_stamina),0x04},
    {debug_tail_fp,sizeof(debug_tail_fp),0x05},
    {debug_tail_art_fp,sizeof(debug_tail_art_fp),0x11},
};

/* text is a copy of the section that starts at text_address. Returns the
   block address, or 0 when a signature is missing, repeated or disagrees. */
static uintptr_t find_debug_flags(uintptr_t text_address,const unsigned char *text,size_t text_size,
                                  uintptr_t data_start,uintptr_t data_end,int matches[3]) {
    uintptr_t block=0; int agree=1;
    for (int s=0;s<3;++s) {
        const DebugFlagSignature *sig=&debug_flag_signatures[s];
        uintptr_t found=0; matches[s]=0;
        for (size_t i=0;i+7+sig->tail_size<=text_size;++i) {
            if (text[i]!=0x80 || text[i+1]!=0x3D || text[i+6]!=0x00 || memcmp(text+i+7,sig->tail,sig->tail_size)) continue;
            int32_t rel; memcpy(&rel,text+i+2,4);
            found=text_address+i+7+(intptr_t)rel-sig->field; ++matches[s];
        }
        if (matches[s]!=1 || found<data_start || found+0x20>data_end) agree=0;
        else if (!block) block=found;
        else if (block!=found) agree=0;
    }
    return agree?block:0;
}

#endif
