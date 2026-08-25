#ifndef _PROJECT_CHECKSUM_H_
#define _PROJECT_CHECKSUM_H_

#include <string.h>

/* FNV-1a, folded across everything a project saves.
 *
 * This exists so autosave can tell whether anything has actually
 * changed. The alternative was to mark a dirty flag at every point that
 * edits data, and there are two dozen of those spread across six
 * screens -- miss one and autosave silently stops covering that kind of
 * edit, which is the exact failure autosave is meant to prevent.
 * Hashing the model instead cannot miss an edit: if the bytes that get
 * saved differ, the checksum differs.
 *
 * Every Persistent implements Checksum(), so a savable class added
 * later cannot forget to take part -- it will not compile. */

#define CHECKSUM_SEED 2166136261u

static inline unsigned int checksumBytes(unsigned int h, const void *p, int len) {
	if ((!p) || (len <= 0)) return h ;
	const unsigned char *b = (const unsigned char *)p ;
	for (int i = 0 ; i < len ; i++) {
		h ^= b[i] ;
		h *= 16777619u ;
	}
	return h ;
}

static inline unsigned int checksumInt(unsigned int h, int v) {
	return checksumBytes(h, &v, sizeof(v)) ;
}

static inline unsigned int checksumString(unsigned int h, const char *s) {
	return s ? checksumBytes(h, s, (int)strlen(s)) : checksumInt(h, -1) ;
}

#endif
