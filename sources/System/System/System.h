#ifndef _SYSTEM_H_
#define _SYSTEM_H_

#include "Foundation/T_Factory.h"
#include "typedefs.h"
#include <stdlib.h>


// How often the free-memory probe actually runs, how far up it looks,
// and how close it bothers to get.
//
// The ceiling is above the largest model's user partition on purpose:
// a probe that saturates reports its own limit rather than the
// machine's, which reads as plenty of room no matter how full things
// are. A quarter megabyte of slop on a figure drawn as a bar is
// nothing, and every halving costs an allocation.
#define MEM_PROBE_MS       2000
#define MEM_PROBE_CEILING  (64u*1024*1024)
#define MEM_PROBE_GRAIN    (256u*1024)

class System: public T_Factory<System> {

public: // Override in implementation
	virtual unsigned long GetClock()=0 ; // millisecs
	/* The analog stick, centred: -128..127 per axis. Returns false
	   where there is none (desktop) -- the nub is a PSP browse control
	   (instrument type/engine flicks), polled by the UI ticker. */
	virtual bool GetAnalog(int &x,int &y) { return false ; }
	virtual int GetBatteryLevel()=0 ;
	virtual void *Malloc(unsigned size)=0 ;
	virtual void Free(void *)=0 ;
  virtual void Memset(void *addr,char value,int size)=0 ;
  virtual void *Memcpy(void *s1, const void *s2, int n)=0  ;
	virtual void PostQuitMessage()=0 ;
	virtual unsigned int GetMemoryUsage()=0 ;
	/* How many more bytes could still be allocated in one piece.
	   GetMemoryUsage on its own answers "how much have we taken" and
	   never "how much is left", which is the half a person loading
	   samples actually needs.

	   Measured by asking rather than by adding pools up. What
	   mallinfo's figures mean varies between libcs, and on the PSP the
	   heap claims its block from the kernel before main() runs, so
	   neither the heap's own accounting nor the kernel's sees the
	   whole picture: the heap reports the 3MB it has grown into and
	   the kernel reports the half megabyte outside the block, and both
	   are true and neither is the answer.

	   The largest block malloc will still hand over is the answer, and
	   trying is the only way to know it. Allocating one large block at
	   the top of the heap and freeing it again is also the least
	   fragmenting thing you can do, and inside an already claimed
	   block it takes nothing from anybody. */
	virtual unsigned int GetMemoryFree() {
		// Safe on this machine specifically, and it is worth saying
		// why. PSPSDK's _sbrk claims ONE partition block the first
		// time anything is allocated, sized to all the free memory
		// there is, and every malloc afterwards is carved out of that
		// block. So a large allocation here takes nothing from the
		// system: it moves the break inside memory the program
		// already owns, and freeing it hands it back to the heap.
		// That is also why the kernel's own free figure is useless to
		// us -- it reports what is left OUTSIDE the block, which is
		// half a megabyte on a machine with sixteen going spare.
		//
		// Drawn every frame; probing is a handful of allocations, so
		// it is answered from a cache between refreshes.
		unsigned long now=GetClock() ;
		if (memProbeAt_&&((now-memProbeAt_)<MEM_PROBE_MS)) return memProbe_ ;
		memProbeAt_=now?now:1 ;
		unsigned int lo=0,hi=MEM_PROBE_CEILING ;
		while ((hi-lo)>MEM_PROBE_GRAIN) {
			unsigned int mid=lo+((hi-lo)>>1) ;
			void *p=malloc(mid) ;
			if (p) { free(p) ; lo=mid ; } else { hi=mid ; }
		}
		memProbe_=lo ;
		return lo ;
	}

	System():memProbe_(0),memProbeAt_(0) {}
	virtual ~System() {}

private:
	unsigned int  memProbe_ ;
	unsigned long memProbeAt_ ;
} ;

#define SYS_MEMSET(a,b,c) { System* system=System::GetInstance() ; system->Memset(a,b,c) ;  }
#define SYS_MEMCPY(a,b,c) {  System* system=System::GetInstance() ; system->Memcpy(a,b,c) ; }
#define SYS_MALLOC(size) (System::GetInstance()->Malloc(size))
#define SYS_FREE(ptr) (System::GetInstance()->Free(ptr))

#define SAFE_DELETE(ptr) if (ptr)  { delete ptr ; ptr=0 ; }
#define SAFE_FREE(ptr) if (ptr) { SYS_FREE(ptr); ptr=0 ; }

#endif
