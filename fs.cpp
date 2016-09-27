
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 * Vita version by Enrique Somolinos (https://github.com/enriquesomolinos)
  */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
//#include <dirent.h>
#include <sys/stat.h>
#include <sys/param.h>
#endif

#ifdef PSVITA
	#include <psp2/io/dirent.h>
	#include <psp2/io/stat.h>
	#include <psp2/types.h>
#endif	
#include "fs.h"
#include "util.h"

struct FileName {
	char *name;
	int dir;
};

struct FileSystem_impl {

	char **_dirsList;
	int _dirsCount;
	FileName *_filesList;
	int _filesCount;

	FileSystem_impl() :
		_dirsList(0), _dirsCount(0), _filesList(0), _filesCount(0) {
	}

	~FileSystem_impl() {
		for (int i = 0; i < _dirsCount; ++i) {
			free(_dirsList[i]);
		}
		free(_dirsList);
		for (int i = 0; i < _filesCount; ++i) {
			free(_filesList[i].name);
		}
		free(_filesList);
	}

	void setRootDirectory(const char *dir) {
		getPathListFromDirectory(dir);
		
	}

	char *findPath(const char *name) const {
		for (int i = 0; i < _filesCount; ++i) {
			if (strcasecmp(_filesList[i].name, name) == 0) {
				const char *dir = _dirsList[_filesList[i].dir];
				const int len = strlen(dir) + 1 + strlen(_filesList[i].name) + 1;
				char *p = (char *)malloc(len);
				if (p) {
					snprintf(p, len, "%s/%s", dir, _filesList[i].name);
				}
				return p;
			}
		}
		return 0;
	}

	void addPath(const char *dir, const char *name) {
		int index = -1;
		for (int i = 0; i < _dirsCount; ++i) {
			if (strcmp(_dirsList[i], dir) == 0) {
				index = i;
				break;
			}
		}
		if (index == -1) {
			_dirsList = (char **)realloc(_dirsList, (_dirsCount + 1) * sizeof(char *));
			if (_dirsList) {
				_dirsList[_dirsCount] = strdup(dir);
				index = _dirsCount;
				++_dirsCount;
			}
		}
		_filesList = (FileName *)realloc(_filesList, (_filesCount + 1) * sizeof(FileName));
		if (_filesList) {
			_filesList[_filesCount].name = strdup(name);
			_filesList[_filesCount].dir = index;
			++_filesCount;
		}
	}

	void getPathListFromDirectory(const char *dir);
};

#ifdef _WIN32
void FileSystem_impl::getPathListFromDirectory(const char *dir) {
	WIN32_FIND_DATA findData;
	char searchPath[MAX_PATH];
	snprintf(searchPath, sizeof(searchPath), "%s/*", dir);
	HANDLE h = FindFirstFile(searchPath, &findData);
	if (h) {
		do {
			if (findData.cFileName[0] == '.') {
				continue;
			}
			char filePath[MAX_PATH];
			snprintf(filePath, sizeof(filePath), "%s/%s", dir, findData.cFileName);
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				getPathListFromDirectory(filePath);
			} else {
				addPath(dir, findData.cFileName);
			}
		} while (FindNextFile(h, &findData));
		FindClose(h);
	}
}
#else
	#ifdef PSVITA
	void FileSystem_impl::getPathListFromDirectory(const char *dir) {
		SceUID d = sceIoDopen(dir);
		
		if (d) {		
			SceIoDirent de;
			memset(&de, 0, sizeof(de));	
			
			
			while (sceIoDread(d,&de)>0) {
				if (de.d_name[0] == '.') {
					continue;
				}
				char filePath[MAXPATHLEN];
				snprintf(filePath, sizeof(filePath), "%s/%s", dir, de.d_name);
				SceIoStat st =de.d_stat;			
					if (st.st_attr ==SCE_SO_IFDIR) {
						getPathListFromDirectory(filePath);
					} else {
						addPath(dir, de.d_name);
					}
				
			}
			sceIoDclose(d);
		}
	}
	#else
	void FileSystem_impl::getPathListFromDirectory(const char *dir) {
		DIR *d = opendir(dir);
		if (d) {
			dirent *de;
			while ((de = readdir(d)) != NULL) {
				if (de->d_name[0] == '.') {
					continue;
				}
				char filePath[MAXPATHLEN];
				snprintf(filePath, sizeof(filePath), "%s/%s", dir, de->d_name);
				struct stat st;
				if (stat(filePath, &st) == 0) {
					if (S_ISDIR(st.st_mode)) {
						getPathListFromDirectory(filePath);
					} else {
						addPath(dir, de->d_name);
					}
				}
			}
			closedir(d);
		}
	}
	#endif
#endif

FileSystem::FileSystem(const char *dataPath) {
	_impl = new FileSystem_impl;
	_impl->setRootDirectory(dataPath);
}

FileSystem::~FileSystem() {
	delete _impl;
}

char *FileSystem::findPath(const char *filename) const {
	return _impl->findPath(filename);
}

bool FileSystem::exists(const char *filename) const {
	char *path = findPath(filename);
	if (path) {
		free(path);
	}
	return path != 0;
}
