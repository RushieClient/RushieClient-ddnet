#include "voice_chat.h"

#include "base/net.h"
#include "base/str.h"
#include "engine/client.h"
#include "engine/textrender.h"
#include "game/client/gameclient.h"
#include "game/client/ui.h"
#include "game/client/ui_rect.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <charconv>
#include <exception>
#include <stdexcept>
#include <utility>

CRCVoiceChat::CRCVoiceChat()
#ifdef CONF_LIVEKIT
	: m_Delegate(this)
#endif
{
}

CRCVoiceChat::~CRCVoiceChat()
{
	OnShutdown();
}

void CRCVoiceChat::OnInit()
{
#ifdef CONF_LIVEKIT
	if(m_Initialized)
		return;
	try
	{
		livekit::initialize(livekit::LogLevel::Info);
		m_Initialized = true;
	}
	catch(const std::exception &Ex)
	{
		dbg_msg("voicechat", "initialization failed: %s", Ex.what());
	}
#endif
}

void CRCVoiceChat::OnReset()
{
#ifdef CONF_LIVEKIT
	ResetConnection();
	m_ManualDisconnect = false;
	m_NextAutoConnect = 0.0f;
#endif
}

void CRCVoiceChat::OnRelease()
{
#ifdef CONF_LIVEKIT
	m_PttHeld = false;
	ApplyMicState();
#endif
}

void CRCVoiceChat::OnRender()
{
#ifdef CONF_LIVEKIT
	ProcessEvents();
	if(m_PttHeld && (Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Menus.IsActive() ||
				GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive()))
		OnRelease();
	ApplyMicState();
	if(!m_Connected && !m_Connecting && m_Thread.joinable())
		ResetConnection();
	UpdateTalking();

	const bool OnServer = Client()->ServerAddress().type != NETTYPE_INVALID;
	if(OnServer && Client()->State() == IClient::STATE_ONLINE && g_Config.m_RcVoiceAutoConnect &&
		!m_Connected && !m_Connecting && !m_ManualDisconnect && Client()->GlobalTime() >= m_NextAutoConnect)
	{
		m_NextAutoConnect = Client()->GlobalTime() + 10.0f;
		dbg_msg("voicechat", "auto connect (room '%s')", RoomNameFromAddress().c_str());
		Connect();
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
	{
		if(m_Reconnecting)
			pStatus = "Voice: reconnecting...";
		else if(m_Connecting)
			pStatus = "Voice: preparing mic...";
		else if(!m_MicReady)
			pStatus = "Voice: mic unavailable";
		else
			pStatus = Muted() ? "Voice: muted" : (Transmitting() ? "Voice: talking" : "Voice: ready (PTT)");
	}
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
	ResetConnection();
	if(m_Initialized)
	{
		livekit::shutdown();
		m_Initialized = false;
	}
#endif
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
	if(!m_Initialized || m_Connecting || m_Connected)
		return;
	if(g_Config.m_RcVoiceUrl[0] == '\0' || g_Config.m_RcVoiceToken[0] == '\0')
	{
		dbg_msg("voicechat", "set rc_voice_url and rc_voice_token first");
		return;
	}
	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(Client()->State() != IClient::STATE_ONLINE || Client()->ServerAddress().type == NETTYPE_INVALID ||
		LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
	{
		dbg_msg("voicechat", "not in game yet");
		return;
	}

	ResetConnection();
	m_ManualDisconnect = false;
	m_Cancelled = false;
	m_Connecting = true;
	try
	{
		// Configuration is copied on the game thread, including device IDs.
		m_Thread = std::thread(&CRCVoiceChat::ConnectThread, this,
			std::string(g_Config.m_RcVoiceUrl), std::string(g_Config.m_RcVoiceToken), LocalClientId,
			std::string(g_Config.m_RcVoiceMicDevice), std::string(g_Config.m_RcVoicePlayoutDevice));
	}
	catch(const std::exception &Ex)
	{
		m_Connecting = false;
		dbg_msg("voicechat", "could not start connection: %s", Ex.what());
	}
#else
	dbg_msg("voicechat", "voice chat is not available in this build");
#endif
}

void CRCVoiceChat::Disconnect()
{
#ifdef CONF_LIVEKIT
	m_ManualDisconnect = true;
	ResetConnection();
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

void CRCVoiceChat::ConPtt(IConsole::IResult *pResult, void *pUserData)
{
#ifdef CONF_LIVEKIT
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->m_PttHeld = pResult->GetInteger(0) != 0 && pSelf->Client()->State() == IClient::STATE_ONLINE &&
			   !pSelf->GameClient()->m_Menus.IsActive() && !pSelf->GameClient()->m_Chat.IsActive() && !pSelf->GameClient()->m_GameConsole.IsActive();
	pSelf->ApplyMicState();
#endif
}

void CRCVoiceChat::ConMute(IConsole::IResult *pResult, void *pUserData)
{
#ifdef CONF_LIVEKIT
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->m_FullMuted = true;
	pSelf->ApplyMicState();
	dbg_msg("voicechat", "mic fully muted (PTT disabled)");
#endif
}

void CRCVoiceChat::ConUnmute(IConsole::IResult *pResult, void *pUserData)
{
#ifdef CONF_LIVEKIT
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->m_FullMuted = false;
	pSelf->ApplyMicState();
	dbg_msg("voicechat", "mic enabled (transmit via PTT)");
#endif
}

void CRCVoiceChat::ConStatus(IConsole::IResult *pResult, void *pUserData)
{
#ifdef CONF_LIVEKIT
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
#endif
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
	return m_Connected && !m_Reconnecting && m_MicReady && m_Transmitting && m_PttHeld && !m_FullMuted;
#else
	return false;
#endif
}

#ifdef CONF_LIVEKIT

void CRCVoiceChat::ConnectThread(std::string Url, std::string Token, int LocalClientId,
	std::string MicDevice, std::string PlayoutDevice)
{
	try
	{
		m_pPlatformAudio = std::make_unique<livekit::PlatformAudio>();
		if(!MicDevice.empty())
			m_pPlatformAudio->setRecordingDevice(MicDevice);
		if(!PlayoutDevice.empty())
			m_pPlatformAudio->setPlayoutDevice(PlayoutDevice);
		m_pRoom = std::make_unique<livekit::Room>();
		m_pRoom->setDelegate(&m_Delegate);
		livekit::RoomOptions Options;
		Options.connect_timeout = std::chrono::seconds(10);
		Options.join_retries = 0;
		if(!m_pRoom->connect(Url, Token, Options))
			throw std::runtime_error("room connection failed");
		if(m_Cancelled)
		{
			m_Connecting = false;
			return;
		}
		m_Connected = true;
		dbg_msg("voicechat", "joined room '%s'", m_pRoom->roomInfo().name.c_str());

		{
			// Hold the queue lock while collecting identities: a leave callback
			// must be queued after this snapshot if the peer is still in it.
			std::lock_guard<std::mutex> Lock(m_EventLock);
			SEvent Ev{EEventType::Snapshot};
			for(const auto &Weak : m_pRoom->remoteParticipants())
				if(auto pParticipant = Weak.lock())
					Ev.m_vIdentities.push_back(pParticipant->identity());
			m_vEvents.push_back(std::move(Ev));
		}

		try
		{
			auto pLocal = m_pRoom->localParticipant().lock();
			if(!pLocal)
				throw std::runtime_error("local participant is unavailable");
			try
			{
				pLocal->setMetadata(std::to_string(LocalClientId));
			}
			catch(const std::exception &)
			{
				dbg_msg("voicechat", "could not update client id metadata (check canUpdateOwnMetadata)");
			}
			if(!m_Cancelled && m_Connected)
			{
				m_pMicSource = m_pPlatformAudio->createAudioSource(livekit::PlatformAudioOptions{});
				auto pTrack = livekit::LocalAudioTrack::createLocalAudioTrack("mic", m_pMicSource);
				pTrack->mute(); // Never publish an open microphone, even when PTT is held.
				livekit::TrackPublishOptions PublishOptions;
				PublishOptions.source = livekit::TrackSource::SOURCE_MICROPHONE;
				pLocal->publishTrack(pTrack, PublishOptions);
				{
					std::lock_guard<std::mutex> Lock(m_MicLock);
					m_pMicTrack = std::move(pTrack);
					m_MicReady = true;
				}
				ApplyMicState();
				dbg_msg("voicechat", "mic published");
			}
		}
		catch(const std::exception &Ex)
		{
			// Keep the room available for listening, but do not report transmission.
			dbg_msg("voicechat", "mic setup failed: %s", Ex.what());
		}
	}
	catch(const std::exception &Ex)
	{
		m_Connected = false;
		dbg_msg("voicechat", "connect failed: %s", Ex.what());
	}
	catch(...)
	{
		m_Connected = false;
		dbg_msg("voicechat", "connect failed with an unknown error");
	}
	if(m_Cancelled)
		m_Connected = false;
	// Leave this true through microphone setup: Connect must not replace a live thread.
	m_Connecting = false;
}

void CRCVoiceChat::ApplyMicState()
{
	std::lock_guard<std::mutex> Lock(m_MicLock);
	if(!m_pMicTrack || !m_MicReady)
		return;
	const bool Transmit = m_Connected && !m_Cancelled && !m_Reconnecting && m_PttHeld && !m_FullMuted;
	if(Transmit == m_Transmitting)
		return;
	try
	{
		if(Transmit)
			m_pMicTrack->unmute();
		else
			m_pMicTrack->mute();
		m_Transmitting = Transmit;
	}
	catch(const std::exception &Ex)
	{
		m_Cancelled = true;
		m_Connected = false;
		m_Transmitting = false;
		dbg_msg("voicechat", "mic state failed; closing voice connection: %s", Ex.what());
	}
}

void CRCVoiceChat::ResetConnection()
{
	m_Cancelled = true;
	m_PttHeld = false;
	m_Connected = false;
	if(m_Thread.joinable())
		m_Thread.join();
	{
		std::lock_guard<std::mutex> Lock(m_MicLock);
		if(m_pMicTrack)
		{
			try
			{
				m_pMicTrack->mute();
			}
			catch(const std::exception &)
			{
			}
		}
		m_pMicTrack.reset();
		m_MicReady = false;
		m_Transmitting = false;
	}
	m_pMicSource.reset();
	m_pPlatformAudio.reset();
	if(m_pRoom)
		m_pRoom->setDelegate(nullptr);
	m_pRoom.reset(); // Waits for SDK callbacks; clear their queue only afterwards.
	{
		std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
		m_mAudioActivity.clear();
	}
	m_Delegate.Reset();
	m_Connected = false;
	m_Connecting = false;
	m_Reconnecting = false;
	m_vParticipants.clear();
	m_mClientIds.clear();
	std::lock_guard<std::mutex> Lock(m_EventLock);
	m_vEvents.clear();
}

int CRCVoiceChat::ParseClientId(const std::string &Metadata)
{
	int Id = -1;
	const char *pEnd = Metadata.data() + Metadata.size();
	const auto Result = std::from_chars(Metadata.data(), pEnd, Id);
	return Result.ec == std::errc{} && Result.ptr == pEnd && Id >= 0 && Id < MAX_CLIENTS ? Id : -1;
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
	for(const SEvent &Ev : vEvents)
	{
		switch(Ev.m_Type)
		{
		case EEventType::ParticipantJoined:
		{
			auto It = std::find_if(m_vParticipants.begin(), m_vParticipants.end(),
				[&](const SParticipant &P) { return P.m_Identity == Ev.m_Identity; });
			if(It == m_vParticipants.end())
				m_vParticipants.push_back({Ev.m_Identity, Ev.m_Name, false});
			else
				It->m_Name = Ev.m_Name;
			m_mClientIds[Ev.m_Identity] = ParseClientId(Ev.m_Metadata);
			break;
		}
		case EEventType::ParticipantLeft:
			m_vParticipants.erase(std::remove_if(m_vParticipants.begin(), m_vParticipants.end(),
						      [&](const SParticipant &P) { return P.m_Identity == Ev.m_Identity; }),
				m_vParticipants.end());
			m_mClientIds.erase(Ev.m_Identity);
			break;
		case EEventType::Snapshot:
			// The worker only supplies immutable identities. Rich participant
			// data from SDK callbacks must not be overwritten by this fallback.
			for(const auto &Identity : Ev.m_vIdentities)
				if(std::none_of(m_vParticipants.begin(), m_vParticipants.end(),
					   [&](const SParticipant &P) { return P.m_Identity == Identity; }))
					m_vParticipants.push_back({Identity, {}, false});
			break;
		case EEventType::Disconnected:
			m_vParticipants.clear();
			m_mClientIds.clear();
			break;
		}
	}
}

void CRCVoiceChat::RecordAudioActivity(const std::string &TrackId, const livekit::AudioFrame &Frame)
{
	// Any nonzero decoded sample counts, including very quiet sound.
	if(std::none_of(Frame.data().begin(), Frame.data().end(), [](std::int16_t Sample) { return Sample != 0; }))
		return;
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	auto It = m_mAudioActivity.find(TrackId);
	if(It != m_mAudioActivity.end() && !It->second.m_Muted)
		It->second.m_SoundUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
}

void CRCVoiceChat::SetAudioMuted(const std::string &TrackId, bool Muted)
{
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	auto It = m_mAudioActivity.find(TrackId);
	if(It != m_mAudioActivity.end())
	{
		It->second.m_Muted = Muted;
		It->second.m_SoundUntil = {};
	}
}

void CRCVoiceChat::ClearAudioActivity(const std::string &Identity)
{
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	std::erase_if(m_mAudioActivity, [&](const auto &Entry) { return Entry.second.m_Identity == Identity; });
}

void CRCVoiceChat::UpdateTalking()
{
	const auto Now = std::chrono::steady_clock::now();
	const bool Receiving = m_Connected && !m_Reconnecting && !m_Cancelled;
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	for(auto &P : m_vParticipants)
		P.m_Talking = Receiving && std::any_of(m_mAudioActivity.begin(), m_mAudioActivity.end(), [&](const auto &Entry) {
			const auto &Activity = Entry.second;
			return Activity.m_Identity == P.m_Identity && !Activity.m_Muted && Now < Activity.m_SoundUntil;
		});
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

#endif

std::vector<CRCVoiceChat::SDevice> CRCVoiceChat::MicDevices()
{
	std::vector<SDevice> vDevices;
#ifdef CONF_LIVEKIT
	if(!m_Initialized)
		return vDevices;
	try
	{
		// Device enumeration must not replace the connection thread's manager.
		livekit::PlatformAudio Audio;
		for(const auto &Dev : Audio.recordingDevices())
			vDevices.push_back({Dev.name, Dev.id});
	}
	catch(const std::exception &Ex)
	{
		dbg_msg("voicechat", "mic devices: %s", Ex.what());
	}
#endif
	return vDevices;
}

std::vector<CRCVoiceChat::SDevice> CRCVoiceChat::PlayoutDevices()
{
	std::vector<SDevice> vDevices;
#ifdef CONF_LIVEKIT
	if(!m_Initialized)
		return vDevices;
	try
	{
		livekit::PlatformAudio Audio;
		for(const auto &Dev : Audio.playoutDevices())
			vDevices.push_back({Dev.name, Dev.id});
	}
	catch(const std::exception &Ex)
	{
		dbg_msg("voicechat", "playout devices: %s", Ex.what());
	}
#endif
	return vDevices;
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
