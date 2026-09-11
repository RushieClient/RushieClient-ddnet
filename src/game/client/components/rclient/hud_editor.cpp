#include "hud_editor.h"

#include "base/time.h"
#include "engine/shared/config.h"
#include "rclient_include.h"

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/gameclient.h>

namespace EditorSettingsOpened
{
	enum
	{
		CHAT = 1 << 0,
		HUDTIMER = 1 << 1,
		HUDDUMACTIONS = 1 << 2,
		HUDPLPOS = 1 << 3,
		HUDSPECCOUNT = 1 << 4,
	};
}

CHudEditor::CHudEditor()
{
	CHudEditor::OnReset();
}

void CHudEditor::OnConsoleInit()
{
	Console()->Register("rc_toggle_hud_editor", "", CFGFLAG_CLIENT, ConToggleHudEditor, this, "Toggle hud editor");
}

void CHudEditor::OnReset()
{
	m_LastMousePos = std::nullopt;
	m_DragElement = 0;
	m_TimeLatestPressedNeed = 0;
	m_MouseWasPressed = false;
	m_OpenedSettings = 0;
}

void CHudEditor::OnRender()
{
	if(!IsActive())
		return;

	Ui()->m_RcForceRealAspect = true;
	Ui()->StartCheck();

	Ui()->m_RcUpdateInputs = false;
	Ui()->Update();
	Ui()->m_RcUpdateInputs = true;

	Ui()->MapScreen();

	CUIRect *pScreen = GameClient()->m_RClient.GetRealScreen();
	const auto ScreenMid = (pScreen->TopLeft() + pScreen->Size() + pScreen->TopLeft()) / 2.0f;

	pScreen->Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_NONE, 0.0f);

	Graphics()->LinesBegin();
	IGraphics::CLineItem aLines[2] = {
		{ScreenMid.x, pScreen->TopLeft().y, ScreenMid.x, (pScreen->TopLeft() + pScreen->Size()).y},
		{pScreen->TopLeft().x, ScreenMid.y, (pScreen->TopLeft() + pScreen->Size()).x, ScreenMid.y}};
	Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
	Graphics()->LinesDraw(aLines, std::size(aLines));
	Graphics()->LinesEnd();

	Ui()->DoLabel(pScreen, "Hold 0.25s to move. Click for settings", 16.0f, TEXTALIGN_MC);

	vec2 BoxSize = vec2(60.0f, 14.0f);
	CUIRect ChatBox, HudTimerBox, DumActionsBox, PlPosBox, SpecCountBox;

	const float RealAspect = Graphics()->ScreenAspectReal();
	const float ChatAspect = (g_Config.m_RcCustomAspectDisable & RcAspectDisable::CHAT)
	    ? RealAspect : Graphics()->ScreenAspect();
	const float FontSize = g_Config.m_ClChatFontSize / 10.0f;
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	vec2 ConfDelta = Ui()->MouseDelta() / WindowSize * vec2(pScreen->w, pScreen->h) / 2.0f;
	const float SmallMargin = 2.0f;

	// ChatRender
	m_ChatPos.x = (5.0f + g_Config.m_RcChatPosX) * 2.0f * RealAspect / ChatAspect;
	m_ChatPos.y = (300.0f
	    - (20.0f * FontSize / 6.0f + (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f))
	    + g_Config.m_RcChatPosY
	    - FontSize * (8.0f / 6.0f))
	    * 2.0f;
	pScreen->VSplitLeft(m_ChatPos.x, nullptr, &ChatBox);
	ChatBox.VSplitLeft(BoxSize.x, &ChatBox, nullptr);
	ChatBox.HSplitTop(m_ChatPos.y, nullptr, &ChatBox);
	ChatBox.HSplitTop(BoxSize.y, &ChatBox, nullptr);
	ChatBox.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
	ChatBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_NONE, 0.0f);
	Ui()->DoLabel(&ChatBox, "Chat", 12.0f, TEXTALIGN_MC);
	if(m_OpenedSettings & EditorSettingsOpened::CHAT)
	{
		CUIRect ResetButton = {ChatBox.x, ChatBox.y + ChatBox.h + SmallMargin, ChatBox.w, 12.0f};
		if(GameClient()->m_Menus.DoButton_Menu(&m_ResetButtonChat, "Reset", 0, &ResetButton))
		{
			g_Config.m_RcChatPosX = 0;
			g_Config.m_RcChatPosY = 0;
		}
		CUIRect PosLabel = {ChatBox.x, ChatBox.y + (ChatBox.h + SmallMargin) * 2, ChatBox.w, 12.0f};
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "x: %.0f, y: %.0f", PosLabel.x, PosLabel.y);
		Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
		PosLabel.y = ChatBox.y + (ChatBox.h + SmallMargin) * 3;
		str_format(aBuf, sizeof(aBuf), "cx: %i, cy: %i", g_Config.m_RcChatPosX, g_Config.m_RcChatPosY);
		Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
	}

	// HudRender
	m_HudTimerPos.x = (300.0f * RealAspect/ 2.0f + g_Config.m_RcHudTimerPosX) * 2.0f;
	m_HudTimerPos.y = (2.0f + g_Config.m_RcHudTimerPosY) * 2.0f;
	pScreen->VSplitLeft(m_HudTimerPos.x - BoxSize.x / 2.0f, nullptr, &HudTimerBox);
	HudTimerBox.VSplitLeft(BoxSize.x, &HudTimerBox, nullptr);
	HudTimerBox.HSplitTop(m_HudTimerPos.y + BoxSize.y / 2.0f, nullptr, &HudTimerBox);
	HudTimerBox.HSplitTop(BoxSize.y, &HudTimerBox, nullptr);
	HudTimerBox.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
	HudTimerBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_NONE, 0.0f);
	Ui()->DoLabel(&HudTimerBox, "Hud Timer", 12.0f, TEXTALIGN_MC);
	if(m_OpenedSettings & EditorSettingsOpened::HUDTIMER)
	{
		CUIRect ResetButton = {HudTimerBox.x, HudTimerBox.y + HudTimerBox.h + SmallMargin, HudTimerBox.w, 12.0f};
		if(GameClient()->m_Menus.DoButton_Menu(&m_ResetButtonHudTimer, "Reset", 0, &ResetButton))
		{
			g_Config.m_RcHudTimerPosX = 0;
			g_Config.m_RcHudTimerPosY = 0;
		}
		CUIRect PosLabel = {HudTimerBox.x, HudTimerBox.y + (HudTimerBox.h + SmallMargin) * 2, HudTimerBox.w, 12.0f};
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "x: %.0f, y: %.0f", PosLabel.x, PosLabel.y);
		Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
		PosLabel.y = HudTimerBox.y + (HudTimerBox.h + SmallMargin) * 3;
		str_format(aBuf, sizeof(aBuf), "cx: %i, cy: %i", g_Config.m_RcHudTimerPosX, g_Config.m_RcHudTimerPosY);
		Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
	}

	// Dummy Actions
	{
		const float BoxHeight = 13.0f * 2 + 3.0f + (g_Config.m_RcShowhudAdvancedDummyActions ? 13.0f * 2 : 0.0f); // 13.0f - icon, 3.0f - spacing(once)
		const float BoxWidth = 16.0f;
		m_DumActionsPos.x = (300.0f * RealAspect - BoxWidth + g_Config.m_RcHudDummyActionsPosX) * 2;
		m_DumActionsPos.y = (285.0f - BoxHeight - 4 + g_Config.m_RcHudDummyActionsPosY) * 2;

		if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
		{
			m_DumActionsPos.y -= 4 * 2;
		}
		m_DumActionsPos.y -= GameClient()->m_Hud.GetMovementInformationBoxHeight() * 2;

		if(g_Config.m_ClShowhudScore)
		{
			m_DumActionsPos.y -= 56 * 2;
		}

		if(g_Config.m_ClShowhudDummyActions && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && Client()->DummyConnected())
		{
			m_DumActionsPos.y = m_DumActionsPos.y - (29.0f - (g_Config.m_RcShowhudAdvancedDummyActions ? 13.0f * 2 : 0.0f) - 4) * 2; // dummy actions height and padding
		}

		pScreen->VSplitLeft(m_DumActionsPos.x + (BoxWidth - BoxSize.x) / 2, nullptr, &DumActionsBox);
		DumActionsBox.VSplitLeft(BoxSize.x, &DumActionsBox, nullptr);
		DumActionsBox.HSplitTop(m_DumActionsPos.y + BoxHeight - BoxSize.y / 2, nullptr, &DumActionsBox);
		DumActionsBox.HSplitTop(BoxSize.y, &DumActionsBox, nullptr);
		DumActionsBox.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
		DumActionsBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_NONE, 0.0f);
		Ui()->DoLabel(&DumActionsBox, "DumActions", 12.0f, TEXTALIGN_MC);
		if(m_OpenedSettings & EditorSettingsOpened::HUDDUMACTIONS)
		{
			CUIRect ResetButton = {DumActionsBox.x, DumActionsBox.y + DumActionsBox.h + SmallMargin, DumActionsBox.w, 12.0f};
			if(GameClient()->m_Menus.DoButton_Menu(&m_ResetButtonDumActions, "Reset", 0, &ResetButton))
			{
				g_Config.m_RcHudDummyActionsPosX = 0;
				g_Config.m_RcHudDummyActionsPosY = 0;
			}
			CUIRect PosLabel = {DumActionsBox.x, DumActionsBox.y + (DumActionsBox.h + SmallMargin) * 2, DumActionsBox.w, 12.0f};
			char aBuf[32];
			str_format(aBuf, sizeof(aBuf), "x: %.0f, y: %.0f", PosLabel.x, PosLabel.y);
			Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
			PosLabel.y = DumActionsBox.y + (DumActionsBox.h + SmallMargin) * 3;
			str_format(aBuf, sizeof(aBuf), "cx: %i, cy: %i", g_Config.m_RcHudDummyActionsPosX, g_Config.m_RcHudDummyActionsPosY);
			Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
		}
	}

	// PlPos
	{
		float BoxHeight = GameClient()->m_Hud.GetMovementInformationBoxHeight();
		const float BoxWidth = 62.0f;
		m_PlPosPos.x = (300.0f * RealAspect - BoxWidth + g_Config.m_RcHudPlayerMovementPosX) * 2;
		m_PlPosPos.y = ( 285.0f - BoxHeight - 4.0f + g_Config.m_RcHudPlayerMovementPosY) * 2;
		if(g_Config.m_ClShowhudScore)
		{
			m_PlPosPos.y -= 56.0f * 2;
		}

		pScreen->VSplitLeft(m_PlPosPos.x + (BoxWidth * 2 - BoxSize.x) / 2, nullptr, &PlPosBox);
		PlPosBox.VSplitLeft(BoxSize.x, &PlPosBox, nullptr);
		PlPosBox.HSplitTop(m_PlPosPos.y + BoxHeight - BoxSize.y / 2, nullptr, &PlPosBox);
		PlPosBox.HSplitTop(BoxSize.y, &PlPosBox, nullptr);
		PlPosBox.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
		PlPosBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_NONE, 0.0f);
		Ui()->DoLabel(&PlPosBox, "Pl Pos", 12.0f, TEXTALIGN_MC);
		if(m_OpenedSettings & EditorSettingsOpened::HUDPLPOS)
		{
			CUIRect ResetButton = {PlPosBox.x, PlPosBox.y + PlPosBox.h + SmallMargin, PlPosBox.w, 12.0f};
			if(GameClient()->m_Menus.DoButton_Menu(&m_ResetButtonPlPos, "Reset", 0, &ResetButton))
			{
				g_Config.m_RcHudPlayerMovementPosX = 0;
				g_Config.m_RcHudPlayerMovementPosY = 0;
			}
			CUIRect PosLabel = {PlPosBox.x, PlPosBox.y + (PlPosBox.h + SmallMargin) * 2, PlPosBox.w, 12.0f};
			char aBuf[32];
			str_format(aBuf, sizeof(aBuf), "x: %.0f, y: %.0f", PosLabel.x, PosLabel.y);
			Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
			PosLabel.y = PlPosBox.y + (PlPosBox.h + SmallMargin) * 3;
			str_format(aBuf, sizeof(aBuf), "cx: %i, cy: %i", g_Config.m_RcHudPlayerMovementPosX, g_Config.m_RcHudPlayerMovementPosY);
			Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
		}
	}
	
	// SpecCount
	{
		const float MWidth = 300.0f * Graphics()->ScreenAspectReal();
		const float BoxHeight = 14.f;
		const float BoxWidth = 13.f;

		float StartX = MWidth - BoxWidth;
		float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;
		if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
		{
			StartY -= 4;
		}
		StartY -= GameClient()->m_Hud.GetMovementInformationBoxHeight();;

		if(g_Config.m_ClShowhudScore)
		{
			StartY -= 56;
		}

		if(g_Config.m_ClShowhudDummyActions && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && Client()->DummyConnected())
		{
			StartY = StartY - 29.0f - (g_Config.m_RcShowhudAdvancedDummyActions ? 13.0f * 2 : 0.0f) - 4; // dummy actions height and padding
		}
	
		StartX = (StartX + g_Config.m_RcHudSpectatorCountPosX) * 2.0f;
		StartY = (StartY + g_Config.m_RcHudSpectatorCountPosY) * 2.0f;
		
		SpecCountBox = {StartX - (BoxSize.x) / 2, StartY + BoxHeight / 2.0f, BoxSize.x, BoxSize.y};
		SpecCountBox.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
		SpecCountBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_NONE, 0.0f);
		Ui()->DoLabel(&SpecCountBox, "SpecCount", 12.0f, TEXTALIGN_MC);
		if(m_OpenedSettings & EditorSettingsOpened::HUDSPECCOUNT)
		{
			CUIRect ResetButton = {SpecCountBox.x, SpecCountBox.y + SpecCountBox.h + SmallMargin, SpecCountBox.w, 12.0f};
			if(GameClient()->m_Menus.DoButton_Menu(&m_ResetButtonPlPos, "Reset", 0, &ResetButton))
			{
				g_Config.m_RcHudSpectatorCountPosX = 0;
				g_Config.m_RcHudSpectatorCountPosY = 0;
			}
			CUIRect PosLabel = {SpecCountBox.x, SpecCountBox.y + (SpecCountBox.h + SmallMargin) * 2, SpecCountBox.w, 12.0f};
			char aBuf[32];
			str_format(aBuf, sizeof(aBuf), "x: %.0f, y: %.0f", PosLabel.x, PosLabel.y);
			Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
			PosLabel.y = SpecCountBox.y + (SpecCountBox.h + SmallMargin) * 3;
			str_format(aBuf, sizeof(aBuf), "cx: %i, cy: %i", g_Config.m_RcHudSpectatorCountPosX, g_Config.m_RcHudSpectatorCountPosY);
			Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
		}
	}


	// Drag
	const bool Pressed = Ui()->MouseButton(0);
	if(Pressed && !m_MouseWasPressed)
	{
		if(HudTimerBox.Inside(Ui()->MousePos()))
		{
			m_DragElement = 2;
			m_DragPos = vec2(g_Config.m_RcHudTimerPosX, g_Config.m_RcHudTimerPosY);
		}
		else if(ChatBox.Inside(Ui()->MousePos()))
		{
			m_DragElement = 1;
			m_DragPos = vec2(g_Config.m_RcChatPosX, g_Config.m_RcChatPosY);
		}
		else if(DumActionsBox.Inside(Ui()->MousePos()))
		{
			m_DragElement = 3;
			m_DragPos = vec2(g_Config.m_RcHudDummyActionsPosX, g_Config.m_RcHudDummyActionsPosY);
		}
		else if(PlPosBox.Inside(Ui()->MousePos()))
		{
			m_DragElement = 4;
			m_DragPos = vec2(g_Config.m_RcHudPlayerMovementPosX, g_Config.m_RcHudPlayerMovementPosY);
		}
		else if(SpecCountBox.Inside(Ui()->MousePos()))
		{
			m_DragElement = 5;
			m_DragPos = vec2(g_Config.m_RcHudSpectatorCountPosX, g_Config.m_RcHudSpectatorCountPosY);
		}
	}
	if(m_DragElement != 0 && Pressed && !m_MouseWasPressed)
		m_TimeLatestPressedNeed = time_get() + time_freq() * 0.25f;

	if(m_DragElement != 0 && !Pressed) {
		if(m_TimeLatestPressedNeed > time_get())
		{
			switch(m_DragElement)
			{
			case 1: m_OpenedSettings ^= EditorSettingsOpened::CHAT; break;
			case 2: m_OpenedSettings ^= EditorSettingsOpened::HUDTIMER; break;
			case 3: m_OpenedSettings ^= EditorSettingsOpened::HUDDUMACTIONS; break;
			case 4: m_OpenedSettings ^= EditorSettingsOpened::HUDPLPOS; break;
			case 5: m_OpenedSettings ^= EditorSettingsOpened::HUDSPECCOUNT; break;
			default:;
			}
		}
		m_DragElement = 0;
		m_TimeLatestPressedNeed = 0;
	}
	else if(m_TimeLatestPressedNeed > time_get())
	{
		if(!(HudTimerBox.Inside(Ui()->MousePos()) ||
			ChatBox.Inside(Ui()->MousePos()) ||
			PlPosBox.Inside(Ui()->MousePos()) ||
			DumActionsBox.Inside(Ui()->MousePos()) ||
			SpecCountBox.Inside(Ui()->MousePos())
		))
		{
			m_DragElement = 0;
			m_TimeLatestPressedNeed = 0;
		}
	}
	else if(m_DragElement == 2)
	{
		m_DragPos += ConfDelta;
		g_Config.m_RcHudTimerPosX = round_to_int(m_DragPos.x);
		g_Config.m_RcHudTimerPosY = round_to_int(m_DragPos.y);
	}
	else if(m_DragElement == 1)
	{
		m_DragPos += vec2(ConfDelta.x * RealAspect / ChatAspect, ConfDelta.y);
		g_Config.m_RcChatPosX = round_to_int(m_DragPos.x);
		g_Config.m_RcChatPosY = round_to_int(m_DragPos.y);
	}
	else if(m_DragElement == 3)
	{
		m_DragPos += ConfDelta;
		g_Config.m_RcHudDummyActionsPosX = round_to_int(m_DragPos.x);
		g_Config.m_RcHudDummyActionsPosY = round_to_int(m_DragPos.y);
	}
	else if(m_DragElement == 4)
	{
		m_DragPos += ConfDelta;
		g_Config.m_RcHudPlayerMovementPosX = round_to_int(m_DragPos.x);
		g_Config.m_RcHudPlayerMovementPosY = round_to_int(m_DragPos.y);
	}
	else if(m_DragElement == 5)
	{
		m_DragPos += ConfDelta;
		g_Config.m_RcHudSpectatorCountPosX = round_to_int(m_DragPos.x);
		g_Config.m_RcHudSpectatorCountPosY = round_to_int(m_DragPos.y);
	}

	m_MouseWasPressed = Pressed;

	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
	Ui()->FinishCheck();
	Ui()->m_RcForceRealAspect = false;
}

bool CHudEditor::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
		SetActive(false);

	return true;
}

bool CHudEditor::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive())
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

void CHudEditor::ConToggleHudEditor(IConsole::IResult *pResult, void *pUserData)
{
	CHudEditor *pSelf = (CHudEditor *)pUserData;
	pSelf->SetActive(!pSelf->IsActive());
}

void CHudEditor::LockMouse()
{
	const vec2 OldMousePos = Ui()->MousePos();

	if(m_LastMousePos == std::nullopt)
	{
		SetUiMousePos(GameClient()->m_RClient.GetRealScreen()->Center());
	}
	else
	{
		SetUiMousePos(m_LastMousePos.value());
	}
	m_LastMousePos = OldMousePos;
}

void CHudEditor::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = GameClient()->m_RClient.GetRealScreen();

	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

void CHudEditor::SetActive(bool Active)
{
	if(m_Active == Active)
		return;

	m_Active = Active;
	if(m_Active)
	{
		LockMouse();
	}
	else
	{
		OnReset();
	}
}