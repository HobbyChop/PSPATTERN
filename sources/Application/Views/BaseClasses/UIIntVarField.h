#ifndef _UI_INT_VAR_FIELD_H_
#define _UI_INT_VAR_FIELD_H_

#include "UIField.h"
#include "Foundation/Variables/Variable.h"

class UIIntVarField: public UIField {

public:

	UIIntVarField(
    GUIPoint &position,
    Variable &v,
    const char *format,
    int min,
    int max,
    int xOffset,
    int yOffset,
    int displayOffset = 0,
    int clipWidth = 0,    // >0: truncate the drawn text to this many chars
    // print the value as a percentage of min..max instead of as itself
    bool percentOfRange = false);
  
	virtual ~UIIntVarField() {} ;
	virtual void Draw(GUIWindow &w,int offset=0) ;
	virtual void ProcessArrow(unsigned short mask) ;
	virtual void OnClick() {} ;
  
  FourCC GetVariableID() ;
	Variable &GetVariable() ;
protected:
	Variable &src_ ;
	const char *format_ ;
	int min_ ;
	int max_ ;
	int xOffset_ ;
	int yOffset_ ;
  int displayOffset_;
  int clipWidth_;
	// Print the value as its percentage of min..max rather than as
	// itself, for a control whose raw number is an implementation
	// detail (an envelope stage stored 0..255).
	bool percentOfRange_ ;
} ;

#endif
