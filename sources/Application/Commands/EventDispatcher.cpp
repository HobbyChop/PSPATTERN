#include "EventDispatcher.h"
#ifdef SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"

int EventDispatcher::keyRepeat_=30 ;
int EventDispatcher::keyDelay_=500 ;

Uint32 OnTimer(Uint32 interval) {
	return EventDispatcher::GetInstance()->OnTimerTick() ;
} ;

EventDispatcher::EventDispatcher() {
	window_=0;
	eventMask_=0 ;
	repeatArmed_=false ;

	// Read config file key repeat

	Config *config=Config::GetInstance() ;
	const char *s=config->GetValue("KEYDELAY") ;
	if (s) {
		keyDelay_=atoi(s) ;
	}

	s=config->GetValue("KEYREPEAT") ;
	if (s) {
		keyRepeat_=atoi(s) ;
	}

	repeatMask_=0 ;
	repeatMask_|=(1<<EPBT_LEFT) ;
	repeatMask_|=(1<<EPBT_RIGHT) ;
	repeatMask_|=(1<<EPBT_UP) ;
	repeatMask_|=(1<<EPBT_DOWN) ;

	timer_=TimerService::GetInstance()->CreateTimer() ;
	timer_->AddObserver(*this) ;

} ;

EventDispatcher::~EventDispatcher() {
	timer_->RemoveObserver(*this) ;
	SAFE_DELETE(timer_) ;
} ;

void EventDispatcher::Execute(FourCC id,float value) {

	if (window_) {
		GUIEventPadButtonType mapping ;
		switch(id) {
			case TRIG_EVENT_A:
				mapping=EPBT_A;
				break ;
			case TRIG_EVENT_B:
				mapping=EPBT_B;
				break ;
			case TRIG_EVENT_LEFT:
				mapping=EPBT_LEFT;
				break ;
			case TRIG_EVENT_RIGHT:
				mapping=EPBT_RIGHT;
				break ;
			case TRIG_EVENT_UP:
				mapping=EPBT_UP;
				break ;
			case TRIG_EVENT_DOWN:
				mapping=EPBT_DOWN;
				break ;
			case TRIG_EVENT_LSHOULDER:
				mapping=EPBT_L;
				break ;
			case TRIG_EVENT_RSHOULDER:
				mapping=EPBT_R;
				break ;
			case TRIG_EVENT_START:
				mapping=EPBT_START;
				break ;
			case TRIG_EVENT_SELECT:
				mapping=EPBT_SELECT;
				break ;
			case TRIG_EVENT_TRIANGLE:
				mapping=EPBT_TRIANGLE;
				break ;
			default:
				// an unmapped trigger used to fall through with
				// `mapping` uninitialised and shift by whatever was
				// on the stack
				return ;
		}

		// Compute mask and repeat if needed

		if (value>0.5) {
			eventMask_|=(1<<mapping) ;
		} else {
			// clear, not XOR: a release without a matched press (missed
			// event, resume) would otherwise SET the bit and fake a
			// held button
			eventMask_&=~(1<<mapping) ;
		}

		// Dispatch event to window

		unsigned long now=System::GetInstance()->GetClock();
		GUIEventType type=(value>0.5)? ET_PADBUTTONDOWN:ET_PADBUTTONUP ;
		GUIEvent event(mapping,type,now,0,0,0) ;
		window_->DispatchEvent(event) ;


		bool wantRepeat=(eventMask_&repeatMask_)!=0 ;
		if (wantRepeat&&!repeatArmed_) {
			// arm once per hold, not on every edge -- re-arming reset
			// the delay and (before SDLTimer fixed its leak) stacked
			// live timers
			timer_->SetPeriod(float(keyDelay_)) ;
			timer_->Start() ;
			repeatArmed_=true ;
		} else if (!wantRepeat&&repeatArmed_) {
			timer_->Stop() ;
			repeatArmed_=false ;
		}
	} ;
};

void EventDispatcher::ClearMaskBits(unsigned short mask) {
	eventMask_&=~mask ;
	if (repeatArmed_&&!(eventMask_&repeatMask_)) {
		timer_->Stop() ;
		repeatArmed_=false ;
	}
}

void EventDispatcher::SetWindow(GUIWindow *window) {
	window_=window ;
} ;

unsigned int EventDispatcher::OnTimerTick() {

	unsigned sendMask=(eventMask_&repeatMask_) ;
	unsigned long now=System::GetInstance()->GetClock();

	if (sendMask) {
		int current=0 ;
		while (sendMask) {
			if (sendMask&1) {
				/* PUSH, do not dispatch. This runs on SDL's timer
				   thread, and dispatching from here executed the whole
				   input pipeline -- view switches, the instrument
				   screen tearing down and rebuilding its field list --
				   concurrently with the main thread drawing that same
				   list. That is the freeze-and-reboot when holding an
				   arrow to browse. PushEvent is SDL_PushEvent
				   underneath (thread-safe by contract); the main loop
				   dispatches and deletes it like any other event. */
				GUIEvent *event=new GUIEvent(current,ET_PADBUTTONDOWN,now,0,0,0) ;
				window_->PushEvent(*event) ;
			}
			sendMask>>=1 ;
			current++ ;
		}		
		return keyRepeat_ ;
	}
	return 0 ;
} ;

void EventDispatcher::Update(Observable &o,I_ObservableData *d) {
	unsigned int tick=OnTimerTick() ;
	if (tick) {
		timer_->SetPeriod(float(tick)) ;
	} else {
		timer_->Stop() ;
	};
} ;
