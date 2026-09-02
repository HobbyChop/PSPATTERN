#ifndef _SAMPLES_DIALOG_H_
#define _SAMPLES_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include <string>
#include <vector>

/* Every sound file the project carries, what each costs and who is
   on it -- and a way to drop one. The compaction on the project
   screen sweeps every unused sample at once; this is that sweep with
   the lights on, one file at a time. A full screen in the house
   style, opened from the project screen. */
class SamplesDialog:public ModalView {
public:
	SamplesDialog(View &view) ;
	virtual ~SamplesDialog() ;

	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void ApplyDeferred() ;
	// the yes/no of a removal comes back through here
	void OnRemoveAnswer(bool yes) ;

private:
	enum Kind { K_WAV, K_BANK, K_STRAY } ;
	struct Row {
		Kind kind ;
		std::string name ;      // the file name on the card
		int poolIndex ;         // wav: its slot; bank: its first preset; stray: -1
		int bankId ;            // soundfonts only
		int presets ;           // soundfonts: how many came out of it
		long ramBytes ;
		int channels ;
		int rate ;
		long frames ;
		std::string usedBy ;    // "03 0A 1F", empty when nothing is on it
		int users ;
	} ;
	void rebuild() ;
	void move(int dir) ;
	void askRemove() ;
	void doRemove() ;
	void preview() ;
	void endPreview() ;

	std::vector<Row> rows_ ;
	int cursor_ ;
	int top_ ;
	long totalRam_ ;
	std::string status_ ;
	// preview lives while SELECT is held, like everywhere else
	bool previewHeld_ ;
	bool previewPending_ ;
	// the removal runs deferred: file and pool work outside the input lock
	bool removePending_ ;
	int removeIndex_ ;
	// bare-X release closes; an X chord (page jump) must not
	bool bHeld_ ;
	bool bChorded_ ;
} ;

#endif
