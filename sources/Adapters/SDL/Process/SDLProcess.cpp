
#include "SDLProcess.h"
#include <SDL/SDL_thread.h>

int _SDLStartThread(void *argp) {
	SysThread *play=(SysThread *)argp ;
	play->startExecution() ;
	return 0 ;
}

bool SDLProcessFactory::BeginThread(SysThread& thread) {
	SDL_CreateThread(_SDLStartThread,&thread);
	return true ;
}

SysSemaphore *SDLProcessFactory::CreateNewSemaphore(int initialcount, int maxcount) {
	return new SDLSysSemaphore(initialcount,maxcount) ;
} ;

SDLSysSemaphore::SDLSysSemaphore(int initialcount,int maxcount) {
	handle_=SDL_CreateSemaphore(0) ;
} ;

SDLSysSemaphore::~SDLSysSemaphore() {
	// dropping the pointer left the SDL semaphore allocated
	if (handle_) SDL_DestroySemaphore(handle_) ;
	handle_=0 ;
} ;

SysSemaphoreResult SDLSysSemaphore::Wait() {
	if (!handle_) {
		return SSR_INVALID ;
	} ;
	return (SysSemaphoreResult)SDL_SemWait(handle_) ;
} ;

SysSemaphoreResult SDLSysSemaphore::TryWait() {
	if (!handle_) {
		return SSR_INVALID ;
	} ;
	// Was a stub returning success without looking at the semaphore.
	// SDL_SemTryWait returns 0 when it took the count, SDL_MUTEX_TIMEDOUT
	// when there was none.
	int r=SDL_SemTryWait(handle_) ;
	if (r==0) return SSR_NO_ERROR ;
	if (r==SDL_MUTEX_TIMEDOUT) return SSR_BUSY ;
	return SSR_OTHER_ERROR ;
}

/* Was a stub that returned immediately -- so any caller using this as
   a cancellable sleep spun at full speed instead of waiting. The UI
   ticker does exactly that. */
SysSemaphoreResult SDLSysSemaphore::WaitTimeout(unsigned long timeout) {
	if (!handle_) {
		return SSR_INVALID ;
	} ;
	int r=SDL_SemWaitTimeout(handle_,(Uint32)timeout) ;
	if (r==0) return SSR_NO_ERROR ;
	if (r==SDL_MUTEX_TIMEDOUT) return SSR_BUSY ;
	return SSR_OTHER_ERROR ;
} ;

SysSemaphoreResult SDLSysSemaphore::Post() {
	if (!handle_) {
		return SSR_INVALID ;
	} ;
	return (SysSemaphoreResult)SDL_SemPost(handle_) ;
} ;
