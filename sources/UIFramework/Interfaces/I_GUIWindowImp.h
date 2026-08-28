#ifndef _I_GUIWINDOWIMP_H_
#define _I_GUIWINDOWIMP_H_

#include "UIFramework/BasicDatas/GUICreateWindowParams.h"
#include "I_GUIGraphics.h"
#include "UIFramework/BasicDatas/GUIEvent.h"

class I_GUIWindowFactory ; // forward declaration
class GUIWindow ;

// Base class for implementation window. Base operation directed
// to system calls are passed from a GUIWindow to an instance of
// system class derived from this interface

class I_GUIWindowImp: public I_GUIGraphics {
public:

	virtual ~I_GUIWindowImp() {} ;

	// This method allows to have back pointer to the framework window
	// to avoid heavy searches when having to forward system events

	void SetWindow(GUIWindow &) ;

	virtual void PushEvent(GUIEvent &)=0 ;

	// When on, DrawRect still draws but does NOT append a dirty rect;
	// the caller supplies one bounding rect. Default no-op.
	virtual void SetBatchRects(bool /*on*/) {} ;
#if defined(PLATFORM_PSP) && defined(PSP_GU_DISPLAY)
	// Queue a scope for the GPU to draw (PSP GU path). Default no-op.
	virtual void GuQueueScope(int,int,int,int,const short*,const short*,int,bool,unsigned int,unsigned int) {} ;
	virtual void GuQueueRect(int,int,int,int,unsigned int) {} ;
#endif

//	virtual void Save()=0 ;
//	virtual void Restore()=0 ;

protected:

	GUIWindow *_window ; // The GUIWindow associated to the Imp.
} ;
#endif
