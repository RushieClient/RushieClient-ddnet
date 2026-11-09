#include "voice_chat.h"

#include "voice_chat_logic.h"

#include <base/log.h>
#include <base/net.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/rclient/rclient_include.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/localization.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <utility>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-negative"
#endif

#ifdef CONF_LIVEKIT
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

static std::int64_t VoiceNowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static vec2 VoicePlayerPosition(const CGameClient *pGame, int ClientId, float IntraTick)
{
	// Use the same snapshot timeline for every participant, independently of
	// rendering and client-side visual prediction.
	const auto &Character = pGame->m_Snap.m_aCharacters[ClientId];
	return mix(vec2(Character.m_Prev.m_X, Character.m_Prev.m_Y),
		vec2(Character.m_Cur.m_X, Character.m_Cur.m_Y), IntraTick);
}
#endif

CRCVoiceChat::CRCVoiceChat()
#ifdef CONF_LIVEKIT
	:
	m_Delegate(this)
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
		m_Deafened = g_Config.m_RcVoiceDeafened != 0;
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
	if(!m_Connecting)
	{
		std::unique_lock<std::mutex> Lock(m_AudioContextLock, std::try_to_lock);
		if(Lock)
			m_DeviceWarnings.clear();
	}
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
		m_ConnectPending = false;
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
	m_PttTail.Cancel();
	ApplyMicState();
#endif
}

void CRCVoiceChat::OnUpdate()
{
#ifdef CONF_LIVEKIT
	ProcessEvents();
	if(Deafened() != (g_Config.m_RcVoiceDeafened != 0))
		SetDeafened(g_Config.m_RcVoiceDeafened != 0);
	if(m_MicrophoneTesting)
	{
		m_MicGain = std::clamp(g_Config.m_RcVoiceMicVolume, 0, 300) / 100.0f;
		if(Client()->GlobalTime() - m_MicrophoneTestLastVisible > 0.5f || !GameClient()->m_Menus.IsActive() ||
			m_TestMicDevice != g_Config.m_RcVoiceMicDevice || m_TestPlayoutDevice != g_Config.m_RcVoicePlayoutDevice)
			StopMicrophoneTest();
	}

	// Menu checkbox writes the config, console commands write the atomic.
	if(Muted() != (g_Config.m_RcVoiceMuted != 0))
	{
		SetMuted(g_Config.m_RcVoiceMuted != 0);
	}

	if(PttActive() && (Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Menus.IsActive() ||
				  GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive()))
		OnRelease();
	// Keep mute/PTT handling responsive during setup, but do not access the
	// room or launch the metadata worker until Room::connect() has returned.
	if(m_Connecting)
		return;
	ApplyMicState();
	// Setup completion and a later disconnect both need cleanup. The setup
	// thread has already been joined by the time most disconnects arrive.
	if(m_Thread.joinable())
	{
		m_Thread.join();
		if(!m_Connected && (!m_MicrophoneTesting || m_Cancelled))
			RequestReset();
		else if(!m_MicrophoneTesting)
		{
			m_ConnectPending = false;
			if(m_MetadataClientId < 0)
				m_MetadataRetry.OnFailure(m_MetadataRequestedClientId, Client()->GlobalTime());
		}
	}
	if(m_ResetPending || (!m_Connected && m_pRoom) || (m_MicrophoneTesting && m_Cancelled))
		ResetConnection();
	if(m_ResetPending)
		return;

	// A reconnect that never completes keeps the HUD on "reconnecting" forever.
	// Give up after a while and let auto connect fetch a fresh token.
	if(m_Connected && m_Reconnecting)
	{
		const float Now = Client()->GlobalTime();
		const float Since = m_ReconnectingSince.load();
		if(Since <= 0.0f)
			m_ReconnectingSince = Now;
		else if(Now - Since > 30.0f)
		{
			log_warn("voicechat", "reconnect stuck for %d seconds, dropping the session", (int)(Now - Since));
			m_ReconnectingSince = 0.0f;
			ResetConnection();
			// The network is probably still down. Retrying in this same frame
			// would only burn an attempt against a dead name server.
			m_NextAutoConnect = Now + 5.0f;
		}
	}
	else
	{
		m_ReconnectingSince = 0.0f;
	}

	UpdateTalking();
	UpdateMetadata();
	UpdatePing();

	const bool OnServer = Client()->ServerAddress().type != NETTYPE_INVALID;
	const bool InGame = OnServer && Client()->State() == IClient::STATE_ONLINE;

	// Server can change without leaving STATE_ONLINE, check once per second.
	if(InGame && m_Connected && Client()->GlobalTime() >= m_NextRoomCheck)
	{
		m_NextRoomCheck = Client()->GlobalTime() + 1.0f;
		const std::string Room = RoomNameFromAddress();
		if(!Room.empty() && !m_ConnectedRoom.empty() && Room != m_ConnectedRoom)
		{
			log_trace("voicechat", "voice room changed from '%s' to '%s', reconnecting", m_ConnectedRoom.c_str(), Room.c_str());
			ResetConnection();
			m_ConnectPending = true;
			m_NextPendingConnect = 0.0f;
		}
	}

	// Keep an explicit connect queued until cleanup and the cooldown finish,
	// even when automatic connections are disabled.
	if(m_ConnectPending && !m_Connected && !m_Connecting && !m_ResetPending && InGame &&
		LocalClientId() >= 0 && Client()->GlobalTime() >= m_NextPendingConnect)
	{
		m_ConnectPending = false;
		ConnectInternal(true);
	}

	// Backoff, then stop after MAX_CONNECT_FAILURES.
	if(InGame && g_Config.m_RcVoiceAutoConnect && !m_MicrophoneTesting && !m_Connected && !m_Connecting && !m_ResetPending && !m_ConnectPending && !m_ManualDisconnect &&
		m_ConnectFailures < MAX_CONNECT_FAILURES && Client()->GlobalTime() >= m_NextAutoConnect)
	{
		m_NextAutoConnect = Client()->GlobalTime() + VoiceLogic::NextAutoConnectDelay(m_ConnectFailures);
		log_trace("voicechat", "auto connect (room '%s', failures %d)", RoomNameFromAddress().c_str(), m_ConnectFailures.load());
		ConnectInternal(false);
	}

#endif
}

void CRCVoiceChat::OnRender()
{
#ifdef CONF_LIVEKIT
	RenderParticipantsOverlay();
	RenderStatusHud();
	RenderDebugHud();
#endif
}

#ifdef CONF_LIVEKIT
void CRCVoiceChat::UpdatePing()
{
	const float Now = Client()->GlobalTime();
	if(!m_Connected || m_Reconnecting || m_Cancelled || !m_pRoom)
	{
		m_PingMs = -1;
		return;
	}
	if(m_LastPingUpdate >= 0.0f && Now - m_LastPingUpdate > 10.0f)
		m_PingMs = -1;
	if(m_StatsThread.joinable())
	{
		if(m_StatsBusy)
			return;
		m_StatsThread.join();
		m_PingMs = m_StatsPingMs;
		m_LastPingUpdate = Now;
	}
	if(g_Config.m_RcVoiceIndicator && g_Config.m_RcVoiceStatus && Now >= m_NextStatsRequest)
	{
		m_NextStatsRequest = Now + 1.0f;
		m_StatsBusy = true;
		try
		{
			m_StatsThread = std::thread(&CRCVoiceChat::PingThread, this);
		}
		catch(const std::exception &)
		{
			m_StatsBusy = false;
			m_PingMs = -1;
		}
	}
}

void CRCVoiceChat::PingThread()
{
	int Ping = -1;
	try
	{
		// Dispatch may take SDK locks or perform synchronous FFI work.
		// Cleanup joins this worker before destroying the room.
		auto Future = m_pRoom->getStats();
		while(!m_Cancelled && !m_Reconnecting && Future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
		{
		}
		if(!m_Cancelled && !m_Reconnecting && Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			const auto Stats = Future.get();
			for(const auto *pStats : {&Stats.publisher_stats, &Stats.subscriber_stats})
			{
				for(const auto &Entry : *pStats)
				{
					const auto *pTransport = std::get_if<livekit::RtcTransportStats>(&Entry.stats);
					if(!pTransport || pTransport->transport.selected_candidate_pair_id.empty())
						continue;
					for(const auto &Candidate : *pStats)
					{
						const auto *pPair = std::get_if<livekit::RtcCandidatePairStats>(&Candidate.stats);
						if(!pPair || pPair->rtc.id != pTransport->transport.selected_candidate_pair_id)
							continue;
						const double Rtt = pPair->candidate_pair.current_round_trip_time;
						if(pPair->candidate_pair.responses_received > 0 && std::isfinite(Rtt) && Rtt >= 0.0 && Rtt < 60.0)
							Ping = std::max(Ping, (int)std::lround(Rtt * 1000.0));
					}
				}
			}
		}
	}
	catch(const std::exception &Ex)
	{
		log_debug("voicechat", "could not retrieve connection stats: %s", Ex.what());
	}
	m_StatsPingMs = Ping;
	m_StatsBusy = false;
}

void CRCVoiceChat::RenderStatusHud()
{
	if(!g_Config.m_RcVoiceIndicator || Client()->State() != IClient::STATE_ONLINE || !g_Config.m_ClShowhud ||
		GameClient()->m_Chat.IsActive() || m_MicrophoneTesting)
		return;

	const CUi::CScopedScreenAspect AspectScope(Ui(), g_Config.m_RcCustomAspectDisable & RcAspectDisable::VOICEINDICATOR);
	const float Height = 300.0f;
	const float Width = Height * Ui()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);
	const float FontSize = GameClient()->m_Chat.FontSize();
	const float Size = FontSize;
	const ivec2 Offset = GameClient()->m_RcHudEditor.GetElementPosition(CHudEditor::ELEM_VOICEINDICATOR);
	const float X = 5.0f + Offset.x;
	const float Y = Height - (20.0f * FontSize / 6.0f + (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f)) + Offset.y;
	const bool Ready = m_Connected && !m_Reconnecting && !m_ResetPending && m_MicReady && !m_MicFailed;
	const bool Muted = g_Config.m_RcVoiceMuted != 0;
	ColorRGBA Color = Ready ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f);
	if(Muted)
		Color = ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f);
	else if(Transmitting())
		Color = ColorRGBA(0.35f, 1.0f, 0.35f, 1.0f);
	char aPing[32];
	const char *pStatus;
	if(m_ResetPending)
		pStatus = RCLocalize("Disconnecting...", "RClient");
	else if(m_Connecting)
		pStatus = RCLocalize("Connecting...", "RClient");
	else if(m_ConnectPending)
		pStatus = RCLocalize("Connection queued...", "RClient");
	else if(m_Reconnecting)
		pStatus = RCLocalize("Reconnecting...", "RClient");
	else if(!m_Connected)
		pStatus = RCLocalize("Disconnected", "RClient");
	else if(m_OutputFailed)
		pStatus = RCLocalize("Audio output unavailable, retrying...", "RClient");
	else if(m_MicFailed)
		pStatus = RCLocalize("Microphone unavailable, retrying...", "RClient");
	else
	{
		if(m_PingMs >= 0)
			str_format(aPing, sizeof(aPing), "%d ms", m_PingMs);
		else
			str_copy(aPing, "-- ms");
		pStatus = aPing;
	}
	const float Padding = 3.0f;
	const float Gap = 3.0f;
	const float StatusWidth = g_Config.m_RcVoiceStatus ? Gap + TextRender()->TextWidth(Size, pStatus) : 0.0f;
	const CUIRect Background{X, Y - 2.0f, Padding * 2.0f + Size * 2.0f + Gap + StatusWidth, Size + 4.0f};
	Background.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_ALL, 3.0f);
	TextRender()->TextColor(Color);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->Text(X + Padding, Y, Size, FontIcon::MICROPHONE, -1.0f);
	TextRender()->TextColor(Deafened() ? ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	TextRender()->Text(X + Padding + Size + Gap, Y, Size, FontIcon::RC_HEADPHONES, -1.0f);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	if(g_Config.m_RcVoiceStatus)
		TextRender()->Text(X + Padding + (Size + Gap) * 2.0f, Y, Size, pStatus, -1.0f);
}

void CRCVoiceChat::RenderParticipantsOverlay()
{
	if(!g_Config.m_RcVoiceOverlay || !g_Config.m_ClShowhud || Client()->State() != IClient::STATE_ONLINE ||
		!m_Connected || m_Reconnecting || m_ResetPending || m_MicrophoneTesting)
		return;
	struct SRow
	{
		std::string m_Name;
		std::string m_Identity;
		int m_ClientId;
		bool m_Muted;
		bool m_Speaking;
		vec2 m_Direction;
		VoiceLogic::SOverlaySortKey m_SortKey;
	};
	std::vector<SRow> vRows;
	// Match the displayed characters, including prediction and smoothing.
	// Audio attenuation keeps its independent snapshot-based timeline.
	vec2 Listener = GameClient()->m_Camera.m_Center;
	if(!g_Config.m_RcVoiceListenerMode)
	{
		const int ListenerId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : LocalClientId();
		if(ListenerId >= 0 && ListenerId < MAX_CLIENTS && GameClient()->m_aClients[ListenerId].m_Active && GameClient()->m_Snap.m_aCharacters[ListenerId].m_Active)
			Listener = GameClient()->m_aClients[ListenerId].m_RenderPos;
	}
	for(const auto &P : m_vParticipants)
	{
		const int Id = ClientIdOf(P.m_Identity);
		const std::string Name = DisplayNameOf(P, Id);
		const auto Settings = UserSettings(Name.c_str());
		const bool Muted = Settings.m_Muted;
		const bool Speaking = P.m_Speaking;
		if(VoiceLogic::ShowOverlayParticipant(Speaking, Muted, g_Config.m_RcVoiceOverlayMode == 0, g_Config.m_RcVoiceOverlayMuted))
		{
			const bool HasPosition = Id >= 0 && Id < MAX_CLIENTS && GameClient()->m_aClients[Id].m_Active && GameClient()->m_Snap.m_aCharacters[Id].m_Active;
			const vec2 Delta = HasPosition ? GameClient()->m_aClients[Id].m_RenderPos - Listener : vec2(0.0f, 0.0f);
			vRows.push_back({Name, P.m_Identity, Id, Muted, Speaking, VoiceLogic::OverlayDirection(HasPosition, Delta),
				{Speaking, std::chrono::duration_cast<std::chrono::nanoseconds>(P.m_SpeakingStartedAt.time_since_epoch()).count(),
					std::chrono::duration_cast<std::chrono::nanoseconds>(P.m_LastSoundUntil.time_since_epoch()).count(), Id}});
		}
	}
	std::sort(vRows.begin(), vRows.end(), [](const SRow &A, const SRow &B) {
		if(A.m_SortKey < B.m_SortKey)
			return true;
		if(B.m_SortKey < A.m_SortKey)
			return false;
		return A.m_Identity < B.m_Identity;
	});
	const CUi::CScopedScreenAspect AspectScope(Ui(), g_Config.m_RcCustomAspectDisable & RcAspectDisable::VOICEOVERLAY);
	const float Height = 300.0f;
	Graphics()->MapScreenToSize(Height * Ui()->ScreenAspect(), Height);
	const ivec2 Offset = GameClient()->m_RcHudEditor.GetElementPosition(CHudEditor::ELEM_VOICEOVERLAY);
	const float X = 5.0f + Offset.x;
	float Y = 90.0f + Offset.y;
	constexpr float Scale = 0.6f;
	const float RowHeight = 16.0f * Scale;
	const float FontSize = 7.0f * Scale;
	const float Padding = 3.0f * Scale;
	const float MaxNameWidth = 60.0f * Scale;
	const float IconsWidth = (g_Config.m_RcVoiceOverlayDirection ? 12.0f * Scale : 0.0f) + (g_Config.m_RcVoiceOverlaySkin ? 18.0f * Scale : 0.0f);
	const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_RcVoiceOverlayBackgroundColor, true));
	constexpr size_t MaxRows = 8;
	const size_t VisibleRows = std::min(vRows.size(), MaxRows);
	std::array<float, MaxRows> aNameWidths{};
	float PanelWidth = 0.0f;
	for(size_t i = 0; i < VisibleRows; ++i)
	{
		aNameWidths[i] = std::min(TextRender()->TextWidth(FontSize, vRows[i].m_Name.c_str()) + 0.5f * Scale, MaxNameWidth);
		PanelWidth = std::max(PanelWidth, Padding * 2.0f + IconsWidth + aNameWidths[i]);
	}
	char aMore[64] = "";
	if(vRows.size() > MaxRows)
	{
		str_format(aMore, sizeof(aMore), RCLocalize("+%d more", "RClient"), (int)(vRows.size() - MaxRows));
		PanelWidth = std::max(PanelWidth, Padding * 2.0f + TextRender()->TextWidth(FontSize, aMore));
	}
	if(g_Config.m_RcVoiceOverlaySharedBackground && VisibleRows > 0)
	{
		const float PanelHeight = (VisibleRows + (aMore[0] ? 1 : 0)) * RowHeight - Scale;
		const CUIRect Panel{X, Y, PanelWidth, PanelHeight};
		Panel.Draw(BackgroundColor, IGraphics::CORNER_ALL, 3.0f * Scale);
	}
	for(size_t i = 0; i < VisibleRows; ++i)
	{
		const auto &Row = vRows[i];
		const float NameWidth = aNameWidths[i];
		CUIRect Rect{X, Y, Padding * 2.0f + IconsWidth + NameWidth, RowHeight - Scale};
		if(!g_Config.m_RcVoiceOverlaySharedBackground)
			Rect.Draw(BackgroundColor, IGraphics::CORNER_ALL, 3.0f * Scale);
		const ColorRGBA Color = Row.m_Muted ? ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f) : (Row.m_Speaking ? ColorRGBA(0.35f, 1.0f, 0.35f, 1.0f) : TextRender()->DefaultTextColor());
		float ContentX = X + Padding;
		if(g_Config.m_RcVoiceOverlayDirection)
		{
			const vec2 Center(ContentX + 5.0f * Scale, Y + Rect.h / 2.0f);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(Color);
			if(Row.m_Direction == vec2(0.0f, 0.0f))
				Graphics()->DrawCircle(Center.x, Center.y, 2.5f * Scale, 16);
			else
			{
				const vec2 Side(-Row.m_Direction.y, Row.m_Direction.x);
				const vec2 Tip = Center + Row.m_Direction * (5.0f * Scale);
				const vec2 Base = Center - Row.m_Direction * Scale;
				const vec2 Back = Center - Row.m_Direction * (5.0f * Scale);
				IGraphics::CFreeformItem Shaft(Back - Side * Scale, Back + Side * Scale, Base - Side * Scale, Base + Side * Scale);
				Graphics()->QuadsDrawFreeform(&Shaft, 1);
				IGraphics::CFreeformItem Head(Tip, Base - Side * (3.5f * Scale), Base + Side * (3.5f * Scale), Base);
				Graphics()->QuadsDrawFreeform(&Head, 1);
			}
			Graphics()->QuadsEnd();
			ContentX += 12.0f * Scale;
		}
		if(g_Config.m_RcVoiceOverlaySkin)
		{
			CTeeRenderInfo TeeInfo;
			if(Row.m_ClientId >= 0 && Row.m_ClientId < MAX_CLIENTS && GameClient()->m_aClients[Row.m_ClientId].m_Active)
				TeeInfo = GameClient()->m_aClients[Row.m_ClientId].m_RenderInfo;
			else
				TeeInfo.Apply(GameClient()->m_Skins.Find("default"));
			TeeInfo.m_Size = 14.0f * Scale;
			vec2 TeeOffset;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, TeeOffset);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(ContentX + 7.0f * Scale, Y + Rect.h / 2.0f + TeeOffset.y));
			ContentX += 18.0f * Scale;
		}
		CUIRect Name{ContentX, Y, NameWidth, Rect.h};
		TextRender()->TextColor(Color);
		SLabelProperties Props;
		Props.m_EllipsisAtEnd = true;
		Props.m_EnableWidthCheck = false;
		Props.m_MinimumFontSize = FontSize;
		Props.m_MaxWidth = NameWidth;
		Ui()->DoLabel(&Name, Row.m_Name.c_str(), FontSize, TEXTALIGN_ML, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Y += RowHeight;
	}
	if(aMore[0])
	{
		TextRender()->Text(X + Padding, Y + 2.0f * Scale, FontSize, aMore, -1.0f);
	}
}

void CRCVoiceChat::RenderDebugHud()
{
	if(!g_Config.m_RcVoiceDebugHud || Client()->State() != IClient::STATE_ONLINE)
		return;

	Ui()->MapScreen();
	const CUIRect Screen = *Ui()->Screen();

	// Setup/cleanup own SDK objects and devices until their workers finish.
	// Only read game-thread UI and atomic connection state on this path.
	if(m_Connecting || m_ResetPending || m_ConnectPending)
	{
		CUIRect Pill{Screen.w - 250.0f, 20.0f, 230.0f, 22.0f};
		Pill.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f), IGraphics::CORNER_ALL, 11.0f);
		Pill.HMargin(4.0f, &Pill);
		Pill.VMargin(10.0f, &Pill);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.85f);
		const char *pStatus = m_ResetPending ? "Voice: disconnecting..." : (m_Connecting ? "Voice: connecting..." : "Voice: connection queued...");
		Ui()->DoLabel(&Pill, RCLocalize(pStatus, "RClient"), 9.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	const int LocalId = LocalClientId();
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
	// Keep room for three rows so the panel does not jump when people join.
	const int RowCount = std::max(3, 2 + (int)vParticipants.size() + (Error.empty() ? 0 : 1));
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
}
#endif

void CRCVoiceChat::OnConsoleInit()
{
	Console()->Register("rc_voice_connect", "", CFGFLAG_CLIENT, ConConnect, this, "Connect to voice chat");
	Console()->Register("rc_voice_disconnect", "", CFGFLAG_CLIENT, ConDisconnect, this, "Disconnect from voice chat or cancel a queued connection");
	Console()->Register("+rc_voice_ptt", "", CFGFLAG_CLIENT, ConPtt, this, "Push to talk (hold)");
	Console()->Register("rc_voice_mute", "", CFGFLAG_CLIENT, ConMute, this, "Fully mute mic (PTT disabled too)");
	Console()->Register("rc_voice_unmute", "", CFGFLAG_CLIENT, ConUnmute, this, "Enable mic (transmit via PTT)");
	Console()->Register("rc_voice_toggle_mute", "", CFGFLAG_CLIENT, ConToggleMute, this, "Toggle microphone mute");
	Console()->Register("rc_voice_toggle_deafen", "", CFGFLAG_CLIENT, ConToggleDeafen, this, "Toggle all incoming voice without changing saved volume or mute settings");
	Console()->Register("rc_voice_status", "", CFGFLAG_CLIENT, ConStatus, this, "Voice chat status");
	Console()->Register("rc_voice_mute_name", "s[name]", CFGFLAG_CLIENT, ConMuteName, this, "Locally mute a player's voice");
	Console()->Register("rc_voice_unmute_name", "s[name]", CFGFLAG_CLIENT, ConUnmuteName, this, "Remove a local voice mute");
	Console()->Register("rc_voice_mute_list", "", CFGFLAG_CLIENT, ConMuteList, this, "List locally muted voices");
	Console()->Register("rc_voice_user", "s[name] i[volume] i[muted]", CFGFLAG_CLIENT, ConUserSettings, this, "Save a user's voice volume (0-200%) and mute state (0/1)");
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
	m_ConnectPending = false;
	// No worker may outlive the component or livekit::shutdown().
	RequestReset();
	if(m_Thread.joinable())
		m_Thread.join();
	if(m_ResetThread.joinable())
		m_ResetThread.join();
	else
		ReleaseSession();
	FinishReset();
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
	return m_Connecting || m_ConnectPending;
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

CRCVoiceChat::EPlayerVoiceState CRCVoiceChat::PlayerVoiceState(int ClientId) const
{
#ifdef CONF_LIVEKIT
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || Client()->State() != IClient::STATE_ONLINE ||
		!m_Connected || m_Reconnecting || m_ResetPending || m_Cancelled)
		return EPlayerVoiceState::DISCONNECTED;
	if(ClientId == LocalClientId())
		return Transmitting() ? EPlayerVoiceState::SPEAKING : EPlayerVoiceState::CONNECTED;
	bool Connected = false;
	bool Speaking = false;
	for(const auto &Participant : m_vParticipants)
	{
		if(ClientIdOf(Participant.m_Identity) != ClientId)
			continue;
		Connected = true;
		if(UserSettings(DisplayNameOf(Participant, ClientId)).m_Muted)
			return EPlayerVoiceState::MUTED;
		Speaking |= Participant.m_Speaking;
	}
	if(Connected)
		return Speaking ? EPlayerVoiceState::SPEAKING : EPlayerVoiceState::CONNECTED;
#endif
	return EPlayerVoiceState::DISCONNECTED;
}

void CRCVoiceChat::StartMicrophoneTest()
{
#ifdef CONF_LIVEKIT
	if(m_MicrophoneTesting || m_Connected || m_Connecting || m_ResetPending || m_ConnectPending || m_Thread.joinable())
	{
		SetLastError("disconnect voice and wait for cleanup before testing the microphone");
		return;
	}
	if(!m_AudioInitialized || !m_Initialized)
	{
		SetLastError("voice audio devices are unavailable");
		return;
	}
	SetLastError("");
	m_TestMicDevice = g_Config.m_RcVoiceMicDevice;
	m_TestPlayoutDevice = g_Config.m_RcVoicePlayoutDevice;
	m_MicGain = std::clamp(g_Config.m_RcVoiceMicVolume, 0, 300) / 100.0f;
	m_MicrophoneTestListen = false;
	m_MicrophoneTestLevel = 0.0f;
	m_MicrophoneTestLastVisible = Client()->GlobalTime();
	m_PttHeld = false;
	m_PttTail.Cancel();
	m_Cancelled = false;
	m_MicrophoneTesting = true;
	m_Connecting = true;
	try
	{
		m_Thread = std::thread(&CRCVoiceChat::MicrophoneTestThread, this, m_TestMicDevice, m_TestPlayoutDevice);
	}
	catch(const std::exception &Ex)
	{
		m_Connecting = false;
		m_MicrophoneTesting = false;
		SetLastError(Ex.what());
	}
#else
	SetLastError("microphone testing is unavailable in this build");
#endif
}

void CRCVoiceChat::StopMicrophoneTest()
{
#ifdef CONF_LIVEKIT
	if(m_MicrophoneTesting)
	{
		m_MicrophoneTestListen = false;
		m_MicrophoneTestLevel = 0.0f;
		ResetConnection();
	}
#endif
}

void CRCVoiceChat::KeepMicrophoneTestVisible()
{
#ifdef CONF_LIVEKIT
	m_MicrophoneTestLastVisible = Client()->GlobalTime();
#endif
}

bool CRCVoiceChat::MicrophoneTesting() const
{
#ifdef CONF_LIVEKIT
	return m_MicrophoneTesting;
#else
	return false;
#endif
}

bool CRCVoiceChat::MicrophoneTestListening() const
{
#ifdef CONF_LIVEKIT
	return m_MicrophoneTestListen;
#else
	return false;
#endif
}

void CRCVoiceChat::SetMicrophoneTestListening(bool Listen)
{
#ifdef CONF_LIVEKIT
	if(!m_MicrophoneTesting || m_ResetPending || m_Cancelled)
		return;
	if(m_MicrophoneTestListen.exchange(Listen) != Listen)
		++m_OutputGeneration;
#endif
}

float CRCVoiceChat::MicrophoneTestLevel() const
{
#ifdef CONF_LIVEKIT
	return m_MicrophoneTestLevel;
#else
	return 0.0f;
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
	{
		m_ConnectFailures = 0;
		m_ManualDisconnect = false;
	}
	if(!m_Initialized || !m_AudioInitialized)
	{
		SetLastError("voice chat is not initialized (LiveKit or audio devices unavailable)");
		return;
	}
	if(m_MicrophoneTesting)
	{
		StopMicrophoneTest();
		m_ConnectPending = true;
		m_NextPendingConnect = 0.0f;
		return;
	}
	if(m_Connecting || m_ResetPending)
	{
		// Repeated connect commands do not schedule a second live session.
		if(m_ResetPending)
		{
			m_ConnectPending = true;
			m_NextPendingConnect = 0.0f;
		}
		return;
	}
	if(m_Connected)
		return;
	if(g_Config.m_RcVoiceTokenUrl[0] == '\0')
	{
		++m_ConnectFailures;
		SetLastError("set rc_voice_token_url to the voice server endpoint first");
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

	const float Now = Client()->GlobalTime();
	if(m_LastTokenRequestTime > 0.0f && Now - m_LastTokenRequestTime < 5.0f)
	{
		m_ConnectPending = true;
		m_NextPendingConnect = m_LastTokenRequestTime + 5.0f;
		SetLastError("");
		dbg_msg("voicechat", "connection queued for %.1f seconds (rc_voice_disconnect cancels)", m_NextPendingConnect - Now);
		return;
	}

	SetLastError("");
	ResetConnection();
	if(m_ResetPending)
	{
		m_ConnectPending = true;
		m_NextPendingConnect = 0.0f;
		return;
	}
	m_ConnectPending = false;
	m_ManualDisconnect = false;
	m_MicGain = std::clamp(g_Config.m_RcVoiceMicVolume, 0, 300) / 100.0f;
	m_Cancelled = false;
	m_Connecting = true;
	try
	{
		const std::string RoomName = RoomNameFromAddress();
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
		m_MetadataRequestedClientId = LocalId;
		m_Thread = std::thread(&CRCVoiceChat::ConnectThread, this,
			LocalId,
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
	m_ConnectPending = false;
	SetLastError("");
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
	auto *pSelf = static_cast<CRCVoiceChat *>(pUserData);
	const bool Allowed = pSelf->Client()->State() == IClient::STATE_ONLINE &&
			     !pSelf->GameClient()->m_Menus.IsActive() && !pSelf->GameClient()->m_Chat.IsActive() && !pSelf->GameClient()->m_GameConsole.IsActive();
	if(!Allowed)
	{
		pSelf->OnRelease();
		return;
	}
	if(pResult->GetInteger(0) != 0)
	{
		pSelf->m_PttTail.Cancel();
		pSelf->m_PttHeld = true;
	}
	else
	{
		if(pSelf->m_PttHeld)
		{
			if(pSelf->Transmitting())
				pSelf->m_PttTail.Start(VoiceNowMs(), g_Config.m_RcVoicePttReleaseDelay);
			// Publish the tail before clearing held, so capture sees no gap.
			pSelf->m_PttHeld = false;
		}
	}
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

void CRCVoiceChat::ConToggleDeafen(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CRCVoiceChat *>(pUserData);
	pSelf->SetDeafened(!pSelf->Deafened());
	dbg_msg("voicechat", "incoming voice %s", pSelf->Deafened() ? "muted" : "enabled");
}

const char *CRCVoiceChat::DisplayNameOf(const SParticipant &Participant, int ClientId) const
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

std::string CRCVoiceChat::ParticipantName(const SParticipant &Participant) const
{
	return DisplayNameOf(Participant, ClientIdOf(Participant.m_Identity));
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
	if(pSelf->m_Connecting && !pSelf->m_ResetPending)
	{
		dbg_msg("voicechat", "session: connecting (audio and room setup in progress)");
		return;
	}
	if(pSelf->m_ResetPending)
	{
		dbg_msg("voicechat", "session: disconnecting  connect_queued=%d", (int)pSelf->m_ConnectPending);
		if(!Error.empty())
			dbg_msg("voicechat", "last_error: %s", Error.c_str());
		return;
	}
	const int LocalId = pSelf->LocalClientId();

	// Session
	const char *pState = "disconnected";
	if(pSelf->m_ConnectPending)
		pState = "connection queued";
	else if(pSelf->m_Connected)
		pState = pSelf->m_Reconnecting ? "reconnecting" : "connected";
	dbg_msg("voicechat", "session: %s  room='%s'  server_room='%s'", pState,
		pSelf->m_ConnectedRoom.empty() ? "-" : pSelf->m_ConnectedRoom.c_str(),
		pSelf->RoomNameFromAddress().empty() ? "-" : pSelf->RoomNameFromAddress().c_str());
	dbg_msg("voicechat", "ids: local_client_id=%d metadata_client_id=%d",
		LocalId, pSelf->m_MetadataClientId.load());
	dbg_msg("voicechat", "mode: token endpoint  autoconnect=%d  failures=%d  manual_disconnect=%d",
		g_Config.m_RcVoiceAutoConnect, pSelf->m_ConnectFailures.load(), (int)pSelf->m_ManualDisconnect);
	if(pSelf->m_ConnectPending)
		dbg_msg("voicechat", "queued connection: retry in %.1fs (rc_voice_disconnect cancels)",
			std::max(0.0f, pSelf->m_NextPendingConnect - pSelf->Client()->GlobalTime()));

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
	dbg_msg("voicechat", "mix: distance=%d stereo=%d listener=%s unpositioned=%d%% spectators=%d%% other_teams=%d%% super=%d%% volume=%d%%",
		g_Config.m_RcVoiceSpatial, g_Config.m_RcVoiceStereo, g_Config.m_RcVoiceListenerMode ? "camera" : "character",
		g_Config.m_RcVoiceUnpositionedVolume, g_Config.m_RcVoiceSpectatorVolume, g_Config.m_RcVoiceOtherTeamVolume,
		g_Config.m_RcVoiceSuperVolume, g_Config.m_RcVoiceVolume);

	// A leftover from a crashed session has no client id and no track.
	if(pSelf->m_Connected && pSelf->m_pRoom)
	{
		const vec2 Listener = pSelf->VoiceListenerPosition();
		std::lock_guard<std::mutex> Lock(pSelf->m_AudioActivityLock);
		dbg_msg("voicechat", "participants: %d (tracks: %d)", (int)pSelf->m_vParticipants.size(), (int)pSelf->m_MAudioActivity.size());
		for(const auto &P : pSelf->m_vParticipants)
		{
			const int ClientId = pSelf->ClientIdOf(P.m_Identity);
			const bool Active = ClientId >= 0 && ClientId < MAX_CLIENTS && pSelf->GameClient()->m_aClients[ClientId].m_Active;
			const bool HasPosition = Active && pSelf->GameClient()->m_Snap.m_aCharacters[ClientId].m_Active;
			const char *pReason = VoiceLogic::MuteReasonLabel((VoiceLogic::EMuteReason)P.m_MuteReason);
			char aDistance[24] = "-";
			if(HasPosition)
			{
				const float Distance = length(VoicePlayerPosition(pSelf->GameClient(), ClientId, pSelf->Client()->IntraGameTick(g_Config.m_ClDummy)) - Listener) / 32.0f;
				str_format(aDistance, sizeof(aDistance), "%.1ft", Distance);
			}
			int Tracks = 0;
			for(const auto &Entry : pSelf->m_MAudioActivity)
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
	log_trace("voicechat", "removed %d stale participant(s), %d left", Removed, (int)pSelf->Participants().size());
}

int CRCVoiceChat::PurgeStaleParticipants()
{
#ifdef CONF_LIVEKIT
	// A crashed client never sends its leave, LiveKit keeps it until timeout.
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	const auto Removed = std::erase_if(m_vParticipants, [&](const SParticipant &P) {
		if(ClientIdOf(P.m_Identity) >= 0)
			return false;
		return std::none_of(m_MAudioActivity.begin(), m_MAudioActivity.end(),
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

void CRCVoiceChat::ConUserSettings(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CRCVoiceChat *>(pUserData);
	if(!pSelf->SetUserSettings(pResult->GetString(0), pResult->GetInteger(1), pResult->GetInteger(2) != 0))
		pSelf->SetLastError("invalid voice user name");
}

CRCVoiceChat::SUserSettings CRCVoiceChat::UserSettings(const char *pName) const
{
	std::lock_guard<std::mutex> Lock(m_UserSettingsLock);
	return m_UserSettings.Get(pName);
}

std::vector<CRCVoiceChat::SUserSettings> CRCVoiceChat::SavedUsers() const
{
	std::lock_guard<std::mutex> Lock(m_UserSettingsLock);
	return m_UserSettings.All();
}

bool CRCVoiceChat::SetUserSettings(const char *pName, int Volume, bool Muted)
{
	std::lock_guard<std::mutex> Lock(m_UserSettingsLock);
	return m_UserSettings.Save(pName, Volume, Muted);
}

void CRCVoiceChat::ForgetUser(const char *pName)
{
	std::lock_guard<std::mutex> Lock(m_UserSettingsLock);
	m_UserSettings.Remove(pName);
}

void CRCVoiceChat::MuteName(const char *pName)
{
	std::lock_guard<std::mutex> Lock(m_UserSettingsLock);
	const auto Settings = m_UserSettings.Get(pName);
	m_UserSettings.Save(pName, Settings.m_Volume, true);
}

void CRCVoiceChat::UnmuteName(const char *pName)
{
	std::lock_guard<std::mutex> Lock(m_UserSettingsLock);
	const auto Settings = m_UserSettings.Get(pName);
	if(Settings.m_Muted)
		m_UserSettings.Save(pName, Settings.m_Volume, false);
}

bool CRCVoiceChat::IsNameMuted(const char *pName) const
{
	return UserSettings(pName).m_Muted;
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
	std::vector<std::string> Result;
	for(const auto &Settings : SavedUsers())
		if(Settings.m_Muted)
			Result.push_back(Settings.m_Name);
	return Result;
}

void CRCVoiceChat::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CRCVoiceChat *pThis = (CRCVoiceChat *)pUserData;
	for(const auto &Settings : pThis->SavedUsers())
	{
		char aEscaped[512];
		char *pDst = aEscaped;
		str_escape(&pDst, Settings.m_Name.c_str(), aEscaped + sizeof(aEscaped));
		char aLine[600];
		str_format(aLine, sizeof(aLine), "rc_voice_user \"%s\" %d %d", aEscaped, Settings.m_Volume, (int)Settings.m_Muted);
		pConfigManager->WriteLine(aLine, ConfigDomain::RCLIENT);
	}
}

void CRCVoiceChat::SetLastError(const char *pError)
{
	{
		std::lock_guard<std::mutex> Lock(m_ErrorLock);
		str_copy(m_aLastError, pError != nullptr ? pError : "", sizeof(m_aLastError));
		if(!pError || !pError[0])
			m_aAudioError[0] = '\0';
	}
	if(pError != nullptr && pError[0] != '\0')
		log_warn("voicechat", "%s", pError);
}

void CRCVoiceChat::SetAudioError(const char *pError)
{
	std::lock_guard<std::mutex> Lock(m_ErrorLock);
	str_copy(m_aAudioError, pError ? pError : "", sizeof(m_aAudioError));
}

std::string CRCVoiceChat::LastError() const
{
	std::lock_guard<std::mutex> Lock(m_ErrorLock);
	return m_aAudioError[0] ? m_aAudioError : m_aLastError;
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
	if(Muted)
		m_PttTail.Cancel();
	ApplyMicState();
#endif
}

bool CRCVoiceChat::Deafened() const
{
#ifdef CONF_LIVEKIT
	return m_Deafened;
#else
	return g_Config.m_RcVoiceDeafened != 0;
#endif
}

void CRCVoiceChat::SetDeafened(bool Deafened)
{
	g_Config.m_RcVoiceDeafened = Deafened ? 1 : 0;
#ifdef CONF_LIVEKIT
	if(m_Deafened.exchange(Deafened) == Deafened)
		return;
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	for(auto &[Id, pAudio] : m_MAudioActivity)
		pAudio->Clear();
	++m_OutputGeneration;
#endif
}

bool CRCVoiceChat::Transmitting() const
{
#ifdef CONF_LIVEKIT
	return !m_MicrophoneTesting && m_Connected && !m_Reconnecting && m_MicReady && !m_MicFailed && m_Transmitting && PttActive() && !m_FullMuted && !m_Cancelled;
#else
	return false;
#endif
}

#ifdef CONF_LIVEKIT

void CRCVoiceChat::ConnectThread(int LocalClientId,
	const std::string &MicDevice, const std::string &PlayoutDevice, const std::shared_ptr<IHttpRequest> &pTokenRequest, const std::string &RoomName)
{
	try
	{
		if(!pTokenRequest)
			throw std::runtime_error("voice token request is unavailable");
		pTokenRequest->Wait();
		if(m_Cancelled)
		{
			ConnectThreadFinish();
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
		if(!pUrl || !pToken || pUrl->type != json_string || pToken->type != json_string ||
			pUrl->u.string.length == 0 || pUrl->u.string.length > 255 ||
			pToken->u.string.length == 0 || pToken->u.string.length > 8192)
			throw std::runtime_error("invalid token endpoint response: expected server_url and participant_token strings");
		const std::string Url(pUrl->u.string.ptr, pUrl->u.string.length);
		const std::string Token(pToken->u.string.ptr, pToken->u.string.length);
		if((!str_startswith(Url.c_str(), "wss://") && !str_startswith(Url.c_str(), "ws://")) ||
			Url.find_first_of(std::string("\0\r\n", 3)) != std::string::npos || Token.find_first_of(std::string("\0\r\n", 3)) != std::string::npos)
			throw std::runtime_error("invalid token endpoint credentials");
		if(m_Cancelled)
		{
			ConnectThreadFinish();
			return;
		}
		// Expired tokens crash Room::connect() inside the SDK.
		{
			const long long Now = (long long)std::time(nullptr);
			if(VoiceLogic::TokenUnusable(Token, Now))
			{
				const long long Expiry = VoiceLogic::TokenExpiry(Token);
				char aError[192];
				if(Expiry > 0)
					str_copy(aError, "token endpoint returned an expired or expiring token; reconnect and check server time");
				else
					str_copy(aError, "token endpoint returned an unusable JWT; check the voice server");
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
		if(m_pRoom->roomInfo().name != RoomName)
			throw std::runtime_error("token endpoint returned a token for a different room");
		if(m_Cancelled)
		{
			ConnectThreadFinish();
			return;
		}
		m_Connected = true;
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

		// After connect: Room::connect() owns the FFI client while it runs.
		StartAudioProcessing();
		try
		{
			auto pLocal = m_pRoom->localParticipant().lock();
			if(!pLocal)
				throw std::runtime_error("local participant is unavailable");
			try
			{
				// Do not read Participant::metadata() here: SDK room events may
				// update the referenced string concurrently with this worker.
				pLocal->setMetadata(std::to_string(LocalClientId));
				m_MetadataClientId = LocalClientId;
			}
			catch(const std::exception &Ex)
			{
				m_MetadataClientId = -1;
				const std::string Error = std::string("could not update voice client id: ") + Ex.what();
				SetLastError(Error.c_str());
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
	ConnectThreadFinish();
}

bool CRCVoiceChat::PttActive() const
{
	return m_PttHeld || m_PttTail.Active(VoiceNowMs());
}

void CRCVoiceChat::ApplyMicState()
{
	// Cleanup owns the track until FinishReset() reaps its worker.
	if(m_ResetPending)
		return;
	std::lock_guard<std::mutex> Lock(m_MicLock);
	if(!m_pMicTrack)
		return;
	const bool Transmit = m_MicReady && !m_MicFailed && m_Connected && !m_Cancelled && !m_Reconnecting && PttActive() && !m_FullMuted;
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

void CRCVoiceChat::RequestReset()
{
	m_Cancelled = true;
	m_PttHeld = false;
	m_PttTail.Cancel();
	m_Connected = false;
	if(m_pTokenRequest)
		m_pTokenRequest->Abort();
	m_ResetPending = true;
}

void CRCVoiceChat::FinishReset()
{
	m_pTokenRequest.reset();
	if(m_ResetThread.joinable())
		m_ResetThread.join();
	m_Connected = false;
	m_Reconnecting = false;
	m_ReconnectingSince = 0.0f;
	m_ConnectedRoom.clear();
	m_MetadataClientId = -1;
	m_MetadataRequestedClientId = -1;
	m_MetadataRetry.Reset();
	m_StatsPingMs = -1;
	m_NextStatsRequest = 0.0f;
	m_LastPingUpdate = -1.0f;
	m_PingMs = -1;
	m_MicrophoneTesting = false;
	m_MicrophoneTestListen = false;
	m_MicrophoneTestLevel = 0.0f;
	m_MicrophoneTestLastVisible = -1.0f;
	SetAudioError("");
	m_vParticipants.clear();
	m_ActiveSpeakerIdentities.clear();
	m_MClientIds.clear();
	{
		std::lock_guard<std::mutex> Lock(m_EventLock);
		m_vEvents.clear();
		m_LocalIdentity.clear();
	}
	m_ResetPending = false;
}

void CRCVoiceChat::ReleaseSession()
{
	// Setup has finished; stop audio and await SDK workers before destroying
	// their room. This cleanup runs off the game thread during play.
	StopAudio();
	if(m_StatsThread.joinable())
		m_StatsThread.join();
	m_StatsBusy = false;
	if(m_MetadataThread.joinable())
		m_MetadataThread.join();
	m_MetadataBusy = false;
	{
		std::lock_guard<std::mutex> Lock(m_MicLock);
		if(m_pMicTrack)
		{
			try
			{
				m_pMicTrack->mute();
			}
			catch(const std::exception &Ex)
			{
				log_debug("voicechat", "could not mute microphone during cleanup: %s", Ex.what());
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
		m_MAudioActivity.clear();
	}
	if(m_EngineInitialized)
		ma_engine_uninit(&m_AudioEngine);
	m_EngineInitialized = false;
	m_Delegate.Reset();
	m_LimiterGain = 1.0f;
	m_ResetBusy = false;
}

void CRCVoiceChat::ConnectThreadFinish()
{
	// Publish completion only. Cleanup touches game-thread participant state;
	// OnUpdate/ResetConnection/OnShutdown joins us before releasing the SDK.
	m_Connecting = false;
}

void CRCVoiceChat::ResetConnection()
{
	RequestReset();
	// SDK calls, device shutdown and metadata may block. Only join workers
	// that have published completion; OnUpdate polls the rest.
	if(m_Connecting || m_ResetBusy)
		return;
	if(m_Thread.joinable())
		m_Thread.join();
	if(m_ResetThread.joinable())
	{
		FinishReset();
		return;
	}
	if(!m_pRoom && !m_EngineInitialized && !m_BuffersInitialized && !m_AudioThread.joinable() && !m_MetadataThread.joinable() && !m_StatsThread.joinable())
	{
		FinishReset();
		return;
	}
	m_StopAudio = true;
	m_ResetBusy = true;
	try
	{
		m_ResetThread = std::thread(&CRCVoiceChat::ReleaseSession, this);
	}
	catch(const std::exception &Ex)
	{
		m_ResetBusy = false;
		SetLastError(Ex.what());
	}
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

	// Reap the finished worker. Never wait for a running one: setMetadata is a
	// blocking FFI request and the swap would freeze the frame.
	if(m_MetadataThread.joinable())
	{
		if(m_MetadataBusy)
			return;
		m_MetadataThread.join();
		if(m_MetadataClientId == m_MetadataRequestedClientId)
			m_MetadataRetry.Reset();
		else
			m_MetadataRetry.OnFailure(m_MetadataRequestedClientId, Client()->GlobalTime());
	}

	const int LocalId = LocalClientId();
	if(m_Reconnecting || LocalId < 0 || LocalId == m_MetadataClientId ||
		!m_MetadataRetry.CanAttempt(LocalId, Client()->GlobalTime()))
		return;

	// Successful swaps remain immediate. Only retrying a failed ID is delayed.
	m_MetadataRequestedClientId = LocalId;
	m_MetadataBusy = true;
	try
	{
		m_MetadataThread = std::thread(&CRCVoiceChat::MetadataThread, this, LocalId);
	}
	catch(const std::exception &Ex)
	{
		m_MetadataBusy = false;
		m_MetadataRetry.OnFailure(LocalId, Client()->GlobalTime());
		const std::string Error = std::string("could not start voice metadata update: ") + Ex.what();
		SetLastError(Error.c_str());
	}
}

void CRCVoiceChat::MetadataThread(int ClientId)
{
	try
	{
		auto pLocal = m_pRoom->localParticipant().lock();
		if(!pLocal)
			throw std::runtime_error("local voice participant is unavailable");
		pLocal->setMetadata(std::to_string(ClientId));
		m_MetadataClientId = ClientId;
		log_trace("voicechat", "voice metadata set to client id %d", ClientId);
	}
	catch(const std::exception &Ex)
	{
		m_MetadataClientId = -1;
		const std::string Error = std::string("could not update voice client id: ") + Ex.what();
		SetLastError(Error.c_str());
	}
	m_MetadataBusy = false;
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
			m_MClientIds[Ev.m_Identity] = VoiceLogic::ParseClientId(Ev.m_Metadata);
			break;
		}
		case EEventType::ParticipantLeft:
			m_vParticipants.erase(std::remove_if(m_vParticipants.begin(), m_vParticipants.end(),
						      [&](const SParticipant &P) { return P.m_Identity == Ev.m_Identity; }),
				m_vParticipants.end());
			m_MClientIds.erase(Ev.m_Identity);
			m_ActiveSpeakerIdentities.erase(Ev.m_Identity);
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
			m_MClientIds.clear();
			m_ActiveSpeakerIdentities.clear();
			break;
		case EEventType::ActiveSpeakers:
			m_ActiveSpeakerIdentities = std::unordered_set<std::string>(Ev.m_vIdentities.begin(), Ev.m_vIdentities.end());
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
		m_MAudioActivity[TrackId] = std::move(pAudio);
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
	auto It = m_MAudioActivity.find(TrackId);
	if(It == m_MAudioActivity.end() || !m_Connected || m_Cancelled || m_Reconnecting || m_StopAudio)
		return;
	auto &Audio = *It->second;
	const auto Now = std::chrono::steady_clock::now();
	// Detect activity before local mute/volume filters discard the samples.
	// Room active-speaker events can arrive noticeably later than the audio.
	if(!Audio.m_Muted && std::any_of(Frame.data().begin(), Frame.data().end(), [](std::int16_t Sample) { return Sample != 0; }))
	{
		if(Now >= Audio.m_SoundUntil)
			Audio.m_SoundStartedAt = Now;
		Audio.m_SoundUntil = Now + std::chrono::milliseconds(300);
	}
	if(Audio.m_Muted || Audio.m_TargetGains == vec2(0.0f, 0.0f) || m_Deafened)
		return;
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
		pInput += InputFrames * Channels;
		Remaining -= InputFrames;
	}
}

void CRCVoiceChat::SetAudioMuted(const std::string &TrackId, bool Muted)
{
	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	auto It = m_MAudioActivity.find(TrackId);
	if(It != m_MAudioActivity.end())
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
	std::erase_if(m_MAudioActivity, [&](const auto &Entry) { return Entry.second->m_Identity == Identity; });
	++m_OutputGeneration;
}

void CRCVoiceChat::UpdateTalking()
{
	m_MicGain = std::clamp(g_Config.m_RcVoiceMicVolume, 0, 300) / 100.0f;
	const auto Now = std::chrono::steady_clock::now();
	const bool Receiving = m_Connected && !m_Reconnecting && !m_Cancelled;
	const vec2 Listener = VoiceListenerPosition();
	const int LocalId = LocalClientId();
	const int MyTeam = LocalId >= 0 && LocalId < MAX_CLIENTS ? GameClient()->m_Teams.Team(LocalId) : -1;

	std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
	for(auto &[TrackId, pAudio] : m_MAudioActivity)
	{
		auto &Audio = *pAudio;
		const int Id = ClientIdOf(Audio.m_Identity);
		const bool Active = Id >= 0 && Id < MAX_CLIENTS && GameClient()->m_aClients[Id].m_Active;
		// Server stops sending a character outside the network clip radius.
		const bool HasPosition = Active && GameClient()->m_Snap.m_aCharacters[Id].m_Active;
		const vec2 Delta = HasPosition ? VoicePlayerPosition(GameClient(), Id, Client()->IntraGameTick(g_Config.m_ClDummy)) - Listener : vec2(0.0f, 0.0f);

		// Cached name, so mutes work before the client id is known.
		if(Active)
			Audio.m_Name = GameClient()->m_aClients[Id].m_aName;
		else
		{
			const auto It = std::find_if(m_vParticipants.begin(), m_vParticipants.end(),
				[&](const SParticipant &P) { return P.m_Identity == Audio.m_Identity; });
			Audio.m_Name = It != m_vParticipants.end() ? ParticipantName(*It) : std::string();
		}

		// Spectators without a character have their own non-spatial volume.
		const bool IsSpectator = Active && GameClient()->m_aClients[Id].m_Team == TEAM_SPECTATORS;
		VoiceLogic::SParticipantMix Mix;
		Mix.m_SessionActive = Receiving;
		Mix.m_Deafened = m_Deafened;
		Mix.m_TrackMuted = Audio.m_Muted;
		const auto Settings = UserSettings(Audio.m_Name.c_str());
		Mix.m_NameMuted = Settings.m_Muted;
		Mix.m_UserVolume = Settings.m_Volume;
		// -1 means unknown and must not mute everyone.
		Mix.m_IsLocal = Id >= 0 && Id == LocalId;
		Mix.m_HasPosition = HasPosition;
		Mix.m_IsSpectator = IsSpectator;
		Mix.m_MyTeam = MyTeam;
		Mix.m_TheirTeam = Active ? GameClient()->m_Teams.Team(Id) : -1;
		Mix.m_SuperTeam = GameClient()->m_Teams.TeamSuper();
		Mix.m_OtherTeamVolume = g_Config.m_RcVoiceOtherTeamVolume;
		Mix.m_SuperVolume = g_Config.m_RcVoiceSuperVolume;
		Mix.m_UnpositionedVolume = g_Config.m_RcVoiceUnpositionedVolume;
		Mix.m_SpectatorVolume = g_Config.m_RcVoiceSpectatorVolume;
		Mix.m_MasterVolume = g_Config.m_RcVoiceVolume;

		const VoiceLogic::EMuteReason Reason = VoiceLogic::MuteReasonOf(Mix);
		const bool Mute = Reason != VoiceLogic::MUTE_NONE;
		Audio.m_MuteReason = Reason;
		const vec2 Gains = Mute ? vec2(0.0f, 0.0f) : VoiceLogic::VoiceGains(HasPosition, g_Config.m_RcVoiceSpatial, VoiceLogic::ParticipantVolume(Mix), Delta, g_Config.m_RcVoiceMinVolume, g_Config.m_RcVoiceMinRadius, g_Config.m_RcVoiceRadius, g_Config.m_RcVoiceVolume, g_Config.m_RcVoiceStereoWidth, g_Config.m_RcVoiceStereoRadius, VoiceLogic::TeamVolume(Mix), Mix.m_UserVolume, g_Config.m_RcVoiceStereo);
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
		const bool WasSpeaking = P.m_Speaking;
		auto StartedAt = Now;
		P.m_Talking = false;
		P.m_Speaking = false;
		P.m_MuteReason = VoiceLogic::MUTE_NONE;
		bool HasTrack = false;
		bool AnyAudible = false;
		for(const auto &Entry : m_MAudioActivity)
		{
			const auto &Activity = *Entry.second;
			if(Activity.m_Identity != P.m_Identity)
				continue;
			HasTrack = true;
			P.m_LastSoundUntil = std::max(P.m_LastSoundUntil, Activity.m_SoundUntil);
			if(Now < Activity.m_SoundUntil)
			{
				P.m_Speaking = true;
				StartedAt = std::min(StartedAt, Activity.m_SoundStartedAt);
			}
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
		P.m_Speaking = P.m_Speaking && Receiving;
		if(P.m_Speaking && !WasSpeaking)
			P.m_SpeakingStartedAt = StartedAt;
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
			return VoicePlayerPosition(pGame, Id, Client()->IntraGameTick(g_Config.m_ClDummy));
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
	auto It = m_MClientIds.find(Identity);
	return It != m_MClientIds.end() ? It->second : -1;
#else
	return -1;
#endif
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
