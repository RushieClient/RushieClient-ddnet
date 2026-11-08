#include "rclient_include.h"

#include <base/math.h>
#include <base/time.h>
#include <base/types.h>

#include <engine/font_icons.h>

#include <game/client/animstate.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

enum
{
	RCLIENT_TAB_SETTINGS = 0,
	RCLIENT_TAB_RCON,
	RCLIENT_TAB_BINDCHAT,
	RCLIENT_TAB_SPECWHEEL,
	RCLIENT_TAB_INFO,
	NUMBER_OF_RCLIENT_TABS
};

static class CMenusRClientConfirmAspect : public SPopupMenuId
{
public:
	CUi *m_pUi = nullptr;
	CGameClient *m_pGameClient = nullptr;
	CButtonContainer m_ConfirmButton;
	CButtonContainer m_DenyButton;
	int m_OldAspectX;
	int m_OldAspectY;
	int64_t m_Timeout;
	static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active);
} s_AspectConfirmPopupContext;

const float FontSize = 14.0f;
const float EditBoxFontSize = 12.0f;
const float LineSize = 20.0f;
const float HeadlineFontSize = 20.0f;

const float HeadlineHeight = HeadlineFontSize + 0.0f;
const float Margin = 10.0f;
const float MarginSmall = 5.0f;
const float MarginExtraSmall = 2.5f;
const float MarginBetweenSections = 30.0f;
const float MarginBetweenViews = 30.0f;

// Height consumed by each common menu row, so tab sizes can be written in terms of widgets.
static float TabHeightCheckBox(int Count = 1) { return LineSize * Count; }
static float TabHeightRadioMenu(int Count = 1) { return (LineSize + 2.0f) * Count; }
static float TabHeightKeyReader(int Count = 1) { return (LineSize + MarginExtraSmall) * Count; }
static float TabHeightEditBox(int Count = 1) { return (LineSize + MarginExtraSmall) * Count; }
static float TabHeightLine(int Count = 1) { return LineSize * Count; }

static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

void CMenus::RenderSettingsRClient(CUIRect MainView)
{
	if(m_VoiceUsersPage)
	{
		RenderSettingsRClientVoiceUsers(MainView);
		return;
	}
	if(m_FavoritesMapsPage)
	{
		RenderSettingsRClientFavoritesMaps(MainView);
		return;
	}
	static int s_CurCustomTab = 0;

	CUIRect TabBar, Button;
	int TabCount = NUMBER_OF_RCLIENT_TABS;
	for(int Tab = 0; Tab < NUMBER_OF_RCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_RcRClientSettingsTabs, Tab))
		{
			TabCount--;
			if(s_CurCustomTab == Tab)
				s_CurCustomTab++;
		}
	}

	MainView.HSplitTop(LineSize, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (TabCount - !Client()->RconAuthed());
	static CButtonContainer s_aPageTabs[NUMBER_OF_RCLIENT_TABS] = {};
	const char *apTabNames[] = {
		RCLocalize("Settings", "RClient"),
		RCLocalize("Rcon", "RClient"),
		RCLocalize("Chat Binds", "RClient"),
		RCLocalize("Spec Wheel", "RClient"),
		RCLocalize("Info", "RClient")};

	for(int Tab = 0; Tab < NUMBER_OF_RCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_RcRClientSettingsTabs, Tab))
			continue;

		if(Tab == RCLIENT_TAB_RCON && !Client()->RconAuthed())
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : Tab == NUMBER_OF_RCLIENT_TABS - 1 ? IGraphics::CORNER_R :
													 IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurCustomTab = Tab;
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	if(s_CurCustomTab == RCLIENT_TAB_SETTINGS)
		RenderSettingsRClientSettings(MainView);
	if(s_CurCustomTab == RCLIENT_TAB_RCON)
		RenderSettingsRClientRcon(MainView);
	if(s_CurCustomTab == RCLIENT_TAB_BINDCHAT)
		RenderSettingsRClientChatBinds(MainView);
	if(s_CurCustomTab == RCLIENT_TAB_SPECWHEEL)
		RenderSettingsRClientSpecWheel(MainView);
	if(s_CurCustomTab == RCLIENT_TAB_INFO)
		RenderSettingsRClientInfo(MainView);
}

void CMenus::RenderSettingsRClientVoiceUsers(CUIRect MainView)
{
	CRCVoiceChat &Voice = GameClient()->m_RcVoiceChat;
	CUIRect Header, BackButton;
	MainView.HSplitTop(LineSize, &Header, &MainView);
	Header.VSplitLeft(80.0f, &BackButton, &Header);
	static CButtonContainer s_BackButton;
	if(DoButton_Menu(&s_BackButton, RCLocalize("Back", "RClient"), 0, &BackButton))
	{
		m_VoiceUsersPage = false;
		return;
	}
	Header.VSplitRight(80.0f, &Header, nullptr);
	Ui()->DoLabel(&Header, RCLocalize("Voice users", "RClient"), 18.0f, TEXTALIGN_MC);
	MainView.HSplitTop(Margin, nullptr, &MainView);

	static int s_Tab = 0;
	static CButtonContainer s_aTabs[2];
	const char *apTabs[] = {RCLocalize("Connected", "RClient"), RCLocalize("Saved", "RClient")};
	const int OldTab = s_Tab;
	DoMenuSettingsBar(&MainView, apTabs, 2, s_aTabs, s_Tab, LineSize);
	if(OldTab != s_Tab)
		Ui()->SetActiveItem(nullptr);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	struct SRow
	{
		std::string m_Key;
		std::string m_Name;
		bool m_Talking = false;
		int m_ClientId = -1;
	};
	std::vector<SRow> vRows;
	const auto vSaved = Voice.SavedUsers();
	if(s_Tab == 0 && Voice.Connected())
	{
		for(const auto &Participant : Voice.Participants())
		{
			const int Id = Voice.ClientIdOf(Participant.m_Identity);
			if(Id >= 0 && (Id == GameClient()->m_aLocalIds[0] || Id == GameClient()->m_aLocalIds[1] || Id == GameClient()->m_Snap.m_LocalClientId))
				continue;
			vRows.push_back({Participant.m_Identity, Voice.ParticipantName(Participant), Participant.m_Talking, Id});
		}
		std::sort(vRows.begin(), vRows.end(), [](const SRow &A, const SRow &B) {
			const auto KeyA = VoiceLogic::CUserSettings::Key(A.m_Name.c_str());
			const auto KeyB = VoiceLogic::CUserSettings::Key(B.m_Name.c_str());
			return KeyA != KeyB ? KeyA < KeyB : A.m_Key < B.m_Key;
		});
	}
	else if(s_Tab == 1)
	{
		for(const auto &User : vSaved)
		{
			int ClientId = -1;
			const auto NameKey = VoiceLogic::CUserSettings::Key(User.m_Name.c_str());
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				if(GameClient()->m_aClients[Id].m_Active && VoiceLogic::CUserSettings::Key(GameClient()->m_aClients[Id].m_aName) == NameKey)
				{
					ClientId = Id;
					break;
				}
			}
			vRows.push_back({NameKey, User.m_Name, false, ClientId});
		}
	}

	// Row IDs and selections follow identities/names rather than list indices,
	// so a join or leave cannot redirect a user's volume edit to another row.
	static std::string s_aSelected[2];
	struct SRowIds
	{
		CButtonContainer m_Select;
		int m_Volume = 0;
		CButtonContainer m_Mute;
	};
	static std::map<std::string, SRowIds> s_aRowIds[2];
	static CListBox s_aLists[2];
	auto &SelectedKey = s_aSelected[s_Tab];
	auto &RowIds = s_aRowIds[s_Tab];
	std::set<std::string> Keys;
	int Selected = -1;
	for(size_t i = 0; i < vRows.size(); ++i)
	{
		Keys.insert(vRows[i].m_Key);
		if(vRows[i].m_Key == SelectedKey)
			Selected = i;
	}
	for(auto It = RowIds.begin(); It != RowIds.end();)
	{
		if(!Keys.contains(It->first))
		{
			// Cancel a drag before its widget ID is destroyed by a participant leaving.
			Ui()->SetActiveItem(nullptr);
			It = RowIds.erase(It);
		}
		else
			++It;
	}
	if(Selected == -1 && !SelectedKey.empty())
	{
		SelectedKey.clear();
		Ui()->SetActiveItem(nullptr);
	}

	CUIRect Controls, List;
	MainView.HSplitBottom(36.0f, &List, &Controls);
	List.HSplitBottom(Margin, &List, nullptr);
	auto &ListBox = s_aLists[s_Tab];
	ListBox.DoStart(40.0f, vRows.size(), 1, 3, Selected, &List, true, IGraphics::CORNER_ALL, true);
	for(const auto &Row : vRows)
	{
		auto &Ids = RowIds[Row.m_Key];
		const CListboxItem Item = ListBox.DoNextItem(&Ids.m_Select);
		if(!Item.m_Visible)
			continue;
		const auto Settings = Voice.UserSettings(Row.m_Name.c_str());
		CUIRect Name, Skin, VolumeRow, MuteButton;
		Item.m_Rect.VMargin(8.0f, &Name);
		Name.VSplitRight(110.0f, &Name, &MuteButton);
		Name.VSplitRight(Margin, &Name, nullptr);
		Name.VSplitRight(Name.w * 0.52f, &Name, &VolumeRow);
		Name.VSplitRight(Margin, &Name, nullptr);
		Name.VSplitLeft(36.0f, &Skin, &Name);
		CTeeRenderInfo TeeInfo;
		if(Row.m_ClientId >= 0 && Row.m_ClientId < MAX_CLIENTS && GameClient()->m_aClients[Row.m_ClientId].m_Active)
			TeeInfo = GameClient()->m_aClients[Row.m_ClientId].m_RenderInfo;
		else
			TeeInfo.Apply(GameClient()->m_Skins.Find("default"));
		TeeInfo.m_Size = 32.0f;
		vec2 Offset;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, Offset);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(Skin.x + Skin.w / 2.0f, Skin.y + Skin.h / 2.0f + Offset.y));
		TextRender()->TextColor(Settings.m_Muted ? ColorRGBA(1.0f, 0.55f, 0.55f, 1.0f) : (Row.m_Talking ? ColorRGBA(0.4f, 1.0f, 0.4f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)));
		SLabelProperties NameProps;
		NameProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Name, Row.m_Name.c_str(), 14.0f, TEXTALIGN_ML, NameProps);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		VolumeRow.HMargin((VolumeRow.h - LineSize) / 2.0f, &VolumeRow);
		MuteButton.HMargin((MuteButton.h - LineSize) / 2.0f, &MuteButton);
		const int Volume = round_to_int(Ui()->DoScrollbarH(&Ids.m_Volume, &VolumeRow, Settings.m_Volume / 200.0f) * 200.0f);
		if(Volume != Settings.m_Volume)
			Voice.SetUserSettings(Row.m_Name.c_str(), Volume, Settings.m_Muted);
		char aVolumeLabel[64];
		str_format(aVolumeLabel, sizeof(aVolumeLabel), "%s: %d%%", RCLocalize("Volume", "RClient"), Volume);
		const ColorRGBA MuteColor = Settings.m_Muted ? ColorRGBA(1.0f, 0.35f, 0.35f, 0.6f) : ColorRGBA(0.35f, 0.85f, 0.35f, 0.6f);
		if(DoButton_Menu(&Ids.m_Mute, aVolumeLabel, 0, &MuteButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, MuteColor))
			Voice.SetUserSettings(Row.m_Name.c_str(), Volume, !Settings.m_Muted);
		GameClient()->m_Tooltips.DoToolTip(&Ids.m_Mute, &MuteButton, Settings.m_Muted ? RCLocalize("Unmute voice", "RClient") : RCLocalize("Mute voice", "RClient"));
	}
	const int NewSelected = ListBox.DoEnd();
	if(NewSelected >= 0 && (size_t)NewSelected < vRows.size())
	{
		SelectedKey = vRows[NewSelected].m_Key;
		Selected = NewSelected;
	}
	if(vRows.empty())
		Ui()->DoLabel(&List, s_Tab == 0 ? RCLocalize("No connected users", "RClient") : RCLocalize("No saved users", "RClient"), 14.0f, TEXTALIGN_MC);

	Controls.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 5.0f);
	Controls.Margin(8.0f, &Controls);
	if(Selected < 0 || (size_t)Selected >= vRows.size())
	{
		Ui()->DoLabel(&Controls, RCLocalize("Select a user", "RClient"), 14.0f, TEXTALIGN_MC);
		return;
	}
	const std::string Name = vRows[Selected].m_Name;
	auto Settings = Voice.UserSettings(Name.c_str());
	const auto NameKey = VoiceLogic::CUserSettings::Key(Name.c_str());
	const bool Saved = std::any_of(vSaved.begin(), vSaved.end(), [&](const auto &User) { return VoiceLogic::CUserSettings::Key(User.m_Name.c_str()) == NameKey; });
	CUIRect NameRow, SaveButton;
	Controls.HSplitTop(LineSize, &NameRow, &Controls);
	NameRow.VSplitRight(95.0f, &NameRow, &SaveButton);
	Ui()->DoLabel(&NameRow, Name.c_str(), 14.0f, TEXTALIGN_ML);
	static CButtonContainer s_SaveButton;
	if(DoButton_Menu(&s_SaveButton, Saved ? RCLocalize("Remove", "RClient") : RCLocalize("Save", "RClient"), 0, &SaveButton))
	{
		if(Saved)
		{
			Voice.ForgetUser(Name.c_str());
			if(s_Tab == 1)
				SelectedKey.clear();
			return;
		}
		Voice.SetUserSettings(Name.c_str(), Settings.m_Volume, Settings.m_Muted);
	}
}

void CMenus::RenderSettingsRClientSettings(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	const float ScrollOffset = MainView.y;
	static float s_PrevScrollOffset = 0.0f;

	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset - ScrollOffset;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** LeftView ***** //
	Column = LeftView;

	// ***** Dummy***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Dummy", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		enum
		{
			DUMMY_TAB_POS = 0,
			DUMMY_TAB_CLAN,
			NUMBER_OF_DUMMY_TABS
		};

		static int s_CurDummyCustomTab = 0;
		static CButtonContainer s_aPageTabs[NUMBER_OF_DUMMY_TABS] = {};
		const char *apTabNames[NUMBER_OF_DUMMY_TABS] = {
			RCLocalize("Position", "RClient"),
			RCLocalize("Clan Change", "RClient")};

		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_DUMMY_TABS, s_aPageTabs, s_CurDummyCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		// Position: 4 checkboxes
		// Clan Change: 2 margins + checkbox + 2 edit boxes + margin
		const float MBiggestTab = std::max(
			TabHeightCheckBox(4),
			MarginSmall * 2.0f + TabHeightCheckBox() + TabHeightEditBox(2) + MarginExtraSmall);
		const float MCurrentY = Column.y;
		if(s_CurDummyCustomTab == DUMMY_TAB_POS)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyPosition, RCLocalize("Show dummy position", "RClient"), &g_Config.m_TcShowhudDummyPosition, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummySpeed, RCLocalize("Show dummy speed", "RClient"), &g_Config.m_TcShowhudDummySpeed, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyAngle, RCLocalize("Show dummy target angle", "RClient"), &g_Config.m_TcShowhudDummyAngle, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudAdvancedDummyActions, RCLocalize("Show advanced dummy actions", "RClient"), &g_Config.m_RcShowhudAdvancedDummyActions, &Column, LineSize);
		}
		if(s_CurDummyCustomTab == DUMMY_TAB_CLAN)
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcPlayerClanAutoChange, RCLocalize("Auto change clan", "RClient"), &g_Config.m_RcPlayerClanAutoChange, &Column, LineSize);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			{
				CUIRect Box;
				Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
				Box.VSplitMid(&Label, &Button);
				Ui()->DoLabel(&Label, RCLocalize("With Dummy", "RClient"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_RcPlayerClanWithDummy, sizeof(g_Config.m_RcPlayerClanWithDummy));
				s_LineInput.SetEmptyText("#YESDUMMY");
				Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
			}
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			{
				CUIRect Box;
				Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
				Box.VSplitMid(&Label, &Button);
				Ui()->DoLabel(&Label, RCLocalize("Without Dummy", "RClient"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_RcPlayerClanNoDummy, sizeof(g_Config.m_RcPlayerClanNoDummy));
				s_LineInput.SetEmptyText("#NODUMMY");
				Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
			}
		}
		if(MBiggestTab > Column.y - MCurrentY)
			Column.HSplitTop(MBiggestTab - (Column.y - MCurrentY), nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Scoreboard ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Scoreboard", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		enum
		{
			SCOREBOARD_TAB_POPUP = 0,
			SCOREBOARD_TAB_FRIENDS,
			NUMBER_OF_SCOREBOARD_TABS
		};

		static int s_CurScoreboardCustomTab = 0;
		static CButtonContainer s_aPageTabs[NUMBER_OF_SCOREBOARD_TABS] = {};
		const char *apTabNames[NUMBER_OF_SCOREBOARD_TABS] = {
			RCLocalize("Popup", "RClient"),
			RCLocalize("Friends", "RClient")};

		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_SCOREBOARD_TABS, s_aPageTabs, s_CurScoreboardCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		// Popup: key reader + 2 checkboxes
		// Friends: checkbox + slider
		const float MBiggestTab = std::max(
			TabHeightKeyReader() + TabHeightCheckBox(2),
			TabHeightCheckBox() + TabHeightLine());
		const float MCurrentY = Column.y;
		if(s_CurScoreboardCustomTab == SCOREBOARD_TAB_POPUP)
		{
			static CButtonContainer s_ReaderButtonScoreboardMouse, s_ClearButtonScoreboardmouse;
			DoLine_KeyReader(Column, s_ReaderButtonScoreboardMouse, s_ClearButtonScoreboardmouse, RCLocalize("Enable scoreboard mouse", "RClient"), "toggle_scoreboard_cursor");
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcScoreboardAlwaysShowQuickActions, RCLocalize("Always show quick actions in popup", "RClient"), &g_Config.m_RcScoreboardAlwaysShowQuickActions, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcScoreboardFreezeInputs, RCLocalize("Freeze inputs when popup opened", "RClient"), &g_Config.m_RcScoreboardFreezeInputs, &Column, LineSize);
		}
		if(s_CurScoreboardCustomTab == SCOREBOARD_TAB_FRIENDS)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowHeartInScoreboard, RCLocalize("Show friend's heart in scoreboard", "RClient"), &g_Config.m_RcShowHeartInScoreboard, &Column, LineSize);
			Column.HSplitTop(LineSize, &Button, &Column);
			static int s_HeartSizeScoreboardId = 0;
			Ui()->DoScrollbarOption(&s_HeartSizeScoreboardId, &g_Config.m_RcSizeOfHeart, &Button, RCLocalize("Heart size", "RClient"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
		}
		if(MBiggestTab > Column.y - MCurrentY)
			Column.HSplitTop(MBiggestTab - (Column.y - MCurrentY), nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Players ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Players", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		enum
		{
			PLAYERS_TAB_EFFECTS = 0,
			PLAYERS_TAB_AFK,
			PLAYERS_TAB_HITBOX,
			NUMBER_OF_PLAYERS_TABS
		};

		static int s_CurPlayersCustomTab = 0;
		static CButtonContainer s_aPageTabs[NUMBER_OF_PLAYERS_TABS] = {};
		const char *apTabNames[NUMBER_OF_PLAYERS_TABS] = {
			RCLocalize("Effects", "RClient"),
			RCLocalize("AFK", "RClient"),
			RCLocalize("Hitbox", "RClient")};
		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_PLAYERS_TABS, s_aPageTabs, s_CurPlayersCustomTab, LineSize);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(Margin, &Label, &Column);
		if(s_CurPlayersCustomTab == PLAYERS_TAB_HITBOX)
		{
			Label.VSplitRight(Column.w / (float)NUMBER_OF_PLAYERS_TABS * 2, nullptr, &Label);
			Label.VSplitRight(Column.w / (float)NUMBER_OF_PLAYERS_TABS, &Label, nullptr);
			Ui()->DoLabel(&Label, RCLocalize("idea from BestClient", "RClient"), Margin, TEXTALIGN_MC);
		}

		// Effects: 2 checkboxes + 2 radio menus
		// AFK: 4 checkboxes
		// Hitbox: radio menu + 3 rows
		const float MBiggestTab = std::max({
			TabHeightCheckBox(2) + TabHeightRadioMenu(2),
			TabHeightCheckBox(4),
			TabHeightRadioMenu() + TabHeightLine(3),
		});
		const float MCurrentY = Column.y;
		if(s_CurPlayersCustomTab == PLAYERS_TAB_EFFECTS)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcHideFrozenFlakesEffect, RCLocalize("Hide frozen flakes", "RClient"), &g_Config.m_RcHideFrozenFlakesEffect, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowSparkleEffect, RCLocalize("Always show sparkles", "RClient"), &g_Config.m_RcShowSparkleEffect, &Column, LineSize);
			static std::vector<CButtonContainer> s_vButtonContainersStepEffects = {{}, {}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Show step effects", "RClient"),
				s_vButtonContainersStepEffects,
				{RCLocalize("Off", "RClient"), RCLocalize("Others", "RClient"), RCLocalize("Everyone", "RClient"), RCLocalize("Own", "RClient")},
				{0, 1, 2, 3},
				g_Config.m_RcShowStepEffects);
			static std::vector<CButtonContainer> s_vButtonContainersHammerHitEffect = {{}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Remove hammer hit effect", "RClient"),
				s_vButtonContainersHammerHitEffect,
				{RCLocalize("Default", "RClient"), RCLocalize("Visual", "RClient"), RCLocalize("Full", "RClient")},
				{0, 1, 2},
				g_Config.m_RcRemoveHammerHitEffect);
		}
		if(s_CurPlayersCustomTab == PLAYERS_TAB_AFK)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAfkEmoteInMenu, RCLocalize("Show AFK emote in menu", "RClient"), &g_Config.m_RcShowAfkEmoteInMenu, &Column, LineSize);
			{
				CUIRect RightSide;
				Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAfkTextureInMenu, RCLocalize("Show texture instead emote in menu", "RClient"), &g_Config.m_RcShowAfkTextureInMenu, &RightSide, LineSize);
				Column.HSplitTop(LineSize, nullptr, &Column);
			}
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAfkEmoteInSpec, RCLocalize("Show AFK emote in spec", "RClient"), &g_Config.m_RcShowAfkEmoteInSpec, &Column, LineSize);
			{
				CUIRect RightSide;
				Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAfkTextureInSpec, RCLocalize("Show texture instead emote in spec", "RClient"), &g_Config.m_RcShowAfkTextureInSpec, &RightSide, LineSize);
				Column.HSplitTop(LineSize, nullptr, &Column);
			}
		}
		if(s_CurPlayersCustomTab == PLAYERS_TAB_HITBOX)
		{
			static std::vector<CButtonContainer> s_vButtonContainersPlayersHitbox = {{}, {}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Show player hitbox", "RClient"),
				s_vButtonContainersPlayersHitbox,
				{RCLocalize("Off", "RClient"), RCLocalize("Others", "RClient"), RCLocalize("Everyone", "RClient"), RCLocalize("Own", "RClient")},
				{0, 1, 2, 3},
				g_Config.m_RcShowHitbox);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcShowHitboxSize, &g_Config.m_RcShowHitboxSize, &Button, RCLocalize("Size of hitbox", "RClient"), 1, 100, &CUi::ms_LinearScrollbarScale, 0);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcShowHitboxQuality, &g_Config.m_RcShowHitboxQuality, &Button, RCLocalize("Quality of hitbox", "RClient"), 1, 32, &CUi::ms_LinearScrollbarScale, 0);
			static CButtonContainer s_HitboxColor;
			DoButton_ColorPickerAutoVMargin(&s_HitboxColor, RCLocalize("Hitbox color", "RClient"), &g_Config.m_RcShowHitboxColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcShowHitboxColor)), &Column, LineSize, true);
		}
		if(MBiggestTab > Column.y - MCurrentY)
			Column.HSplitTop(MBiggestTab - (Column.y - MCurrentY), nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Chat Bubbles ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Chat Bubbles", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(Margin, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("by qxdFox/Entity Client", "RClient"), Margin, TEXTALIGN_MC);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatBubbles, RCLocalize("Enable Chat Bubbles", "RClient"), &g_Config.m_RcChatBubbles, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatBubblesSelfInput, RCLocalize("Show Chat Bubbles Yourself Input", "RClient"), &g_Config.m_RcChatBubblesSelfInput, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatBubblesSelfBubble, RCLocalize("Show Chat Bubbles Above Yourself", "RClient"), &g_Config.m_RcChatBubblesSelfBubble, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcChatBubbleSize, &g_Config.m_RcChatBubbleSize, &Button, RCLocalize("Chat bubble size", "RClient"), 15, 30, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcChatBubbleShowTime, &g_Config.m_RcChatBubbleShowTime, &Button, RCLocalize("Show time", "RClient"), 200, 1000, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcChatBubbleFadeOut, &g_Config.m_RcChatBubbleFadeOut, &Button, RCLocalize("Fade out time", "RClient"), 15, 100, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcChatBubbleFadeIn, &g_Config.m_RcChatBubbleFadeIn, &Button, RCLocalize("Fade in time", "RClient"), 15, 100, &CUi::ms_LinearScrollbarScale, 0);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Master servers ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Master Servers", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcUseRushieMasterServerMirrors, RCLocalize("Use Rushie master server mirror", "RClient"), &g_Config.m_RcUseRushieMasterServerMirrors, &Column, LineSize);
	if(g_Config.m_RcUseRushieMasterServerMirrors)
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcUseBestClientMasterServerMirrors, RCLocalize("Use BestClient master server mirror", "RClient"), &g_Config.m_RcUseBestClientMasterServerMirrors, &Column, LineSize);
	else
		Column.HSplitTop(LineSize, nullptr, &Column);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Integrations", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcDiscordRPC, RCLocalize("Discord RPC", "RClient"), &g_Config.m_TcDiscordRPC, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSteam, RCLocalize("Steam", "RClient"), &g_Config.m_RcSteam, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Aspect Ratio", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomAspectEnabled, RCLocalize("Enable custom aspect ratio", "RClient"), &g_Config.m_RcCustomAspectEnabled, &Column, LineSize);
	float BoxSize = (Column.w - Margin * 2) / 3;
	CUIRect Boxes, ToolTipsLabel;
	Column.HSplitTop(LineSize, &Boxes, &Column);
	Column.HSplitTop(Margin, &ToolTipsLabel, &Column);
	Boxes.VSplitLeft(BoxSize, &Button, &Boxes);
	ToolTipsLabel.VSplitLeft(BoxSize, &Label, &ToolTipsLabel);
	Ui()->DoLabel(&Label, RCLocalize("Width", "RClient"), Margin, TEXTALIGN_MC);
	static int s_ScreenWidthAspect = g_Config.m_RcCustomAspectX;
	static CLineInputNumber s_LineInputAspectWidth(s_ScreenWidthAspect);
	s_LineInputAspectWidth.SetEmptyText("16");
	if(!s_LineInputAspectWidth.IsActive())
		s_LineInputAspectWidth.SetInteger(s_ScreenWidthAspect);
	if(Ui()->DoEditBox(&s_LineInputAspectWidth, &Button, EditBoxFontSize))
		s_ScreenWidthAspect = std::clamp(s_LineInputAspectWidth.GetInteger(), 1, 10000);
	Boxes.VSplitLeft(Margin, &Button, &Boxes);
	Ui()->DoLabel(&Button, "/", LineSize, TEXTALIGN_MC);
	Boxes.VSplitLeft(BoxSize, &Button, &Boxes);
	ToolTipsLabel.VSplitLeft(Margin, nullptr, &ToolTipsLabel);
	ToolTipsLabel.VSplitLeft(BoxSize, &Label, &ToolTipsLabel);
	Ui()->DoLabel(&Label, RCLocalize("Height", "RClient"), Margin, TEXTALIGN_MC);
	static int s_ScreenHeightAspect = g_Config.m_RcCustomAspectY;
	static CLineInputNumber s_LineInputAspectHeight(s_ScreenHeightAspect);
	s_LineInputAspectHeight.SetEmptyText("9");
	if(!s_LineInputAspectHeight.IsActive())
		s_LineInputAspectHeight.SetInteger(s_ScreenHeightAspect);
	if(Ui()->DoEditBox(&s_LineInputAspectHeight, &Button, EditBoxFontSize))
		s_ScreenHeightAspect = std::clamp(s_LineInputAspectHeight.GetInteger(), 1, 10000);
	Boxes.VSplitLeft(Margin, &Button, &Boxes);
	Ui()->DoLabel(&Button, "→", LineSize, TEXTALIGN_MC);
	Boxes.VSplitLeft(BoxSize, &Button, &Boxes);
	static CButtonContainer s_ApplyBtnAspect;
	if(DoButton_Menu(&s_ApplyBtnAspect, RCLocalize("Apply", "RClient"), 0, &Button))
	{
		const float AspectConfirmTimeoutSec = 10.0f;
		s_AspectConfirmPopupContext.m_pUi = Ui();
		s_AspectConfirmPopupContext.m_pGameClient = GameClient();
		s_AspectConfirmPopupContext.m_OldAspectX = g_Config.m_RcCustomAspectX;
		s_AspectConfirmPopupContext.m_OldAspectY = g_Config.m_RcCustomAspectY;
		g_Config.m_RcCustomAspectX = s_ScreenWidthAspect;
		g_Config.m_RcCustomAspectY = s_ScreenHeightAspect;
		GameClient()->m_RClient.SetForcedAspectRatio();
		s_AspectConfirmPopupContext.m_Timeout = time_get() + (int64_t)(AspectConfirmTimeoutSec * time_freq());

		const CUIRect *pScreen = Ui()->Screen();
		const float PopupW = 300.0f;
		const float PopupH = 120.0f;

		SPopupMenuProperties MProps;
		MProps.m_CloseAtClickOutside = false;
		MProps.m_CloseAtEscape = false;
		Ui()->DoPopupMenu(&s_AspectConfirmPopupContext, pScreen->w / 2.0f - PopupW / 2.0f, pScreen->h / 2.0f - PopupH / 2.0f, PopupW, PopupH, &s_AspectConfirmPopupContext, CMenusRClientConfirmAspect::Render, MProps);
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableAll = 0;
	if(DoButton_CheckBox(&s_AspectDisableAll, RCLocalize("Disable for almost all UI", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::ALL, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::ALL;
	if(!(g_Config.m_RcCustomAspectDisable & RcAspectDisable::ALL))
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableWheels = 0;
		if(DoButton_CheckBox(&s_AspectDisableWheels, RCLocalize("Disable for Wheels", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::WHEELS, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::WHEELS;

		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableMenus = 0;
		if(DoButton_CheckBox(&s_AspectDisableMenus, RCLocalize("Disable for Menus", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::MENUS, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::MENUS;

		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableScoreboard = 0;
		if(DoButton_CheckBox(&s_AspectDisableScoreboard, RCLocalize("Disable for Scoreboard", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::SCOREBOARD, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::SCOREBOARD;

		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableConsole = 0;
		if(DoButton_CheckBox(&s_AspectDisableConsole, RCLocalize("Disable for Console", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::CONSOLE, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::CONSOLE;

		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableAboveBelow = 0;
		if(DoButton_CheckBox(&s_AspectDisableAboveBelow, RCLocalize("Disable for Above/Below Player", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::ABOVEBELOWPL, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::ABOVEBELOWPL;
	}
	else
		Column.HSplitTop(LineSize * 5, nullptr, &Column);

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableHud = 0;
	if(DoButton_CheckBox(&s_AspectDisableHud, RCLocalize("Disable for Hud", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::HUD, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::HUD;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableChat = 0;
	if(DoButton_CheckBox(&s_AspectDisableChat, RCLocalize("Disable for Chat", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::CHAT, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::CHAT;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableVoiceIndicator = 0;
	if(DoButton_CheckBox(&s_AspectDisableVoiceIndicator, RCLocalize("Disable for Voice indicator", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::VOICEINDICATOR, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::VOICEINDICATOR;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableVoiceOverlay = 0;
	if(DoButton_CheckBox(&s_AspectDisableVoiceOverlay, RCLocalize("Disable for Voice overlay", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::VOICEOVERLAY, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::VOICEOVERLAY;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableEdgeinfo = 0;
	if(DoButton_CheckBox(&s_AspectDisableEdgeinfo, RCLocalize("Disable for Edge info", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::EDGEINFO, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::EDGEINFO;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableInfoMessages = 0;
	if(DoButton_CheckBox(&s_AspectDisableInfoMessages, RCLocalize("Disable for Info messages", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::INFOMESSAGES, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::INFOMESSAGES;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableNotifyInSpec = 0;
	if(DoButton_CheckBox(&s_AspectDisableNotifyInSpec, RCLocalize("Disable for Notify in spec", "RClient"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::NOTIFYINSPEC, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::NOTIFYINSPEC;

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Above/Below Player", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(Margin, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("idea from BestClient", "RClient"), Margin, TEXTALIGN_MC);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNotifyWhenAbovePosPlayer, RCLocalize("Notify when above player", "RClient"), &g_Config.m_RcNotifyWhenAbovePosPlayer, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNotifyWhenSamePosPlayer, RCLocalize("Notify when same pos as player", "RClient"), &g_Config.m_RcNotifyWhenSamePosPlayer, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNotifyWhenBelowPosPlayer, RCLocalize("Notify when below player", "RClient"), &g_Config.m_RcNotifyWhenBelowPosPlayer, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Spectator ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Spectator", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSpectatorMoveEnable, RCLocalize("Enable spectator movement", "RClient"), &g_Config.m_RcSpectatorMoveEnable, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcSpectatorMoveSpeed, &g_Config.m_RcSpectatorMoveSpeed, &Button, RCLocalize("Spectator Speed", "RClient"), 1, 200, &CUi::ms_LinearScrollbarScale, 0);
	static CButtonContainer s_ReaderButtonSpecGoLeft, s_ClearButtonSpecGoLeft,
		s_ReaderButtonSpecGoRight, s_ClearButtonSpecGoRight,
		s_ReaderButtonSpecGoUp, s_ClearButtonSpecGoUp,
		s_ReaderButtonSpecGoDown, s_ClearButtonSpecGoDown;
	DoLine_KeyReader(Column, s_ReaderButtonSpecGoLeft, s_ClearButtonSpecGoLeft, RCLocalize("Spec go Left", "RClient"), "+rc_spec_go_left");
	DoLine_KeyReader(Column, s_ReaderButtonSpecGoRight, s_ClearButtonSpecGoRight, RCLocalize("Spec go Right", "RClient"), "+rc_spec_go_right");
	DoLine_KeyReader(Column, s_ReaderButtonSpecGoUp, s_ClearButtonSpecGoUp, RCLocalize("Spec go Up", "RClient"), "+rc_spec_go_up");
	DoLine_KeyReader(Column, s_ReaderButtonSpecGoDown, s_ClearButtonSpecGoDown, RCLocalize("Spec go Down", "RClient"), "+rc_spec_go_down");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSpecClosestInSpec, RCLocalize("Allow click on players in /spec", "RClient"), &g_Config.m_RcSpecClosestInSpec, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Helping Function ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Helpful Functions", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		enum
		{
			HELP_TAB_MAIN = 0,
			HELP_TAB_SORT,
			NUMBER_OF_HELP_TABS
		};

		static int s_CurHelpCustomTab = 0;
		static CButtonContainer s_aPageTabs[NUMBER_OF_HELP_TABS] = {};
		const char *apTabNames[NUMBER_OF_HELP_TABS] = {
			RCLocalize("Main", "RClient"),
			RCLocalize("Sort", "RClient")};
		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_HELP_TABS, s_aPageTabs, s_CurHelpCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		// Main: 11 rows + 2 radio menus
		// Sort: 2 radio menus
		const float MBiggestTab = std::max(
			TabHeightLine(11) + TabHeightRadioMenu(2),
			TabHeightRadioMenu(2));
		const float MCurrentY = Column.y;

		if(s_CurHelpCustomTab == HELP_TAB_MAIN)
		{
			static std::vector<CButtonContainer> s_vButtonContainersAutoLock = {{}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Auto Lock Team", "RClient"),
				s_vButtonContainersAutoLock,
				{RCLocalize("Off", "RClient"), RCLocalize("Empty", "RClient"), RCLocalize("Any", "RClient")},
				{0, 1, 2},
				g_Config.m_RcAutoLockTeam);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcAntiUnSpec, RCLocalize("Anti UnSpec in player", "RClient"), &g_Config.m_RcAntiUnSpec, &Column, LineSize);
			Column.HSplitTop(LineSize, &Button, &Column);
			Button.VSplitLeft(Margin, nullptr, &Button);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcAntiUnSpecSetPlayerCamera, RCLocalize("Set camera to player when try unspec", "RClient"), &g_Config.m_RcAntiUnSpecSetPlayerCamera, &Button, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowRechargeBars, RCLocalize("Show Recharge bars (by +KZ/Kaizo Client)", "RClient"), &g_Config.m_RcShowRechargeBars, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcClearConfigsInUnknownFile, RCLocalize("Clear ddnet config from unknown configs on exit", "RClient"), &g_Config.m_RcClearConfigsInUnknownFile, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowOpenSecondClientButton, RCLocalize("Show open second client button", "RClient"), &g_Config.m_RcShowOpenSecondClientButton, &Column, LineSize);
			static std::vector<CButtonContainer> s_vButtonContainersSaveCount = {{}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Show Save Count", "RClient"),
				s_vButtonContainersSaveCount,
				{RCLocalize("Off", "RClient"), RCLocalize("File", "RClient"), RCLocalize("/load", "RClient")},
				{0, 1, 2},
				g_Config.m_RcShowSavesCount);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcSndGunFireVolume, &g_Config.m_RcSndGunFireVolume, &Button, RCLocalize("Gun fire volume", "RClient"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSaveAlphaInOtherTeamInSpec, RCLocalize("Fix cl_show_others_alpha in spec", "RClient"), &g_Config.m_RcSaveAlphaInOtherTeamInSpec, &Column, LineSize);
			static int s_WeaponSlotsHelpId = 0;
			DoButton_CheckBoxAutoVMarginAndSet(&s_WeaponSlotsHelpId, RCLocalize("Weapon slots", "RClient"), &g_Config.m_RcWeaponSlots, &Column, LineSize);
			static CButtonContainer s_HookLineContinueColor, s_HookLineContinuePlayerColor;
			DoButton_ColorPickerAutoVMargin(&s_HookLineContinueColor, RCLocalize("Continue hook line after hit block", "RClient"), &g_Config.m_RcHookLineContinueColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcHookLineContinueColor)), &Column, LineSize, true, &g_Config.m_RcContinueHookLine);
			DoButton_ColorPickerAutoVMargin(&s_HookLineContinuePlayerColor, RCLocalize("Continue hook line after hit player", "RClient"), &g_Config.m_RcHookLinePlayerContinueColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcHookLinePlayerContinueColor)), &Column, LineSize, true, &g_Config.m_RcContinuePlayerHookLine);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSendSpecPlPos, RCLocalize("Can send spec target pos in !sendpos", "RClient"), &g_Config.m_RcSendSpecPlPos, &Column, LineSize);
		}

		if(s_CurHelpCustomTab == HELP_TAB_SORT)
		{
			static std::vector<CButtonContainer> s_vButtonContainersSortScoreboard = {{}, {}, {}, {}, {}};
			DoLine_RadioMenu_WLabelSize(Column, RCLocalize("Sort Scoreboard", "RClient"), Column.w / 4,
				s_vButtonContainersSortScoreboard,
				{RCLocalize("name-team-score", "RClient"), RCLocalize("id-team-score", "RClient"), RCLocalize("id-score", "RClient"), RCLocalize("id-team", "RClient"), RCLocalize("id", "RClient")},
				{0, 1, 2, 3, 4},
				g_Config.m_RcScoreboardSortId);

			static std::vector<CButtonContainer> s_vButtonContainersSortSpectator = {{}, {}, {}};
			DoLine_RadioMenu_WLabelSize(Column, RCLocalize("Sort Spectator", "RClient"), Column.w / 4,
				s_vButtonContainersSortSpectator,
				{RCLocalize("name-team", "RClient"), RCLocalize("id-team", "RClient"), RCLocalize("id", "RClient")},
				{0, 1, 2},
				g_Config.m_RcSpectatorSortId);
		}
		if(MBiggestTab > Column.y - MCurrentY)
			Column.HSplitTop(MBiggestTab - (Column.y - MCurrentY), nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;

	// ***** Chat ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Chat", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		enum
		{
			CHAT_TAB_MAIN = 0,
			CHAT_TAB_FILTER,
			CHAT_TAB_TRANSLATE,
			NUMBER_OF_CHAT_TABS
		};

		static int s_CurChatCustomTab = 0;
		static CButtonContainer s_aPageTabs[NUMBER_OF_CHAT_TABS] = {};
		const char *apTabNames[NUMBER_OF_CHAT_TABS] = {
			RCLocalize("Main", "RClient"),
			RCLocalize("Filter", "RClient"),
			RCLocalize("Translate", "RClient")};

		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_CHAT_TABS, s_aPageTabs, s_CurChatCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		// Main: 5 rows
		// Filter: radio menu + 7 rows + 2 edit boxes (worst case)
		// Translate: 8 rows + 4 margins + 2 edit boxes (worst case)
		const float MBiggestTab = std::max({
			TabHeightLine(5),
			TabHeightRadioMenu() + TabHeightLine(7) + TabHeightEditBox(2),
			TabHeightLine(8) + MarginSmall * 4.0f + TabHeightEditBox(2),
		});
		const float MCurrentY = Column.y;
		if(s_CurChatCustomTab == CHAT_TAB_MAIN)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatShowMouse, RCLocalize("Show mouse when open chat", "RClient"), &g_Config.m_RcChatShowMouse, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCommandsFixLayout, RCLocalize("Fix language commands", "RClient"), &g_Config.m_RcCommandsFixLayout, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatScroll, RCLocalize("Enable chat scroll", "RClient"), &g_Config.m_RcChatScroll, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatShowScrollbar, RCLocalize("Show scroll bar", "RClient"), &g_Config.m_RcChatShowScrollbar, &Column, LineSize);
			static CButtonContainer s_HighlightMessageColor;
			DoButton_ColorPickerAutoVMargin(&s_HighlightMessageColor, RCLocalize("Enable highlight players in chat", "RClient"), &g_Config.m_RcHighlightChatBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcHighlightChatBackgroundColor, true)), &Column, LineSize, true, &g_Config.m_RcEnableHighlightChat);
		}
		if(s_CurChatCustomTab == CHAT_TAB_FILTER)
		{
			static std::vector<CButtonContainer> s_vButtonContainersChatFilter = {{}, {}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Chat Filter Mode", "RClient"),
				s_vButtonContainersChatFilter,
				{RCLocalize("Off", "RClient"), RCLocalize("Partial", "RClient"), RCLocalize("Full", "RClient"), RCLocalize("Both", "RClient")},
				{0, 1, 2, 3},
				g_Config.m_RcMessageFilterMode);
			static CButtonContainer s_BlockedMessageColor;
			DoButton_ColorPickerAutoVMargin(&s_BlockedMessageColor, RCLocalize("Print Blocked Message", "RClient"), &g_Config.m_RcMessageFilterPrintBlockedMessageColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcMessageFilterPrintBlockedMessageColor)), &Column, LineSize, false, &g_Config.m_RcMessageFilterPrintBlockedMessage);
			if(g_Config.m_RcMessageFilterMode == 1 || g_Config.m_RcMessageFilterMode == 3)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcMessageFilterMultiplyChangeWordOnPartialMatch, RCLocalize("Multiply Partial Match", "RClient"), &g_Config.m_RcMessageFilterMultiplyChangeWordOnPartialMatch, &Column, LineSize);
				{
					CUIRect Box;
					Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
					Box.VSplitMid(&Label, &Button);
					Ui()->DoLabel(&Label, RCLocalize("Partial Word Replacement", "RClient"), FontSize, TEXTALIGN_ML);
					static CLineInput s_LineInput(g_Config.m_RcMessageFilterWordOnPartialMatch, sizeof(g_Config.m_RcMessageFilterWordOnPartialMatch));
					s_LineInput.SetEmptyText(RCLocalize("*", "RClient"));
					Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
				}
			}
			if(g_Config.m_RcMessageFilterMode == 2 || g_Config.m_RcMessageFilterMode == 3)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcMessageFilterMultiplyChangeWordOnFullMatch, RCLocalize("Multiply Full Match", "RClient"), &g_Config.m_RcMessageFilterMultiplyChangeWordOnFullMatch, &Column, LineSize);
				{
					CUIRect Box;
					Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
					Box.VSplitMid(&Label, &Button);
					Ui()->DoLabel(&Label, RCLocalize("Full Word Replacement", "RClient"), FontSize, TEXTALIGN_ML);
					static CLineInput s_LineInput(g_Config.m_RcMessageFilterWordOnFullMatch, sizeof(g_Config.m_RcMessageFilterWordOnFullMatch));
					s_LineInput.SetEmptyText(RCLocalize("^", "RClient"));
					Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
				}
			}
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcMessageFilterChat, RCLocalize("Filter Chat", "RClient"), &g_Config.m_RcMessageFilterChat, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcMessageFilterNameplates, RCLocalize("Filter Nameplates", "RClient"), &g_Config.m_RcMessageFilterNameplates, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcMessageFilterScoreboard, RCLocalize("Filter Scoreboard", "RClient"), &g_Config.m_RcMessageFilterScoreboard, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcMessageFilterInfoMessages, RCLocalize("Filter Info messages", "RClient"), &g_Config.m_RcMessageFilterInfoMessages, &Column, LineSize);
		}
		if(s_CurChatCustomTab == CHAT_TAB_TRANSLATE)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTranslateAuto, RCLocalize("Use auto translate", "RClient"), &g_Config.m_TcTranslateAuto, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcTranslateServerMessages, RCLocalize("Translate server messages", "RClient"), &g_Config.m_RcTranslateServerMessages, &Column, LineSize);
			{
				static CUi::SDropDownState s_StateTranslateOthers;
				static CScrollRegion s_ScrollRegionTranslateOthers;
				s_StateTranslateOthers.m_SelectionPopupContext.m_pScrollRegion = &s_ScrollRegionTranslateOthers;
				int LangSelectedOld = -1;
				for(size_t i = 0; i < GameClient()->m_RClient.m_LatestLangsList.size(); ++i)
				{
					if(!str_utf8_comp_nocase(GameClient()->m_RClient.m_LatestLangsList[i].m_LangCode, g_Config.m_TcTranslateTarget))
					{
						LangSelectedOld = i;
						break;
					}
				}
				CUIRect DropDownRect;
				Column.HSplitTop(LineSize, &DropDownRect, &Column);
				DropDownRect.VSplitMid(&Label, &DropDownRect);
				Ui()->DoLabel(&Label, RCLocalize("Latest languages", "RClient"), FontSize, TEXTALIGN_ML);
				const int LangSelectedNew = Ui()->DoDropDown(&DropDownRect, LangSelectedOld,
					GameClient()->m_RClient.m_SLangDropDownNames.data(), GameClient()->m_RClient.m_SLangDropDownNames.size(), s_StateTranslateOthers);
				if(LangSelectedOld != LangSelectedNew)
				{
					str_copy(g_Config.m_TcTranslateTarget, GameClient()->m_RClient.m_LatestLangsList[LangSelectedNew].m_LangCode);
				}

				Column.HSplitTop(MarginSmall, nullptr, &Column);
				CUIRect Box;
				Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
				Box.VSplitLeft(Box.w / 4 * 3, &Label, &Button);
				Ui()->DoLabel(&Label, RCLocalize("Your language target (in ISO 639-1 code)", "RClient"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget));
				s_LineInput.SetEmptyText(RCLocalize("ru", "RClient"));
				if(Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize))
				{
					const ChatThings::STranslateLangs pLang = GameClient()->m_RClient.GetLanguageName(g_Config.m_TcTranslateTarget);
					if(pLang.m_LangCode[0] != '\0')
					{
						GameClient()->m_RClient.AddNewLanguage(pLang);
					}
				}
				if(LangSelectedOld == -1)
				{
					SLabelProperties Props;
					Props.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
					Column.HSplitTop(LineSize / 1.5f, &Label, &Column);
					Ui()->DoLabel(&Label, RCLocalize("Unknown language", "RClient"), FontSize / 1.5f, TEXTALIGN_MR, Props);
				}
			}
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcTranslateSend, RCLocalize("Translate your messages", "RClient"), &g_Config.m_RcTranslateSend, &Column, LineSize);

			{
				static CUi::SDropDownState s_StateTranslateYour;
				static CScrollRegion s_ScrollRegionTranslateYour;
				s_StateTranslateYour.m_SelectionPopupContext.m_pScrollRegion = &s_ScrollRegionTranslateYour;
				int LangSelectedOldYour = -1;
				for(size_t i = 0; i < GameClient()->m_RClient.m_LatestLangsList.size(); ++i)
				{
					if(!str_utf8_comp_nocase(GameClient()->m_RClient.m_LatestLangsList[i].m_LangCode, g_Config.m_RcTranslateSendTarget))
					{
						LangSelectedOldYour = i;
						break;
					}
				}
				CUIRect DropDownRect;
				Column.HSplitTop(LineSize, &DropDownRect, &Column);
				DropDownRect.VSplitMid(&Label, &DropDownRect);
				Ui()->DoLabel(&Label, RCLocalize("Latest languages", "RClient"), FontSize, TEXTALIGN_ML);
				const int LangSelectedNew = Ui()->DoDropDown(&DropDownRect, LangSelectedOldYour,
					GameClient()->m_RClient.m_SLangDropDownNames.data(), GameClient()->m_RClient.m_SLangDropDownNames.size(), s_StateTranslateYour);
				if(LangSelectedOldYour != LangSelectedNew)
				{
					str_copy(g_Config.m_RcTranslateSendTarget, GameClient()->m_RClient.m_LatestLangsList[LangSelectedNew].m_LangCode);
				}

				Column.HSplitTop(MarginSmall, nullptr, &Column);
				CUIRect Box;
				Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
				Box.VSplitLeft(Box.w / 4 * 3, &Label, &Button);
				Ui()->DoLabel(&Label, RCLocalize("Send language target (in ISO 639-1 code)", "RClient"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_RcTranslateSendTarget, sizeof(g_Config.m_RcTranslateSendTarget));
				s_LineInput.SetEmptyText(RCLocalize("en", "RClient"));
				if(Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize))
				{
					const ChatThings::STranslateLangs pLang = GameClient()->m_RClient.GetLanguageName(g_Config.m_RcTranslateSendTarget);
					if(pLang.m_LangCode[0] != '\0')
					{
						GameClient()->m_RClient.AddNewLanguage(pLang);
					}
				}
				if(LangSelectedOldYour == -1)
				{
					SLabelProperties Props;
					Props.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
					Column.HSplitTop(LineSize / 1.5f, &Label, &Column);
					Ui()->DoLabel(&Label, RCLocalize("Unknown language", "RClient"), FontSize / 1.5f, TEXTALIGN_MR, Props);
				}
			}
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatShowTranslateFastSettings, RCLocalize("Fast settings in chat", "RClient"), &g_Config.m_RcChatShowTranslateFastSettings, &Column, LineSize);
			{
				static CUi::SDropDownState s_StateTranslateBackend;
				static CScrollRegion s_ScrollRegionTranslateBackend;
				s_StateTranslateBackend.m_SelectionPopupContext.m_pScrollRegion = &s_ScrollRegionTranslateBackend;

				static const char *s_apBackendNames[] = {"ftapi", "libretranslate", "googlegtx", "fedilab", "bing", "duckduckgo"};
				static const char *s_apBackendNamesVisual[] = {RCLocalize("ftapi (not work)"), RCLocalize("libretranslate (need key)"), RCLocalize("googlegtx (maybe work)"), RCLocalize("fedilab (not work)"), RCLocalize("bing (works on upd)"), RCLocalize("duckduckgo (works on upd))")};
				int BackendSelectedOld = -1;
				for(size_t i = 0; i < std::size(s_apBackendNames); ++i)
				{
					if(!str_comp_nocase(s_apBackendNames[i], g_Config.m_TcTranslateBackend))
					{
						BackendSelectedOld = i;
						break;
					}
				}
				CUIRect DropDownRect;
				Column.HSplitTop(LineSize, &DropDownRect, &Column);
				DropDownRect.VSplitMid(&Label, &DropDownRect);
				Ui()->DoLabel(&Label, RCLocalize("Choose backend", "RClient"), FontSize, TEXTALIGN_ML);
				const int BackendSelectedNew = Ui()->DoDropDown(&DropDownRect, BackendSelectedOld,
					s_apBackendNamesVisual, std::size(s_apBackendNames), s_StateTranslateBackend);
				if(BackendSelectedOld != BackendSelectedNew)
				{
					str_copy(g_Config.m_TcTranslateBackend, s_apBackendNames[BackendSelectedNew]);
				}
				Column.HSplitTop(MarginSmall, nullptr, &Column);
			}

			if(!str_comp(g_Config.m_TcTranslateBackend, "googlegtx"))
			{
				static CUi::SDropDownState s_StateTranslateBackend;
				static CScrollRegion s_ScrollRegionTranslateBackend;
				s_StateTranslateBackend.m_SelectionPopupContext.m_pScrollRegion = &s_ScrollRegionTranslateBackend;

				static const char *s_apBackendNames[] = {"google", "googleapis", "client5"};
				int BackendSelectedOld = g_Config.m_RcTranslateGoogleEndpoint;
				CUIRect DropDownRect;
				Column.HSplitTop(LineSize, &DropDownRect, &Column);
				DropDownRect.VSplitMid(&Label, &DropDownRect);
				Ui()->DoLabel(&Label, RCLocalize("Google backends", "RClient"), FontSize, TEXTALIGN_ML);
				const int BackendSelectedNew = Ui()->DoDropDown(&DropDownRect, BackendSelectedOld,
					s_apBackendNames, std::size(s_apBackendNames), s_StateTranslateBackend);
				if(BackendSelectedOld != BackendSelectedNew)
				{
					g_Config.m_RcTranslateGoogleEndpoint = BackendSelectedNew;
				}
				Column.HSplitTop(MarginSmall, nullptr, &Column);
			}
		}
		if(MBiggestTab > Column.y - MCurrentY)
			Column.HSplitTop(MBiggestTab - (Column.y - MCurrentY), nullptr, &Column);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Nameplate ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Nameplates", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesVoice, RCLocalize("Show voice headphones", "RClient"), &g_Config.m_RcNamePlatesVoice, &Column, LineSize);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		enum
		{
			NAMEPLATE_TAB_HOOK = 0,
			NAMEPLATE_TAB_FIRE,
			NAMEPLATE_TAB_WEAPONS,
			NUMBER_OF_NAMEPLATES_TABS
		};

		static int s_CurNameplatesCustomTab = 0;
		static CButtonContainer s_aPageTabs[NUMBER_OF_NAMEPLATES_TABS] = {};
		const char *apTabNames[NUMBER_OF_NAMEPLATES_TABS] = {
			RCLocalize("Hook", "RClient"),
			RCLocalize("Fire", "RClient"),
			RCLocalize("Weapons", "RClient")};
		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_NAMEPLATES_TABS, s_aPageTabs, s_CurNameplatesCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		// Hook: radio menu + 2 rows
		// Fire: radio menu + 3 rows + note
		// Weapons: radio menu + 6 rows
		const float MBiggestTab = std::max({
			TabHeightRadioMenu() + TabHeightLine(2),
			TabHeightRadioMenu() + TabHeightLine(3) + LineSize / 2.0f,
			TabHeightRadioMenu() + TabHeightLine(6),
		});
		const float MCurrentY = Column.y;
		if(s_CurNameplatesCustomTab == NAMEPLATE_TAB_HOOK)
		{
			static std::vector<CButtonContainer> s_vButtonContainersHookDetection = {{}, {}, {}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Hook Detection", "RClient"),
				s_vButtonContainersHookDetection,
				{RCLocalize("Off", "RClient"), RCLocalize("Others", "RClient"), RCLocalize("All", "RClient"), RCLocalize("Own", "RClient"), RCLocalize("Dummy", "RClient")},
				{0, 1, 2, 3, 4},
				g_Config.m_RcNamePlatesHook);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcNamePlatesHookSize, &g_Config.m_RcNamePlatesHookSize, &Button, RCLocalize("Hook size", "RClient"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesHookShiftOnInvis, RCLocalize("Hook shift on invis", "RClient"), &g_Config.m_RcNamePlatesHookShiftOnInvis, &Column, LineSize);
		}
		if(s_CurNameplatesCustomTab == NAMEPLATE_TAB_FIRE)
		{
			static std::vector<CButtonContainer> s_vButtonContainersFireDetection = {{}, {}, {}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Fire Detection", "RClient"),
				s_vButtonContainersFireDetection,
				{RCLocalize("Off", "RClient"), RCLocalize("Others(*)", "RClient"), RCLocalize("All(*)", "RClient"), RCLocalize("Own", "RClient"), RCLocalize("Dummy", "RClient")},
				{0, 1, 2, 3, 4},
				g_Config.m_RcNamePlatesFire);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcNamePlatesFireSize, &g_Config.m_RcNamePlatesFireSize, &Button, RCLocalize("Fire size", "RClient"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesFireShiftOnInvis, RCLocalize("Fire shift on invis", "RClient"), &g_Config.m_RcNamePlatesFireShiftOnInvis, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesFirePreInput, RCLocalize("Use preinput for fire", "RClient"), &g_Config.m_RcNamePlatesFirePreInput, &Column, LineSize);
			Column.HSplitTop(LineSize / 2.0f, &Label, &Column);
			Ui()->DoLabel(&Label, RCLocalize("* - Can be fake info in some cases. Better when preinput enabled"), FontSize / 2.0f, TEXTALIGN_ML);
		}
		if(s_CurNameplatesCustomTab == NAMEPLATE_TAB_WEAPONS)
		{
			static std::vector<CButtonContainer> s_vButtonContainersWeaponsShow = {{}, {}, {}, {}};
			DoLine_RadioMenu(Column, RCLocalize("Weapons in nameplates", "RClient"),
				s_vButtonContainersWeaponsShow,
				{RCLocalize("Off", "RClient"), RCLocalize("Others", "RClient"), RCLocalize("All", "RClient"), RCLocalize("Dummy", "RClient")},
				{0, 1, 2, 3},
				g_Config.m_RcNamePlatesWeapons);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcNamePlatesWeaponsSize, &g_Config.m_RcNamePlatesWeaponsSize, &Button, RCLocalize("Weapons size", "RClient"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsOwn, RCLocalize("Show own weapons", "RClient"), &g_Config.m_RcNamePlatesWeaponsOwn, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsShotgun, RCLocalize("Show shotgun", "RClient"), &g_Config.m_RcNamePlatesWeaponsShotgun, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsGrenade, RCLocalize("Show grenade", "RClient"), &g_Config.m_RcNamePlatesWeaponsGrenade, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsNinja, RCLocalize("Show ninja", "RClient"), &g_Config.m_RcNamePlatesWeaponsNinja, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsLaser, RCLocalize("Show laser", "RClient"), &g_Config.m_RcNamePlatesWeaponsLaser, &Column, LineSize);
		}
		if(MBiggestTab > Column.y - MCurrentY)
			Column.HSplitTop(MBiggestTab - (Column.y - MCurrentY), nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Anti AFK ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Anti AFK", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		enum
		{
			AFK_TAB_NONACTIVE = 0,
			AFK_TAB_SPEC,
			NUMBER_OF_AFK_TABS
		};

		static int s_CurAfkCustomTab = 0;
		static CButtonContainer s_aPageTabs[NUMBER_OF_AFK_TABS] = {};
		const char *apTabNames[NUMBER_OF_AFK_TABS] = {
			RCLocalize("NonActive", "RClient"),
			RCLocalize("Spec", "RClient")};

		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_AFK_TABS, s_aPageTabs, s_CurAfkCustomTab, LineSize);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(Margin, &Label, &Column);
		if(s_CurAfkCustomTab == AFK_TAB_SPEC)
		{
			Label.VSplitRight(Column.w / (float)NUMBER_OF_AFK_TABS, nullptr, &Label);
			Ui()->DoLabel(&Label, RCLocalize("idea from BestClient", "RClient"), Margin, TEXTALIGN_MC);
		}

		// NonActive: 2 checkboxes + radio menu
		// Spec: 6 rows + radio menu
		const float MBiggestTab = std::max(
			TabHeightCheckBox(2) + TabHeightRadioMenu(),
			TabHeightLine(6) + TabHeightRadioMenu());
		const float MCurrentY = Column.y;

		if(s_CurAfkCustomTab == AFK_TAB_NONACTIVE)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcPlayOnMoveNonInactive, RCLocalize("Play sound when moved and window non active", "RClient"), &g_Config.m_RcPlayOnMoveNonInactive, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSendInMenuInfoWhenInactive, RCLocalize("Send in menu info when inactive", "RClient"), &g_Config.m_RcSendInMenuInfoWhenInactive, &Column, LineSize);
			if(g_Config.m_RcPlayOnMoveNonInactive)
			{
				static std::vector<CButtonContainer> s_vButtonContainersNonActive = {{}, {}, {}};
				DoLine_RadioMenu(Column, RCLocalize("Choose sound non active", "RClient"),
					s_vButtonContainersNonActive,
					{RCLocalize("Wake up", "RClient"), RCLocalize("Grenade", "RClient"), RCLocalize("Tag", "RClient")},
					{0, 1, 2},
					g_Config.m_RcSoundOnMoveNonInactive);
			}
		}
		if(s_CurAfkCustomTab == AFK_TAB_SPEC)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNotifyOnMoveInSpec, RCLocalize("Notify when moved in spec", "RClient"), &g_Config.m_RcNotifyOnMoveInSpec, &Column, LineSize);
			if(g_Config.m_RcNotifyOnMoveInSpec)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcPlayOnMoveInSpec, RCLocalize("Play sound when moved in spec", "RClient"), &g_Config.m_RcPlayOnMoveInSpec, &Column, LineSize);
				static std::vector<CButtonContainer> s_vButtonContainersInSpec = {{}, {}, {}};
				DoLine_RadioMenu(Column, RCLocalize("Choose sound in spec", "RClient"),
					s_vButtonContainersInSpec,
					{RCLocalize("Wake up", "RClient"), RCLocalize("Grenade", "RClient"), RCLocalize("Tag", "RClient")},
					{0, 1, 2},
					g_Config.m_RcSoundOnMoveInSpec);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcTextOnMoveInSpec, RCLocalize("Show text when moved in spec", "RClient"), &g_Config.m_RcTextOnMoveInSpec, &Column, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcTextOnMoveInSpecRemoveOnInput, RCLocalize("Remove text on any input", "RClient"), &g_Config.m_RcTextOnMoveInSpecRemoveOnInput, &Column, LineSize);
			}
		}
		if(MBiggestTab > Column.y - MCurrentY)
			Column.HSplitTop(MBiggestTab - (Column.y - MCurrentY), nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Custom Clients ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Client Indicator", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(Margin, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("by +KZ/Kaizo Client", "RClient"), Margin, TEXTALIGN_MC);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomClientsSendClientType, RCLocalize("Send to server that u use RClient", "RClient"), &g_Config.m_RcCustomClientsSendClientType, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomClientsCollectClientType, RCLocalize("Detect other clients", "RClient"), &g_Config.m_RcCustomClientsCollectClientType, &Column, LineSize);
	static std::vector<CButtonContainer> s_vButtonContainersCustomInNameplates = {{}, {}, {}, {}};
	DoLine_RadioMenu(Column, RCLocalize("Show client type in nameplates", "RClient"),
		s_vButtonContainersCustomInNameplates,
		{RCLocalize("Off", "RClient"), RCLocalize("Others", "RClient"), RCLocalize("Everyone", "RClient"), RCLocalize("Only you", "RClient")},
		{0, 1, 2, 3},
		g_Config.m_RcCustomClientsInNameplates);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomClientsInScoreboard, RCLocalize("Show client type in scoreboard", "RClient"), &g_Config.m_RcCustomClientsInScoreboard, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcCustomClientsInNameplatesSize, &g_Config.m_RcCustomClientsInNameplatesSize, &Button, RCLocalize("Icons size in nameplates", "RClient"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcCustomClientsInScoreboardSize, &g_Config.m_RcCustomClientsInScoreboardSize, &Button, RCLocalize("Icons size in scoreboard", "RClient"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomClientsCollectClientTypeBestClient, RCLocalize("Detect BestClient users", "RClient"), &g_Config.m_RcCustomClientsCollectClientTypeBestClient, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Binds ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Binds", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
		s_ReaderButtonPseudofly, s_ClearButtonPseudofly,
		s_ReaderButton45degrees, s_ClearButton45degrees,
		s_ReaderButtonSmallsens, s_ClearButtonSmallsens,
		s_ReaderButtonFindTeleport, s_ClearButtonFindTeleport,
		s_ReaderButtonLJump, s_ClearButtonLJump,
		s_ReaderButtonRJump, s_ClearButtonRJump,
		s_ReaderButtonFindFinish, s_ClearButtonFindFinish;
	DoLine_KeyReader(Column, s_ReaderButtonDeepfly, s_ClearButtonDeepfly, RCLocalize("Toggle deepfly", "RClient"), "rc_toggle_deepfly");
	{
		CUIRect RightSide;
		Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcDeepFlyOnRMB, RCLocalize("Deepfly on rmb", "RClient"), &g_Config.m_RcDeepFlyOnRMB, &RightSide, LineSize);
		Column.HSplitTop(LineSize, nullptr, &Column);
	}
	DoLine_KeyReader(Column, s_ReaderButtonPseudofly, s_ClearButtonPseudofly, RCLocalize("Pseudo fly", "RClient"), "+toggle cl_dummy_hammer 1 0");
	DoLine_KeyReader(Column, s_ReaderButton45degrees, s_ClearButton45degrees, RCLocalize("Toggle 45 degrees", "RClient"), "+rc_45_degrees");
	{
		CUIRect RightSide;
		Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcToggle45degrees, RCLocalize("Toggle 45 degrees", "RClient"), &g_Config.m_RcToggle45degrees, &RightSide, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Rc45degreesEcho, RCLocalize("Echo 45 degrees", "RClient"), &g_Config.m_Rc45degreesEcho, &RightSide, LineSize);
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	}
	DoLine_KeyReader(Column, s_ReaderButtonSmallsens, s_ClearButtonSmallsens, RCLocalize("Toggle small sens", "RClient"), "+rc_small_sens");
	{
		CUIRect RightSide;
		Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcToggleSmallSens, RCLocalize("Toggle small sens", "RClient"), &g_Config.m_RcToggleSmallSens, &RightSide, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSmallSensEcho, RCLocalize("Echo small sens", "RClient"), &g_Config.m_RcSmallSensEcho, &RightSide, LineSize);
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	}
	DoLine_KeyReader(Column, s_ReaderButtonFindTeleport, s_ClearButtonFindTeleport, RCLocalize("Find Teleport", "RClient"), "rc_goto_tele_cursor");
	DoLine_KeyReader(Column, s_ReaderButtonFindFinish, s_ClearButtonFindFinish, RCLocalize("Find Finish", "RClient"), "rc_goto_finish_cursor");
	DoLine_KeyReader(Column, s_ReaderButtonLJump, s_ClearButtonLJump, RCLocalize("Left jump", "RClient"), "+left;+jump");
	DoLine_KeyReader(Column, s_ReaderButtonRJump, s_ClearButtonRJump, RCLocalize("Right jump", "RClient"), "+right;+jump");
	static int s_WeaponSlotsBindsId = 0;
	DoButton_CheckBoxAutoVMarginAndSet(&s_WeaponSlotsBindsId, RCLocalize("Weapon slots", "RClient"), &g_Config.m_RcWeaponSlots, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Streamer mode ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Streamer mode", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(FontSize, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Only windows", "RClient"), FontSize, TEXTALIGN_ML);
	static std::vector<CButtonContainer> s_vButtonContainersScreenShare = {{}, {}, {}};
	DoLine_RadioMenu(Column, RCLocalize("Hide window from capture when RCON opened", "RClient"),
		s_vButtonContainersScreenShare,
		{RCLocalize("Off", "RClient"), RCLocalize("Monitor(win7-11)", "RClient"), RCLocalize("Exclude(win10-11)", "RClient")},
		{0, 1, 2},
		g_Config.m_RcRconSteamerMode);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Edge info ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Edge info", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_ReaderButtonEdgeInfo, s_ClearButtonEdgeInfo;
	DoLine_KeyReader(Column, s_ReaderButtonEdgeInfo, s_ClearButtonEdgeInfo, RCLocalize("Toggle Edge info", "RClient"), "rc_toggle_edge_info");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcEdgeInfoCords, RCLocalize("Show coordinates info", "RClient"), &g_Config.m_RcEdgeInfoCords, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcEdgeInfoJump, RCLocalize("Show jump info", "RClient"), &g_Config.m_RcEdgeInfoJump, &Column, LineSize);
	static CButtonContainer s_EdgeInfoFreezeColor, s_EdgeInfoKillColor, s_EdgeInfoSafeColor;
	DoButton_ColorPickerAutoVMargin(&s_EdgeInfoFreezeColor, RCLocalize("Above Freeze color", "RClient"), &g_Config.m_RcEdgeInfoColorFreeze, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcEdgeInfoColorFreeze)), &Column, LineSize, false);
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	DoButton_ColorPickerAutoVMargin(&s_EdgeInfoKillColor, RCLocalize("Above Kill color", "RClient"), &g_Config.m_RcEdgeInfoColorKill, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcEdgeInfoColorKill)), &Column, LineSize, false);
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	DoButton_ColorPickerAutoVMargin(&s_EdgeInfoSafeColor, RCLocalize("Above Save color", "RClient"), &g_Config.m_RcEdgeInfoColorSafe, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcEdgeInfoColorSafe)), &Column, LineSize, false);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Hud ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Hud", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudPlayerCheckpoint, RCLocalize("Show checkpoint", "RClient"), &g_Config.m_RcShowhudPlayerCheckpoint, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudSmallerHud, RCLocalize("Smaller hud (angle,checkpoint)", "RClient"), &g_Config.m_RcShowhudSmallerHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudRampSpeedX, RCLocalize("Show ramp * VelX speed", "RClient"), &g_Config.m_RcShowhudRampSpeedX, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_HeartSizeHudId = 0;
	Ui()->DoScrollbarOption(&s_HeartSizeHudId, &g_Config.m_RcSizeOfHeart, &Button, RCLocalize("Heart size", "RClient"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudVotingPercent, RCLocalize("Show percent in vote", "RClient"), &g_Config.m_RcShowhudVotingPercent, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	static CButtonContainer s_ReaderButtonHudEditor, s_ClearButtonHudEditor, s_OpenHudEditor;
	int SOpenRcHudEditor = GameClient()->m_RcHudEditor.IsActive();
	if(DoButton_Menu(&s_OpenHudEditor, "Open Hud Editor", SOpenRcHudEditor, &Button))
	{
		if(Client()->State() != IClient::STATE_OFFLINE)
		{
			GameClient()->m_Menus.SetActive(false);
			GameClient()->m_RcHudEditor.SetActive(true);
		}
	}
	Column.HSplitTop(MarginSmall, &Button, &Column);
	DoLine_KeyReader(Column, s_ReaderButtonHudEditor, s_ClearButtonHudEditor, RCLocalize("Bind Hud Editor", "RClient"), "rc_toggle_hud_editor");

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Hud ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Swap Timer", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcEnableSwapTimer, RCLocalize("Enable swap timer", "RClient"), &g_Config.m_RcEnableSwapTimer, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcEnableSwapTimerOnLeftSide, RCLocalize("Swap timer on left side", "RClient"), &g_Config.m_RcEnableSwapTimerOnLeftSide, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Voice chat ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Voice chat", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(Margin, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Full ai vibecoded", "RClient"), Margin, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		enum
		{
			VOICE_TAB_GENERAL = 0,
			VOICE_TAB_DEVICES,
			VOICE_TAB_SOUND,
			VOICE_TAB_ADVANCED,
			NUMBER_OF_VOICE_TABS
		};
		static int s_CurVoiceTab = VOICE_TAB_GENERAL;
		static int s_aCurVoiceSubTabs[NUMBER_OF_VOICE_TABS] = {};
		static CButtonContainer s_aVoiceTabs[NUMBER_OF_VOICE_TABS];
		static CButtonContainer s_aaVoiceSubTabs[NUMBER_OF_VOICE_TABS][3];
		const char *apTabNames[NUMBER_OF_VOICE_TABS] = {
			RCLocalize("General", "RClient"),
			RCLocalize("Devices", "RClient"),
			RCLocalize("Sound", "RClient"),
			RCLocalize("Advanced", "RClient")};
		const char *aapSubTabNames[NUMBER_OF_VOICE_TABS][3] = {
			{RCLocalize("Settings", "RClient"), RCLocalize("HUD", "RClient"), RCLocalize("Binds", "RClient")},
			{nullptr, nullptr, nullptr},
			{RCLocalize("Players", "RClient"), RCLocalize("Distance", "RClient"), RCLocalize("Stereo", "RClient")},
			{nullptr, nullptr, nullptr}};
		const int aSubTabCounts[NUMBER_OF_VOICE_TABS] = {3, 0, 3, 0};
		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_VOICE_TABS, s_aVoiceTabs, s_CurVoiceTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		const float MCurrentY = Column.y;
		if(aSubTabCounts[s_CurVoiceTab] > 0)
			DoMenuSettingsBar(&Column, aapSubTabNames[s_CurVoiceTab], aSubTabCounts[s_CurVoiceTab], s_aaVoiceSubTabs[s_CurVoiceTab], s_aCurVoiceSubTabs[s_CurVoiceTab], LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		const int SubTab = s_aCurVoiceSubTabs[s_CurVoiceTab];
		// Keep connection and player controls at the same position on every tab.
		const float MBiggestTab = MarginSmall + std::max({
								TabHeightCheckBox(6) + TabHeightRadioMenu(2) + TabHeightLine(2) + MarginSmall,
								TabHeightLine(3) + TabHeightCheckBox(3),
								TabHeightLine(3) + TabHeightKeyReader(3),
								TabHeightLine(9) + MarginSmall + 14.0f,
								TabHeightLine(7) + MarginSmall,
								TabHeightEditBox() + TabHeightCheckBox() + TabHeightLine(3),
							});
		static std::vector<CRCVoiceChat::SDevice> s_avDevices[2];
		static CUi::SDropDownState s_aDeviceStates[2];
		static CScrollRegion s_aDeviceScrollRegions[2];
		static bool s_aLoaded[2] = {};
		auto RenderVoiceDevice = [&](bool Capture) {
			const int Device = Capture ? 0 : 1;
			auto &vDevices = s_avDevices[Device];
			if(!s_aLoaded[Device])
			{
				vDevices = Capture ? GameClient()->m_RcVoiceChat.MicDevices() : GameClient()->m_RcVoiceChat.PlayoutDevices();
				if(vDevices.empty())
					vDevices.push_back({"System default", ""});
				s_aLoaded[Device] = true;
			}
			char *pCurrent = Capture ? g_Config.m_RcVoiceMicDevice : g_Config.m_RcVoicePlayoutDevice;
			std::vector<const char *> vpNames;
			int Selected = -1;
			for(size_t i = 0; i < vDevices.size(); ++i)
			{
				vpNames.push_back(vDevices[i].m_Id.empty() ? RCLocalize("System default", "RClient") : vDevices[i].m_Name.c_str());
				if(vDevices[i].m_Id == pCurrent || (!vDevices[i].m_Id.empty() && str_startswith(pCurrent, "sdl:") && vDevices[i].m_Name == pCurrent + 4))
					Selected = i;
			}
			if(Selected < 0)
			{
				Selected = vpNames.size();
				vpNames.push_back(RCLocalize("Saved device unavailable (system default)", "RClient"));
			}
			Column.HSplitTop(LineSize, &Button, &Column);
			Button.VSplitMid(&Label, &Button);
			Ui()->DoLabel(&Label, Capture ? RCLocalize("Microphone", "RClient") : RCLocalize("Speakers / headphones", "RClient"), EditBoxFontSize, TEXTALIGN_ML);
			s_aDeviceStates[Device].m_SelectionPopupContext.m_pScrollRegion = &s_aDeviceScrollRegions[Device];
			const int NewSelected = Ui()->DoDropDown(&Button, Selected, vpNames.data(), vpNames.size(), s_aDeviceStates[Device]);
			if(NewSelected != Selected && NewSelected >= 0 && (size_t)NewSelected < vDevices.size())
				str_copy(pCurrent, vDevices[NewSelected].m_Id.c_str(), Capture ? sizeof(g_Config.m_RcVoiceMicDevice) : sizeof(g_Config.m_RcVoicePlayoutDevice));
		};
		if(s_CurVoiceTab == VOICE_TAB_GENERAL)
		{
			if(SubTab == 0)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceAutoConnect, RCLocalize("Auto connect on server join", "RClient"), &g_Config.m_RcVoiceAutoConnect, &Column, LineSize);
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcVoiceMicVolume, &g_Config.m_RcVoiceMicVolume, &Button, RCLocalize("Microphone volume (%)", "RClient"), 0, 300, &CUi::ms_LinearScrollbarScale, 0);
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcVoiceVolume, &g_Config.m_RcVoiceVolume, &Button, RCLocalize("Headphone volume (%)", "RClient"), 0, 400, &CUi::ms_LinearScrollbarScale, 0);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceMuted, RCLocalize("Mute my microphone", "RClient"), &g_Config.m_RcVoiceMuted, &Column, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceDeafened, RCLocalize("Mute headphones", "RClient"), &g_Config.m_RcVoiceDeafened, &Column, LineSize);
			}
			else if(SubTab == 1)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceIndicator, RCLocalize("Show voice indicator", "RClient"), &g_Config.m_RcVoiceIndicator, &Column, LineSize);
				if(g_Config.m_RcVoiceIndicator)
				{
					CUIRect HudOptions = Column;
					HudOptions.VSplitLeft(Margin, nullptr, &HudOptions);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceStatus, RCLocalize("Show voice status and ping", "RClient"), &g_Config.m_RcVoiceStatus, &HudOptions, LineSize);
					Column.HSplitTop(LineSize, nullptr, &Column);
				}
				Column.HSplitTop(MarginSmall, nullptr, &Column);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceOverlay, RCLocalize("Show voice overlay", "RClient"), &g_Config.m_RcVoiceOverlay, &Column, LineSize);
				if(g_Config.m_RcVoiceOverlay)
				{
					static std::vector<CButtonContainer> s_vVoiceOverlayModes(2);
					DoLine_RadioMenu(Column, RCLocalize("Show players", "RClient"), s_vVoiceOverlayModes,
						{RCLocalize("Speaking", "RClient"), RCLocalize("Connected", "RClient")}, {0, 1}, g_Config.m_RcVoiceOverlayMode);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceOverlayMuted, RCLocalize("Show players muted by me", "RClient"), &g_Config.m_RcVoiceOverlayMuted, &Column, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceOverlaySkin, RCLocalize("Show player skins", "RClient"), &g_Config.m_RcVoiceOverlaySkin, &Column, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceOverlayDirection, RCLocalize("Show player direction", "RClient"), &g_Config.m_RcVoiceOverlayDirection, &Column, LineSize);
					static std::vector<CButtonContainer> s_vVoiceBackgroundModes(2);
					DoLine_RadioMenu(Column, RCLocalize("Background", "RClient"), s_vVoiceBackgroundModes,
						{RCLocalize("Per player", "RClient"), RCLocalize("Shared", "RClient")}, {0, 1}, g_Config.m_RcVoiceOverlaySharedBackground);
					static CButtonContainer s_VoiceOverlayBackgroundColor;
					DoButton_ColorPickerAutoVMargin(&s_VoiceOverlayBackgroundColor, RCLocalize("Overlay background", "RClient"), &g_Config.m_RcVoiceOverlayBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcVoiceOverlayBackgroundColor, true)), &Column, LineSize, true);
				}
			}
			else
			{
				static CButtonContainer s_aVoiceBindReaders[3], s_aVoiceBindClearButtons[3];
				DoLine_KeyReader(Column, s_aVoiceBindReaders[0], s_aVoiceBindClearButtons[0], RCLocalize("Talk (hold)", "RClient"), "+rc_voice_ptt");
				DoLine_KeyReader(Column, s_aVoiceBindReaders[1], s_aVoiceBindClearButtons[1], RCLocalize("Mic mute", "RClient"), "rc_voice_toggle_mute");
				DoLine_KeyReader(Column, s_aVoiceBindReaders[2], s_aVoiceBindClearButtons[2], RCLocalize("Incoming voice mute", "RClient"), "rc_voice_toggle_deafen");
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcVoicePttReleaseDelay, &g_Config.m_RcVoicePttReleaseDelay, &Button, RCLocalize("PTT release delay (ms)", "RClient"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0);
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_RcVoicePttReleaseDelay, &Button, RCLocalize("Keep sending briefly after releasing the talk key to preserve word endings. 0 disables the delay.", "RClient"));
			}
		}
		else if(s_CurVoiceTab == VOICE_TAB_DEVICES)
		{
			RenderVoiceDevice(true);
			RenderVoiceDevice(false);
			Column.HSplitTop(LineSize, &Button, &Column);
			static CButtonContainer s_RefreshVoiceDevices;
			if(DoButton_Menu(&s_RefreshVoiceDevices, RCLocalize("Refresh voice devices", "RClient"), 0, &Button))
			{
				for(int Device = 0; Device < 2; ++Device)
				{
					Ui()->ClosePopupMenu(&s_aDeviceStates[Device].m_SelectionPopupContext);
					s_aDeviceStates[Device].m_SelectionPopupContext.Reset();
					s_aLoaded[Device] = false;
				}
			}
			Column.HSplitTop(LineSize, &Label, &Column);
			Ui()->DoLabel(&Label, RCLocalize("Device changes apply on next voice connection", "RClient"), EditBoxFontSize, TEXTALIGN_ML);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcVoiceMicVolume, &g_Config.m_RcVoiceMicVolume, &Button, RCLocalize("Microphone volume (%)", "RClient"), 0, 300, &CUi::ms_LinearScrollbarScale, 0);
			auto &Voice = GameClient()->m_RcVoiceChat;
			Voice.KeepMicrophoneTestVisible();
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			static CButtonContainer s_MicrophoneTestButton;
			const bool Testing = Voice.MicrophoneTesting();
			if(!Testing && (Voice.Connected() || Voice.Connecting()))
				Ui()->DoLabel(&Button, RCLocalize("Disconnect voice to test the microphone", "RClient"), EditBoxFontSize, TEXTALIGN_ML);
			else if(DoButton_Menu(&s_MicrophoneTestButton, Testing ? RCLocalize("Stop microphone test", "RClient") : RCLocalize("Start microphone test", "RClient"), 0, &Button))
			{
				if(Testing)
					Voice.StopMicrophoneTest();
				else
					Voice.StartMicrophoneTest();
			}
			if(Testing)
			{
				Column.HSplitTop(LineSize, &Button, &Column);
				static CButtonContainer s_ListenToMicrophone;
				if(DoButton_CheckBox(&s_ListenToMicrophone, RCLocalize("Listen to yourself", "RClient"), Voice.MicrophoneTestListening(), &Button))
					Voice.SetMicrophoneTestListening(!Voice.MicrophoneTestListening());
			}
			Column.HSplitTop(LineSize, &Label, &Column);
			Ui()->DoLabel(&Label, RCLocalize("Microphone level", "RClient"), FontSize, TEXTALIGN_ML);
			CUIRect Meter, Fill;
			Column.HSplitTop(14.0f, &Meter, &Column);
			Meter.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 3.0f);
			const float Peak = Voice.MicrophoneTestLevel();
			const float Level = Peak > 0.0f ? std::clamp((20.0f * std::log10(Peak) + 60.0f) / 60.0f, 0.0f, 1.0f) : 0.0f;
			Meter.Margin(2.0f, &Fill);
			Fill.w *= Level;
			if(Fill.w > 0.0f)
				Fill.Draw(Peak >= 0.95f ? ColorRGBA(1.0f, 0.3f, 0.3f, 0.9f) : ColorRGBA(0.35f, 0.85f, 0.35f, 0.9f), IGraphics::CORNER_ALL, 2.0f);
			Column.HSplitTop(LineSize, &Label, &Column);
			Ui()->DoLabel(&Label, RCLocalize("Local playback only. Use headphones.", "RClient"), EditBoxFontSize, TEXTALIGN_ML);
		}
		else if(s_CurVoiceTab == VOICE_TAB_SOUND)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcVoiceVolume, &g_Config.m_RcVoiceVolume, &Button, RCLocalize("Headphone volume (%)", "RClient"), 0, 400, &CUi::ms_LinearScrollbarScale, 0);
			Column.HSplitTop(MarginSmall, nullptr, &Column);
			if(SubTab == 0)
			{
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcVoiceUnpositionedVolume, &g_Config.m_RcVoiceUnpositionedVolume, &Button, RCLocalize("Players without a position (%)", "RClient"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcVoiceSpectatorVolume, &g_Config.m_RcVoiceSpectatorVolume, &Button, RCLocalize("Spectator voice volume (%)", "RClient"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcVoiceOtherTeamVolume, &g_Config.m_RcVoiceOtherTeamVolume, &Button, RCLocalize("Other teams voice volume (%)", "RClient"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcVoiceSuperVolume, &g_Config.m_RcVoiceSuperVolume, &Button, RCLocalize("TEAM_SUPER voice volume (%)", "RClient"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
			}
			else
			{
				if(SubTab == 1)
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceSpatial, RCLocalize("Fade voice with distance", "RClient"), &g_Config.m_RcVoiceSpatial, &Column, LineSize);
					Column.HSplitTop(MarginSmall, nullptr, &Column);
					Column.HSplitTop(LineSize, &Button, &Column);
					g_Config.m_RcVoiceMinRadius = std::clamp(g_Config.m_RcVoiceMinRadius, 0, g_Config.m_RcVoiceRadius - 1);
					if(Ui()->DoScrollbarOption(&g_Config.m_RcVoiceMinRadius, &g_Config.m_RcVoiceMinRadius, &Button, RCLocalize("Full volume until (tiles)", "RClient"), 0, 199, &CUi::ms_LinearScrollbarScale, 0))
						g_Config.m_RcVoiceRadius = std::max(g_Config.m_RcVoiceRadius, g_Config.m_RcVoiceMinRadius + 1);
					GameClient()->m_Tooltips.DoToolTip(&g_Config.m_RcVoiceMinRadius, &Button, RCLocalize("Nearby voices stay at full volume. Fading starts after this distance.", "RClient"));
					Column.HSplitTop(LineSize, &Button, &Column);
					if(Ui()->DoScrollbarOption(&g_Config.m_RcVoiceRadius, &g_Config.m_RcVoiceRadius, &Button, RCLocalize("Minimum volume from (tiles)", "RClient"), 1, 200, &CUi::ms_LinearScrollbarScale, 0))
						g_Config.m_RcVoiceMinRadius = std::min(g_Config.m_RcVoiceMinRadius, g_Config.m_RcVoiceRadius - 1);
					GameClient()->m_Tooltips.DoToolTip(&g_Config.m_RcVoiceRadius, &Button, RCLocalize("At this distance and farther, voices stay at the minimum volume.", "RClient"));
					Column.HSplitTop(LineSize, &Button, &Column);
					Ui()->DoScrollbarOption(&g_Config.m_RcVoiceMinVolume, &g_Config.m_RcVoiceMinVolume, &Button, RCLocalize("Minimum volume (%)", "RClient"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);
					GameClient()->m_Tooltips.DoToolTip(&g_Config.m_RcVoiceMinVolume, &Button, RCLocalize("Volume of distant players after fading. Set to 0% to mute them.", "RClient"));
				}
				else
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceStereo, RCLocalize("Stereo (left/right)", "RClient"), &g_Config.m_RcVoiceStereo, &Column, LineSize);
					Column.HSplitTop(MarginSmall, nullptr, &Column);
					Column.HSplitTop(LineSize, &Button, &Column);
					Ui()->DoScrollbarOption(&g_Config.m_RcVoiceStereoWidth, &g_Config.m_RcVoiceStereoWidth, &Button, RCLocalize("Stereo strength (%)", "RClient"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
					Column.HSplitTop(LineSize, &Button, &Column);
					Ui()->DoScrollbarOption(&g_Config.m_RcVoiceStereoRadius, &g_Config.m_RcVoiceStereoRadius, &Button, RCLocalize("Left/right distance (tiles)", "RClient"), 1, 200, &CUi::ms_LinearScrollbarScale, 0);
				}
			}
		}
		else if(s_CurVoiceTab == VOICE_TAB_ADVANCED)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcVoiceRoomMode, RCLocalize("Custom room (name@server)", "RClient"), &g_Config.m_RcVoiceRoomMode, &Column, LineSize);

			{
				CUIRect Box;
				Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
				Box.VSplitMid(&Label, &Button);
				Ui()->DoLabel(&Label, RCLocalize("Custom room name", "RClient"), FontSize, TEXTALIGN_ML);
				static CLineInput s_RoomInput(g_Config.m_RcVoiceRoomName, sizeof(g_Config.m_RcVoiceRoomName));
				Ui()->DoEditBox(&s_RoomInput, &Button, EditBoxFontSize);
			}
			Column.HSplitTop(LineSize, &Label, &Column);
			Ui()->DoLabel(&Label, RCLocalize("Empty name uses the game server room", "RClient"), EditBoxFontSize, TEXTALIGN_ML);
			Column.HSplitTop(LineSize, &Label, &Column);
			Ui()->DoLabel(&Label, RCLocalize("Room changes apply on next voice connection", "RClient"), EditBoxFontSize, TEXTALIGN_ML);
		}
		if(MBiggestTab > Column.y - MCurrentY)
			Column.HSplitTop(MBiggestTab - (Column.y - MCurrentY), nullptr, &Column);

		CRCVoiceChat &VoiceChat = GameClient()->m_RcVoiceChat;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CButtonContainer s_VoiceUsersButton;
		if(DoButton_Menu(&s_VoiceUsersButton, RCLocalize("Player volume and mute", "RClient"), 0, &Button))
			m_VoiceUsersPage = true;

		const std::string VoiceError = VoiceChat.LastError();
		if(!VoiceError.empty())
		{
			char aError[128];
			str_copy(aError, VoiceError.c_str(), 120);
			Column.HSplitTop(LineSize, &Label, &Column);
			TextRender()->TextColor(1.0f, 0.55f, 0.55f, 1.0f);
			Ui()->DoLabel(&Label, aError, EditBoxFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CButtonContainer s_VoiceConnectBtn;
		const bool VoiceConnected = VoiceChat.Connected();
		const bool VoiceConnecting = VoiceChat.Connecting();
		const bool VoiceActive = VoiceConnected || VoiceConnecting;
		if(VoiceChat.MicrophoneTesting())
			Ui()->DoLabel(&Button, RCLocalize("Microphone test active", "RClient"), FontSize, TEXTALIGN_MC);
		else if(DoButton_Menu(&s_VoiceConnectBtn, VoiceConnected ? RCLocalize("Disconnect voice", "RClient") : (VoiceConnecting ? RCLocalize("Cancel connecting", "RClient") : RCLocalize("Connect voice", "RClient")), 0, &Button))
		{
			if(VoiceActive)
				VoiceChat.Disconnect();
			else
				VoiceChat.Connect();
		}
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** END OF PAGE 1 SETTINGS ***** //
	RightView = Column;

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsRClientRcon(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Label;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	const float ScrollOffset = MainView.y;
	static float s_PrevScrollOffset = 0.0f;

	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset - ScrollOffset;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** LeftView ***** //
	Column = LeftView;

	// ***** Base ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Main", "RClient"), HeadlineFontSize, TEXTALIGN_MC);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcRconAuthOnDummyConnect, RCLocalize("Auto login rcon on dummy", "RClient"), &g_Config.m_RcRconAuthOnDummyConnect, &Column, LineSize);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;

	// ***** Admin panel ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Admin Panel", "RClient"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		static CButtonContainer s_ReaderButtonAdminPanel, s_ClearButtonAdminPanel;
		DoLine_KeyReader(Column, s_ReaderButtonAdminPanel, s_ClearButtonAdminPanel, RCLocalize("Open Admin Panel", "RClient"), "rc_toggle_adminpanel");
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcAdminPanelPlaySounds, RCLocalize("Play sound on action", "RClient"), &g_Config.m_RcAdminPanelPlaySounds, &Column, LineSize);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** END OF PAGE 1 SETTINGS ***** //
	RightView = Column;

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsRClientChatBinds(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	const float ScrollOffset = MainView.y;
	static float s_PrevScrollOffset = 0.0f;

	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset - ScrollOffset;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** All the stuff ***** //

	auto DoBindchatDefault = [&](CUIRect &Column, CBindChat::CBindDefault &BindDefault) {
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CBindChat::CBind *pOldBind = GameClient()->m_BindChat.GetBind(BindDefault.m_Bind.m_aCommand);
		static char s_aTempName[BINDCHAT_MAX_NAME] = "";
		char *pName;
		if(pOldBind == nullptr)
			pName = s_aTempName;
		else
			pName = pOldBind->m_aName;
		if(DoEditBoxWithLabel(&BindDefault.m_LineInput, &Button, RCLocalize(BindDefault.m_pTitle), BindDefault.m_Bind.m_aName, pName, BINDCHAT_MAX_NAME) && BindDefault.m_LineInput.IsActive())
		{
			if(!pOldBind && pName[0] != '\0')
			{
				auto BindNew = BindDefault.m_Bind;
				str_copy(BindNew.m_aName, pName);
				GameClient()->m_BindChat.RemoveBind(pName); // Prevent duplicates
				GameClient()->m_BindChat.AddBind(BindNew);
				s_aTempName[0] = '\0';
			}
			if(pOldBind && pName[0] == '\0')
			{
				GameClient()->m_BindChat.RemoveBind(pName);
			}
		}
	};

	auto DoBindchatDefaults = [&](CUIRect &Column, const char *pTitle, std::vector<CBindChat::CBindDefault> &vBindchatDefaults) {
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		Ui()->DoLabel(&Label, pTitle, HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		for(CBindChat::CBindDefault &BindchatDefault : vBindchatDefaults)
			DoBindchatDefault(Column, BindchatDefault);
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	};

	float SizeL = 0.0f, SizeR = 0.0f;
	for(auto &[pTitle, vBindDefaults] : CBindChat::BIND_DEFAULTS_RCLIENT)
	{
		float &Size = SizeL > SizeR ? SizeR : SizeL;
		CUIRect &Column = SizeL > SizeR ? RightView : LeftView;
		DoBindchatDefaults(Column, RCLocalize(pTitle), vBindDefaults);
		Size += vBindDefaults.size() * (MarginSmall + LineSize) + HeadlineHeight + HeadlineFontSize + MarginSmall * 2.0f;
	}

	MainView.y = std::max(LeftView.y, RightView.y);
	CUIRect ResetBindsChat;
	MainView.HSplitTop(FontSize * 1.25f, &ResetBindsChat, &MainView);
	static CButtonContainer s_ResetBindsChat;
	if(GameClient()->m_Menus.DoButton_Menu(&s_ResetBindsChat, RCLocalize("Reset RClient chatbinds", "RClient"), 0, &ResetBindsChat, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 0.0f, 0.0f, 0.75f)))
	{
		GameClient()->m_RClient.ResetRClientChatBinds();
	}

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsRClientInfo(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("RClient Links", "RClient"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_GithubButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, RCLocalize("Discord", "RClient"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/xxVrBecVx9");
	if(DoButtonLineSize_Menu(&s_WebsiteButton, RCLocalize("Website", "RClient"), 0, &ButtonRight, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://rushie-client.ru/");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_GithubButton, RCLocalize("Github", "RClient"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://github.com/RushieClient/RushieClient-ddnet");

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 4.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Config Files", "RClient"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect TClientConfig, ProfilesFile;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&TClientConfig, &ProfilesFile, MarginSmall);

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, RCLocalize("RClient Settings", "RClient"), 0, &TClientConfig, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::RCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	// =======RIGHT VIEW========

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, RCLocalize("RClient Developers", "RClient"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = 50.0f;
	const float CardSize = TeeSize + MarginSmall;
	CUIRect TeeRect, DevCardRect;
	static CButtonContainer s_LinkButton1;
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Voix"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "Voix", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton1, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/1Voix1");
		RenderDevSkin(TeeRect.Center(), 50.0f, "Bomb 2", "bomb", false, 0, 0, 0, false, true);
	}

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, RCLocalize("Big Thanks To", "RClient"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	static CButtonContainer s_CarreyLinkButton1, s_CarreyLinkButton2;
	{
		CUIRect ButtonsBox;
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Carrey"), &Label, &ButtonsBox);
		Ui()->DoLabel(&Label, "Carrey", LineSize, TEXTALIGN_ML);
		ButtonsBox.VSplitLeft(MarginSmall, nullptr, &ButtonsBox);
		ButtonsBox.h = LineSize;
		ButtonsBox.w = LineSize * 2 + MarginSmall;
		ButtonsBox.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		ButtonsBox.VSplitLeft(LineSize, &Button, &ButtonsBox);
		if(Ui()->DoButton_FontIcon(&s_CarreyLinkButton1, FontIcon::RC_TWITCH, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://www.twitch.tv/icarreyj");
		ButtonsBox.VSplitLeft(MarginSmall, nullptr, &ButtonsBox);
		if(Ui()->DoButton_FontIcon(&s_CarreyLinkButton2, FontIcon::RC_DISCORD, 0, &ButtonsBox, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://discord.gg/BKUzXdDhy6");
		RenderDevSkin(TeeRect.Center(), 50.0f, "Apish Coke Carrey", "Apish Coke Carrey", false, 0, 0, 0, false, true);
	}

	{
		CUIRect ButtonsBox;
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Wee"), &Label, &ButtonsBox);
		Ui()->DoLabel(&Label, "Wee", LineSize, TEXTALIGN_ML);
		RenderDevSkin(TeeRect.Center(), 50.0f, "twinbop", "twinbop", true, 2490112, 1769216, 0, false, true);
	}

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, RCLocalize("Hide Settings Tabs", "test"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;

	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);
	RightView.HSplitTop(LineSize * 3.5f, nullptr, &RightView);

	const char *apTabNames[] = {
		RCLocalize("Settings", "RClient"),
		RCLocalize("Rcon", "RClient"),
		RCLocalize("Chat Binds", "RClient"),
		RCLocalize("Spec Wheel", "RClient")};
	static int s_aShowTabs[NUMBER_OF_RCLIENT_TABS] = {};
	for(int i = 0; i < NUMBER_OF_RCLIENT_TABS - 1; ++i)
	{
		if(i == RCLIENT_TAB_RCON && !Client()->RconAuthed())
			continue;
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabs[i], apTabNames[i], &s_aShowTabs[i], i % 2 == 0 ? &LeftSettings : &RightSettings, LineSize);
		SetFlag(g_Config.m_RcRClientSettingsTabs, i, s_aShowTabs[i]);
	}

	// RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	// Ui()->DoLabel(&Label, RCLocalize("Integration", "RClient"), HeadlineFontSize, TEXTALIGN_ML);
	// RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcDiscordRPC, RCLocalize("Enable Discord Integration", "RClient"), &g_Config.m_TcDiscordRPC, &RightView, LineSize);

	RightView.HSplitBottom(LineSize, &RightView, &Button);
	static CButtonContainer s_CrashButton;
	if(DoButton_Menu(&s_CrashButton, RCLocalize("Crash Client"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, GameClient()->m_RClient.m_CrashClientConfirm ? ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f) : ColorRGBA(1.0f, 0.0f, 0.0f, 0.1f)))
	{
		GameClient()->m_RClient.RcCrashClient();
	}
	RightView.HSplitBottom(MarginSmall, &RightView, nullptr);
	RightView.HSplitBottom(LineSize, &RightView, &Button);
	static CButtonContainer s_FavoriteMapsList;
	if(DoButton_Menu(&s_FavoriteMapsList, RCLocalize("Favorite Maps"), 0, &Button))
	{
		m_FavoritesMapsPage = true;
	}
}

CUi::EPopupMenuFunctionResult CMenusRClientConfirmAspect::Render(void *pContext, CUIRect View, bool Active)
{
	CMenusRClientConfirmAspect *pPopupContext = static_cast<CMenusRClientConfirmAspect *>(pContext);
	CUi *pUi = pPopupContext->m_pUi;
	CGameClient *pGameClient = pPopupContext->m_pGameClient;

	ColorRGBA Red(1.0f, 0.4f, 0.4f);
	ColorRGBA Green(0.4f, 1.0f, 0.4f);

	CUIRect Label, Countdown, Buttons, ConfirmButton, DenyButton;
	View.HSplitMid(&Label, &Buttons, Margin);
	Label.HSplitMid(&Label, &Countdown, MarginSmall);

	pUi->DoLabel(&Label, RCLocalize("Keep this settings?", "RClient"), HeadlineFontSize, TEXTALIGN_MC);

	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "Reset in %.2f", (pPopupContext->m_Timeout - time_get()) / (float)time_freq());
	pUi->DoLabel(&Countdown, aBuf, HeadlineFontSize / 2.0f, TEXTALIGN_MC);

	Buttons.VSplitMid(&DenyButton, &ConfirmButton, Margin);
	if(pUi->DoButton_PopupMenu(&pPopupContext->m_ConfirmButton, RCLocalize("Confirm", "RClient"), &ConfirmButton, FontSize, TEXTALIGN_MC, 0, false, true, Green))
		return CUi::POPUP_CLOSE_CURRENT;

	if(pUi->DoButton_PopupMenu(&pPopupContext->m_DenyButton, RCLocalize("Deny", "RClient"), &DenyButton, FontSize, TEXTALIGN_MC, 0, false, true, Red))
	{
		g_Config.m_RcCustomAspectX = pPopupContext->m_OldAspectX;
		g_Config.m_RcCustomAspectY = pPopupContext->m_OldAspectY;
		pGameClient->m_RClient.SetForcedAspectRatio();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(time_get() > pPopupContext->m_Timeout)
	{
		g_Config.m_RcCustomAspectX = pPopupContext->m_OldAspectX;
		g_Config.m_RcCustomAspectY = pPopupContext->m_OldAspectY;
		pGameClient->m_RClient.SetForcedAspectRatio();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::RenderSettingsRClientSpecWheel(CUIRect MainView)
{
	CUIRect LeftView, RightView, Label, Button;
	MainView.VSplitLeft(MainView.w / 2.1f, &LeftView, &RightView);

	const float Radius = std::min(RightView.w, RightView.h) / 2.0f;
	vec2 Center = RightView.Center();
	// Draw Circle
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f);
	Graphics()->DrawCircle(Center.x, Center.y, Radius, 64);
	Graphics()->QuadsEnd();

	static char s_aBindName[SPECWHEEL_MAX_NAME];
	static char s_aBindCommand[SPECWHEEL_MAX_CMD];

	static int s_SelectedBindIndex = -1;
	int HoveringIndex = -1;

	float MouseDist = distance(Center, Ui()->MousePos());
	const int SegmentCount = GameClient()->m_RcSpecWheel.m_vSpecBinds.size();
	if(MouseDist < Radius && MouseDist > Radius * 0.25f && SegmentCount > 0)
	{
		float SegmentAngle = 2.0f * pi / SegmentCount;

		float HoveringAngle = angle(Ui()->MousePos() - Center) + SegmentAngle / 2.0f;
		if(HoveringAngle < 0.0f)
			HoveringAngle += 2.0f * pi;

		HoveringIndex = (int)(HoveringAngle / (2.0f * pi) * SegmentCount);
		HoveringIndex = std::clamp(HoveringIndex, 0, SegmentCount - 1);
		if(Ui()->MouseButtonClicked(0))
		{
			s_SelectedBindIndex = HoveringIndex;
			str_copy(s_aBindName, GameClient()->m_RcSpecWheel.m_vSpecBinds[HoveringIndex].m_aName);
			str_copy(s_aBindCommand, GameClient()->m_RcSpecWheel.m_vSpecBinds[HoveringIndex].m_aCommand);
		}
		else if(Ui()->MouseButtonClicked(1) && s_SelectedBindIndex >= 0 && HoveringIndex >= 0 && HoveringIndex != s_SelectedBindIndex)
		{
			CSpecWheel::CBind BindA = GameClient()->m_RcSpecWheel.m_vSpecBinds[s_SelectedBindIndex];
			CSpecWheel::CBind BindB = GameClient()->m_RcSpecWheel.m_vSpecBinds[HoveringIndex];
			str_copy(GameClient()->m_RcSpecWheel.m_vSpecBinds[s_SelectedBindIndex].m_aName, BindB.m_aName);
			str_copy(GameClient()->m_RcSpecWheel.m_vSpecBinds[s_SelectedBindIndex].m_aCommand, BindB.m_aCommand);
			str_copy(GameClient()->m_RcSpecWheel.m_vSpecBinds[HoveringIndex].m_aName, BindA.m_aName);
			str_copy(GameClient()->m_RcSpecWheel.m_vSpecBinds[HoveringIndex].m_aCommand, BindA.m_aCommand);
		}
		else if(Ui()->MouseButtonClicked(2))
		{
			s_SelectedBindIndex = HoveringIndex;
		}
	}
	else if(MouseDist < Radius && Ui()->MouseButtonClicked(0))
	{
		s_SelectedBindIndex = -1;
		str_copy(s_aBindName, "");
		str_copy(s_aBindCommand, "");
	}

	const float Theta = pi * 2.0f / std::max<float>(1.0f, GameClient()->m_RcSpecWheel.m_vSpecBinds.size());
	for(int i = 0; i < static_cast<int>(GameClient()->m_RcSpecWheel.m_vSpecBinds.size()); i++)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		float SegmentFontSize = FontSize * 1.1f;
		if(i == s_SelectedBindIndex)
		{
			SegmentFontSize = FontSize * 1.7f;
			TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
		}
		else if(i == HoveringIndex)
		{
			SegmentFontSize = FontSize * 1.35f;
		}

		const CSpecWheel::CBind Bind = GameClient()->m_RcSpecWheel.m_vSpecBinds[i];
		const float Angle = Theta * i;

		const vec2 Pos = direction(Angle) * (Radius * 0.75f) + Center;
		const CUIRect Rect = CUIRect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
		Ui()->DoLabel(&Rect, Bind.m_aName, SegmentFontSize, TEXTALIGN_MC);
	}

	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, RCLocalize("Name:", "RClient"), FontSize, TEXTALIGN_ML);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
	s_NameInput.SetEmptyText(RCLocalize("Name", "RClient"));
	Ui()->DoEditBox(&s_NameInput, &Button, EditBoxFontSize);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, RCLocalize("Command:", "RClient"), FontSize, TEXTALIGN_ML);
	static CLineInput s_BindInput;
	s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
	s_BindInput.SetEmptyText(RCLocalize("Command", "RClient"));
	Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

	static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	if(DoButton_Menu(&s_OverrideButton, RCLocalize("Override Selected", "RClient"), 0, &Button) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_RcSpecWheel.m_vSpecBinds.size()))
	{
		CSpecWheel::CBind TempBind;
		if(str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		str_copy(GameClient()->m_RcSpecWheel.m_vSpecBinds[s_SelectedBindIndex].m_aName, TempBind.m_aName);
		str_copy(GameClient()->m_RcSpecWheel.m_vSpecBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	CUIRect ButtonAdd, ButtonRemove;
	Button.VSplitMid(&ButtonRemove, &ButtonAdd, MarginSmall);
	if(DoButton_Menu(&s_AddButton, RCLocalize("Add Bind", "RClient"), 0, &ButtonAdd))
	{
		CSpecWheel::CBind TempBind;
		if(str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		GameClient()->m_RcSpecWheel.AddBind(TempBind.m_aName, s_aBindCommand);
		s_SelectedBindIndex = static_cast<int>(GameClient()->m_RcSpecWheel.m_vSpecBinds.size()) - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, RCLocalize("Remove Bind", "RClient"), 0, &ButtonRemove) && s_SelectedBindIndex >= 0)
	{
		GameClient()->m_RcSpecWheel.RemoveBind(s_SelectedBindIndex);
		s_SelectedBindIndex = -1;
	}

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("The command is ran in console not chat", "RClient"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Use left mouse to select", "RClient"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Use right mouse to swap with selected", "RClient"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Use middle mouse select without copy", "RClient"), FontSize * 0.8f, TEXTALIGN_ML);

	LeftView.HSplitTop(LineSize, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("RClient \\/", "RClient"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Use %plnick% for replacing with nickname", "RClient"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Use %plid% for replacing with client id", "RClient"), FontSize * 0.8f, TEXTALIGN_ML);
	CUIRect RightBox;
	LeftView.HSplitTop(LineSize * 0.8f, &RightBox, &LeftView);
	RightBox.VSplitLeft(LineSize, nullptr, &RightBox);
	SLabelProperties Props;
	Props.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	Ui()->DoLabel(&RightBox, RCLocalize("Use bracket yourself \"%plnick%\"", "RClient"), FontSize * 0.8f, TEXTALIGN_ML, Props);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Example: echo \"\\\"%plnick%\\\" %plid%\"", "RClient"), FontSize * 0.8f, TEXTALIGN_ML);

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
	DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, RCLocalize("Spec Wheel Key", "RClient"), "+specwheel");

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcResetBindWheelMouse, RCLocalize("Reset position of mouse when opening specwheel", "RClient"), &g_Config.m_TcResetBindWheelMouse, &Label, LineSize);
}

void CMenus::RenderSettingsRClientFavoritesMaps(CUIRect MainView)
{
	CUIRect Header, BackButton;
	MainView.HSplitTop(LineSize, &Header, &MainView);
	Header.VSplitLeft(80.0f, &BackButton, &Header);
	static CButtonContainer s_BackButton;
	if(DoButton_Menu(&s_BackButton, RCLocalize("Back", "RClient"), 0, &BackButton))
	{
		m_FavoritesMapsPage = false;
		return;
	}
	Header.VSplitRight(80.0f, &Header, nullptr);
	Ui()->DoLabel(&Header, RCLocalize("Favorite Maps", "RClient"), 18.0f, TEXTALIGN_MC);
	MainView.HSplitTop(Margin, nullptr, &MainView);

	const auto &vMaps = GameClient()->m_RClient.FavoriteMaps();
	static CListBox s_ListBox;
	static int s_SelectedMap = -1;
	s_SelectedMap = std::clamp(s_SelectedMap, -1, static_cast<int>(vMaps.size()) - 1);

	static CLineInputBuffered<128> s_MapInput;
	static CLineInputBuffered<256> s_MapperInput;
	static CLineInputBuffered<128> s_CommunityInput;
	CUIRect Controls, Details;
	MainView.HSplitBottom(24.0f, &MainView, &Controls);
	MainView.HSplitBottom(Margin, &MainView, nullptr);

	char aDetail[512];
	str_format(aDetail, sizeof(aDetail), "%s: DDRaceNetwork", RCLocalize("Community"));
	const float DetailsWidth = std::ceil(TextRender()->TextWidth(FontSize, aDetail)) + Margin * 2.0f + 1.0f;
	MainView.VSplitRight(DetailsWidth, &MainView, &Details);
	MainView.VSplitRight(Margin, &MainView, nullptr);

	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	Props.m_EnableWidthCheck = false;

	CUIRect Name, Mapper, Community, Date;
	MainView.HSplitTop(28.0f, &Header, &MainView);
	Header.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_T, 5.0f);
	Header.VSplitRight(s_ListBox.ScrollbarWidthMax(), &Header, nullptr);
	Header.VMargin(4.0f, &Header);
	const float CellPadding = 8.0f;
	const float CommunityWidth = std::ceil(std::max(TextRender()->TextWidth(EditBoxFontSize, RCLocalize("Community")), TextRender()->TextWidth(EditBoxFontSize, "DDRaceNetwork"))) + CellPadding + 1.0f;
	const float DateWidth = std::ceil(TextRender()->TextWidth(EditBoxFontSize, "8888-88-88 88:88")) + CellPadding + 1.0f;
	const float MapperWidth = DateWidth;

	Header.VSplitRight(DateWidth, &Header, &Date);
	Header.VSplitRight(CommunityWidth, &Header, &Community);
	Header.VSplitRight(MapperWidth, &Name, &Mapper);
	Name.VMargin(4.0f, &Name);
	Mapper.VMargin(4.0f, &Mapper);
	Community.VMargin(4.0f, &Community);
	Date.VMargin(4.0f, &Date);
	Props.m_MaxWidth = Name.w;
	Ui()->DoLabel(&Name, RCLocalize("Map"), EditBoxFontSize, TEXTALIGN_ML, Props);
	Props.m_MaxWidth = Mapper.w;
	Ui()->DoLabel(&Mapper, RCLocalize("Mapper"), EditBoxFontSize, TEXTALIGN_ML, Props);
	Props.m_MaxWidth = Community.w;
	Ui()->DoLabel(&Community, RCLocalize("Community"), EditBoxFontSize, TEXTALIGN_ML, Props);
	Props.m_MaxWidth = Date.w;
	Ui()->DoLabel(&Date, RCLocalize("Added"), EditBoxFontSize, TEXTALIGN_ML, Props);

	s_ListBox.SetActive(!s_MapInput.IsActive() && !s_MapperInput.IsActive() && !s_CommunityInput.IsActive());
	s_ListBox.DoStart(32.0f, static_cast<int>(vMaps.size()), 1, 3, s_SelectedMap, &MainView, true, IGraphics::CORNER_B, true);
	for(const auto &Map : vMaps)
	{
		const CListboxItem Item = s_ListBox.DoNextItem(&Map);
		if(!Item.m_Visible)
			continue;

		CUIRect Row = Item.m_Rect;
		Row.VMargin(4.0f, &Row);
		Row.VSplitRight(DateWidth, &Row, &Date);
		Row.VSplitRight(CommunityWidth, &Row, &Community);
		Row.VSplitRight(MapperWidth, &Name, &Mapper);
		Name.VMargin(4.0f, &Name);
		Mapper.VMargin(4.0f, &Mapper);
		Community.VMargin(4.0f, &Community);
		Date.VMargin(4.0f, &Date);

		char aTime[32] = "-";
		if(Map.m_FavoriteTime >= 0)
			str_timestamp_ex(Map.m_FavoriteTime, aTime, sizeof(aTime), "%Y-%m-%d %H:%M");

		Props.m_MaxWidth = Name.w;
		Ui()->DoLabel(&Name, Map.m_MapName.c_str(), FontSize, TEXTALIGN_ML, Props);
		Props.m_MaxWidth = Mapper.w;
		if(Ui()->DoLabel(&Mapper, Map.m_MapperNickname.empty() ? "-" : Map.m_MapperNickname.c_str(), EditBoxFontSize, TEXTALIGN_ML, Props).m_Truncated && !Map.m_MapperNickname.empty())
			GameClient()->m_Tooltips.DoToolTip(&Map, &Mapper, Map.m_MapperNickname.c_str());
		Props.m_MaxWidth = Community.w;
		Ui()->DoLabel(&Community, Map.m_Community.empty() ? "-" : Map.m_Community.c_str(), EditBoxFontSize, TEXTALIGN_ML, Props);
		Props.m_MaxWidth = Date.w;
		Ui()->DoLabel(&Date, aTime, EditBoxFontSize, TEXTALIGN_ML, Props);
	}
	s_SelectedMap = s_ListBox.DoEnd();
	if(vMaps.empty())
		Ui()->DoLabel(&MainView, RCLocalize("No favorite maps yet"), FontSize, TEXTALIGN_MC);

	Details.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 5.0f);
	Details.Margin(Margin, &Details);
	const bool HasSelection = s_SelectedMap >= 0 && s_SelectedMap < static_cast<int>(vMaps.size());
	const auto *pSelectedMap = HasSelection ? &vMaps[s_SelectedMap] : nullptr;
	CUIRect InfoRow;
	Props.m_MaxWidth = Details.w;
	Details.HSplitTop(LineSize, &InfoRow, &Details);
	str_format(aDetail, sizeof(aDetail), "%s: %s", RCLocalize("MapName"), HasSelection ? pSelectedMap->m_MapName.c_str() : "-");
	if(Ui()->DoLabel(&InfoRow, aDetail, FontSize, TEXTALIGN_ML, Props).m_Truncated && HasSelection)
	{
		Ui()->DoButtonLogic(pSelectedMap->m_MapName.c_str(), 0, &InfoRow, BUTTONFLAG_NONE);
		GameClient()->m_Tooltips.DoToolTip(pSelectedMap->m_MapName.c_str(), &InfoRow, pSelectedMap->m_MapName.c_str());
	}
	Details.HSplitTop(LineSize, &InfoRow, &Details);
	str_format(aDetail, sizeof(aDetail), "%s: %s", RCLocalize("Mapper"), HasSelection && !pSelectedMap->m_MapperNickname.empty() ? pSelectedMap->m_MapperNickname.c_str() : "-");
	if(Ui()->DoLabel(&InfoRow, aDetail, FontSize, TEXTALIGN_ML, Props).m_Truncated && HasSelection && !pSelectedMap->m_MapperNickname.empty())
	{
		Ui()->DoButtonLogic(pSelectedMap->m_MapperNickname.c_str(), 0, &InfoRow, BUTTONFLAG_NONE);
		GameClient()->m_Tooltips.DoToolTip(pSelectedMap->m_MapperNickname.c_str(), &InfoRow, pSelectedMap->m_MapperNickname.c_str());
	}
	Details.HSplitTop(LineSize, &InfoRow, &Details);
	str_format(aDetail, sizeof(aDetail), "%s: %s", RCLocalize("Community"), HasSelection && !pSelectedMap->m_Community.empty() ? pSelectedMap->m_Community.c_str() : "-");
	Ui()->DoLabel(&InfoRow, aDetail, FontSize, TEXTALIGN_ML, Props);
	Details.HSplitTop(LineSize, &InfoRow, &Details);
	char aTime[32] = "-";
	if(HasSelection && pSelectedMap->m_FavoriteTime >= 0)
		str_timestamp_ex(pSelectedMap->m_FavoriteTime, aTime, sizeof(aTime), "%Y-%m-%d %H:%M");
	str_format(aDetail, sizeof(aDetail), "%s: %s", RCLocalize("Added"), aTime);
	Ui()->DoLabel(&InfoRow, aDetail, FontSize, TEXTALIGN_ML, Props);

	static CButtonContainer s_MapButton, s_CopyMapButton, s_CopyMapCommandButton, s_RemoveButton;
	CUIRect Button;
	Details.HSplitTop(MarginSmall, nullptr, &Details);
	Details.HSplitTop(24.0f, &Button, &Details);
	const bool CanChangeMap = HasSelection && Client()->State() == IClient::STATE_ONLINE;
	const std::string MapCommand = HasSelection ? "/map " + pSelectedMap->m_MapName : "";
	if(DoButton_Menu(&s_MapButton, RCLocalize("Switch Map"), CanChangeMap ? 0 : -1, &Button))
		GameClient()->m_Chat.SendChat(0, MapCommand.c_str(), true);
	Details.HSplitTop(MarginSmall, nullptr, &Details);
	Details.HSplitTop(24.0f, &Button, &Details);
	if(DoButton_Menu(&s_CopyMapButton, RCLocalize("Copy Map"), HasSelection ? 0 : -1, &Button))
		Input()->SetClipboardText(pSelectedMap->m_MapName.c_str());
	Details.HSplitTop(MarginSmall, nullptr, &Details);
	Details.HSplitTop(24.0f, &Button, &Details);
	if(DoButton_Menu(&s_CopyMapCommandButton, RCLocalize("Copy /map"), HasSelection ? 0 : -1, &Button))
		Input()->SetClipboardText(MapCommand.c_str());
	CUIRect RemoveButton;
	Details.HSplitBottom(24.0f, &Details, &RemoveButton);
	Details.HSplitBottom(MarginSmall, &Details, nullptr);
	Details.HSplitBottom(LineSize, nullptr, &Button);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowFavoriteMapIcon, RCLocalize("Show icon in server list"), &g_Config.m_RcShowFavoriteMapIcon, &Button, LineSize);
	if(DoButton_Menu(&s_RemoveButton, RCLocalize("Remove selected"), HasSelection ? 0 : -1, &RemoveButton))
	{
		GameClient()->m_RClient.RemoveFavoriteMapByIndex(s_SelectedMap);
		s_SelectedMap = -1;
	}

	CUIRect MapInput, MapperInput, CommunityInput, AddButton;
	Controls.VSplitRight(80.0f, &Controls, &AddButton);
	Controls.VSplitRight(MarginSmall, &Controls, nullptr);
	const float InputWidth = (Controls.w - MarginSmall * 2.0f) / 3.0f;
	Controls.VSplitLeft(InputWidth, &MapInput, &Controls);
	Controls.VSplitLeft(MarginSmall, nullptr, &Controls);
	Controls.VSplitLeft(InputWidth, &MapperInput, &Controls);
	Controls.VSplitLeft(MarginSmall, nullptr, &CommunityInput);
	s_MapInput.SetEmptyText(RCLocalize("Map"));
	s_MapperInput.SetEmptyText(RCLocalize("Mapper"));
	s_CommunityInput.SetEmptyText(RCLocalize("Community"));
	Ui()->DoEditBox(&s_MapInput, &MapInput, EditBoxFontSize);
	Ui()->DoEditBox(&s_MapperInput, &MapperInput, EditBoxFontSize);
	Ui()->DoEditBox(&s_CommunityInput, &CommunityInput, EditBoxFontSize);
	static CButtonContainer s_AddButton;
	const bool CanAdd = *str_utf8_skip_whitespaces(s_MapInput.GetString()) != '\0';
	if(DoButton_Menu(&s_AddButton, RCLocalize("Add"), CanAdd ? 0 : -1, &AddButton))
	{
		GameClient()->m_RClient.AddFavoriteMap(s_MapInput.GetString(), s_MapperInput.GetString(), s_CommunityInput.GetString());
		s_SelectedMap = static_cast<int>(vMaps.size()) - 1;
		s_ListBox.ScrollToSelected();
		s_MapInput.Clear();
		s_MapperInput.Clear();
		s_CommunityInput.Clear();
	}
}
