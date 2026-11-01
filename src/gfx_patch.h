/* In-memory HUD layout for menu/01_000_fe.gfx (replaces the loose file that
   build 4 shipped, so loaders that only load DLLs work too).

   The layout is five PlaceObject records: the HP, FP (MP), stamina (SP) and
   HPRegain placements inside the Gauge sprite get an X scale so the bars can
   reach the 16:9 edge, plus one PlaceObject3 inside sprite 573. Each record
   is matched by tag code and exact vanilla body (unique in the vanilla 2.7.1
   file, which 2.7.2.0 did not change) and replaced by the build 4 body.
   Nothing else in the movie is touched, so other in-memory patches of the
   same file (ERNativeUI, Multiple Spirit Ashes) keep their own changes. If a
   record is neither vanilla nor ours, the movie is left unchanged.

   Hook: a jmp rel32 at the entry of CS::CSScaleformFileOpener::OpenFile
   (vtable slot 1, found by RTTI; the vtable itself is never changed because
   ERNativeUI and Multiple Spirit Ashes check that it points into the game).
   The previous code (the game or an earlier hook) is called first and the
   returned file is patched, so inline hooks on the same entry (MinHook,
   Multiple Spirit Ashes) chain in any order. See gfx_hook_entry. */
#ifndef TV_GFX_PATCH_H
#define TV_GFX_PATCH_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char gfx_v0[]={0x26,0x0B,0x00,0xA6,0x01,0x1A,0xB8,0x2E,0x9A,0x00,0x53,0x50,0x00};
static const unsigned char gfx_p0[]={0x26,0x0B,0x00,0xA6,0x01,0xC9,0x35,0xC2,0x40,0x00,0x0D,0x5C,0x17,0x4D,0x00,0x53,0x50,0x00};
static const unsigned char gfx_v1[]={0x2E,0x0D,0x00,0xAB,0x01,0x1A,0xB8,0x2D,0xE5,0x80,0x68,0xFB,0x3E,0xCF,0xB4,0x00,0x4D,0x50,0x00};
static const unsigned char gfx_p1[]={0x2E,0x0D,0x00,0xAB,0x01,0xC9,0x35,0xC2,0x40,0x00,0x0D,0x5C,0x16,0xF2,0xC0,0x68,0xFB,0x3E,0xCF,0xB4,0x00,0x4D,0x50,0x00};
static const unsigned char gfx_v2[]={0x2E,0x0F,0x00,0xB2,0x01,0x1A,0xB8,0x2D,0x31,0x00,0x68,0xFB,0x3E,0xCF,0xB4,0x00,0x48,0x50,0x00};
static const unsigned char gfx_p2[]={0x2E,0x0F,0x00,0xB2,0x01,0xC9,0x35,0xC2,0x40,0x00,0x0D,0x5C,0x16,0x98,0x80,0x68,0xFB,0x3E,0xCF,0xB4,0x00,0x48,0x50,0x00};
static const unsigned char gfx_v3[]={0x2E,0x11,0x00,0xBB,0x01,0x1A,0xB8,0x2D,0x31,0x00,0x68,0xFB,0x3E,0xCF,0xB4,0x00,0x48,0x50,0x52,0x65,0x67,0x61,0x69,0x6E,0x00};
static const unsigned char gfx_p3[]={0x2E,0x11,0x00,0xBB,0x01,0xC9,0x35,0xC2,0x40,0x00,0x0D,0x5C,0x16,0x98,0x80,0x68,0xFB,0x3E,0xCF,0xB4,0x00,0x48,0x50,0x52,0x65,0x67,0x61,0x69,0x6E,0x00};
static const unsigned char gfx_v4[]={0x06,0x10,0x01,0x00,0xAD,0x00,0x84,0xC5,0x00,0x01,0x80,0x00,0x82,0x08,0x00,0x00,0x00};
static const unsigned char gfx_p4[]={0x06,0x10,0x01,0x00,0xAD,0x00,0x83,0x14,0x00,0x06,0x00,0x02,0x08,0x20,0x00,0x00,0x00};

typedef struct { const char *name; uint16_t code; const unsigned char *vanilla, *patched; size_t vanilla_size, patched_size; } GfxRecord;
#define GFX_RECORD(n,c,v,p) {n,c,v,p,sizeof(v),sizeof(p)}
static const GfxRecord gfx_records[5]={
    GFX_RECORD("Gauge.SP",26,gfx_v0,gfx_p0),
    GFX_RECORD("Gauge.MP",26,gfx_v1,gfx_p1),
    GFX_RECORD("Gauge.HP",26,gfx_v2,gfx_p2),
    GFX_RECORD("Gauge.HPRegain",26,gfx_v3,gfx_p3),
    GFX_RECORD("sprite 573 depth 1",70,gfx_v4,gfx_p4),
};
#define GFX_RECORD_COUNT 5

enum { GFX_PATCHED=1, GFX_ALREADY=2, GFX_NOT_FOUND=3, GFX_BAD=4 };

typedef struct { const unsigned char *in; size_t size, at; } GfxReader;
/* One SWF tag at r->at inside [.., end): code, header size and body size. */
static int gfx_tag(const unsigned char *b,size_t at,size_t end,uint16_t *code,size_t *header,size_t *length) {
    if (at+2>end) return 0;
    uint16_t cl=(uint16_t)(b[at]|b[at+1]<<8);
    *code=(uint16_t)(cl>>6); *length=cl&0x3F; *header=2;
    if (*length==0x3F) {
        if (at+6>end) return 0;
        *length=(size_t)b[at+2]|(size_t)b[at+3]<<8|(size_t)b[at+4]<<16|(size_t)b[at+5]<<24; *header=6;
    }
    return *length<=end-at-*header;
}
static size_t gfx_put_header(unsigned char *out,uint16_t code,size_t length,int long_form) {
    if (!long_form && length<0x3F) { uint16_t cl=(uint16_t)(code<<6|length); out[0]=(unsigned char)cl; out[1]=(unsigned char)(cl>>8); return 2; }
    uint16_t cl=(uint16_t)(code<<6|0x3F); out[0]=(unsigned char)cl; out[1]=(unsigned char)(cl>>8);
    out[2]=(unsigned char)length; out[3]=(unsigned char)(length>>8); out[4]=(unsigned char)(length>>16); out[5]=(unsigned char)(length>>24);
    return 6;
}

/* Patches an uncompressed GFX/FWS movie. On GFX_PATCHED *out is a malloc'd
   copy (caller owns it). seen[i]: 0 missing, 1 vanilla, 2 already ours. */
static int gfx_patch_fe(const unsigned char *in,size_t size,unsigned char **out,size_t *out_size,int seen[GFX_RECORD_COUNT]) {
    *out=NULL; *out_size=0;
    for (int i=0;i<GFX_RECORD_COUNT;++i) seen[i]=0;
    /* Uncompressed only: "GFX" (Scaleform) or "FWS"; "CFX"/"CWS" are zlib. */
    if (size<12 || size>64u*1024u*1024u || (memcmp(in,"GFX",3) && memcmp(in,"FWS",3))) return GFX_BAD;
    size_t body=8+((5+4*(size_t)(in[8]>>3)+7)/8)+4;
    if (body>=size) return GFX_BAD;
    /* Pass 1: classify every record (each must be seen exactly once). */
    int duplicates=0;
    for (size_t at=body;at<size;) {
        uint16_t code; size_t header,length;
        if (!gfx_tag(in,at,size,&code,&header,&length)) return GFX_BAD;
        if (code==39 && length>=4) {
            size_t inner_end=at+header+length;
            for (size_t i=at+header+4;i<inner_end;) {
                uint16_t c2; size_t h2,l2;
                if (!gfx_tag(in,i,inner_end,&c2,&h2,&l2)) return GFX_BAD;
                for (int r=0;r<GFX_RECORD_COUNT;++r) {
                    const GfxRecord *g=&gfx_records[r]; int kind=0;
                    if (c2!=g->code) continue;
                    if (l2==g->vanilla_size && !memcmp(in+i+h2,g->vanilla,l2)) kind=1;
                    else if (l2==g->patched_size && !memcmp(in+i+h2,g->patched,l2)) kind=2;
                    if (kind) { if (seen[r]) duplicates=1; seen[r]=kind; }
                }
                i+=h2+l2;
                if (!c2) break;
            }
        }
        at+=header+length;
        if (!code) break;
    }
    int vanilla=0;
    for (int r=0;r<GFX_RECORD_COUNT;++r) { if (!seen[r]) return GFX_NOT_FOUND; if (seen[r]==1) ++vanilla; }
    if (duplicates) return GFX_NOT_FOUND;
    if (!vanilla) return GFX_ALREADY;
    /* Pass 2: copy, replacing the vanilla records and resizing their sprites. */
    size_t growth=0;
    for (int r=0;r<GFX_RECORD_COUNT;++r) if (seen[r]==1) growth+=gfx_records[r].patched_size-gfx_records[r].vanilla_size;
    size_t capacity=size+growth+64;
    unsigned char *o=malloc(capacity), *sprite=malloc(size+growth+16);
    if (!o || !sprite) { free(o); free(sprite); return GFX_BAD; }
    memcpy(o,in,body); size_t w=body;
    for (size_t at=body;at<size;) {
        uint16_t code; size_t header,length;
        gfx_tag(in,at,size,&code,&header,&length);
        int touched=0; size_t sprite_size=0;
        if (code==39 && length>=4) {
            size_t inner_end=at+header+length;
            memcpy(sprite,in+at+header,4); sprite_size=4;
            for (size_t i=at+header+4;i<inner_end;) {
                uint16_t c2; size_t h2,l2; int replaced=0;
                gfx_tag(in,i,inner_end,&c2,&h2,&l2);
                for (int r=0;r<GFX_RECORD_COUNT && !replaced;++r) {
                    const GfxRecord *g=&gfx_records[r];
                    if (c2!=g->code || l2!=g->vanilla_size || memcmp(in+i+h2,g->vanilla,l2)) continue;
                    sprite_size+=gfx_put_header(sprite+sprite_size,c2,g->patched_size,h2==6);
                    memcpy(sprite+sprite_size,g->patched,g->patched_size); sprite_size+=g->patched_size;
                    replaced=touched=1;
                }
                if (!replaced) { memcpy(sprite+sprite_size,in+i,h2+l2); sprite_size+=h2+l2; }
                i+=h2+l2;
                if (!c2) break;
            }
            if (touched && sprite_size<inner_end-(at+header)) touched=0; /* never shrinks; keep as is */
        }
        if (touched) {
            w+=gfx_put_header(o+w,code,sprite_size,header==6);
            memcpy(o+w,sprite,sprite_size); w+=sprite_size;
        } else { memcpy(o+w,in+at,header+length); w+=header+length; }
        at+=header+length;
        if (!code) { if (at<size) { memcpy(o+w,in+at,size-at); w+=size-at; } break; }
    }
    free(sprite);
    if (w!=size+growth) { free(o); return GFX_BAD; }
    o[4]=(unsigned char)w; o[5]=(unsigned char)(w>>8); o[6]=(unsigned char)(w>>16); o[7]=(unsigned char)(w>>24);
    *out=o; *out_size=w;
    return GFX_PATCHED;
}

static int gfx_url_is_fe(const char *url) {
    const char *name=url;
    for (const char *p=url;*p;++p) if (*p=='/' || *p=='\\' || *p==':') name=p+1;
    return !_stricmp(name,"01_000_fe.gfx");
}

#endif
