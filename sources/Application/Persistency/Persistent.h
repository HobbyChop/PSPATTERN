#ifndef _PERSISTENT_H_
#define _PERSISTENT_H_

#include "Foundation/Services/SubService.h"
#include "Externals/TinyXML/tinyxml.h"

class Persistent:SubService {
public:
	Persistent(const char *nodeName) ;
	void Save(TiXmlNode *node) ;
	bool Restore(TiXmlElement *element) ;

	// Fold this object's savable state into h. Pure on purpose: a new
	// savable class that forgets to take part would quietly stop
	// autosave from noticing its edits, so it has to not compile
	// instead. See Checksum.h.
	virtual unsigned int Checksum(unsigned int h) = 0 ;
protected:
	virtual void SaveContent(TiXmlNode *node)=0 ;
	virtual void RestoreContent(TiXmlElement *element)=0 ;
private:
	const char *nodeName_ ;
} ;

#endif
