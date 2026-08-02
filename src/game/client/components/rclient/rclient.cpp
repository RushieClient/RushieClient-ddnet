#include "rclient.h"

#include "base/str.h"
#include "engine/shared/config.h"
#include "game/client/gameclient.h"
#include "game/version.h"

#include <engine/shared/json.h>

static constexpr const char *RCLIENT_INFO_URL = "https://server.rushie-client.ru/version";

namespace ChatLayoutFix
{
	struct SChatLetter
	{
		const char *m_pWrongLetter;
		char m_LetterEnglish;
	};
	// 1-3 -- russian
	static const SChatLetter s_aLineLayout[] = {
		{"й",'q'}, {"ц", 'w'}, {"у", 'e'}, {"к", 'r'}, {"е", 't'}, {"н", 'y'}, {"г", 'u'}, {"ш", 'i'}, {"щ", 'o'}, {"з", 'p'}, {"х", '['}, {"ъ", ']'},
		{"ф",'a'}, {"ы", 's'}, {"в", 'd'}, {"а", 'f'}, {"п", 'g'}, {"р", 'h'}, {"о", 'j'}, {"л", 'k'}, {"д", 'l'}, {"ж", ';'}, {"э", '\''},
		{"я",'z'}, {"ч", 'x'}, {"с", 'c'}, {"м", 'v'}, {"и", 'b'}, {"т", 'n'}, {"ь", 'm'}, {"б", ','}, {"ю", '.'}, {"ё", '`'}
	};
}

CRClient::CRClient()
{
	CRClient::OnReset();
}

void CRClient::OnReset()
{

}

void CRClient::OnInit()
{
	FetchRClientInfo();
}

void CRClient::OnRender()
{
	if(m_pRClientDDstatsTask && m_pRClientDDstatsTask->State() == EHttpState::DONE)
	{
		FinishRclientDDstatsProfile();
		ResetRclientDDstatsProfile();
	}
	if(g_Config.m_RcPlayerClanAutoChange)
		DummyConnectedClan(Client()->DummyConnected());

	if(m_pRClientInfoTask)
	{
		if(m_pRClientInfoTask->State() == EHttpState::DONE)
		{
			FinishRClientInfo();
			ResetRClientInfoTask();
		}
	}

	if(m_pRClientDDstatsTaskFindHours && m_pRClientDDstatsTaskFindHours->State() == EHttpState::DONE)
	{
		FinishRclientDDstatsFindHours();
		ResetRclientDDstatsFindHours();
	}

	if(g_Config.m_RcRconSteamerMode)
	{
		if(GameClient()->m_GameConsole.IsActive() && GameClient()->m_GameConsole.GetConsoleType() == CGameConsole::CONSOLETYPE_REMOTE)
		{
			if(!ScreenSharePrivacyOld)
			{
				GameClient()->Graphics()->SetWindowScreenCaptureProtect(g_Config.m_RcRconSteamerMode);
				ScreenSharePrivacyOld = true;
			}
		}
		else
		{
			if(ScreenSharePrivacyOld)
			{
				GameClient()->Graphics()->SetWindowScreenCaptureProtect(0);
				ScreenSharePrivacyOld = false;
			}
		}
	}
}

void CRClient::OnConsoleInit()
{
	ConfigManager()->RegisterCallback(CRClient::ConfigSaveCallback, this, ConfigDomain::RCLIENT);
	Console()->Register("rc_find_player_from_ddstats", "s[type]", CFGFLAG_CLIENT, ConFindPlayerFromDdstats, this, "Fetch player from DDstats");
	Console()->Register("rc_find_skin_from_ddstats", "s[type]", CFGFLAG_CLIENT, ConFindSkinFromDdstats, this, "Fetch player's skin from DDstats");
	Console()->Register("rc_copy_skin_from_ddstats", "s[type]", CFGFLAG_CLIENT, ConCopySkinFromDdstats, this, "Fetch and copy player's skin from DDstats");
	Console()->Register("rc_find_skin", "r[player]", CFGFLAG_CLIENT, ConFindSkin, this, "Find skin");
	Console()->Register("rc_copy_skin", "r[player]", CFGFLAG_CLIENT, ConCopySkin, this, "Copy skin");
	Console()->Register("rc_find_player", "r[player]", CFGFLAG_CLIENT, ConFindPlayer, this, "Find Player");
	Console()->Register("rc_copy_color", "r[player]", CFGFLAG_CLIENT, ConCopyColor, this, "Copy Color skin");
	Console()->Register("rc_backup_player_profile", "", CFGFLAG_CLIENT, ConBackupPlayerProfile, this, "Backup player profile");
	Console()->Register("rc_tracker_add", "r[player]", CFGFLAG_CLIENT, ConTrackerAdd, this, "Add player to tracker");
	Console()->Register("rc_tracker_remove", "r[player]", CFGFLAG_CLIENT, ConTrackerRemove, this, "Remove player from tracker");
	Console()->Register("rc_tracker_reset", "", CFGFLAG_CLIENT, ConTrackerReset, this, "Reset tracker");
	Console()->Register("rc_toggle_deepfly", "", CFGFLAG_CLIENT, ConToggleDeepfly, this, "Toggle deepfly");
	Console()->Register("+rc_small_sens", "", CFGFLAG_CLIENT, ConToggleSmallSens, this, "small sens");
	Console()->Register("+rc_45_degrees", "", CFGFLAG_CLIENT, ConToggle45Degrees, this, "45degrees");
	Console()->Register("rc_message_filter_add_word", "s[word]", CFGFLAG_CLIENT, ConAddCensorWord, this, "Add word to censor list");
	Console()->Register("rc_message_filter_remove_word", "s[word]", CFGFLAG_CLIENT, ConRemoveCensorWord, this, "Remove word from censor list");
	Console()->Register("rc_message_filter_print_words", "", CFGFLAG_CLIENT, ConPrintCensorList, this, "Print censor list");
	Console()->Register("rc_find_hours", "s[player]", CFGFLAG_CLIENT, ConPlayerFindHours, this, "Find hours");
	Console()->Chain("rc_message_filter_mode", ConchainResetCensorListCache, this);
	Console()->Chain("rc_message_filter_multiply_change_word_on_full_match", ConchainResetCensorListCache, this);
	Console()->Chain("rc_message_filter_word_on_full_match", ConchainResetCensorListCache, this);
	Console()->Chain("rc_message_filter_multiply_change_word_on_partial_match", ConchainResetCensorListCache, this);
	Console()->Chain("rc_message_filter_word_on_partial_match", ConchainResetCensorListCache, this);
}

void CRClient::OnMessage(int MsgType, void *pRawMsg)
{
}

void CRClient::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_OFFLINE)
		ResetBinds();
}

void CRClient::OnShutdown()
{

}

void CRClient::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CRClient *pSelf = (CRClient *)pUserData;
	char aBuf[128];
	if(pSelf->m_45degreesEnabled)
	{
		str_format(aBuf, sizeof(aBuf), "inp_mousesens %d", pSelf->m_Small45OldSens);
		pConfigManager->WriteLine(aBuf, ConfigDomain::RCLIENT);
		str_format(aBuf, sizeof(aBuf), "cl_mouse_max_distance %d", pSelf->m_45degreesDistanceOld);
		pConfigManager->WriteLine(aBuf, ConfigDomain::RCLIENT);
	}
	if(pSelf->m_SmallSensEnabled)
	{
		str_format(aBuf, sizeof(aBuf), "inp_mousesens %d", pSelf->m_Small45OldSens);
		pConfigManager->WriteLine(aBuf, ConfigDomain::RCLIENT);
	}
	if(pSelf->m_DeepflyEnabled && str_find_nocase(pSelf->GameClient()->m_Binds.Get(g_Config.m_RcDeepFlyOnRMB ? KEY_MOUSE_2 : KEY_MOUSE_1, 0), "+toggle cl_dummy_hammer 1 0"))
	{
		std::string Text {pSelf->GameClient()->m_Binds.Get(g_Config.m_RcDeepFlyOnRMB ? KEY_MOUSE_2 : KEY_MOUSE_1, 0)};
		std::string ToDelete{"; +toggle cl_dummy_hammer 1 0"};
		size_t Start {Text.find(ToDelete)};
		while (Start != std::string::npos)
		{
			Text.erase(Start, ToDelete.length());
			Start = Text.find(ToDelete, Start + ToDelete.length());
		}
		str_format(aBuf, sizeof(aBuf), "bind %s \"%s\"", g_Config.m_RcDeepFlyOnRMB ? "mouse2" : "mouse1", Text.c_str());
		pConfigManager->WriteLine(aBuf, ConfigDomain::RCLIENT);
	}
	for(size_t i = 0; i < pSelf->CensorWordsList.size(); i++)
	{
		str_format(aBuf, sizeof(aBuf), "rc_message_filter_add_word %s", pSelf->CensorWordsList[i].c_str());
		pConfigManager->WriteLine(aBuf, ConfigDomain::RCLIENTCENSORLIST);
	}
}

// Need things
static int FindPlayerClientId(CGameClient *pGameClient ,const char *Nickname)
{
	int ClientID = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(str_comp_nocase(pGameClient->m_aClients[i].m_aName, Nickname) == 0)
		{
			ClientID = i;
			break;
		}
	}
	if(ClientID == -1)
	{
		if(pGameClient->m_aClients[str_toint(Nickname)].m_Active)
			ClientID = str_toint(Nickname);
	}
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		return ClientID;
	else
		return -1;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif

static void FastPrint(CGameClient *pGameClient, const char *pName, const char *pFmt, ...)
{
	char aBuf[256];
	va_list Args;
	va_start(Args, pFmt);
	str_format_v(aBuf, sizeof(aBuf), pFmt, Args);
	va_end(Args);
	pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pName, aBuf);
}

static void FastEcho(CGameClient *pGameClient, const char *pFmt, ...)
{
	char aBuf[256];
	va_list Args;
	va_start(Args, pFmt);
	str_format_v(aBuf, sizeof(aBuf), pFmt, Args);
	va_end(Args);
	pGameClient->Echo(aBuf);
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

//Version
void CRClient::FetchRClientInfo()
{
	if(m_pRClientInfoTask && !m_pRClientInfoTask->Done())
		return;
	char aUrl[256];
	str_copy(aUrl, RCLIENT_INFO_URL);
	m_pRClientInfoTask = HttpGet(aUrl);
	m_pRClientInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pRClientInfoTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pRClientInfoTask);
}

typedef std::tuple<int, int, int> TVersion;
static const TVersion gs_InvalidTCVersion = std::make_tuple(-1, -1, -1);

static TVersion ToTCVersion(char *pStr)
{
	int aVersion[3] = {0, 0, 0};
	const char *p = strtok(pStr, ".");

	for(int i = 0; i < 3 && p; ++i)
	{
		if(!str_isallnum(p))
			return gs_InvalidTCVersion;

		aVersion[i] = str_toint(p);
		p = strtok(NULL, ".");
	}

	if(p)
		return gs_InvalidTCVersion;

	return std::make_tuple(aVersion[0], aVersion[1], aVersion[2]);
}

void CRClient::FinishRClientInfo()
{
	json_value *pJson = m_pRClientInfoTask->ResultJson();
	if(!pJson)
		return;
	const json_value &Json = *pJson;
	const json_value &CurrentVersion = Json["version"];

	if(CurrentVersion.type == json_string)
	{
		char aNewVersionStr[64];
		str_copy(aNewVersionStr, CurrentVersion);
		char aCurVersionStr[64];
		str_copy(aCurVersionStr, RCLIENT_VERSION);
		if(ToTCVersion(aNewVersionStr) > ToTCVersion(aCurVersionStr))
		{
			str_copy(m_aVersionStr, CurrentVersion);
		}
		else
		{
			m_aVersionStr[0] = '0';
			m_aVersionStr[1] = '\0';
		}
		m_FetchedRClientInfo = true;
	}

	json_value_free(pJson);
}

bool CRClient::NeedUpdate()
{
	return str_comp(m_aVersionStr, "0") != 0;
}

void CRClient::ResetRClientInfoTask()
{
	if(m_pRClientInfoTask)
	{
		m_pRClientInfoTask->Abort();
		m_pRClientInfoTask = NULL;
	}
}

//Dummy clan
void CRClient::DummyConnectedClan(const bool IsDummyConnected)
{
	if(IsDummyConnected && !m_DummyConnectedPrevState)
	{
		m_DummyConnectedPrevState = IsDummyConnected;
		str_copy(g_Config.m_PlayerClan, g_Config.m_RcPlayerClanWithDummy, sizeof(g_Config.m_PlayerClan));
		GameClient()->SendInfo(false);
	}
	else if(!IsDummyConnected && m_DummyConnectedPrevState)
	{
		m_DummyConnectedPrevState = IsDummyConnected;
		str_copy(g_Config.m_PlayerClan, g_Config.m_RcPlayerClanNoDummy, sizeof(g_Config.m_PlayerClan));
		GameClient()->SendInfo(false);
	}
}


// Find/Copy Skin
static std::string TrimRight(const char *aInput)
{
	std::string Result(aInput);
	str_utf8_trim_right(Result.data());
	return Result;
}

void CRClient::ConFindPlayerFromDdstats(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	pThis->m_DDstatsSearchType = 1;
	str_copy(pThis->m_DDstatsSearchNickname, TrimRight(pResult->GetString(0)).c_str(), sizeof(pThis->m_DDstatsSearchNickname));
	pThis->FetchRclientDDstatsProfile();
}

void CRClient::ConFindSkinFromDdstats(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	pThis->m_DDstatsSearchType = 2;
	str_copy(pThis->m_DDstatsSearchNickname, TrimRight(pResult->GetString(0)).c_str(), sizeof(pThis->m_DDstatsSearchNickname));
	pThis->FetchRclientDDstatsProfile();
}

void CRClient::ConCopySkinFromDdstats(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	pThis->m_DDstatsSearchType = 3;
	str_copy(pThis->m_DDstatsSearchNickname, TrimRight(pResult->GetString(0)).c_str(), sizeof(pThis->m_DDstatsSearchNickname));
	pThis->FetchRclientDDstatsProfile();
}

void CRClient::FetchRclientDDstatsProfile()
{
	if(m_pRClientDDstatsTask && !m_pRClientDDstatsTask->Done())
	{
		return;
	}
	char aUrl[256];
	char aEncodedNickname[256];
	EscapeUrl(aEncodedNickname, sizeof(aEncodedNickname), m_DDstatsSearchNickname);
	str_format(aUrl, sizeof(aUrl), "https://ddstats.tw/profile/json?player=%s", aEncodedNickname);
	m_pRClientDDstatsTask = HttpGet(aUrl);
	m_pRClientDDstatsTask->Timeout(CTimeout{20000, 0, 500, 10});
	m_pRClientDDstatsTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pRClientDDstatsTask);
}

static void PrintPlayerInfo(CGameClient *pGameClient , const char *Nickname, const char *Skin, const char *Clan, const int Country, const int CustomColor, const int SkinColorBodyint, const int SkinColorFeetint)
{
	FastPrint(pGameClient , "Info","- Nickname: %s", Nickname);
	FastPrint(pGameClient , "Info","- Skin name: %s", Skin);
	FastPrint(pGameClient , "Info","- Clan: %s", Clan);
	FastPrint(pGameClient , "Info","- Country: %d", Country);
	if(CustomColor)
	{
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Info", "- Custom Color: 1");
		FastPrint(pGameClient , "Info","- Body Color: %d", SkinColorBodyint);
		FastPrint(pGameClient , "Info","- Feet Color: %d", SkinColorFeetint);
	}
	else
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Info", "- Custom Color: 0");
}

static void PrintSkinInfo(CGameClient *pGameClient, const char *Skin, const int CustomColor, const int SkinColorBodyint, const int SkinColorFeetint)
{
	FastPrint(pGameClient , "Info","- Skin name: %s", Skin);
	if(CustomColor)
	{
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Info", "- Custom Color: 1");
		FastPrint(pGameClient , "Info","- Body Color: %d", SkinColorBodyint);
		FastPrint(pGameClient , "Info","- Feet Color: %d", SkinColorFeetint);
	}
	else
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Info", "- Custom Color: 0");
}

static void PrintColorInfo(CGameClient *pGameClient, const int CustomColor, const int SkinColorBodyint, const int SkinColorFeetint)
{
	if(CustomColor)
	{
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Info", "- Custom Color: 1");
		FastPrint(pGameClient , "Info","- Body Color: %d", SkinColorBodyint);
		FastPrint(pGameClient , "Info","- Feet Color: %d", SkinColorFeetint);
	}
	else
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Info", "- Custom Color: 0");
}

void CRClient::ApplySkinToPlayer(const char *Skin, const int CustomColor, const int SkinColorBodyint, const int SkinColorFeetint)
{
	if(g_Config.m_ClDummy == 1)
	{
		str_copy(DummySkinBeforeCopyPlayer, g_Config.m_ClDummySkin, sizeof(DummySkinBeforeCopyPlayer));
		DummyUseCustomColorBeforeCopyPlayer = g_Config.m_ClDummyUseCustomColor;
		DummyBodyColorBeforeCopyPlayer = g_Config.m_ClDummyColorBody;
		DummyFeetColorBeforeCopyPlayer = g_Config.m_ClDummyColorFeet;
		str_copy(g_Config.m_ClDummySkin, Skin, sizeof(g_Config.m_ClDummySkin));
		g_Config.m_ClDummyUseCustomColor = CustomColor;
		g_Config.m_ClDummyColorBody = SkinColorBodyint;
		g_Config.m_ClDummyColorFeet = SkinColorFeetint;
		GameClient()->SendDummyInfo(false);
	}
	if(g_Config.m_ClDummy == 0)
	{
		str_copy(PlayerSkinBeforeCopyPlayer, g_Config.m_ClPlayerSkin, sizeof(PlayerSkinBeforeCopyPlayer));
		PlayerUseCustomColorBeforeCopyPlayer = g_Config.m_ClPlayerUseCustomColor;
		PlayerBodyColorBeforeCopyPlayer = g_Config.m_ClPlayerColorBody;
		PlayerFeetColorBeforeCopyPlayer = g_Config.m_ClPlayerColorFeet;
		str_copy(g_Config.m_ClPlayerSkin, Skin, sizeof(g_Config.m_ClPlayerSkin));
		g_Config.m_ClPlayerUseCustomColor = CustomColor;
		g_Config.m_ClPlayerColorBody = SkinColorBodyint;
		g_Config.m_ClPlayerColorFeet = SkinColorFeetint;
		GameClient()->SendInfo(false);
	}
}

void CRClient::ApplyColorToPlayer(const int CustomColor, const int SkinColorBodyint, const int SkinColorFeetint)
{
	if(g_Config.m_ClDummy == 1)
	{
		str_copy(DummySkinBeforeCopyPlayer, g_Config.m_ClDummySkin, sizeof(DummySkinBeforeCopyPlayer));
		DummyUseCustomColorBeforeCopyPlayer = g_Config.m_ClDummyUseCustomColor;
		DummyBodyColorBeforeCopyPlayer = g_Config.m_ClDummyColorBody;
		DummyFeetColorBeforeCopyPlayer = g_Config.m_ClDummyColorFeet;
		g_Config.m_ClDummyUseCustomColor = CustomColor;
		g_Config.m_ClDummyColorBody = SkinColorBodyint;
		g_Config.m_ClDummyColorFeet = SkinColorFeetint;
		GameClient()->SendDummyInfo(false);
	}
	if(g_Config.m_ClDummy == 0)
	{
		str_copy(PlayerSkinBeforeCopyPlayer, g_Config.m_ClPlayerSkin, sizeof(PlayerSkinBeforeCopyPlayer));
		PlayerUseCustomColorBeforeCopyPlayer = g_Config.m_ClPlayerUseCustomColor;
		PlayerBodyColorBeforeCopyPlayer = g_Config.m_ClPlayerColorBody;
		PlayerFeetColorBeforeCopyPlayer = g_Config.m_ClPlayerColorFeet;
		g_Config.m_ClPlayerUseCustomColor = CustomColor;
		g_Config.m_ClPlayerColorBody = SkinColorBodyint;
		g_Config.m_ClPlayerColorFeet = SkinColorFeetint;
		GameClient()->SendInfo(false);
	}
}

void CRClient::FinishRclientDDstatsProfile()
{
	json_value *pJson = m_pRClientDDstatsTask->ResultJson();
	if(!pJson)
	{
		GameClient()->Echo("No that player");
		m_DDstatsSearchType = 0;
		return;
	}
	const json_value &Json = *pJson;
	const json_value &Nickname = Json["name"];
	const json_value &Clan = Json["clan"];
	const json_value &Country = Json["country"];
	const json_value &Skin = Json["skin_name"];
	const json_value &SkinColorBody = Json["skin_color_body"];
	const json_value &SkinColorFeet = Json["skin_color_feet"];

	if(Nickname.type == json_string)
	{
		int Countryint = Country.u.integer;
		int SkinColorBodyint = SkinColorBody.u.integer;
		int SkinColorFeetint = SkinColorFeet.u.integer;
		int CustomColor = SkinColorFeetint != 0 || SkinColorBodyint != 0;
		if(m_DDstatsSearchType == 1)
			PrintPlayerInfo(GameClient(), Nickname.u.string.ptr, Skin.u.string.ptr, Clan.u.string.ptr, Countryint, CustomColor, SkinColorBodyint, SkinColorFeetint);
		if(m_DDstatsSearchType == 2)
			PrintSkinInfo(GameClient(), Skin.u.string.ptr, CustomColor, SkinColorBodyint, SkinColorFeetint);
		if(m_DDstatsSearchType == 3)
		{
			PrintSkinInfo(GameClient(), Skin.u.string.ptr, CustomColor, SkinColorBodyint, SkinColorFeetint);
			ApplySkinToPlayer(Skin.u.string.ptr, CustomColor, SkinColorBodyint, SkinColorFeetint);
		}
		m_DDstatsSearchType = 0;
	}
	json_value_free(pJson);
}

void CRClient::ResetRclientDDstatsProfile()
{
	if(m_pRClientDDstatsTask)
	{
		m_pRClientDDstatsTask->Abort();
		m_pRClientDDstatsTask = nullptr;
	}
}

void CRClient::ConFindSkin(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	int ClientId = FindPlayerClientId(pThis->GameClient(), TrimRight(pResult->GetString(0)).c_str());
	if(ClientId == -1)
	{
		pThis->GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", "Invalid client ID");
		pThis->GameClient()->Echo("No that player on server");
		return;
	}
	const CGameClient::CClientData &ClientData = pThis->GameClient()->m_aClients[ClientId];
	if(ClientData.m_aSkinName[0])
	{
		PrintSkinInfo(pThis->GameClient(), ClientData.m_aSkinName, ClientData.m_UseCustomColor, ClientData.m_ColorBody, ClientData.m_ColorFeet);
	}
}

void CRClient::ConFindPlayer(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	int ClientId = FindPlayerClientId(pThis->GameClient(), TrimRight(pResult->GetString(0)).c_str());
	if(ClientId == -1)
	{
		pThis->GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", "Invalid client ID");
		pThis->GameClient()->Echo("No that player on server");
		return;
	}
	const CGameClient::CClientData &ClientData = pThis->GameClient()->m_aClients[ClientId];
	if(ClientData.m_aSkinName[0])
	{
		PrintPlayerInfo(pThis->GameClient(), ClientData.m_aName, ClientData.m_aSkinName, ClientData.m_aClan, ClientData.m_Country, ClientData.m_UseCustomColor, ClientData.m_ColorBody, ClientData.m_ColorFeet);
	}
}

void CRClient::ConCopySkin(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	int ClientId = FindPlayerClientId(pThis->GameClient(), TrimRight(pResult->GetString(0)).c_str());
	if(ClientId == -1)
	{
		pThis->GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", "Invalid client ID");
		pThis->GameClient()->Echo("No that player on server");
		return;
	}
	const CGameClient::CClientData &ClientData = pThis->GameClient()->m_aClients[ClientId];
	if(ClientData.m_aSkinName[0])
	{
		PrintSkinInfo(pThis->GameClient(), ClientData.m_aSkinName, ClientData.m_UseCustomColor, ClientData.m_ColorBody, ClientData.m_ColorFeet);
		pThis->ApplySkinToPlayer(ClientData.m_aSkinName, ClientData.m_UseCustomColor, ClientData.m_ColorBody, ClientData.m_ColorFeet);
	}
}

void CRClient::ConCopyColor(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	int ClientId = FindPlayerClientId(pThis->GameClient(), TrimRight(pResult->GetString(0)).c_str());
	if(ClientId == -1)
	{
		pThis->GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", "Invalid client ID");
		pThis->GameClient()->Echo("No that player on server");
		return;
	}
	const CGameClient::CClientData &ClientData = pThis->GameClient()->m_aClients[ClientId];
	if(ClientData.m_aSkinName[0])
	{
		PrintColorInfo(pThis->GameClient(), ClientData.m_UseCustomColor, ClientData.m_ColorBody, ClientData.m_ColorFeet);
		pThis->ApplyColorToPlayer(ClientData.m_UseCustomColor, ClientData.m_ColorBody, ClientData.m_ColorFeet);
	}
}

void CRClient::ConBackupPlayerProfile(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pSelf = (CRClient *)pUserData;
	if(g_Config.m_ClDummy == 1)
	{
		if(str_length(pSelf->DummySkinBeforeCopyPlayer) > 0)
		{
			str_copy(g_Config.m_ClDummySkin, pSelf->DummySkinBeforeCopyPlayer, sizeof(g_Config.m_ClDummySkin));
			g_Config.m_ClDummyUseCustomColor = pSelf->DummyUseCustomColorBeforeCopyPlayer;
			g_Config.m_ClDummyColorBody = pSelf->DummyBodyColorBeforeCopyPlayer;
			g_Config.m_ClDummyColorFeet = pSelf->DummyFeetColorBeforeCopyPlayer;
			pSelf->GameClient()->SendDummyInfo(false);
		}
		else
		{
			pSelf->GameClient()->Echo("There no info of player/skin copy");
		}
	}
	if(g_Config.m_ClDummy == 0)
	{
		if(str_length(pSelf->PlayerSkinBeforeCopyPlayer) > 0)
		{
			str_copy(g_Config.m_ClPlayerSkin, pSelf->PlayerSkinBeforeCopyPlayer, sizeof(g_Config.m_ClPlayerSkin));
			g_Config.m_ClPlayerUseCustomColor = pSelf->PlayerUseCustomColorBeforeCopyPlayer;
			g_Config.m_ClPlayerColorBody = pSelf->PlayerBodyColorBeforeCopyPlayer;
			g_Config.m_ClPlayerColorFeet = pSelf->PlayerFeetColorBeforeCopyPlayer;
			pSelf->GameClient()->SendInfo(false);
		}
		else
		{
			pSelf->GameClient()->Echo("There no info of player/skin copy");
		}
	}
}

// Tracker
void CRClient::ConTrackerAdd(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	int ClientId = FindPlayerClientId(pThis->GameClient(), TrimRight(pResult->GetString(0)).c_str());
	if(ClientId == -1)
	{
		pThis->GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Tracker", "Invalid client ID");
		pThis->GameClient()->Echo("No that player on server");
		return;
	}
	const CGameClient::CClientData &ClientData = pThis->GameClient()->m_aClients[ClientId];
	if(ClientData.m_aName[0] && ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		FastPrint(pThis->GameClient(), "Tracker", "Added player: %s", ClientData.m_aName);
		FastEcho(pThis->GameClient(), "[[green]]Tracker: Added player: %s", ClientData.m_aName);
		pThis->m_vPlayersInTracker.push_back({ClientData.ClientId(), ClientData.m_aName});
	}
}
void CRClient::ConTrackerRemove(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	int ClientId = FindPlayerClientId(pThis->GameClient(), TrimRight(pResult->GetString(0)).c_str());
	if(ClientId == -1)
	{
		pThis->GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Tracker", "Invalid client ID");
		pThis->GameClient()->Echo("No that player on server");
		return;
	}
	const CGameClient::CClientData &ClientData = pThis->GameClient()->m_aClients[ClientId];
	if(ClientData.m_aName[0] && ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		for(size_t i = 0; i < pThis->m_vPlayersInTracker.size(); i++)
		{
			if(ClientData.ClientId() == pThis->m_vPlayersInTracker[i].m_ClientId)
			{
				FastPrint(pThis->GameClient(), "Tracker", "Removed player: %s", pThis->m_vPlayersInTracker[i].m_Nickname.c_str());
				FastEcho(pThis->GameClient(), "[[red]]Tracker: Removed player: %s", pThis->m_vPlayersInTracker[i].m_Nickname.c_str());
				pThis->m_vPlayersInTracker.erase(pThis->m_vPlayersInTracker.begin() + i);
				return;
			}
		}
		pThis->GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Tracker", "Player not in tracker");
		pThis->GameClient()->Echo("Player not in tracker");
	}
}
void CRClient::ConTrackerReset(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pThis = static_cast<CRClient *>(pUserData);
	for(size_t i = 0; i < pThis->m_vPlayersInTracker.size(); i++)
	{
		FastPrint(pThis->GameClient(), "Tracker", "Removed player: %s", pThis->m_vPlayersInTracker[i].m_Nickname.c_str());
		FastEcho(pThis->GameClient(), "[[red]]Tracker: Removed player: %s", pThis->m_vPlayersInTracker[i].m_Nickname.c_str());
	}
	pThis->m_vPlayersInTracker.clear();
}

void CRClient::TrackerClientIdRemove(int ClientId)
{
	for(size_t i = 0; i < m_vPlayersInTracker.size(); i++)
	{
		if(ClientId == m_vPlayersInTracker[i].m_ClientId)
		{
			FastPrint(GameClient(), "Tracker", "Removed player: %s", m_vPlayersInTracker[i].m_Nickname.c_str());
			FastEcho(GameClient(), "[[red]]Tracker: Removed player: %s", m_vPlayersInTracker[i].m_Nickname.c_str());
			m_vPlayersInTracker.erase(m_vPlayersInTracker.begin() + i);
			return;
		}
	}
}
bool CRClient::TrackerIsTracked(int ClientId)
{
	for(size_t i = 0; i < m_vPlayersInTracker.size(); i++)
	{
		if(ClientId == m_vPlayersInTracker[i].m_ClientId)
		{
			return true;
		}
	}
	return false;
}
void CRClient::TrackerClientIdAdd(int ClientId)
{
	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
	if(ClientData.m_aName[0] && ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		FastPrint(GameClient(), "Tracker", "Added player: %s", ClientData.m_aName);
		FastEcho(GameClient(), "[[green]]Tracker: Added player: %s", ClientData.m_aName);
		m_vPlayersInTracker.push_back({ClientData.ClientId(), ClientData.m_aName});
	}
}

// Binds
void CRClient::ResetBinds()
{
	if(m_45degreesEnabled)
	{
		Toggle45Degrees(false);
	}
	if(m_SmallSensEnabled)
	{
		ToggleSmallSens(false);
	}
	if(m_DeepflyEnabled && str_find_nocase(GameClient()->m_Binds.Get(g_Config.m_RcDeepFlyOnRMB ? KEY_MOUSE_2 : KEY_MOUSE_1, 0), "+toggle cl_dummy_hammer 1 0"))
	{
		ToggleDeepFly(false, GameClient()->m_Binds.Get(g_Config.m_RcDeepFlyOnRMB ? KEY_MOUSE_2 : KEY_MOUSE_1, 0));
	}
}

void CRClient::ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pSelf = static_cast<CRClient *>(pUserData);
	bool m_45degreestoggle = pResult->GetInteger(0) != 0;
	if(pSelf->m_SmallSensEnabled)
	{
		if(m_45degreestoggle && !pSelf->m_45degreestogglelastinput)
			pSelf->GameClient()->Echo("[[red]] Cant enable 45 degrees. Small send enabled");
		pSelf->m_45degreestogglelastinput = m_45degreestoggle;
		return;
	}
	if(g_Config.m_RcToggle45degrees)
	{
		if(m_45degreestoggle && !pSelf->m_45degreestogglelastinput)
		{
			if(!pSelf->m_45degreesEnabled)
			{
				pSelf->Toggle45Degrees(true);
			}
			else
			{
				pSelf->Toggle45Degrees(false);
			}
		}
		pSelf->m_45degreestogglelastinput = m_45degreestoggle;
	}
	else
	{
		if(m_45degreestoggle && !pSelf->m_45degreestogglelastinput && !pSelf->m_45degreesEnabled)
		{
			pSelf->Toggle45Degrees(true);
		}
		else if(!m_45degreestoggle && pSelf->m_45degreesEnabled)
		{
			pSelf->Toggle45Degrees(false);
		}
		pSelf->m_45degreestogglelastinput = m_45degreestoggle;
	}
}
void CRClient::Toggle45Degrees(bool Enable, bool NeedEcho)
{
	if(Enable)
	{
		m_45degreesEnabled = true;
		if(g_Config.m_Rc45degreesEcho && NeedEcho)
			GameClient()->Echo("[[green]] 45° on");
		if(m_Small45OldSens == -1)
			m_Small45OldSens = g_Config.m_InpMousesens;
		if(m_45degreesDistanceOld == -1)
			m_45degreesDistanceOld = g_Config.m_ClMouseMaxDistance;
		g_Config.m_ClMouseMaxDistance = 2;
		g_Config.m_InpMousesens = 4;
	}
	else
	{
		m_45degreesEnabled = false;
		if(g_Config.m_Rc45degreesEcho && NeedEcho)
			GameClient()->Echo("[[red]] 45° off");
		if(m_45degreesDistanceOld != -1)
		{
			g_Config.m_ClMouseMaxDistance = m_45degreesDistanceOld;
			m_45degreesDistanceOld = -1;
		}
		else
		{
			GameClient()->Echo("[[red]] Didn't find old distance. Binding 400");
			g_Config.m_ClMouseMaxDistance = 400;
			m_45degreesDistanceOld = -1;
		}
		if(m_Small45OldSens != -1)
		{
			g_Config.m_InpMousesens = m_Small45OldSens;
			m_Small45OldSens = -1;
		}
		else
		{
			GameClient()->Echo("[[red]] Didn't find old sens. Binding 100 sens");
			g_Config.m_InpMousesens = 100;
			m_Small45OldSens = -1;
		}
	}
}

void CRClient::ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pSelf = static_cast<CRClient *>(pUserData);
	bool m_SmallSenstoggle = pResult->GetInteger(0) != 0;
	if(pSelf->m_45degreesEnabled)
	{
		if(m_SmallSenstoggle && !pSelf->m_Smallsenstogglelastinput)
			pSelf->GameClient()->Echo("[[red]] Cant enable small sens. 45 degrees enabled");
		pSelf->m_Smallsenstogglelastinput = m_SmallSenstoggle;
		return;
	}
	if(g_Config.m_RcToggleSmallSens)
	{
		if(m_SmallSenstoggle && !pSelf->m_Smallsenstogglelastinput)
		{
			if(!pSelf->m_SmallSensEnabled)
			{
				pSelf->ToggleSmallSens(true);
			}
			else
			{
				pSelf->ToggleSmallSens(false);
			}
		}
		pSelf->m_Smallsenstogglelastinput = m_SmallSenstoggle;
	}
	else
	{
		if(m_SmallSenstoggle && !pSelf->m_Smallsenstogglelastinput && !pSelf->m_SmallSensEnabled)
		{
			pSelf->ToggleSmallSens(true);
		}
		else if(!m_SmallSenstoggle && pSelf->m_SmallSensEnabled)
		{
			pSelf->ToggleSmallSens(false);
		}
		pSelf->m_Smallsenstogglelastinput = m_SmallSenstoggle;
	}
}
void CRClient::ToggleSmallSens(bool Enable, bool NeedEcho)
{
	if(Enable)
	{
		m_SmallSensEnabled = true;
		if(g_Config.m_RcSmallSensEcho && NeedEcho)
			GameClient()->Echo("[[green]] small sens on");
		if(m_Small45OldSens == -1)
			m_Small45OldSens = g_Config.m_InpMousesens;
		g_Config.m_InpMousesens = 1;
	}
	else
	{
		m_SmallSensEnabled = false;
		if(g_Config.m_RcSmallSensEcho && NeedEcho)
			GameClient()->Echo("[[red]] small sens off");
		if(m_Small45OldSens != -1)
		{
			g_Config.m_InpMousesens = m_Small45OldSens;
			m_Small45OldSens = -1;
		}
		else
		{
			GameClient()->Echo("[[red]] Didn't find old sens. Binding 100 sens");
			g_Config.m_InpMousesens = 100;
			m_Small45OldSens = -1;
		}
	}
}

void CRClient::ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pSelf = static_cast<CRClient *>(pUserData);
	char CurBind[128];
	str_copy(CurBind, pSelf->GameClient()->m_Binds.Get(g_Config.m_RcDeepFlyOnRMB ? KEY_MOUSE_2 : KEY_MOUSE_1, 0), sizeof(CurBind));
	if(str_find_nocase(CurBind, "+toggle cl_dummy_hammer 1 0"))
	{
		pSelf->ToggleDeepFly(false, CurBind);
	}
	else
	{
		pSelf->ToggleDeepFly(true, CurBind);
	}
}
void CRClient::ToggleDeepFly(bool Enable, const char *CurBind, bool NeedEcho)
{
	if(!Enable)
	{
		m_DeepflyEnabled = false;
		std::string Text {CurBind};
		std::string ToDelete{"; +toggle cl_dummy_hammer 1 0"};
		size_t Start {Text.find(ToDelete)};
		while (Start != std::string::npos)
		{
			Text.erase(Start, ToDelete.length());
			Start = Text.find(ToDelete, Start + ToDelete.length());
		}
		GameClient()->m_Binds.Bind(g_Config.m_RcDeepFlyOnRMB ? KEY_MOUSE_2 : KEY_MOUSE_1, Text.c_str(), false, 0);
		GameClient()->Echo("[[red]] Deepfly off");
	}
	else
	{
		m_DeepflyEnabled = true;
		std::string Text {CurBind};
		Text.append("; +toggle cl_dummy_hammer 1 0");
		GameClient()->Echo("[[green]] Deepfly on");
		GameClient()->m_Binds.Bind(g_Config.m_RcDeepFlyOnRMB ? KEY_MOUSE_2 : KEY_MOUSE_1, Text.c_str(), false, 0);
	}
}

// Message Filter
void CRClient::ConchainResetCensorListCache(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	((CRClient *)pUserData)->m_CensorMessageListCache.clear();
}

void CRClient::ConAddCensorWord(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pSelf = static_cast<CRClient *>(pUserData);
	char aBuf[256];
	str_utf8_tolower(pResult->GetString(0), aBuf, sizeof(aBuf));
	pSelf->CensorWordsList.push_back(aBuf);
}

void CRClient::ConRemoveCensorWord(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pSelf = static_cast<CRClient *>(pUserData);
	const char *CensorWord = pResult->GetString(0);
	for(size_t i = 0; i < pSelf->CensorWordsList.size(); i++)
	{
		if(!str_utf8_comp_nocase(CensorWord, pSelf->CensorWordsList[i].c_str()))
		{
			FastPrint(pSelf->GameClient(), "Censor", "Removed word: %s", pSelf->CensorWordsList[i].c_str());
			pSelf->CensorWordsList.erase(pSelf->CensorWordsList.begin() + i);
			return;
		}
	}
}

void CRClient::ConPrintCensorList(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pSelf = static_cast<CRClient *>(pUserData);
	std::string AllWords;
	for(size_t i = 0; i < pSelf->CensorWordsList.size(); i++)
	{
		if(i != 0)
			AllWords.append(", ");
		AllWords.append(pSelf->CensorWordsList[i]);
	}
	FastPrint(pSelf->GameClient(), "All words", "%s", AllWords.c_str());
}

const char *CRClient::FilterMessage(const char *Message, bool IsChat, int ClientId)
{
	//TODO: Transfer message filter in chat.cpp
	if(g_Config.m_RcMessageFilterMode == 0)
	{
		return Message;
	}

	for(size_t i = 0; i < m_CensorMessageListCache.size(); i++)
	{
		if(!str_utf8_comp_nocase(Message, m_CensorMessageListCache[i].m_BlockedMessage.c_str()))
		{
			m_FilteredMessage = m_CensorMessageListCache[i].m_FinalMessage;
			return m_FilteredMessage.c_str();
		}
	}

	bool CensorFoundInMessage = false;
	std::string text {Message};
	if(g_Config.m_RcMessageFilterMode == 1)
	{
		for(size_t i = 0; i < CensorWordsList.size(); i++)
		{
			std::string to_delete{CensorWordsList[i]};
			const char *pFound = str_utf8_find_nocase(text.c_str(), to_delete.c_str());
			while(pFound)
			{
				CensorFoundInMessage = true;
				size_t start = pFound - text.c_str();
				if(g_Config.m_RcMessageFilterMultiplyChangeWordOnPartialMatch)
				{
					size_t CharCount = 0;
					size_t BytesCount = 0;
					str_utf8_stats(to_delete.c_str(), to_delete.size(), to_delete.size(), &BytesCount, &CharCount);
					if(strlen(g_Config.m_RcMessageFilterWordOnPartialMatch) < 2)
					{
						text.replace(start, to_delete.length(), CharCount + 1, g_Config.m_RcMessageFilterWordOnPartialMatch[0]);
						pFound = str_utf8_find_nocase(text.c_str() + start + CharCount + 1, to_delete.c_str());
					}
					else
					{
						std::string to_change;
						to_change.reserve((CharCount + 1) * strlen(g_Config.m_RcMessageFilterWordOnPartialMatch));
						for(size_t j = 0; j < CharCount + 1; j++)
							to_change += g_Config.m_RcMessageFilterWordOnPartialMatch;
						text.replace(start, to_delete.length(), to_change);
						pFound = str_utf8_find_nocase(text.c_str() + start + to_change.size(), to_delete.c_str());
					}
				}
				else
				{
					text.replace(start, to_delete.length(), g_Config.m_RcMessageFilterWordOnPartialMatch);
					pFound = str_utf8_find_nocase(text.c_str() + start + strlen(g_Config.m_RcMessageFilterWordOnPartialMatch), to_delete.c_str());
				}
			}
		}
		if(CensorFoundInMessage && IsChat && g_Config.m_RcMessageFilterPrintBlockedMessage)
		{
			std::string BlockedMessage;
			if(ClientId != -1)
			{
				BlockedMessage += GameClient()->m_aClients[ClientId].m_aName;
				BlockedMessage += " said ";
			}
			else
			{
				BlockedMessage += "Server said ";
			}
			BlockedMessage += Message;
			GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "CensorList", BlockedMessage.c_str(), g_Config.m_RcMessageFilterPrintBlockedMessageColor);
		}
		if(CensorFoundInMessage)
		{
			m_CensorMessageListCache.push_back({Message, text});
			if(m_CensorMessageListCache.size() > 25)
				m_CensorMessageListCache.erase(m_CensorMessageListCache.cbegin());
		}
		m_FilteredMessage = text;
		return m_FilteredMessage.c_str();
	}
	if(g_Config.m_RcMessageFilterMode == 2)
	{
		for(size_t i = 0; i < CensorWordsList.size(); i++)
		{
			std::string to_delete{CensorWordsList[i]};
			const char *pFound = str_utf8_find_nocase(text.c_str(), to_delete.c_str());
			while(pFound)
			{
				CensorFoundInMessage = true;
				size_t start = pFound - text.c_str();
				size_t word_start = text.find_last_of(' ', start);
				if(word_start == std::string::npos)
					word_start = 0;
				else word_start++;
				size_t word_end = text.find_first_of(' ', start + to_delete.length());
				if(word_end == std::string::npos)
					word_end = text.length();
				if(g_Config.m_RcMessageFilterMultiplyChangeWordOnFullMatch)
				{
					size_t CharCount = 0;
					size_t BytesCount = 0;
					str_utf8_stats(text.c_str() + word_start, word_end - word_start, word_end - word_start, &BytesCount, &CharCount);
					if(strlen(g_Config.m_RcMessageFilterWordOnFullMatch) < 2)
					{
						text.replace(word_start, word_end - word_start, CharCount + 1, g_Config.m_RcMessageFilterWordOnFullMatch[0]);
						pFound = str_utf8_find_nocase(text.c_str() + word_start + (CharCount + 1), to_delete.c_str());
					}
					else
					{
						std::string to_change;
						to_change.reserve((CharCount + 1) * strlen(g_Config.m_RcMessageFilterWordOnFullMatch));
						for(size_t j = 0; j < CharCount + 1; j++)
							to_change += g_Config.m_RcMessageFilterWordOnFullMatch;
						text.replace(word_start, word_end - word_start, to_change);
						pFound = str_utf8_find_nocase(text.c_str() + word_start + to_change.size(), to_delete.c_str());
					}
				}
				else
				{
					text.replace(word_start, word_end - word_start, g_Config.m_RcMessageFilterWordOnFullMatch);
					pFound = str_utf8_find_nocase(text.c_str() + word_start + strlen(g_Config.m_RcMessageFilterWordOnFullMatch), to_delete.c_str());
				}
			}
		}
		if(CensorFoundInMessage && IsChat && g_Config.m_RcMessageFilterPrintBlockedMessage)
		{
			std::string BlockedMessage;
			if(ClientId != -1)
			{
				BlockedMessage += GameClient()->m_aClients[ClientId].m_aName;
				BlockedMessage += " said ";
			}
			else
			{
				BlockedMessage += "Server said ";
			}
			BlockedMessage += Message;
			GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "CensorList", BlockedMessage.c_str(), g_Config.m_RcMessageFilterPrintBlockedMessageColor);
		}
		if(CensorFoundInMessage)
		{
			m_CensorMessageListCache.push_back({Message, text});
			if(m_CensorMessageListCache.size() > 25)
				m_CensorMessageListCache.erase(m_CensorMessageListCache.cbegin());
		}
		m_FilteredMessage = text;
		return m_FilteredMessage.c_str();
	}
	if(g_Config.m_RcMessageFilterMode == 3)
	{
		for(size_t i = 0; i < CensorWordsList.size(); i++)
		{
			std::string to_delete{CensorWordsList[i]};
			const char *pFound = str_utf8_find_nocase(text.c_str(), to_delete.c_str());
			while(pFound)
			{
				CensorFoundInMessage = true;
				size_t start = pFound - text.c_str();
				size_t word_start = text.find_last_of(' ', start);
				if(word_start == std::string::npos)
					word_start = 0;
				else word_start++;
				size_t word_end = text.find_first_of(' ', start + to_delete.length());
				if(word_end == std::string::npos)
					word_end = text.length();
				if(!str_utf8_comp_nocase(text.c_str() + word_start, to_delete.c_str()))
				{
					if(g_Config.m_RcMessageFilterMultiplyChangeWordOnFullMatch)
					{
						size_t CharCount = 0;
						size_t BytesCount = 0;
						str_utf8_stats(text.c_str() + word_start, word_end - word_start, word_end - word_start, &BytesCount, &CharCount);
						if(strlen(g_Config.m_RcMessageFilterWordOnFullMatch) < 2)
						{
							text.replace(word_start, word_end - word_start, CharCount + 1, g_Config.m_RcMessageFilterWordOnFullMatch[0]);
							pFound = str_utf8_find_nocase(text.c_str() + word_start + (CharCount + 1), to_delete.c_str());
						}
						else
						{
							std::string to_change;
							to_change.reserve((CharCount + 1) * strlen(g_Config.m_RcMessageFilterWordOnFullMatch));
							for(size_t j = 0; j < CharCount + 1; j++)
								to_change += g_Config.m_RcMessageFilterWordOnFullMatch;
							text.replace(word_start, word_end - word_start, to_change);
							pFound = str_utf8_find_nocase(text.c_str() + word_start + to_change.size(), to_delete.c_str());
						}
					}
					else
					{
						text.replace(word_start, word_end - word_start, g_Config.m_RcMessageFilterWordOnFullMatch);
						pFound = str_utf8_find_nocase(text.c_str() + word_start + strlen(g_Config.m_RcMessageFilterWordOnFullMatch), to_delete.c_str());
					}
				}
				else
				{
					if(g_Config.m_RcMessageFilterMultiplyChangeWordOnPartialMatch)
					{
						size_t CharCount = 0;
						size_t BytesCount = 0;
						str_utf8_stats(to_delete.c_str(), to_delete.size(), to_delete.size(), &BytesCount, &CharCount);
						if(strlen(g_Config.m_RcMessageFilterWordOnPartialMatch) < 2)
						{
							text.replace(start, to_delete.length(), CharCount + 1, g_Config.m_RcMessageFilterWordOnPartialMatch[0]);
							pFound = str_utf8_find_nocase(text.c_str() + start + (CharCount + 1), to_delete.c_str());
						}
						else
						{
							std::string to_change;
							to_change.reserve((CharCount + 1) * strlen(g_Config.m_RcMessageFilterWordOnPartialMatch));
							for(size_t j = 0; j < CharCount + 1; j++)
								to_change += g_Config.m_RcMessageFilterWordOnPartialMatch;
							text.replace(start, to_delete.length(), to_change);
							pFound = str_utf8_find_nocase(text.c_str() + start + to_change.size(), to_delete.c_str());
						}
					}
					else
					{
						text.replace(start, to_delete.length(), g_Config.m_RcMessageFilterWordOnPartialMatch);
						pFound = str_utf8_find_nocase(text.c_str() + start + strlen(g_Config.m_RcMessageFilterWordOnPartialMatch), to_delete.c_str());
					}
				}
			}
		}
		if(CensorFoundInMessage && IsChat && g_Config.m_RcMessageFilterPrintBlockedMessage)
		{
			std::string BlockedMessage;
			if(ClientId != -1)
			{
				BlockedMessage += GameClient()->m_aClients[ClientId].m_aName;
				BlockedMessage += " said ";
			}
			else
			{
				BlockedMessage += "Server said ";
			}
			BlockedMessage += Message;
			GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "CensorList", BlockedMessage.c_str(), g_Config.m_RcMessageFilterPrintBlockedMessageColor);
		}
		if(CensorFoundInMessage)
		{
			m_CensorMessageListCache.push_back({Message, text});
			if(m_CensorMessageListCache.size() > 25)
				m_CensorMessageListCache.erase(m_CensorMessageListCache.cbegin());
		}
		m_FilteredMessage = text;
		return m_FilteredMessage.c_str();
	}
	return Message;
}

// Translated
CRClient::CLineTranslate::CLineTranslate()
{
	m_aText[0] = '\0';
	m_WorkId = -1;
	m_JobIntVariable = -1;
}

void CRClient::DoTranslateWork(CTranslateResponse &TranslatedClass, CLineTranslate &LineForTranslate)
{
	if(TranslatedClass.m_Error)
	{
		FastPrint(GameClient(), "Translate", TranslatedClass.m_Text);
		FastEcho(GameClient(), "Error, check console");
		return;
	}

	if(LineForTranslate.m_WorkId == 0)
	{
		GameClient()->m_Chat.SendChat(LineForTranslate.m_JobIntVariable, TranslatedClass.m_Text, true);
	}

}

// WarList
bool CRClient::IsInWarlist(int ClientId, int Index)
{
	CWarDataCache &WarData = GameClient()->m_WarList.m_WarPlayers[ClientId];
	for(size_t i = 0; i < WarData.m_WarGroupMatches.size(); i++)
	{
		if(WarData.m_WarGroupMatches[i])
		{
			if(Index == (int)i)
				return true;
		}
	}
	return false;
}

// FindHours
void CRClient::ConPlayerFindHours(IConsole::IResult *pResult, void *pUserData)
{
	CRClient *pSelf = static_cast<CRClient *>(pUserData);
	pSelf->FetchRclientDDstatsFindHours(TrimRight(pResult->GetString(0)).c_str(), pResult->GetString(1));
}

void CRClient::FetchRclientDDstatsFindHours(const char *PlayerNickname, const char *WriteInChat)
{
	if(m_pRClientDDstatsTaskFindHours && !m_pRClientDDstatsTaskFindHours->Done())
		return;
	char aUrl[256];
	char Nickname[256];
	EscapeUrl(Nickname, sizeof(Nickname), PlayerNickname);
	if(!str_find_nocase("w", WriteInChat))
		FindHoursWriteInChat = true;
	else
		FindHoursWriteInChat = false;
	str_format(aUrl, sizeof(aUrl), "https://ddstats.tw/player/json?player=%s", Nickname);
	m_pRClientDDstatsTaskFindHours = HttpGet(aUrl);
	m_pRClientDDstatsTaskFindHours->Timeout(CTimeout{10000, 0, 500, 10});
	m_pRClientDDstatsTaskFindHours->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pRClientDDstatsTaskFindHours);
}

void CRClient::FinishRclientDDstatsFindHours()
{
	json_value *pJson = m_pRClientDDstatsTaskFindHours->ResultJson();
	if(!pJson)
		return;
	const json_value Json = *pJson;
	const json_value *General = json_object_get(&Json, "general_activity");
	const json_value *Profile = json_object_get(&Json, "profile");
	if(General->type == json_object && Profile->type == json_object)
	{
		const json_value *Seconds = json_object_get(General, "total_seconds_played");
		const json_value *Points = json_object_get(Profile, "points");
		const json_value *NicknameJson = json_object_get(Profile, "name");
		if(Seconds->type == json_integer && Points->type == json_integer && NicknameJson->type == json_string)
		{
			int Hours = Seconds->u.integer / 3600;
			int PointsFinal = Points->u.integer;
			const char *Nickname = NicknameJson->u.string.ptr;
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Player %s has %d hours and %d points", Nickname, Hours, PointsFinal);
			GameClient()->Echo(aBuf);
			if(FindHoursWriteInChat)
				GameClient()->m_Chat.SendChat(0, aBuf);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "FindHours", aBuf);
		}
		else
		{
			GameClient()->Echo("Invalid 'total_seconds_played' in JSON");
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "FindHours", "Invalid 'total_seconds_played' in JSON");
		}
	}
	else
	{
		GameClient()->Echo("Invalid 'general_activity' in JSON");
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "FindHours", "Invalid 'general_activity' in JSON");
	}
	json_value_free(pJson);
}

void CRClient::ResetRclientDDstatsFindHours()
{
	if(m_pRClientDDstatsTaskFindHours)
	{
		m_pRClientDDstatsTaskFindHours->Abort();
		m_pRClientDDstatsTaskFindHours = NULL;
	}
}

// Scoreboard
float CRClient::GetScoreboardHeight(bool IsDefaultRender ,bool IsBigger, int ClientId)
{
	// Default: m_ScoreboardPopupContext.m_IsLocal ? 30.0f : 60.0f
	// Default: m_ScoreboardPopupContext.m_IsLocal ? 58.5f : 87.5f
	constexpr float OuterPopupPadding = 2.0f * (1.0f + 4.0f); // popup border + margin on both sides
	constexpr float InnerMargin = 10.0f; // View.Margin(5.0f) inside PopupScoreboard
	constexpr float LabelHeight = 12.0f;
	constexpr float ItemSpacing = 2.0f;
	constexpr float ButtonHeight = 17.5f;
	constexpr float QuickActionHeight = 25.0f + ItemSpacing * 2.0f; // height of one quick-action row including spacing

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	const int LocalTeam = GameClient()->m_Teams.Team(LocalId);
	const int TargetTeam = GameClient()->m_Teams.Team(ClientId);
	const bool LocalInTeam = LocalTeam != TEAM_FLOCK && LocalTeam != TEAM_SUPER;
	const bool TargetInTeam = TargetTeam != TEAM_FLOCK && TargetTeam != TEAM_SUPER;
	const bool LocalIsTarget = LocalId == ClientId;
	int ExtraButtonRows = 0;
	if(LocalInTeam && LocalTeam == TargetTeam)
		ExtraButtonRows++; // Exit
	if(TargetInTeam && LocalTeam != TargetTeam)
		ExtraButtonRows++; // Join
	if(LocalInTeam && TargetTeam != LocalTeam)
		ExtraButtonRows++; // Invite
	if(!LocalIsTarget && LocalInTeam && TargetTeam == LocalTeam)
		ExtraButtonRows++; // Kick
	if(LocalInTeam && LocalTeam == TargetTeam)
		ExtraButtonRows++; // Lock

	// Both popup entry points currently render the same stack of buttons.
	const int ButtonRows = (IsDefaultRender ? 8 : 7) + ExtraButtonRows;

	float ScoreboardHeight = OuterPopupPadding + InnerMargin + LabelHeight;
	if(IsBigger)
	{
		ScoreboardHeight += QuickActionHeight * 2.0f; // friend/mute/emote + tracker/team/war
	}
	ScoreboardHeight += ButtonRows * (ButtonHeight + ItemSpacing * 2.0f);

	if (ExtraButtonRows != 0)
		ScoreboardHeight += ItemSpacing * 4.0f;

	return ScoreboardHeight;
}

int CRClient::GetCheckpointId()
{
	int PlayerId = -1;
	if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW && GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		const auto &Player = GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId];
		PlayerId = Player.ClientId();
	}
	else if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		PlayerId = GameClient()->m_Snap.m_LocalClientId;

	if(PlayerId != -1)
	{
		const auto &Char = GameClient()->m_Snap.m_aCharacters[PlayerId];
		if(!Char.m_Active || !Char.m_HasExtendedData)
			return -1;
		return Char.m_ExtendedData.m_TeleCheckpoint;
	}

	return -1;
}

const char *CRClient::FixLayoutLine(const char *Line)
{
	if(Line[0] != '/' && Line[0] != '.')
		return Line;

	for(size_t i = 0; i < m_FixLayoutListCache.size(); i++)
	{
		if(!str_utf8_comp_nocase(Line, m_FixLayoutListCache[i].m_FirstMessage.c_str()))
		{
			str_copy(m_LineLayoutFix, m_FixLayoutListCache[i].m_FixedMessage.c_str());
			return m_LineLayoutFix;
		}
	}

	std::string OutString;
	bool Changed = false;
	const char *pIn = Line;

	while(*pIn != '\0' && *pIn != ' ')
	{
		const ChatLayoutFix::SChatLetter *pKey = nullptr;
		for(const ChatLayoutFix::SChatLetter &Key : ChatLayoutFix::s_aLineLayout)
		{
			if(str_utf8_comp_nocase_num(pIn, Key.m_pWrongLetter, str_length(Key.m_pWrongLetter)) == 0)
			{
				pKey = &Key;
				break;
			}
		}

		if(pKey == nullptr)
		{
			OutString += *pIn;
			pIn++;
		}
		else
		{
			OutString += pKey->m_LetterEnglish;
			pIn += str_length(pKey->m_pWrongLetter);
			Changed = true;
		}
	}

	OutString += pIn;
	str_copy(m_LineLayoutFix, OutString.c_str(), sizeof(m_LineLayoutFix));
	if(Changed)
	{
		m_FixLayoutListCache.push_back({Line, m_LineLayoutFix});
		if(m_FixLayoutListCache.size() > 15)
			m_FixLayoutListCache.erase(m_FixLayoutListCache.cbegin());
	}
	return m_LineLayoutFix;
}