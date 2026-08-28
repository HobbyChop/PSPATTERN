#include "kcall.h"
#include <pspsdk.h>
#include <pspsysevent.h>
#include <psputilsforkernel.h>


PSP_MODULE_INFO("kcall", 0x1006, 1, 1);
PSP_NO_CREATE_MAIN_THREAD();

int kcall(FCall const f, const unsigned int seg) {
  const unsigned int addr = (seg | (unsigned int)f);
  sceKernelIcacheInvalidateAll();
  /*
  switch (seg) {
    case 1: return ((FCall)(0x80000000 | (unsigned int)f))();
    case 2: return ((FCall)(0x40000000 | (unsigned int)f))();
    case 3: return ((FCall)(0xa0000000 | (unsigned int)f))();
  }
  */
  return ((FCall)addr)();
}

int kcall(FPCall const f, const unsigned int seg, void* const param) {
  const unsigned int addr = (seg | (unsigned int)f);
  sceKernelIcacheInvalidateAll();
  return ((FPCall)addr)(param);
}

int kinit(const void* const handler) {
  PspSysEventHandler* seh = sceKernelReferSysEventHandler();
  while (seh != NULL) {
    if (seh->name[3] == 'M' && seh->name[4] == 'e' && seh->name[5] == 'R') {
      seh->handler = (PspSysEventHandlerFunc)handler;
      
      // sceKernelUnregisterSysEventHandler(seh);
      // meLibRpc.handler = (PspSysEventHandlerFunc)handler;
      // sceKernelRegisterSysEventHandler(&meLibRpc);
      
      return 0;
    }
    seh = seh->next;
  }
  return -1;
}

int module_start(SceSize args, void *argp) {
  return 0;
}

int module_stop() {
  return 0;
}
