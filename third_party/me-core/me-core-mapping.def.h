// Do not include this file - it serves as source data for the awk converter to ease function address mapping additions
// Note that the naming of the functions are not definitive
{
  {0x000002e4, 0x00000000}, // void _meCoreUnknown_000002e4(u32, u32)             // meCoreSystemImageCopy AndReset ? // from what, to where ?
  {0x00000374, 0x00000000}, // void _meCoreWaitForScSyscalls(void)                // seems to handle syscall from sc
  {0x00000570, 0x00000000}, // int  _meCoreUnknown_00000570(u32, u32)             // meCoreSetDSPFrequency ? // meCoreConfigureTransferMode(int, int) ?
  {0x00000618, 0x00000000}, // int  _meCoreUnknown_00000618(u32, u32, u32, u32)   // _meCoreDMACPrimMuxUnknown_00000618
  {0x00000708, 0x00000000}, // void _meCoreUnknown_00000708(u32, u32, u32, u32, u32, u32, u32, u32)
  {0x00000948, 0x00000000}, // int  _meCoreUnknown_00000948()                     // _meCoreDspActivateAndSync() ?
  {0x00000af0, 0x00000000}, // u32   _meCoreGenerate2DBlockFromQuad(/*Quad*/u32 *buff)
  {0x00000c08, 0x00000000}, // int   _meCoreGetDualBuffers(u32 width, u32 height, u32 *buff0, u32 *buff1) // width = width or width | mode (16 [0b10000])
  {0x00000c64, 0x00000000}, // void  _meCoreCopy2DMemoryBlock(/*Quad*/u32 *buff, void *src, u32 height, u32 destX, u32 destY, u32 stride, u32 extra) // extra et to one adds a padding ? 
  {0x00000dbc, 0x00000000}, // void  _meCoreInterleave2DBlockPixelData(/*Quad*/u32 *buff, void *src, u32 height, u32 destX, u32 destX, u32 stride, u32 extra)
  {0x00001054, 0x00000000}, // u32   _meCoreCompose2DBlockFromQuad(/*Quad*/u32 *buff)

  {0x000018c4, 0x00001890}, // void  _meCoreBusClockAWEdramEnable(void)
  {0x000018fc, 0x000018c8}, // void _meCoreBusClockEnableDMACPrimMux()
  {0x00001918, 0x000018e4}, // int  _meCoreBusClockDisableDMACPrimMux()
  {0x0000196c, 0x00000000}, // void _meCoreBusClockEnableKirk(void)               // _meCoreBusClockEnableH264(void) ?
  {0x00001988, 0x00000000}, // void _meCoreBusClockDisableKirk(void)              // _meCoreBusClockPreserveH264(void) ?
  {0x000019a4, 0x00001970}, // int  _meCoreBusClockEnable(u32)
  {0x000019f4, 0x000019c0}, // int  _meCoreBusClockPreserve(u32)                  // takes a mask which preserves the specified bits
  {0x00001a38, 0x00000000}, // int  _meCoreBusClockStorageEnableATAHDD()          // dsp related clock instead ?
  {0x00001a58, 0x00000000}, // void _meCoreBusClockStorageDisableATAHDD()         // dsp related clock instead ?
  
  {0x00001a78, 0x00001a44}, // void _meCoreBusClockEnableVMECtrl()
  {0x00001a98, 0x00001a64}, // void _meCoreBusClockDisableVMECtrl()
  
  {0x00001ab8, 0x00000000}, // int  _meCoreBusClockStorageEnableDisable(u32, u32) // mask, value
  {0x00001b14, 0x00000000}, // int  _meCoreBusClockMemoryStickSelect(u32)
  
  {0x00001b7c, 0x00001b48}, // int  _meCoreSetCpuFrequency(u32 num, u32 den);
  {0x00001bc4, 0x00001b90}, // int  _meCoreSetBusFrequency(u32 num, u32 den);

  // dmac, vme
  {0x000209d0, 0x000122f0}, // void _meCoreDMACPrimWaitVMEFinish(void)
  {0x00020a08, 0x00012328}, // void _meCoreDMACPrimMuxSetConfig(u32)              // unclear
  {0x00020ca8, 0x000125c8}, // void _meCoreDMACPrimMuxSetCtrl_0x002(void)         // init
  {0x00020cb8, 0x000125d8}, // void _meCoreDMACPrimMuxSetCtrl_0x008(void)         // ?
  {0x00020cc8, 0x000125e8}, // void _meCoreDMACPrimMuxSetCtrl_0x003(void)         // ?
  {0x00020cd8, 0x000125f8}, // void _meCoreDMACPrimMuxSetCtrl_0x018(void)         // start  
  {0x00020ce8, 0x00012608}, // void _meCoreDMACPrimSendDefaultVMEContext(void)
  
  {0x000210b4, 0x000129d4}, // void _meCoreDMACPrimaryProcessUnknown_000210b4(u32 memSrc, u32 bufSrc, u32 blockCount, u32 lastBlockIndex, u32 memDst, u32 bufDstLow, u32 bufDstHigh, u32 dstCount) // not sure
  {0x00021168, 0x00012a88}, // void _meCoreDMACPrimMuxWaitStatus(u32)
  {0x000212cc, 0x00012bec}, // u32* _meCoreMemcpy(u32*, u32*, u32)                // dst, src, size
  {0x00021338, 0x00012c58}, // u32* _meCoreMemset(u32*, u32, u32)                 // dst, value, size

  {0x00021614, 0x00012f34}, // void* _meCoreEDRAMAlloc(u32) // from 0x00180000 on slim
  {0x000216f8, 0x00013018}, // u32   _meCoreEDRAMFree(void*)

  {0x00021884, 0x00000000}, // u32* _meCoreEDRAMHeapInit(void)

  // task related ?
  {0x00021910, 0x00000000}, // u32  _meCore32PoolsEnqueueEntry(u32 index, u32 priority, int* status)  
  {0x000219b4, 0x00000000}, // void _meCore32PoolsInitMemory(void)
  {0x00021a30, 0x00000000}, // int  _meCore32PoolsUpdateDispatchQueues(void)

  {0x00021ad8, 0x000121f0}, // int  _meCoreInterruptSetMask(u32)
  {0x00021ae4, 0x000121fc}, // int  _meCoreInterruptClearMask(void)
  {0x00021af4, 0x00000000}, // int  _meCoreInterruptSetMask_2(u32) // ?
  {0x00021b34, 0x00012288}, // u64  _meCoreExceptionInitHandlers(void)
  {0x00021b7c, 0x00000000}, // int  _meCorePriorityQueueDefaultInsert(void)
  {0x00021bbc, 0x00013230}, // int  _meCoreEmitSoftwareInterrupt(void)

  // mutex
  {0x00021bd8, 0x0001324c}, // int  _meCoreHwMutexTryLock(void)
  {0x00021c04, 0x00013278}, // int  _meCoreHwMutexUnlock(void)
  {0x00021c20, 0x00013294}, // u32  _meCoreHwMutexAtomicRead(u32*)                          // target
  {0x00021c6c, 0x000132e0}, // u32  _meCoreHwMutexAtomicWrite(u32*, u32, u32)               // target, mask, value

  {0x00021cd4, 0x00000000}, // int   _meCoreProcessAudioMixer(/*AudioMixer*/u32 *mixer, u32 *output, u32 config)
  {0x00021e3c, 0x00000000}, // u32*  _meCoreProcessMultiChannelAudio(u32 *output, /*AudioMixer*/u32 *mixer, int effect)

  // todo: review
  {0x000230f4, 0x00000000}, // void  _meCoreConfigureReverbEffect(u32* reverbState, u32 config);
  {0x00023494, 0x00000000}, // u32*  _meCoreProcessReverbEffectSamples(u32** effectState, u2 *in, u32 *out)

  {0x00038d6c, 0x00000000}, // u32   _meCoreIsIndexValidFor(u32 index, u32* in)
  {0x00039d04, 0x00000000}, // u32   _meCoreGetIndexedValueFor(int index, u32 *out, void *in)

  {0x00065908, 0x00000000}, // u32  _meCoreCheckAndCopy164bytesFromOffset0x8308(u32, u32)   // ??
  {0x00065a10, 0x00000000}, // bool _meCoreCheck0xdec264(u32*)                              // what is the purpose ?
  {0x00066804, 0x00000000}, // u32  _meCoreCopy164bytesFromOffset0x8308(u32, u32*)          // ??

  {0x0006e854, 0x00000000}, // void  _meCoreAllocateSlotFrom(/*SlotPool*/u32 **slotPool, u32 index)
  {0x0006f098, 0x00000000}, // u32*  _meCoreAllocateSlot(/*SlotPool*/u32 *slotPool)
  
  // typedef struct { u32 unk0; u32 unk1; u32 unk2; u32 count; /*0x0c*/ u32 unk4; u32 **slots; /*0x14*/ } SlotPool;

  // cache
  {0x0008bc10, 0x0007b3bc}, // int  _meCoreDcacheWritebackInvalidateAll(void)
  {0x0008bc38, 0x0007b3e8}, // int  _meCoreDcacheWritebackInvalidateRange(u32, u32)
  {0x0008bcbc, 0x0007b46c}, // int  _meCoreDcacheInvalidateRange(u32, u32)
  {0x0008bd4c, 0x0007b4fc}, // int  _meCoreDcacheWritebackRange(u32, u32)
  {0x0008be64, 0x00000000}, // int  _meCoreDecompressKL4E(byte *out, int size, char *in, u32 *cursor)

  // wip
  {0x000218a8, 0x00000000}, // int  _meCoreEDRAMAllocWords(uint count, uint size)
  {0x00021884, 0x00000000}, // u32* _meCoreEDRAMHeapInit(void)
  
  // typedef struct { u32 width; u32 height; u32 unk2; void* src0; void* src1; void* dst0; void* dst1; void* src2; void* src3; void* dst2; void* dst3; void* dst; void* inter0; void* inter1; } Quad;
  // typedef struct {u8  blockCount; /*0x8*/ u8  effectsEnabled; /*0x9*/ AudioChannel channels[32]; /*0x714*/ u32 channelMask; /*0xe14*/ u16  global1; /*0xe1a*/ u16  global2; /*0x387*4*/ u16  config; /*0x386*4*/ u32* reverbEffect; } AudioMixer;

  {0x00020a2c, 0x0001234c}, // void _meCoreDMACPrimMemoryToRingBuffer(void* src, u32 dst offset, u32 count);
  {0x00020a5c, 0x0001237c}, // void _meCoreDMACPrimRingBufferToMemory(void* dst, u32 src offset, u32 count);
  
  {0x000209b4, 0x000122d4}, // void _meCoreDMACPrimWaitTransferFinish();
  {0x00020c84, 0x000125a4}, // void _meCoreDMACPrimPreloadVMEContext);
}
