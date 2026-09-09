
#ifndef _SAMPLE_VARIABLE_H_
#define _SAMPLE_VARIABLE_H_

#include "Foundation/Variables/WatchedVariable.h"
#include "Foundation/Observable.h"

class SampleVariable:public WatchedVariable,public I_Observer {
public:
	SampleVariable(const char *name,FourCC id) ;
	~SampleVariable() ;
	/* Re-read the pool's list and try the stored name once more.
	   A name that did not resolve when the project was restored is
	   kept rather than thrown away, and this is what gives it a
	   second chance once the pool holds more than it did. */
	void ReResolve() ;
protected:
    virtual void Update(Observable &o,I_ObservableData *d) ;

} ;
#endif