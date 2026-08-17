#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_RCLIENT_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_RCLIENT_H

#include "engine/shared/http.h"
#include <engine/shared/console.h>
#include <game/client/component.h>
#include <game/client/components/chat.h>

namespace ChatThings
{
	struct STranslateLangs
	{
		const char *m_LangCode;
		const char *m_LangName;
	};
}

class CRClient : public CComponent
{
	//Find/Copy Skin
	static void ConFindPlayerFromDdstats(IConsole::IResult *pResult, void *pUserData);
	static void ConFindSkinFromDdstats(IConsole::IResult *pResult, void *pUserData);
	static void ConCopySkinFromDdstats(IConsole::IResult *pResult, void *pUserData);
	static void ConFindSkin(IConsole::IResult *pResult, void *pUserData);
	static void ConCopySkin(IConsole::IResult *pResult, void *pUserData);
	static void ConFindPlayer(IConsole::IResult *pResult, void *pUserData);
	static void ConCopyColor(IConsole::IResult *pResult, void *pUserData);
	static void ConBackupPlayerProfile(IConsole::IResult *pResult, void *pUserData);
	std::shared_ptr<CHttpRequest> m_pRClientDDstatsTask = nullptr;
	void FetchRclientDDstatsProfile();
	void FinishRclientDDstatsProfile();
	void ResetRclientDDstatsProfile();
	char m_DDstatsSearchNickname[32];
	int m_DDstatsSearchType = 0; //1-FindPlayer 2-FindSkin 3-CopySkin
	char PlayerSkinBeforeCopyPlayer[42];
	int PlayerUseCustomColorBeforeCopyPlayer = 0;
	int PlayerBodyColorBeforeCopyPlayer = 0;
	int PlayerFeetColorBeforeCopyPlayer = 0;
	char DummySkinBeforeCopyPlayer[42];
	int DummyUseCustomColorBeforeCopyPlayer = 0;
	int DummyBodyColorBeforeCopyPlayer = 0;
	int DummyFeetColorBeforeCopyPlayer = 0;

	//Dummy clan
	void DummyConnectedClan(bool IsDummyConnected);
	bool m_DummyConnectedPrevState = false;

	//Tracker
	static void ConTrackerAdd(IConsole::IResult *pResult, void *pUserData);
	static void ConTrackerRemove(IConsole::IResult *pResult, void *pUserData);
	static void ConTrackerReset(IConsole::IResult *pResult, void *pUserData);

	//Binds
	static void ConToggle45Degrees(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSmallSens(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleDeepfly(IConsole::IResult *pResult, void *pUserData);
	bool m_DeepflyEnabled;
	bool m_SmallSensEnabled;
	bool m_Smallsenstogglelastinput;
	int m_Small45OldSens = -1;
	bool m_45degreesEnabled;
	int m_45degreesDistanceOld = -1;
	bool m_45degreestogglelastinput;
	void ResetBinds();

	// Message Filter
	static void ConAddCensorWord(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveCensorWord(IConsole::IResult *pResult, void *pUserData);
	static void ConPrintCensorList(IConsole::IResult *pResult, void *pUserData);
	std::vector<std::string> CensorWordsList;
	struct SCensorListCache
	{
		std::string m_BlockedMessage;
		std::string m_FinalMessage;
	};
	std::vector<SCensorListCache> m_CensorMessageListCache;
	static void ConchainResetCensorListCache(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	// FindHours
	static void ConPlayerFindHours(IConsole::IResult *pResult, void *pUserData);
	bool FindHoursWriteInChat = false;

	// Find time
	static void ConPlayerFindTime(IConsole::IResult *pResult, void *pUserData);
	char MapNameH[256];

	// Streamer mod
	bool ScreenSharePrivacyOld = false;

	//Aspect Ratio
	static void ConForceAspect(IConsole::IResult *pResult, void *pUserData);

	//Translate
	static void ConAddLanguage(IConsole::IResult *pResult, void *pUserData);
	static void ConResetLanguages(IConsole::IResult *pResult, void *pUserData);

	//Find checkpoint/finish
	static void ConGotoTeleCursor(IConsole::IResult *pResult, void *pUserData);
	static void ConGotoFinishCursor(IConsole::IResult *pResult, void *pUserData);
public:
	CRClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;
	void OnRender() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;
	void OnNewSnapshot() override;

	//Tracker
	struct SPlayerList
	{
		int m_ClientId;
		std::string m_Nickname;
	};
	std::vector<SPlayerList> m_vPlayersInTracker;
	void TrackerClientIdRemove(int ClientId);
	void TrackerClientIdAdd(int ClientId);
	bool TrackerIsTracked(int ClientId);

	//Binds
	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);
	void ToggleDeepFly(bool Enable, const char *CurBind, bool NeedEcho = true);
	void ToggleSmallSens(bool Enable, bool NeedEcho = true);
	void Toggle45Degrees(bool Enable, bool NeedEcho = true);

	// Message Filter
	const char *FilterMessage(const char *Message, bool IsChat = false, int ClientId = -1);
	std::string m_FilteredMessage;

	// Translate
	class CLineTranslate
	{
	public:
		CLineTranslate();
		char m_aText[256];
		int m_WorkId = -1; // 0-ChatSend
		int m_JobIntVariable = -1;
		std::shared_ptr<CTranslateResponse> m_pTranslateResponse;
	};
	void DoTranslateWork(CTranslateResponse &TranslatedClass, CLineTranslate &LineForTranslate);


	// Version
	std::shared_ptr<CHttpRequest> m_pRClientInfoTask = nullptr;
	void FetchRClientInfo();
	void FinishRClientInfo();
	void ResetRClientInfoTask();
	bool NeedUpdate();
	bool m_FetchedRClientInfo = false;
	char m_aVersionStr[10] = "0";

	// Warlist
	bool IsInWarlist(int ClientId, int Index);

	// Copy Skin
	void ApplySkinToPlayer(const char *Skin, const int CustomColor, const int SkinColorBodyint, const int SkinColorFeetint);
	void ApplyColorToPlayer(const int CustomColor, const int SkinColorBodyint, const int SkinColorFeetint);

	// Find Hours
	std::shared_ptr<CHttpRequest> m_pRClientDDstatsTaskFindHours = nullptr;
	void FetchRclientDDstatsFindHours(const char *PlayerNickname, const char *WriteInChat);
	void FinishRclientDDstatsFindHours();
	void ResetRclientDDstatsFindHours();

	// Find Time
	std::shared_ptr<CHttpRequest> m_pRClientDDstatsTaskFindTime = nullptr;
	void FetchRclientDDstatsFindTime(const char *PlayerNickname, const char *MapName);
	void FinishRclientDDstatsFindTime();
	void ResetRclientDDstatsFindTime();

	// Scoreboard/Chat height
	float GetScoreboardHeight(bool IsDefaultRender ,bool IsBigger, int ClientId = -1);
	float GetChatHeight(int ClientId);

	// Hud
	int GetCheckpointId();

	// Chat Commands
	const char *FixLayoutLine(const char *Line);
	char m_LineLayoutFix[256];
	struct SFixLayoutListCache
	{
		std::string m_FirstMessage;
		std::string m_FixedMessage;
	};
	std::vector<SFixLayoutListCache> m_FixLayoutListCache;

	// Reset RClient ChatBinds
	void ResetRClientChatBinds();
	bool RemoveChatBindCommand(const char *pCommand);

	// Aspect Ratio
	void SetForcedAspectRatio();

	// Translate
	std::vector<ChatThings::STranslateLangs> m_LatestLangsList;
	std::vector<const char *> s_LangDropDownNames;
	void AddNewLanguage(ChatThings::STranslateLangs Lang);
	ChatThings::STranslateLangs GetLanguageName(const char *pCode);
	void ResetLanguages();

	// Animation
	static float EaseInOutQuad(const float T) { return T == 0.0f ? 0.0f : T == 1.0f ? 1.0f : ((T < 0.5f) ? (2.0f * T * T) : (1.0f - std::pow(-2.0f * T + 2.0f, 2) / 2.0f)); }

	// Chat Checking
	void ChatCheckingMessages(CNetMsg_Sv_Chat *pMsg);

	// Anti UnSpec
	bool AntiUnSpec();
	bool ConfirmUnSpec = false;

	// Sorting players
	const CNetObj_PlayerInfo *GetSortedPlayersScoreboard(int Config, int ClientId);
	const CNetObj_PlayerInfo *GetSortedPlayersSpectator(int Config, int ClientId);
	const CNetObj_PlayerInfo **GetSortedPlayersSpectatorArray(int Config);
};

#endif //GAME_CLIENT_COMPONENTS_RCLIENT_RCLIENT_H
