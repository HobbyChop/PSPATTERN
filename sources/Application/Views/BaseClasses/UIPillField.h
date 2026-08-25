#ifndef _UI_PILL_FIELD_H_
#define _UI_PILL_FIELD_H_

// An enum as a row of choice pills: [saw][pulse] — the active option
// is a filled accent block, the others framed. This is how enums are
// edited everywhere; a slider is never an enum.

#include "UIIntVarField.h"

class UIPillField: public UIIntVarField {
public:
	// label may be 0 for a bare pill row
	UIPillField(GUIPoint &position,Variable &v,const char *label,
	            int optionCount) ;
	virtual ~UIPillField() {} ;
	virtual void Draw(GUIWindow &w,int offset=0) ;
private:
	int optionCount_ ;
} ;
#endif
