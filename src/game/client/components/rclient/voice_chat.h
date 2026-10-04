#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_CHAT_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_CHAT_H

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <base/dbg.h>
#include <engine/console.h>
#include <game/client/component.h>
#ifdef CONF_LIVEKIT
#include <livekit/livekit.h>
#endif

class CRCVoiceChat : public CComponent
{
public:
	struct SParticipant
	{
		std::string m_Identity;
		std::string m_Name;
		bool m_Talking = false;
	};

	struct SDevice
	{
		std::string m_Name;
		std::string m_Id;
	};

	std::vector<SDevice> MicDevices();
	std::vector<SDevice> PlayoutDevices();

private:
#ifdef CONF_LIVEKIT

	enum class EEventType
	{
		ParticipantJoined,
		ParticipantLeft,
		ActiveSpeakers,
		Snapshot,
	};

	struct SEvent
	{
		EEventType m_Type;
		std::string m_Identity;
		std::string m_Name;
		std::vector<std::string> m_vIdentities;
		std::vector<std::string> m_vNames;
	};

	void PushEvent(SEvent Event);
	void ProcessEvents();

	std::mutex m_EventLock;
	std::vector<SEvent> m_vEvents;
	std::vector<SParticipant> m_vParticipants;

	class CDelegate : public livekit::RoomDelegate
	{
		CRCVoiceChat *m_pVoice;

	public:
		explicit CDelegate(CRCVoiceChat *pVoice) :
			m_pVoice(pVoice) {}

		void onConnectionStateChanged(livekit::Room &Room, const livekit::ConnectionStateChangedEvent &Event) override
		{
			const char *pState = Event.state == livekit::ConnectionState::Connected ? "connected" :
						  Event.state == livekit::ConnectionState::Reconnecting ? "reconnecting" : "disconnected";
			dbg_msg("voicechat", "state: %s", pState);
		}

		void onDisconnected(livekit::Room &Room, const livekit::DisconnectedEvent &Event) override
		{
			m_pVoice->m_Connected = false;
			dbg_msg("voicechat", "disconnected (reason %d)", (int)Event.reason);
		}

		void onParticipantConnected(livekit::Room &Room, const livekit::ParticipantConnectedEvent &Event) override
		{
			SEvent Ev;
			Ev.m_Type = EEventType::ParticipantJoined;
			Ev.m_Identity = Event.participant->identity();
			Ev.m_Name = Event.participant->name();
			m_pVoice->PushEvent(std::move(Ev));
		}

		void onParticipantDisconnected(livekit::Room &Room, const livekit::ParticipantDisconnectedEvent &Event) override
		{
			SEvent Ev;
			Ev.m_Type = EEventType::ParticipantLeft;
			Ev.m_Identity = Event.participant->identity();
			m_pVoice->PushEvent(std::move(Ev));
		}

		void onActiveSpeakersChanged(livekit::Room &Room, const livekit::ActiveSpeakersChangedEvent &Event) override
		{
			dbg_msg("voicechat", "active speakers: %d", (int)Event.speakers.size());
			SEvent Ev;
			Ev.m_Type = EEventType::ActiveSpeakers;
			for(auto pSpeaker : Event.speakers)
				Ev.m_vIdentities.push_back(pSpeaker->identity());
			m_pVoice->PushEvent(std::move(Ev));
		}

		void onTrackSubscribed(livekit::Room &Room, const livekit::TrackSubscribedEvent &Event) override
		{
			if(Event.track->kind() != livekit::TrackKind::KIND_AUDIO)
				return;
			const std::string Identity = Event.participant->identity();
			CRCVoiceChat *pVoice = m_pVoice;
			Room.setOnAudioFrameCallback(Identity, Event.track->name(),
				[pVoice, Identity](const livekit::AudioFrame &Frame) {
					pVoice->UpdateSpeakingLevel(Identity, Frame);
				});
			dbg_msg("voicechat", "listening to '%s' for level", Identity.c_str());
		}

		void onTrackUnsubscribed(livekit::Room &Room, const livekit::TrackUnsubscribedEvent &Event) override
		{
			Room.clearOnAudioFrameCallback(Event.participant->identity(), Event.track->name());
		}
	};

	void ConnectThread(std::string Url, std::string Token, int LocalClientId);

	std::unique_ptr<livekit::Room> m_pRoom;
	CDelegate m_Delegate;
	std::thread m_Thread;
	std::atomic<bool> m_Connecting{false};
	std::atomic<bool> m_Connected{false};
	std::unique_ptr<livekit::PlatformAudio> m_pPlatformAudio;
	std::shared_ptr<livekit::PlatformAudioSource> m_pMicSource;
	std::shared_ptr<livekit::LocalAudioTrack> m_pMicTrack;

	static void ConConnect(IConsole::IResult *pResult, void *pUserData);
	static void ConDisconnect(IConsole::IResult *pResult, void *pUserData);

	std::atomic<bool> m_FullMuted{false};
	std::atomic<bool> m_PttHeld{false};

	void UpdateSpeakingLevel(const std::string &Identity, const livekit::AudioFrame &Frame);

	std::mutex m_LevelLock;
	std::unordered_map<std::string, float> m_mLevels;
	float m_LastLevelTick = 0.0f;
	std::unordered_map<std::string, int> m_mClientIds;
	float m_NextClientIdRefresh = 0.0f;

	void ApplyMicState();

	std::string RoomNameFromAddress() const;

	bool m_ManualDisconnect = false;
	float m_NextAutoConnect = 0.0f;

	static void ConPtt(IConsole::IResult *pResult, void *pUserData);
	static void ConMute(IConsole::IResult *pResult, void *pUserData);
	static void ConUnmute(IConsole::IResult *pResult, void *pUserData);
	static void ConStatus(IConsole::IResult *pResult, void *pUserData);
#endif

public:
	CRCVoiceChat();
	~CRCVoiceChat() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnShutdown() override;

	bool Connected() const;
	bool Connecting() const;
	std::vector<SParticipant> Participants() const;
	void Connect();
	void Disconnect();

	bool Muted() const;
	bool Transmitting() const;
	int ClientIdOf(const std::string &Identity) const;
};

#endif // GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_CHAT_H
