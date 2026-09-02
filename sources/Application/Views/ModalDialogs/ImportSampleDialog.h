#ifndef _IMPORT_SAMPLE_DIALOG_H_
#define _IMPORT_SAMPLE_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include "Application/Instruments/SampleConvert.h"
#include "Foundation/T_SimpleList.h"
#include "System/FileSystem/FileSystem.h"
#include <string>

class ImportSampleDialog:public ModalView {
public:
	ImportSampleDialog(View &view) ;
	virtual ~ImportSampleDialog() ;

	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType ,unsigned int currentTick) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void ApplyDeferred() ;

protected:
	void setCurrentFolder(Path *path) ;
	void warpToNextSample(int dir) ;
	void import(Path &element,const SampleImportOptions &opt) ;
	void queueImport(Path &element) ;
	void preview(Path &element) ;
	void endPreview() ;
	// the options step: read the header, show it, take the choices
	void openOptions(Path &element) ;
	void closeOptions() ;
	void drawList() ;
	void drawOptions() ;
	void processListButtons(unsigned short mask) ;
	void processOptionButtons(unsigned short mask) ;
private:
	Path *getImportElement();
	bool isSampleLibRoot();
	T_SimpleList<Path> sampleList_ ;
	int currentSample_ ;
	int topIndex_ ;
	int toInstr_ ;
	// preview lives while SELECT is held, like audition everywhere else
	bool previewHeld_ ;
	// bare-B release backs out one folder (closes at the root); a B
	// chord (page jump) must not
	bool bHeld_ ;
	bool bChorded_ ;
	std::string status_ ;   // one transient line: imported X, can't play Y
	static bool initStatic_ ;
	static Path sampleLib_ ;
	static Path currentPath_ ;
	// preview queued from the press (inside the input lock), started
	// from ApplyDeferred (outside it)
	Path pendingPreview_ ;
	bool previewPending_ ;

	/* The options step. O on a wav lands here rather than importing
	   at once: what the file is, and how the project should store it.
	   The choices are remembered for the session, so a batch at the
	   same settings is O, O per file. The header read is a file open,
	   so it waits for ApplyDeferred like the preview does. */
	bool inOptions_ ;
	Path pendingOptions_ ;
	bool optionsPending_ ;
	Path optPath_ ;
	int optRow_ ;            // 0 channels, 1 rate
	int srcChannels_ ;
	int srcRate_ ;
	int srcBits_ ;
	bool srcFloat_ ;
	long srcFrames_ ;
	long srcBytes_ ;
	static SampleImportOptions options_ ;
	/* Import is deferred too, in two steps: a conversion is seconds
	   of card traffic, and Redraw runs ApplyDeferred BEFORE it paints,
	   so the redraw that announces "importing" has to be a different
	   one from the redraw that does the work. */
	int importPhase_ ;       // 0 idle, 1 announce, 2 run
	Path pendingImport_ ;
} ;


#endif
