
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include "sfxplayer.h"
#include "mixer.h"
#include "resource.h"
#include "systemstub.h"
#include "util.h"


SfxPlayer::SfxPlayer(Resource *res)
	: _res(res), _delay(0), _resNum(0) {
	_playing = false;
}

void SfxPlayer::setEventsDelay(uint16_t delay) {
	debug(DBG_SND, "SfxPlayer::setEventsDelay(%d)", delay);
	_delay = delay * 60 / 7050;
}

void SfxPlayer::loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos) {
	debug(DBG_SND, "SfxPlayer::loadSfxModule(0x%X, %d, %d)", resNum, delay, pos);
	MemEntry *me = &_res->_memList[resNum];
	if (me->status == Resource::STATUS_LOADED && me->type == 1) {
		_resNum = resNum;
		memset(&_sfxMod, 0, sizeof(SfxModule));
		_sfxMod.curOrder = pos;
		_sfxMod.numOrder = READ_BE_UINT16(me->bufPtr + 0x3E);
		debug(DBG_SND, "SfxPlayer::loadSfxModule() curOrder = 0x%X numOrder = 0x%X", _sfxMod.curOrder, _sfxMod.numOrder);
		for (int i = 0; i < 0x80; ++i) {
			_sfxMod.orderTable[i] = *(me->bufPtr + 0x40 + i);
		}
		if (delay == 0) {
			_delay = READ_BE_UINT16(me->bufPtr);
		} else {
			_delay = delay;
		}
		_delay = _delay * 60 / 7050;
		_sfxMod.data = me->bufPtr + 0xC0;
		debug(DBG_SND, "SfxPlayer::loadSfxModule() eventDelay = %d ms", _delay);
		prepareInstruments(me->bufPtr + 2);
	} else {
		warning("SfxPlayer::loadSfxModule() ec=0x%X", 0xF8);
	}
}

void SfxPlayer::prepareInstruments(const uint8_t *p) {
	memset(_sfxMod.samples, 0, sizeof(_sfxMod.samples));
	for (int i = 0; i < 15; ++i) {
		SfxInstrument *ins = &_sfxMod.samples[i];
		uint16_t resNum = READ_BE_UINT16(p); p += 2;
		if (resNum != 0) {
			ins->volume = READ_BE_UINT16(p);
			MemEntry *me = &_res->_memList[resNum];
			if (me->status == Resource::STATUS_LOADED && me->type == 0) {
				ins->data = me->bufPtr;
				debug(DBG_SND, "Loaded instrument 0x%X n=%d volume=%d", resNum, i, ins->volume);
			} else {
				error("Error loading instrument 0x%X", resNum);
			}
		}
		p += 2; // skip volume
	}
}

void SfxPlayer::play(int rate) {
	_playing = true;
	_rate = rate;
	_samplesLeft = 0;
	memset(_channels, 0, sizeof(_channels));
}

static void mixChannel(int8_t &s, SfxChannel *ch) {
	if (ch->sampleLen == 0) {
		return;
	}
	int pos1 = ch->pos.offset >> Frac::BITS;
	ch->pos.offset += ch->pos.inc;
	int pos2 = pos1 + 1;
	if (ch->sampleLoopLen != 0) {
		if (pos1 == ch->sampleLoopPos + ch->sampleLoopLen - 1) {
			pos2 = ch->sampleLoopPos;
			ch->pos.offset = pos2 << Frac::BITS;
		}
	} else {
		if (pos1 == ch->sampleLen - 1) {
			ch->sampleLen = 0;
			return;
		}
	}
	int16_t sample = ch->pos.interpolate((int8_t)ch->sampleData[pos1], (int8_t)ch->sampleData[pos2]);
	sample = s + sample * ch->volume / 64;
	if (sample < -128) {
		sample = -128;
	} else if (sample > 127) {
		sample = 127;
	}
	s = (int8_t)sample;
}

void SfxPlayer::mixSamples(int8_t *buf, int len) {
	memset(buf, 0, len * 2);
	const int samplesPerTick = _rate / (1000 / _delay);
	while (len != 0) {
		if (_samplesLeft == 0) {
			handleEvents();
			_samplesLeft = samplesPerTick;
		}
		int count = _samplesLeft;
		if (count > len) {
			count = len;
		}
		_samplesLeft -= count;
		len -= count;
		for (int i = 0; i < count; ++i) {
			mixChannel(*buf, &_channels[0]);
			mixChannel(*buf, &_channels[3]);
			++buf;
			mixChannel(*buf, &_channels[1]);
			mixChannel(*buf, &_channels[2]);
			++buf;
		}
	}
}

static void nr(const int8_t *in, int len, int8_t *out) {
	static int prevL = 0;
	static int prevR = 0;
	for (int i = 0; i < len; ++i) {
		const int sL = *in++ >> 1;
		*out++ = sL + prevL;
		prevL = sL;
		const int sR = *in++ >> 1;
		*out++ = sR + prevR;
		prevR = sR;
	}
}

void SfxPlayer::readSamples(int8_t *buf, int len) {
	if (_delay == 0) {
		memset(buf, 0, len * 2);
	} else {
		int8_t bufin[len * 2];
		mixSamples(bufin, len);
		nr(bufin, len, buf);
	}
}

void SfxPlayer::start() {
	debug(DBG_SND, "SfxPlayer::start()");
	_sfxMod.curPos = 0;
}

void SfxPlayer::stop() {
	debug(DBG_SND, "SfxPlayer::stop()");
	_playing = false;
	_resNum = 0;
}

void SfxPlayer::handleEvents() {
	uint8_t order = _sfxMod.orderTable[_sfxMod.curOrder];
	const uint8_t *patternData = _sfxMod.data + _sfxMod.curPos + order * 1024;
	for (uint8_t ch = 0; ch < 4; ++ch) {
		handlePattern(ch, patternData);
		patternData += 4;
	}
	_sfxMod.curPos += 4 * 4;
	debug(DBG_SND, "SfxPlayer::handleEvents() order = 0x%X curPos = 0x%X", order, _sfxMod.curPos);
	if (_sfxMod.curPos >= 1024) {
		_sfxMod.curPos = 0;
		order = _sfxMod.curOrder + 1;
		if (order == _sfxMod.numOrder) {
			_resNum = 0;
			_playing = false;
		}
		_sfxMod.curOrder = order;
	}
}

void SfxPlayer::handlePattern(uint8_t channel, const uint8_t *data) {
	SfxPattern pat;
	memset(&pat, 0, sizeof(SfxPattern));
	pat.note_1 = READ_BE_UINT16(data + 0);
	pat.note_2 = READ_BE_UINT16(data + 2);
	if (pat.note_1 != 0xFFFD) {
		uint16_t sample = (pat.note_2 & 0xF000) >> 12;
		if (sample != 0) {
			uint8_t *ptr = _sfxMod.samples[sample - 1].data;
			if (ptr != 0) {
				debug(DBG_SND, "SfxPlayer::handlePattern() preparing sample %d", sample);
				pat.sampleVolume = _sfxMod.samples[sample - 1].volume;
				pat.sampleStart = 8;
				pat.sampleBuffer = ptr;
				pat.sampleLen = READ_BE_UINT16(ptr) * 2;
				uint16_t loopLen = READ_BE_UINT16(ptr + 2) * 2;
				if (loopLen != 0) {
					pat.loopPos = pat.sampleLen;
					pat.loopLen = loopLen;
				} else {
					pat.loopPos = 0;
					pat.loopLen = 0;
				}
				int16_t m = pat.sampleVolume;
				uint8_t effect = (pat.note_2 & 0x0F00) >> 8;
				if (effect == 5) { // volume up
					uint8_t volume = (pat.note_2 & 0xFF);
					m += volume;
					if (m > 0x3F) {
						m = 0x3F;
					}
				} else if (effect == 6) { // volume down
					uint8_t volume = (pat.note_2 & 0xFF);
					m -= volume;
					if (m < 0) {
						m = 0;
					}
				}
				_channels[channel].volume = m;
				pat.sampleVolume = m;
			}
		}
	}
	if (pat.note_1 == 0xFFFD) {
		debug(DBG_SND, "SfxPlayer::handlePattern() _scriptVars[0xF4] = 0x%X", pat.note_2);
		*_markVar = pat.note_2;
	} else if (pat.note_1 != 0) {
		if (pat.note_1 == 0xFFFE) {
			_channels[channel].sampleLen = 0;
		} else if (pat.sampleBuffer != 0) {
			assert(pat.note_1 >= 0x37 && pat.note_1 < 0x1000);
			// convert amiga period value to hz
			uint16_t freq = 7159092 / (pat.note_1 * 2);
			debug(DBG_SND, "SfxPlayer::handlePattern() adding sample freq = 0x%X", freq);
			SfxChannel *ch = &_channels[channel];
			ch->sampleData = pat.sampleBuffer + pat.sampleStart;
			ch->sampleLen = pat.sampleLen;
			ch->sampleLoopPos = pat.loopPos;
			ch->sampleLoopLen = pat.loopLen;
			ch->volume = pat.sampleVolume;
			ch->pos.offset = 0;
			ch->pos.inc = (freq << Frac::BITS) / _rate;
		}
	}
}
