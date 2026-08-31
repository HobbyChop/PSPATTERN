#ifndef _UI_ACTION_FIELD_
#define _UI_ACTION_FIELD_

#include "UIField.h"
#include "Foundation/Observable.h"

class UIActionField: public UIField,public Observable {
public:
	UIActionField(const char *name,unsigned int fourcc,GUIPoint &position) ;
	UIActionField(std::string name,unsigned int fourcc,GUIPoint &position) ;
	virtual ~UIActionField() ;
	virtual void Draw(GUIWindow &w,int offset=0) ;
	virtual void ProcessArrow(unsigned short mask) {} ;
	virtual void OnClick() ;
	// the ladder walk identifies rows by id; ours is its action code
	virtual FourCC GetVariableID() { return fourcc_ ; }
	const char *GetString() ;

protected:
	std::string name_ ;
	unsigned int fourcc_ ;
} ;
#endif
