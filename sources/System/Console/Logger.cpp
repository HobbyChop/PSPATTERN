#include "Logger.h"
#include <iostream>

void StdOutLogger::AddLine(const char *line)
{
	std::cout << line << std::endl ;
}

// ----------------------------------------------

FileLogger::FileLogger(const Path &path)
:path_(path)
,file_(0)
{
}

FileLogger::~FileLogger()
{
  if (file_)
  {
    fclose(file_);
  }
}

Result FileLogger::Init()
{
	file_= fopen(path_.GetPath().c_str(),"w") ;
  if (!file_)
  {
    return Result("Failed to open log file");
  }
  // stays OPEN. A line used to be fopen/append/fclose -- three FAT
  // transactions on the Memory Stick, 10-60ms, and any Trace on the
  // render path was an instant audible underrun with LOG=YES. Buffered
  // stdio flushes in ~4KB clumps instead; a crash can cost the tail of
  // the log, which a debug facility can afford.
  return Result::NoError;
}

void FileLogger::AddLine(const char *line)
{
	if (!file_) return ;
	fprintf(file_,"%s\n",line) ;
}