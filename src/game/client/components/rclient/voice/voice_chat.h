#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_VOICE_CHAT_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_VOICE_CHAT_H

#include "voice_chat_logic.h"

#include <base/dbg.h>

#include <engine/config.h>
#include <engine/console.h>

#include <game/client/component.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef CONF_LIVEKIT
#include <engine/http.h>

#include <future>

// Before miniaudio.h: these change the layout of ma_engine, a member of
// CRCVoiceChat, so every file including this header sees the same set.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_GENERATION
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_WAV

#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_ALSA
#define MA_ENABLE_AUDIO4
#define MA_ENABLE_COREAUDIO
#define MA_ENABLE_DSOUND
#define MA_ENABLE_JACK
#define MA_ENABLE_NULL
#define MA_ENABLE_OSS
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_SNDIO
#define MA_ENABLE_WASAPI
#define MA_ENABLE_WINMM

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wduplicated-branches"
#endif
#include "../external/miniaudio.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <livekit/livekit.h>
#endif

class CRCVoiceChat : public CComponent
{
public:
	enum class EPlayerVoiceState
	{
		DISCONNECTED,
		CONNECTED,
		MUTED,
		SPEAKING,
	};

	struct SParticipant
	{
		std::string m_Identity;
		std::string m_Name;
		bool m_Talking = false;
		int m_MuteReason = 0; // VoiceLogic::EMuteReason, MUTE_NONE means audible
		bool m_Speaking = false; // Incoming audio activity, including locally muted voices.
		std::chrono::steady_clock::time_point m_SpeakingStartedAt;
		std::chrono::steady_clock::time_point m_LastSoundUntil;
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
	static void ConToggleMute(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleDeafen(IConsole::IResult *pResult, void *pUserData);
	static void ConMuteName(IConsole::IResult *pResult, void *pUserData);
	static void ConUnmuteName(IConsole::IResult *pResult, void *pUserData);
	static void ConMuteList(IConsole::IResult *pResult, void *pUserData);
	static void ConUserSettings(IConsole::IResult *pResult, void *pUserData);
	static void ConPurge(IConsole::IResult *pResult, void *pUserData);

	// Drops participants without client id and without an audio track.
	int PurgeStaleParticipants();

	// In-game name, falls back to the token name.
	const char *DisplayNameOf(const SParticipant &Participant, int ClientId) const;
	bool IsLocalIdentity(const std::string &Identity);

	// Per-nick local settings, saved in the RClient config domain.
	void MuteName(const char *pName);
	void UnmuteName(const char *pName);
	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	mutable std::mutex m_UserSettingsLock;
	VoiceLogic::CUserSettings m_UserSettings;

	void SetLastError(const char *pError);
	void ConnectInternal(bool Manual);
	mutable std::mutex m_ErrorLock;
	char m_aLastError[256] = "";
	char m_aAudioError[256] = "";
	void SetAudioError(const char *pError);

#ifdef CONF_LIVEKIT
	enum class EEventType
	{
		ParticipantJoined,
		ParticipantLeft,
		Snapshot,
		Disconnected,
		ActiveSpeakers,
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
	void RenderDebugHud();
	void RenderStatusHud();
	void RenderParticipantsOverlay();
	std::unordered_set<std::string> m_ActiveSpeakerIdentities; // Game thread only.
	void UpdatePing();
	void PingThread();
	// Cancels the session without waiting for the setup thread.
	void RequestReset();
	// Reaps completed cleanup and clears game-thread participant state.
	void FinishReset();
	// Stops audio and releases SDK objects off the game thread.
	void ReleaseSession();
	// Tail of ConnectThread: publishes completion for game-thread cleanup.
	void ConnectThreadFinish();
	void ResetConnection();
	int LocalClientId() const;
	void UpdateMetadata();
	void MetadataThread(int ClientId);

	static constexpr int MAX_CONNECT_FAILURES = 3;

	std::mutex m_EventLock;
	std::vector<SEvent> m_vEvents;
	std::vector<SParticipant> m_vParticipants;
	std::string m_LocalIdentity; // filters our own leftover session
	std::unordered_map<std::string, int> m_MClientIds;
	struct SAudioActivity
	{
		struct SSource
		{
			ma_data_source_base m_Base{};
			SAudioActivity *m_pAudio = nullptr;
		} m_Source;
		std::string m_Identity;
		std::string m_Name; // Cached, refreshed by UpdateTalking.
		bool m_Muted = false;
		int m_MuteReason = 0; // VoiceLogic::EMuteReason
		std::chrono::steady_clock::time_point m_SoundUntil;
		std::chrono::steady_clock::time_point m_SoundStartedAt;
		vec2 m_TargetGains{0.0f, 0.0f};
		vec2 m_AppliedGains{0.0f, 0.0f};
		ma_pcm_rb m_Buffer{}; // 60 ms of mono PCM at 48 kHz.
		ma_sound m_Sound{};
		ma_gainer m_Gainer{};
		ma_channel_converter m_StereoConverter{};
		ma_data_converter m_Converter{};
		unsigned m_Initialized = 0;
		float m_LastSample = 0.0f;
		std::chrono::steady_clock::time_point m_BufferTime;
		int m_SourceRate = 0;
		int m_SourceChannels = 0;
		void Init(ma_engine *pEngine);
		~SAudioActivity();
		void Clear();
	};
	std::mutex m_AudioActivityLock;
	std::unordered_map<std::string, std::unique_ptr<SAudioActivity>> m_MAudioActivity; // Keyed by track SID; stable node addresses.
	void AddAudioTrack(const std::string &TrackId, const std::string &Identity, bool Muted);
	static void CheckAudio(ma_result Result, const char *pOperation);
	static ma_result ReadTrack(ma_data_source *pSource, void *pOutput, ma_uint64 Frames, ma_uint64 *pRead);
	static ma_result TrackFormat(ma_data_source *pSource, ma_format *pFormat, ma_uint32 *pChannels, ma_uint32 *pRate, ma_channel *pMap, size_t MapSize);
	static ma_uint32 ReadBuffer(ma_pcm_rb &Buffer, void *pOutput, ma_uint32 Frames);
	static ma_uint32 WriteBuffer(ma_pcm_rb &Buffer, const void *pInput, ma_uint32 Frames);
	void RecordAudioActivity(const std::string &TrackId, const livekit::AudioFrame &Frame);
	void SetAudioMuted(const std::string &TrackId, bool Muted);
	void ClearAudioActivity(const std::string &Identity);
	void UpdateTalking();
	static void ApplyMicGain(std::int16_t *pSamples, size_t Samples, float Gain);
	vec2 VoiceListenerPosition() const;
	bool OpenVoiceDevice(ma_device &Device, const std::string &Id, bool Capture);
	bool AudioDeviceAvailable(const std::string &Id, bool Capture);
	bool RestoreVoiceDevice(bool Capture);
	void RecoverAudioDevices();
	void UpdateDeviceDelay();
	std::vector<SDevice> AudioDevices(bool Capture);
	static std::string AudioDeviceId(ma_backend Backend, const ma_device_info &Device);
	static void DeviceCallback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 Frames);
	static void DeviceNotification(const ma_device_notification *pNotification);
	void StartAudio(const std::string &MicDevice, const std::string &PlayoutDevice);
	void StartAudioProcessing(bool MicrophoneTest = false);
	void MicrophoneTestThread(const std::string &MicDevice, const std::string &PlayoutDevice);
	void StartAudioThread();
	void AudioThread();
	void DisableMicrophone(const char *pError);
	void MixAudio(std::int16_t *pOutput);
	void StopAudio();

	class CDelegate : public livekit::RoomDelegate
	{
		CRCVoiceChat *m_pVoice;
		bool m_ParticipantsSnapshotted = false;

		void PushParticipant(const livekit::Participant &Participant)
		{
			SEvent Ev{EEventType::ParticipantJoined};
			Ev.m_Identity = Participant.identity();
			Ev.m_Name = Participant.name();
			Ev.m_Metadata = Participant.metadata();
			m_pVoice->PushEvent(std::move(Ev));
		}

		// Only from room event callbacks: Participant getters return references
		// that the SDK updates on that same event stream. This accessor only
		// locks Room's participant map; it does not send an FFI request.
		void SnapshotParticipants(livekit::Room &Room)
		{
			if(m_ParticipantsSnapshotted)
				return;
			for(const auto &Weak : Room.remoteParticipants())
				if(auto pParticipant = Weak.lock())
					PushParticipant(*pParticipant);
			m_ParticipantsSnapshotted = true;
		}

	public:
		explicit CDelegate(CRCVoiceChat *pVoice) :
			m_pVoice(pVoice) {}

		// ReleaseSession calls this after destroying Room and draining callbacks.
		void Reset() { m_ParticipantsSnapshotted = false; }

		void onActiveSpeakersChanged(livekit::Room &Room, const livekit::ActiveSpeakersChangedEvent &Event) override
		{
			SnapshotParticipants(Room);
			SEvent Ev{EEventType::ActiveSpeakers};
			for(const auto *pParticipant : Event.speakers)
				if(pParticipant)
					Ev.m_vIdentities.push_back(pParticipant->identity());
			m_pVoice->PushEvent(std::move(Ev));
		}

		void onConnectionStateChanged(livekit::Room &Room, const livekit::ConnectionStateChangedEvent &Event) override
		{
			m_pVoice->m_Reconnecting = Event.state == livekit::ConnectionState::Reconnecting;
			if(Event.state == livekit::ConnectionState::Reconnecting)
			{
				m_pVoice->m_PttHeld = false;
				m_pVoice->m_PttTail.Cancel();
				m_pVoice->PushEvent({EEventType::ActiveSpeakers});
			}
			if(Event.state == livekit::ConnectionState::Connected)
				SnapshotParticipants(Room);
		}

		// The SDK reports reconnect success through these two as well; relying on
		// the state change alone leaves the flag stuck when only they fire.
		void onReconnecting(livekit::Room &Room, const livekit::ReconnectingEvent &Event) override
		{
			m_pVoice->m_Reconnecting = true;
			m_pVoice->m_PttHeld = false;
			m_pVoice->m_PttTail.Cancel();
			m_pVoice->PushEvent({EEventType::ActiveSpeakers});
			m_ParticipantsSnapshotted = false;
		}

		void onReconnected(livekit::Room &Room, const livekit::ReconnectedEvent &Event) override
		{
			m_pVoice->m_Reconnecting = false;
			SnapshotParticipants(Room);
			dbg_msg("voicechat", "reconnected");
		}

		void onDisconnected(livekit::Room &Room, const livekit::DisconnectedEvent &Event) override
		{
			m_pVoice->m_Cancelled = true;
			m_pVoice->m_Connected = false;
			m_pVoice->m_Reconnecting = false;
			m_pVoice->m_PttHeld = false;
			m_pVoice->m_PttTail.Cancel();
			m_pVoice->PushEvent({EEventType::Disconnected});
			dbg_msg("voicechat", "disconnected (reason %d)", (int)Event.reason);
		}

		void onParticipantConnected(livekit::Room &Room, const livekit::ParticipantConnectedEvent &Event) override
		{
			SnapshotParticipants(Room);
			if(Event.participant)
				PushParticipant(*Event.participant);
		}

		void onParticipantDisconnected(livekit::Room &Room, const livekit::ParticipantDisconnectedEvent &Event) override
		{
			if(Event.participant)
			{
				m_pVoice->ClearAudioActivity(Event.participant->identity());
				m_pVoice->PushEvent({EEventType::ParticipantLeft, Event.participant->identity()});
			}
		}

		void onParticipantNameChanged(livekit::Room &Room, const livekit::ParticipantNameChangedEvent &Event) override
		{
			SnapshotParticipants(Room);
			if(Event.participant)
				PushParticipant(*Event.participant);
		}

		void onParticipantMetadataChanged(livekit::Room &Room, const livekit::ParticipantMetadataChangedEvent &Event) override
		{
			SnapshotParticipants(Room);
			if(Event.participant)
				PushParticipant(*Event.participant);
		}

		void onParticipantsUpdated(livekit::Room &Room, const livekit::ParticipantsUpdatedEvent &Event) override
		{
			SnapshotParticipants(Room);
			for(const auto *pParticipant : Event.participants)
				if(pParticipant)
					PushParticipant(*pParticipant);
		}

		void onConnectionQualityChanged(livekit::Room &Room, const livekit::ConnectionQualityChangedEvent &Event) override
		{
			// Covers rooms where every existing participant is receive-only.
			SnapshotParticipants(Room);
		}

		void onRoomUpdated(livekit::Room &Room, const livekit::RoomUpdatedEvent &Event) override
		{
			SnapshotParticipants(Room);
		}

		void onTrackSubscribed(livekit::Room &Room, const livekit::TrackSubscribedEvent &Event) override
		{
			SnapshotParticipants(Room);
			if(!Event.participant || !Event.track || Event.track->kind() != livekit::TrackKind::KIND_AUDIO)
			{
				if(Event.participant)
					PushParticipant(*Event.participant);
				return;
			}
			const std::string Identity = Event.participant->identity();
			const std::string TrackId = Event.track->sid();
			const std::string TrackName = Event.publication ? Event.publication->name() : Event.track->name();
			m_pVoice->AddAudioTrack(TrackId, Identity, Event.publication ? Event.publication->muted() : Event.track->muted());
			Room.setOnAudioFrameCallback(Identity, TrackName, [pVoice = m_pVoice, TrackId](const livekit::AudioFrame &Frame) { pVoice->RecordAudioActivity(TrackId, Frame); }, livekit::AudioStream::Options{2});
		}

		void onTrackUnsubscribed(livekit::Room &Room, const livekit::TrackUnsubscribedEvent &Event) override
		{
			if(!Event.participant || !Event.track || Event.track->kind() != livekit::TrackKind::KIND_AUDIO)
				return;
			const std::string TrackName = Event.publication ? Event.publication->name() : Event.track->name();
			Room.clearOnAudioFrameCallback(Event.participant->identity(), TrackName);
			std::lock_guard<std::mutex> Lock(m_pVoice->m_AudioActivityLock);
			m_pVoice->m_MAudioActivity.erase(Event.track->sid());
			++m_pVoice->m_OutputGeneration;
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
			SnapshotParticipants(Room);
		}
	};

	void ConnectThread(int LocalClientId,
		const std::string &MicDevice, const std::string &PlayoutDevice, const std::shared_ptr<IHttpRequest> &pTokenRequest, const std::string &RoomName);

	// ResetConnection joins the setup thread before replacing SDK objects.
	std::unique_ptr<livekit::Room> m_pRoom;
	CDelegate m_Delegate;
	std::thread m_Thread;
	std::shared_ptr<IHttpRequest> m_pTokenRequest; // Created and aborted on the game thread.
	std::atomic<bool> m_Connecting{false};
	std::atomic<bool> m_Connected{false};
	std::atomic<bool> m_Reconnecting{false};
	std::atomic<bool> m_Cancelled{false};
	// Cleanup runs after setup and may wait for metadata/SDK requests.
	std::atomic<bool> m_ResetPending{false};
	std::thread m_ResetThread;
	std::atomic<bool> m_ResetBusy{false};
	// Connect() was requested during cleanup or the token request cooldown.
	std::atomic<bool> m_ConnectPending{false};
	float m_NextPendingConnect = 0.0f;
	std::thread m_StatsThread;
	std::atomic<bool> m_StatsBusy{false};
	std::atomic<int> m_StatsPingMs{-1};
	float m_NextStatsRequest = 0.0f;
	float m_LastPingUpdate = -1.0f;
	int m_PingMs = -1;
	std::shared_ptr<livekit::AudioSource> m_pMicSource;
	std::unique_ptr<livekit::AudioProcessingModule> m_pAudioProcessing;
	std::thread m_AudioThread;
	std::atomic<bool> m_StopAudio{true};
	std::atomic<bool> m_MicFailed{false};
	std::atomic<float> m_MicGain{1.0f};
	std::atomic<bool> m_MicrophoneTesting{false};
	std::atomic<bool> m_MicrophoneTestListen{false};
	std::atomic<float> m_MicrophoneTestLevel{0.0f};
	float m_MicrophoneTestLastVisible = -1.0f;
	std::string m_TestMicDevice;
	std::string m_TestPlayoutDevice;
	ma_context m_AudioContext{};
	ma_engine m_AudioEngine{};
	ma_device m_OutputDevice{};
	ma_device m_CaptureDevice{};
	ma_pcm_rb m_OutputBuffer{};
	ma_pcm_rb m_CaptureBuffer{};
	bool m_EngineInitialized = false;
	bool m_OutputInitialized = false;
	bool m_CaptureInitialized = false;
	bool m_BuffersInitialized = false;
	int m_DeviceDelayMs = 0;
	int m_CaptureDelayMs = 0;
	int m_OutputDelayMs = 0;
	std::string m_SessionMicDevice;
	std::string m_SessionPlayoutDevice;
	bool m_CaptureFallback = false;
	bool m_OutputFallback = false;
	std::chrono::steady_clock::time_point m_NextAudioRecovery;
	bool m_AudioInitialized = false;
	std::mutex m_AudioContextLock;
	std::unordered_set<std::string> m_DeviceWarnings;
	std::mutex m_DeviceBufferLock; // Device callbacks try-lock; never wait on SDK or mixing.
	std::atomic<bool> m_OutputFailed{false};
	std::atomic<unsigned> m_OutputGeneration{0};
	std::atomic<unsigned> m_MicGeneration{0};
	std::mutex m_MicLock; // Protects the track and serializes mute/unmute.
	std::shared_ptr<livekit::LocalAudioTrack> m_pMicTrack;
	std::atomic<bool> m_MicReady{false};
	std::atomic<bool> m_Transmitting{false};
	std::atomic<bool> m_FullMuted{false};
	std::atomic<bool> m_Deafened{false};
	std::atomic<bool> m_PttHeld{false};
	VoiceLogic::CPttTail m_PttTail;
	bool m_Initialized = false;

	void ApplyMicState();
	bool PttActive() const;
	std::string RoomNameFromAddress() const;
	bool m_ManualDisconnect = false;
	float m_NextAutoConnect = 0.0f;

	std::string m_ConnectedRoom;
	float m_NextRoomCheck = 0.0f;

	// In-game client id published as metadata, needed for the dummy.
	std::atomic<int> m_MetadataClientId{-1};
	std::thread m_MetadataThread;
	std::atomic<bool> m_MetadataBusy{false};
	int m_MetadataRequestedClientId = -1; // Game thread; result is read after joining the worker.
	VoiceLogic::CMetadataRetry m_MetadataRetry;
	// When the current reconnect started, to give up on a stuck one.
	std::atomic<float> m_ReconnectingSince{0.0f};

	std::atomic<int> m_ConnectFailures{0};
	float m_LastTokenRequestTime = 0.0f;

	float m_LimiterGain = 1.0f;
#endif

public:
	CRCVoiceChat();
	~CRCVoiceChat() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRelease() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnShutdown() override;

	bool Connected() const;
	bool Connecting() const;
	std::vector<SParticipant> Participants() const;
	EPlayerVoiceState PlayerVoiceState(int ClientId) const;
	void Connect();
	void Disconnect();
	bool Muted() const;
	void SetMuted(bool Muted);
	bool Deafened() const;
	void SetDeafened(bool Deafened);
	bool Transmitting() const;
	void StartMicrophoneTest();
	void StopMicrophoneTest();
	void KeepMicrophoneTestVisible();
	bool MicrophoneTesting() const;
	bool MicrophoneTestListening() const;
	void SetMicrophoneTestListening(bool Listen);
	float MicrophoneTestLevel() const;
	int ClientIdOf(const std::string &Identity) const;

	using SUserSettings = VoiceLogic::SUserSettings;
	std::string ParticipantName(const SParticipant &Participant) const;
	SUserSettings UserSettings(const char *pName) const;
	std::vector<SUserSettings> SavedUsers() const;
	bool SetUserSettings(const char *pName, int Volume, bool Muted);
	void ForgetUser(const char *pName);

	// Legacy console commands share the settings used by the user list.
	bool IsNameMuted(const char *pName) const;
	void ToggleNameMute(const char *pName);
	std::vector<std::string> MutedNames() const;
	std::string LastError() const;
};

#endif // GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_VOICE_CHAT_H
