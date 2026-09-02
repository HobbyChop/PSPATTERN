#include "SoundFontManager.h"
#include "System/System/System.h"
#include "System/FileSystem/FileSystem.h"

SoundFontManager::SoundFontManager() {
	for (int i=0;i<MAX_SOUNDFONTS;i++) bankBytes_[i]=0 ;
} ;

SoundFontManager::~SoundFontManager() {
} ;

void SoundFontManager::UnloadBank(sfBankID id) {
	if (id<0||id>=MAX_SOUNDFONTS) return ;
	std::vector<void *> &data=sampleData_[id] ;
	for (size_t i=0;i<data.size();i++) {
		SAFE_FREE(data[i]) ;
	}
	data.clear() ;
	sfUnloadSFBank(id) ;
	bankPath_[id].clear() ;
	bankBytes_[id]=0 ;
}

void SoundFontManager::Reset() {
	for (sfBankID i=0;i<MAX_SOUNDFONTS;i++) UnloadBank(i) ;
} ;

const char *SoundFontManager::GetBankPath(sfBankID id) {
	if (id<0||id>=MAX_SOUNDFONTS||bankPath_[id].empty()) return 0 ;
	return bankPath_[id].c_str() ;
}

long SoundFontManager::GetBankBytes(sfBankID id) {
	if (id<0||id>=MAX_SOUNDFONTS) return 0 ;
	return bankBytes_[id] ;
}

/*
  Returns a nonnegative short or an element of
  {-SF_BANK_TABLE_FULL, -SF_LOAD_ERROR, -SF_OPEN_ERROR}.
 */
sfBankID SoundFontManager::LoadBank(const char *path) {

	sfBankID id=sfReadSFBFile((char *)path) ;
	if (id==-1) {
        enaErrors err_code = sfGetError();
        return -(err_code == enaLOADERROR ? SF_LOAD_ERROR : SF_BANK_TABLE_FULL);
	}
	if (id<0||id>=MAX_SOUNDFONTS) return -SF_LOAD_ERROR ;

	// open the file

	I_File *fin=FileSystem::GetInstance()->Open(path,"r") ;
	if (!fin) {
        return -SF_OPEN_ERROR;
    }

	// Grab the sample offset

	long offset=sfGetSMPLOffset(id) ;

	// Grab the sample headerzz

	WORD headerCount=0 ;
	SFSAMPLEHDRPTR  &headers=sfGetSampHdrs(id,&headerCount ); 

	// Loop on every sample, load them and adapt the pointers

	for (int i=0;i<headerCount;i++) {

		sfSampleHdr &current=headers[i] ;

		long from=current.dwStart*2+offset ;
		long to=current.dwEnd*2+offset ;
		
		int byteSize=to-from ;

		void *buffer=malloc(byteSize) ;

		if (buffer) {
			fin->Seek(from,SEEK_SET) ;
			fin->Read(buffer,byteSize,1) ;
			bankBytes_[id]+=byteSize ;
		}

		// now adapt the headers so the start is the memory point
		// and all others are sample offset

		current.dwEnd=(current.dwEnd-current.dwStart) ;
		current.dwStartloop=(current.dwStartloop-current.dwStart) ;
		current.dwEndloop=(current.dwEndloop-current.dwStart) ;
        // ADDR is pointer-sized, works on both 32-bit and 64-bit
        current.dwStart = (ADDR)buffer;

        sampleData_[id].push_back(buffer);
    }
	fin->Close() ;
	SAFE_DELETE(fin) ;

	bankPath_[id]=path ;
	return id ;
} ;
