#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_VOICE_CHAT_LOGIC_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_VOICE_CHAT_LOGIC_H

#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/teamscore.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

// Pure helpers of the RClient voice chat, unit tested without engine or SDK.
namespace VoiceLogic
{
	struct SOverlaySortKey
	{
		bool m_Speaking = false;
		std::int64_t m_StartedAt = 0;
		std::int64_t m_LastSoundAt = 0;
		int m_ClientId = -1;

		bool operator<(const SOverlaySortKey &Other) const
		{
			if(m_Speaking != Other.m_Speaking)
				return m_Speaking;
			if(m_Speaking && m_StartedAt != Other.m_StartedAt)
				return m_StartedAt < Other.m_StartedAt;
			if(!m_Speaking && m_LastSoundAt != Other.m_LastSoundAt)
				return m_LastSoundAt > Other.m_LastSoundAt;
			// Participants without a game client id come after known players.
			const int Id = m_ClientId >= 0 ? m_ClientId : MAX_CLIENTS;
			const int OtherId = Other.m_ClientId >= 0 ? Other.m_ClientId : MAX_CLIENTS;
			return Id < OtherId;
		}
	};

	// Transmission tail is separate from the overlay's speech hold time.
	class CPttTail
	{
		std::atomic<std::int64_t> m_Until{0};

	public:
		void Start(std::int64_t NowMs, int DelayMs)
		{
			const int Delay = std::clamp(DelayMs, 0, 1000);
			m_Until = Delay ? NowMs + Delay : 0;
		}
		void Cancel() { m_Until = 0; }
		bool Active(std::int64_t NowMs) const { return NowMs < m_Until.load(); }
	};

	inline float MicrophonePeak(const std::int16_t *pSamples, size_t Count)
	{
		int Peak = 0;
		for(size_t i = 0; i < Count; ++i)
			Peak = std::max(Peak, std::abs(static_cast<int>(pSamples[i])));
		return Peak / 32768.0f;
	}

	class CMetadataRetry
	{
		int m_FailedClientId = -1;
		int m_Failures = 0;
		double m_NextAttempt = 0.0;

	public:
		bool CanAttempt(int ClientId, double Now) const
		{
			return ClientId != m_FailedClientId || Now >= m_NextAttempt;
		}

		void OnFailure(int ClientId, double Now)
		{
			if(ClientId != m_FailedClientId)
				m_Failures = 0;
			m_FailedClientId = ClientId;
			m_Failures = std::min(m_Failures + 1, 5);
			m_NextAttempt = Now + std::min(30.0, 2.0 * (1 << (m_Failures - 1)));
		}

		void Reset() { *this = CMetadataRetry(); }
	};

	struct SUserSettings
	{
		std::string m_Name;
		int m_Volume = 100;
		bool m_Muted = false;
	};

	class CUserSettings
	{
		std::map<std::string, SUserSettings> m_Users;

	public:
		static std::string Name(const char *pName)
		{
			if(!pName)
				return {};
			const std::string Value = pName;
			const auto First = Value.find_first_not_of(" \t");
			if(First == std::string::npos)
				return {};
			return Value.substr(First, Value.find_last_not_of(" \t") - First + 1);
		}

		static std::string Key(const char *pName)
		{
			std::string Result = Name(pName);
			for(char &c : Result)
				if(c >= 'A' && c <= 'Z')
					c += 'a' - 'A';
			return Result;
		}

		SUserSettings Get(const char *pName) const
		{
			const auto It = m_Users.find(Key(pName));
			return It != m_Users.end() ? It->second : SUserSettings{Name(pName)};
		}

		bool Save(const char *pName, int Volume, bool Muted)
		{
			const std::string UserName = Name(pName);
			// Token names are limited to 64 UTF-8 bytes. Reject control characters
			// so that a saved console command always occupies a single line.
			if(UserName.empty() || UserName.size() > 64 || std::any_of(UserName.begin(), UserName.end(), [](unsigned char c) { return c < 32; }))
				return false;
			m_Users[Key(UserName.c_str())] = {UserName, std::clamp(Volume, 0, 200), Muted};
			return true;
		}

		void Remove(const char *pName) { m_Users.erase(Key(pName)); }

		std::vector<SUserSettings> All() const
		{
			std::vector<SUserSettings> Result;
			Result.reserve(m_Users.size());
			for(const auto &[Key, User] : m_Users)
				Result.push_back(User);
			return Result;
		}
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

	inline vec2 VoiceGains(bool HasPosition, bool Spatial, int UnpositionedVolume, vec2 Delta, int MinVolume, int FullVolumeRadius, int MinimumVolumeRadius, int MasterVolume, int StereoWidth, int StereoRadius, int TeamVolume = 100, int UserVolume = 100, bool Stereo = true)
	{
		if(!std::isfinite(Delta.x) || !std::isfinite(Delta.y))
			return vec2(0.0f, 0.0f);

		const float Minimum = std::clamp(MinVolume, 0, 100) / 100.0f;
		float Volume = HasPosition ? 1.0f : std::clamp(UnpositionedVolume, 0, 200) / 100.0f;
		float Pan = 0.0f;
		if(HasPosition && Spatial)
		{
			const int EndTiles = std::clamp(MinimumVolumeRadius, 1, 200);
			const float Outer = EndTiles * 32.0f;
			const float Inner = std::clamp(FullVolumeRadius, 0, EndTiles - 1) * 32.0f;
			const float Distance = length(Delta);
			const float Fade = std::clamp((Distance - Inner) / (Outer - Inner), 0.0f, 1.0f);
			Volume = 1.0f + (Minimum - 1.0f) * Fade;
		}
		if(HasPosition && Stereo)
			Pan = std::clamp(Delta.x / (std::clamp(StereoRadius, 1, 200) * 32.0f) * (std::clamp(StereoWidth, 0, 200) / 100.0f), -1.0f, 1.0f);
		const float Angle = (Pan + 1.0f) * (pi / 4.0f);
		return vec2(std::cos(Angle), std::sin(Angle)) * (Volume * (std::clamp(MasterVolume, 0, 400) / 100.0f) * (std::clamp(TeamVolume, 0, 200) / 100.0f) * (std::clamp(UserVolume, 0, 200) / 100.0f));
	}

	inline vec2 OverlayDirection(bool HasPosition, vec2 Delta)
	{
		const float Distance = length(Delta);
		if(!HasPosition || !std::isfinite(Distance) || Distance < 0.01f)
			return vec2(0.0f, 0.0f);
		return Delta / Distance;
	}

	inline bool ShowOverlayParticipant(bool Speaking, bool Muted, bool SpeakingOnly, bool ShowMuted)
	{
		return (!SpeakingOnly || Speaking) && (!Muted || ShowMuted);
	}

	inline float LimiterTarget(float Peak)
	{
		if(!std::isfinite(Peak) || Peak <= 0.9f)
			return 1.0f;
		return 0.9f / Peak;
	}

	struct SParticipantMix
	{
		bool m_SessionActive = true;
		bool m_Deafened = false;
		bool m_TrackMuted = false;
		bool m_NameMuted = false;
		bool m_IsLocal = false;
		bool m_HasPosition = false;
		bool m_IsSpectator = false;
		int m_MyTeam = -1;
		int m_TheirTeam = -1;
		int m_SuperTeam = TEAM_SUPER;
		int m_OtherTeamVolume = 100;
		int m_SuperVolume = 100;
		int m_UnpositionedVolume = 50;
		int m_SpectatorVolume = 100;
		int m_MasterVolume = 100;
		int m_UserVolume = 100;
	};

	inline int ParticipantVolume(const SParticipantMix &Mix)
	{
		return Mix.m_HasPosition ? 100 : std::clamp(Mix.m_IsSpectator ? Mix.m_SpectatorVolume : Mix.m_UnpositionedVolume, 0, 200);
	}

	inline int TeamVolume(const SParticipantMix &Mix)
	{
		if(Mix.m_IsSpectator)
			return 100;
		if(Mix.m_TheirTeam == Mix.m_SuperTeam)
			return std::clamp(Mix.m_SuperVolume, 0, 200);
		// Team 0 is a collection of individuals, not a shared voice team.
		// Unknown teams use the other-team setting until metadata is available.
		if(Mix.m_MyTeam > TEAM_FLOCK && Mix.m_MyTeam < Mix.m_SuperTeam && Mix.m_MyTeam == Mix.m_TheirTeam)
			return 100;
		return std::clamp(Mix.m_OtherTeamVolume, 0, 200);
	}

	// Shown next to a muted participant in the HUD.
	enum EMuteReason
	{
		MUTE_NONE = 0,
		MUTE_SESSION,
		MUTE_TRACK,
		MUTE_NAME,
		MUTE_LOCAL,
		MUTE_VOLUME,
		MUTE_TEAM,
		MUTE_NO_TRACK,
		MUTE_DEAFENED,
	};

	inline EMuteReason MuteReasonOf(const SParticipantMix &Mix)
	{
		if(!Mix.m_SessionActive)
			return MUTE_SESSION;
		if(Mix.m_Deafened)
			return MUTE_DEAFENED;
		if(Mix.m_TrackMuted)
			return MUTE_TRACK;
		if(Mix.m_NameMuted)
			return MUTE_NAME;
		if(Mix.m_IsLocal)
			return MUTE_LOCAL;

		if(TeamVolume(Mix) == 0)
			return MUTE_TEAM;
		if(Mix.m_MasterVolume <= 0 || Mix.m_UserVolume <= 0 || ParticipantVolume(Mix) == 0)
			return MUTE_VOLUME;

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
		case MUTE_DEAFENED: return "deafened";
		case MUTE_TRACK: return "muted";
		case MUTE_NAME: return "muted by you";
		case MUTE_LOCAL: return "you";
		case MUTE_VOLUME: return "volume 0";
		case MUTE_TEAM: return "team volume 0";
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
		const auto Padding = Input.find('=');
		if(Input.size() % 4 == 1 ||
			(Padding != std::string::npos && (Input.size() % 4 != 0 || Input.size() - Padding > 2 ||
								 Input.find_first_not_of('=', Padding) != std::string::npos)))
			return {};
		std::string Encoded;
		Encoded.reserve(Input.size() + 4);
		for(char c : Input)
		{
			if(c == '-')
				Encoded.push_back('+');
			else if(c == '_')
				Encoded.push_back('/');
			else if(c == '=')
				Encoded.push_back(c);
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
			if((Encoded[i + 2] == '=' && (Encoded[i + 3] != '=' || (b & 15) != 0)) ||
				(Encoded[i + 2] != '=' && Encoded[i + 3] == '=' && (c < 0 || (c & 3) != 0)))
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
		if(SecondDot == std::string::npos || FirstDot == 0 || SecondDot == FirstDot + 1 ||
			SecondDot + 1 >= Token.size() || Token.find('.', SecondDot + 1) != std::string::npos)
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
				{
					const char *pEnd = Payload.data() + Payload.size();
					const char *pTail = Result.ptr;
					while(pTail < pEnd && (*pTail == ' ' || *pTail == '\t' || *pTail == '\r' || *pTail == '\n'))
						++pTail;
					if(pTail < pEnd && (*pTail == ',' || *pTail == '}'))
						return Expiry;
				}
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
		return NowUnixSeconds < 0 || NowUnixSeconds >= Expiry || std::max(0LL, MarginSeconds) >= Expiry - NowUnixSeconds;
	}

	inline int DeviceDelayMs(unsigned PeriodSizeInFrames, unsigned Periods, unsigned SampleRate)
	{
		const unsigned Rate = std::max(1u, SampleRate);
		return int(std::min(1000.0 * PeriodSizeInFrames * Periods / Rate, double(std::numeric_limits<int>::max())));
	}
}

#endif // GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_VOICE_CHAT_LOGIC_H
