#include "UIStepperField.h"
#include "Application/AppWindow.h"
#include <stdio.h>
#include <string.h>

UIStepperField::UIStepperField(GUIPoint &position,Variable &v,
                               const char *label,const char *valueFormat,
                               int min,int max,int displayOffset):
	UIIntVarField(position,v,label,min,max,1,4,displayOffset),
	valueFormat_(valueFormat)
{
} ;

void UIStepperField::Draw(GUIWindow &w,int offset) {

	GUITextProperties props ;
	GUIPoint position=GetPosition() ;
	position._y+=offset ;
	AppWindow &app=(AppWindow &)w ;

	// focused label gets the inverse block, matching the integer fields
	props.invert_=focus_ ;
	app.SetColor(focus_?CD_HILITE2:CD_ROW2) ;
	w.DrawString(format_,position,props) ;
	props.invert_=false ;
	position._x+=strlen(format_) ;

	app.SetColor(focus_?CD_NORMAL:CD_ROW) ;
	w.DrawString("<",position,props) ;
	position._x+=2 ;

	char buffer[16] ;
	if (src_.GetType()==Variable::CHAR_LIST) {
		strncpy(buffer,src_.GetString(),15) ;
		buffer[15]=0 ;
	} else {
		// Shifted for display, so a parameter stored 0..255 around a
		// centre can read as the signed amount it actually is.
		int v=src_.GetInt()+displayOffset_ ;
		sprintf(buffer,valueFormat_,v,v) ;
	}
	app.SetColor(CD_HILITE2) ;
	w.DrawString(buffer,position,props) ;
	position._x+=strlen(buffer)+1 ;

	app.SetColor(focus_?CD_NORMAL:CD_ROW) ;
	w.DrawString(">",position,props) ;
} ;
