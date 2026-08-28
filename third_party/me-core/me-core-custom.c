#include "me-core-custom.h"

__attribute__((weak, noinline, aligned(4)))
void meLibOnExternalInterrupt(void) {
}

__attribute__((weak, noinline, aligned(4)))
void meLibOnInternalTimerInterrupt(void) {
  // todo: default implementation
}

__attribute__((weak, noinline, aligned(4)))
void meLibOnException(void) {
}

__attribute__((weak, noinline, aligned(4)))
void meLibOnSleep(void) {
}

__attribute__((weak, noinline, aligned(4)))
void meLibOnWake(void) {
}

__attribute__((noinline, aligned(4)))
static void meLibExceptionHandleExternalInterrupt(void) {
  asm volatile(  
    ".set noreorder                  \n"
    // save $ra
    "addi     $sp, $sp, -8           \n"
    "sw       $ra, 0($sp)            \n"
    // clear ME interrupt flag on system level
    "li       $k0, 0x80000000        \n"
    "sw       $k0, 0xbc300000($0)    \n"
    "sync                            \n"
    // wait for 0xbc300000 to be cleared (become 0)
    "1:\n"
    "lw      $k1, 0xbc300000($0)\n"
    "bne     $k1, $0, 1b\n"
    "nop\n"
    // call meLibOnExternalInterrupt
    "la       $k0, %0                \n"
    "li       $k1, 0x80000000        \n"
    "or       $k0, $k0, $k1          \n"
    "cache    0x8, 0($k0)            \n"
    "sync                            \n"
    "jal      $k0                    \n"
    "nop                             \n"
    // restore $ra
    "lw       $ra, 0($sp)            \n"
    "addi     $sp, $sp, 8            \n"
    ".set reorder                    \n"
    :
    : "i" (meLibOnExternalInterrupt)
    : "k0", "k1", "memory"
  );
}

__attribute__((noinline, aligned(4)))
static void meLibExceptionHandler(void) {
  asm volatile(
    ".set push                       \n"
    ".set noreorder                  \n"
    ".set noat                       \n"
    
    // save regs context
    "addi     $sp, $sp, -24          \n"
    "sw       $k0, 0($sp)            \n"
    "sw       $k1, 4($sp)            \n"
    "sw       $ra, 8($sp)            \n"
    "sw       $at, 16($sp)           \n"
    
    // clear EXL & ERL bits, save status and disable interrupt
    "mfc0     $k0, $12               \n"
    "li       $k1, 0xfffffff9        \n"
    "and      $k0, $k0, $k1          \n"
    "sw       $k0, 12($sp)           \n"
    "mtc0     $0, $12                \n"
    "sync                            \n"
    
    // call meLibOnException (optional callback)
    "la       $k0, %1                \n"
    "li       $k1, 0x80000000        \n"
    "or       $k0, $k0, $k1          \n"
    "cache    0x8, 0($k0)            \n"
    "sync                            \n"
    "jal      $k0                    \n"
    "nop                             \n"
    
    // check IP 7 on cause register, jump to related handler then update compare if enabled
    "mfc0    $k0, $13                \n"
    "andi    $k0, $k0, 0x8000        \n"
    "beqz    $k0, 3f                 \n"
    "nop                             \n"
    // jump to Internal Timer interrupt (IP 7)
    "la       $k0, %2                \n"
    "li       $k1, 0x80000000        \n"
    "or       $k0, $k0, $k1          \n"
    "cache    0x8, 0($k0)            \n"
    "sync                            \n"
    "jal      $k0                    \n"
    "nop                             \n"
    // update compare register
    "mfc0    $k1, $9                 \n"
    "li      $k0, 166500             \n"
    "addu    $k1, $k1, $k0           \n"
    "mtc0    $k1, $11                \n"
    "sync                            \n"
    
    "3:                              \n"
    
    // check DB on cause register, readjust epc if necessary
    "mfc0     $k0, $13               \n"
    "srl      $k1, $k0, 31           \n"
    "beqz     $k1, 2f                \n"
    "nop                             \n"
    // readjust epc (if it occured in a delay slot then replay the branching)
    "mfc0     $k1, $14               \n"
    "addiu    $k1, $k1, -4            \n"
    "mtc0     $k1, $14               \n"
    "sync                            \n"
    
    "2:                              \n"
    
    // check IP 2 on cause register, jump to related handler if enabled
    "mfc0     $k0, $13               \n"
    "andi     $k0, $k0, 0x400        \n"
    "beqz     $k0, 1f                \n"
    "nop                             \n"
    // check ME interrupt flag - unecessary
    /*
    "lw       $k0, 0xbc300000($0)    \n"
    "sync                            \n"
    "lui      $k1, 0x8000            \n"
    "and      $k0, $k0, $k1          \n"
    "beq      $k0, $zero, 1f         \n"
    "nop                             \n"
    */
    // jump to the related handler if IP 2 or ME interrupt flag is enabled 
    "la       $k0, %0                \n"
    "li       $k1, 0x80000000        \n"
    "or       $k0, $k0, $k1          \n"
    "cache    0x8, 0($k0)            \n"
    "sync                            \n"
    "jal      $k0                    \n"
    "nop                             \n"
    
    "1:                              \n"
    
    // restore status with external interrupt enabled
    "lw       $k0, 12($sp)            \n"
    "mtc0     $k0, $12               \n"
    "sync                            \n"
    // restore regs context
    "lw       $k0, 0($sp)            \n"
    "lw       $k1, 4($sp)            \n"
    "lw       $ra, 8($sp)            \n"
    "lw       $at, 16($sp)           \n"
    "addi     $sp, $sp, 24           \n"
    // avoid pipeline hazards
    "nop                             \n"
    "nop                             \n"
    // exit
    "eret                            \n"
    // avoid potential pipeline timing / flush issues
    "nop                             \n"
    "nop                             \n"
    "nop                             \n"
    "nop                             \n"
    "nop                             \n"
    "nop                             \n"
    "nop                             \n"
    ".set pop                        \n"
    :
    : "i" (meLibExceptionHandleExternalInterrupt), "i" (meLibOnException), "i" (meLibOnInternalTimerInterrupt)
    : "k0", "k1", "memory"
  );
}

__attribute__((noinline, aligned(4)))
void meLibExceptionHandlerInit(const u8 ip7) {
  u32 interrupts = 0x401;
  if (ip7) {
    interrupts = 0x8401;
  }
  SET_SRAM_SHARED_VAR(0, 0);
  asm volatile(
    ".set noreorder                  \n"
    // setup exception handler
    "la       $k0, %1                \n"
    "li       $k1, 0x80000000        \n"
    "or       $k0, $k0, $k1          \n"
    "cache    0x8, 0($k0)            \n"
    "sync                            \n"
    // load exception handler
    "mtc0     $k0, $25               \n"
    "sync                            \n"
    // enable ME interrupt on system level
    "li       $k0, 0x80000000        \n"
    "sw       $k0, 0xbc300008($0)    \n"
    "sync                            \n"
    // clear interrupts flag on system level
    "li       $k0, 0xffffffff        \n"
    "sw       $k0, 0xbc300000($0)    \n"
    "sync                            \n"

    "move     $k1, %0                \n"
    
    // check timer interrupt (IP 7) on status, init compare and count if enabled
    "andi     $k0, $k1, 0x8000       \n"
    "beqz     $k0, 1f                \n"
    "nop                             \n"
    // init compare and count registers
    "mtc0    $0, $9                  \n"
    "sync                            \n"
    "li      $k0, 166500              \n"
    "mtc0    $k0, $11                 \n"
    "sync                            \n"
    
    "1:                              \n"

    // setup external interrupt on cp0 level
    "mfc0     $k0, $12               \n"
//  "li       $k1, 0x401             \n"
    "or       $k0, $k0, $k1          \n"
    "mtc0     $k0, $12               \n"
    "sync                            \n"
    ".set reorder                    \n"
    :
    : "r"(interrupts), "i" (meLibExceptionHandler)
    : "k0", "k1", "memory"
  );
}

u32 SC_HW_RESET = 0x14;

void meLibReset() {

  #define me_section_size (&__stop__me_section - &__start__me_section)
  memcpy((void*)ME_HANDLER_BASE, (void*)&__start__me_section, me_section_size);
  
  sceKernelDcacheWritebackInvalidateAll();
  sceKernelIcacheInvalidateAll();
  
  HW_SYS_RESET_ENABLE = SC_HW_RESET; // 0x14;
  HW_SYS_RESET_ENABLE = 0x00;
  meLibSync();
}

static inline int meLibInit() {
  const int tableId = meCoreGetTableIdFromWitnessWord();
  if (tableId < 2) { // tmp bl and sd img
    return ERROR_ON_ME_IMG;
  }
  meCoreSelectSystemTable(tableId);
  
  meLibReset();
  return tableId;
}

__attribute__((noinline, aligned(4)))
static int eventHandler(int eventId) {
    
  static unsigned int lastId = 0;
  if ((eventId == 0x00000402) && (lastId != 0x00000402)) {
    meLibOnSleep();
    lastId = 0x00000402;
  }
  else if ((eventId == 0x00010005) && (lastId != 0x00010005)) {
    meLibOnWake();
    lastId = 0x00010005;
  }  
  return 0;
}

int meLibDefaultInit() {
  sceKernelDcacheWritebackInvalidateAll();
  sceKernelIcacheInvalidateAll();
  int error = meLibLoadPrx();
  if (error < 0) {
    return error;
  }
  if(kinit((void*)eventHandler) < 0) {
    return ERROR_ON_KINIT;
  };
  return kcall(meLibInit, 0);
}
