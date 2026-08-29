#include "UIIntVarField.h"


#include "UIIntVarField.h"
#include "UIFramework/Interfaces/I_GUIGraphics.h"
#include "System/Console/Trace.h"
#include "Application/AppWindow.h"

#define abs(x) (x<0?-x:x)

UIIntVarField::UIIntVarField(
  GUIPoint &position,
  Variable &v,
  const char *format,
  int min,
  int max,
  int xOffset,
  int yOffset,
  int displayOffset,
  int clipWidth,
  bool percentOfRange)
:UIField(position)
,src_(v)
{
	format_=format ;
	min_=min ;
	max_=max ;
	xOffset_=xOffset ;
	yOffset_=yOffset ;
  displayOffset_ = displayOffset;
  clipWidth_ = clipWidth;
  percentOfRange_ = percentOfRange;
} ;

void UIIntVarField::Draw(GUIWindow &w,int offset) {

	GUITextProperties props ;
	GUIPoint position=GetPosition() ;
	position._y+=offset ;

	if (focus_) {
		((AppWindow&)w).SetColor(CD_HILITE2) ;
		props.invert_=true ;
	} else {
		((AppWindow&)w).SetColor(CD_NORMAL) ;
	}
	Variable::Type type=src_.GetType() ;
	char buffer[80] ;
	switch (type) {
		case Variable::INT:
			{
			int ivalue=src_.GetInt()+displayOffset_ ;
			if (percentOfRange_) {
				int range=max_-min_ ;
				if (range<1) range=1 ;
				ivalue=(src_.GetInt()-min_)*100/range ;
			}
			snprintf(buffer,sizeof(buffer),format_,ivalue,ivalue) ;
			}
			break ;
		case Variable::CHAR_LIST:
		case Variable::BOOL:
			{
			const char *cvalue=src_.GetString() ;
			// snprintf, not sprintf: cvalue can be a samplelib FILENAME,
			// and a long one overran this stack buffer -- a wild jump on
			// every redraw of the row (the browse-crash detonator)
			snprintf(buffer,sizeof(buffer),format_,cvalue) ;
			}
			break ;

		default:
			strcpy(buffer,"++wtf++");
	}
	if (clipWidth_ > 0 && (int)strlen(buffer) > clipWidth_) {
		buffer[clipWidth_] = 0;
	}
	w.DrawString(buffer,position,props) ;
} ;

void UIIntVarField::ProcessArrow(unsigned short mask) {
	int value=src_.GetInt() ;

	switch(mask) {
		case EPBM_UP:
			value+=yOffset_ ;
			break ;
		case EPBM_DOWN:
			value-=yOffset_ ;
			break ;
		case EPBM_LEFT:
			value-=xOffset_ ;
			break ;
  		case EPBM_RIGHT:
			value+=xOffset_ ;
			break ;
	} ;
	if (value<min_) {
		value=min_ ;
	} ;
	if (value>max_) {
		value=max_ ;
	}
	
	src_.SetInt(value) ;
} ;

FourCC UIIntVarField::GetVariableID() {
    return src_.GetID() ;
} ;

Variable &UIIntVarField::GetVariable() {
	return src_ ;
} ;
