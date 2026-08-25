#include "NullView.h"
#include "Application/Version.h"

NullView::NullView(GUIWindow &w,ViewData *viewData):View(w,viewData) {
}

NullView::~NullView() {
} 

void NullView::ProcessButtonMask(unsigned short mask,bool pressed) {

} ;

void NullView::DrawView() {

	Clear() ;


	GUITextProperties props;
	SetColor(CD_HILITE2) ;

	// This is the first screen a new user sees, and it used to name
	// the upstream project and its version rather than this one.
	char buildString[80] ;
	sprintf(buildString,"%s %s",PSPATTERN_NAME,PSPATTERN_VERSION_STRING) ;
	GUIPoint pos ;
	pos._y=28;
	pos._x=(40-strlen(buildString))/2 ;
	DrawString(pos._x,pos._y,buildString,props) ;

} ;

void NullView::OnPlayerUpdate(PlayerEventType ,unsigned int tick) {

} ;

void NullView::OnFocus() {
} ;
