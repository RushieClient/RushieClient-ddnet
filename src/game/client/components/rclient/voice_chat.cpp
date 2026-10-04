#include "voice_chat.h"

#include "base/net.h"
#include "base/str.h"
#include "engine/client.h"
#include "engine/textrender.h"
#include "game/client/gameclient.h"
#include "game/client/ui.h"
#include "game/client/ui_rect.h"

#include <engine/shared/config.h>

CRCVoiceChat::CRCVoiceChat()
#ifdef CONF_LIVEKIT
	: m_Delegate(this)
{
#else
{
	CRCVoiceChat::OnReset();
#endif
}

CRCVoiceChat::~CRCVoiceChat()
{
#ifdef CONF_LIVEKIT
	if(m_Thread.joinable())
		m_Thread.join();
	m_pRoom.reset();
#endif
}

void CRCVoiceChat::OnInit()
{
#ifdef CONF_LIVEKIT
	livekit::initialize(livekit::LogLevel::Debug);
	dbg_msg("voicechat", "livekit initialized");
#endif
}

void CRCVoiceChat::OnReset()
{
#ifdef CONF_LIVEKIT
	if(m_Thread.joinable())
		m_Thread.join();
	m_pMicTrack.reset();
	m_pMicSource.reset();
	m_pRoom.reset();
	m_Connected = false;
	m_vParticipants.clear();
	{
		std::lock_guard<std::mutex> Lock(m_EventLock);
		m_vEvents.clear();
	}
	m_ManualDisconnect = false;
	{
		std::lock_guard<std::mutex> Lock(m_LevelLock);
		m_mLevels.clear();
	}
#endif
}

void CRCVoiceChat::OnRender()
{
#ifdef CONF_LIVEKIT
	ProcessEvents();

	const bool OnServer = Client()->ServerAddress().type != NETTYPE_INVALID;
	if(OnServer && Client()->State() == IClient::STATE_ONLINE && g_Config.m_RcVoiceAutoConnect &&
		!m_Connected && !m_Connecting && !m_ManualDisconnect && Client()->GlobalTime() >= m_NextAutoConnect)
	{
		m_NextAutoConnect = Client()->GlobalTime() + 10.0f;
		dbg_msg("voicechat", "auto connect (room '%s')", RoomNameFromAddress().c_str());
		Connect();
	}

	const float Now = Client()->GlobalTime();
	const float Dt = Now - m_LastLevelTick;
	m_LastLevelTick = Now;
	{
		std::lock_guard<std::mutex> Lock(m_LevelLock);
		for(auto &Entry : m_mLevels)
			Entry.second *= std::exp(-8.0f * Dt);

		for(auto &P : m_vParticipants)
		{
			auto It = m_mLevels.find(P.m_Identity);
			P.m_Talking = It != m_mLevels.end() && It->second > 0.0f;
		}
	}

	if(m_Connected && Client()->GlobalTime() >= m_NextClientIdRefresh)
	{
		m_NextClientIdRefresh = Client()->GlobalTime() + 2.0f;
		for(auto &P : m_vParticipants)
		{
			int Id = -1;
			if(auto pRemote = m_pRoom->remoteParticipant(P.m_Identity).lock())
			{
				const char *pMeta = pRemote->metadata().c_str();
				char *pEnd = nullptr;
				const long Parsed = strtol(pMeta, &pEnd, 10);
				if(pEnd != pMeta && *pEnd == '\0' && Parsed >= 0 && Parsed < MAX_CLIENTS)
					Id = (int)Parsed;
			}
			m_mClientIds[P.m_Identity] = Id;
		}
	}

	if(!g_Config.m_RcVoiceHud)
		return;

	Ui()->MapScreen();
	const CUIRect Screen = *Ui()->Screen();
	const std::vector<SParticipant> vParticipants = Participants();

	CUIRect Panel{Screen.w - 280.0f, 20.0f, 260.0f, 30.0f + 16.0f * vParticipants.size()};
	Panel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_ALL, 10.0f);
	Panel.VMargin(10.0f, &Panel);

	CUIRect Row;
	Panel.HSplitTop(16.0f, &Row, &Panel);
	const char *pStatus = "Voice: off";
	if(Connected())
		pStatus = Muted() ? "Voice: muted" : (Transmitting() ? "Voice: talking" : "Voice: ready (PTT)");
	else if(Connecting())
		pStatus = "Voice: connecting...";
	Ui()->DoLabel(&Row, pStatus, 10.0f, TEXTALIGN_ML);

	for(const auto &P : vParticipants)
	{
		Panel.HSplitTop(16.0f, &Row, &Panel);
		if(P.m_Talking)
			TextRender()->TextColor(0.4f, 1.0f, 0.4f, 1.0f);

		const int ClientId = ClientIdOf(P.m_Identity);
		char aLabel[96];
		if(ClientId >= 0)
			str_format(aLabel, sizeof(aLabel), "%s [%d]", (P.m_Name.empty() ? P.m_Identity : P.m_Name).c_str(), ClientId);
		else
			str_format(aLabel, sizeof(aLabel), "%s", (P.m_Name.empty() ? P.m_Identity : P.m_Name).c_str());
		Ui()->DoLabel(&Row, aLabel, 10.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
#endif
}

void CRCVoiceChat::OnConsoleInit()
{
	Console()->Register("rc_voice_connect", "", CFGFLAG_CLIENT, ConConnect, this, "Connect to voice chat");
	Console()->Register("rc_voice_disconnect", "", CFGFLAG_CLIENT, ConDisconnect, this, "Disconnect from voice chat");
	Console()->Register("+rc_voice_ptt", "", CFGFLAG_CLIENT, ConPtt, this, "Push to talk (hold)");
	Console()->Register("rc_voice_mute", "", CFGFLAG_CLIENT, ConMute, this, "Fully mute mic (PTT disabled too)");
	Console()->Register("rc_voice_unmute", "", CFGFLAG_CLIENT, ConUnmute, this, "Enable mic (transmit via PTT)");
	Console()->Register("rc_voice_status", "", CFGFLAG_CLIENT, ConStatus, this, "Voice chat status");
}

void CRCVoiceChat::OnShutdown()
{
#ifdef CONF_LIVEKIT

#endif
	OnReset();
}

bool CRCVoiceChat::Connected() const
{
#ifdef CONF_LIVEKIT
	return m_Connected;
#else
	return false;
#endif
}

bool CRCVoiceChat::Connecting() const
{
#ifdef CONF_LIVEKIT
	return m_Connecting;
#else
	return false;
#endif
}

std::vector<CRCVoiceChat::SParticipant> CRCVoiceChat::Participants() const
{
#ifdef CONF_LIVEKIT
	return m_vParticipants;
#else
	return {};
#endif
}

void CRCVoiceChat::Connect()
{
#ifdef CONF_LIVEKIT
	if(m_Connecting || m_Connected)
	{
		dbg_msg("voicechat", "already connecting/connected");
		return;
	}
	if(g_Config.m_RcVoiceUrl[0] == '\0' || g_Config.m_RcVoiceToken[0] == '\0')
	{
		dbg_msg("voicechat", "set rc_voice_url and rc_voice_token first");
		return;
	}

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;

	if(LocalClientId < 0) { dbg_msg("voicechat", "not in game yet"); return; }

	m_Connecting = true;
	if(m_Thread.joinable())
		m_Thread.join();
	m_Thread = std::thread(&CRCVoiceChat::ConnectThread, this,
		std::string(g_Config.m_RcVoiceUrl), std::string(g_Config.m_RcVoiceToken), LocalClientId);
#else
	dbg_msg("voicechat", "voice chat is not available in this build");
#endif
}

void CRCVoiceChat::Disconnect()
{
#ifdef CONF_LIVEKIT
	if(m_Connecting)
	{
		dbg_msg("voicechat", "still connecting, wait");
		return;
	}
	if(!m_Connected)
	{
		dbg_msg("voicechat", "not connected");
		return;
	}
	m_ManualDisconnect = true;
	m_pRoom->disconnect();
#else
	dbg_msg("voicechat", "voice chat is not available in this build");
#endif
}

void CRCVoiceChat::ConConnect(IConsole::IResult *pResult, void *pUserData)
{
	((CRCVoiceChat *)pUserData)->Connect();
}

void CRCVoiceChat::ConDisconnect(IConsole::IResult *pResult, void *pUserData)
{
	((CRCVoiceChat *)pUserData)->Disconnect();
}

bool CRCVoiceChat::Muted() const
{
#ifdef CONF_LIVEKIT
	return m_FullMuted;
#else
	return false;
#endif
}

bool CRCVoiceChat::Transmitting() const
{
#ifdef CONF_LIVEKIT
	return m_Connected && m_PttHeld && !m_FullMuted;
#else
	return false;
#endif
}

#ifdef CONF_LIVEKIT

void CRCVoiceChat::ConnectThread(std::string Url, std::string Token, int LocalClientId)
{
	m_pMicTrack.reset();
	m_pMicSource.reset();
	m_pPlatformAudio.reset();

	m_pRoom = std::make_unique<livekit::Room>();
	m_pRoom->setDelegate(&m_Delegate);

	livekit::RoomOptions Options;
	Options.connect_timeout = std::chrono::milliseconds(10000);

	if(!m_pRoom->connect(Url, Token, Options))
	{
		m_Connecting = false;
		dbg_msg("voicechat", "connect failed");
		return;
	}

	m_Connected = true;
	m_Connecting = false;
	dbg_msg("voicechat", "joined room '%s'", m_pRoom->roomInfo().name.c_str());

	{
		SEvent Ev;
		Ev.m_Type = EEventType::Snapshot;
		for(const auto &pWeak : m_pRoom->remoteParticipants())
		{
			if(auto pParticipant = pWeak.lock())
			{
				Ev.m_vIdentities.push_back(pParticipant->identity());
				Ev.m_vNames.push_back(pParticipant->name());
			}
		}
		PushEvent(std::move(Ev));
	}

	try
	{
		m_pPlatformAudio = std::make_unique<livekit::PlatformAudio>();

		if(g_Config.m_RcVoiceMicDevice[0] != '\0')
			m_pPlatformAudio->setRecordingDevice(g_Config.m_RcVoiceMicDevice);
		if(g_Config.m_RcVoicePlayoutDevice[0] != '\0')
			m_pPlatformAudio->setPlayoutDevice(g_Config.m_RcVoicePlayoutDevice);

		livekit::PlatformAudioOptions MicOptions;
		m_pMicSource = m_pPlatformAudio->createAudioSource(MicOptions);
		m_pMicTrack = livekit::LocalAudioTrack::createLocalAudioTrack("mic", m_pMicSource);

		if(auto pLocal = m_pRoom->localParticipant().lock())
		{
			pLocal->publishTrack(m_pMicTrack, livekit::TrackPublishOptions{});
			if(LocalClientId >= 0)
				pLocal->setMetadata(std::to_string(LocalClientId));
		}

		ApplyMicState();
	}
	catch(const std::exception &Ex)
	{
		dbg_msg("voicechat", "mic setup failed: %s", Ex.what());
		return;
	}

	dbg_msg("voicechat", "mic published");
}

void CRCVoiceChat::ConPtt(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->m_PttHeld = pResult->GetInteger(0) != 0;
	pSelf->ApplyMicState();
}

void CRCVoiceChat::ConMute(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->m_FullMuted = true;
	pSelf->ApplyMicState();
	dbg_msg("voicechat", "mic fully muted (PTT disabled)");
}

void CRCVoiceChat::ConUnmute(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->m_FullMuted = false;
	pSelf->ApplyMicState();
	dbg_msg("voicechat", "mic enabled (transmit via PTT)");
}

void CRCVoiceChat::ConStatus(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	if(!pSelf->m_Connected)
	{
		dbg_msg("voicechat", "not connected");
		return;
	}
	dbg_msg("voicechat", "room='%s' full_mute=%d ptt_held=%d participants=%d",
		pSelf->m_pRoom->roomInfo().name.c_str(),
		(int)pSelf->m_FullMuted, (int)pSelf->m_PttHeld,
		(int)pSelf->m_vParticipants.size());
	for(const auto &P : pSelf->m_vParticipants)
		dbg_msg("voicechat", "  %s (%s)%s", P.m_Name.c_str(), P.m_Identity.c_str(),
			P.m_Talking ? " [talking]" : "");
	dbg_msg("voicechat", "server room='%s'", pSelf->RoomNameFromAddress().c_str());
}

void CRCVoiceChat::ApplyMicState()
{
	if(!m_pMicTrack || !m_Connected)
		return;
	if(m_PttHeld && !m_FullMuted)
		m_pMicTrack->unmute();
	else
		m_pMicTrack->mute();
}


void CRCVoiceChat::PushEvent(SEvent Event)
{
	std::lock_guard<std::mutex> Lock(m_EventLock);
	m_vEvents.push_back(std::move(Event));
}

void CRCVoiceChat::ProcessEvents()
{
	std::vector<SEvent> vEvents;
	{
		std::lock_guard<std::mutex> Lock(m_EventLock);
		vEvents.swap(m_vEvents);
	}

	for(SEvent &Ev : vEvents)
	{
		switch(Ev.m_Type)
		{
		case EEventType::ParticipantJoined:
			if(std::find_if(m_vParticipants.begin(), m_vParticipants.end(),
				   [&](const SParticipant &P) { return P.m_Identity == Ev.m_Identity; }) == m_vParticipants.end())
			{
				m_vParticipants.push_back({Ev.m_Identity, Ev.m_Name, false});
				dbg_msg("voicechat", "join: '%s' (%s)", Ev.m_Name.c_str(), Ev.m_Identity.c_str());
			}
			break;

		case EEventType::ParticipantLeft:
		{
			auto It = std::find_if(m_vParticipants.begin(), m_vParticipants.end(),
				[&](const SParticipant &P) { return P.m_Identity == Ev.m_Identity; });
			if(It != m_vParticipants.end())
			{
				dbg_msg("voicechat", "left: '%s' (%s)", It->m_Name.c_str(), It->m_Identity.c_str());
				m_vParticipants.erase(It);
			}
			std::lock_guard<std::mutex> Lock(m_LevelLock);
			m_mLevels.erase(Ev.m_Identity);
			m_mClientIds.erase(Ev.m_Identity);
			break;
		}

		case EEventType::ActiveSpeakers:
			for(auto &P : m_vParticipants)
				P.m_Talking = std::find(Ev.m_vIdentities.begin(), Ev.m_vIdentities.end(), P.m_Identity) != Ev.m_vIdentities.end();
			break;

		case EEventType::Snapshot:
			m_vParticipants.clear();
			for(size_t i = 0; i < Ev.m_vIdentities.size(); i++)
				m_vParticipants.push_back({Ev.m_vIdentities[i],
					i < Ev.m_vNames.size() ? Ev.m_vNames[i] : "", false});
			break;
		}
	}
}

std::string CRCVoiceChat::RoomNameFromAddress() const
{
	const NETADDR &Addr = Client()->ServerAddress();
	if(Addr.type == NETTYPE_INVALID)
		return {};

	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aAddr, sizeof(aAddr), true); // "1.2.3.4:8303"

	std::string Sanitized;
	Sanitized.reserve(str_length(aAddr));
	for(const char *p = aAddr; *p; p++)
	{
		const char c = *p;
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '.' || c == '-' || c == '_' || c == ':' || c == '@')
			Sanitized.push_back(c);
		else
			Sanitized.push_back('_');
	}

	if(g_Config.m_RcVoiceRoomMode == 1 && g_Config.m_RcVoiceRoomName[0] != '\0')
		return std::string(g_Config.m_RcVoiceRoomName) + "@" + Sanitized;
	return Sanitized;
}

void CRCVoiceChat::UpdateSpeakingLevel(const std::string &Identity, const livekit::AudioFrame &Frame)
{
	double Sum = 0.0;
	const auto &Data = Frame.data();
	for(std::int16_t Sample : Data)
		Sum += double(Sample) * Sample;
	const float Level = Data.empty() ? 0.0f : (float)(std::sqrt(Sum / Data.size()) / 32768.0);

	std::lock_guard<std::mutex> Lock(m_LevelLock);
	float &Smoothed = m_mLevels[Identity];
	if(Level > Smoothed)
		Smoothed = Level;
}

#endif

std::vector<CRCVoiceChat::SDevice> CRCVoiceChat::MicDevices()
{
#ifdef CONF_LIVEKIT
	if(!m_pPlatformAudio)
	{
		try
		{
			m_pPlatformAudio = std::make_unique<livekit::PlatformAudio>();
		}
		catch(const std::exception &Ex)
		{
			dbg_msg("voicechat", "platform audio: %s", Ex.what());
			return {};
		}
	}
	std::vector<SDevice> vDevices;
	try
	{
		for(const auto &Dev : m_pPlatformAudio->recordingDevices())
			vDevices.push_back({Dev.name, Dev.id});
	}
	catch(const std::exception &Ex)
	{
		dbg_msg("voicechat", "mic devices: %s", Ex.what());
	}
	return vDevices;
#else
	return {};
#endif
}

std::vector<CRCVoiceChat::SDevice> CRCVoiceChat::PlayoutDevices()
{
#ifdef CONF_LIVEKIT
	if(!m_pPlatformAudio)
	{
		try
		{
			m_pPlatformAudio = std::make_unique<livekit::PlatformAudio>();
		}
		catch(const std::exception &Ex)
		{
			dbg_msg("voicechat", "platform audio: %s", Ex.what());
			return {};
		}
	}
	std::vector<SDevice> vDevices;
	try
	{
		for(const auto &Dev : m_pPlatformAudio->playoutDevices())
			vDevices.push_back({Dev.name, Dev.id});
	}
	catch(const std::exception &Ex)
	{
		dbg_msg("voicechat", "playout devices: %s", Ex.what());
	}
	return vDevices;
#else
	return {};
#endif
}

int CRCVoiceChat::ClientIdOf(const std::string &Identity) const
{
#ifdef CONF_LIVEKIT
	auto It = m_mClientIds.find(Identity);
	return It != m_mClientIds.end() ? It->second : -1;
#else
	return -1;
#endif
}
