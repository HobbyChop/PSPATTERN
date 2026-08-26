#ifndef _SRP_UPDATERS_H_
#define _SRP_UPDATERS_H_

#include "I_SRPUpdater.h"
#include "Foundation/Types/Types.h"


class VolumeRamp: public I_SRPUpdater {
public:
	VolumeRamp() {} ;
	virtual ~VolumeRamp() {} ;
	void SetData(float target,float speed,float start) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	fixed current_ ;
	fixed target_ ;
	fixed speed_ ;
} ;

class FCRamp: public I_SRPUpdater {
public:
	FCRamp() {} ;
	virtual ~FCRamp() {} ;
	void SetData(float target,float speed,float start) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	fixed current_ ;
	fixed target_ ;
	fixed speed_ ;
} ;

class FRRamp: public I_SRPUpdater {
public:
	FRRamp() {} ;
	virtual ~FRRamp() {} ;
	void SetData(float target,float speed,float start) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	fixed current_ ;
	fixed target_ ;
	fixed speed_ ;
} ;

class FBMixRamp: public I_SRPUpdater {
public:
	FBMixRamp() {} ;
	virtual ~FBMixRamp() {} ;
	void SetData(float target,float speed,float start) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	fixed current_ ;
	fixed target_ ;
	fixed speed_ ;
} ;

class FBTunRamp: public I_SRPUpdater {
public:
	FBTunRamp() {} ;
	virtual ~FBTunRamp() {} ;
	void SetData(float target,float speed,float start) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	fixed current_ ;
	fixed target_ ;
	fixed speed_ ;
} ;

class LogSpeedRamp: public I_SRPUpdater {
public:
	LogSpeedRamp() {} ;
	virtual ~LogSpeedRamp() {} ;
	void SetData(float target,float speed,float start) ;
	float GetCurrent() ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	fixed current_ ;
	fixed target_ ;
	fixed speed_ ;
} ;

class LinSpeedRamp: public I_SRPUpdater {
public:
	LinSpeedRamp() {} ;
	virtual ~LinSpeedRamp() {} ;
	void SetData(float target,float speed,float start) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	fixed current_ ;
	fixed target_ ;
	fixed speed_ ;
} ;

class Arp: public I_SRPUpdater {
public:
	Arp() {} ;
	virtual ~Arp() {} ;
	void SetData(uint data) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	uchar arp_[5] ;     // Arp setting
	uchar arpPosition_ ;// Position of in the arpegiator
	uchar arpLength_ ;  // Length of arp data
	fixed current_  ;
} ;

class Panner: public I_SRPUpdater {
public:
	Panner() {} ;
	virtual ~Panner() {} ;
	void SetData(float target,float speed,float start) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	fixed current_ ;
	fixed target_ ;
	fixed speed_ ;
} ;


/* Left as a stub by the tracker this is forked from. Speed is a
   phase advance per tick and depth is in sixteenths of a semitone,
   so 10 is one semitone and 02 the smallest wobble worth having. */
class Vibrato: public I_SRPUpdater {
public:
	Vibrato() { phase_=0 ; speed_=0 ; depth_=0 ; } ;
	virtual ~Vibrato() {} ;
	void SetData(uint data) ;
	virtual void Trigger(bool tableTick) ;
	virtual void UpdateSRP(struct RUParams &rup) ;
private:
	ushort phase_ ;
	ushort speed_ ;
	uchar depth_ ;
	fixed current_ ;
} ;

#endif
