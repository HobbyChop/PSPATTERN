#ifndef ME_CORE_CUSTOM_H
#define ME_CORE_CUSTOM_H

#include "me-core-mapper.h"
#include "me-lib.h"

extern char __start__me_section;
extern char __stop__me_section;
extern u32 SC_HW_RESET;

extern void meLibOnProcess(void);

#ifdef __cplusplus
extern "C" {
#endif

  extern void meLibSaveContext(void);
  extern void meLibRestoreContext(void);
  extern void meLibOnExternalInterrupt(void);
  extern void meLibOnException(void);
  extern void meLibOnSleep(void);
  extern void meLibOnWake(void);
  void meLibReset();
  int  meLibDefaultInit();
  void meLibExceptionHandlerInit(const u8 ip7);

#ifdef __cplusplus
}
#endif

#define ME_LIB_SETUP_SIMPLE_SUSPEND_HANDLER()     \
extern "C" __attribute__((noinline, aligned(4)))  \
void meLibOnSleep() {                             \
                                                  \
  HW_SYS_RESET_ENABLE = SC_HW_RESET;              \
  meLibSync();                                    \
}                                                 \
                                                  \
extern "C" __attribute__((noinline, aligned(4)))  \
void meLibOnWake() {                              \
                                                  \
  HW_SYS_RESET_ENABLE = SC_HW_RESET;              \
  HW_SYS_RESET_ENABLE = 0;                        \
  meLibSync();                                    \
}

#define ME_LIB_SETUP_DEFAULT_SUSPEND_HANDLER() \
extern "C" __attribute__((noinline, aligned(4))) \
void meLibOnExternalInterrupt(void) { \
  asm volatile( \
    ".set push                       \n" \
    ".set volatile                   \n" \
    ".set noreorder                  \n" \
    ".set noat                       \n" \
    \
    /* save regs context */ \
    "addi     $sp, $sp, -16          \n" \
    "sw       $k0, 0($sp)            \n" \
    "sw       $k1, 4($sp)            \n" \
    "sw       $ra, 8($sp)            \n" \
    "sw       $at, 12($sp)           \n" \
    \
    /* if SRAM_SHARED_VAR_0 equal 1 */   \
    "li       $k0, %2                \n" \
    "cache    0x8, 0($k0)            \n" \
    "sync                            \n" \
    "lw       $k1, 0($k0)            \n" \
    "li       $k0, 1                 \n" \
    "bne      $k1, $k0, 1f           \n" \
    "nop                             \n" \
    \
    /* call meLibSaveContext */ \
    "la       $k0, %0                \n" \
    "li       $k1, 0x80000000        \n" \
    "or       $k0, $k0, $k1          \n" \
    "cache    0x8, 0($k0)            \n" \
    "sync                            \n" \
    "jal      $k0                    \n" \
    "nop                             \n" \
    "1:                              \n" \
    \
    /* if SRAM_SHARED_VAR_0 equal 2 */ \
    "li       $k0, %2                \n" \
    "lw       $k1, 0($k0)            \n" \
    "li       $k0, 2                 \n" \
    "bne      $k1, $k0, 2f           \n" \
    "nop                             \n" \
    \
    /* call meLibRestoreContext */ \
    "la       $k0, %1                \n" \
    "li       $k1, 0x80000000        \n" \
    "or       $k0, $k0, $k1          \n" \
    "cache    0x8, 0($k0)            \n" \
    "sync                            \n" \
    "jal      $k0                    \n" \
    "nop                             \n" \
    "2:                              \n" \
    \
    /* reset SRAM_SHARED_VAR_0 */ \
    "li       $k0, %2                \n" \
    "sw       $0, 0($k0)             \n" \
    "sync                            \n" \
    /*"1:                              \n"*/ \
    /*"lw       $k1, 0($k0)            \n"*/ \
    /*"bne      $k1, $0, 1b            \n"*/ \
    /*"nop                             \n"*/ \
    \
    /* restore regs context */ \
    "lw       $k0, 0($sp)            \n" \
    "lw       $k1, 4($sp)            \n" \
    "lw       $ra, 8($sp)            \n" \
    "lw       $at, 12($sp)           \n" \
    "addi     $sp, $sp, 16           \n" \
    \
    ".set pop                        \n" \
    : \
    :  "i" (meLibSaveContext), "i" (meLibRestoreContext), "i" (SRAM_SHARED_VAR_0) \
    : "k0", "k1", "memory" \
  ); \
} \
\
extern "C" __attribute__((noinline, aligned(4)))  \
void meLibOnSleep() {                             \
  SET_SRAM_SHARED_VAR(0, 1);                      \
  meLibSendExternalSoftInterrupt();               \
  while (GET_SRAM_SHARED_VAR(0)) {                \
    meLibDelayPipeline();                         \
  }                                               \
  HW_SYS_RESET_ENABLE = SC_HW_RESET;              \
  meLibSync();                                    \
}                                                 \
                                                  \
extern "C" __attribute__((noinline, aligned(4)))  \
void meLibOnWake() {                              \
  SET_SRAM_SHARED_VAR(0, 3);                      \
  HW_SYS_RESET_ENABLE = SC_HW_RESET;              \
  HW_SYS_RESET_ENABLE = 0x00;                     \
  meLibSync();                                    \
  while (GET_SRAM_SHARED_VAR(0)) {                \
    meLibDelayPipeline();                         \
  }                                               \
  SET_SRAM_SHARED_VAR(0, 2);                      \
  meLibSendExternalSoftInterrupt();               \
}

#endif

