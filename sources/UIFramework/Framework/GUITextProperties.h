#ifndef _GUI_TEXT_PROPERTIES_H_
#define _GUI_TEXT_PROPERTIES_H_

struct GUITextProperties {
       GUITextProperties():invert_(false),transparent_(false) {} ;
       bool invert_ ;
       // draw ink only, leave background pixels untouched — lets text
       // float over the pixel-overlay panels/strips
       bool transparent_ ;
};

#endif
