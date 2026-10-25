#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_CHAT_LOGIC_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_CHAT_LOGIC_H

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>

#include <base/math.h>
#include <base/vmath.h>
#include <engine/shared/protocol.h>

// Pure helpers of the RClient voice chat, unit tested without engine or SDK.
namespace VoiceLogic
{
	// Values of rc_voice_filter_unpositioned.
	enum
	{
		POSITION_FILTER_OFF = 0,
		POSITION_FILTER_PLAYERS = 1,
		POSITION_FILTER_ALL = 2,
	};

	inline int ParseClientId(const std::string &Metadata)
	{
		int Id = -1;
		const char *pEnd = Metadata.data() + Metadata.size();
		const auto Result = std::from_chars(Metadata.data(), pEnd, Id);
		return Result.ec == std::errc{} && Result.ptr == pEnd && Id >= 0 && Id < MAX_CLIENTS ? Id : -1;
	}

	inline std::string SanitizeRoomName(const char *pAddress, int RoomMode, const char *pRoomName)
	{
		if(pAddress == nullptr || pAddress[0] == '\0')
			return {};

		std::string RoomName = pAddress;
		if(RoomMode == 1 && pRoomName != nullptr && pRoomName[0] != '\0')
			RoomName = std::string(pRoomName) + "@" + RoomName;
		for(char &c : RoomName)
		{
			if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				   c == '.' || c == '-' || c == '_' || c == ':' || c == '@'))
				c = '_';
		}
		return RoomName;
	}

	inline vec2 VoiceGains(bool HasPosition, bool Spatial, bool Mute, vec2 Delta, int MinVolume, int MinRadius, int Radius, int MasterVolume, int StereoWidth, int StereoRadius)
	{
		if(!std::isfinite(Delta.x) || !std::isfinite(Delta.y))
			return vec2(0.0f, 0.0f);

		const float Minimum = std::clamp(MinVolume, 0, 100) / 100.0f;
		float Volume = HasPosition ? 1.0f : (Mute ? 0.0f : Minimum);
		float Pan = 0.0f;
		if(HasPosition && Spatial)
		{
			const float Outer = std::clamp(Radius, 1, 200) * 32.0f;
			const float Inner = std::clamp(MinRadius, 1, std::clamp(Radius, 1, 200)) * 32.0f;
			const float Distance = length(Delta);
			Volume = Distance >= Outer ? 0.0f : 1.0f + (Minimum - 1.0f) * std::min(Distance / Inner, 1.0f);
			Pan = std::clamp(Delta.x / (std::clamp(StereoRadius, 1, 200) * 32.0f) * (std::clamp(StereoWidth, 0, 200) / 100.0f), -1.0f, 1.0f);
		}
		const float Angle = (Pan + 1.0f) * (pi / 4.0f);
		return vec2(std::cos(Angle), std::sin(Angle)) * (Volume * (std::clamp(MasterVolume, 0, 400) / 100.0f));
	}

	inline float LimiterTarget(float Peak)
	{
		if(!std::isfinite(Peak) || Peak <= 0.9f)
			return 1.0f;
		return 0.9f / Peak;
	}

	struct SParticipantMix
	{
		bool SessionActive = true;
		bool TrackMuted = false;
		bool NameMuted = false;
		bool IsLocal = false;
		bool HasPosition = false;
		bool IsSpectator = false;
		bool TeamOnly = false;
		bool MyTeamValid = false;
		bool TeamMismatch = false;
		int FilterMode = POSITION_FILTER_PLAYERS;
	};

	// Shown next to a muted participant in the HUD.
	enum EMuteReason
	{
		MUTE_NONE = 0,
		MUTE_SESSION,
		MUTE_TRACK,
		MUTE_NAME,
		MUTE_LOCAL,
		MUTE_POSITION,
		MUTE_TEAM,
		MUTE_NO_TRACK,
	};

	inline EMuteReason MuteReasonOf(const SParticipantMix &Mix)
	{
		if(!Mix.SessionActive)
			return MUTE_SESSION;
		if(Mix.TrackMuted)
			return MUTE_TRACK;
		if(Mix.NameMuted)
			return MUTE_NAME;
		if(Mix.IsLocal)
			return MUTE_LOCAL;

		// Spectators never have a character, so the position filter keeps them
		// audible in mode 1 and mutes them in mode 2.
		const bool MuteByPosition = !Mix.HasPosition &&
					    (Mix.FilterMode == POSITION_FILTER_ALL ||
						    (Mix.FilterMode == POSITION_FILTER_PLAYERS && !Mix.IsSpectator));
		if(MuteByPosition)
			return MUTE_POSITION;

		if(Mix.TeamOnly && Mix.MyTeamValid && !Mix.IsSpectator && Mix.TeamMismatch)
			return MUTE_TEAM;

		return MUTE_NONE;
	}

	inline bool ShouldMute(const SParticipantMix &Mix)
	{
		return MuteReasonOf(Mix) != MUTE_NONE;
	}

	inline const char *MuteReasonLabel(EMuteReason Reason)
	{
		switch(Reason)
		{
		case MUTE_SESSION: return "no session";
		case MUTE_TRACK: return "muted";
		case MUTE_NAME: return "muted by you";
		case MUTE_LOCAL: return "you";
		case MUTE_POSITION: return "no pos";
		case MUTE_TEAM: return "other team";
		case MUTE_NO_TRACK: return "no audio";
		case MUTE_NONE: break;
		}
		return nullptr;
	}

	// Delay before the next automatic connect attempt.
	inline float NextAutoConnectDelay(int Failures)
	{
		return std::min(300.0f, 10.0f * (float)(1 << std::clamp(Failures, 0, 6)));
	}

	inline std::string DecodeBase64Url(const std::string &Input)
	{
		std::string Encoded;
		Encoded.reserve(Input.size() + 4);
		for(char c : Input)
		{
			if(c == '-')
				Encoded.push_back('+');
			else if(c == '_')
				Encoded.push_back('/');
			else if(c == '=')
				continue;
			else if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/')
				Encoded.push_back(c);
			else
				return {};
		}
		while(Encoded.size() % 4 != 0)
			Encoded.push_back('=');

		auto Value = [](char c) -> int {
			if(c >= 'A' && c <= 'Z')
				return c - 'A';
			if(c >= 'a' && c <= 'z')
				return c - 'a' + 26;
			if(c >= '0' && c <= '9')
				return c - '0' + 52;
			if(c == '+')
				return 62;
			if(c == '/')
				return 63;
			return -1;
		};

		std::string Output;
		Output.reserve(Encoded.size() / 4 * 3);
		for(size_t i = 0; i + 3 < Encoded.size(); i += 4)
		{
			const int a = Value(Encoded[i]), b = Value(Encoded[i + 1]), c = Value(Encoded[i + 2]), d = Value(Encoded[i + 3]);
			if(a < 0 || b < 0)
				return {};
			Output.push_back((char)((a << 2) | (b >> 4)));
			if(Encoded[i + 2] != '=')
			{
				if(c < 0)
					return {};
				Output.push_back((char)(((b & 0x0F) << 4) | (c >> 2)));
				if(Encoded[i + 3] != '=')
				{
					if(d < 0)
						return {};
					Output.push_back((char)(((c & 0x03) << 6) | d));
				}
			}
		}
		return Output;
	}

	// Unix seconds from the "exp" claim, 0 if absent.
	inline long long TokenExpiry(const std::string &Token)
	{
		const auto FirstDot = Token.find('.');
		if(FirstDot == std::string::npos)
			return 0;
		const auto SecondDot = Token.find('.', FirstDot + 1);
		if(SecondDot == std::string::npos)
			return 0;

		const std::string Payload = DecodeBase64Url(Token.substr(FirstDot + 1, SecondDot - FirstDot - 1));
		if(Payload.empty())
			return 0;

		const std::string Key = "\"exp\"";
		size_t Pos = Payload.find(Key);
		while(Pos != std::string::npos)
		{
			size_t Cursor = Pos + Key.size();
			while(Cursor < Payload.size() && (Payload[Cursor] == ' ' || Payload[Cursor] == '\t' || Payload[Cursor] == '\n' || Payload[Cursor] == '\r'))
				++Cursor;
			if(Cursor < Payload.size() && Payload[Cursor] == ':')
			{
				++Cursor;
				while(Cursor < Payload.size() && (Payload[Cursor] == ' ' || Payload[Cursor] == '\t' || Payload[Cursor] == '\n' || Payload[Cursor] == '\r'))
					++Cursor;
				long long Expiry = 0;
				const auto Result = std::from_chars(Payload.data() + Cursor, Payload.data() + Payload.size(), Expiry);
				if(Result.ec == std::errc{} && Expiry > 0)
					return Expiry;
			}
			Pos = Payload.find(Key, Pos + 1);
		}
		return 0;
	}

	// The SDK crashes on an expired token instead of failing, so check it first.
	inline bool TokenUnusable(const std::string &Token, long long NowUnixSeconds, long long MarginSeconds = 30)
	{
		if(Token.empty())
			return true;
		const long long Expiry = TokenExpiry(Token);
		if(Expiry <= 0)
			return true;
		return NowUnixSeconds + MarginSeconds >= Expiry;
	}

	inline int DeviceDelayMs(unsigned PeriodSizeInFrames, unsigned Periods, unsigned SampleRate)
	{
		const unsigned Rate = std::max(1u, SampleRate);
		return int(1000.0 * PeriodSizeInFrames * Periods / Rate);
	}
}

#endif // GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_CHAT_LOGIC_H
