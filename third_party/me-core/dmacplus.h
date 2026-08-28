// m-c/d, m-cid, mcidclan 2026
#ifndef ME_CUSTOM_DMACPLUS_H
#define ME_CUSTOM_DMACPLUS_H

#include <pspkernel.h>
#include "hw-registers.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DMA_CONTROL_SC2SC(width, size)  DMA_CONTROL_CONFIG(1, 1, 0, 0, (width), (size))
#define DMA_CONTROL_SC2ME(width, size)  DMA_CONTROL_CONFIG(0, 0, 1, 0, (width), (size))
#define DMA_CONTROL_ME2SC(width, size)  DMA_CONTROL_CONFIG(0, 0, 0, 1, (width), (size))

#define DMA_CONTROL_CONFIG(idst, isrc, mdst, msrc, width, size) ( \
  (1U << 31) |                  /* terminal count interrupt enable bit */ \
  ((unsigned)(idst) << 27) |    /* dst addr increment ?                */ \
  ((unsigned)(isrc) << 26) |    /* src addr increment ?                */ \
  ((unsigned)(mdst) << 25) |    /* dst AHB master select               */ \
  ((unsigned)(msrc) << 24) |    /* src AHB master select               */ \
  ((unsigned)(width) << 21) |   /* dst transfer width                  */ \
  ((unsigned)(width) << 18) |   /* src transfer width                  */ \
  (1U << 15) |                  /* dst burst size or dst step ?        */ \
  (1U << 12) |                  /* src burst size or src step ?        */ \
  ((unsigned)(size)))           /* transfer size                       */

// note: addr increment, needs to be set while copying data from sc to sc
//  0 sc, 1 me, lli will not work if not set correctly
// note: master select
//  0 sc, 1 me, lli will not work if not set correctly
// note: transfer width
//  0 => 1 byte,
//  1 => 2 bytes,
//  2 => 4 bytes,
//  3 => 8 bytes,
//  4 => 16 bytes

enum DMACPLUS_BYTE_COUNT {
  
  DMACPLUS_BYTE_COUNT_1,  //0
  DMACPLUS_BYTE_COUNT_2,  //1
  DMACPLUS_BYTE_COUNT_4,  //2
  DMACPLUS_BYTE_COUNT_8,  //3
  DMACPLUS_BYTE_COUNT_16  //4
};

typedef struct DMADescriptor {
  
  u32 src;
  u32 dst;
  u32 next;
  u32 ctrl;
  u32 status;
} DMADescriptor __attribute__((aligned(16)));

typedef u32 (*DmaControl)(const u32, const u32);

// hardware registers behind sceDmacplusSc2Me
static inline void dmacplusFromSc(const u32 src, const u32 dst, const u32 width, const u32 size) {

  int intr = sceKernelCpuSuspendIntr();
  hw(0xBC800180) = src;                          // src
  hw(0xBC800184) = dst;                          // dest
  hw(0xBC800188) = 0;                            // addr of the next LLI
  hw(0xBC80018c) = DMA_CONTROL_SC2ME(width, size);   // control attr
  hw(0xBC800190) =                               // status
    (1 << 0);                                     // channel enable
  asm volatile("sync");
  sceKernelCpuResumeIntrWithSync(intr);
}

static inline void dmacplusLLIFromSc(const DMADescriptor* const lli) {
  
  int intr = sceKernelCpuSuspendIntr();
  hw(0xBC800180) = lli->src;                          // src
  hw(0xBC800184) = lli->dst;                          // dest
  hw(0xBC800188) = lli->next;                         // addr of the next LLI
  hw(0xBC80018c) = lli->ctrl;                         // control attr
  hw(0xBC800190) = lli->status;                       // status
  asm volatile("sync");
  sceKernelCpuResumeIntrWithSync(intr);
}

static inline u32 dmaControlMe2Sc(const u32 width, const u32 size) {
  
  return DMA_CONTROL_ME2SC(width, size);
}

// hardware registers behind sceDmacplusMe2Sc
static inline void dmacplusFromMe(const u32 src, const u32 dst, const u32 width, const u32 size) {

  int intr = sceKernelCpuSuspendIntr();
  hw(0xBC8001a0) = src;                          // src
  hw(0xBC8001a4) = dst;                          // dest
  hw(0xBC8001a8) = 0;                            // addr of the next LLI
  hw(0xBC8001ac) = DMA_CONTROL_ME2SC(width, size);   // control attr
  hw(0xBC8001b0) =                               // status
    (1 << 0);                                     // channel enable
  asm volatile("sync");
  sceKernelCpuResumeIntrWithSync(intr);
}

static inline void dmacplusLLIFromMe(const DMADescriptor* const lli) {
  
  int intr = sceKernelCpuSuspendIntr();
  hw(0xBC8001a0) = lli->src;                          // src
  hw(0xBC8001a4) = lli->dst;                          // dest
  hw(0xBC8001a8) = lli->next;                         // addr of the next LLI
  hw(0xBC8001ac) = lli->ctrl;                         // control attr
  hw(0xBC8001b0) = lli->status;                       // status
  asm volatile("sync");
  sceKernelCpuResumeIntrWithSync(intr);
}

static inline u32 dmaControlSc2Me(const u32 width, const u32 size) {
  
  return DMA_CONTROL_SC2ME(width, size);
}

static inline void dmacplusLLIOverSc(const DMADescriptor* const lli) {
  
  int intr = sceKernelCpuSuspendIntr();
  hw(0xBC8001c0) = lli->src;                          // src
  hw(0xBC8001c4) = lli->dst;                          // dest
  hw(0xBC8001c8) = lli->next;                         // addr of the next LLI
  hw(0xBC8001cc) = lli->ctrl;                         // control attr
  hw(0xBC8001d0) = lli->status;                       // status
  asm volatile("sync");
  sceKernelCpuResumeIntrWithSync(intr);
}

//
static inline void clearPendingOperations() {
  
  hw(0xBC800010) = 0xffffffff;
  hw(0xBC800008) = 0xffffffff;
  asm volatile("sync");
}

static inline void cleanSc2MeChannel() {
  
  int intr = sceKernelCpuSuspendIntr();
  hw(0xBC800190) = 0;
  asm volatile("sync");
  sceKernelCpuResumeIntrWithSync(intr);
}

static inline void waitSc2MeChannel() {
  
  int intr = sceKernelCpuSuspendIntr();
  while (1) {
    asm volatile("sync");
    const u32 ch0 = hw(0xBC800190);
    asm volatile("sync");
    if (!ch0) {
      break;
    }
    asm volatile("nop;nop;nop;nop;nop;nop;nop");
  }
  sceKernelCpuResumeIntrWithSync(intr);
  
  clearPendingOperations();
}

static inline void cleanChannels() {
  
  int intr = sceKernelCpuSuspendIntr();
  hw(0xBC800190) = 0;
  hw(0xBC8001b0) = 0;
  hw(0xBC8001d0) = 0;
  asm volatile("sync");
  sceKernelCpuResumeIntrWithSync(intr);
}

static inline void waitChannels() {
  
  int intr = sceKernelCpuSuspendIntr();
  while (1) {
    asm volatile("sync");
    const u32 ch0 = hw(0xBC800190);
    const u32 ch1 = hw(0xBC8001b0);
    const u32 ch2 = hw(0xBC8001d0);
    asm volatile("sync");
    if (!ch0 && !ch1 && !ch2) {
      break;
    }
    asm volatile("nop;nop;nop;nop;nop;nop;nop");
  }
  sceKernelCpuResumeIntrWithSync(intr);
  
  clearPendingOperations();
}

typedef void* (*LLIAllocator)(int, int);

typedef struct  LLIConfig {
  
  DmaControl dc;
  u32 src;
  u32 dst;
  u32 size;
  LLIAllocator alloc;
} LLIConfig __attribute__((aligned(16)));

// custom LLI builder
static inline DMADescriptor* dmacplusInitLLIs(const LLIConfig* const cfg) {
// const DmaControl dc, const u32 src, const u32 dst, const u32 size

  static const u32 MAX_TRANSFER_SIZE = 4095;
  static const struct { u32 width; u32 widthBit; } modes[] = {
    {16, 4}, {8, 3}, {4, 2}, {2, 1}, {1, 0}
  };

  u32 lliCount = 0;
  u32 remaining = cfg->size;
  
  for (int w = 0; w < 4 && remaining > 0; w++) {
    
    u32 block = modes[w].width * MAX_TRANSFER_SIZE;
    lliCount += remaining / block;
    remaining %= block;
  }
  lliCount += (remaining > 0);

  const u32 byteCount = sizeof(DMADescriptor) * lliCount;
  DMADescriptor* const lli = (DMADescriptor*) cfg->alloc(64, (byteCount + 63) & ~63);

  u32 i = 0;
  u32 offset = 0;
  remaining = cfg->size;

  for (int w = 0; w < 5 && remaining > 0; w++) {
    
    u32 width = modes[w].width;
    u32 widthBit = modes[w].widthBit;
    u32 block = width * MAX_TRANSFER_SIZE;

    if (w < 4 && remaining >= block) {
      u32 blockCount = remaining / block;
      for (u32 j = 0; j < blockCount; j++) {
        lli[i].src = cfg->src + offset;
        lli[i].dst = cfg->dst + offset;
        lli[i].ctrl = cfg->dc(widthBit, MAX_TRANSFER_SIZE);
        lli[i].status = 1;
        lli[i].next = (i < (lliCount - 1)) ? ((u32)&lli[i + 1]) : 0;
        offset += block;
        remaining -= block;
        i++;
      }
    }
    else if (remaining > 0) {
      
      lli[i].src = cfg->src + offset;
      lli[i].dst = cfg->dst + offset;
      lli[i].ctrl = cfg->dc(0, remaining);
      lli[i].status = 1;
      lli[i].next = 0;
      i++;
      remaining = 0;
    }
  }
  
  sceKernelDcacheWritebackInvalidateRange((void*)lli, byteCount);
  return lli;
}

#ifdef __cplusplus
}
#endif

#endif
