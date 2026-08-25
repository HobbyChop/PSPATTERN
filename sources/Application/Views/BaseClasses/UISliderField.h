#ifndef _UI_SLIDER_FIELD_H_
#define _UI_SLIDER_FIELD_H_

// A parameter row in the family synth style (PSPECTRA/PSPOLY):
//   label:    [########--__] 7F
// The bar is drawn with the PSPATTERN font's segment glyphs
// (codes 1-8 = partial fill, 9 = empty track), so it works anywhere
// the custom font is loaded. Editing behaves exactly like
// UIIntVarField (arrows +/- step, modifier for big step).

#include "UIIntVarField.h"

// How the number beside the bar reads. The bar itself moves on every
// keypress, so the readout is free to be the human summary rather
// than the raw byte.
enum SliderDisplay {
	// 0..100% for a parameter that spans a whole byte; the plain
	// number for a range that already means something (master 10..100)
	SD_AUTO,
	// L99 .. CTR .. R99. A pan reading "7F" tells you nothing about
	// which side of the room the sound is on.
	SD_PAN,
} ;

#define SLIDER_CELLS 12
// Width of the readout ("100%")
#define SLIDER_VALUE_CELLS 4
// Last column a slider may draw into by default. DrawPanel puts its
// right border three pixels into column cx+cw, so a readout ending in
// that column is drawn straight through the frame line -- every
// slider has to stop at least one column short of its panel's edge.
#define SLIDER_LAST_COL 38
#define SLIDER_GLYPH_TRACK 9

class UISliderField: public UIIntVarField {
public:
	UISliderField(GUIPoint &position,Variable &v,const char *label,
	              int min,int max,int xOffset,int yOffset,
	              int barCells=SLIDER_CELLS,
	              SliderDisplay display=SD_AUTO,
	              int lastCol=SLIDER_LAST_COL) ;
	virtual ~UISliderField() {} ;
	virtual void Draw(GUIWindow &w,int offset=0) ;
private:
	int barCells_ ;
	SliderDisplay display_ ;
	int lastCol_ ;
} ;
#endif
