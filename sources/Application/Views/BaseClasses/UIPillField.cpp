#include "UIPillField.h"
#include "Application/AppWindow.h"
#include <stdio.h>
#include <string.h>

UIPillField::UIPillField(GUIPoint &position,Variable &v,const char *label,
                         int optionCount):
	UIIntVarField(position,v,label?label:"",0,optionCount-1,1,1),
	optionCount_(optionCount)
{
} ;

void UIPillField::Draw(GUIWindow &w,int offset) {

	GUITextProperties props ;
	GUIPoint position=GetPosition() ;
	position._y+=offset ;
	AppWindow &app=(AppWindow &)w ;

	if (format_[0]) {
		app.SetColor(focus_?CD_NORMAL:CD_ROW2) ;
		w.DrawString(format_,position,props) ;
		position._x+=strlen(format_) ;
	}

	static const char *boolNames[2]={"off","on"} ;
	int active=(src_.GetType()==Variable::BOOL)?(src_.GetBool()?1:0):src_.GetInt() ;
	char **list=(src_.GetType()==Variable::CHAR_LIST)?src_.GetListPointer():0 ;
	for (int i=0;i<optionCount_;i++) {
		const char *name=list?((list[i])?list[i]:"?"):
			((optionCount_==2)?boolNames[i]:"?") ;
		int cells=(int)strlen(name) ;
		GUITextProperties p2 ;
		if (i==active) {
			// filled pill: inverted accent text = accent block with
			// dark ink
			p2.invert_=true ;
			app.SetColor(CD_HILITE1) ;
			w.DrawString(name,position,p2) ;
			if (focus_) {
				// On the block's own outer pixels, over the text --
				// see OpRing. Around the block it bled into whatever
				// sat on the row below, which in the mixer panel is
				// another pill.
				app.OpRing(position._x*8,position._y*8,
				           cells*8,8,AppWindow::OC_WHITE) ;
			}
		} else {
			// inactive options are dim text only — boxed options on
			// adjacent rows collide at the 12px row pitch
			app.SetColor(CD_ROW2) ;
			w.DrawString(name,position,p2) ;
		}
		position._x+=cells+1 ;
	}
} ;
