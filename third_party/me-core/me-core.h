#ifndef ME_CORE_H
#define ME_CORE_H

#include "me-core-custom.h"

__attribute__((noinline, aligned(4)))
void meLibOnPreProcess() {
  meLibDcacheInvalidateRange(ME_CORE_BASE_ADDR, (0x90000 + 63) & ~63);
  meLibIcacheInvalidateRange(ME_CORE_BASE_ADDR, (0x90000 + 63) & ~63);
  
  hw(0xbc200000) = 511 << 16 | 511;
  hw(0xBC200004) = 511 << 16 | 511;
  hw(0xBC200008) = 511 << 16 | 511;
  meLibSync();
  
  meLibOnProcess();
}

__attribute__((section("_me_section"), used))
void meLibHandler() { // size 0x108 (264)
  HW_SYS_BUS_CLOCK_ENABLE      = 0x0f;
  HW_SYS_TACHYON_CONFIG_STATUS |= 0x02;
  HW_SYS_NMI_FLAGS             = 0xffffffff;
  meLibSync();
  
  meLibUnlockHwUserRegisters();
  meLibUnlockMemory();
  
  #ifndef DISABLE_VME_MINIMAL_CONFIG
  meLibSetMinimalVmeConfig();
  #endif
  
  asm volatile(
    ".set noreorder                  \n"
    "li             $k0, 0x30000000  \n"
    "mtc0           $k0, $12         \n"
    "sync                            \n"

    "li             $k0, 0x279c637c  \n"
    "lw             $k1, 0x88300018  \n"
    "beq            $k0, $k1, 1f     \n"
    "nop                             \n"
    "li             $sp, 0x80200000  \n"
    "b              2f               \n"
    "nop                             \n"
    "1:                              \n"
    "li             $sp, 0x80400000  \n"
    "2:                              \n"

    "la             $k0, %0          \n"
    "li             $k1, 0x80000000  \n"
    "or             $k0, $k0, $k1    \n"
    
    "cache          0x8, 0($k0)      \n"
    "sync                            \n"
        
    "jr             $k0              \n"
    "nop                             \n"
    ".set reorder                    \n"
    :
    : "i" (meLibOnPreProcess)
    : "k0", "k1", "memory"
  );
}

#endif
