#include "rclient_indicator.h"

#include <base/system.h>

#include <generated/rclient_secret_token.h>

#include "engine/client.h"
#include "engine/external/json-parser/json.h"
#include "engine/serverbrowser.h"
#include "game/client/gameclient.h"
#include <game/version.h>

#include <cstdint>
#include <string>

static constexpr int POLL_TIMEOUT_SECONDS = 20;
static constexpr int POLL_RETRY_SECONDS = 1;
static constexpr int HTTP_TIMEOUT_MS = 25000;
static constexpr int HTTP_CONNECT_TIMEOUT_MS = 10000;
static constexpr int LONGPOLL_TIMEOUT_MS = (POLL_TIMEOUT_SECONDS + 5) * 1000;

static const char *GetRclientUsersUrl()
{
	return g_Config.m_RiServerUrl == 0 ? "https://server.rushie-client.ru/users.json" : "https://server.rushie.qzz.io/users.json";
}

static const char *GetRclientClientVersion()
{
	return RCLIENT_VERSION;
}

static bool ShouldIgnoreIndicatorErrors()
{
	return g_Config.m_RiRclientIndicatorIgnoreErrors != 0;
}

static HTTPLOG GetIndicatorHttpLogLevel()
{
	if(ShouldIgnoreIndicatorErrors())
		return HTTPLOG::NONE;
	return g_Config.m_ConsoleOutputLevel >= 1 ? HTTPLOG::ALL : HTTPLOG::NONE;
}

static unsigned char BuildIndicatorDataMask(int Index)
{
	const unsigned char OffsetA = static_cast<unsigned char>((Index * RCLIENT_BUILD_DATA_STEP_A) & 0xFF);
	const unsigned char OffsetB = static_cast<unsigned char>((((Index + 1) * RCLIENT_BUILD_DATA_STEP_B) + RCLIENT_BUILD_DATA_SALT) & 0xFF);
	return static_cast<unsigned char>((RCLIENT_BUILD_DATA_SEED + OffsetA) ^ OffsetB);
}

static std::string DecodeBuildIndicatorSecretToken()
{
	std::string Result;
	Result.reserve(RCLIENT_BUILD_DATA_LENGTH);
	for(int i = 0; i < RCLIENT_BUILD_DATA_LENGTH; ++i)
		Result.push_back(static_cast<char>(gs_aRclientBuildData[i] ^ BuildIndicatorDataMask(i)));
	return Result;
}

static void ClearBuildSecretToken(std::string &Token)
{
	for(char &Byte : Token)
		Byte = '\0';
	Token.clear();
}

static bool ParseServerAddress(const char *pAddrStr, char *pHost, size_t HostSize, int &Port)
{
	const char *pColon = str_rchr(pAddrStr, ':');
	if(!pColon || pColon == pAddrStr || *(pColon + 1) == '\0')
		return false;

	str_truncate(pHost, HostSize, pAddrStr, pColon - pAddrStr);
	if(pHost[0] == '[')
	{
		const int Len = str_length(pHost);
		if(Len >= 2 && pHost[Len - 1] == ']')
		{
			mem_move(pHost, pHost + 1, Len - 2);
			pHost[Len - 2] = '\0';
		}
	}

	Port = str_toint(pColon + 1);
	return Port > 0 && Port <= 65535;
}

static uint64_t RotL64(uint64_t Value, int Count)
{
	return (Value << Count) | (Value >> (64 - Count));
}

static uint64_t ReadU64LE(const uint8_t *pData)
{
	uint64_t Value = 0;
	for(int i = 0; i < 8; ++i)
		Value |= (uint64_t)pData[i] << (i * 8);
	return Value;
}

static void WriteU16LE(uint8_t *pData, uint16_t Value)
{
	pData[0] = Value & 0xff;
	pData[1] = (Value >> 8) & 0xff;
}

static void WriteU32LE(uint8_t *pData, uint32_t Value)
{
	pData[0] = Value & 0xff;
	pData[1] = (Value >> 8) & 0xff;
	pData[2] = (Value >> 16) & 0xff;
	pData[3] = (Value >> 24) & 0xff;
}

static void WriteU64LE(uint8_t *pData, uint64_t Value)
{
	for(int i = 0; i < 8; ++i)
		pData[i] = (Value >> (i * 8)) & 0xff;
}

static void FormatHexLower(const uint8_t *pData, int DataSize, char *pBuffer, int BufferSize)
{
	static const char s_aHex[] = "0123456789abcdef";
	const int Required = DataSize * 2 + 1;
	if(BufferSize < Required)
	{
		if(BufferSize > 0)
			pBuffer[0] = '\0';
		return;
	}

	for(int i = 0; i < DataSize; ++i)
	{
		pBuffer[i * 2] = s_aHex[(pData[i] >> 4) & 0xf];
		pBuffer[i * 2 + 1] = s_aHex[pData[i] & 0xf];
	}
	pBuffer[DataSize * 2] = '\0';
}

static uint64_t SipHash24(const uint8_t aKey[16], const uint8_t *pData, size_t Size)
{
	const uint64_t K0 = ReadU64LE(aKey);
	const uint64_t K1 = ReadU64LE(aKey + 8);
	uint64_t V0 = 0x736f6d6570736575ULL ^ K0;
	uint64_t V1 = 0x646f72616e646f6dULL ^ K1;
	uint64_t V2 = 0x6c7967656e657261ULL ^ K0;
	uint64_t V3 = 0x7465646279746573ULL ^ K1;

	auto SipRound = [&]() {
		V0 += V1;
		V1 = RotL64(V1, 13);
		V1 ^= V0;
		V0 = RotL64(V0, 32);
		V2 += V3;
		V3 = RotL64(V3, 16);
		V3 ^= V2;
		V0 += V3;
		V3 = RotL64(V3, 21);
		V3 ^= V0;
		V2 += V1;
		V1 = RotL64(V1, 17);
		V1 ^= V2;
		V2 = RotL64(V2, 32);
	};

	const uint8_t *pCur = pData;
	const uint8_t *pEnd = pData + (Size & ~size_t(7));
	while(pCur != pEnd)
	{
		const uint64_t M = ReadU64LE(pCur);
		pCur += 8;
		V3 ^= M;
		SipRound();
		SipRound();
		V0 ^= M;
	}

	uint64_t Last = (uint64_t)Size << 56;
	switch(Size & 7)
	{
	case 7: Last |= (uint64_t)pCur[6] << 48; [[fallthrough]];
	case 6: Last |= (uint64_t)pCur[5] << 40; [[fallthrough]];
	case 5: Last |= (uint64_t)pCur[4] << 32; [[fallthrough]];
	case 4: Last |= (uint64_t)pCur[3] << 24; [[fallthrough]];
	case 3: Last |= (uint64_t)pCur[2] << 16; [[fallthrough]];
	case 2: Last |= (uint64_t)pCur[1] << 8; [[fallthrough]];
	case 1: Last |= (uint64_t)pCur[0]; [[fallthrough]];
	default: break;
	}

	V3 ^= Last;
	SipRound();
	SipRound();
	V0 ^= Last;
	V2 ^= 0xff;
	for(int i = 0; i < 4; ++i)
		SipRound();
	return V0 ^ V1 ^ V2 ^ V3;
}

static int BuildSipHashMessage(int ClientId, const char *pServerIp, int Port, uint32_t Timestamp, uint8_t *pBuffer, int BufferSize)
{
	const int ServerIpLen = str_length(pServerIp);
	if(ServerIpLen < 0 || ServerIpLen > 0xffff)
		return -1;

	const int Required = 4 + 2 + ServerIpLen + 2 + 4;
	if(BufferSize < Required)
		return -1;

	int Offset = 0;
	WriteU32LE(pBuffer + Offset, (uint32_t)ClientId);
	Offset += 4;
	WriteU16LE(pBuffer + Offset, (uint16_t)ServerIpLen);
	Offset += 2;
	mem_copy(pBuffer + Offset, pServerIp, ServerIpLen);
	Offset += ServerIpLen;
	WriteU16LE(pBuffer + Offset, (uint16_t)Port);
	Offset += 2;
	WriteU32LE(pBuffer + Offset, Timestamp);
	Offset += 4;
	return Offset;
}

static bool ComputeIndicatorAuthHex(int ClientId, const char *pServerIp, int Port, uint32_t Timestamp, char *pHashHex, int HashHexSize)
{
	std::string SecretToken = DecodeBuildIndicatorSecretToken();
	if(SecretToken.size() != 16)
	{
		ClearBuildSecretToken(SecretToken);
		return false;
	}

	uint8_t aMessage[512];
	const int MessageSize = BuildSipHashMessage(ClientId, pServerIp, Port, Timestamp, aMessage, sizeof(aMessage));
	if(MessageSize < 0)
	{
		ClearBuildSecretToken(SecretToken);
		return false;
	}

	const uint64_t Hash = SipHash24(reinterpret_cast<const uint8_t *>(SecretToken.data()), aMessage, (size_t)MessageSize);
	uint8_t aHashBytes[8];
	WriteU64LE(aHashBytes, Hash);
	FormatHexLower(aHashBytes, sizeof(aHashBytes), pHashHex, HashHexSize);
	ClearBuildSecretToken(SecretToken);
	return true;
}

static bool TryParseVoiceAuth(const json_value &VoiceAuth, uint32_t &Timestamp, uint64_t &Hash)
{
	if(VoiceAuth.type != json_object)
		return false;

	const json_value &TimestampValue = VoiceAuth["timestamp"];
	const json_value &HashValue = VoiceAuth["hash"];
	if(TimestampValue.type != json_integer || HashValue.type != json_string)
		return false;
	if(TimestampValue.u.integer < 0 || TimestampValue.u.integer > 0xffffffffLL)
		return false;

	uint8_t aHashBytes[8];
	if(str_hex_decode(aHashBytes, sizeof(aHashBytes), HashValue.u.string.ptr) != 0)
		return false;

	Timestamp = (uint32_t)TimestampValue.u.integer;
	Hash = ReadU64LE(aHashBytes);
	return true;
}

CRClientIndicator::CRClientIndicator()
{
}

void CRClientIndicator::OnInit()
{
	ClearVoiceAuth();
}

void CRClientIndicator::OnRender()
{
	auto HandleTaskDone = [&](std::shared_ptr<CHttpRequest> &pTask, auto &&Finish, auto &&Reset) {
		if(!pTask)
			return;
		if(pTask->State() == EHttpState::DONE)
		{
			Finish();
			Reset();
		}
		else if(pTask->State() == EHttpState::ERROR || pTask->State() == EHttpState::ABORTED)
		{
			Reset();
		}
	};

	HandleTaskDone(m_pRClientUsersTask, [this]() { FinishRClientUsers(); }, [this]() { ResetRClientUsers(); });
	HandleTaskDone(m_pRClientUsersTaskSend, [this]() { FinishRClientUsersSend(); }, [this]() { ResetRClientUsersSend(); });

	const int64_t Now = time_get();
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	if(!Online)
	{
		if(m_WasOnline && !m_LastServerAddress.empty() && m_LastLocalId >= 0)
			SendPlayerData(m_LastServerAddress.c_str(), m_LastLocalId, m_LastDummyId, false);

		ResetRClientUsers();
		m_WasOnline = false;
		ClearUsers();
		ClearVoiceAuth();
		m_LastPollAttempt = 0;
		m_ServerRev = 0;
		return;
	}

	if(!m_WasOnline)
	{
		m_WasOnline = true;
		m_LastPollAttempt = 0;
	}

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);
	const char *pServerAddress = CurrentServerInfo.m_aAddress;

	int LocalClientId = GameClient()->m_aLocalIds[0];
	int DummyClientId = -1;
	if(Client()->DummyConnected())
		DummyClientId = GameClient()->m_aLocalIds[1];

	bool ForceSync = false;
	if(m_LastServerAddress != pServerAddress)
	{
		if(!m_LastServerAddress.empty() && m_LastLocalId >= 0)
			SendPlayerData(m_LastServerAddress.c_str(), m_LastLocalId, m_LastDummyId, false);

		ResetRClientUsers();
		ClearUsers();
		ClearVoiceAuth();
		m_LastServerAddress = pServerAddress;
		m_ServerRev = 0;
		ForceSync = true;
	}

	if(m_LastLocalId != LocalClientId || m_LastDummyId != DummyClientId)
	{
		if(m_LastLocalId >= 0 && !m_LastServerAddress.empty())
			SendPlayerData(m_LastServerAddress.c_str(), m_LastLocalId, m_LastDummyId, false);

		m_LastLocalId = LocalClientId;
		m_LastDummyId = DummyClientId;
		m_ServerRev = 0;
		ClearVoiceAuth();
		ForceSync = true;
	}

	const int64_t PollRetry = time_freq() * POLL_RETRY_SECONDS;
	if(!m_pRClientUsersTask && (ForceSync || Now - m_LastPollAttempt >= PollRetry))
	{
		if(LocalClientId >= 0)
			FetchRClientUsers(pServerAddress, LocalClientId, DummyClientId);
		m_LastPollAttempt = Now;
	}
}

void CRClientIndicator::SendPlayerData(const char *pServerAddress, int ClientId, int DummyClientId, bool Online)
{
	const char *pUsersUrl = GetRclientUsersUrl();
	if(!pUsersUrl || ClientId < 0)
		return;

	char aServerIp[128];
	int ServerPort = 0;
	if(!ParseServerAddress(pServerAddress, aServerIp, sizeof(aServerIp), ServerPort))
		return;

	const uint32_t AuthTimestamp = (uint32_t)time_timestamp();
	char aAuthHash[32];
	if(!ComputeIndicatorAuthHex(ClientId, aServerIp, ServerPort, AuthTimestamp, aAuthHash, sizeof(aAuthHash)))
		return;

	ResetRClientUsersSend();

	const char *pOnlineStr = Online ? "true" : "false";
	const char *pVoiceEnabledStr = g_Config.m_RiVoiceEnable ? "true" : "false";
	const char *pVoiceMutedStr = g_Config.m_RiVoiceEnable && g_Config.m_RiVoiceMicMute ? "true" : "false";
	char aJsonData[768];

	if(DummyClientId >= 0)
	{
		str_format(aJsonData, sizeof(aJsonData),
			"{"
			"\"server_address\":\"%s\","
			"\"server_ip\":\"%s\","
			"\"server_port\":%d,"
			"\"player_id\":%d,"
			"\"dummy_id\":%d,"
			"\"online\":%s,"
			"\"voice_enabled\":%s,"
			"\"voice_muted\":%s,"
			"\"client_version\":\"%s\","
			"\"auth_timestamp\":%u,"
			"\"auth_hash\":\"%s\""
			"}",
			pServerAddress,
			aServerIp,
			ServerPort,
			ClientId,
			DummyClientId,
			pOnlineStr,
			pVoiceEnabledStr,
			pVoiceMutedStr,
			GetRclientClientVersion(),
			AuthTimestamp,
			aAuthHash);
	}
	else
	{
		str_format(aJsonData, sizeof(aJsonData),
			"{"
			"\"server_address\":\"%s\","
			"\"server_ip\":\"%s\","
			"\"server_port\":%d,"
			"\"player_id\":%d,"
			"\"online\":%s,"
			"\"voice_enabled\":%s,"
			"\"voice_muted\":%s,"
			"\"client_version\":\"%s\","
			"\"auth_timestamp\":%u,"
			"\"auth_hash\":\"%s\""
			"}",
			pServerAddress,
			aServerIp,
			ServerPort,
			ClientId,
			pOnlineStr,
			pVoiceEnabledStr,
			pVoiceMutedStr,
			GetRclientClientVersion(),
			AuthTimestamp,
			aAuthHash);
	}

	m_pRClientUsersTaskSend = std::make_shared<CHttpRequest>(pUsersUrl);
	m_pRClientUsersTaskSend->PostJson(aJsonData);
	m_pRClientUsersTaskSend->Timeout(CTimeout{HTTP_TIMEOUT_MS, 0, 500, 5});
	m_pRClientUsersTaskSend->IpResolve(IPRESOLVE::V4);
	m_pRClientUsersTaskSend->LogProgress(GetIndicatorHttpLogLevel());
	Http()->Run(m_pRClientUsersTaskSend);
}

void CRClientIndicator::FetchRClientUsers(const char *pServerAddress, int ClientId, int DummyClientId)
{
	if(m_pRClientUsersTask && !m_pRClientUsersTask->Done())
		return;

	char aServerIp[128];
	int ServerPort = 0;
	if(!ParseServerAddress(pServerAddress, aServerIp, sizeof(aServerIp), ServerPort))
		return;

	const char *pUsersUrl = GetRclientUsersUrl();
	if(!pUsersUrl)
		return;

	m_pRClientUsersTask = HttpGet(pUsersUrl);
	ApplyPollHeaders(*m_pRClientUsersTask, pServerAddress, ClientId, DummyClientId);
	m_pRClientUsersTask->HeaderString("X-RClient-Server-Ip", aServerIp);
	m_pRClientUsersTask->HeaderInt("X-RClient-Server-Port", ServerPort);
	m_pRClientUsersTask->Timeout(CTimeout{HTTP_CONNECT_TIMEOUT_MS, LONGPOLL_TIMEOUT_MS, 0, 0});
	m_pRClientUsersTask->IpResolve(IPRESOLVE::V4);
	m_pRClientUsersTask->LogProgress(GetIndicatorHttpLogLevel());
	Http()->Run(m_pRClientUsersTask);
}

void CRClientIndicator::FinishRClientUsersSend()
{
	if(!m_pRClientUsersTaskSend)
		return;

	const int Status = m_pRClientUsersTaskSend->StatusCode();
	if((Status == 401 || Status == 403) && !ShouldIgnoreIndicatorErrors())
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "RClient", "Indicator POST auth rejected");
}

void CRClientIndicator::ResetRClientUsersSend()
{
	if(m_pRClientUsersTaskSend)
	{
		m_pRClientUsersTaskSend->Abort();
		m_pRClientUsersTaskSend = nullptr;
	}
}

void CRClientIndicator::FinishRClientUsers()
{
	if(!m_pRClientUsersTask)
		return;

	const int Status = m_pRClientUsersTask->StatusCode();
	if(Status == 401 || Status == 403)
	{
		ClearVoiceAuth();
		if(!ShouldIgnoreIndicatorErrors())
			GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "RClient", "Indicator GET auth rejected");
		return;
	}

	json_value *pJson = m_pRClientUsersTask->ResultJson();
	if(!pJson)
		return;

	const json_value &Json = *pJson;
	const json_value &Error = Json["error"];
	if(Error.type == json_string)
	{
		ClearVoiceAuth();
		json_value_free(pJson);
		return;
	}

	uint32_t VoiceTimestamp = 0;
	uint64_t VoiceHash = 0;
	const bool HasVoiceAuth = TryParseVoiceAuth(Json["_voice_auth"], VoiceTimestamp, VoiceHash);

	m_vRClientUsers.clear();

	if(Json.type == json_object)
	{
		const json_value &Rev = Json["_rev"];
		if(Rev.type == json_integer)
			m_ServerRev = (int)Rev.u.integer;

		for(unsigned int i = 0; i < Json.u.object.length; i++)
		{
			const char *pServerAddr = Json.u.object.values[i].name;
			const json_value &PlayersObj = *Json.u.object.values[i].value;

			if(pServerAddr[0] == '_')
				continue;

			if(PlayersObj.type != json_object)
				continue;

			for(unsigned int j = 0; j < PlayersObj.u.object.length; j++)
			{
				const char *pPlayerIdStr = PlayersObj.u.object.values[j].name;
				int PlayerId = atoi(pPlayerIdStr);
				const json_value &PlayerData = *PlayersObj.u.object.values[j].value;

				bool VoiceEnabled = false;
				bool VoiceMuted = false;
				int DummyId = -1;
				bool HasDummy = false;

				if(PlayerData.type == json_object)
				{
					for(unsigned int k = 0; k < PlayerData.u.object.length; k++)
					{
						const char *pKey = PlayerData.u.object.values[k].name;
						const json_value &Value = *PlayerData.u.object.values[k].value;
						if(str_comp(pKey, "dummy_id") == 0 && Value.type == json_integer)
						{
							DummyId = Value.u.integer;
							HasDummy = true;
						}
						else if(str_comp(pKey, "voice_enabled") == 0)
						{
							if(Value.type == json_boolean)
								VoiceEnabled = Value.u.boolean != 0;
							else if(Value.type == json_integer)
								VoiceEnabled = Value.u.integer != 0;
						}
						else if(str_comp(pKey, "voice_muted") == 0)
						{
							if(Value.type == json_boolean)
								VoiceMuted = Value.u.boolean != 0;
							else if(Value.type == json_integer)
								VoiceMuted = Value.u.integer != 0;
						}
					}
				}

				m_vRClientUsers.push_back({std::string(pServerAddr), PlayerId, VoiceEnabled, VoiceMuted});

				if(HasDummy)
					m_vRClientUsers.push_back({std::string(pServerAddr), DummyId, VoiceEnabled, VoiceMuted});
			}
		}
	}

	if(HasVoiceAuth)
	{
		m_VoiceAuthTimestamp.store(VoiceTimestamp);
		m_VoiceAuthHash.store(VoiceHash);
		m_VoiceAuthReceivedTime.store(time_get());
	}
	else
	{
		ClearVoiceAuth();
	}

	json_value_free(pJson);
}

void CRClientIndicator::ResetRClientUsers()
{
	if(m_pRClientUsersTask)
	{
		m_pRClientUsersTask->Abort();
		m_pRClientUsersTask = nullptr;
	}
}

void CRClientIndicator::ApplyPollHeaders(CHttpRequest &Request, const char *pServerAddress, int ClientId, int DummyClientId)
{
	char aServerIp[128];
	int ServerPort = 0;
	if(!ParseServerAddress(pServerAddress, aServerIp, sizeof(aServerIp), ServerPort))
		return;

	const uint32_t AuthTimestamp = (uint32_t)time_timestamp();
	char aAuthHash[32];
	if(!ComputeIndicatorAuthHex(ClientId, aServerIp, ServerPort, AuthTimestamp, aAuthHash, sizeof(aAuthHash)))
		return;

	Request.HeaderString("X-RClient-Server", pServerAddress);
	Request.HeaderInt("X-RClient-Since", m_ServerRev);
	Request.HeaderInt("X-RClient-Timeout", POLL_TIMEOUT_SECONDS);
	Request.HeaderInt("X-RClient-Voice", g_Config.m_RiVoiceEnable);
	Request.HeaderInt("X-RClient-Voice-Muted", g_Config.m_RiVoiceEnable && g_Config.m_RiVoiceMicMute);
	Request.HeaderString("X-RClient-Version", GetRclientClientVersion());
	Request.HeaderString("X-RClient-Auth-Hash", aAuthHash);
	Request.HeaderInt("X-RClient-Auth-Timestamp", (int)AuthTimestamp);
	if(ClientId >= 0)
		Request.HeaderInt("X-RClient-Player", ClientId);
	if(DummyClientId >= 0)
		Request.HeaderInt("X-RClient-Dummy", DummyClientId);
}

void CRClientIndicator::ClearVoiceAuth()
{
	m_VoiceAuthTimestamp.store(0);
	m_VoiceAuthHash.store(0);
	m_VoiceAuthReceivedTime.store(0);
}

void CRClientIndicator::ClearUsers()
{
	m_vRClientUsers.clear();
}

bool CRClientIndicator::IsPlayerRClient(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);

	for(const auto &User : m_vRClientUsers)
	{
		if(str_comp(User.m_ServerAddress.c_str(), CurrentServerInfo.m_aAddress) == 0 && User.m_PlayerId == ClientId)
			return true;
	}

	return false;
}

bool CRClientIndicator::IsPlayerRClientVoiceEnabled(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);

	for(const auto &User : m_vRClientUsers)
	{
		if(str_comp(User.m_ServerAddress.c_str(), CurrentServerInfo.m_aAddress) == 0 && User.m_PlayerId == ClientId)
			return User.m_VoiceEnabled;
	}

	return false;
}

bool CRClientIndicator::IsPlayerRClientVoiceMuted(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);

	for(const auto &User : m_vRClientUsers)
	{
		if(str_comp(User.m_ServerAddress.c_str(), CurrentServerInfo.m_aAddress) == 0 && User.m_PlayerId == ClientId)
			return User.m_VoiceMuted;
	}

	return false;
}

bool CRClientIndicator::GetCachedVoiceAuth(uint32_t &Timestamp, uint64_t &Hash, int64_t &ReceivedTime) const
{
	Timestamp = m_VoiceAuthTimestamp.load();
	Hash = m_VoiceAuthHash.load();
	ReceivedTime = m_VoiceAuthReceivedTime.load();
	return Timestamp != 0 && Hash != 0 && ReceivedTime != 0;
}
