#ifndef _UI_STEPPER_FIELD_H_
#define _UI_STEPPER_FIELD_H_

// A small count as  label  < value >  — for things you nudge, not
// sweep (unison voices, crush bits, octaves).

#include "UIIntVarField.h"

class UIStepperField: public UIIntVarField {
public:
	UIStepperField(GUIPoint &position,Variable &v,const char *label,
	               const char *valueFormat,int min,int max,
	               int displayOffset=0) ;
	virtual ~UIStepperField() {} ;
	virtual void Draw(GUIWindow &w,int offset=0) ;
private:
	const char *valueFormat_ ;
} ;
#endif
