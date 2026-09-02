#ifndef _SOUND_FONT_MANAGER_H_
#define _SOUND_FONT_MANAGER_H_

#include "Foundation/T_Singleton.h"
#include "Externals/Soundfont/ENAB.H"
#include <vector>
#include <string>

#define MAX_SOUNDFONTS MAXLOADEDBANKS

enum SFManagerError {
    SF_BANK_TABLE_FULL = 1,
    SF_LOAD_ERROR = 2,
    SF_OPEN_ERROR = 3,
};

class SoundFontManager : public T_Singleton<SoundFontManager> {
  public:
	SoundFontManager() ;
	~SoundFontManager() ;
	void Reset() ;
	sfBankID LoadBank(const char *path) ;
	// what LoadBank was given, so the file can be found -- and
	// dropped -- by the bank it became. 0 for a slot never loaded.
	const char *GetBankPath(sfBankID id) ;
	// the sample data this bank holds in RAM
	long GetBankBytes(sfBankID id) ;
	// one bank out: its sample data freed, its slot free again. The
	// caller has dropped every preset of it from the pool already.
	void UnloadBank(sfBankID id) ;
private:
	// per bank, so one can go without the others
	std::vector<void *> sampleData_[MAX_SOUNDFONTS] ;
	std::string bankPath_[MAX_SOUNDFONTS] ;
	long bankBytes_[MAX_SOUNDFONTS] ;
};
#endif
