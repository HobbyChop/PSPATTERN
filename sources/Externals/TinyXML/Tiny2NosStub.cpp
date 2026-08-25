#include "Tiny2NosStub.h"
#include "System/Console/Trace.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void fprintf(I_File *f,char *fmt,...) {
     char stackBuf[1024] ;
     va_list args;

     // Every element of the save file is written through here. The
     // buffer was 256 bytes and the call was vsprintf, which is smaller
     // than plenty of the strings that pass through -- a long sample
     // path ran straight off the end. Measure first, and fall back to
     // the heap rather than truncate, because a truncated element is
     // malformed XML and costs the whole project.
     va_start(args,fmt);
     int len=vsnprintf(stackBuf,sizeof(stackBuf),fmt,args);
     va_end(args);
     if (len<0) return ;

     if (len<(int)sizeof(stackBuf)) {
          f->Write(stackBuf,1,len) ;
          return ;
     }

     char *heapBuf=(char *)malloc(len+1) ;
     if (!heapBuf) {
          f->Write(stackBuf,1,sizeof(stackBuf)-1) ;
          return ;
     }
     va_start(args,fmt);
     vsnprintf(heapBuf,len+1,fmt,args);
     va_end(args);
     f->Write(heapBuf,1,len) ;
     free(heapBuf) ;

} ;
