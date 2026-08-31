#ifndef _UI_FIELD_H_
#define _UI_FIELD_H_

#include "UIFramework/BasicDatas/GUIPoint.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include "View.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"

class UIField {
public:
	UIField(GUIPoint &position) ;
	virtual ~UIField() ;
	virtual void Draw(GUIWindow &w,int offset=0)=0 ;
	virtual void OnClick()=0 ; // A depressed
	virtual void ProcessArrow(unsigned short mask)=0 ;
	virtual void OnBClick() {} ; // B depressed
	virtual void ProcessBArrow(unsigned short mask) {} ;
	/* several screens cast whatever has focus to UIIntVarField to ask
	   this; with an action field in the list that cast read string
	   bytes as a variable reference. Virtual with a null default makes
	   the question safe to ask of any field. */
	virtual FourCC GetVariableID() { return 0 ; }
	void SetFocus() ;
	void ClearFocus() ;
	bool HasFocus() ;
	void SetPosition(GUIPoint &) ;
	GUIPoint GetPosition() ;
	GUIColor GetColor() ;

	virtual bool IsStatic() ;

protected:
	int x_ ;
	int y_ ;
	bool focus_ ;
} ;
#endif

