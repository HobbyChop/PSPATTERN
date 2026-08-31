#ifndef _IMPORT_SAMPLE_DIALOG_H_
#define _IMPORT_SAMPLE_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
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
	void import(Path &element) ;
	void preview(Path &element) ;
	void endPreview() ;
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

} ;


#endif

