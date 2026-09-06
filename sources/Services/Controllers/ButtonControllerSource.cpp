#include "ButtonControllerSource.h"
#include "System/Console/Trace.h"
#include <stdlib.h>

ButtonControllerSource::ButtonControllerSource(const char *name):ControllerSource("but",name) {
} ;

ButtonControllerSource::~ButtonControllerSource() {
} ;

Channel *ButtonControllerSource::GetChannel(const char *url) {
	int button=atoi(url) ;
	// the number comes from mapping.xml
	if ((button<0)||(button>=MAX_BUTTON)) return 0 ;
	return channel_+button ;
}

void ButtonControllerSource::SetButton(int button,bool value) 
{
	channel_[button].SetValue(value?1.0f:0.0f) ;
	channel_[button].NotifyObservers() ;
}
