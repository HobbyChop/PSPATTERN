
#include "PSPSystem.h"
#include "Adapters/PSP/Midi/PSPMidiService.h"
#include "Adapters/PSP/Midi/PSPUsbMidiLink.h"
#include "Adapters/SDL/Audio/SDLAudio.h"
#include "Adapters/SDL/GUI/SDLEventManager.h"
#include "Adapters/SDL/GUI/GUIFactory.h"
#include "Adapters/SDL/GUI/SDLGUIWindowImp.h"
#include "Adapters/SDL/Process/SDLProcess.h"
#include "Adapters/PSP/FileSystem/PSPFileSystem.h"
#include "Adapters/SDL/Timer/SDLTimer.h"
#include "Application/Model/Config.h"
#include "System/Console/Logger.h"
#include <time.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psppower.h>
#include <sys/time.h>
#include <malloc.h>
#include <stdlib.h>

EventManager *PSPSystem::eventManager_ = NULL ;

int PSPSystem::MainLoop() 
{
	eventManager_->InstallMappings() ;
	return eventManager_->MainLoop() ;
} ;

void PSPSystem::Boot(int argc,char **argv) {
#ifdef PSPDEBUG
	pspDebugScreenInit();
#endif

	// Install System
	System::Install(new PSPSystem()) ;

	// the nub: analog sampling on, so GetAnalog reads real positions.
	// SDL's joystick init sets the same mode later; setting it here too
	// costs nothing and removes the ordering dependency.
	sceCtrlSetSamplingCycle(0) ;
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG) ;

	// Install FileSystem
	FileSystem::Install(new PSPFileSystem()) ;

	Path bootPath(argv[0]) ;
	Path parent=bootPath.GetParent() ;

	Path::SetAlias("bin",parent.GetPath().c_str()) ;
	Path::SetAlias("root",parent.GetPath().c_str()) ;

	Config::GetInstance()->ProcessArguments(argc,argv) ;

  // No log file unless asked for: an open file on ms0 plus any
  // chatty Trace path keeps the Memory Stick LED flashing and wears
  // the stick. <LOG value="YES"/> in config.xml enables lgpt.log.
  const char *logIt=Config::GetInstance()->GetValue("LOG") ;
  if ((logIt)&&(!strcmp(logIt,"YES")))
  {
    Path logPath("bin:lgpt.log");
    FileLogger *fileLogger=new FileLogger(logPath);
    if(fileLogger->Init().Succeeded())
    {
      Trace::GetInstance()->SetLogger(*fileLogger);
    }
  }
	 
	// Install GUI Factory
	I_GUIWindowFactory::Install(new GUIFactory()) ;

	// Install Timers

	TimerService::GetInstance()->Install(new SDLTimerService()) ;

	// Install Sound

	// Audio block size, in frames.
	//
	// This was 128, which gives the whole render -- eight voices, the
	// channel strips and the sends -- 2.9 milliseconds to finish, 344
	// times a second. Any block that overruns is filled with the
	// silent buffer the driver keeps for the purpose, and a run of
	// those is a buzz at the block rate rather than a dropout anybody
	// would call a dropout.
	//
	// A small block also repeats every per-block cost 344 times a
	// second: each voice re-reads its parameters and recomputes its
	// pan on entry to every render, and that work is per BLOCK, not
	// per sample. Doubling the block halves it.
	//
	// 256 by default, and settable, because the right number is a
	// property of the machine and the song and I would rather it be
	// found than guessed. Latency is the price: frames times the
	// prebuffer count over 44100. Must be a multiple of 64, which is
	// what the PSP's audio hardware accepts.
	AudioSettings hints ;
	hints.bufferSize_=256 ;
	hints.preBufferCount_=6 ;
	const char *abs=Config::GetInstance()->GetValue("AUDIOBUFFERSIZE") ;
	if (abs) {
		int v=atoi(abs) ;
		if (v>=64 && v<=4096) {
			v=(v/64)*64 ;
			if (v>=64) hints.bufferSize_=v ;
		}
	}
	const char *apb=Config::GetInstance()->GetValue("AUDIOPREBUFFER") ;
	if (apb) {
		int v=atoi(apb) ;
		if (v>=2 && v<=16) hints.preBufferCount_=v ;
	}
	Trace::Log("AUDIO","block %d frames, prebuffer %d, latency %dms",
	           hints.bufferSize_,hints.preBufferCount_,
	           hints.bufferSize_*hints.preBufferCount_*1000/44100) ;
	Audio::Install(new SDLAudio(hints)) ;

	// Install Midi: usbmidi.prx (PSP-MIDI adapter) next to the EBOOT.
	// Missing prx is fine - the service just registers no device.
	PSPUsbMidiLink::Load(argv[0]) ;
	MidiService::Install(new PSPMidiService()) ;

	// Install Threads

	SysProcessFactory::Install(new SDLProcessFactory()) ;

	if ( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_TIMER) < 0 )   {
		return;
	}
#ifndef SDL2
    SDL_EnableUNICODE(1);
#endif
    SDL_ShowCursor(SDL_DISABLE);

	atexit(SDL_Quit);

 	eventManager_=I_GUIWindowFactory::GetInstance()->GetEventManager() ;
	eventManager_->Init() ;

	// PSP SDL Basic config

	bool invert=false ;
	Config *config=Config::GetInstance() ;
	const char *s=config->GetValue("INVERT") ;

	if ((s)&&(!strcmp(s,"YES"))) {
		invert=true ;
	}

	if (!invert) {
		eventManager_->MapAppButton("but:0:0",APP_BUTTON_B) ;
		eventManager_->MapAppButton("but:0:3",APP_BUTTON_A) ;
	}else {
		eventManager_->MapAppButton("but:0:0",APP_BUTTON_A) ;
		eventManager_->MapAppButton("but:0:3",APP_BUTTON_B) ;
	}
	eventManager_->MapAppButton("but:0:7",APP_BUTTON_LEFT) ;
	eventManager_->MapAppButton("but:0:9",APP_BUTTON_RIGHT) ;
	eventManager_->MapAppButton("but:0:8",APP_BUTTON_UP) ;
	eventManager_->MapAppButton("but:0:6",APP_BUTTON_DOWN) ;
	eventManager_->MapAppButton("but:0:4",APP_BUTTON_L) ;
	eventManager_->MapAppButton("but:0:5",APP_BUTTON_R) ;
	eventManager_->MapAppButton("but:0:11",APP_BUTTON_START) ;
	// SELECT rides the same built-in rails as every other button. It
	// used to be mapped only by mapping.xml, so a card whose copy
	// predates the select line had a dead button -- audition and the
	// song bookmarks both -- and nothing on screen to say why.
	eventManager_->MapAppButton("but:0:10",APP_BUTTON_SELECT) ;

} ;

int PSPSystem::GetBatteryLevel() {
	// polled every UI frame by the song view's side panel; a battery
	// figure does not move at 60Hz, so answer from a 2s cache and save
	// two power syscalls per frame (same idea as GetMemoryFree's cache)
	static unsigned long at=0 ; static int cached=-1 ;
	unsigned long now=GetClock() ;
	if (at&&((now-at)<2000)) return cached ;
	at=now?now:1 ;
	if (!scePowerIsBatteryExist()) { cached=-1 ; return -1 ; }
	int pct=scePowerGetBatteryLifePercent() ;
	cached=(pct<0)?-1:pct ;
	return cached ;
} ;

void PSPSystem::Shutdown() {
	PSPUsbMidiLink::Unload() ;
} ;

bool PSPSystem::GetAnalog(int &x,int &y) {
	SceCtrlData pad ;
	// peek, not read: SDL's own control reads must keep seeing events
	if (sceCtrlPeekBufferPositive(&pad,1)<=0) return false ;
	x=(int)pad.Lx-128 ;
	y=(int)pad.Ly-128 ;
	return true ;
}

unsigned long PSPSystem::GetClock() {
	struct timeval now;
	Uint32 ticks;
	gettimeofday(&now, NULL);
	ticks=(now.tv_sec)*1000+(now.tv_usec)/1000;
	return(ticks);
}

void PSPSystem::Sleep(int millisec) {
/*	if (millisec>0)
		::Sleep(millisec) ;
*/}

void *PSPSystem::Malloc(unsigned size) {
	return malloc(size) ;
}

void PSPSystem::Free(void *ptr) {
	free(ptr) ;
} 

void PSPSystem::Memset(void *addr,char val,int size) {
    
    unsigned int ad=(unsigned int)addr ;
    if (((ad&0x3)==0)&&((size&0x3)==0)) { // Are we 4-byte aligned ?
        unsigned int intVal=0 ;
        for (int i=0;i<4;i++) {
             intVal=(intVal<<8)+val ;  
        }
        unsigned int *dst=(unsigned int *)addr ;
        size_t intSize=size>>2 ;
        
        for (unsigned int i=0;i<intSize;i++) {
            *dst++=intVal ;
        }        
    } else {
        memset(addr,val,size) ;
    } ;
} ;

void *PSPSystem::Memcpy(void *s1, const void *s2, int n) {
    return memcpy(s1,s2,n) ;
} ;  
/*
void PSPSystem::AddUserLog(const char *msg) {
#ifdef PSPDEBUG
	pspDebugScreenPrintf("%s\n",msg) ;
#endif
};
*/
void PSPSystem::PostQuitMessage() {
	SDLEventManager::GetInstance()->PostQuitMessage() ;
} ; 

unsigned int PSPSystem::GetMemoryUsage() {
	// mallinfo walks the free list under the malloc lock -- O(fragments)
	// and contending the allocator, per frame, for a panel bar. Cache it
	// a second at a time like GetMemoryFree does.
	static unsigned long at=0 ; static unsigned int cached=0 ;
	unsigned long now=GetClock() ;
	if (at&&((now-at)<1000)) return cached ;
	at=now?now:1 ;
	struct mallinfo m=mallinfo();
	cached=m.uordblks ;
	return cached ;
}

