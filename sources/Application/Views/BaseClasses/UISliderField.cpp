#include "UISliderField.h"
#include "Application/AppWindow.h"
#include <stdio.h>
#include <string.h>

UISliderField::UISliderField(GUIPoint &position,Variable &v,
                             const char *label,int min,int max,
                             int xOffset,int yOffset,int barCells,
                             SliderDisplay display,int lastCol):
	UIIntVarField(position,v,label,min,max,xOffset,yOffset),
	barCells_(barCells),
	display_(display),
	lastCol_(lastCol)
{
} ;

void UISliderField::Draw(GUIWindow &w,int offset) {

	GUITextProperties props ;
	GUIPoint position=GetPosition() ;
	position._y+=offset ;
	AppWindow &app=(AppWindow &)w ;

	// label: grey at rest; the focused row gets an inverse block on its
	// name -- the same marker the integer fields use -- so the selection
	// is unmistakable. The bar tick alone read as too faint on a busy
	// page (tester feedback).
	props.invert_=focus_ ;
	app.SetColor(focus_?CD_HILITE2:CD_ROW2) ;
	w.DrawString(format_,position,props) ;
	props.invert_=false ;

	// the bar: a real pixel gradient bar (the PSPECTRA grad_bar look),
	// composited over the char grid by AppWindow at flush time. The
	// cells underneath are blanked in the cache so stale glyphs never
	// bleed through.
	int v=src_.GetInt() ;
	int range=max_-min_ ;
	if (range<1) range=1 ;

	// The readout is up to four cells ("100%") and has to finish on or
	// before lastCol_, which is the column before the panel frame. Any
	// overrun comes out of the bar rather than out of the number -- one
	// cell of bar is invisible, a per-cent sign drawn on the frame line
	// is not.
	int cells=barCells_ ;
	int lastUsed=position._x+(int)strlen(format_)+cells+SLIDER_VALUE_CELLS ;
	if (lastUsed>lastCol_) {
		cells-=(lastUsed-lastCol_) ;
	}
	if (cells<3) cells=3 ;

	int barW=cells*8-2 ;
	int fillPx=(v-min_)*barW/range ;

	char blank[SLIDER_CELLS+1] ;
	memset(blank,' ',cells) ;
	blank[cells]=0 ;
	GUITextProperties barProps ;
	position._x+=strlen(format_) ;
	app.SetColor(CD_BACKGROUND) ;
	w.DrawString(blank,position,barProps) ;

	app.OpBar(position._x*8,position._y*8+1,barW,fillPx,focus_) ;

	// the value: a number you can say out loud
	char buffer[8] ;
	if (display_==SD_PAN) {
		// Centre is the middle of the range and each side is scaled to
		// 99, so the two halves read symmetrically whatever the stored
		// range happens to be.
		int centre=min_+range/2 ;
		int half=range/2 ;
		if (half<1) half=1 ;
		if (v==centre) {
			strcpy(buffer,"CTR") ;
		} else if (v<centre) {
			sprintf(buffer,"L%2.2d",(centre-v)*99/half) ;
		} else {
			sprintf(buffer,"R%2.2d",(v-centre)*99/half) ;
		}
	} else if (range>=0xFE) {
		// A parameter spanning a whole byte has no natural unit, so
		// percent of travel is the honest reading, and it agrees with
		// the bar sitting next to it.
		sprintf(buffer,"%3d%%",(v-min_)*100/range) ;
	} else {
		// A range that already means something is printed as itself.
		sprintf(buffer,"%3d",v) ;
	}
	position._x+=cells+1 ;
	app.SetColor(CD_HILITE2) ;
	w.DrawString(buffer,position,barProps) ;
	app.SetColor(CD_NORMAL) ;
} ;
