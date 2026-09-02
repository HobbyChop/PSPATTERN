#ifndef _IMPORT_SAMPLE_DIALOG_H_
#define _IMPORT_SAMPLE_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include "Application/Instruments/SampleConvert.h"
#include "Foundation/T_SimpleList.h"
#include "System/FileSystem/FileSystem.h"
#include <string>

/* The import screen, two pages. The first is the file list with a
   panel saying what the file under the cursor is; TRIANGLE chooses a
   file and opens its settings page, where the same key confirms the
   import and X only steps back to the list. TRIANGLE is the only key
   that imports, so the O-and-arrow editing habit from the instrument
   screen cannot take a file by accident, and a stray X cannot leave
   the screen from the settings. SELECT plays the file as it will be
   stored. A full screen in the house style. */
class ImportSampleDialog:public ModalView {
public:
	ImportSampleDialog(View &view) ;
	virtual ~ImportSampleDialog() ;

	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType ,unsigned int currentTick) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void ApplyDeferred() ;
	// the yes/no on a replacement comes back through here
	void OnReplaceAnswer(bool yes) ;

protected:
	enum Page { P_LIST=0, P_SETTINGS=1 } ;
	void setCurrentFolder(Path *path) ;
	void warpToNextSample(int dir) ;
	void import(Path &element,const SampleImportOptions &opt) ;
	void queueImport(Path &element) ;
	void askImport() ;
	void choose() ;
	void preview(Path &element) ;
	void endPreview() ;
	void readInfo() ;
	void stepOption(int dir) ;
	void drawList() ;
	void drawChoice() ;      // page one, the right column
	void drawSettings() ;    // page two
	void processListButtons(unsigned short mask) ;
	void processSettingsButtons(unsigned short mask) ;
private:
	Path *getImportElement();
	bool isSampleLibRoot();
	T_SimpleList<Path> sampleList_ ;
	int currentSample_ ;
	int topIndex_ ;
	int toInstr_ ;
	// preview lives while SELECT is held, like audition everywhere else
	bool previewHeld_ ;
	// bare-X release backs out one level: the settings page, then the
	// folder, then close at the root. A chord must not.
	bool bHeld_ ;
	bool bChorded_ ;
	std::string status_ ;   // one transient line: imported X, can't read Y
	static bool initStatic_ ;
	static Path sampleLib_ ;
	static Path currentPath_ ;
	// preview queued from the press (inside the input lock), started
	// from ApplyDeferred (outside it)
	Path pendingPreview_ ;
	bool previewPending_ ;

	/* the settings page. What the file is comes from its header, read
	   deferred like everything that opens a file; the settings are
	   remembered for the session, so a batch at one setting is
	   TRIANGLE, TRIANGLE per file. */
	Page page_ ;
	Path chosen_ ;           // the file the settings page is about
	int optRow_ ;            // 0 channels, 1 rate
	bool infoPending_ ;
	bool infoValid_ ;
	// a setting changed while the preview plays: applied deferred, since
	// the shape change takes the mixer lock and the press holds it
	bool shapePending_ ;
	std::string infoName_ ;  // the file the info below describes
	int srcChannels_ ;
	int srcRate_ ;
	int srcBits_ ;
	bool srcFloat_ ;
	long srcFrames_ ;
	long srcBytes_ ;
	static SampleImportOptions options_ ;
	/* Import is deferred in two steps: a conversion is seconds of card
	   traffic, and Redraw runs ApplyDeferred BEFORE it paints, so the
	   redraw that announces "importing" has to be a different one from
	   the redraw that does the work. */
	int importPhase_ ;       // 0 idle, 1 announce, 2 run
	Path pendingImport_ ;
} ;

#endif
