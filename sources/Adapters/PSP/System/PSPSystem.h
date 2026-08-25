#ifndef _PSP_SYSTEM_H_
#define _PSP_SYSTEM_H_

#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"

class PSPSystem: public System {
public:
	static void Boot(int argc,char **argv) ;
	static void Shutdown() ;
	static int MainLoop() ;

public: // System implementation
	virtual unsigned long GetClock() ;
	virtual void Sleep(int millisec);
	virtual void *Malloc(unsigned size) ;
	virtual void Free(void *) ;
  virtual void Memset(void *addr,char val,int size) ;
  virtual void *Memcpy(void *s1, const void *s2, int n)  ; 
	// the song screen's battery readout was dead because this returned -1
	// on the only platform where it means anything
	virtual int GetBatteryLevel() ;
	virtual void PostQuitMessage() ;
	virtual unsigned int GetMemoryUsage() ;
	
	static bool finished_ ;
private:
	static std::string eboot_ ;
  static EventManager *eventManager_;

} ;
#endif
