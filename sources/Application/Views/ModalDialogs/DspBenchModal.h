#ifndef _DSP_BENCH_MODAL_H_
#define _DSP_BENCH_MODAL_H_

#include "Application/Utils/DspBench.h"
#include "Application/Views/BaseClasses/ModalView.h"

/* The load table, read on the machine it matters on. Runs the bench
   the first time it draws -- so the "running" frame reaches the
   screen before the main thread disappears for a couple of seconds
   -- and then shows what a voice of each engine costs at 1, 2, 4 and
   8 voices, as a percentage of one block's realtime budget. */
class DspBenchModal : public ModalView {
  public:
	DspBenchModal(View &view) ;
	virtual ~DspBenchModal() ;

	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void AnimationUpdate() ;

  private:
	DspBench::Result result_ ;
	bool started_ ;
} ;
#endif
