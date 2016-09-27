
/*
* Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 * Vita version by Enrique Somolinos (https://github.com/enriquesomolinos)
 */

#ifndef FS_H__
#define FS_H__

#include "intern.h"

struct FileSystem_impl;

struct FileSystem {
	FileSystem(const char *dataPath);
	~FileSystem();

	FileSystem_impl *_impl;

	char *findPath(const char *filename) const;
	bool exists(const char *filename) const;
};

#endif // FS_H__

