#ifndef _PERSISTENCY_SERVICE_H_
#define _PERSISTENCY_SERVICE_H_

#include "Foundation/Services/Service.h"
#include "Foundation/T_Singleton.h"
 #include "Externals/TinyXML/tinyxml.h"

class PersistencyService: public Service,public T_Singleton<PersistencyService> {
public:
	PersistencyService() ;
    // returns false if the project could not be written -- the caller must
    // tell the user, or a full Memory Stick looks exactly like success
    bool Save(const char *name = "project:lgptsav.dat");
    bool Load(const char *name = "project:lgptsav.dat") ;

    // Fold every registered Persistent's state into one number, so
    // autosave can tell whether anything has changed since the last
    // write without serialising the whole project to find out.
    unsigned int Checksum() ;
} ;

class PersistencyDocument: public TiXmlDocument {
public:
	PersistencyDocument(const char *filename):TiXmlDocument(filename) { version_=0 ;} ;
	PersistencyDocument(const std::string& filename):TiXmlDocument(filename) { version_=0 ;} ;
	int version_ ;
} ;
#endif
