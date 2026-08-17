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

#include "rclient_include.h"

enum
{
	RCLIENT_TAB_SETTINGS = 0,
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
		RCLocalize("Spec Wheel"),
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
	if(s_CurCustomTab == RCLIENT_TAB_SPECWHEEL)
		RenderSettingsRClientSpecWheel(MainView);
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

	// ***** Dummy***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Dummy"), HeadlineFontSize, TEXTALIGN_MC);
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
			RCLocalize("Position"),
			RCLocalize("Clan Change")};

		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_DUMMY_TABS, s_aPageTabs, s_CurDummyCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(s_CurDummyCustomTab == DUMMY_TAB_POS)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyPosition, TCLocalize("Show dummy position"), &g_Config.m_TcShowhudDummyPosition, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummySpeed, TCLocalize("Show dummy speed"), &g_Config.m_TcShowhudDummySpeed, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowhudDummyAngle, TCLocalize("Show dummy target angle"), &g_Config.m_TcShowhudDummyAngle, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudAdvancedDummyActions, TCLocalize("Show advanced dummy actions"), &g_Config.m_RcShowhudAdvancedDummyActions, &Column, LineSize);
		}
		if(s_CurDummyCustomTab == DUMMY_TAB_CLAN)
		{
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
		}
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Scoreboard ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Scoreboard"), HeadlineFontSize, TEXTALIGN_MC);
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
			RCLocalize("Popup"),
			RCLocalize("Friends")};

		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_SCOREBOARD_TABS, s_aPageTabs, s_CurScoreboardCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(s_CurScoreboardCustomTab == SCOREBOARD_TAB_POPUP)
		{
			static CButtonContainer s_ReaderButtonScoreboardMouse, s_ClearButtonScoreboardmouse;
			DoLine_KeyReader(Column, s_ReaderButtonScoreboardMouse, s_ClearButtonScoreboardmouse, TCLocalize("Enable scoreboard mouse"), "toggle_scoreboard_cursor");
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcScoreboardAlwaysShowQuickActions, RCLocalize("Always show quick actions in popup"), &g_Config.m_RcScoreboardAlwaysShowQuickActions, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcScoreboardFreezeInputs, RCLocalize("Freeze inputs when popup opened"), &g_Config.m_RcScoreboardFreezeInputs, &Column, LineSize);
		}
		if(s_CurScoreboardCustomTab == SCOREBOARD_TAB_FRIENDS)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowHeartInScoreboard, RCLocalize("Show friend's heart in scoreboard"), &g_Config.m_RcShowHeartInScoreboard, &Column, LineSize);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcSizeOfHeart, &g_Config.m_RcSizeOfHeart, &Button, RCLocalize("Heart size"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
		}
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Players ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Players"), HeadlineFontSize, TEXTALIGN_MC);
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
			RCLocalize("Effects"),
			RCLocalize("AFK"),
			RCLocalize("Hitbox")
		};
		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_PLAYERS_TABS, s_aPageTabs, s_CurPlayersCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(s_CurPlayersCustomTab == PLAYERS_TAB_EFFECTS)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcHideFrozenFlakesEffect, RCLocalize("Hide frozen flakes"), &g_Config.m_RcHideFrozenFlakesEffect, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowSparkleEffect, RCLocalize("Always show sparkles"), &g_Config.m_RcShowSparkleEffect, &Column, LineSize);
		}
		if(s_CurPlayersCustomTab == PLAYERS_TAB_AFK)
		{
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
		}
		if(s_CurPlayersCustomTab == PLAYERS_TAB_HITBOX)
		{
			static std::vector<CButtonContainer> s_vButtonContainersPlayersHitbox = {{}, {}, {}, {}};
			DoLine_RadioMenu(Column, TCLocalize("Show player hitbox"),
				   s_vButtonContainersPlayersHitbox,
				   {Localize("Off"), Localize("Others"), Localize("Everyone"), Localize("Own")},
				   {0, 1, 2, 3},
				   g_Config.m_RcShowHitbox);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcShowHitboxSize, &g_Config.m_RcShowHitboxSize, &Button, RCLocalize("Size of hitbox"), 1, 100, &CUi::ms_LinearScrollbarScale, 0);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcShowHitboxQuality, &g_Config.m_RcShowHitboxQuality, &Button, RCLocalize("Quality of hitbox"), 1, 32, &CUi::ms_LinearScrollbarScale, 0);
			static CButtonContainer s_HitboxColor;
			DoButton_ColorPickerAutoVMargin(&s_HitboxColor, Localize("Hitbox color"), &g_Config.m_RcShowHitboxColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcShowHitboxColor)), &Column, LineSize, true);
		}
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Chat Bubbles ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Chat Bubbles"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(Margin, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("by qxdFox/Entity Client"), Margin, TEXTALIGN_MC);
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

	// ***** Master servers ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Master Servers"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcUseRushieMasterServerMirrors, TCLocalize("Use Rushie master server mirror"), &g_Config.m_RcUseRushieMasterServerMirrors, &Column, LineSize);
	if(g_Config.m_RcUseRushieMasterServerMirrors)
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcUseBestClientMasterServerMirrors, TCLocalize("Use BestClient master server mirror"), &g_Config.m_RcUseBestClientMasterServerMirrors, &Column, LineSize);
	else
		Column.HSplitTop(LineSize, nullptr, &Column);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Integrations"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcDiscordRPC, RCLocalize("Discord RPC"), &g_Config.m_TcDiscordRPC, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSteam, RCLocalize("Steam"), &g_Config.m_RcSteam, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Aspect Ratio"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCustomAspectEnabled, RCLocalize("Enable custom aspect ratio"), &g_Config.m_RcCustomAspectEnabled, &Column, LineSize);
	float BoxSize = (Column.w - Margin * 2) / 3;
	CUIRect Boxes, ToolTipsLabel;
	Column.HSplitTop(LineSize, &Boxes, &Column);
	Column.HSplitTop(Margin, &ToolTipsLabel, &Column);
	Boxes.VSplitLeft(BoxSize, &Button, &Boxes);
	ToolTipsLabel.VSplitLeft(BoxSize, &Label, &ToolTipsLabel);
	Ui()->DoLabel(&Label, RCLocalize("Width"), Margin, TEXTALIGN_MC);
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
	Ui()->DoLabel(&Label, RCLocalize("Height"), Margin, TEXTALIGN_MC);
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
	const float AspectConfirmTimeoutSec = 10.0f;
	if(DoButton_Menu(&s_ApplyBtnAspect, RCLocalize("Apply"), 0, &Button))
	{
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

		SPopupMenuProperties m_Props;
		m_Props.m_CloseAtClickOutside = false;
		m_Props.m_CloseAtEscape = false;
		Ui()->DoPopupMenu(&s_AspectConfirmPopupContext, pScreen->w / 2.0f - PopupW / 2.0f, pScreen->h / 2.0f - PopupH / 2.0f, PopupW, PopupH, &s_AspectConfirmPopupContext, CMenusRClientConfirmAspect::Render, m_Props);
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableAll = 0;
	if(DoButton_CheckBox(&s_AspectDisableAll, RCLocalize("Disable for almost all UI"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::ALL, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::ALL;
	if(!(g_Config.m_RcCustomAspectDisable & RcAspectDisable::ALL))
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableWheels = 0;
		if(DoButton_CheckBox(&s_AspectDisableWheels, RCLocalize("Disable for Wheels"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::WHEELS, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::WHEELS;

		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableMenus = 0;
		if(DoButton_CheckBox(&s_AspectDisableMenus, RCLocalize("Disable for Menus"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::MENUS, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::MENUS;

		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableScoreboard = 0;
		if(DoButton_CheckBox(&s_AspectDisableScoreboard, RCLocalize("Disable for Scoreboard"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::SCOREBOARD, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::SCOREBOARD;

		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableConsole = 0;
		if(DoButton_CheckBox(&s_AspectDisableConsole, RCLocalize("Disable for Console"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::CONSOLE, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::CONSOLE;

		Column.HSplitTop(LineSize, &Button, &Column);
		static int s_AspectDisableAboveBelow = 0;
		if(DoButton_CheckBox(&s_AspectDisableAboveBelow, RCLocalize("Disable for Above/Below Player"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::ABOVEBELOWPL, &Button))
			g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::ABOVEBELOWPL;

	}
	else
		Column.HSplitTop(LineSize * 5, nullptr, &Column);

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableHud = 0;
	if(DoButton_CheckBox(&s_AspectDisableHud, RCLocalize("Disable for Hud"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::HUD, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::HUD;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableChat = 0;
	if(DoButton_CheckBox(&s_AspectDisableChat, RCLocalize("Disable for Chat"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::CHAT, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::CHAT;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableEdgeinfo = 0;
	if(DoButton_CheckBox(&s_AspectDisableEdgeinfo, RCLocalize("Disable for Edge info"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::EDGEINFO, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::EDGEINFO;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableInfoMessages = 0;
	if(DoButton_CheckBox(&s_AspectDisableInfoMessages, RCLocalize("Disable for Info messages"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::INFOMESSAGES, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::INFOMESSAGES;

	Column.HSplitTop(LineSize, &Button, &Column);
	static int s_AspectDisableNotifyInSpec = 0;
	if(DoButton_CheckBox(&s_AspectDisableNotifyInSpec, RCLocalize("Disable for Notify in spec"), g_Config.m_RcCustomAspectDisable & RcAspectDisable::NOTIFYINSPEC, &Button))
		g_Config.m_RcCustomAspectDisable ^= RcAspectDisable::NOTIFYINSPEC;

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Above/Below Player"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNotifyWhenAbovePosPlayer, RCLocalize("Notify when above player"), &g_Config.m_RcNotifyWhenAbovePosPlayer, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNotifyWhenSamePosPlayer, RCLocalize("Notify when same pos as player"), &g_Config.m_RcNotifyWhenSamePosPlayer, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNotifyWhenBelowPosPlayer, RCLocalize("Notify when below player"), &g_Config.m_RcNotifyWhenBelowPosPlayer, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcNotifyWhenPosPlayerPosX, &g_Config.m_RcNotifyWhenPosPlayerPosX, &Button, RCLocalize("Text pos x"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcNotifyWhenPosPlayerPosY, &g_Config.m_RcNotifyWhenPosPlayerPosY, &Button, RCLocalize("Text pos y"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Spectator ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Spectator"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcSpectatorMoveEnable, RCLocalize("Enable spectator movement"), &g_Config.m_RcSpectatorMoveEnable, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcSpectatorMoveSpeed, &g_Config.m_RcSpectatorMoveSpeed, &Button, RCLocalize("Spectator Speed"), 1, 200, &CUi::ms_LinearScrollbarScale, 0);
	static CButtonContainer s_ReaderButtonSpecGoLeft, s_ClearButtonSpecGoLeft,
				s_ReaderButtonSpecGoRight, s_ClearButtonSpecGoRight,
				s_ReaderButtonSpecGoUp, s_ClearButtonSpecGoUp,
				s_ReaderButtonSpecGoDown, s_ClearButtonSpecGoDown;
	DoLine_KeyReader(Column, s_ReaderButtonSpecGoLeft, s_ClearButtonSpecGoLeft, TCLocalize("Spec go Left"), "+rc_spec_go_left");
	DoLine_KeyReader(Column, s_ReaderButtonSpecGoRight, s_ClearButtonSpecGoRight, TCLocalize("Spec go Right"), "+rc_spec_go_right");
	DoLine_KeyReader(Column, s_ReaderButtonSpecGoUp, s_ClearButtonSpecGoUp, TCLocalize("Spec go Up"), "+rc_spec_go_up");
	DoLine_KeyReader(Column, s_ReaderButtonSpecGoDown, s_ClearButtonSpecGoDown, TCLocalize("Spec go Down"), "+rc_spec_go_down");


	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;


	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;

	// ***** Chat ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Chat"), HeadlineFontSize, TEXTALIGN_MC);
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
			RCLocalize("Main"),
			RCLocalize("Filter"),
			RCLocalize("Translate")
		};

		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_CHAT_TABS, s_aPageTabs, s_CurChatCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(s_CurChatCustomTab == CHAT_TAB_MAIN)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatShowMouse, RCLocalize("Show mouse when open chat"), &g_Config.m_RcChatShowMouse, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCommandsFixLayout, RCLocalize("Fix layout commands"), &g_Config.m_RcCommandsFixLayout, &Column, LineSize);
		}
		if(s_CurChatCustomTab == CHAT_TAB_FILTER)
		{
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
		}
		if(s_CurChatCustomTab == CHAT_TAB_TRANSLATE)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTranslateAuto, TCLocalize("Use auto translate"), &g_Config.m_TcTranslateAuto, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcTranslateServerMessages, TCLocalize("Translate server messages"), &g_Config.m_RcTranslateServerMessages, &Column, LineSize);
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
				Ui()->DoLabel(&Label, RCLocalize("Latest languages"), FontSize, TEXTALIGN_ML);
				const int LangSelectedNew = Ui()->DoDropDown(&DropDownRect, LangSelectedOld,
					GameClient()->m_RClient.s_LangDropDownNames.data(), GameClient()->m_RClient.s_LangDropDownNames.size(), s_StateTranslateOthers);
				if(LangSelectedOld != LangSelectedNew)
				{
					str_copy(g_Config.m_TcTranslateTarget, GameClient()->m_RClient.m_LatestLangsList[LangSelectedNew].m_LangCode);
				}

				Column.HSplitTop(MarginSmall, nullptr, &Column);
				CUIRect Box;
				Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
				Box.VSplitLeft(Box.w / 4 * 3, &Label, &Button);
				Ui()->DoLabel(&Label, RCLocalize("Your language target (in ISO 639-1 code)"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget));
				s_LineInput.SetEmptyText(RCLocalize("ru"));
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
					Ui()->DoLabel(&Label, RCLocalize("Unknown language"), FontSize / 1.5f, TEXTALIGN_MR, Props);
				}
			}
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcTranslateSend, TCLocalize("Translate your messages"), &g_Config.m_RcTranslateSend, &Column, LineSize);

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
				Ui()->DoLabel(&Label, RCLocalize("Latest languages"), FontSize, TEXTALIGN_ML);
				const int LangSelectedNew = Ui()->DoDropDown(&DropDownRect, LangSelectedOldYour,
					GameClient()->m_RClient.s_LangDropDownNames.data(), GameClient()->m_RClient.s_LangDropDownNames.size(), s_StateTranslateYour);
				if(LangSelectedOldYour != LangSelectedNew)
				{
					str_copy(g_Config.m_RcTranslateSendTarget, GameClient()->m_RClient.m_LatestLangsList[LangSelectedNew].m_LangCode);
				}

				Column.HSplitTop(MarginSmall, nullptr, &Column);
				CUIRect Box;
				Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
				Box.VSplitLeft(Box.w / 4 * 3, &Label, &Button);
				Ui()->DoLabel(&Label, RCLocalize("Send language target (in ISO 639-1 code)"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_RcTranslateSendTarget, sizeof(g_Config.m_RcTranslateSendTarget));
				s_LineInput.SetEmptyText(RCLocalize("en"));
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
					Ui()->DoLabel(&Label, RCLocalize("Unknown language"), FontSize / 1.5f, TEXTALIGN_MR, Props);
				}
			}
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatShowTranslateFastSettings, TCLocalize("Fast settings in chat"), &g_Config.m_RcChatShowTranslateFastSettings, &Column, LineSize);
		}
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Nameplate ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Nameplates"), HeadlineFontSize, TEXTALIGN_MC);
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
			RCLocalize("Hook"),
			RCLocalize("Fire"),
			RCLocalize("Weapons")
		};
		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_NAMEPLATES_TABS, s_aPageTabs, s_CurNameplatesCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(s_CurNameplatesCustomTab == NAMEPLATE_TAB_HOOK)
		{
			static std::vector<CButtonContainer> s_vButtonContainersHookDetection = {{}, {}, {}, {}, {}};
			DoLine_RadioMenu(Column, TCLocalize("Hook Detection"),
				   s_vButtonContainersHookDetection,
				   {Localize("Off"), Localize("Others"), Localize("All"), Localize("Own"), "Dummy"},
				   {0, 1, 2, 3, 4},
				   g_Config.m_RcNamePlatesHook);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcNamePlatesHookSize, &g_Config.m_RcNamePlatesHookSize, &Button, RCLocalize("Hook size"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesHookShiftOnInvis, RCLocalize("Hook shift on invis"), &g_Config.m_RcNamePlatesHookShiftOnInvis, &Column, LineSize);
		}
		if(s_CurNameplatesCustomTab == NAMEPLATE_TAB_FIRE)
		{
			static std::vector<CButtonContainer> s_vButtonContainersFireDetection = {{}, {}, {}, {}, {}};
			DoLine_RadioMenu(Column, TCLocalize("Fire Detection"),
				   s_vButtonContainersFireDetection,
				   {Localize("Off"), Localize("Others(fake)"), Localize("All(fake)"), Localize("Own"), "Dummy"},
				   {0, 1, 2, 3, 4},
				   g_Config.m_RcNamePlatesFire);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcNamePlatesFireSize, &g_Config.m_RcNamePlatesFireSize, &Button, RCLocalize("Fire size"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesFireShiftOnInvis, RCLocalize("Fire shift on invis"), &g_Config.m_RcNamePlatesFireShiftOnInvis, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesFirePreInput, RCLocalize("Use preinput for fire"), &g_Config.m_RcNamePlatesFirePreInput, &Column, LineSize);
		}
		if(s_CurNameplatesCustomTab == NAMEPLATE_TAB_WEAPONS)
		{
			static std::vector<CButtonContainer> s_vButtonContainersWeaponsShow = {{}, {}, {}, {}};
			DoLine_RadioMenu(Column, TCLocalize("Weapons in nameplates"),
				   s_vButtonContainersWeaponsShow,
				   {Localize("Off"), Localize("Others"), Localize("All"), "Dummy"},
				   {0, 1, 2, 3},
				   g_Config.m_RcNamePlatesWeapons);
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_RcNamePlatesWeaponsSize, &g_Config.m_RcNamePlatesWeaponsSize, &Button, RCLocalize("Weapons size"), -50, 100, &CUi::ms_LinearScrollbarScale, 0);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsOwn, RCLocalize("Show own weapons"), &g_Config.m_RcNamePlatesWeaponsOwn, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsShotgun, RCLocalize("Show shotgun"), &g_Config.m_RcNamePlatesWeaponsShotgun, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsGrenade, RCLocalize("Show grenade"), &g_Config.m_RcNamePlatesWeaponsGrenade, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsNinja, RCLocalize("Show ninja"), &g_Config.m_RcNamePlatesWeaponsNinja, &Column, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNamePlatesWeaponsLaser, RCLocalize("Show laser"), &g_Config.m_RcNamePlatesWeaponsLaser, &Column, LineSize);
		}
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Anti AFK ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Anti AFK"), HeadlineFontSize, TEXTALIGN_MC);
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
			RCLocalize("NonActive"),
			RCLocalize("Spec")
		};

		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_AFK_TABS, s_aPageTabs, s_CurAfkCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(s_CurAfkCustomTab == AFK_TAB_NONACTIVE)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcPlayOnMoveNonInactive, RCLocalize("Play sound when moved and window non active"), &g_Config.m_RcPlayOnMoveNonInactive, &Column, LineSize);
			if(g_Config.m_RcPlayOnMoveNonInactive)
			{
				static std::vector<CButtonContainer> s_vButtonContainersNonActive = {{}, {}, {}};
				DoLine_RadioMenu(Column, TCLocalize("Choose sound non active"),
					   s_vButtonContainersNonActive,
					   {Localize("Wake up"), Localize("Grenade"), Localize("Tag")},
					   {0, 1, 2},
					   g_Config.m_RcSoundOnMoveNonInactive);
			}
			else
				Column.HSplitTop(LineSize + 2.0f, nullptr, &Column);
		}
		if(s_CurAfkCustomTab == AFK_TAB_SPEC)
		{
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
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcTextOnMoveInSpec, RCLocalize("Show text when moved in spec"), &g_Config.m_RcTextOnMoveInSpec, &Column, LineSize);
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcTextOnMoveInSpecPosX, &g_Config.m_RcTextOnMoveInSpecPosX, &Button, RCLocalize("Text pos x"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);
				Column.HSplitTop(LineSize, &Button, &Column);
				Ui()->DoScrollbarOption(&g_Config.m_RcTextOnMoveInSpecPosY, &g_Config.m_RcTextOnMoveInSpecPosY, &Button, RCLocalize("Text pos y"), 0, 100, &CUi::ms_LinearScrollbarScale, 0);
			}
			else
				Column.HSplitTop(LineSize * 5 + 2.0f, nullptr, &Column); // 2.0f for radio menu
		}
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Custom Clients ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Client Indicator"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(Margin, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("by +KZ"), Margin, TEXTALIGN_MC);
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

	// ***** Binds ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Binds"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
				s_ReaderButton45degrees, s_ClearButton45degrees,
				s_ReaderButtonSmallsens, s_ClearButtonSmallsens,
				s_ReaderButtonFindTeleport, s_ClearButtonFindTeleport,
				s_ReaderButtonFindFinish, s_ClearButtonFindFinish;
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
	DoLine_KeyReader(Column, s_ReaderButtonFindTeleport, s_ClearButtonFindTeleport, TCLocalize("Find Teleport"), "rc_goto_tele_cursor");
	DoLine_KeyReader(Column, s_ReaderButtonFindFinish, s_ClearButtonFindFinish, TCLocalize("Find Finish"), "rc_goto_finish_cursor");

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Streamer mode ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Streamer mode"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(FontSize, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Only windows"), FontSize, TEXTALIGN_ML);
	static std::vector<CButtonContainer> s_vButtonContainersScreenShare = {{}, {}, {}};
	DoLine_RadioMenu(Column, TCLocalize("Hide window from capture when RCON opened"),
		   s_vButtonContainersScreenShare,
		   {Localize("Off"), Localize("Monitor(win7-11)"), Localize("Exclude(win10-11)")},
		   {0, 1, 2},
		   g_Config.m_RcRconSteamerMode);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Edge info ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Edge info"), HeadlineFontSize, TEXTALIGN_MC);
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
	DoButton_ColorPickerAutoVMargin(&s_EdgeInfoSafeColor, Localize("Above Save color"), &g_Config.m_RcEdgeInfoColorSafe, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::RcEdgeInfoColorSafe)), &Column, LineSize, false);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Hud ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Hud"), HeadlineFontSize, TEXTALIGN_MC);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudPlayerCheckpoint, RCLocalize("Show checkpoint"), &g_Config.m_RcShowhudPlayerCheckpoint, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudSmallerHud, RCLocalize("Smaller hud (angle,checkpoint)"), &g_Config.m_RcShowhudSmallerHud, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcSizeOfHeart, &g_Config.m_RcSizeOfHeart, &Button, RCLocalize("Heart size"), 0, 200, &CUi::ms_LinearScrollbarScale, 0);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowhudVotingPercent, RCLocalize("Show percent in hud"), &g_Config.m_RcShowhudVotingPercent, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, RCLocalize("Helpful Functions"), HeadlineFontSize, TEXTALIGN_MC);
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
			RCLocalize("Main"),
			RCLocalize("Sort")
		};
		DoMenuSettingsBar(&Column, apTabNames, NUMBER_OF_HELP_TABS, s_aPageTabs, s_CurHelpCustomTab, LineSize);
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(s_CurHelpCustomTab == HELP_TAB_MAIN)
		{
			static std::vector<CButtonContainer> s_vButtonContainersAutoLock = {{}, {}, {}};
			DoLine_RadioMenu(Column, TCLocalize("Auto Lock Team"),
				   s_vButtonContainersAutoLock,
				   {Localize("Off"), Localize("Empty"), Localize("Any")},
				   {0, 1, 2},
				   g_Config.m_RcAutoLockTeam);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcAntiUnSpec, RCLocalize("Anti UnSpec in player"), &g_Config.m_RcAntiUnSpec, &Column, LineSize);
		}

		if(s_CurHelpCustomTab == HELP_TAB_SORT)
		{
			static std::vector<CButtonContainer> s_vButtonContainersSortScoreboard = {{}, {}, {}, {}, {}};
			DoLine_RadioMenu_WLabelSize(Column, TCLocalize("Sort Scoreboard"), Column.w / 4,
				   s_vButtonContainersSortScoreboard,
				   {"name-team-score", "id-team-score", "id-score", "id-team", "id"},
				   {0, 1, 2, 3, 4},
				   g_Config.m_RcScoreboardSortId);

			static std::vector<CButtonContainer> s_vButtonContainersSortSpectator = {{}, {}, {}};
			DoLine_RadioMenu_WLabelSize(Column, TCLocalize("Sort Spectator"), Column.w / 4,
				   s_vButtonContainersSortSpectator,
				   {"name-team", "id-team", "id"},
				   {0, 1, 2},
				   g_Config.m_RcSpectatorSortId);
		}
	}

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

	MainView.y = maximum(LeftView.y, RightView.y);
	CUIRect ResetBindsChat;
	MainView.HSplitTop(FontSize * 1.25f, &ResetBindsChat, &MainView);
	static CButtonContainer s_ResetBindsChat;
	if(GameClient()->m_Menus.DoButton_Menu(&s_ResetBindsChat, Localize("Reset RClient chatbinds"), 0, &ResetBindsChat, BUTTONFLAG_LEFT, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 0.0f, 0.0f, 0.75f)))
	{
		GameClient()->m_RClient.ResetRClientChatBinds();
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

	pUi->DoLabel(&Label, RCLocalize("Keep this settings?"), HeadlineFontSize, TEXTALIGN_MC);

	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "Reset in %.2f", (pPopupContext->m_Timeout - time_get()) / (float)time_freq());
	pUi->DoLabel(&Countdown, aBuf, HeadlineFontSize / 2.0f, TEXTALIGN_MC);

	Buttons.VSplitMid(&DenyButton, &ConfirmButton, Margin);
	if(pUi->DoButton_PopupMenu(&pPopupContext->m_ConfirmButton, RCLocalize("Confirm"), &ConfirmButton, FontSize, TEXTALIGN_MC, 0, false, true, Green))
		return CUi::POPUP_CLOSE_CURRENT;

	if(pUi->DoButton_PopupMenu(&pPopupContext->m_DenyButton, RCLocalize("Deny"), &DenyButton, FontSize, TEXTALIGN_MC, 0, false, true, Red))
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

	const float Radius = minimum(RightView.w, RightView.h) / 2.0f;
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
	Ui()->DoLabel(&Label, TCLocalize("Name:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
	s_NameInput.SetEmptyText(TCLocalize("Name"));
	Ui()->DoEditBox(&s_NameInput, &Button, EditBoxFontSize);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Command:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_BindInput;
	s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
	s_BindInput.SetEmptyText(TCLocalize("Command"));
	Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

	static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	if(DoButton_Menu(&s_OverrideButton, TCLocalize("Override Selected"), 0, &Button) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_RcSpecWheel.m_vSpecBinds.size()))
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
	if(DoButton_Menu(&s_AddButton, TCLocalize("Add Bind"), 0, &ButtonAdd))
	{
		CSpecWheel::CBind TempBind;
		if(str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		GameClient()->m_RcSpecWheel.AddBind(TempBind.m_aName, s_aBindCommand);
		s_SelectedBindIndex = static_cast<int>(GameClient()->m_RcSpecWheel.m_vSpecBinds.size()) - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, TCLocalize("Remove Bind"), 0, &ButtonRemove) && s_SelectedBindIndex >= 0)
	{
		GameClient()->m_RcSpecWheel.RemoveBind(s_SelectedBindIndex);
		s_SelectedBindIndex = -1;
	}

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("The command is ran in console not chat"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use left mouse to select"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use right mouse to swap with selected"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use middle mouse select without copy"), FontSize * 0.8f, TEXTALIGN_ML);

	LeftView.HSplitTop(LineSize, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("RClient \\/"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use %plnick% for replacing with nickname"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use %plid% for replacing with client id"), FontSize * 0.8f, TEXTALIGN_ML);
	CUIRect RightBox;
	LeftView.HSplitTop(LineSize * 0.8f, &RightBox, &LeftView);
	RightBox.VSplitLeft(LineSize, nullptr, &RightBox);
	SLabelProperties Props;
	Props.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	Ui()->DoLabel(&RightBox, TCLocalize("Use bracket yourself \"%plnick%\""), FontSize * 0.8f, TEXTALIGN_ML, Props);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Example: echo \"\\\"%plnick%\\\" %plid%\""), FontSize * 0.8f, TEXTALIGN_ML);

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
	DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, TCLocalize("Spec Wheel Key"), "+specwheel");

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcResetBindWheelMouse, TCLocalize("Reset position of mouse when opening specwheel"), &g_Config.m_TcResetBindWheelMouse, &Label, LineSize);
}