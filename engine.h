
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
  * Vita version by Enrique Somolinos (https://github.com/enriquesomolinos)
 */

#ifndef ENGINE_H__
#define ENGINE_H__

#include "intern.h"
#include "script.h"
#include "mixer.h"
#include "sfxplayer.h"
#include "resource.h"
#include "video.h"

struct Graphics;
struct SystemStub;

struct Engine {

	Graphics *_graphics;
	SystemStub *_stub;
	Script _log;
	Mixer _mix;
	Resource _res;
	SfxPlayer _ply;
	Video _vid;
	int _partNum;

	Engine(Graphics *graphics, SystemStub *stub, const char *dataDir, int partNum);

	const char *getGameTitle(Language lang) const { return _res.getGameTitle(lang); }

	void run(Language lang);
	void setup();
	void finish();
	void processInput();
	
	void saveGameState(uint8_t slot, const char *desc);
	void loadGameState(uint8_t slot);
};

#endif
