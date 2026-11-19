#ifndef QTREGB_AUDIO_SETTINGS_H
#define QTREGB_AUDIO_SETTINGS_H

#include <string>

#include "types.h"

enum class LinkAudioSource : qtreGB::u8
{
	Player1 = 0,
	Player2,
	Both,
};

inline const char* link_audio_source_name(LinkAudioSource source)
{
	switch(source)
	{
		case LinkAudioSource::Player1: return "p1";
		case LinkAudioSource::Player2: return "p2";
		case LinkAudioSource::Both: return "both";
	}
	return "p1";
}

inline bool parse_link_audio_source(const std::string& text,
	LinkAudioSource& source)
{
	if(text == "p1" || text == "player1" || text == "player 1")
	{
		source = LinkAudioSource::Player1;
		return true;
	}
	if(text == "p2" || text == "player2" || text == "player 2")
	{
		source = LinkAudioSource::Player2;
		return true;
	}
	if(text == "both" || text == "ambos")
	{
		source = LinkAudioSource::Both;
		return true;
	}
	return false;
}

struct AudioSettings
{
	bool enabled = true;
	int volume = 50;
	LinkAudioSource link_source = LinkAudioSource::Player1;
};

#endif
