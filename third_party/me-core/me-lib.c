#include "me-lib.h"


#define PRX_FILE "./kcall.prx"
extern unsigned char embedded_kcall[];
extern unsigned int embedded_kcall_len;

static int writePrx(void* start, int size) {
  // sceKernelDcacheWritebackRange(start, size);
  SceUID fd = sceIoOpen(PRX_FILE, PSP_O_WRONLY | PSP_O_CREAT, 0777);
  if (fd < 0) {
    return -1;
  }
  int _size = sceIoWrite(fd, start, size);
  sceIoClose(fd);
  if (_size != size) {
    return -1;
  }
  return 0;
}

// todo: move to me-core-kcall
int meLibLoadPrx() {
  if(writePrx(embedded_kcall, (int)embedded_kcall_len) < 0) {
    return ERROR_ON_WRITE_PRX;
  }
  if (pspSdkLoadStartModule(PRX_FILE, PSP_MEMORY_PARTITION_KERNEL) < 0) {
    return ERROR_ON_LOAD_PRX;
  }
  return 0;
}

void meLibHalt() {
  asm volatile(".word 0x70000000");
}

int meLibSendExternalSoftInterrupt() {
  asm volatile("sync");
  hw(0xBC100044) = 1;
  asm volatile("sync");
  return 0;
}

u32 meLibGetCpuId() {
  u32 unique;
  asm volatile(
    "sync\n"
    "mfc0 %0, $22\n"
    "sync"
    : "=r" (unique)
  );
  return (unique + 1) & 3;
  // reads processor id from cp0 register $22
  // 0 = main cpu
  // 1 = me
}

// kernel function to unlock the mutex
int meLibHwMutexUnlock() {
  // if acquired, briefly holds the lock with a pipeline delay,
  // allowing cache operations to complete, could be useful
  meLibDelayPipeline();
  meLibMutex = 0;
  asm volatile("sync");
  // provides opportunities for others with a pipeline delay
  meLibDelayPipeline();
  return 0;
}

// kernel function that waits and attempts to lock and acquire the mutex
int meLibHwMutexLock() {
  const u32 unique = meLibGetCpuId();
  do {
    meLibMutex = unique; // the main CPU can affect only bit[0] (0b01), while the Me can only affect bit[1] (0b10)
    asm volatile("sync");
    if (!(((meLibMutex & 3) ^ unique))) { // see note
      return 0; // lock acquired
    }
    // gives a breath with a pipeline delay (7 stages)
    meLibDelayPipeline();
  } while (1);
  return 1;
}

// kernel function to attempt locking and acquiring the mutex
int meLibHwMutexTryLock() {
  const u32 unique = meLibGetCpuId();
  meLibMutex = unique;
  asm volatile("sync");
  if (!(((meLibMutex & 3) ^ unique))) { // see note
    return 0; // lock acquired
  }
  asm volatile("sync"); // make sure to be sync before leaving kernel mode
  return 1;
}

// note:
// it appears that the main CPU can read the mutex and only set bit[0],
// while the Me can read the mutex and only set bit[1]
//
// mutex    unique
// 11  xor  01 =>   not 10 = 0
// 11  xor  10 =>   not 01 = 0
// 10  xor  01 =>   not 11 = 0
// 10  xor  10 =>   not 00 = 1
// 01  xor  01 =>   not 00 = 1
// 01  xor  10 =>   not 11 = 0

void meLibDcacheWritebackInvalidateAll() {
  asm volatile ("sync");
  for (int i = 0; i < 8192; i += 64) {
    asm volatile ("cache 0x14, 0(%0)" :: "r"(i));
    asm volatile ("cache 0x14, 0(%0)" :: "r"(i));
  }
  asm volatile ("sync");
}

void meLibDcacheWritebackInvalidateRange(const u32 addr, const u32 size) {
  asm volatile("sync");
  for (volatile u32 i = addr; i < addr + size; i += 64) {
    asm volatile(
      "cache 0x1b, 0(%0)\n"
      "cache 0x1b, 0(%0)\n"
      :: "r"(i)
    );
  }
  asm volatile("sync");
}

void meLibDcacheInvalidateRange(const u32 addr, const u32 size) {
  asm volatile("sync");
  for (volatile u32 i = addr; i < addr + size; i += 64) {
    asm volatile(
      "cache 0x19, 0(%0)\n"
      "cache 0x19, 0(%0)\n"
      :: "r"(i)
    );
  }
  asm volatile("sync");
}

void meLibDcacheWritebackRange(const u32 addr, const u32 size) {
  asm volatile("sync");
  for (volatile u32 i = addr; i < addr + size; i += 64) {
    asm volatile(
      "cache 0x1a, 0(%0)\n"
      "cache 0x1a, 0(%0)\n"
      :: "r"(i)
    );
  }
  asm volatile("sync");
}

void meLibIcacheInvalidateAll() {
  asm volatile ("sync");
  for (int i = 0; i < 8192; i += 64) {
    asm("cache 0x04, 0(%0)" :: "r"(i));
    asm("cache 0x04, 0(%0)" :: "r"(i));
  }
  asm volatile ("sync");
}

void meLibIcacheInvalidateRange(const u32 addr, const u32 size) {
  asm volatile("sync");
  for (volatile u32 i = addr; i < addr + size; i += 64) {
    asm volatile(
      "cache 0x08, 0(%0)\n"
      "cache 0x08, 0(%0)\n"
      :: "r"(i)
    );
  }
  asm volatile("sync");
}

void meLibAllocUncached32(Uncached32* const ptr, const u32 size) {
  
  if (!ptr->base) {
    
    const u32 byteCount = size * 4;
    ptr->base = memalign(64, byteCount);
    memset(ptr->base, 0, byteCount);
    sceKernelDcacheWritebackInvalidateAll();
    *(ptr->mem) = (u32*)(UNCACHED_USER_MASK | (u32)ptr->base);
    
    asm volatile (
      ".set push          \n"
      ".set noreorder     \n"
      "cache 0x1b, 0(%0)  \n"
      "sync               \n"
      ".set pop           \n"
      : : "r" (ptr->mem) : "memory"
    );
    return;
  }
  else if (!size) {
    
    free(ptr->base);
  }
  
  *(ptr->mem) = NULL;
  return;
}
