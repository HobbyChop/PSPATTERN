#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "Foundation/T_Singleton.h"
#include "Foundation/Variables/VariableContainer.h"
#include "System/Console/Trace.h"
#include "Externals/TinyXML/tinyxml.h"

class Config: public T_Singleton<Config>,public VariableContainer {
public:
	Config() ;
	~Config() ;
	const char *GetValue(const char *key) ;
	void ProcessArguments(int argc,char **argv) ;
	// Write the current option values back to config.xml. Surgical on the
	// raw text so every comment and the hand-tuned layout survive: only
	// each key's value="..." is rewritten, and a key the file lacks is
	// appended before </CONFIG>. Returns false if the file can't be read
	// or written. Seed a new option (FindVariable/Insert + SetString)
	// before calling if you need to persist a key the file never had.
	bool Save() ;
} ;

#endif
