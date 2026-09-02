// VC6GUI.cpp : Defines the entry point for the application.
//

#include "Application/Application.h"
#include "System/Console/Trace.h"
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
#include <pspctrl.h>
#include <pspge.h>
#include <stdarg.h>

#ifdef PSP_ME_OFFLOAD
extern "C" unsigned int PSPME_Ready(void);
extern "C" unsigned int PSPME_SleepCount(void);
extern "C" unsigned int PSPME_WakeCount(void);
#endif
#include <psppower.h>
#include <pspaudio.h>
#include <pspimpose_driver.h>
#include "Adapters/PSP/Midi/PSPUsbMidiLink.h"
#include "Adapters/PSP/Audio/embedded_kcall_v2.inc"  // kcall.prx v5 (backlight)

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
extern "C" void PSPME_OnResume(void) ;
extern "C" void SDLGUI_MarkGuLost(void) ;

static volatile int g_resumePending = 0;

/* QUASI-STANDBY. Real standby is off the table while our code runs on
   the Media Engine (Sony's suspend can't re-init it safely), so the
   power switch is repurposed: instead of a true suspend it drops the
   machine into a low-power REST -- screen and clock down, audio and
   the second core idle, everything still resident so waking is
   instant and needs no re-init. scePowerLock(0) at boot keeps the
   real (broken) suspend from ever firing; the switch still notifies
   us, and we toggle rest on each slide. Not a true standby: it keeps
   draining slowly, so we auto-save on the way down and beep on the
   way to empty. */
static volatile int g_quasiToggleReq = 0;
static volatile unsigned int g_lastSwitchUs = 0;
extern "C" int pspQuasiWakeRequested(void) { return g_quasiToggleReq; }
extern "C" void pspQuasiClearWake(void)    { g_quasiToggleReq = 0; }

int powerCallback(int unknown, int pwrflags, void *common) {

	if (pwrflags & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY)) {
		// stop feeding the DAC before the world freezes, or the last
		// buffer loops audibly as the machine goes down
		SDL_PauseAudio(1);
	}

	if (pwrflags & PSP_POWER_CB_POWER_SWITCH) {
		// the power switch: our quasi-standby toggle, debounced so one
		// physical slide is one toggle (the flag can fire press+release)
		unsigned int now = sceKernelGetSystemTimeLow();
		if (now - g_lastSwitchUs > 700 * 1000) {
			g_lastSwitchUs = now;
			g_quasiToggleReq = 1;
			SDL_Event event;
			event.type = SDL_VIDEOEXPOSE;   // wake the event loop
			SDL_PushEvent(&event);
		}
	}

	if (pwrflags & PSP_POWER_CB_RESUME_COMPLETE) {
		g_resumePending = 1;
		SDL_Event event;
		event.type = SDL_VIDEOEXPOSE;   // wakes the blocked event loop
		SDL_PushEvent(&event);
	}
	return 0;
}

/* A short low beep on a spare audio channel -- used only in rest, to
   warn of a nearly-empty battery (rest is not true standby and will
   drain flat). Self-contained: reserves its own channel so it works
   while the app's audio is paused. */
/* A soft triangle tone on a spare audio channel -- gentler than a
   square, and independent of the app's (paused) audio. */
static void quasiTone(int ch, int freqHz, int ms, int amp) {
	if (ch < 0 || freqHz < 20) return;
	static short buf[1024 * 2];
	int period = 44100 / freqHz;
	int half = period > 1 ? period / 2 : 1;
	for (int i = 0; i < 1024; i++) {
		int ph = i % period;
		int t = (ph < half) ? ph : (period - ph);     // 0..half..0
		short v = (short)((t * 2 * amp / half) - amp); // triangle
		buf[i * 2] = v; buf[i * 2 + 1] = v;
	}
	int reps = (ms * 44100 / 1000) / 1024;
	if (reps < 1) reps = 1;
	for (int r = 0; r < reps; r++)
		sceAudioOutputBlocking(ch, 0x1800, buf);
}

/* Two little signatures: a descending "resting" chirp on the way down,
   an ascending "awake" chirp on the way back -- each distinct so the
   ear knows which happened without looking. */
static void quasiMelody(const int *freqs, int n) {
	int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024,
	                           PSP_AUDIO_FORMAT_STEREO);
	if (ch < 0) return;
	for (int i = 0; i < n; i++) quasiTone(ch, freqs[i], 110, 2200);
	sceAudioChRelease(ch);
}
static void quasiSleepTone(void) { const int f[] = {784, 587, 440}; quasiMelody(f, 3); }
static void quasiWakeTone(void)  { const int f[] = {440, 587, 784}; quasiMelody(f, 3); }

/* Low-battery warning beep in rest, unchanged intent. */
static void quasiBeep(void) {
	int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024,
	                           PSP_AUDIO_FORMAT_STEREO);
	if (ch < 0) return;
	quasiTone(ch, 262, 140, 2600);   // a low, plain note
	sceAudioChRelease(ch);
}

/* Wake condition in rest: the power switch OR any button. Buttons are
   armed only after they are first seen released, so a button still
   held from before rest does not wake it instantly. */
static int g_quasiBtnArmed = 0;
static int quasiWakeNow(void) {
	if (pspQuasiWakeRequested()) return 1;
	SceCtrlData pad;
	sceCtrlPeekBufferPositive(&pad, 1);
	unsigned int b = pad.Buttons;
	if (!g_quasiBtnArmed) { if (b == 0) g_quasiBtnArmed = 1; return 0; }
	return (b != 0) ? 1 : 0;
}

/* Enter low-power rest and stay until the next power-switch slide.
   Runs on the main thread (drawing + clock changes must). */
extern "C" int meSetBrightness(int level);
extern "C" int meGetBrightness(void);

void PSPHandleQuasiStandby(void) {
	AppWindow *w = (AppWindow *)Application::GetInstance()->GetWindow();
	if (!w) return;

	// 1. protect the work: rest is not true standby and can drain flat
	PersistencyService::GetInstance()->Save("project:lgptsav.autosav");

	// 2. rough life estimate from the battery percent (a guess, labelled)
	int pct = scePowerGetBatteryLifePercent();
	if (pct < 0) pct = 0; if (pct > 100) pct = 100;
	int estMin = pct * 12 * 60 / 100;

	int savedBright = meGetBrightness();
	if (savedBright <= 0) savedBright = 84;   // restore to something visible
	int savedBl = sceImposeGetBacklightOffTime();
	g_quasiBtnArmed = 0;
	pspQuasiClearWake();

	// 3. a descending chirp on the way down
	SDL_PauseAudio(1);
	quasiSleepTone();

	// 4. ONE screen: the warning IS the countdown, so the wait before
	//    blackout is just the countdown. Minimum clock; a slide OR any
	//    button wakes at once.
	scePowerSetClockFrequency(33, 33, 16);
	const int SECS = 15;
	for (int sLeft = SECS; sLeft > 0 && !quasiWakeNow(); sLeft--) {
		w->DrawQuasiMessage(pct, estMin, sLeft);
		for (int q = 0; q < 4 && !quasiWakeNow(); q++)
			sceKernelDelayThread(250 * 1000);
	}

	if (!quasiWakeNow()) {
		// 5. lights out for real -- kernel backlight off -- and black
		//    the content, then rest until a slide or a button
		w->QuasiBlank();
		// stop the system's own backlight auto-off timer from firing
		// (~30s) and wrestling control back -- we drive the backlight
		// directly while resting
		sceImposeSetBacklightOffTime(3600);
		meSetBrightness(0);
		unsigned int lastBeep = 0;
		int reassert = 0;
		while (!quasiWakeNow()) {
			sceKernelDelayThread(250 * 1000);
			// the system re-applies the user's brightness after a
			// moment, so hold it dark by re-asserting ~once a second
			if (++reassert >= 4) { reassert = 0; meSetBrightness(0); }
			int p = scePowerGetBatteryLifePercent();
			if (p >= 0 && p <= 5) {
				unsigned int now = sceKernelGetSystemTimeLow();
				if (now - lastBeep > 30u * 1000 * 1000) {
					lastBeep = now;
					quasiBeep();
				}
			}
		}
	}
	pspQuasiClearWake();

	// 6. wake: backlight back on instantly (kernel), full clock, audio,
	//    an ascending chirp, and a full repaint
	sceImposeSetBacklightOffTime(savedBl > 0 ? savedBl : 30);
	meSetBrightness(savedBright);
	scePowerTick(PSP_POWER_TICK_DISPLAY);
	scePowerSetClockFrequency(333, 333, 166);
	SDL_PauseAudio(0);
	quasiWakeTone();
	w->QuasiWake();
}

/* Called from the main thread once the event loop wakes up after a
   resume — everything here needs to run outside the callback. */

/* THE BLACK BOX. Screens flash; this appends to a file at the card
   root, timestamped, opened and closed per line so a freeze loses
   nothing. The user's whole job shrinks to: reproduce once, then
   send PSPATTERN-DIAG.txt. */
static volatile int diagBusy_ = 0;
/* After a resume the memory stick remounts on its own worker threads
   for one to two seconds (worst case ~5) and I/O in that window can
   BLOCK, not just fail -- uofw-verified, the classic post-resume
   trap. The resume path clears this flag and re-sets it only once
   the card answers, so no diag line can be the blocking first touch. */
static volatile int diagCardReady_ = 1;
extern "C" void pspDiagCardGate(int ready) { diagCardReady_ = ready; }

/* Every ME revive that has ever succeeded on this device ran with
   the audio paused -- the forensic build's inline revive sat before
   SDL_PauseAudio(0) by construction. The engine calls this to
   reproduce that condition wherever the revive runs from. */
extern "C" void pspSetAudioPaused(int paused) {
	SDL_PauseAudio(paused ? 1 : 0);
}

/* The GE is a bus master too: display lists queued by the last draw
   keep executing after the CPU moves on, and the only proven revive
   ran before the first post-resume draw -- GE silent. This waits for
   every queued list to complete so the revive's quiet bus includes
   the graphics engine. */
extern "C" void pspGeDrain(void) {
	sceGeDrawSync(0);
}
extern "C" void pspDiag(const char *fmt, ...) {
	/* Recorder disarmed for release: the diagnostic black box wore the
	   card and clutters it. Plumbing stays; delete this one line to
	   re-arm when a field report needs evidence. */
	return;
	/* Re-armed for one question: does the wake-boot verify, and is
	   the core alive cold? The recorder is a handful of lifecycle
	   lines per session now; it goes silent again at release. */
	if (!diagCardReady_) return;
	char line[160];
	unsigned int t = sceKernelGetSystemTimeLow();
	int n = snprintf(line, sizeof(line), "%010u ", t);
	va_list ap;
	va_start(ap, fmt);
	n += vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
	line[n++] = 0x0a;   // newline, spelled numerically
	while (diagBusy_) sceKernelDelayThread(200);
	diagBusy_ = 1;
	int fd = sceIoOpen("ms0:/PSPATTERN-DIAG.txt",
	                   PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (fd >= 0) {
		sceIoWrite(fd, line, n);
		sceIoClose(fd);
	}
	diagBusy_ = 0;
}



void PSPHandleResume() {
	if (!g_resumePending) return;
	g_resumePending = 0;
	/* card-independent breadcrumb: diagCardReady_ is still 1 from last
	   session here (the card gate below clears it), so this write
	   lands, and it answers the one question the crash log cannot --
	   did we come back UP at all, or die going DOWN in Sony's
	   suspend? Seeing this line = resumed, crash is in resume code
	   below; NOT seeing it = the sleep/wake itself failed. */
	pspDiag("resume: ENTERED (came back up)");
	Trace::Log("RESUME", "begin");

	/* ORDER IS THE WHOLE GAME HERE, learned twice over:

	   First, things that touch nothing shared -- controller sampling
	   and the GE mark are core-local and cannot race anything.

	   Second, the memory stick, GENTLY. uofw's resume source settles
	   what races what: clocks, PLL, DDR and the buses are fully
	   restored BEFORE this callback can run -- but the memory stick
	   remounts on its own worker threads for one to two seconds
	   AFTER it (worst ~5), and I/O in that window can BLOCK, not
	   just fail. The 'LED never lights' freeze was the very first
	   sceIoOpen (a diag line, ironically) blocking against that
	   remount. So: a flat second of settle first, covering the
	   blocking window; then the ready-poll with a five-second
	   budget; and the diag writer stays gated off until the card
	   has actually answered.

	   Third, and only then, file I/O and the rest. The second
	   core's revive is not here at all: it is deferred to the UI
	   tick, seconds from now, once the resume's traffic tail (the
	   phase-1 busy barrier, the thundering herd of thawed threads,
	   the remount itself) has passed. */

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	SDLGUI_MarkGuLost();

	{
		extern void pspDiagCardGate(int ready);
		pspDiagCardGate(0);
		/* One second: the settle no longer times any revive (the core
		   reboots at wake, in the sysevent), it only spans the memory
		   stick's blocking-remount window before the first card
		   touch. */
		sceKernelDelayThread(1000 * 1000);
		FileSystem *fs = FileSystem::GetInstance();
		int ok = 0;
		for (int i = 0; i < 80; i++) {       // + up to 4s of polling
			Path probe("bin:");
			if (fs->GetFileType(probe.GetPath().c_str()) == FT_DIR) {
				ok = 1;
				break;
			}
			sceKernelDelayThread(50 * 1000);
		}
		if (ok) {
			fs->Sync();
			pspDiagCardGate(1);
		}
		// not ok: the card never answered; diag stays silent so no
		// write can wedge -- the next successful session re-arms it
	}

	pspDiag("resume: card settled, sleep=%u wake=%u", PSPME_SleepCount(),
	        PSPME_WakeCount());

#ifdef PSP_ME_OFFLOAD
	// a real suspend is prevented by scePowerLock in normal use; if one
	// slips through (battery-critical force-suspend), verify/park here
	PSPME_OnResume();
#endif

	SDL_PauseAudio(0);
	pspDiag("resume: audio unpaused");
	PSPUsbMidiLink::OnResume();
	pspDiag("resume: usb done");

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

	/* Repurpose the power switch for quasi-standby: scePowerLock(0)
	   stops the real (ME-unsafe) suspend from ever firing, while the
	   switch still notifies powerCallback so we can drop into our own
	   low-power rest instead. Hold-to-power-off still works (syscon
	   handles it below the lock). */
	scePowerLock(0);

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

