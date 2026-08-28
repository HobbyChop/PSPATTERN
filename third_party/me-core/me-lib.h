#ifndef ME_LIB_H
#define ME_LIB_H

#include <pspsdk.h>
#include <pspkernel.h>
#include <psppower.h>
#include <malloc.h>
#include <string.h>

#include "hw-registers.h"
#include "kernel/kcall.h"
#include "vme-lib.h"

#define ERROR_ON_WRITE_PRX  -3
#define ERROR_ON_LOAD_PRX   -4
#define ERROR_ON_KINIT      -5
#define ERROR_ON_ME_IMG     -6

#define meLibSync()                       asm volatile("sync")
#define meLibDelayPipeline()              asm volatile("nop; nop; nop; nop; nop; nop; nop; sync;")
#define meLibCallHwMutexTryLock()         (kcall(meCoreHwMutexTryLock, CACHED_KERNEL_MASK))
#define meLibCallHwMutexUnlock()          (kcall(meCoreHwMutexUnlock, CACHED_KERNEL_MASK))
#define meLibEmitSoftwareInterrupt()      (kcall(meCoreEmitSoftwareInterrupt, CACHED_KERNEL_MASK))

#define meLibSetSharedUncached32(size)    meLibSetSharedUncachedMem(size, u32)

#ifdef __cplusplus
  #define meLibSetSharedUncachedMem(size, type) \
    static volatile type _meLibSharedMemory[(size)] __attribute__((aligned(64), section(".uncached"))) = {(type)0}; \
    volatile type* const meLibSharedMemory __attribute__((aligned(64), section(".uncached"))) = \
      (volatile type*)(UNCACHED_USER_MASK | (uintptr_t)_meLibSharedMemory)

  #define meLibMakeUncachedMem(name, size, type) \
    volatile type name[(size)] __attribute__((aligned(64), section(".uncached"))) = {(type)0};

  #define meLibMakeSharedUncachedMem(name, size, type) \
    static volatile type _meLib_##name[(size)] __attribute__((aligned(64), section(".uncached"))) = {(type)0}; \
    volatile type* const name __attribute__((aligned(64), section(".uncached"))) = \
      (volatile type*)(UNCACHED_USER_MASK | (uintptr_t)_meLib_##name)
#endif


#define meLibMakeMemSegVar(name, mask, type) ((volatile type* const)((mask) | (type)name))
#define meLibMakeUncachedUserSeg(name, type) meLibMakeMemSegVar(name, UNCACHED_USER_MASK, type)
#define meLibMakeUncachedKernelSeg(name, type) meLibMakeMemSegVar(name, UNCACHED_KERNEL_MASK, type)

#define defineVar(name) \
  volatile u32 _##name[1] __attribute__((aligned(64), section(".uncached"))) = {0}; \
  volatile u32* name __attribute__((aligned(64), section(".uncached"))) = NULL;

#define buildUncachedVar(name) \
  name = ((volatile u32* const)((UNCACHED_USER_MASK) | (u32)_##name)); \
  name[0] = 0;

// define the non-cached kernel mutex
#define meLibMutex hw(0xbc100048)

#define meLibUnlockHwUserRegisters()            \
{                                               \
  const u32 START = 0xbc000030;               \
  const u32 END   = 0xbc000044;               \
  for(u32 reg = START; reg <= END; reg+=4) {  \
    hw(reg) = -1;                               \
  }                                             \
  meLibSync();                                  \
}                                               \
  

#define meLibUnlockMemory()                     \
{                                               \
  const u32 START = 0xbc000000;               \
  const u32 END   = 0xbc00002c;               \
  for(u32 reg = START; reg <= END; reg+=4) {  \
    hw(reg) = -1;                               \
  }                                             \
  meLibSync();                                  \
}                                               \
  

#define meLibSetMinimalVmeConfig()              \
{                                               \
  hw(0xBCC00000) = -1;                          \
  hw(0xBCC00010) = 1;                           \
  while (hw(0xBCC00010)) {                      \
    meLibSync();                                \
  };                                            \
  hw(0xBCC00070) = 0;                           \
  hw(0xBCC00020) = -1;                          \
  hw(0xBCC00030) = 1;                           \
  hw(0xBCC00040) = 2; /*1*/                     \
  meLibSync();                                  \
}                                               \

#define meLibSuspendCpuIntr(var) \
  asm volatile(                  \
    "mfic  %0, $0 \n"            \
    "mtic  $0, $0 \n"            \
    "sync         \n"            \
    : "=r"(var)                  \
    :                            \
    : "$8"                       \
  )

#define meLibResumeCpuIntr(var)  \
  asm volatile(                  \
    "mtic  %0, $0 \n"            \
    "sync         \n"            \
    :                            \
    : "r"(var)                   \
  )

typedef struct Uncached32 {
  volatile u32** mem;
  void* base;
} Uncached32;

#define meLibGetUncached32(var, size)       \
  Uncached32 _##var = {&(var), NULL}; \
  meLibAllocUncached32(&_##var, size);

#define meLibFreeUncached32(var) \
  meLibAllocUncached32(&_##var, 0);

#ifdef __cplusplus
extern "C" {
#endif

  void meLibHalt();
  int  meLibSendExternalSoftInterrupt();
  
  u32 meLibGetCpuId();
  int meLibHwMutexUnlock();
  int meLibHwMutexLock();
  int meLibHwMutexTryLock();

  void meLibDcacheWritebackInvalidateAll();
  void meLibDcacheWritebackInvalidateRange(const u32 addr, const u32 size);
  void meLibDcacheInvalidateRange(const u32 addr, const u32 size);
  void meLibDcacheWritebackRange(const u32 addr, const u32 size);
  void meLibIcacheInvalidateAll();
  void meLibIcacheInvalidateRange(const u32 addr, const u32 size);
  
  int  meLibLoadPrx();
  void meLibAllocUncached32(Uncached32* const mem, const u32 size);
  
#ifdef __cplusplus
}
#endif

#endif
