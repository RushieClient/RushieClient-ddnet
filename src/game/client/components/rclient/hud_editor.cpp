#include "hud_editor.h"

#include "engine/shared/config.h"
#include <engine/graphics.h>
#include <engine/console.h>

#include <game/client/gameclient.h>
#include "rclient_include.h"

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
	pScreen->Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_NONE, 0.0f);

	Ui()->DoLabel(pScreen, "Hold 1s to move. Click for settings", 16.0f, TEXTALIGN_MC);

	vec2 BoxSize = vec2(60.0f, 14.0f);
	CUIRect ChatBox, HudTimerBox;

	const float RealAspect = Graphics()->ScreenAspectReal();
	const float ChatAspect = (g_Config.m_RcCustomAspectDisable & RcAspectDisable::CHAT)
	    ? RealAspect : Graphics()->ScreenAspect();
	const float FontSize = g_Config.m_ClChatFontSize / 10.0f;
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	vec2 ConfDelta = Ui()->MouseDelta() / WindowSize * vec2(pScreen->w, pScreen->h) / 2.0f;

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
	}
	m_MouseWasPressed = Pressed;
	if(m_DragElement != 0 && !Pressed) {
		m_DragElement = 0;
	} else if(m_DragElement == 2)
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