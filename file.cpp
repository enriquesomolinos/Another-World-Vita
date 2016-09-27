
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 * Vita version by Enrique Somolinos (https://github.com/enriquesomolinos)
  */

#include <sys/param.h>
#include "file.h"
#include "fs.h"
#include "util.h"
#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>

#ifdef PSVITA
	#include <psp2/io/dirent.h>
	#include <psp2/io/stat.h>
	#include <psp2/types.h>
	#include <debugnet.h>
#endif	

struct File_impl {
	bool _ioErr;
	File_impl() : _ioErr(false) {}
	virtual ~File_impl() {}
	virtual bool open(const char *path, const char *mode) = 0;
	virtual bool open(const char *path) = 0;
	virtual void close() = 0;
	virtual uint32_t size() = 0;
	virtual void seek(int off, int whence) = 0;
	virtual void seek2(int32_t off) = 0;
	virtual uint32_t read(void *ptr, uint32_t len) = 0;
	virtual uint32_t write(void *ptr, uint32_t len) = 0;
};

struct StdioFile : File_impl {
	FILE *_fp;
	StdioFile() : _fp(0) {}
	bool open(const char *path, const char *mode) {
		_ioErr = false;
		_fp = fopen(path, mode);
		return (_fp != 0);
	}
	bool open(const char *path) {
		return open(path,"rb");
	}
	void close() {
		if (_fp) {
			fclose(_fp);
			_fp = 0;
		}
	}
	uint32_t size() {
		uint32_t sz = 0;
		if (_fp) {
			int pos = ftell(_fp);
			fseek(_fp, 0, SEEK_END);
			sz = ftell(_fp);
			fseek(_fp, pos, SEEK_SET);
		}
		return sz;
	}
	void seek2(int32_t off) {
		if (_fp) {
			fseek(_fp, off, SEEK_SET);
		}
	}
	
	void seek(int off, int whence) {
		if (_fp) {
			fseek(_fp, off, whence);
		}
	}
	uint32_t read(void *ptr, uint32_t len) {
		if (_fp) {
			uint32_t r = fread(ptr, 1, len, _fp);
			if (r != len) {
				_ioErr = true;
			}
			return r;
		}
		return 0;
	}
	uint32_t write(void *ptr, uint32_t len) {
		if (_fp) {
			uint32_t r = fwrite(ptr, 1, len, _fp);
			if (r != len) {
				_ioErr = true;
			}
			return r;
		}
		return 0;
	}
};



File::File()
	: _impl(0) {
}

File::~File() {
	if (_impl) {
		_impl->close();
		delete _impl;
	}
}
bool File::open(const char *filepath) {
	_impl->close();
	_impl = new StdioFile;
	
	debugNetPrintf(DEBUG," _impl->open(filepath, ");
	return _impl->open(filepath, "rb");
}



bool File::open(const char *filename, const char *path) {
	FileSystem fs(path);
	char *mode="rb";
	if (_impl) {
		_impl->close();
		delete _impl;
		_impl = 0;
	}
	
	_impl = new StdioFile;
	char *path2 = fs.findPath(filename);
	if (path2) {
		bool ret = _impl->open(path2, mode);
		free(path2);
		return ret;
	}
	return false;
}


bool File::open(const char *filename, const char *mode, FileSystem *fs) {
	if (_impl) {
		_impl->close();
		delete _impl;
		_impl = 0;
	}
	assert(mode[0] != 'z');
	_impl = new StdioFile;
	char *path = fs->findPath(filename);
	if (path) {
		bool ret = _impl->open(path, mode);
		free(path);
		return ret;
	}
	return false;
}

bool File::open(const char *filename, const char *mode, const char *directory) {
	if (_impl) {
		_impl->close();
		delete _impl;
		_impl = 0;
	}

	if (!_impl) {
		_impl = new StdioFile;
	}
	char path[MAXPATHLEN];
	snprintf(path, sizeof(path), "%s/%s", directory, filename);
	return _impl->open(path, mode);
}

void File::close() {
	if (_impl) {
		_impl->close();
	}
}

bool File::ioErr() const {
	return _impl->_ioErr;
}

uint32_t File::size() {
	return _impl->size();
}
void File::seek(int off, int whence) {
	_impl->seek(off, whence);
}
void File::seek2(int32_t off) {
	_impl->seek2(off);
}


uint32_t File::read(void *ptr, uint32_t len) {
	return _impl->read(ptr, len);
}

uint8_t File::readByte() {
	uint8_t b;
	read(&b, 1);
	return b;
}

uint16_t File::readUint16LE() {
	uint8_t lo = readByte();
	uint8_t hi = readByte();
	return (hi << 8) | lo;
}

uint32_t File::readUint32LE() {
	uint16_t lo = readUint16LE();
	uint16_t hi = readUint16LE();
	return (hi << 16) | lo;
}

uint16_t File::readUint16BE() {
	uint8_t hi = readByte();
	uint8_t lo = readByte();
	return (hi << 8) | lo;
}

uint32_t File::readUint32BE() {
	uint16_t hi = readUint16BE();
	uint16_t lo = readUint16BE();
	return (hi << 16) | lo;
}

uint32_t File::write(void *ptr, uint32_t len) {
	return _impl->write(ptr, len);
}

void File::writeByte(uint8_t b) {
	write(&b, 1);
}

void File::writeUint16BE(uint16_t n) {
	writeByte(n >> 8);
	writeByte(n & 0xFF);
}

void File::writeUint32BE(uint32_t n) {
	writeUint16BE(n >> 16);
	writeUint16BE(n & 0xFFFF);
}

void dumpFile(const char *filename, const uint8_t *p, int size) {
	char path[MAXPATHLEN];
	snprintf(path, sizeof(path), "DUMP/%s", filename);
	FILE *fp = fopen(filename, "wb");
	if (fp) {
		const int wr = fwrite(p, 1, size, fp);
		if (wr != size) {
			warning("Failed to write %d bytes (expected %d)", wr, size);
		}
		fclose(fp);
	}
}

