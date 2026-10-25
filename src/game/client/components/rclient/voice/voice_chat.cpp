#include "voice_chat.h"

#include "voice_chat_logic.h"

#include "base/net.h"
#include "base/str.h"
#include <base/log.h>
#include "engine/client.h"
#include "engine/textrender.h"
#include "game/client/gameclient.h"
#include "game/client/ui.h"
#include "game/client/ui_rect.h"
#include "game/localization.h"
#include "game/teamscore.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <utility>

#ifdef CONF_LIVEKIT
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
		m_FullMuted = g_Config.m_RcVoiceMuted != 0;
		ma_context_config Config = ma_context_config_init();
		ma_backend Backends[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm,
			ma_backend_coreaudio, ma_backend_pulseaudio, ma_backend_alsa, ma_backend_jack,
			ma_backend_sndio, ma_backend_audio4, ma_backend_oss};
		CheckAudio(ma_context_init(Backends, std::size(Backends), &Config, &m_AudioContext), "initialize audio devices");
		m_AudioInitialized = true;
	}
	catch(const std::exception &Ex)
	{
		char aError[256];
		str_format(aError, sizeof(aError), "voice chat initialization failed: %s", Ex.what());
		SetLastError(aError);
	}
#endif
}

void CRCVoiceChat::OnReset()
{
#ifdef CONF_LIVEKIT
	// Map change: keep the session, OnRender reconnects if the client id changed.
	m_ResumeCheck = true;
	m_DeviceWarnings.clear();
	m_DeviceDelayMs = 0;
#endif
}

void CRCVoiceChat::OnStateChange(int NewState, int OldState)
{
#ifdef CONF_LIVEKIT
	if(NewState < IClient::STATE_ONLINE)
	{
		ResetConnection();
		m_ManualDisconnect = false;
		m_NextAutoConnect = 0.0f;
		m_ConnectFailures = 0;
		m_ConnectedRoom.clear();
		m_ResumeCheck = false;
	}
#else
	(void)NewState;
	(void)OldState;
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

	// Menu checkbox writes the config, console commands write the atomic.
	if(Muted() != (g_Config.m_RcVoiceMuted != 0))
	{
		m_FullMuted = g_Config.m_RcVoiceMuted != 0;
		ApplyMicState();
	}

	if(m_PttHeld && (Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Menus.IsActive() ||
				GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive()))
		OnRelease();
	ApplyMicState();
	if(!m_Connected && !m_Connecting && m_Thread.joinable())
		ResetConnection();
	UpdateTalking();
	UpdateMetadata();

	const bool OnServer = Client()->ServerAddress().type != NETTYPE_INVALID;
	const bool InGame = OnServer && Client()->State() == IClient::STATE_ONLINE;
	const int LocalId = LocalClientId();

	// Reconnect only if the client id actually changed during a map change.
	if(m_ResumeCheck)
	{
		if(!m_Connected && !m_Connecting)
			m_ResumeCheck = false;
		else if(InGame && LocalId >= 0)
		{
			m_ResumeCheck = false;
			if(m_Connected && LocalId != m_ConnectedClientId)
			{
				log_trace("voicechat", "client id changed from %d to %d on reset, reconnecting", m_ConnectedClientId, LocalId);
				ResetConnection();
				ConnectInternal(true);
			}
		}
	}

	// Server can change without leaving STATE_ONLINE, check once per second.
	if(InGame && m_Connected && g_Config.m_RcVoiceTokenUrl[0] != '\0' && Client()->GlobalTime() >= m_NextRoomCheck)
	{
		m_NextRoomCheck = Client()->GlobalTime() + 1.0f;
		const std::string Room = RoomNameFromAddress();
		if(!Room.empty() && !m_ConnectedRoom.empty() && Room != m_ConnectedRoom)
		{
			log_trace("voicechat", "voice room changed from '%s' to '%s', reconnecting", m_ConnectedRoom.c_str(), Room.c_str());
			ResetConnection();
			ConnectInternal(true);
		}
	}

	// Backoff, then stop after MAX_CONNECT_FAILURES.
	if(InGame && g_Config.m_RcVoiceAutoConnect && !m_Connected && !m_Connecting && !m_ManualDisconnect &&
		m_ConnectFailures < MAX_CONNECT_FAILURES && Client()->GlobalTime() >= m_NextAutoConnect)
	{
		m_NextAutoConnect = Client()->GlobalTime() + VoiceLogic::NextAutoConnectDelay(m_ConnectFailures);
		log_trace("voicechat", "auto connect (room '%s', failures %d)", RoomNameFromAddress().c_str(), m_ConnectFailures);
		ConnectInternal(false);
	}

	if(!g_Config.m_RcVoiceHud || Client()->State() != IClient::STATE_ONLINE)
		return;

	Ui()->MapScreen();
	const CUIRect Screen = *Ui()->Screen();
	const std::string Error = LastError();
	const bool Session = m_Connected || m_Connecting || m_Reconnecting;

	if(!Session)
	{
		const float PillWidth = 230.0f;
		CUIRect Pill{Screen.w - PillWidth - 20.0f, 20.0f, PillWidth, Error.empty() ? 22.0f : 38.0f};
		Pill.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f), IGraphics::CORNER_ALL, 11.0f);
		Pill.VMargin(6.0f, &Pill);
		Pill.HMargin(10.0f, &Pill);

		CUIRect Row;
		Pill.HSplitTop(14.0f, &Row, &Pill);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.7f);
		Ui()->DoLabel(&Row, RCLocalize("Voice: off", "RClient"), 9.0f, TEXTALIGN_ML);
		if(!Error.empty())
		{
			char aMessage[128];
			str_copy(aMessage, Error.c_str(), 120);
			Pill.HSplitTop(14.0f, &Row, &Pill);
			TextRender()->TextColor(1.0f, 0.55f, 0.55f, 0.9f);
			Ui()->DoLabel(&Row, aMessage, 8.0f, TEXTALIGN_ML);
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	const std::vector<SParticipant> vParticipants = Participants();

	const float PanelWidth = 300.0f;
	const int RowCount = 2 + (int)vParticipants.size() + (Error.empty() ? 0 : 1);
	CUIRect Panel{Screen.w - PanelWidth - 20.0f, 20.0f, PanelWidth, 10.0f + 14.0f * RowCount};
	Panel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_ALL, 10.0f);
	Panel.VMargin(8.0f, &Panel);

	CUIRect Row;
	auto DrawRow = [&](const char *pText, ColorRGBA Color, float Size) {
		Panel.HSplitTop(14.0f, &Row, &Panel);
		TextRender()->TextColor(Color);
		Ui()->DoLabel(&Row, pText, Size, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	};

	const char *pStatus = "Voice: ready (PTT)";
	if(m_Reconnecting)
		pStatus = "Voice: reconnecting...";
	else if(m_Connecting)
		pStatus = "Voice: preparing mic...";
	else if(!m_MicReady || m_MicFailed)
		pStatus = "Voice: mic unavailable";
	else if(Muted())
		pStatus = "Voice: muted";
	else if(Transmitting())
		pStatus = "Voice: talking";
	DrawRow(RCLocalize(pStatus, "RClient"), ColorRGBA(1.0f, 1.0f, 1.0f, 0.85f), 9.0f);

	// Own row: shows whether the microphone works.
	{
		char aLabel[96];
		const char *pName = LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_aName[0] != '\0' ?
					    GameClient()->m_aClients[LocalId].m_aName :
					    (LocalId >= 0 ? "(loading)" : "(not in game)");
		const char *pSuffix = Muted() ? " [mute]" : (m_MicReady && !m_MicFailed ? "" : " [mic off]");
		if(LocalId >= 0)
			str_format(aLabel, sizeof(aLabel), "%s [%d]%s", pName, LocalId, pSuffix);
		else
			str_format(aLabel, sizeof(aLabel), "%s%s", pName, pSuffix);
		DrawRow(aLabel, ColorRGBA(1.0f, 1.0f, 1.0f, Muted() ? 0.5f : 1.0f), 8.0f);
	}

	for(const auto &P : vParticipants)
	{
		const int ClientId = ClientIdOf(P.m_Identity);
		// Never list our own leftover session twice.
		if((LocalId >= 0 && ClientId == LocalId) || IsLocalIdentity(P.m_Identity))
			continue;
		const char *pName = DisplayNameOf(P, ClientId);
		const char *pReason = VoiceLogic::MuteReasonLabel((VoiceLogic::EMuteReason)P.m_MuteReason);

		char aLabel[96];
		if(ClientId >= 0)
			str_format(aLabel, sizeof(aLabel), "%s%s [%d]", P.m_Talking ? "* " : "", pName, ClientId);
		else
			str_format(aLabel, sizeof(aLabel), "%s%s", P.m_Talking ? "* " : "", pName);

		ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
		if(pReason != nullptr)
			Color = ColorRGBA(0.62f, 0.62f, 0.62f, 1.0f); // filtered: readable, but obviously inactive
		else if(P.m_Talking)
			Color = ColorRGBA(0.4f, 1.0f, 0.4f, 1.0f);

		Panel.HSplitTop(14.0f, &Row, &Panel);
		if(pReason != nullptr)
		{
			char aReason[48];
			str_format(aReason, sizeof(aReason), "(%s)", pReason);
			CUIRect NameRect, ReasonRect;
			Row.VSplitRight(96.0f, &NameRect, &ReasonRect);
			TextRender()->TextColor(Color);
			Ui()->DoLabel(&NameRect, aLabel, 8.0f, TEXTALIGN_ML);

			ReasonRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_ALL, 3.0f);
			TextRender()->TextColor(0.75f, 0.75f, 0.75f, 0.8f);
			Ui()->DoLabel(&ReasonRect, aReason, 7.0f, TEXTALIGN_MC);
		}
		else
		{
			TextRender()->TextColor(Color);
			Ui()->DoLabel(&Row, aLabel, 8.0f, TEXTALIGN_ML);
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if(!Error.empty())
	{
		char aMessage[128];
		str_copy(aMessage, Error.c_str(), 120);
		DrawRow(aMessage, ColorRGBA(1.0f, 0.55f, 0.55f, 0.9f), 8.0f);
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
	Console()->Register("rc_voice_toggle_mute", "", CFGFLAG_CLIENT, ConToggleMute, this, "Toggle microphone mute");
	Console()->Register("rc_voice_status", "", CFGFLAG_CLIENT, ConStatus, this, "Voice chat status");
	Console()->Register("rc_voice_mute_name", "s[name]", CFGFLAG_CLIENT, ConMuteName, this, "Locally mute a player's voice");
	Console()->Register("rc_voice_unmute_name", "s[name]", CFGFLAG_CLIENT, ConUnmuteName, this, "Remove a local voice mute");
	Console()->Register("rc_voice_mute_list", "", CFGFLAG_CLIENT, ConMuteList, this, "List locally muted voices");
	Console()->Register("rc_voice_purge", "", CFGFLAG_CLIENT, ConPurge, this, "Drop stale voice participants left by crashed sessions");

	// Muted names are extra lines in settings_rclient.cfg.
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::RCLIENT);
}

void CRCVoiceChat::OnShutdown()
{
#ifdef CONF_LIVEKIT
	m_ManualDisconnect = true;
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
	ConnectInternal(true);
}

void CRCVoiceChat::ConnectInternal(bool Manual)
{
#ifdef CONF_LIVEKIT
	if(Manual)
		m_ConnectFailures = 0;
	if(!m_Initialized || !m_AudioInitialized)
	{
		SetLastError("voice chat is not initialized (LiveKit or audio devices unavailable)");
		return;
	}
	if(m_Connecting || m_Connected)
		return;
	if(g_Config.m_RcVoiceTokenUrl[0] == '\0' && (g_Config.m_RcVoiceUrl[0] == '\0' || g_Config.m_RcVoiceToken[0] == '\0'))
	{
		++m_ConnectFailures;
		SetLastError("set rc_voice_token_url or manual rc_voice_url and rc_voice_token first");
		return;
	}
	const int LocalId = LocalClientId();
	if(Client()->State() != IClient::STATE_ONLINE || Client()->ServerAddress().type == NETTYPE_INVALID ||
		LocalId < 0 || LocalId >= MAX_CLIENTS)
	{
		++m_ConnectFailures;
		SetLastError("not in game yet");
		return;
	}

	const bool UseEndpoint = g_Config.m_RcVoiceTokenUrl[0] != '\0';
	const float Now = Client()->GlobalTime();
	if(UseEndpoint && m_LastTokenRequestTime > 0.0f && Now - m_LastTokenRequestTime < 5.0f)
	{
		SetLastError("token endpoint rate limited, retrying in a few seconds");
		return;
	}

	SetLastError("");
	ResetConnection();
	m_ManualDisconnect = false;
	m_MicGain = std::clamp(g_Config.m_RcVoiceMicVolume, 0, 300) / 100.0f;
	m_Cancelled = false;
	m_Connecting = true;
	try
	{
		const std::string RoomName = RoomNameFromAddress();
		if(UseEndpoint)
		{
			CJsonStringWriter Json;
			Json.BeginObject();
			Json.WriteAttribute("room_name");
			Json.WriteStrValue(RoomName.c_str());
			Json.WriteAttribute("participant_name");
			Json.WriteStrValue(GameClient()->m_aClients[LocalId].m_aName);
			Json.WriteAttribute("participant_metadata");
			Json.WriteStrValue(std::to_string(LocalId).c_str());
			Json.EndObject();
			m_LastTokenRequestTime = Now;
			m_pTokenRequest = HttpPostJson(g_Config.m_RcVoiceTokenUrl, Json.GetOutputString().c_str());
			m_pTokenRequest->Timeout(CTimeout{4000, 10000, 500, 5});
			m_pTokenRequest->MaxResponseSize(16 * 1024);
			m_pTokenRequest->LogProgress(HTTPLOG::NONE);
			m_pTokenRequest->FailOnErrorStatus(false);
			GameClient()->Http()->Run(m_pTokenRequest);
		}
		m_Thread = std::thread(&CRCVoiceChat::ConnectThread, this,
			std::string(g_Config.m_RcVoiceUrl), std::string(g_Config.m_RcVoiceToken), LocalId,
			std::string(g_Config.m_RcVoiceMicDevice), std::string(g_Config.m_RcVoicePlayoutDevice), m_pTokenRequest, RoomName);
	}
	catch(const std::exception &Ex)
	{
		if(m_pTokenRequest)
			m_pTokenRequest->Abort();
		m_pTokenRequest.reset();
		m_Connecting = false;
		++m_ConnectFailures;
		char aError[256];
		str_format(aError, sizeof(aError), "could not start connection: %s", Ex.what());
		SetLastError(aError);
	}
#else
	dbg_msg("voicechat", "voice chat is not available in this build");
	SetLastError("voice chat is not available in this build");
#endif
}

void CRCVoiceChat::Disconnect()
{
#ifdef CONF_LIVEKIT
	m_ManualDisconnect = true;
	m_ConnectFailures = 0;
	m_NextAutoConnect = 0.0f;
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
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->SetMuted(true);
	dbg_msg("voicechat", "mic fully muted (PTT disabled)");
}

void CRCVoiceChat::ConUnmute(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->SetMuted(false);
	dbg_msg("voicechat", "mic enabled (transmit via PTT)");
}

void CRCVoiceChat::ConToggleMute(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->SetMuted(!pSelf->Muted());
	dbg_msg("voicechat", "mic %s", pSelf->Muted() ? "fully muted" : "enabled");
}

const char *CRCVoiceChat::DisplayNameOf(const SParticipant &Participant, int ClientId)
{
#ifdef CONF_LIVEKIT
	// In-game name wins over the token name.
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_Active)
	{
		const char *pGameName = GameClient()->m_aClients[ClientId].m_aName;
		if(pGameName[0] != '\0')
			return pGameName;
	}
#else
	(void)ClientId;
#endif
	return Participant.m_Name.empty() ? Participant.m_Identity.c_str() : Participant.m_Name.c_str();
}

bool CRCVoiceChat::IsLocalIdentity(const std::string &Identity)
{
#ifdef CONF_LIVEKIT
	std::lock_guard<std::mutex> Lock(m_EventLock);
	return !m_LocalIdentity.empty() && Identity == m_LocalIdentity;
#else
	(void)Identity;
	return false;
#endif
}

void CRCVoiceChat::ConStatus(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	const std::string Error = pSelf->LastError();

	dbg_msg("voicechat", "=== voice chat status ===");
#ifdef CONF_LIVEKIT
	const int LocalId = pSelf->LocalClientId();

	// Session
	const char *pState = "disconnected";
	if(pSelf->m_Connected)
		pState = pSelf->m_Reconnecting ? "reconnecting" : "connected";
	else if(pSelf->m_Connecting)
		pState = "connecting";
	dbg_msg("voicechat", "session: %s  room='%s'  server_room='%s'", pState,
		pSelf->m_ConnectedRoom.empty() ? "-" : pSelf->m_ConnectedRoom.c_str(),
		pSelf->RoomNameFromAddress().empty() ? "-" : pSelf->RoomNameFromAddress().c_str());
	dbg_msg("voicechat", "ids: local_client_id=%d metadata_client_id=%d connected_client_id=%d",
		LocalId, pSelf->m_MetadataClientId, pSelf->m_ConnectedClientId);
	dbg_msg("voicechat", "mode: %s  autoconnect=%d  failures=%d  manual_disconnect=%d",
		g_Config.m_RcVoiceTokenUrl[0] != '\0' ? "token endpoint" : "manual url+token",
		g_Config.m_RcVoiceAutoConnect, pSelf->m_ConnectFailures, (int)pSelf->m_ManualDisconnect);

	// Audio
	dbg_msg("voicechat", "audio: engine=%d  output=%d  capture=%d  device_delay=%dms",
		(int)pSelf->m_EngineInitialized, (int)pSelf->m_OutputInitialized,
		(int)pSelf->m_CaptureInitialized, pSelf->m_DeviceDelayMs);
	dbg_msg("voicechat", "audio: mic=%s  out=%s", g_Config.m_RcVoiceMicDevice[0] != '\0' ? g_Config.m_RcVoiceMicDevice : "system default",
		g_Config.m_RcVoicePlayoutDevice[0] != '\0' ? g_Config.m_RcVoicePlayoutDevice : "system default");
	dbg_msg("voicechat", "mic: ready=%d failed=%d transmitting=%d muted=%d ptt=%d",
		(int)pSelf->m_MicReady, (int)pSelf->m_MicFailed, (int)pSelf->m_Transmitting,
		(int)pSelf->m_FullMuted, (int)pSelf->m_PttHeld);

	// Filter settings
	dbg_msg("voicechat", "filters: spatial=%d listener=%s unpositioned=%d team_only=%d volume=%d%%",
		g_Config.m_RcVoiceSpatial, g_Config.m_RcVoiceListenerMode ? "camera" : "character",
		g_Config.m_RcVoiceFilterUnpositioned, g_Config.m_RcVoiceTeamOnly, g_Config.m_RcVoiceVolume);

	// A leftover from a crashed session has no client id and no track.
	if(pSelf->m_Connected && pSelf->m_pRoom)
	{
		const vec2 Listener = pSelf->VoiceListenerPosition();
		std::lock_guard<std::mutex> Lock(pSelf->m_AudioActivityLock);
		dbg_msg("voicechat", "participants: %d (tracks: %d)", (int)pSelf->m_vParticipants.size(), (int)pSelf->m_mAudioActivity.size());
		for(const auto &P : pSelf->m_vParticipants)
		{
			const int ClientId = pSelf->ClientIdOf(P.m_Identity);
			const bool Active = ClientId >= 0 && ClientId < MAX_CLIENTS && pSelf->GameClient()->m_aClients[ClientId].m_Active;
			const bool HasPosition = Active && pSelf->GameClient()->m_Snap.m_aCharacters[ClientId].m_Active;
			const char *pReason = VoiceLogic::MuteReasonLabel((VoiceLogic::EMuteReason)P.m_MuteReason);
			char aDistance[24] = "-";
			if(HasPosition)
			{
				const float Distance = length(pSelf->GameClient()->m_aClients[ClientId].m_RenderPos - Listener) / 32.0f;
				str_format(aDistance, sizeof(aDistance), "%.1ft", Distance);
			}
			int Tracks = 0;
			for(const auto &Entry : pSelf->m_mAudioActivity)
				if(Entry.second->m_Identity == P.m_Identity)
					++Tracks;

			const char *pDisplay = pSelf->DisplayNameOf(P, ClientId);
			dbg_msg("voicechat", "  '%s' id=%d dist=%s tracks=%d talking=%d%s%s%s",
				pDisplay[0] != '\0' ? pDisplay : "(no name)", ClientId, aDistance, Tracks,
				(int)P.m_Talking,
				pReason != nullptr ? " muted=" : "", pReason != nullptr ? pReason : "",
				Tracks == 0 ? "  [no audio track]" : "");
		}
	}
#else
	dbg_msg("voicechat", "voice chat is not available in this build (no CONF_LIVEKIT)");
#endif

	const std::vector<std::string> vMuted = pSelf->MutedNames();
	dbg_msg("voicechat", "muted_names: %d", (int)vMuted.size());
	for(const auto &Name : vMuted)
		dbg_msg("voicechat", "  %s", Name.c_str());
	if(!Error.empty())
		dbg_msg("voicechat", "last_error: %s", Error.c_str());
	dbg_msg("voicechat", "========================");
}

void CRCVoiceChat::ConMuteName(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->MuteName(pResult->GetString(0));
}

void CRCVoiceChat::ConUnmuteName(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	pSelf->UnmuteName(pResult->GetString(0));
}

void CRCVoiceChat::ConPurge(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	const int Removed = pSelf->PurgeStaleParticipants();
	log_trace("voicechat", "removed %d stale participant(s), %d left", Removed, (int)pSelf->m_vParticipants.size());
}

int CRCVoiceChat::PurgeStaleParticipants()
{
#ifdef CONF_LIVEKIT
	// A crashed client never sends its leave, LiveKit keeps it until timeout.
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	const auto Removed = std::erase_if(m_vParticipants, [&](const SParticipant &P) {
		if(ClientIdOf(P.m_Identity) >= 0)
			return false;
		return std::none_of(m_mAudioActivity.begin(), m_mAudioActivity.end(),
			[&](const auto &Entry) { return Entry.second->m_Identity == P.m_Identity; });
	});
	return (int)Removed;
#else
	return 0;
#endif
}

void CRCVoiceChat::ConMuteList(IConsole::IResult *pResult, void *pUserData)
{
	CRCVoiceChat *pSelf = (CRCVoiceChat *)pUserData;
	const std::vector<std::string> vMuted = pSelf->MutedNames();
	if(vMuted.empty())
	{
		dbg_msg("voicechat", "no muted voices");
		return;
	}
	dbg_msg("voicechat", "muted voices: %d", (int)vMuted.size());
	for(const auto &Name : vMuted)
		dbg_msg("voicechat", "  %s", Name.c_str());
}

// Nick is the only stable mute key: identity is random per connection.
static std::string TrimMuteName(const char *pName)
{
	if(pName == nullptr)
		return {};
	std::string Name = pName;
	const auto First = Name.find_first_not_of(" \t");
	if(First == std::string::npos)
		return {};
	const auto Last = Name.find_last_not_of(" \t");
	return Name.substr(First, Last - First + 1);
}

static std::string MuteKeyOf(const std::string &Name)
{
	std::string Key = Name;
	for(char &c : Key)
		if(c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	return Key;
}

void CRCVoiceChat::MuteName(const char *pName)
{
	const std::string Name = TrimMuteName(pName);
	if(Name.empty())
		return;
	std::lock_guard<std::mutex> Lock(m_MuteLock);
	if(!m_MutedNameSet.insert(MuteKeyOf(Name)).second)
	{
		log_trace("voicechat", "'%s' is already muted", Name.c_str());
		return;
	}
	m_vMutedNames.push_back(Name);
	log_trace("voicechat", "muted voice of '%s'", Name.c_str());
}

void CRCVoiceChat::UnmuteName(const char *pName)
{
	const std::string Name = TrimMuteName(pName);
	if(Name.empty())
		return;
	std::lock_guard<std::mutex> Lock(m_MuteLock);
	if(m_MutedNameSet.erase(MuteKeyOf(Name)) == 0)
		return;
	std::erase_if(m_vMutedNames, [&](const std::string &Entry) { return MuteKeyOf(Entry) == MuteKeyOf(Name); });
	log_trace("voicechat", "unmuted voice of '%s'", Name.c_str());
}

bool CRCVoiceChat::IsNameMuted(const char *pName) const
{
	const std::string Name = TrimMuteName(pName);
	if(Name.empty())
		return false;
	std::lock_guard<std::mutex> Lock(m_MuteLock);
	return m_MutedNameSet.find(MuteKeyOf(Name)) != m_MutedNameSet.end();
}

void CRCVoiceChat::ToggleNameMute(const char *pName)
{
	if(IsNameMuted(pName))
		UnmuteName(pName);
	else
		MuteName(pName);
}

std::vector<std::string> CRCVoiceChat::MutedNames() const
{
	std::lock_guard<std::mutex> Lock(m_MuteLock);
	return m_vMutedNames;
}

void CRCVoiceChat::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CRCVoiceChat *pThis = (CRCVoiceChat *)pUserData;
	std::lock_guard<std::mutex> Lock(pThis->m_MuteLock);
	for(const std::string &Name : pThis->m_vMutedNames)
	{
		char aEscaped[512];
		char *pDst = aEscaped;
		str_escape(&pDst, Name.c_str(), aEscaped + sizeof(aEscaped));
		char aLine[600];
		str_format(aLine, sizeof(aLine), "rc_voice_mute_name \"%s\"", aEscaped);
		pConfigManager->WriteLine(aLine, ConfigDomain::RCLIENT);
	}
}

void CRCVoiceChat::SetLastError(const char *pError)
{
	{
		std::lock_guard<std::mutex> Lock(m_ErrorLock);
		str_copy(m_aLastError, pError != nullptr ? pError : "", sizeof(m_aLastError));
	}
	if(pError != nullptr && pError[0] != '\0')
		log_warn("voicechat", "%s", pError);
}

std::string CRCVoiceChat::LastError() const
{
	std::lock_guard<std::mutex> Lock(m_ErrorLock);
	return m_aLastError;
}

bool CRCVoiceChat::Muted() const
{
#ifdef CONF_LIVEKIT
	return m_FullMuted;
#else
	return g_Config.m_RcVoiceMuted != 0;
#endif
}

void CRCVoiceChat::SetMuted(bool Muted)
{
	g_Config.m_RcVoiceMuted = Muted ? 1 : 0;
#ifdef CONF_LIVEKIT
	m_FullMuted = Muted;
	ApplyMicState();
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
		if(Url.empty() || Token.empty())
			throw std::runtime_error("missing voice server URL or token");
		// Expired tokens crash Room::connect() inside the SDK.
		{
			const long long Now = (long long)std::time(nullptr);
			if(VoiceLogic::TokenUnusable(Token, Now))
			{
				const long long Expiry = VoiceLogic::TokenExpiry(Token);
				char aError[192];
				if(Expiry > 0)
					str_format(aError, sizeof(aError), "voice token expired %lld minutes ago, generate a new one with local_token.py", (Now - Expiry) / 60);
				else
					str_copy(aError, "voice token is not a usable JWT, generate a new one with local_token.py");
				throw std::runtime_error(aError);
			}
		}
		log_trace("voicechat", "connecting to '%s' (token valid for %lld min)", Url.c_str(),
			(VoiceLogic::TokenExpiry(Token) - (long long)std::time(nullptr)) / 60);
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
		m_ConnectedClientId = LocalClientId;
		m_ConnectedRoom = m_pRoom->roomInfo().name;
		m_MetadataClientId = -1;
		m_ConnectFailures = 0;
		dbg_msg("voicechat", "joined room '%s'", m_pRoom->roomInfo().name.c_str());

		// Own identity, to filter our own leftover session.
		if(auto pLocal = m_pRoom->localParticipant().lock())
		{
			std::string Identity = pLocal->identity();
			std::lock_guard<std::mutex> Lock(m_EventLock);
			m_LocalIdentity = Identity;
		}

		// Not inside an SDK callback, so remoteParticipants() is safe here.
		m_Delegate.SnapshotParticipants(*m_pRoom);

		// After connect: Room::connect() owns the FFI client while it runs.
		StartAudioProcessing();
		try
		{
			auto pLocal = m_pRoom->localParticipant().lock();
			if(!pLocal)
				throw std::runtime_error("local participant is unavailable");
			try
			{
				if(pLocal->metadata() != std::to_string(LocalClientId))
					pLocal->setMetadata(std::to_string(LocalClientId));
				m_MetadataClientId = LocalClientId;
			}
			catch(const std::exception &)
			{
				m_MetadataClientId = -1;
				SetLastError("cannot update voice metadata: the token needs can_update_own_metadata");
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
				log_trace("voicechat", "mic published");
			}
		}
		catch(const std::exception &Ex)
		{
			SetLastError(Ex.what());
			log_warn("voicechat", "mic setup failed: %s", Ex.what());
		}

		// Audio thread uses the FFI client, so start it last.
		StartAudioThread();
	}
	catch(const std::exception &Ex)
	{
		m_Connected = false;
		if(!m_Cancelled)
		{
			++m_ConnectFailures;
			SetLastError(Ex.what());
		}
	}
	catch(...)
	{
		m_Connected = false;
		if(!m_Cancelled)
		{
			++m_ConnectFailures;
			SetLastError("connect failed with an unknown error");
		}
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
		SetLastError(Ex.what());
		log_warn("voicechat", "mic state failed; closing voice connection: %s", Ex.what());
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
	m_ConnectedRoom.clear();
	m_ConnectedClientId = -1;
	m_MetadataClientId = -1;
	m_LimiterGain = 1.0f;
	m_vParticipants.clear();
	m_mClientIds.clear();
	std::lock_guard<std::mutex> Lock(m_EventLock);
	m_vEvents.clear();
	m_LocalIdentity.clear();
}

int CRCVoiceChat::LocalClientId() const
{
	// Dummy: the controlled player is m_aLocalIds[ClDummy].
	const auto *pGame = GameClient();
	const int Id = pGame->m_aLocalIds[g_Config.m_ClDummy];
	if(Id >= 0 && Id < MAX_CLIENTS)
		return Id;
	return pGame->m_Snap.m_LocalClientId;
}

void CRCVoiceChat::UpdateMetadata()
{
	if(!m_Connected || m_Cancelled || !m_pRoom)
		return;
	const int LocalId = LocalClientId();
	if(LocalId < 0 || LocalId == m_MetadataClientId)
		return;
	// Throttle, a dummy swap switches the player within a frame or two.
	if(Client()->GlobalTime() < m_LastMetadataSync + 2.0f)
		return;

	m_LastMetadataSync = Client()->GlobalTime();
	m_MetadataClientId = LocalId;
	try
	{
		auto pLocal = m_pRoom->localParticipant().lock();
		if(!pLocal)
		{
			m_MetadataClientId = -1;
			return;
		}
		pLocal->setMetadata(std::to_string(LocalId));
		log_trace("voicechat", "voice metadata set to client id %d", LocalId);
	}
	catch(const std::exception &Ex)
	{
		m_MetadataClientId = -1;
		SetLastError("cannot update voice metadata: the token needs can_update_own_metadata");
		log_warn("voicechat", "could not update client id metadata: %s", Ex.what());
	}
}

void CRCVoiceChat::PushEvent(SEvent Event)
{
	std::lock_guard<std::mutex> Lock(m_EventLock);
	m_vEvents.push_back(std::move(Event));
}

void CRCVoiceChat::ProcessEvents()
{
	std::vector<SEvent> vEvents;
	std::string LocalIdentity;
	{
		std::lock_guard<std::mutex> Lock(m_EventLock);
		vEvents.swap(m_vEvents);
		LocalIdentity = m_LocalIdentity;
	}
	for(const SEvent &Ev : vEvents)
	{
		// Our own participant is never a remote one.
		if(Ev.m_Type == EEventType::ParticipantJoined && !LocalIdentity.empty() && Ev.m_Identity == LocalIdentity)
			continue;
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
			m_mClientIds[Ev.m_Identity] = VoiceLogic::ParseClientId(Ev.m_Metadata);
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
			{
				if(!LocalIdentity.empty() && Identity == LocalIdentity)
					continue;
				if(std::none_of(m_vParticipants.begin(), m_vParticipants.end(),
					   [&](const SParticipant &P) { return P.m_Identity == Identity; }))
					m_vParticipants.push_back({Identity, {}, false});
			}
			break;
		case EEventType::Disconnected:
			m_vParticipants.clear();
			m_mClientIds.clear();
			break;
		}
	}
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
		log_warn("voicechat", "could not add audio track: %s", Ex.what());
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
	const int LocalId = LocalClientId();
	const int FilterMode = std::clamp(g_Config.m_RcVoiceFilterUnpositioned,
		static_cast<int>(VoiceLogic::POSITION_FILTER_OFF), static_cast<int>(VoiceLogic::POSITION_FILTER_ALL));
	const bool TeamOnly = g_Config.m_RcVoiceTeamOnly != 0;
	const int MyTeam = LocalId >= 0 ? GameClient()->m_Teams.Team(LocalId) : -1;
	const bool MyTeamValid = MyTeam != TEAM_FLOCK && MyTeam != TEAM_SUPER;

	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	for(auto &[TrackId, pAudio] : m_mAudioActivity)
	{
		auto &Audio = *pAudio;
		const int Id = ClientIdOf(Audio.m_Identity);
		const bool Active = Id >= 0 && Id < MAX_CLIENTS && GameClient()->m_aClients[Id].m_Active;
		// Server stops sending a character outside the network clip radius.
		const bool HasPosition = Active && GameClient()->m_Snap.m_aCharacters[Id].m_Active;
		const vec2 Delta = HasPosition ? GameClient()->m_aClients[Id].m_RenderPos - Listener : vec2(0.0f, 0.0f);

		// Cached name, so mutes work before the client id is known.
		if(Active)
			Audio.m_Name = GameClient()->m_aClients[Id].m_aName;
		else
		{
			const auto It = std::find_if(m_vParticipants.begin(), m_vParticipants.end(),
				[&](const SParticipant &P) { return P.m_Identity == Audio.m_Identity; });
			Audio.m_Name = It != m_vParticipants.end() ? It->m_Name : std::string();
		}

		// Spectators have no character, so the position filter checks them separately.
		const bool IsSpectator = Active && GameClient()->m_aClients[Id].m_Team == TEAM_SPECTATORS;
		bool TeamMismatch = false;
		if(TeamOnly && MyTeamValid && Active && !IsSpectator)
		{
			const int TheirTeam = GameClient()->m_Teams.Team(Id);
			TeamMismatch = TheirTeam != TEAM_FLOCK && TheirTeam != TEAM_SUPER && TheirTeam != MyTeam;
		}

		VoiceLogic::SParticipantMix Mix;
		Mix.SessionActive = Receiving;
		Mix.TrackMuted = Audio.m_Muted;
		Mix.NameMuted = IsNameMuted(Audio.m_Name.c_str());
		// -1 means unknown and must not mute everyone.
		Mix.IsLocal = Id >= 0 && Id == LocalId;
		Mix.HasPosition = HasPosition;
		Mix.IsSpectator = IsSpectator;
		Mix.TeamOnly = TeamOnly;
		Mix.MyTeamValid = MyTeamValid;
		Mix.TeamMismatch = TeamMismatch;
		Mix.FilterMode = FilterMode;

		const VoiceLogic::EMuteReason Reason = VoiceLogic::MuteReasonOf(Mix);
		const bool Mute = Reason != VoiceLogic::MUTE_NONE;
		Audio.m_MuteReason = Reason;
		const vec2 Gains = Mute ? vec2(0.0f, 0.0f) : VoiceLogic::VoiceGains(HasPosition, g_Config.m_RcVoiceSpatial, false, Delta,
										g_Config.m_RcVoiceMinVolume, g_Config.m_RcVoiceMinRadius, g_Config.m_RcVoiceRadius,
										g_Config.m_RcVoiceVolume, g_Config.m_RcVoiceStereoWidth, g_Config.m_RcVoiceStereoRadius);
		if(Gains == vec2(0.0f, 0.0f) || Audio.m_TargetGains == vec2(0.0f, 0.0f))
		{
			Audio.Clear();
			if(Gains != Audio.m_TargetGains)
				++m_OutputGeneration;
		}
		Audio.m_TargetGains = Gains;
	}

	// Roll track reasons up to the participant: first muted track explains it.
	for(auto &P : m_vParticipants)
	{
		P.m_Talking = false;
		P.m_MuteReason = VoiceLogic::MUTE_NONE;
		bool HasTrack = false;
		bool AnyAudible = false;
		for(const auto &Entry : m_mAudioActivity)
		{
			const auto &Activity = *Entry.second;
			if(Activity.m_Identity != P.m_Identity)
				continue;
			HasTrack = true;
			if(Activity.m_MuteReason == VoiceLogic::MUTE_NONE)
				AnyAudible = true;
			else if(P.m_MuteReason == VoiceLogic::MUTE_NONE)
				P.m_MuteReason = Activity.m_MuteReason;
			if(!Activity.m_Muted && Activity.m_TargetGains != vec2(0.0f, 0.0f) && Now < Activity.m_SoundUntil)
				P.m_Talking = true;
		}
		if(AnyAudible)
			P.m_MuteReason = VoiceLogic::MUTE_NONE;
		else if(!HasTrack)
		{
			// Either still subscribing or a leftover session.
			P.m_MuteReason = VoiceLogic::MUTE_NO_TRACK;
		}
		P.m_Talking = P.m_Talking && Receiving;
	}
}

void CRCVoiceChat::ApplyMicGain(std::int16_t *pSamples, size_t Samples, float Gain)
{
	if(Gain == 1.0f || Samples == 0)
		return;
	std::array<float, 480> aSamples;
	for(size_t Offset = 0; Offset < Samples; Offset += aSamples.size())
	{
		const size_t Count = std::min(aSamples.size(), Samples - Offset);
		ma_pcm_s16_to_f32(aSamples.data(), pSamples + Offset, Count, ma_dither_mode_none);
		ma_apply_volume_factor_f32(aSamples.data(), Count, Gain);
		ma_pcm_f32_to_s16(pSamples + Offset, aSamples.data(), Count, ma_dither_mode_none);
	}
}

vec2 CRCVoiceChat::VoiceListenerPosition() const
{
	const auto *pGame = GameClient();
	if(!g_Config.m_RcVoiceListenerMode)
	{
		const int Id = pGame->m_Snap.m_SpecInfo.m_Active ? pGame->m_Snap.m_SpecInfo.m_SpectatorId : LocalClientId();
		if(Id >= 0 && Id < MAX_CLIENTS && pGame->m_aClients[Id].m_Active && pGame->m_Snap.m_aCharacters[Id].m_Active)
			return pGame->m_aClients[Id].m_RenderPos;
	}
	return pGame->m_Camera.m_Center;
}

std::string CRCVoiceChat::RoomNameFromAddress() const
{
	const NETADDR &Addr = Client()->ServerAddress();
	if(Addr.type == NETTYPE_INVALID)
		return {};

	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aAddr, sizeof(aAddr), true);
	return VoiceLogic::SanitizeRoomName(aAddr, g_Config.m_RcVoiceRoomMode, g_Config.m_RcVoiceRoomName);
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

int CRCVoiceChat::ClientIdOf(const std::string &Identity) const
{
#ifdef CONF_LIVEKIT
	auto It = m_mClientIds.find(Identity);
	return It != m_mClientIds.end() ? It->second : -1;
#else
	return -1;
#endif
}
