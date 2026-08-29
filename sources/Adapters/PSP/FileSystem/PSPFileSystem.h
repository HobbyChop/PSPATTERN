
#ifndef _PSP_FILESYSTEM_H_ 
#define _PSP_FILESYSTEM_H_ 

#include "System/FileSystem/FileSystem.h"
#include <pspiofilemgr.h>

/* 32KB, not 1KB. A project save streams ~50KB of XML through this
   buffer, and at 1KB that was ~50 unaligned sceIoWrites to the Memory
   Stick -- the documented 1-2s input freeze on save. At 32KB it is two.
   Costs 31KB per OPEN file, and files are open briefly and one or two
   at a time. */
#define WRITE_BUFFER_SIZE 32768

class PSPFile: public I_File {
public:
	PSPFile(SceUID) ;
	virtual ~PSPFile() ;
	virtual int Read(void *ptr, int size, int nmemb) ;
	virtual int Write(const void *ptr, int size, int nmemb) ;
	virtual void Printf(const char *format,...);
	virtual void Seek(long offset,int whence) ;
	virtual long Tell() ;
	virtual void Close() ;
protected:
	void flush() ;
private:
	SceUID file_ ;
	unsigned char writeBuffer_[WRITE_BUFFER_SIZE] ;
	int writeBufferPos_ ;
	
} ;

class PSPDir: public I_Dir {
public:
    PSPDir(const char *path) ;
	virtual ~PSPDir() {} ;
    virtual void GetContent(char *mask) ;
} ;

class PSPFileSystem: public FileSystem {
public:
	virtual I_File *Open(const char *path,char *mode);
	virtual I_Dir *Open(const char *path) ;
	virtual FileType GetFileType(const char *path) ;
	virtual Result MakeDir(const char *path) ;
	virtual void Delete(const char *path) ;
	virtual bool Rename(const char *from,const char *to) ;
} ;
#endif
