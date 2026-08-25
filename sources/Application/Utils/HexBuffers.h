#ifndef _HEX_BUFFERS_H_
#define _HEX_BUFFERS_H_

#include "Externals/TinyXML/tinyxml.h"

void saveHexBuffer(TiXmlNode *parent,const char *nodeName,unsigned char *src,unsigned len) ;
void saveHexBuffer(TiXmlNode *parent,const char *nodeName,unsigned short *src,unsigned len) ;
void saveHexBuffer(TiXmlNode *parent,const char *nodeName,unsigned int *src,unsigned len) ;
// dstSize is REQUIRED: the chunk lengths come straight out of the project
// file, so without it a corrupt or hand-edited save writes anywhere in
// memory. Anything past the end is discarded.
void restoreHexBuffer(TiXmlNode *node,unsigned char *dst,unsigned dstSize) ;

#endif
