#ifndef _EVENT_DISPATCHER_H_
#define _EVENT_DISPATCHER_H_

#include "CommandDispatcher.h"
#include "Foundation/T_Singleton.h"
#include "Application/AppWindow.h"
#include "System/Timer/Timer.h"
#include "Foundation/Observable.h"

class EventDispatcher: public T_Singleton<EventDispatcher>,public CommandExecuter,public I_Observer {
public:
	EventDispatcher() ;
	~EventDispatcher() ;
	void SetWindow(GUIWindow *window) ;
	virtual void Execute(FourCC id,float value) ;
	unsigned int OnTimerTick() ;
	/* clear bits the HARDWARE says are up: the stuck-mask cure. Stops
	   the repeat timer too if nothing repeatable stays held. Called
	   from the UI ticker; touches state also touched on the input
	   path, but both are single writers of distinct bits and the worst
	   race re-clears an already-clear bit. */
	void ClearMaskBits(unsigned short mask) ;
	int GetEventMask() { return eventMask_ ; } ;
    virtual void Update(Observable &o,I_ObservableData *d) ;
private:
	GUIWindow *window_ ;	
	static int keyRepeat_ ;
	static int keyDelay_ ;
	unsigned int eventMask_ ;
	unsigned int repeatMask_ ;
	// repeat timer armed for the current hold (see Execute)
	bool repeatArmed_ ;
	I_Timer *timer_ ;
} ;

#endif