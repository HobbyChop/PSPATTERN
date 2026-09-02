/* User-side syscall stubs so the app can call the kcall.prx kernel
   backlight bridge (library "kcall", flags 0x40090000). The prebuilt
   libme-core kcall stub still provides kcall_2/kcall_3/kinit; this
   adds the two brightness calls. NIDs from the prx export table. */
#include "pspstub.s"
	STUB_START "kcall", 0x40090000, 0x00030005
	STUB_FUNC  0xBC00D799, meSetBrightness
	STUB_FUNC  0x2DF93A6D, meGetBrightness
	STUB_FUNC  0x1B996C9D, meSetPowerLED
	STUB_END
