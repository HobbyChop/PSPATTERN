#include "PersistencyService.h"
#include "Persistent.h"
#include "Externals/Compression/lz.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"
#include "Checksum.h"

// A whole project is a few hundred KB of XML; anything claiming more than
// this is corrupt or hostile, not a song.
#define MAX_UNCOMPRESSED_SAVE (16*1024*1024)

PersistencyService::PersistencyService():Service(MAKE_FOURCC('S','V','P','S')) {
} ;

unsigned int PersistencyService::Checksum() {
	unsigned int h=CHECKSUM_SEED ;
	IteratorPtr<SubService> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
		h=currentItem->Checksum(h) ;
	} ;
	return h ;
} ;

bool PersistencyService::Save(const char *name) {

    Path filename(name);

    // Write to a temporary file first. A failure part way through used to
    // leave the real save truncated or spliced, with the previous good
    // version already gone.
    std::string tmpPath=filename.GetPath()+".tmp" ;

    TiXmlDocument doc(tmpPath);
    TiXmlElement first("LITTLEGPTRACKER") ;
	TiXmlNode *node=doc.InsertEndChild(first) ;

	// Loop on all registered service
	// accumulating XML flow
	
	IteratorPtr<SubService> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
		currentItem->Save(node) ;
	} ;

	if (!doc.SaveFile()) {
		Trace::Error("could not write %s",tmpPath.c_str()) ;
		return false ;
	}

	// Only once the new file is complete does the old one go away.
	FileSystem *fs=FileSystem::GetInstance() ;
	fs->Delete(filename.GetPath().c_str()) ;
	if (fs->Rename(tmpPath.c_str(),filename.GetPath().c_str())) {
		return true ;
	}

	// The platform has no rename: write straight to the real path instead.
	// Less safe, but never worse than the behaviour this replaced.
	Trace::Log("SAVE","no rename on this platform, writing in place") ;
	TiXmlDocument direct(filename.GetPath()) ;
	TiXmlElement root("LITTLEGPTRACKER") ;
	TiXmlNode *dnode=direct.InsertEndChild(root) ;
	IteratorPtr<SubService> it2(GetIterator()) ;
	for (it2->Begin();!it2->IsDone();it2->Next()) {
		Persistent *currentItem=(Persistent *)&it2->CurrentItem() ;
		currentItem->Save(dnode) ;
	} ;
	if (!direct.SaveFile()) {
		Trace::Error("could not write %s",filename.GetPath().c_str()) ;
		return false ;
	}
	fs->Delete(tmpPath.c_str()) ;
	return true ;
};

bool PersistencyService::Load(const char *name) {

	Path filename(name) ;
	PersistencyDocument doc( filename.GetPath() );

	// Try opening the file
	
	FileSystem *fs=FileSystem::GetInstance() ;
	I_File *file=fs->Open(filename.GetPath().c_str(),"r") ;
	if (!file) return false ;
	
	// get file size and read all buffer
	
	file->Seek(0,SEEK_END) ;
	int length=file->Tell() ;

	// +1: TinyXML parses to the NUL — an unterminated buffer runs
	// into heap garbage, the parse "fails", and the LZ fallback
	// below then shreds memory decompressing plain XML
	unsigned char *compBuffer=(unsigned char *)SYS_MALLOC(length+1) ;

  file->Seek(0,SEEK_SET) ;
	file->Read(compBuffer,1,length) ;
	compBuffer[length]=0 ;
	file->Close();
	delete file ;

	// the LZ fallback is only for actual compressed saves — never
	// feed it something that already looks like XML
	if (!doc.Parse((char *)compBuffer) && length>4 && compBuffer[0]!='<') {
        
		// Get uncompressed buffer size from first byte
		
		int offset=sizeof(int) ;
		int fullLength ;
		memcpy(&fullLength,compBuffer,offset) ;

		// fullLength is the first four bytes of the file and nothing has
		// checked it. LZ_Uncompress has no output bound of its own, so a
		// bogus header writes as far as the stream expands.
		if ((fullLength<=0)||(fullLength>MAX_UNCOMPRESSED_SAVE)) {
			Trace::Error("save header claims %d bytes: refusing",fullLength) ;
			SYS_FREE(compBuffer) ;
			return false ;
		}

		// Allocate a buffer to decompress data
		
		unsigned char *xmlSource=(unsigned char *)SYS_MALLOC(fullLength) ;
		if (!xmlSource) {
			Trace::Error("could not allocate space for %d bytes") ;
			return false ;
		}

    LZ_Uncompress(compBuffer+offset,xmlSource,length-offset);

		// Initialize XML document on decompressed buffer
		doc.Parse((char *)xmlSource) ;

		SYS_FREE(xmlSource) ;

		
	} ; 
	SYS_FREE(compBuffer) ;

	TiXmlNode* node = 0;
	node = doc.FirstChild( "LITTLEGPTRACKER" );
	if (!node) {
		Trace::Error("could not find master node") ;
		return false ;
	};

	TiXmlElement* element =node->ToElement();
	node = element->FirstChildElement() ;
	if (node) {
		element = node->ToElement();
		while (element) {
			IteratorPtr<SubService> it(GetIterator()) ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
				if (currentItem->Restore(element)) {
					break ;
				} ;
			}
			element = element->NextSiblingElement();
		} ;
	}
	return true ;
} ;
