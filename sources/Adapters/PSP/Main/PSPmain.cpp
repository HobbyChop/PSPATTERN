// VC6GUI.cpp : Defines the entry point for the application.
//

#include "Application/Application.h"
#include "Application/AppWindow.h"
#include "Application/Model/Config.h"
#include "Adapters/PSP/System/PSPSystem.h"
#include "Foundation/T_Singleton.h"
#include <SDL/SDL.h>
#include <string.h>
#include "Adapters/SDL/GUI/SDLGUIWindowImp.h"
#include "Application/Persistency/PersistencyService.h" 
#include "Adapters/SDL/GUI/SDLGUIWindowImp.h"

#include <pspkernel.h>
#include <pspdebug.h>
#include <psppower.h>
#include "Adapters/PSP/Midi/PSPUsbMidiLink.h"

/* Define the module info section */
PSP_MODULE_INFO("PSPATTERN", 0, 1, 1);
/* Define the main thread's attribute value (optional) */
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
/* Define printf, just to make typing easier */

/* Define printf, just to make typing easier */
#define printf	pspDebugScreenPrintf

/* 
   This part of the code is more or less identical to the sdktest sample 
*/

/* Define printf, just to make typing easier */
#define printf	pspDebugScreenPrintf

/* Exit callback */
int exitCallback(int arg1, int arg2, void *common) {
	sceKernelExitGame();
	return 0;
}

/* Suspend / resume.

   The callback runs on the callback thread while the rest of the app
   is about to be frozen, so it does the minimum that must happen
   before the CPU stops (silence the audio device) and defers the
   rest to the main thread through a flag + a wake-up event.

   On the way back: the USB device end needs re-arming for the MIDI
   adapter, the audio device needs un-pausing, and the renderer only
   repaints cells that changed — so the whole diff cache has to be
   invalidated or the LCD stays stale/black. */
static volatile int g_resumePending = 0;

int powerCallback(int unknown, int pwrflags, void *common) {

	if (pwrflags & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY)) {
		// stop feeding the DAC before the world freezes, or the last
		// buffer loops audibly as the machine goes down
		SDL_PauseAudio(1);
	}

	if (pwrflags & PSP_POWER_CB_RESUME_COMPLETE) {
		g_resumePending = 1;
		SDL_Event event;
		event.type = SDL_VIDEOEXPOSE;   // wakes the blocked event loop
		SDL_PushEvent(&event);
	}
	return 0;
}

/* Called from the main thread once the event loop wakes up after a
   resume — everything here needs to run outside the callback. */
void PSPHandleResume() {
	if (!g_resumePending) return;
	g_resumePending = 0;

	SDL_PauseAudio(0);
	PSPUsbMidiLink::OnResume();

	GUIWindow *w = Application::GetInstance()->GetWindow();
	if (w) {
		((AppWindow *)w)->InvalidateScreen();
	}
}

/* Callback thread */
int callbackThread(SceSize args, void *argp) {
	int cbid;

	cbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
	sceKernelRegisterExitCallback(cbid);
	cbid = sceKernelCreateCallback("Power Callback", powerCallback, NULL);
	scePowerRegisterCallback(0, cbid);
	sceKernelSleepThreadCB();

	return 0;
}

/* Sets up the callback thread and returns its thread id */
int setupCallbacks(void) {
	int thid = 0;

	thid = sceKernelCreateThread("update_thread", callbackThread, 0x11, 0xFA0, 0, 0);
	if (thid >= 0) {
		sceKernelStartThread(thid, 0, 0);
	}
	return thid;
}

/* The main thread's priority, captured once at startup.

   The audio render runs on an SDL worker thread, and SDL creates its
   workers ABOVE the main thread. So a heavy block would render at a
   priority the main thread cannot preempt, and while it ran the main
   thread -- which handles input and paints the scope and the meters --
   got no slices at all. That is the UI freezing under high DSP.

   The render thread reads this and drops itself to match, so the two
   round-robin instead: the main thread keeps its slices for input and
   drawing, and the audio buffer's lead (six blocks pre-buffered)
   absorbs the render being interleaved. The SDL output callback that
   actually feeds the DAC is a different thread and stays above both,
   so the sound is not at risk from this. See SDLAudioDriver.cpp. */
extern "C" { int g_pspMainThreadPriority = 0x20 ; }

#ifdef PSP_ME_OFFLOAD
extern "C" int  PSPME_Init(void) ;
extern "C" void PSPME_Shutdown(void) ;
#endif

int main(int argc,char *argv[])
{

	setupCallbacks();

	// Capture our own priority before any worker thread starts.
	g_pspMainThreadPriority = sceKernelGetThreadCurrentPriority() ;

	/* The power switch is left alone: suspend is handled in
	   powerCallback / PSPHandleResume. (Earlier builds held
	   scePowerLock(0) to dodge a broken resume — that also made the
	   hardware suspend switch do nothing at all.) */

	/* The PSP boots at 222MHz. Eight voices of unison saws through an SVF
	   is the most expensive thing this app does, so take the full clock --
	   the sibling synths in this family do the same. */
	scePowerSetClockFrequency(333,333,166) ;

	PSPSystem::Boot(argc,argv) ;

#ifdef PSP_ME_OFFLOAD
	// ME send-FX offload, runtime-toggleable (config ME_OFFLOAD, default
	// on). When off we simply never start the ME: SendFx::Return::Render
	// sees PSPME_Ready()==0 and runs the send bank on the main core
	// (processBank) by itself. Gating INIT -- not a start-then-halt --
	// is the standby-safe way to disable it.
	const char *meCfg=Config::GetInstance()->GetValue("ME_OFFLOAD") ;
	bool meOn=!(meCfg && meCfg[0]=='N') ;
	if (meOn) PSPME_Init() ;
#endif

	SDLCreateWindowParams params ;
	params.title="littlegptracker" ;
	params.cacheFonts_=false ;
    params.framebuffer_=false ;
	Application::GetInstance()->Init(params) ;
	PSPSystem::MainLoop() ;
    PSPSystem::Shutdown() ;
#ifdef PSP_ME_OFFLOAD
	if (meOn) PSPME_Shutdown() ;   // matched teardown (skipped if never started)
#endif
	scePowerUnlock(0);
	sceKernelExitGame();
	return 0 ;
}

