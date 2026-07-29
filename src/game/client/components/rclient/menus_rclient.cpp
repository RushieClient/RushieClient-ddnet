#include "engine/font_icons.h"
#include "game/client/animstate.h"
#include "game/client/components/menus.h"
#include "game/client/gameclient.h"
#include "game/client/ui_listbox.h"
#include "game/localization.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/types.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

enum
{
	RCLIENT_TAB_SETTINGS = 0,
	RCLIENT_TAB_BINDCHAT,
	RCLIENT_TAB_INFO,
	NUMBER_OF_RCLIENT_TABS
};

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
	const float TabWidth = TabBar.w / TabCount;
	static CButtonContainer s_aPageTabs[NUMBER_OF_RCLIENT_TABS] = {};
	const char *apTabNames[] = {
		RCLocalize("Settings"),
		RCLocalize("Chat Binds"),
		RCLocalize("Info")};

	for(int Tab = 0; Tab < NUMBER_OF_RCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_RcRClientSettingsTabs, Tab))
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
	if(s_CurCustomTab == RCLIENT_TAB_BINDCHAT)
		RenderSettingsRClientChatBinds(MainView);
	if(s_CurCustomTab == RCLIENT_TAB_INFO)
		RenderSettingsRClientInfo(MainView);
}

void CMenus::RenderSettingsRClientSettings(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.y += ScrollOffset.y;

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
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** LeftView ***** //
	Column = LeftView;

	// ***** Visual Miscellaneous ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Player Inspector/Tracker"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(LineSize, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Now in bindchat"), FontSize, TEXTALIGN_ML);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Dummy Change Clan ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Dummy Change Clan"), HeadlineFontSize, TEXTALIGN_ML);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcPlayerClanAutoChange, RCLocalize("Auto change clan"), &g_Config.m_RcPlayerClanAutoChange, &Column, LineSize);
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, RCLocalize("With Dummy"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_RcPlayerClanWithDummy, sizeof(g_Config.m_RcPlayerClanWithDummy));
		s_LineInput.SetEmptyText(RCLocalize("#YESDUMMY"));
		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, RCLocalize("Without Dummy"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_RcPlayerClanNoDummy, sizeof(g_Config.m_RcPlayerClanNoDummy));
		s_LineInput.SetEmptyText(RCLocalize("#NODUMMY"));
		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Dummy***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Dummy"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyPosition, TCLocalize("Show dummy position"), &g_Config.m_TcShowhudDummyPosition, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummySpeed, TCLocalize("Show dummy speed"), &g_Config.m_TcShowhudDummySpeed, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyAngle, TCLocalize("Show dummy target angle"), &g_Config.m_TcShowhudDummyAngle, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudAdvancedDummyActions, TCLocalize("Show advanced dummy actions"), &g_Config.m_RcShowhudAdvancedDummyActions, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Master servers ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Master Servers"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcUseRushieMasterServerMirrors, TCLocalize("Use Rushie master server mirror"), &g_Config.m_RcUseRushieMasterServerMirrors, &Column, LineSize);
	if(g_Config.m_RcUseRushieMasterServerMirrors)
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcUseBestClientMasterServerMirrors, TCLocalize("Use BestClient master server mirror"), &g_Config.m_RcUseBestClientMasterServerMirrors, &Column, LineSize);
	else
		Column.HSplitTop(LineSize, nullptr, &Column);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Auto translate ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Auto translate"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTranslateAuto, TCLocalize("Use auto translate"), &g_Config.m_TcTranslateAuto, &Column, LineSize);
	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, RCLocalize("Your language target (in ISO 639 code)"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget));
		s_LineInput.SetEmptyText(RCLocalize("ru"));
		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcTranslateSend, TCLocalize("Translate your messages"), &g_Config.m_RcTranslateSend, &Column, LineSize);
	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, RCLocalize("Send language target (in ISO 639 code)"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_RcTranslateSendTarget, sizeof(g_Config.m_RcTranslateSendTarget));
		s_LineInput.SetEmptyText(RCLocalize("en"));
		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Scoreboard ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Scoreboard"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_ReaderButtonScoreboardMouse, s_ClearButtonScoreboardmouse;
	DoLine_KeyReader(Column, s_ReaderButtonScoreboardMouse, s_ClearButtonScoreboardmouse, TCLocalize("Enable scoreboard mouse"), "toggle_scoreboard_cursor");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowHeartInScoreboard, RCLocalize("Show friend's heart in scoreboard"), &g_Config.m_RcShowHeartInScoreboard, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcSizeOfHeart, &g_Config.m_RcSizeOfHeart, &Button, RCLocalize("Heart size"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcScoreboardAlwaysShowQuickActions, RCLocalize("Always show quick actions in popup"), &g_Config.m_RcScoreboardAlwaysShowQuickActions, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcScoreboardFreezeInputs, RCLocalize("Freeze inputs when popup opened"), &g_Config.m_RcScoreboardFreezeInputs, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Chat Bubbles ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Chat Bubbles (Entity)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatBubbles, RCLocalize("Enable Chat Bubbles"), &g_Config.m_RcChatBubbles, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcChatBubbleSize, &g_Config.m_RcChatBubbleSize, &Button, RCLocalize("Chat bubble size"), 15, 30, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcChatBubbleShowTime, &g_Config.m_RcChatBubbleShowTime, &Button, RCLocalize("Show time"), 200, 1000, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcChatBubbleFadeOut, &g_Config.m_RcChatBubbleFadeOut, &Button, RCLocalize("Fade out time"), 15, 100, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcChatBubbleFadeIn, &g_Config.m_RcChatBubbleFadeIn, &Button, RCLocalize("Fade in time"), 15, 100, &CUi::ms_LinearScrollbarScale, 0);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Chat Features ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Chat Features"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCommandsFixLayout, RCLocalize("Fix layout"), &g_Config.m_RcCommandsFixLayout, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Players ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Players"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcHideFrozenFlakesEffect, RCLocalize("Hide frozen flakes"), &g_Config.m_RcHideFrozenFlakesEffect, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowSparkleEffect, RCLocalize("Always show sparkles"), &g_Config.m_RcShowSparkleEffect, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAfkEmoteInMenu, RCLocalize("Show AFK emote in menu"), &g_Config.m_RcShowAfkEmoteInMenu, &Column, LineSize);
	{
		CUIRect RightSide;
		Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAfkTextureInMenu, TCLocalize("Show texture instead emote in menu"), &g_Config.m_RcShowAfkTextureInMenu, &RightSide, LineSize);
		Column.HSplitTop(LineSize, nullptr, &Column);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAfkEmoteInSpec, RCLocalize("Show AFK emote in spec"), &g_Config.m_RcShowAfkEmoteInSpec, &Column, LineSize);
	{
		CUIRect RightSide;
		Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAfkTextureInSpec, TCLocalize("Show texture instead emote in spec"), &g_Config.m_RcShowAfkTextureInSpec, &RightSide, LineSize);
		Column.HSplitTop(LineSize, nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;

	// ***** Binds ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Binds"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
				s_ReaderButton45degrees, s_ClearButton45degrees,
				s_ReaderButtonSmallsens, s_ClearButtonSmallsens;
	DoLine_KeyReader(Column, s_ReaderButtonDeepfly, s_ClearButtonDeepfly, TCLocalize("Toggle deepfly"), "rc_toggle_deepfly");
	{
		CUIRect RightSide;
		Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcDeepFlyOnRMB, TCLocalize("Deepfly on rmb"), &g_Config.m_RcDeepFlyOnRMB, &RightSide, LineSize);
		Column.HSplitTop(LineSize, nullptr, &Column);
	}
	DoLine_KeyReader(Column, s_ReaderButton45degrees, s_ClearButton45degrees, TCLocalize("Toggle 45degrees"), "+rc_45_degrees");
	{
		CUIRect RightSide;
		Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcToggle45degrees, TCLocalize("Toggle 45 degrees"), &g_Config.m_RcToggle45degrees, &RightSide, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Rc45degreesEcho, TCLocalize("Echo 45 degrees"), &g_Config.m_Rc45degreesEcho, &RightSide, LineSize);
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	}
	DoLine_KeyReader(Column, s_ReaderButtonSmallsens, s_ClearButtonSmallsens, TCLocalize("Toggle smallsens"), "+rc_small_sens");
	{
		CUIRect RightSide;
		Column.VSplitLeft(MarginBetweenViews, nullptr, &RightSide);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcToggleSmallSens, TCLocalize("Toggle small sens"), &g_Config.m_RcToggleSmallSens, &RightSide, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSmallSensEcho, TCLocalize("Echo small sens"), &g_Config.m_RcSmallSensEcho, &RightSide, LineSize);
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Chat Filter ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Chat Filter"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static std::vector<CButtonContainer> s_vButtonContainersChatFilter = {{}, {}, {}, {}};
	DoLine_RadioMenu(Column, TCLocalize("Chat Filter Mode"),
		   s_vButtonContainersChatFilter,
		   {Localize("Off"), Localize("Partial"), Localize("Full"), Localize("Both")},
		   {0, 1, 2, 3},
		   g_Config.m_RcMessageFilterMode);
	static CButtonContainer s_BlockedMessageColor;
	DoButton_ColorPickerAutoVMargin(&s_BlockedMessageColor, Localize("Print Blocked Message"), &g_Config.m_RcMessageFilterPrintBlockedMessageColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcMessageFilterPrintBlockedMessageColor)), &Column, LineSize, false, &g_Config.m_RcMessageFilterPrintBlockedMessage);
	if(g_Config.m_RcMessageFilterMode == 1 || g_Config.m_RcMessageFilterMode == 3)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcMessageFilterMultiplyChangeWordOnPartialMatch, TCLocalize("Multiply Partial Match"), &g_Config.m_RcMessageFilterMultiplyChangeWordOnPartialMatch, &Column, LineSize);
		{
			CUIRect Box;
			Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
			Box.VSplitMid(&Label, &Button);
			Ui()->DoLabel(&Label, RCLocalize("Partial Word Replacement"), FontSize, TEXTALIGN_ML);
			static CLineInput s_LineInput(g_Config.m_RcMessageFilterWordOnPartialMatch, sizeof(g_Config.m_RcMessageFilterWordOnPartialMatch));
			s_LineInput.SetEmptyText(RCLocalize("*"));
			Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
		}
	}
	if(g_Config.m_RcMessageFilterMode == 2 || g_Config.m_RcMessageFilterMode == 3)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcMessageFilterMultiplyChangeWordOnFullMatch, TCLocalize("Multiply Full Match"), &g_Config.m_RcMessageFilterMultiplyChangeWordOnFullMatch, &Column, LineSize);
		{
			CUIRect Box;
			Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
			Box.VSplitMid(&Label, &Button);
			Ui()->DoLabel(&Label, RCLocalize("Full Word Replacement"), FontSize, TEXTALIGN_ML);
			static CLineInput s_LineInput(g_Config.m_RcMessageFilterWordOnFullMatch, sizeof(g_Config.m_RcMessageFilterWordOnFullMatch));
			s_LineInput.SetEmptyText(RCLocalize("^"));
			Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
		}
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Edge info ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Edge info"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_ReaderButtonEdgeInfo, s_ClearButtonEdgeInfo;
	DoLine_KeyReader(Column, s_ReaderButtonEdgeInfo, s_ClearButtonEdgeInfo, TCLocalize("Toggle Edge info"), "rc_toggle_edge_info");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcEdgeInfoCords, RCLocalize("Show coordinates info"), &g_Config.m_RcEdgeInfoCords, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcEdgeInfoJump, RCLocalize("Show jump info"), &g_Config.m_RcEdgeInfoJump, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcEdgeInfoPosX, &g_Config.m_RcEdgeInfoPosX, &Button, RCLocalize("Edge info X pos"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcEdgeInfoPosY, &g_Config.m_RcEdgeInfoPosY, &Button, RCLocalize("Edge info Y pos"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);
	static CButtonContainer s_EdgeInfoFreezeColor, s_EdgeInfoKillColor, s_EdgeInfoSafeColor;
	DoButton_ColorPickerAutoVMargin(&s_EdgeInfoFreezeColor, Localize("Above Freeze color"), &g_Config.m_RcEdgeInfoColorFreeze, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcEdgeInfoColorFreeze)), &Column, LineSize, false);
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	DoButton_ColorPickerAutoVMargin(&s_EdgeInfoKillColor, Localize("Above Kill color"), &g_Config.m_RcEdgeInfoColorKill, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcEdgeInfoColorKill)), &Column, LineSize, false);
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	DoButton_ColorPickerAutoVMargin(&s_EdgeInfoSafeColor, Localize("Above Freeze color"), &g_Config.m_RcEdgeInfoColorSafe, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcEdgeInfoColorSafe)), &Column, LineSize, false);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Hud ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Hud"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudPlayerCheckpoint, RCLocalize("Show checkpoint"), &g_Config.m_RcShowhudPlayerCheckpoint, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudSmallerHud, RCLocalize("Smaller hud (angle,checkpoint)"), &g_Config.m_RcShowhudSmallerHud, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcSizeOfHeart, &g_Config.m_RcSizeOfHeart, &Button, RCLocalize("Heart size"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Nameplate ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Nameplates"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static std::vector<CButtonContainer> s_vButtonContainersHookDetection = {{}, {}, {}, {}, {}};
	DoLine_RadioMenu(Column, TCLocalize("Hook Detection"),
		   s_vButtonContainersHookDetection,
		   {Localize("Off"), Localize("Others"), Localize("All"), Localize("Own"), "Dummy"},
		   {0, 1, 2, 3, 4},
		   g_Config.m_RcNamePlatesHook);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcNamePlatesHookSize, &g_Config.m_RcNamePlatesHookSize, &Button, RCLocalize("Hook size"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesHookShiftOnInvis, RCLocalize("Hook shift on invis"), &g_Config.m_RcNamePlatesHookShiftOnInvis, &Column, LineSize);
	static std::vector<CButtonContainer> s_vButtonContainersFireDetection = {{}, {}, {}, {}, {}};
	DoLine_RadioMenu(Column, TCLocalize("Fire Detection"),
		   s_vButtonContainersFireDetection,
		   {Localize("Off"), Localize("Others"), Localize("All"), Localize("Own"), "Dummy"},
		   {0, 1, 2, 3, 4},
		   g_Config.m_RcNamePlatesFire);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcNamePlatesFireSize, &g_Config.m_RcNamePlatesFireSize, &Button, RCLocalize("Fire size"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesFireShiftOnInvis, RCLocalize("Fire shift on invis"), &g_Config.m_RcNamePlatesFireShiftOnInvis, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Anti AFK ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Anti AFK"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcPlayOnMoveNonInactive, RCLocalize("Play sound when moved and window non active"), &g_Config.m_RcPlayOnMoveNonInactive, &Column, LineSize);
	static std::vector<CButtonContainer> s_vButtonContainersNonActive = {{}, {}, {}};
	DoLine_RadioMenu(Column, TCLocalize("Choose sound non active"),
		   s_vButtonContainersNonActive,
		   {Localize("Wake up"), Localize("Grenade"), Localize("Tag")},
		   {0, 1, 2},
		   g_Config.m_RcSoundOnMoveNonInactive);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNotifyOnMoveInSpec, RCLocalize("Notify when moved in spec"), &g_Config.m_RcNotifyOnMoveInSpec, &Column, LineSize);
	if(g_Config.m_RcNotifyOnMoveInSpec)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcPlayOnMoveInSpec, RCLocalize("Play sound when moved in spec"), &g_Config.m_RcPlayOnMoveInSpec, &Column, LineSize);
		static std::vector<CButtonContainer> s_vButtonContainersInSpec = {{}, {}, {}};
		DoLine_RadioMenu(Column, TCLocalize("Choose sound in spec"),
			   s_vButtonContainersInSpec,
			   {Localize("Wake up"), Localize("Grenade"), Localize("Tag")},
			   {0, 1, 2},
			   g_Config.m_RcSoundOnMoveInSpec);
	}
	else
		Column.HSplitTop(LineSize * 2 + 2.0f, nullptr, &Column); // 2.0f for radio menu

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Custom Clients ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Client Indicator"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomClientsSendClientType, RCLocalize("Send to server that u use RClient"), &g_Config.m_RcCustomClientsSendClientType, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomClientsCollectClientType, RCLocalize("Detect other clients"), &g_Config.m_RcCustomClientsCollectClientType, &Column, LineSize);
	static std::vector<CButtonContainer> s_vButtonContainersCustomInNameplates = {{}, {}, {}, {}};
	DoLine_RadioMenu(Column, TCLocalize("Show client type in nameplates"),
		   s_vButtonContainersCustomInNameplates,
		   {Localize("Off"), Localize("Others"), Localize("Everyone"), Localize("Only you")},
		   {0, 1, 2, 3},
		   g_Config.m_RcCustomClientsInNameplates);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomClientsInScoreboard, RCLocalize("Show client type in scoreboard"), &g_Config.m_RcCustomClientsInScoreboard, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** END OF PAGE 1 SETTINGS ***** //
	RightView = Column;

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsRClientChatBinds(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.y += ScrollOffset.y;

	MainView.HSplitTop(Margin, nullptr, &MainView);
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
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
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

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + MarginSmall * 2.0f;
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
	Ui()->DoLabel(&Label, RCLocalize("RClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_GithubButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, RCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/xxVrBecVx9");
	if(DoButtonLineSize_Menu(&s_WebsiteButton, RCLocalize("Website"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://rushie-client.ru/");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_GithubButton, RCLocalize("Github"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://github.com/RushieClient/RushieClient-ddnet");

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 4.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, RCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect TClientConfig, ProfilesFile;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&TClientConfig, &ProfilesFile, MarginSmall);

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, RCLocalize("RClient Settings"), 0, &TClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::RCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	// =======RIGHT VIEW========

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, RCLocalize("RClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
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
		RenderDevSkin(TeeRect.Center(), 50.0f, "Bomb 2", "bomb", false, 0, 0, 0, false, true, ColorRGBA(0.92f, 0.29f, 0.48f, 1.0f), ColorRGBA(0.55f, 0.64f, 0.76f, 1.0f));
	}

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, RCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;

	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);
	RightView.HSplitTop(LineSize * 3.5f, nullptr, &RightView);

	const char *apTabNames[] = {
		RCLocalize("Settings"),
		RCLocalize("Chat Binds"),
		RCLocalize("Info")};
	static int s_aShowTabs[NUMBER_OF_RCLIENT_TABS] = {};
	for(int i = 0; i < NUMBER_OF_RCLIENT_TABS - 1; ++i)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabs[i], apTabNames[i], &s_aShowTabs[i], i % 2 == 0 ? &LeftSettings : &RightSettings, LineSize);
		SetFlag(g_Config.m_RcRClientSettingsTabs, i, s_aShowTabs[i]);
	}

	// RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	// Ui()->DoLabel(&Label, RCLocalize("Integration"), HeadlineFontSize, TEXTALIGN_ML);
	// RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcDiscordRPC, RCLocalize("Enable Discord Integration"), &g_Config.m_TcDiscordRPC, &RightView, LineSize);
}