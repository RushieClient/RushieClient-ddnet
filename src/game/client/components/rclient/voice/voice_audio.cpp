// Miniaudio transport of the voice chat.
#ifdef CONF_LIVEKIT

// The MA_* switches live in voice_chat.h. They must be active before the first
// include of miniaudio.h in this file, and the implementation must be requested
// on that same first include: miniaudio puts its implementation inside the same
// include guard as the declarations, so a second include never compiles it.
#define MINIAUDIO_IMPLEMENTATION
#include "voice_chat.h"
#undef MINIAUDIO_IMPLEMENTATION

#include "voice_chat_logic.h"

#include <base/hash.h>
#include <base/log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-negative"
#endif

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
			   MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, &m_Sound),
		"initialize voice sound");
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
	// Incoming speech activity survives local playback clears and mutes.
	m_LastSample = 0.0f;
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
		// Fade an underrun to zero instead of holding a DC sample indefinitely.
		for(ma_uint32 i = Read; i < Count; ++i)
		{
			Audio.m_LastSample *= 0.95f;
			aMono[i] = Audio.m_LastSample;
		}
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
	default:
		Size = 0;
		while(Size < sizeof(Device.id) && reinterpret_cast<const char *>(pId)[Size])
			++Size;
		break;
	}
	char aHash[SHA256_MAXSTRSIZE];
	sha256_str(sha256(pId, Size), aHash, sizeof(aHash));
	return std::string("ma:") + ma_get_backend_name(Backend) + ":" + aHash;
}

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

bool CRCVoiceChat::AudioDeviceAvailable(const std::string &Id, bool Capture)
{
	std::lock_guard<std::mutex> Lock(m_AudioContextLock);
	ma_device_info *pOutputs, *pInputs;
	ma_uint32 Outputs, Inputs;
	if(Id.empty() || ma_context_get_devices(&m_AudioContext, &pOutputs, &Outputs, &pInputs, &Inputs) != MA_SUCCESS)
		return false;
	const ma_device_info *pDevices = Capture ? pInputs : pOutputs;
	for(ma_uint32 i = 0; i < (Capture ? Inputs : Outputs); ++i)
		if(Id == AudioDeviceId(m_AudioContext.backend, pDevices[i]) || Id == std::string("sdl:") + pDevices[i].name)
			return true;
	return false;
}

bool CRCVoiceChat::OpenVoiceDevice(ma_device &Device, const std::string &Id, bool Capture)
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
		log_warn("voicechat", "%s device not found, using system default", Capture ? "microphone" : "output");
	ma_device_config Config = ma_device_config_init(Capture ? ma_device_type_capture : ma_device_type_playback);
	Config.sampleRate = 48000;
	Config.periodSizeInFrames = 480;
	Config.periods = 2;
	Config.wasapi.noAutoStreamRouting = pSelected ? MA_TRUE : MA_FALSE;
	Config.capture.format = Config.playback.format = ma_format_s16;
	Config.capture.channels = 1;
	Config.playback.channels = 2;
	Config.capture.pDeviceID = Capture ? pSelected : nullptr;
	Config.playback.pDeviceID = Capture ? nullptr : pSelected;
	Config.dataCallback = DeviceCallback;
	Config.notificationCallback = DeviceNotification;
	Config.pUserData = this;
	CheckAudio(ma_device_init(&m_AudioContext, &Config, &Device), Capture ? "open voice microphone" : "open voice output");
	// Snapshot latency before start: automatic routing may later mutate the
	// backend's device fields from its own thread.
	if(Capture)
		m_CaptureDelayMs = VoiceLogic::DeviceDelayMs(Device.capture.internalPeriodSizeInFrames, Device.capture.internalPeriods, Device.capture.internalSampleRate);
	else
		m_OutputDelayMs = VoiceLogic::DeviceDelayMs(Device.playback.internalPeriodSizeInFrames, Device.playback.internalPeriods, Device.playback.internalSampleRate);
	return !Id.empty() && !pSelected;
}

void CRCVoiceChat::DeviceCallback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 Frames)
{
	auto *pVoice = static_cast<CRCVoiceChat *>(pDevice->pUserData);
	if(pOutput)
		ma_silence_pcm_frames(pOutput, Frames, ma_format_s16, 2);
	std::unique_lock<std::mutex> Lock(pVoice->m_DeviceBufferLock, std::try_to_lock);
	if(!Lock || pVoice->m_StopAudio)
		return;
	if(pOutput && !pVoice->m_OutputFailed)
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
		{
			pVoice->m_MicFailed = true;
			pVoice->m_MicReady = false;
			pVoice->m_PttHeld = false;
			pVoice->m_PttTail.Cancel();
		}
		else
			pVoice->m_OutputFailed = true;
	}
	if(pNotification->type == ma_device_notification_type_rerouted)
	{
		if(pNotification->pDevice->type == ma_device_type_capture)
		{
			pVoice->m_PttHeld = false;
			pVoice->m_PttTail.Cancel();
			++pVoice->m_MicGeneration;
		}
		else
			++pVoice->m_OutputGeneration;
	}
}

void CRCVoiceChat::StartAudio(const std::string &MicDevice, const std::string &PlayoutDevice)
{
	m_MicFailed = m_OutputFailed = false;
	m_MicReady = false;
	m_SessionMicDevice = MicDevice;
	m_SessionPlayoutDevice = PlayoutDevice;
	m_CaptureFallback = m_OutputFallback = false;
	m_NextAudioRecovery = std::chrono::steady_clock::now() + std::chrono::seconds(2);
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
	m_StopAudio = false;
	try
	{
		m_OutputFallback = OpenVoiceDevice(m_OutputDevice, PlayoutDevice, false);
		m_OutputInitialized = true;
		CheckAudio(ma_device_start(&m_OutputDevice), "start voice output");
	}
	catch(const std::exception &Ex)
	{
		m_OutputFailed = true;
		SetAudioError(Ex.what());
		log_warn("voicechat", "audio output unavailable: %s", Ex.what());
		if(m_OutputInitialized)
			ma_device_uninit(&m_OutputDevice);
		m_OutputInitialized = false;
	}
	try
	{
		m_CaptureFallback = OpenVoiceDevice(m_CaptureDevice, MicDevice, true);
		m_CaptureInitialized = true;
		CheckAudio(ma_device_start(&m_CaptureDevice), "start voice microphone");
	}
	catch(const std::exception &Ex)
	{
		m_MicFailed = true;
		SetAudioError(Ex.what());
		log_warn("voicechat", "microphone unavailable: %s", Ex.what());
		if(m_CaptureInitialized)
			ma_device_uninit(&m_CaptureDevice);
		m_CaptureInitialized = false;
	}
	UpdateDeviceDelay();
	// FFI objects follow, Room::connect() owns the client while it runs.
}

void CRCVoiceChat::MicrophoneTestThread(const std::string& MicDevice, const std::string& PlayoutDevice)
{
	try
	{
		StartAudio(MicDevice, PlayoutDevice);
		if(m_MicFailed)
			throw std::runtime_error("microphone is unavailable");
		if(!m_Cancelled)
		{
			StartAudioProcessing(true);
			if(m_MicFailed)
				throw std::runtime_error("microphone processing is unavailable");
			StartAudioThread();
		}
	}
	catch(const std::exception &Ex)
	{
		m_Cancelled = true;
		SetLastError(Ex.what());
	}
	ConnectThreadFinish();
}

void CRCVoiceChat::StartAudioProcessing(bool MicrophoneTest)
{
	// Keep the published source even when capture is unavailable. A recovered
	// device feeds the same track without leaving or republishing in the room.
	if(!MicrophoneTest && !m_pMicSource)
		m_pMicSource = std::make_shared<livekit::AudioSource>(48000, 1, 0);
	if(m_MicFailed)
		return;
	try
	{
		livekit::AudioProcessingModule::Options Options;
		// A monitoring test must not try to cancel the voice being monitored.
		Options.echo_cancellation = !MicrophoneTest;
		Options.noise_suppression = true;
		Options.auto_gain_control = true;
		Options.high_pass_filter = true;
		m_pAudioProcessing = std::make_unique<livekit::AudioProcessingModule>(Options);
		log_trace("voicechat", "microphone processing ready");
	}
	catch(const std::exception &Ex)
	{
		m_MicFailed = true;
		SetAudioError(Ex.what());
		log_warn("voicechat", "microphone processing unavailable: %s", Ex.what());
		m_pAudioProcessing.reset();
	}
}

void CRCVoiceChat::StartAudioThread()
{
	if(m_AudioThread.joinable())
		return;
	m_AudioThread = std::thread(&CRCVoiceChat::AudioThread, this);
}

void CRCVoiceChat::MixAudio(std::int16_t *pOutput)
{
	std::array<float, 960> aMix{};
	{
		std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
		const auto Now = std::chrono::steady_clock::now();
		for(auto &[Id, pAudio] : m_MAudioActivity)
			if(!m_Connected || m_Cancelled || m_Reconnecting || m_Deafened || pAudio->m_Muted || Now - pAudio->m_BufferTime > std::chrono::milliseconds(60))
				pAudio->Clear();
		CheckAudio(ma_engine_read_pcm_frames(&m_AudioEngine, aMix.data(), 480, nullptr), "mix voice audio");
	}
	if(m_Deafened)
		aMix.fill(0.0f);

	float Peak = 0.0f;
	for(float Sample : aMix)
		Peak = std::max(Peak, std::abs(Sample));

	// Limiter instead of per-block normalisation, which pumps on loud voices.
	const float Target = VoiceLogic::LimiterTarget(Peak);
	if(Target < m_LimiterGain)
		m_LimiterGain = Target;
	else
		m_LimiterGain = std::min(Target, m_LimiterGain + (Target - m_LimiterGain) * 0.05f);

	for(float &Sample : aMix)
		Sample = std::isfinite(Sample) ? std::tanh(Sample * m_LimiterGain) : 0.0f;
	ma_pcm_f32_to_s16(pOutput, aMix.data(), aMix.size(), ma_dither_mode_none);
}

void CRCVoiceChat::UpdateDeviceDelay()
{
	m_DeviceDelayMs = 0;
	if(m_OutputInitialized && !m_OutputFailed)
		m_DeviceDelayMs += m_OutputDelayMs;
	if(m_CaptureInitialized && !m_MicFailed)
		m_DeviceDelayMs += m_CaptureDelayMs;
}

bool CRCVoiceChat::RestoreVoiceDevice(bool Capture)
{
	auto &Device = Capture ? m_CaptureDevice : m_OutputDevice;
	bool &Initialized = Capture ? m_CaptureInitialized : m_OutputInitialized;
	bool &Fallback = Capture ? m_CaptureFallback : m_OutputFallback;
	const auto &Id = Capture ? m_SessionMicDevice : m_SessionPlayoutDevice;
	if(Capture)
	{
		m_MicReady = false;
		m_PttHeld = false;
		m_PttTail.Cancel();
		m_pAudioProcessing.reset();
	}
	// Device callbacks only publish flags. Stop/init/start must run here,
	// outside those callbacks and outside the game/render thread.
	if(Initialized)
		ma_device_uninit(&Device);
	Initialized = false;
	if(m_StopAudio || m_Cancelled)
		return false;
	try
	{
		Fallback = OpenVoiceDevice(Device, Id, Capture);
		Initialized = true;
		{
			std::lock_guard<std::mutex> Lock(m_DeviceBufferLock);
			ma_pcm_rb_reset(Capture ? &m_CaptureBuffer : &m_OutputBuffer);
		}
		if(Capture)
		{
			++m_MicGeneration;
			m_MicFailed = false;
			StartAudioProcessing(m_MicrophoneTesting);
			if(m_MicFailed || !m_pAudioProcessing)
				throw std::runtime_error("microphone processing unavailable");
		}
		else
		{
			++m_OutputGeneration;
			m_OutputFailed = false;
			std::lock_guard<std::mutex> Lock(m_AudioActivityLock);
			for(auto &[TrackId, pAudio] : m_MAudioActivity)
				pAudio->Clear();
		}
		UpdateDeviceDelay();
		CheckAudio(ma_device_start(&Device), Capture ? "restart voice microphone" : "restart voice output");
		if(Capture)
		{
			std::lock_guard<std::mutex> Lock(m_MicLock);
			m_MicReady = !m_MicrophoneTesting && m_pMicTrack && !m_MicFailed;
		}
		log_trace("voicechat", "%s device restored%s", Capture ? "microphone" : "output", Fallback ? " (system default fallback)" : "");
		return true;
	}
	catch(const std::exception &Ex)
	{
		if(Capture)
		{
			m_MicFailed = true;
			m_pAudioProcessing.reset();
		}
		else
			m_OutputFailed = true;
		SetAudioError(Ex.what());
		log_trace("voicechat", "%s device recovery failed: %s", Capture ? "microphone" : "output", Ex.what());
		if(Initialized)
			ma_device_uninit(&Device);
		Initialized = false;
		return false;
	}
}

void CRCVoiceChat::RecoverAudioDevices()
{
	const auto Now = std::chrono::steady_clock::now();
	if(m_StopAudio || m_Cancelled || Now < m_NextAudioRecovery)
		return;
	m_NextAudioRecovery = Now + std::chrono::seconds(2);
	if(m_OutputInitialized && ma_device_get_state(&m_OutputDevice) != ma_device_state_started)
		m_OutputFailed = true;
	if(m_CaptureInitialized && ma_device_get_state(&m_CaptureDevice) != ma_device_state_started)
	{
		m_MicFailed = true;
		m_MicReady = false;
		m_PttHeld = false;
		m_PttTail.Cancel();
	}
	if(m_OutputFailed || (m_OutputFallback && AudioDeviceAvailable(m_SessionPlayoutDevice, false)))
		RestoreVoiceDevice(false);
	if(m_MicFailed || (m_CaptureFallback && AudioDeviceAvailable(m_SessionMicDevice, true)))
		RestoreVoiceDevice(true);
	UpdateDeviceDelay();
	if(!m_MicFailed && !m_OutputFailed)
		SetAudioError("");
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
			if(m_MicFailed && m_pAudioProcessing)
				DisableMicrophone("microphone device stopped or disconnected");
			RecoverAudioDevices();
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
				if(m_pAudioProcessing && !m_MicFailed && CaptureFrames >= 480)
					HaveMicrophone = ReadBuffer(m_CaptureBuffer, Microphone.data().data(), 480) == 480;
			}
			if(!m_MicrophoneTesting && !m_OutputFailed && OutputFrames < 960)
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
					if(!m_MicrophoneTesting)
						m_pAudioProcessing->setStreamDelayMs(m_DeviceDelayMs + int(OutputFrames + CaptureFrames) / 48);
					m_pAudioProcessing->processStream(Microphone);
					if(m_MicrophoneTesting)
					{
						ApplyMicGain(Microphone.data().data(), Microphone.data().size(), m_MicGain.load());
						m_MicrophoneTestLevel = VoiceLogic::MicrophonePeak(Microphone.data().data(), Microphone.data().size());
						if(m_MicrophoneTestListen && !m_Cancelled && !m_OutputFailed)
						{
							for(size_t i = 0; i < 480; ++i)
								aOutput[i * 2] = aOutput[i * 2 + 1] = Microphone.data()[i];
							std::lock_guard<std::mutex> Lock(m_DeviceBufferLock);
							if(m_MicrophoneTestListen && !m_Cancelled && OutputGeneration == m_OutputGeneration)
							{
								if(ma_pcm_rb_available_write(&m_OutputBuffer) < 480)
									ma_pcm_rb_reset(&m_OutputBuffer);
								WriteBuffer(m_OutputBuffer, aOutput.data(), 480);
							}
						}
					}
					else if(Transmitting() && !m_Cancelled && MicGeneration == m_MicGeneration)
					{
						ApplyMicGain(Microphone.data().data(), Microphone.data().size(), m_MicGain.load());
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
		SetLastError(Ex.what());
		log_warn("voicechat", "audio output failed: %s", Ex.what());
	}
}

void CRCVoiceChat::DisableMicrophone(const char *pError)
{
	m_MicFailed = true;
	m_MicReady = false;
	m_PttHeld = false;
	m_PttTail.Cancel();
	m_MicrophoneTestLevel = 0.0f;
	m_pAudioProcessing.reset();
	if(m_CaptureInitialized)
		ma_device_stop(&m_CaptureDevice);
	std::lock_guard<std::mutex> Lock(m_DeviceBufferLock);
	ma_pcm_rb_reset(&m_CaptureBuffer);
	SetAudioError(pError);
	log_warn("voicechat", "microphone processing failed: %s", pError);
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

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif
