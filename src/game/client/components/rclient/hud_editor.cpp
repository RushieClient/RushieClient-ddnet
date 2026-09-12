#include "hud_editor.h"

#include "rclient_include.h"

#include <base/time.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

namespace
{
	constexpr float BOX_WIDTH = 60.0f;
	constexpr float BOX_HEIGHT = 14.0f;
	constexpr float SMALL_MARGIN = 2.0f;
}

CHudEditor::CHudEditor()
{
	m_aElements[ELEM_CHAT] = {"Chat", &g_Config.m_RcChatPosX, &g_Config.m_RcChatPosY};
	m_aElements[ELEM_HUDTIMER] = {"Hud Timer", &g_Config.m_RcHudTimerPosX, &g_Config.m_RcHudTimerPosY};
	m_aElements[ELEM_DUMACTIONS] = {"DumActions", &g_Config.m_RcHudDummyActionsPosX, &g_Config.m_RcHudDummyActionsPosY};
	m_aElements[ELEM_PLPOS] = {"Pl Pos", &g_Config.m_RcHudPlayerMovementPosX, &g_Config.m_RcHudPlayerMovementPosY};
	m_aElements[ELEM_SPECCOUNT] = {"SpecCount", &g_Config.m_RcHudSpectatorCountPosX, &g_Config.m_RcHudSpectatorCountPosY};
	m_aElements[ELEM_PLAYERSTATE] = {"PlayerState", &g_Config.m_RcHudPlayerStatePosX, &g_Config.m_RcHudPlayerStatePosY};
	m_aElements[ELEM_FROZENHUD] = {"FrozenHud", &g_Config.m_RcHudFrozenHudPosX, &g_Config.m_RcHudFrozenHudPosY};
	m_aElements[ELEM_FROZENTEXT] = {"FrozenText", &g_Config.m_RcHudFrozenTextPosX, &g_Config.m_RcHudFrozenTextPosY};
	m_aElements[ELEM_FPSTEXT] = {"FpsText", &g_Config.m_RcHudFpsTextPosX, &g_Config.m_RcHudFpsTextPosY};
	m_aElements[ELEM_LASTTEXT] = {"LastText", &g_Config.m_RcHudLastTextPosX, &g_Config.m_RcHudLastTextPosY};
	CHudEditor::OnReset();
}

void CHudEditor::OnConsoleInit()
{
	Console()->Register("rc_toggle_hud_editor", "", CFGFLAG_CLIENT, ConToggleHudEditor, this, "Toggle hud editor");
}

void CHudEditor::OnReset()
{
	m_LastMousePos = std::nullopt;
	m_DragElement = ELEM_NONE;
	m_TimeLatestPressedNeed = 0;
	m_MouseWasPressed = false;
	m_OpenedSettings = 0;
}

inline int CHudEditor::GetDigitsIndex(int Value, int Max)
{
	if(Value < 0)
	{
		Value *= -1;
	}
	int DigitsIndex = std::log10((Value ? Value : 1));
	if(DigitsIndex > Max)
	{
		DigitsIndex = Max;
	}
	if(DigitsIndex < 0)
	{
		DigitsIndex = 0;
	}
	return DigitsIndex;
}

inline float CHudEditor::GetMovementInformationBoxHeight()
{
	// if(GameClient()->m_Snap.m_SpecInfo.m_Active && (GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW || GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId].m_SpecCharPresent))
	// 	return g_Config.m_ClShowhudPlayerPosition ? 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT + 2.0f : 0.0f;
	// float BoxHeight = 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT * (g_Config.m_ClShowhudPlayerPosition + g_Config.m_ClShowhudPlayerSpeed) + 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT * g_Config.m_ClShowhudPlayerAngle;
	// if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	// {
	// 	BoxHeight += 2.0f;
	// }
	// return BoxHeight;
	float BoxHeight = 0.0f;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && (GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW || GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId].m_SpecCharPresent))
	{
		if(!GameClient()->m_RClient.m_vPlayersInTracker.empty())
			BoxHeight += GameClient()->m_RClient.m_vPlayersInTracker.size() * MOVEMENT_INFORMATION_LINE_HEIGHT * 3.0f;
		if(g_Config.m_ClShowhudPlayerPosition)
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT * 3.0f;
		if(g_Config.m_ClShowhudPlayerPosition && g_Config.m_TcShowhudDummyPosition && Client()->DummyConnected())
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT * 2.0f;
	}
	else
	{
		if(!GameClient()->m_RClient.m_vPlayersInTracker.empty())
			BoxHeight += GameClient()->m_RClient.m_vPlayersInTracker.size() * MOVEMENT_INFORMATION_LINE_HEIGHT * 3.0f;
		if(g_Config.m_ClShowhudPlayerPosition)
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT * 3.0f;
		if(g_Config.m_ClShowhudPlayerPosition && g_Config.m_TcShowhudDummyPosition && Client()->DummyConnected())
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT * 2.0f;
		if(g_Config.m_ClShowhudPlayerSpeed)
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT * 3.0f;
		if(g_Config.m_ClShowhudPlayerSpeed && g_Config.m_TcShowhudDummySpeed && Client()->DummyConnected())
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT * 2.0f;
		if(g_Config.m_ClShowhudPlayerAngle)
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT * (g_Config.m_RcShowhudSmallerHud ? 1.0f : 2.0f);
		if(g_Config.m_ClShowhudPlayerAngle && g_Config.m_TcShowhudDummyAngle && Client()->DummyConnected())
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT;
		if(g_Config.m_RcShowhudPlayerCheckpoint)
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT * (g_Config.m_RcShowhudSmallerHud ? 1.0f : 2.0f);
	}
	if(BoxHeight > 0.0f)
		BoxHeight += 2.0f;
	return BoxHeight;
}

void CHudEditor::ComputeElementBox(int Idx)
{
	CUIRect *pScreen = GameClient()->m_RClient.GetRealScreen();
	const float RealAspect = Graphics()->ScreenAspectReal();
	float MWidth = 300.0f * RealAspect;
	float MHeight = 300.0f;
	vec2 Pos;

	switch(Idx)
	{
	case ELEM_CHAT:
	{
		const float FontSize = g_Config.m_ClChatFontSize / 10.0f;
		Pos.x = (5.0f + g_Config.m_RcChatPosX) * 2.0f;
		Pos.y = (MHeight - (20.0f * FontSize / 6.0f + (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f)) + g_Config.m_RcChatPosY - FontSize * (8.0f / 6.0f)) * 2.0f;
		break;
	}
	case ELEM_HUDTIMER:
		Pos.x = (MWidth / 2.0f + g_Config.m_RcHudTimerPosX) * 2.0f - BOX_WIDTH / 2.0f;
		Pos.y = (2.0f + g_Config.m_RcHudTimerPosY) * 2.0f + BOX_HEIGHT / 2.0f;
		break;
	case ELEM_DUMACTIONS:
	{
		const float BoxHeight = 13.0f * 2 + 3.0f + (g_Config.m_RcShowhudAdvancedDummyActions ? 13.0f * 2 : 0.0f); // 13.0f - icon, 3.0f - spacing(once)
		const float BoxWidth = 16.0f;
		Pos.x = (MWidth - BoxWidth + g_Config.m_RcHudDummyActionsPosX) * 2 + (BoxWidth - BOX_WIDTH) / 2;
		Pos.y = (285.0f - BoxHeight - 4 + g_Config.m_RcHudDummyActionsPosY) * 2;

		if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
		{
			Pos.y -= 4 * 2;
		}
		Pos.y -= GetMovementInformationBoxHeight() * 2;

		if(g_Config.m_ClShowhudScore)
		{
			Pos.y -= 56 * 2;
		}

		if(g_Config.m_ClShowhudDummyActions && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && Client()->DummyConnected())
		{
			Pos.y = Pos.y - (29.0f - (g_Config.m_RcShowhudAdvancedDummyActions ? 13.0f * 2 : 0.0f) - 4) * 2; // dummy actions height and padding
		}
		Pos.y += BoxHeight - BOX_HEIGHT / 2;
		break;
	}
	case ELEM_PLPOS:
	{
		const float BoxHeight = GetMovementInformationBoxHeight();
		const float BoxWidth = 62.0f;
		Pos.x = (MWidth - BoxWidth + g_Config.m_RcHudPlayerMovementPosX) * 2 + (BoxWidth * 2 - BOX_WIDTH) / 2;
		Pos.y = (285.0f - BoxHeight - 4.0f + g_Config.m_RcHudPlayerMovementPosY) * 2;
		if(g_Config.m_ClShowhudScore)
		{
			Pos.y -= 56.0f * 2;
		}
		Pos.y += BoxHeight - BOX_HEIGHT / 2;
		break;
	}
	case ELEM_SPECCOUNT:
	{
		const float BoxHeight = 14.f;
		const float BoxWidth = 13.f;

		float StartX = MWidth - BoxWidth;
		float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;
		if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
		{
			StartY -= 4;
		}
		StartY -= GetMovementInformationBoxHeight();

		if(g_Config.m_ClShowhudScore)
		{
			StartY -= 56;
		}

		if(g_Config.m_ClShowhudDummyActions && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && Client()->DummyConnected())
		{
			StartY = StartY - 29.0f - (g_Config.m_RcShowhudAdvancedDummyActions ? 13.0f * 2 : 0.0f) - 4; // dummy actions height and padding
		}

		Pos.x = (StartX + g_Config.m_RcHudSpectatorCountPosX) * 2.0f - BOX_WIDTH / 2;
		Pos.y = (StartY + g_Config.m_RcHudSpectatorCountPosY) * 2.0f + BoxHeight / 2.0f;
		break;
	}
	case ELEM_PLAYERSTATE:
	{
		const bool HasHealth = GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo;
		const bool HasAmmo = GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo;
		Pos.x = (5.0f + g_Config.m_RcHudPlayerStatePosX) * 2.0f;
		Pos.y = (5.0f + 12.0f + (HasHealth ? 24.0f : 0.0f) + (HasAmmo ? 12.0f : 0.0f) + g_Config.m_RcHudPlayerStatePosY) * 2.0f;
		break;
	}
	case ELEM_FROZENHUD:
	{
		float StartPos = (MWidth / 2.0f + 38.0f * (MWidth / MHeight) / 1.78f + g_Config.m_RcHudFrozenHudPosX) * 2.0f;
		Pos.x = StartPos;
		Pos.y = g_Config.m_RcHudFrozenHudPosY * 2.0f;
		break;
	}
	case ELEM_FROZENTEXT:
	{
		Pos.x = (MWidth / 2.0f + g_Config.m_RcHudFrozenTextPosX) * 2.0f - BOX_WIDTH / 2.0f;
		Pos.y = (12.0f + g_Config.m_RcHudFrozenTextPosY) * 2.0f + BOX_HEIGHT / 2.0f - SMALL_MARGIN;
		break;
	}
	case ELEM_FPSTEXT:
	{
		const int FramesPerSecond = round_to_int(1.0f / Client()->FrameTimeAverage());
		static float s_TextWidth0 = TextRender()->TextWidth(12.f, "0", -1, -1.0f);
		static float s_TextWidth00 = TextRender()->TextWidth(12.f, "00", -1, -1.0f);
		static float s_TextWidth000 = TextRender()->TextWidth(12.f, "000", -1, -1.0f);
		static float s_TextWidth0000 = TextRender()->TextWidth(12.f, "0000", -1, -1.0f);
		static float s_TextWidth00000 = TextRender()->TextWidth(12.f, "00000", -1, -1.0f);
		static const float s_aTextWidth[5] = {s_TextWidth0, s_TextWidth00, s_TextWidth000, s_TextWidth0000, s_TextWidth00000};
		int DigitIndex = GetDigitsIndex(FramesPerSecond, 4);

		Pos.x = (MWidth - 10 - s_aTextWidth[DigitIndex] + g_Config.m_RcHudFpsTextPosX) * 2.0f;
		Pos.y = (5 + g_Config.m_RcHudFpsTextPosY) * 2.0f + BOX_HEIGHT;
		break;
	}
	case ELEM_LASTTEXT:
	{
		float FontSize = g_Config.m_TcNotifyWhenLastSize;
		float XPos = std::clamp((g_Config.m_TcNotifyWhenLastX / 100.0f) * MWidth, 1.0f, MWidth - FontSize) + g_Config.m_RcHudLastTextPosX;
		float YPos = std::clamp((g_Config.m_TcNotifyWhenLastY / 100.0f) * MHeight, 1.0f, MHeight - FontSize) + g_Config.m_RcHudLastTextPosY;
		Pos.x = XPos * 2.0f;
		Pos.y = YPos * 2.0f;
		break;
	}
	default:
		return;
	}

	m_aBoxes[Idx] = {pScreen->x + Pos.x, pScreen->y + Pos.y, BOX_WIDTH, BOX_HEIGHT};
}

void CHudEditor::RenderElementSettings(int Idx)
{
	const SElement &Element = m_aElements[Idx];
	const CUIRect &Box = m_aBoxes[Idx];

	CUIRect ResetButton = {Box.x, Box.y + Box.h + SMALL_MARGIN, Box.w, 12.0f};
	if(GameClient()->m_Menus.DoButton_Menu(&m_aResetButtons[Idx], "Reset", 0, &ResetButton))
	{
		*Element.m_pConfigX = 0;
		*Element.m_pConfigY = 0;
	}

	CUIRect PosLabel = {Box.x, Box.y + (Box.h + SMALL_MARGIN) * 2, Box.w, 12.0f};
	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "x: %.0f, y: %.0f", PosLabel.x, PosLabel.y);
	Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
	PosLabel.y = Box.y + (Box.h + SMALL_MARGIN) * 3;
	str_format(aBuf, sizeof(aBuf), "cx: %i, cy: %i", *Element.m_pConfigX, *Element.m_pConfigY);
	Ui()->DoLabel(&PosLabel, aBuf, 12.0f, TEXTALIGN_MC);
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

	for(int i = 0; i < ELEM_COUNT; i++)
	{
		ComputeElementBox(i);

		CUIRect &Box = m_aBoxes[i];
		Box.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
		Box.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_NONE, 0.0f);
		Ui()->DoLabel(&Box, m_aElements[i].m_pName, 12.0f, TEXTALIGN_MC);

		if(m_OpenedSettings & (1 << i))
			RenderElementSettings(i);
	}

	// Drag
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const vec2 ConfDelta = Ui()->MouseDelta() / WindowSize * vec2(pScreen->w, pScreen->h) / 2.0f;

	const bool Pressed = Ui()->MouseButton(0);
	if(Pressed && !m_MouseWasPressed)
	{
		for(int i = 0; i < ELEM_COUNT; i++)
		{
			if(m_aBoxes[i].Inside(Ui()->MousePos()))
			{
				m_DragElement = i;
				m_DragPos = vec2(*m_aElements[i].m_pConfigX, *m_aElements[i].m_pConfigY);
				m_TimeLatestPressedNeed = time_get() + time_freq() * 0.25f;
				break;
			}
		}
	}

	if(m_DragElement != ELEM_NONE && !Pressed)
	{
		if(m_TimeLatestPressedNeed > time_get())
			m_OpenedSettings ^= 1 << m_DragElement;
		m_DragElement = ELEM_NONE;
		m_TimeLatestPressedNeed = 0;
	}
	else if(m_TimeLatestPressedNeed > time_get())
	{
		bool InsideAny = false;
		for(const CUIRect &Box : m_aBoxes)
			InsideAny = InsideAny || Box.Inside(Ui()->MousePos());
		if(!InsideAny)
		{
			m_DragElement = ELEM_NONE;
			m_TimeLatestPressedNeed = 0;
		}
	}
	else if(m_DragElement != ELEM_NONE)
	{
		const SElement &Element = m_aElements[m_DragElement];
		m_DragPos += ConfDelta;
		*Element.m_pConfigX = round_to_int(m_DragPos.x);
		*Element.m_pConfigY = round_to_int(m_DragPos.y);
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
