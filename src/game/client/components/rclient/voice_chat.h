#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_CHAT_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_CHAT_H

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
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
	// Commands also exist in builds without LiveKit.
	static void ConConnect(IConsole::IResult *pResult, void *pUserData);
	static void ConDisconnect(IConsole::IResult *pResult, void *pUserData);
	static void ConPtt(IConsole::IResult *pResult, void *pUserData);
	static void ConMute(IConsole::IResult *pResult, void *pUserData);
	static void ConUnmute(IConsole::IResult *pResult, void *pUserData);
	static void ConStatus(IConsole::IResult *pResult, void *pUserData);

#ifdef CONF_LIVEKIT
	enum class EEventType
	{
		ParticipantJoined,
		ParticipantLeft,
		Snapshot,
		Disconnected,
	};

	struct SEvent
	{
		EEventType m_Type;
		std::string m_Identity;
		std::string m_Name;
		std::vector<std::string> m_vIdentities;
		std::string m_Metadata;
	};

	void PushEvent(SEvent Event);
	void ProcessEvents();
	void ResetConnection();
	static int ParseClientId(const std::string &Metadata);

	std::mutex m_EventLock;
	std::vector<SEvent> m_vEvents;
	std::vector<SParticipant> m_vParticipants;
	std::unordered_map<std::string, int> m_mClientIds;
	struct SAudioActivity
	{
		std::string m_Identity;
		bool m_Muted = false;
		std::chrono::steady_clock::time_point m_SoundUntil{};
	};
	std::mutex m_AudioActivityLock;
	std::unordered_map<std::string, SAudioActivity> m_mAudioActivity; // Keyed by track SID.
	void RecordAudioActivity(const std::string &TrackId, const livekit::AudioFrame &Frame);
	void SetAudioMuted(const std::string &TrackId, bool Muted);
	void ClearAudioActivity(const std::string &Identity);
	void UpdateTalking();

	class CDelegate : public livekit::RoomDelegate
	{
		CRCVoiceChat *m_pVoice;
		bool m_SnapshotSent = false;

		void CaptureParticipants(livekit::Room &Room)
		{
			if(m_SnapshotSent)
				return;
			m_SnapshotSent = true;
			// Copy mutable name/metadata on the SDK event thread, not the game thread.
			for(const auto &Weak : Room.remoteParticipants())
				if(auto pParticipant = Weak.lock())
					PushParticipant(*pParticipant);
		}

		void PushParticipant(const livekit::Participant &Participant)
		{
			SEvent Ev{EEventType::ParticipantJoined};
			Ev.m_Identity = Participant.identity();
			Ev.m_Name = Participant.name();
			Ev.m_Metadata = Participant.metadata();
			m_pVoice->PushEvent(std::move(Ev));
		}

	public:
		explicit CDelegate(CRCVoiceChat *pVoice) :
			m_pVoice(pVoice) {}

		void Reset() { m_SnapshotSent = false; }

		void onConnectionStateChanged(livekit::Room &Room, const livekit::ConnectionStateChangedEvent &Event) override
		{
			m_pVoice->m_Reconnecting = Event.state == livekit::ConnectionState::Reconnecting;
			if(Event.state == livekit::ConnectionState::Connected)
				CaptureParticipants(Room);
		}

		void onDisconnected(livekit::Room &Room, const livekit::DisconnectedEvent &Event) override
		{
			m_pVoice->m_Cancelled = true;
			m_pVoice->m_Connected = false;
			m_pVoice->m_Reconnecting = false;
			m_pVoice->m_PttHeld = false;
			m_pVoice->PushEvent({EEventType::Disconnected});
			dbg_msg("voicechat", "disconnected (reason %d)", (int)Event.reason);
		}

		void onParticipantConnected(livekit::Room &Room, const livekit::ParticipantConnectedEvent &Event) override
		{
			CaptureParticipants(Room);
			if(Event.participant)
				PushParticipant(*Event.participant);
		}

		void onParticipantDisconnected(livekit::Room &Room, const livekit::ParticipantDisconnectedEvent &Event) override
		{
			CaptureParticipants(Room);
			if(Event.participant)
			{
				m_pVoice->ClearAudioActivity(Event.participant->identity());
				m_pVoice->PushEvent({EEventType::ParticipantLeft, Event.participant->identity()});
			}
		}

		void onParticipantNameChanged(livekit::Room &Room, const livekit::ParticipantNameChangedEvent &Event) override
		{
			CaptureParticipants(Room);
			auto pLocal = Room.localParticipant().lock();
			if(Event.participant && (!pLocal || Event.participant != pLocal.get()))
				PushParticipant(*Event.participant);
		}

		void onParticipantMetadataChanged(livekit::Room &Room, const livekit::ParticipantMetadataChangedEvent &Event) override
		{
			CaptureParticipants(Room);
			auto pLocal = Room.localParticipant().lock();
			if(Event.participant && (!pLocal || Event.participant != pLocal.get()))
				PushParticipant(*Event.participant);
		}

		void onTrackSubscribed(livekit::Room &Room, const livekit::TrackSubscribedEvent &Event) override
		{
			CaptureParticipants(Room);
			if(Event.participant)
				PushParticipant(*Event.participant);
			if(!Event.participant || !Event.track || Event.track->kind() != livekit::TrackKind::KIND_AUDIO)
				return;
			const std::string Identity = Event.participant->identity();
			const std::string TrackId = Event.track->sid();
			const std::string TrackName = Event.publication ? Event.publication->name() : Event.track->name();
			{
				std::lock_guard<std::mutex> Lock(m_pVoice->m_AudioActivityLock);
				m_pVoice->m_mAudioActivity[TrackId] = {Identity, Event.publication ? Event.publication->muted() : Event.track->muted(), {}};
			}
			Room.setOnAudioFrameCallback(Identity, TrackName,
				[pVoice = m_pVoice, TrackId](const livekit::AudioFrame &Frame) {
					pVoice->RecordAudioActivity(TrackId, Frame);
				});
		}

		void onTrackUnsubscribed(livekit::Room &Room, const livekit::TrackUnsubscribedEvent &Event) override
		{
			if(!Event.participant || !Event.track || Event.track->kind() != livekit::TrackKind::KIND_AUDIO)
				return;
			const std::string TrackName = Event.publication ? Event.publication->name() : Event.track->name();
			Room.clearOnAudioFrameCallback(Event.participant->identity(), TrackName);
			std::lock_guard<std::mutex> Lock(m_pVoice->m_AudioActivityLock);
			m_pVoice->m_mAudioActivity.erase(Event.track->sid());
		}

		void onTrackMuted(livekit::Room &Room, const livekit::TrackMutedEvent &Event) override
		{
			if(Event.publication && Event.publication->kind() == livekit::TrackKind::KIND_AUDIO)
				m_pVoice->SetAudioMuted(Event.publication->sid(), true);
		}

		void onTrackUnmuted(livekit::Room &Room, const livekit::TrackUnmutedEvent &Event) override
		{
			if(Event.publication && Event.publication->kind() == livekit::TrackKind::KIND_AUDIO)
				m_pVoice->SetAudioMuted(Event.publication->sid(), false);
		}

		void onLocalTrackPublished(livekit::Room &Room, const livekit::LocalTrackPublishedEvent &Event) override
		{
			CaptureParticipants(Room);
		}
	};

	void ConnectThread(std::string Url, std::string Token, int LocalClientId,
		std::string MicDevice, std::string PlayoutDevice);

	// ResetConnection joins the setup thread before replacing SDK objects.
	std::unique_ptr<livekit::Room> m_pRoom;
	CDelegate m_Delegate;
	std::thread m_Thread;
	std::atomic<bool> m_Connecting{false};
	std::atomic<bool> m_Connected{false};
	std::atomic<bool> m_Reconnecting{false};
	std::atomic<bool> m_Cancelled{false};
	std::unique_ptr<livekit::PlatformAudio> m_pPlatformAudio;
	std::shared_ptr<livekit::PlatformAudioSource> m_pMicSource;
	std::mutex m_MicLock; // Protects the track and serializes mute/unmute.
	std::shared_ptr<livekit::LocalAudioTrack> m_pMicTrack;
	std::atomic<bool> m_MicReady{false};
	std::atomic<bool> m_Transmitting{false};
	std::atomic<bool> m_FullMuted{false};
	std::atomic<bool> m_PttHeld{false};
	bool m_Initialized = false;

	void ApplyMicState();
	std::string RoomNameFromAddress() const;
	bool m_ManualDisconnect = false;
	float m_NextAutoConnect = 0.0f;
#endif

public:
	CRCVoiceChat();
	~CRCVoiceChat() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRelease() override;
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
