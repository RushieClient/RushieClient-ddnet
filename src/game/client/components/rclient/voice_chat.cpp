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
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

#ifdef CONF_LIVEKIT
#define MINIAUDIO_IMPLEMENTATION
#include "external/miniaudio.h"
#undef MINIAUDIO_IMPLEMENTATION

#include <base/hash.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#endif

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
		ma_context_config Config = ma_context_config_init();
		ma_backend Backends[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm,
			ma_backend_coreaudio, ma_backend_pulseaudio, ma_backend_alsa, ma_backend_jack,
			ma_backend_sndio, ma_backend_audio4, ma_backend_oss};
		CheckAudio(ma_context_init(Backends, std::size(Backends), &Config, &m_AudioContext), "initialize audio devices");
		m_AudioInitialized = true;
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
		else if(!m_MicReady || m_MicFailed)
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
	if(m_AudioInitialized)
		ma_context_uninit(&m_AudioContext);
	m_AudioInitialized = false;
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
	if(!m_Initialized || !m_AudioInitialized || m_Connecting || m_Connected)
		return;
	if(g_Config.m_RcVoiceTokenUrl[0] == '\0' && (g_Config.m_RcVoiceUrl[0] == '\0' || g_Config.m_RcVoiceToken[0] == '\0'))
	{
		dbg_msg("voicechat", "set rc_voice_token_url or manual rc_voice_url and rc_voice_token first");
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
	m_MicGain = std::clamp(g_Config.m_RcVoiceMicVolume, 0, 300) / 100.0f;
	m_Cancelled = false;
	m_Connecting = true;
	try
	{
		const std::string RoomName = RoomNameFromAddress();
		if(g_Config.m_RcVoiceTokenUrl[0] != '\0')
		{
			CJsonStringWriter Json;
			Json.BeginObject();
			Json.WriteAttribute("room_name");
			Json.WriteStrValue(RoomName.c_str());
			Json.WriteAttribute("participant_name");
			Json.WriteStrValue(GameClient()->m_aClients[LocalClientId].m_aName);
			Json.WriteAttribute("participant_metadata");
			Json.WriteStrValue(std::to_string(LocalClientId).c_str());
			Json.EndObject();
			m_pTokenRequest = HttpPostJson(g_Config.m_RcVoiceTokenUrl, Json.GetOutputString().c_str());
			m_pTokenRequest->Timeout(CTimeout{4000, 10000, 500, 5});
			m_pTokenRequest->MaxResponseSize(16 * 1024);
			m_pTokenRequest->LogProgress(HTTPLOG::NONE);
			m_pTokenRequest->FailOnErrorStatus(false);
			GameClient()->Http()->Run(m_pTokenRequest);
		}
		m_Thread = std::thread(&CRCVoiceChat::ConnectThread, this,
			std::string(g_Config.m_RcVoiceUrl), std::string(g_Config.m_RcVoiceToken), LocalClientId,
			std::string(g_Config.m_RcVoiceMicDevice), std::string(g_Config.m_RcVoicePlayoutDevice), m_pTokenRequest, RoomName);
	}
	catch(const std::exception &Ex)
	{
		if(m_pTokenRequest)
			m_pTokenRequest->Abort();
		m_pTokenRequest.reset();
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
	return m_Connected && !m_Reconnecting && m_MicReady && !m_MicFailed && m_Transmitting && m_PttHeld && !m_FullMuted;
#else
	return false;
#endif
}

#ifdef CONF_LIVEKIT

void CRCVoiceChat::ConnectThread(std::string Url, std::string Token, int LocalClientId,
	std::string MicDevice, std::string PlayoutDevice, std::shared_ptr<IHttpRequest> pTokenRequest, std::string RoomName)
{
	try
	{
		if(pTokenRequest)
		{
			pTokenRequest->Wait();
			if(m_Cancelled)
			{
				m_Connecting = false;
				return;
			}
			if(pTokenRequest->State() != EHttpState::DONE)
				throw std::runtime_error("token endpoint request failed (network, TLS, timeout or response too large)");
			if(pTokenRequest->StatusCode() < 200 || pTokenRequest->StatusCode() >= 300)
				throw std::runtime_error("token endpoint returned HTTP " + std::to_string(pTokenRequest->StatusCode()));
			std::unique_ptr<json_value, decltype(&json_value_free)> pJson(pTokenRequest->ResultJson(), json_value_free);
			if(!pJson || pJson->type != json_object)
				throw std::runtime_error("invalid token endpoint response: expected a JSON object");
			const json_value *pUrl = json_object_get(pJson.get(), "server_url");
			const json_value *pToken = json_object_get(pJson.get(), "participant_token");
			if(pUrl->type != json_string || pToken->type != json_string ||
				pUrl->u.string.length == 0 || pUrl->u.string.length > 255 ||
				pToken->u.string.length == 0 || pToken->u.string.length > 8192)
				throw std::runtime_error("invalid token endpoint response: expected server_url and participant_token strings");
			Url.assign(pUrl->u.string.ptr, pUrl->u.string.length);
			Token.assign(pToken->u.string.ptr, pToken->u.string.length);
			if((!str_startswith(Url.c_str(), "wss://") && !str_startswith(Url.c_str(), "ws://")) ||
				Url.find_first_of(std::string("\0\r\n", 3)) != std::string::npos || Token.find_first_of(std::string("\0\r\n", 3)) != std::string::npos)
				throw std::runtime_error("invalid token endpoint credentials");
		}
		if(m_Cancelled)
		{
			m_Connecting = false;
			return;
		}
		StartAudio(MicDevice, PlayoutDevice);
		m_pRoom = std::make_unique<livekit::Room>();
		m_pRoom->setDelegate(&m_Delegate);
		livekit::RoomOptions Options;
		Options.connect_timeout = std::chrono::seconds(10);
		Options.join_retries = 0;
		if(!m_pRoom->connect(Url, Token, Options))
			throw std::runtime_error("room connection failed");
		if(pTokenRequest && m_pRoom->roomInfo().name != RoomName)
			throw std::runtime_error("token endpoint returned a token for a different room");
		if(m_Cancelled)
		{
			m_Connecting = false;
			return;
		}
		m_Connected = true;
		dbg_msg("voicechat", "joined room '%s'", m_pRoom->roomInfo().name.c_str());

		{
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
				if(pLocal->metadata() != std::to_string(LocalClientId))
					pLocal->setMetadata(std::to_string(LocalClientId));
			}
			catch(const std::exception &)
			{
				dbg_msg("voicechat", "could not update client id metadata (check canUpdateOwnMetadata)");
			}
			if(!m_Cancelled && m_Connected && m_pMicSource)
			{
				auto pTrack = livekit::LocalAudioTrack::createLocalAudioTrack("mic", m_pMicSource);
				pTrack->mute();
				livekit::TrackPublishOptions PublishOptions;
				PublishOptions.source = livekit::TrackSource::SOURCE_MICROPHONE;
				pLocal->publishTrack(pTrack, PublishOptions);
				{
					std::lock_guard<std::mutex> Lock(m_MicLock);
					m_pMicTrack = std::move(pTrack);
					m_MicReady = !m_MicFailed;
				}
				ApplyMicState();
				dbg_msg("voicechat", "mic published");
			}
		}
		catch(const std::exception &Ex)
		{
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
	m_Connecting = false;
}

void CRCVoiceChat::ApplyMicState()
{
	std::lock_guard<std::mutex> Lock(m_MicLock);
	if(!m_pMicTrack)
		return;
	const bool Transmit = m_MicReady && !m_MicFailed && m_Connected && !m_Cancelled && !m_Reconnecting && m_PttHeld && !m_FullMuted;
	if(Transmit == m_Transmitting)
		return;
	try
	{
		if(Transmit)
			m_pMicTrack->unmute();
		else
			m_pMicTrack->mute();
		m_Transmitting = Transmit;
		++m_MicGeneration;
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
	if(m_pTokenRequest)
		m_pTokenRequest->Abort();
	if(m_Thread.joinable())
		m_Thread.join();
	m_pTokenRequest.reset();
	StopAudio();
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
	m_pAudioProcessing.reset();
	if(m_pRoom)
		m_pRoom->setDelegate(nullptr);
	m_pRoom.reset();
	{
		std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
		m_mAudioActivity.clear();
	}
	if(m_EngineInitialized)
		ma_engine_uninit(&m_AudioEngine);
	m_EngineInitialized = false;
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

void CRCVoiceChat::CheckAudio(ma_result Result, const char *pOperation)
{
	if(Result != MA_SUCCESS)
		throw std::runtime_error(std::string(pOperation) + ": " + ma_result_description(Result));
}

ma_uint32 CRCVoiceChat::ReadBuffer(ma_pcm_rb &Buffer, void *pOutput, ma_uint32 Frames)
{
	ma_uint32 Read = 0;
	const ma_uint32 FrameBytes = ma_get_bytes_per_frame(Buffer.format, Buffer.channels);
	while(Read < Frames)
	{
		ma_uint32 Count = Frames - Read;
		void *pData;
		if(ma_pcm_rb_acquire_read(&Buffer, &Count, &pData) != MA_SUCCESS || !Count)
			break;
		if(pOutput)
			std::memcpy(static_cast<char *>(pOutput) + Read * FrameBytes, pData, Count * FrameBytes);
		ma_pcm_rb_commit_read(&Buffer, Count);
		Read += Count;
	}
	return Read;
}

ma_uint32 CRCVoiceChat::WriteBuffer(ma_pcm_rb &Buffer, const void *pInput, ma_uint32 Frames)
{
	ma_uint32 Written = 0;
	const ma_uint32 FrameBytes = ma_get_bytes_per_frame(Buffer.format, Buffer.channels);
	while(Written < Frames)
	{
		ma_uint32 Count = Frames - Written;
		void *pData;
		if(ma_pcm_rb_acquire_write(&Buffer, &Count, &pData) != MA_SUCCESS || !Count)
			break;
		std::memcpy(pData, static_cast<const char *>(pInput) + Written * FrameBytes, Count * FrameBytes);
		ma_pcm_rb_commit_write(&Buffer, Count);
		Written += Count;
	}
	return Written;
}

void CRCVoiceChat::SAudioActivity::Init(ma_engine *pEngine)
{
	CheckAudio(ma_pcm_rb_init(ma_format_f32, 1, 2880, nullptr, nullptr, &m_Buffer), "initialize voice buffer");
	m_Initialized = 1;
	static const ma_data_source_vtable Vtable = {ReadTrack, nullptr, TrackFormat, nullptr, nullptr, nullptr, 0};
	ma_data_source_config SourceConfig = ma_data_source_config_init();
	SourceConfig.vtable = &Vtable;
	m_Source.m_pAudio = this;
	CheckAudio(ma_data_source_init(&SourceConfig, &m_Source.m_Base), "initialize voice source");
	m_Initialized = 2;
	const ma_gainer_config GainConfig = ma_gainer_config_init(2, 480);
	CheckAudio(ma_gainer_init(&GainConfig, nullptr, &m_Gainer), "initialize voice gains");
	m_Initialized = 3;
	ma_gainer_set_gain(&m_Gainer, 0.0f);
	const ma_channel_converter_config StereoConfig = ma_channel_converter_config_init(ma_format_f32, 1, nullptr, 2, nullptr, ma_channel_mix_mode_default);
	CheckAudio(ma_channel_converter_init(&StereoConfig, nullptr, &m_StereoConverter), "initialize voice stereo converter");
	m_Initialized = 4;
	CheckAudio(ma_sound_init_from_data_source(pEngine, &m_Source.m_Base,
		MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, &m_Sound), "initialize voice sound");
	m_Initialized = 5;
	CheckAudio(ma_sound_start(&m_Sound), "start voice sound");
}

CRCVoiceChat::SAudioActivity::~SAudioActivity()
{
	if(m_Initialized >= 5)
		ma_sound_uninit(&m_Sound);
	if(m_Initialized >= 4)
		ma_channel_converter_uninit(&m_StereoConverter, nullptr);
	if(m_Initialized >= 3)
		ma_gainer_uninit(&m_Gainer, nullptr);
	if(m_Initialized >= 2)
		ma_data_source_uninit(&m_Source.m_Base);
	if(m_Initialized >= 1)
		ma_pcm_rb_uninit(&m_Buffer);
	if(m_SourceRate)
		ma_data_converter_uninit(&m_Converter, nullptr);
}

void CRCVoiceChat::SAudioActivity::Clear()
{
	ma_pcm_rb_reset(&m_Buffer);
	m_SoundUntil = {};
	if(m_SourceRate)
		ma_data_converter_reset(&m_Converter);
}

ma_result CRCVoiceChat::TrackFormat(ma_data_source *pSource, ma_format *pFormat, ma_uint32 *pChannels, ma_uint32 *pRate, ma_channel *pMap, size_t MapSize)
{
	if(pFormat)
		*pFormat = ma_format_f32;
	if(pChannels)
		*pChannels = 2;
	if(pRate)
		*pRate = 48000;
	if(pMap)
		ma_channel_map_init_standard(ma_standard_channel_map_default, pMap, MapSize, 2);
	return MA_SUCCESS;
}

ma_result CRCVoiceChat::ReadTrack(ma_data_source *pSource, void *pOutput, ma_uint64 Frames, ma_uint64 *pRead)
{
	auto &Audio = *reinterpret_cast<SAudioActivity::SSource *>(pSource)->m_pAudio;
	const vec2 Gains = ma_pcm_rb_available_read(&Audio.m_Buffer) ? Audio.m_TargetGains : vec2(0.0f, 0.0f);
	if(Gains != Audio.m_AppliedGains)
	{
		float aGains[] = {Gains.x, Gains.y};
		ma_gainer_set_gains(&Audio.m_Gainer, aGains);
		Audio.m_AppliedGains = Gains;
	}
	ma_uint64 Done = 0;
	while(Done < Frames)
	{
		std::array<float, 480> aMono{};
		std::array<float, 960> aStereo;
		const auto Count = static_cast<ma_uint32>(std::min<ma_uint64>(480, Frames - Done));
		const ma_uint32 Read = ReadBuffer(Audio.m_Buffer, aMono.data(), Count);
		if(Read)
			Audio.m_LastSample = aMono[Read - 1];
		std::fill(aMono.begin() + Read, aMono.begin() + Count, Audio.m_LastSample);
		ma_channel_converter_process_pcm_frames(&Audio.m_StereoConverter, aStereo.data(), aMono.data(), Count);
		ma_gainer_process_pcm_frames(&Audio.m_Gainer, aStereo.data(), aStereo.data(), Count);
		if(pOutput)
			std::memcpy(static_cast<float *>(pOutput) + Done * 2, aStereo.data(), Count * 2 * sizeof(float));
		Done += Count;
	}
	if(pRead)
		*pRead = Frames;
	return MA_SUCCESS;
}

void CRCVoiceChat::AddAudioTrack(const std::string &TrackId, const std::string &Identity, bool Muted)
{
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	if(m_Cancelled || m_StopAudio || !m_EngineInitialized)
		return;
	try
	{
		auto pAudio = std::make_unique<SAudioActivity>();
		pAudio->m_Identity = Identity;
		pAudio->m_Muted = Muted;
		pAudio->Init(&m_AudioEngine);
		m_mAudioActivity[TrackId] = std::move(pAudio);
	}
	catch(const std::exception &Ex)
	{
		dbg_msg("voicechat", "could not add audio track: %s", Ex.what());
	}
}

void CRCVoiceChat::RecordAudioActivity(const std::string &TrackId, const livekit::AudioFrame &Frame)
{
	const int Rate = Frame.sampleRate();
	const int Channels = Frame.numChannels();
	if(Rate < 8000 || Rate > 192000 || Channels < 1 || Channels > 8 || Frame.data().empty() || Frame.data().size() % Channels != 0)
		return;
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	auto It = m_mAudioActivity.find(TrackId);
	if(It == m_mAudioActivity.end() || It->second->m_Muted || It->second->m_TargetGains == vec2(0.0f, 0.0f) ||
		!m_Connected || m_Cancelled || m_Reconnecting || m_StopAudio)
		return;
	auto &Audio = *It->second;
	const auto Now = std::chrono::steady_clock::now();
	if(Now - Audio.m_BufferTime > std::chrono::milliseconds(60))
		Audio.Clear();
	Audio.m_BufferTime = Now;
	if(Rate != Audio.m_SourceRate || Channels != Audio.m_SourceChannels)
	{
		Audio.Clear();
		if(Audio.m_SourceRate)
			ma_data_converter_uninit(&Audio.m_Converter, nullptr);
		Audio.m_SourceRate = 0;
		const ma_data_converter_config Config = ma_data_converter_config_init(ma_format_s16, ma_format_f32, Channels, 1, Rate, 48000);
		if(ma_data_converter_init(&Config, nullptr, &Audio.m_Converter) != MA_SUCCESS)
			return;
		Audio.m_SourceRate = Rate;
		Audio.m_SourceChannels = Channels;
	}
	ma_uint64 Remaining = std::min<ma_uint64>(Frame.data().size() / Channels, Rate * 60 / 1000);
	const auto *pInput = Frame.data().data() + Frame.data().size() - Remaining * Channels;
	while(Remaining)
	{
		std::array<float, 2880> aConverted;
		ma_uint64 InputFrames = Remaining, OutputFrames = aConverted.size();
		if(ma_data_converter_process_pcm_frames(&Audio.m_Converter, pInput, &InputFrames, aConverted.data(), &OutputFrames) != MA_SUCCESS || (!InputFrames && !OutputFrames))
			break;
		const auto Count = static_cast<ma_uint32>(OutputFrames);
		const ma_uint32 Available = ma_pcm_rb_available_write(&Audio.m_Buffer);
		if(Count > Available)
			ReadBuffer(Audio.m_Buffer, nullptr, Count - Available);
		WriteBuffer(Audio.m_Buffer, aConverted.data(), Count);
		if(std::any_of(aConverted.begin(), aConverted.begin() + Count, [](float Sample) { return Sample != 0.0f; }))
			Audio.m_SoundUntil = Now + std::chrono::milliseconds(200);
		pInput += InputFrames * Channels;
		Remaining -= InputFrames;
	}
}

void CRCVoiceChat::SetAudioMuted(const std::string &TrackId, bool Muted)
{
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	auto It = m_mAudioActivity.find(TrackId);
	if(It != m_mAudioActivity.end())
	{
		It->second->m_Muted = Muted;
		It->second->Clear();
		if(Muted)
			It->second->m_TargetGains = vec2(0.0f, 0.0f);
		++m_OutputGeneration;
	}
}

void CRCVoiceChat::ClearAudioActivity(const std::string &Identity)
{
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	std::erase_if(m_mAudioActivity, [&](const auto &Entry) { return Entry.second->m_Identity == Identity; });
	++m_OutputGeneration;
}

void CRCVoiceChat::UpdateTalking()
{
	m_MicGain = std::clamp(g_Config.m_RcVoiceMicVolume, 0, 300) / 100.0f;
	const auto Now = std::chrono::steady_clock::now();
	const bool Receiving = m_Connected && !m_Reconnecting && !m_Cancelled;
	const vec2 Listener = VoiceListenerPosition();
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	for(auto &[TrackId, pAudio] : m_mAudioActivity)
	{
		auto &Audio = *pAudio;
		const int Id = ClientIdOf(Audio.m_Identity);
		const bool HasPosition = Id >= 0 && Id < MAX_CLIENTS && GameClient()->m_aClients[Id].m_Active && GameClient()->m_Snap.m_aCharacters[Id].m_Active;
		const vec2 Delta = HasPosition ? GameClient()->m_aClients[Id].m_RenderPos - Listener : vec2(0.0f, 0.0f);
		const vec2 Gains = Receiving && !Audio.m_Muted ? VoiceGains(HasPosition, g_Config.m_RcVoiceSpatial, g_Config.m_RcVoiceFilterUnpositioned, Delta,
			g_Config.m_RcVoiceMinVolume, g_Config.m_RcVoiceMinRadius, g_Config.m_RcVoiceRadius,
			g_Config.m_RcVoiceVolume, g_Config.m_RcVoiceStereoWidth, g_Config.m_RcVoiceStereoRadius) : vec2(0.0f, 0.0f);
		if(Gains == vec2(0.0f, 0.0f) || Audio.m_TargetGains == vec2(0.0f, 0.0f))
		{
			Audio.Clear();
			if(Gains != Audio.m_TargetGains)
				++m_OutputGeneration;
		}
		Audio.m_TargetGains = Gains;
	}
	for(auto &P : m_vParticipants)
		P.m_Talking = Receiving && std::any_of(m_mAudioActivity.begin(), m_mAudioActivity.end(), [&](const auto &Entry) {
			const auto &Activity = *Entry.second;
			return Activity.m_Identity == P.m_Identity && !Activity.m_Muted && Activity.m_TargetGains != vec2(0.0f, 0.0f) && Now < Activity.m_SoundUntil;
		});
}

vec2 CRCVoiceChat::VoiceGains(bool HasPosition, bool Spatial, bool Filter, vec2 Delta, int MinVolume, int MinRadius, int Radius, int MasterVolume, int StereoWidth, int StereoRadius)
{
	const float Minimum = std::clamp(MinVolume, 0, 100) / 100.0f;
	float Volume = HasPosition ? 1.0f : (Filter ? 0.0f : Minimum);
	float Pan = 0.0f;
	if(HasPosition && Spatial)
	{
		const float Outer = std::clamp(Radius, 1, 200) * 32.0f;
		const float Inner = std::clamp(MinRadius, 1, std::clamp(Radius, 1, 200)) * 32.0f;
		const float Distance = length(Delta);
		Volume = Distance >= Outer ? 0.0f : 1.0f + (Minimum - 1.0f) * std::min(Distance / Inner, 1.0f);
		Pan = std::clamp(Delta.x / (std::clamp(StereoRadius, 1, 200) * 32.0f) * (std::clamp(StereoWidth, 0, 200) / 100.0f), -1.0f, 1.0f);
	}
	if(!std::isfinite(Delta.x) || !std::isfinite(Delta.y))
		return vec2(0.0f, 0.0f);
	const float Angle = (Pan + 1.0f) * (pi / 4.0f);
	return vec2(std::cos(Angle), std::sin(Angle)) * (Volume * (std::clamp(MasterVolume, 0, 400) / 100.0f));
}

void CRCVoiceChat::ApplyMicGain(std::int16_t *pSamples, float Gain)
{
	if(Gain == 1.0f)
		return;
	std::array<float, 480> aSamples;
	ma_pcm_s16_to_f32(aSamples.data(), pSamples, aSamples.size(), ma_dither_mode_none);
	ma_apply_volume_factor_f32(aSamples.data(), aSamples.size(), Gain);
	ma_pcm_f32_to_s16(pSamples, aSamples.data(), aSamples.size(), ma_dither_mode_none);
}

vec2 CRCVoiceChat::VoiceListenerPosition() const
{
	const auto *pGame = GameClient();
	if(!g_Config.m_RcVoiceListenerMode)
	{
		const int Id = pGame->m_Snap.m_SpecInfo.m_Active ? pGame->m_Snap.m_SpecInfo.m_SpectatorId : pGame->m_Snap.m_LocalClientId;
		if(Id >= 0 && Id < MAX_CLIENTS && pGame->m_aClients[Id].m_Active && pGame->m_Snap.m_aCharacters[Id].m_Active)
			return pGame->m_aClients[Id].m_RenderPos;
	}
	return pGame->m_Camera.m_Center;
}

std::string CRCVoiceChat::AudioDeviceId(ma_backend Backend, const ma_device_info &Device)
{
	const void *pId = &Device.id;
	size_t Size;
	switch(Backend)
	{
	case ma_backend_wasapi:
		Size = 0;
		while(Size < std::size(Device.id.wasapi) && Device.id.wasapi[Size])
			++Size;
		Size *= sizeof(Device.id.wasapi[0]);
		break;
	case ma_backend_dsound: Size = sizeof(Device.id.dsound); break;
	case ma_backend_winmm: Size = sizeof(Device.id.winmm); break;
	case ma_backend_jack: Size = sizeof(Device.id.jack); break;
	case ma_backend_null: Size = sizeof(Device.id.nullbackend); break;
	default: Size = std::strlen(reinterpret_cast<const char *>(pId)); break;
	}
	char aHash[SHA256_MAXSTRSIZE];
	sha256_str(sha256(pId, Size), aHash, sizeof(aHash));
	return std::string("ma:") + ma_get_backend_name(Backend) + ":" + aHash;
}

void CRCVoiceChat::OpenVoiceDevice(ma_device &Device, const std::string &Id, bool Capture)
{
	std::lock_guard<std::mutex> Lock(m_AudioContextLock);
	ma_device_info *pOutputs, *pInputs;
	ma_uint32 Outputs, Inputs;
	CheckAudio(ma_context_get_devices(&m_AudioContext, &pOutputs, &Outputs, &pInputs, &Inputs), "enumerate voice devices");
	const ma_device_info *pDevices = Capture ? pInputs : pOutputs;
	const ma_uint32 Count = Capture ? Inputs : Outputs;
	ma_device_id Selected;
	const ma_device_id *pSelected = nullptr;
	for(ma_uint32 i = 0; i < Count && !Id.empty(); ++i)
	{
		if(Id == AudioDeviceId(m_AudioContext.backend, pDevices[i]) || Id == std::string("sdl:") + pDevices[i].name)
		{
			Selected = pDevices[i].id;
			pSelected = &Selected;
			break;
		}
	}
	if(!Id.empty() && !pSelected && m_DeviceWarnings.insert((Capture ? "mic:" : "output:") + Id).second)
		dbg_msg("voicechat", "%s device not found, using system default", Capture ? "microphone" : "output");
	ma_device_config Config = ma_device_config_init(Capture ? ma_device_type_capture : ma_device_type_playback);
	Config.sampleRate = 48000;
	Config.periodSizeInFrames = 480;
	Config.periods = 2;
	Config.wasapi.noAutoStreamRouting = MA_TRUE;
	Config.capture.format = Config.playback.format = ma_format_s16;
	Config.capture.channels = 1;
	Config.playback.channels = 2;
	Config.capture.pDeviceID = Capture ? pSelected : nullptr;
	Config.playback.pDeviceID = Capture ? nullptr : pSelected;
	Config.dataCallback = DeviceCallback;
	Config.notificationCallback = DeviceNotification;
	Config.pUserData = this;
	CheckAudio(ma_device_init(&m_AudioContext, &Config, &Device), Capture ? "open voice microphone" : "open voice output");
}

void CRCVoiceChat::DeviceCallback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 Frames)
{
	auto *pVoice = static_cast<CRCVoiceChat *>(pDevice->pUserData);
	if(pOutput)
		ma_silence_pcm_frames(pOutput, Frames, ma_format_s16, 2);
	std::unique_lock<std::mutex> Lock(pVoice->m_DeviceBufferLock, std::try_to_lock);
	if(!Lock || pVoice->m_StopAudio)
		return;
	if(pOutput)
		ReadBuffer(pVoice->m_OutputBuffer, pOutput, Frames);
	if(pInput && !pVoice->m_MicFailed)
	{
		if(Frames > ma_pcm_rb_available_write(&pVoice->m_CaptureBuffer))
			ma_pcm_rb_reset(&pVoice->m_CaptureBuffer);
		const ma_uint32 Count = std::min<ma_uint32>(Frames, 2880);
		WriteBuffer(pVoice->m_CaptureBuffer, static_cast<const std::int16_t *>(pInput) + Frames - Count, Count);
	}
}

void CRCVoiceChat::DeviceNotification(const ma_device_notification *pNotification)
{
	auto *pVoice = static_cast<CRCVoiceChat *>(pNotification->pDevice->pUserData);
	if(!pVoice->m_StopAudio && (pNotification->type == ma_device_notification_type_stopped || pNotification->type == ma_device_notification_type_interruption_began))
	{
		if(pNotification->pDevice->type == ma_device_type_capture)
			pVoice->m_MicFailed = true;
		else
			pVoice->m_OutputFailed = true;
	}
}

void CRCVoiceChat::StartAudio(const std::string &MicDevice, const std::string &PlayoutDevice)
{
	m_MicFailed = m_OutputFailed = false;
	ma_engine_config Config = ma_engine_config_init();
	Config.noDevice = MA_TRUE;
	Config.channels = 2;
	Config.sampleRate = 48000;
	Config.periodSizeInFrames = 480;
	CheckAudio(ma_engine_init(&Config, &m_AudioEngine), "initialize voice mixer");
	m_EngineInitialized = true;
	CheckAudio(ma_pcm_rb_init(ma_format_s16, 2, 1920, nullptr, nullptr, &m_OutputBuffer), "initialize output buffer");
	const ma_result CaptureBufferResult = ma_pcm_rb_init(ma_format_s16, 1, 2880, nullptr, nullptr, &m_CaptureBuffer);
	if(CaptureBufferResult != MA_SUCCESS)
	{
		ma_pcm_rb_uninit(&m_OutputBuffer);
		CheckAudio(CaptureBufferResult, "initialize capture buffer");
	}
	m_BuffersInitialized = true;
	OpenVoiceDevice(m_OutputDevice, PlayoutDevice, false);
	m_OutputInitialized = true;
	m_DeviceDelayMs = int(1000 * m_OutputDevice.playback.internalPeriodSizeInFrames * m_OutputDevice.playback.internalPeriods / std::max<ma_uint32>(1, m_OutputDevice.playback.internalSampleRate));
	try
	{
		OpenVoiceDevice(m_CaptureDevice, MicDevice, true);
		m_CaptureInitialized = true;
		m_DeviceDelayMs += int(1000 * m_CaptureDevice.capture.internalPeriodSizeInFrames * m_CaptureDevice.capture.internalPeriods / std::max<ma_uint32>(1, m_CaptureDevice.capture.internalSampleRate));
		livekit::AudioProcessingModule::Options Options;
		Options.echo_cancellation = true;
		Options.noise_suppression = true;
		Options.auto_gain_control = true;
		Options.high_pass_filter = true;
		m_pAudioProcessing = std::make_unique<livekit::AudioProcessingModule>(Options);
		m_pMicSource = std::make_shared<livekit::AudioSource>(48000, 1, 0);
		CheckAudio(ma_device_start(&m_CaptureDevice), "start voice microphone");
	}
	catch(const std::exception &Ex)
	{
		m_MicFailed = true;
		dbg_msg("voicechat", "microphone unavailable: %s", Ex.what());
		if(m_CaptureInitialized)
			ma_device_uninit(&m_CaptureDevice);
		m_CaptureInitialized = false;
		m_pAudioProcessing.reset();
		m_pMicSource.reset();
	}
	m_StopAudio = false;
	CheckAudio(ma_device_start(&m_OutputDevice), "start voice output");
	m_AudioThread = std::thread(&CRCVoiceChat::AudioThread, this);
}

void CRCVoiceChat::MixAudio(std::int16_t *pOutput)
{
	std::array<float, 960> aMix{};
	{
		std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
		const auto Now = std::chrono::steady_clock::now();
		for(auto &[Id, pAudio] : m_mAudioActivity)
			if(!m_Connected || m_Cancelled || m_Reconnecting || pAudio->m_Muted || Now - pAudio->m_BufferTime > std::chrono::milliseconds(60))
				pAudio->Clear();
		CheckAudio(ma_engine_read_pcm_frames(&m_AudioEngine, aMix.data(), 480, nullptr), "mix voice audio");
	}
	float Peak = 1.0f;
	for(float Sample : aMix)
		Peak = std::max(Peak, std::abs(Sample));
	ma_apply_volume_factor_f32(aMix.data(), aMix.size(), 1.0f / Peak);
	ma_pcm_f32_to_s16(pOutput, aMix.data(), aMix.size(), ma_dither_mode_none);
}

void CRCVoiceChat::AudioThread()
{
	try
	{
		livekit::AudioFrame Microphone = livekit::AudioFrame::create(48000, 1, 480);
		livekit::AudioFrame Reference = livekit::AudioFrame::create(48000, 2, 480);
		std::array<std::int16_t, 960> aOutput;
		unsigned OutputGeneration = m_OutputGeneration, MicGeneration = m_MicGeneration;
		while(!m_StopAudio)
		{
			if(m_OutputFailed)
				throw std::runtime_error("voice output device stopped or disconnected");
			if(m_MicFailed && m_pAudioProcessing)
				DisableMicrophone("microphone device stopped or disconnected");
			ma_uint32 OutputFrames, CaptureFrames;
			bool HaveMicrophone = false;
			{
				std::lock_guard<std::mutex> Lock(m_DeviceBufferLock);
				if(OutputGeneration != m_OutputGeneration)
				{
					ma_pcm_rb_reset(&m_OutputBuffer);
					OutputGeneration = m_OutputGeneration;
				}
				if(MicGeneration != m_MicGeneration)
				{
					ma_pcm_rb_reset(&m_CaptureBuffer);
					MicGeneration = m_MicGeneration;
				}
				OutputFrames = ma_pcm_rb_available_read(&m_OutputBuffer);
				CaptureFrames = ma_pcm_rb_available_read(&m_CaptureBuffer);
				if(m_pAudioProcessing && CaptureFrames >= 480)
					HaveMicrophone = ReadBuffer(m_CaptureBuffer, Microphone.data().data(), 480) == 480;
			}
			if(OutputFrames < 960)
			{
				MixAudio(aOutput.data());
				bool Queued = false;
				{
					std::lock_guard<std::mutex> Lock(m_DeviceBufferLock);
					if(OutputGeneration == m_OutputGeneration)
						Queued = WriteBuffer(m_OutputBuffer, aOutput.data(), 480) == 480;
				}
				if(Queued && m_pAudioProcessing)
				{
					try
					{
						std::copy(aOutput.begin(), aOutput.end(), Reference.data().begin());
						m_pAudioProcessing->processReverseStream(Reference);
					}
					catch(const std::exception &Ex)
					{
						DisableMicrophone(Ex.what());
					}
				}
			}
			if(HaveMicrophone && m_pAudioProcessing)
			{
				try
				{
					m_pAudioProcessing->setStreamDelayMs(m_DeviceDelayMs + int(OutputFrames + CaptureFrames) / 48);
					m_pAudioProcessing->processStream(Microphone);
					if(Transmitting() && !m_Cancelled && MicGeneration == m_MicGeneration)
					{
						ApplyMicGain(Microphone.data().data(), m_MicGain.load());
						m_pMicSource->captureFrame(Microphone, 20);
					}
				}
				catch(const std::exception &Ex)
				{
					DisableMicrophone(Ex.what());
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}
	catch(const std::exception &Ex)
	{
		m_Connected = false;
		m_Cancelled = true;
		dbg_msg("voicechat", "audio output failed: %s", Ex.what());
	}
}

void CRCVoiceChat::DisableMicrophone(const char *pError)
{
	m_MicFailed = true;
	m_MicReady = false;
	m_pAudioProcessing.reset();
	if(m_CaptureInitialized)
		ma_device_stop(&m_CaptureDevice);
	std::lock_guard<std::mutex> Lock(m_DeviceBufferLock);
	ma_pcm_rb_reset(&m_CaptureBuffer);
	dbg_msg("voicechat", "microphone processing failed: %s", pError);
}

void CRCVoiceChat::StopAudio()
{
	m_StopAudio = true;
	if(m_AudioThread.joinable())
		m_AudioThread.join();
	if(m_CaptureInitialized)
		ma_device_uninit(&m_CaptureDevice);
	if(m_OutputInitialized)
		ma_device_uninit(&m_OutputDevice);
	m_CaptureInitialized = m_OutputInitialized = false;
	if(m_BuffersInitialized)
	{
		ma_pcm_rb_uninit(&m_CaptureBuffer);
		ma_pcm_rb_uninit(&m_OutputBuffer);
	}
	m_BuffersInitialized = false;
}

std::string CRCVoiceChat::RoomNameFromAddress() const
{
	const NETADDR &Addr = Client()->ServerAddress();
	if(Addr.type == NETTYPE_INVALID)
		return {};

	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aAddr, sizeof(aAddr), true);

	std::string RoomName = aAddr;
	if(g_Config.m_RcVoiceRoomMode == 1 && g_Config.m_RcVoiceRoomName[0] != '\0')
		RoomName = std::string(g_Config.m_RcVoiceRoomName) + "@" + RoomName;
	for(char &c : RoomName)
	{
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			   c == '.' || c == '-' || c == '_' || c == ':' || c == '@'))
			c = '_';
	}
	return RoomName;
}

#endif

std::vector<CRCVoiceChat::SDevice> CRCVoiceChat::MicDevices()
{
#ifdef CONF_LIVEKIT
	return AudioDevices(true);
#else
	return {};
#endif
}

std::vector<CRCVoiceChat::SDevice> CRCVoiceChat::PlayoutDevices()
{
#ifdef CONF_LIVEKIT
	return AudioDevices(false);
#else
	return {};
#endif
}

#ifdef CONF_LIVEKIT
std::vector<CRCVoiceChat::SDevice> CRCVoiceChat::AudioDevices(bool Capture)
{
	std::lock_guard<std::mutex> Lock(m_AudioContextLock);
	std::vector<SDevice> vDevices;
	if(!m_AudioInitialized)
		return vDevices;
	ma_device_info *pOutputs, *pInputs;
	ma_uint32 Outputs, Inputs;
	if(ma_context_get_devices(&m_AudioContext, &pOutputs, &Outputs, &pInputs, &Inputs) != MA_SUCCESS)
		return vDevices;
	vDevices.push_back({"System default", ""});
	const ma_device_info *pDevices = Capture ? pInputs : pOutputs;
	for(ma_uint32 i = 0; i < (Capture ? Inputs : Outputs); ++i)
		vDevices.push_back({pDevices[i].name, AudioDeviceId(m_AudioContext.backend, pDevices[i])});
	return vDevices;
}

#endif

int CRCVoiceChat::ClientIdOf(const std::string &Identity) const
{
#ifdef CONF_LIVEKIT
	auto It = m_mClientIds.find(Identity);
	return It != m_mClientIds.end() ? It->second : -1;
#else
	return -1;
#endif
}
