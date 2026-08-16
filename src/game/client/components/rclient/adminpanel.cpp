#include "adminpanel.h"

#include "engine/font_icons.h"
#include "game/localization.h"
#include "rclient_include.h"

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/components/controls.h>
#include <game/client/components/emoticon.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
struct SAdminPanelProperties
{
	static constexpr float ms_HeadlineFontSize = 8.0f;
	static constexpr float ms_FontSize = 8.0f;
	static constexpr float ms_IconFontSize = 11.0f;
	static constexpr float ms_Padding = 6.0f;
	static constexpr float ms_Rounding = 3.0f;

	static constexpr float ms_ItemSpacing = 2.0f;

	static constexpr float ms_RconActionHeight = 25.0f;
	static constexpr float ms_RconActionWidth = 75.0f;
	static constexpr float ms_ReadyButtonsWidth = 80.0f;
	static constexpr float ms_RconTimersWidth = 50.0f;
	static constexpr float ms_ButtonHeight = 12.0f;

	static constexpr float ms_PlayerBtnWidth = 60.0f;

	static ColorRGBA WindowColor() { return ColorRGBA(0.451f, 0.451f, 0.451f, 0.9f); };
	static ColorRGBA WindowColorDark() { return ColorRGBA(0.2f, 0.2f, 0.2f, 0.9f); };
	static ColorRGBA GeneralButtonColor() { return ColorRGBA(0.541f, 0.561f, 0.48f, 0.8f); };
	static ColorRGBA GeneralActiveButtonColor() { return ColorRGBA(0.53f, 0.78f, 0.53f, 0.8f); };

	static ColorRGBA ActionGeneralButtonColor() { return ColorRGBA(0.541f, 0.561f, 0.48f, 0.8f); };
	static ColorRGBA ActionActiveButtonColor() { return ColorRGBA(0.53f, 0.78f, 0.53f, 0.8f); };

	static ColorRGBA ActionBanAltButtonColor() { return ColorRGBA(0.7f, 0.1f, 0.1f, 0.8f); };
	static ColorRGBA ActionBanButtonColor() { return ColorRGBA(1.0f, 0.24f, 0.24f, 0.8f); };
	static ColorRGBA ActionKillAltButtonColor() { return ColorRGBA(0.5f, 0.45f, 0.0f, 0.8f); };
	static ColorRGBA ActionKillButtonColor() { return ColorRGBA(1.0f, 0.902f, 0.0f, 0.8f); };
	static ColorRGBA ActionKickAltButtonColor() { return ColorRGBA(0.5f, 0.27f, 0.0f, 0.8f); };
	static ColorRGBA ActionKickButtonColor() { return ColorRGBA(1.0f, 0.549f, 0.0f, 0.8f); };
	static ColorRGBA ActionMuteAltButtonColor() { return ColorRGBA(0.25f, 0.0f, 0.25f, 0.8f); };
	static ColorRGBA ActionMuteButtonColor() { return ColorRGBA(0.502f, 0.0f, 0.502f, 0.8f); };
};

void CAdminPanel::DoIconLabeledButton(CUIRect *pRect, const char *pTitle, const char *pIcon, float TextSize, float Height, ColorRGBA IconColor) const
{
	CUIRect Label;
	pRect->VSplitLeft(Height, &Label, pRect);
	DoIconButton(&Label, pIcon, TextSize, IconColor);
	Ui()->DoLabel(pRect, pTitle, TextSize, TEXTALIGN_MC);
}

void CAdminPanel::DoIconLabeledButtonDown(CUIRect *pRect, const char *pTitle, const char *pIcon, float IconSize, float TextSize, float Height, float Dif, ColorRGBA IconColor) const
{
	CUIRect Icon, Label;
	pRect->HSplitTop(Height, &Icon, &Label);
	DoIconButton(&Icon, pIcon, IconSize, IconColor);
	Label.HSplitTop(Dif, nullptr, &Label);
	Label.HSplitTop(Label.h / 2, &Label, nullptr);
	Ui()->DoLabel(&Label, pTitle, TextSize, TEXTALIGN_MC);
}

void CAdminPanel::DoLabelLabeledButtonDown(CUIRect *pRect, const char *pTitleDown, const char *pTitle, float TextSize, float TextSizeDown, float Height, float Dif) const
{
	CUIRect Label, LabelDown;
	pRect->HSplitTop(Height, &Label, &LabelDown);
	Ui()->DoLabel(&Label, pTitle, TextSize, TEXTALIGN_MC);
	LabelDown.HSplitTop(Dif, nullptr, &LabelDown);
	LabelDown.HSplitTop(LabelDown.h / 2, &LabelDown, nullptr);
	Ui()->DoLabel(&LabelDown, pTitleDown, TextSizeDown, TEXTALIGN_MC);
}

void CAdminPanel::DoIconButton(CUIRect *pRect, const char *pIcon, float TextSize, ColorRGBA IconColor) const
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(IconColor);
	Ui()->DoLabel(pRect, pIcon, TextSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

CAdminPanel::CAdminPanel()
{
	CAdminPanel::OnReset();
	m_PlayerPopup.m_LineInput.SetBuffer(m_PlayerPopup.m_InputReason, sizeof(m_PlayerPopup.m_InputReason));
}

void CAdminPanel::OnConsoleInit()
{
	Console()->Register("rc_toggle_adminpanel", "", CFGFLAG_CLIENT, ConToggleAdminPanel, this, "Toggle admin panel");
}

void CAdminPanel::ConToggleAdminPanel(IConsole::IResult *pResult, void *pUserData)
{
	CAdminPanel *pSelf = (CAdminPanel *)pUserData;
	if(pSelf->Client()->RconAuthed())
		pSelf->SetActive(!pSelf->IsActive());
	else
	{
		pSelf->GameClient()->Echo("THIS FUNCTION IS NOT FOR YOU, MORTAL. ☠");
		pSelf->GameClient()->Echo("You've gone too far. We know who you are.");
		pSelf->GameClient()->Echo("I see you.");
	}
}

void CAdminPanel::LockMouse()
{
	if(m_LastMousePos == std::nullopt)
	{
		vec2 MouseCenter = Ui()->Screen()->Center();
		MouseCenter = {MouseCenter.x / 2.0f, MouseCenter.y / 2.0f};
		SetUiMousePos(MouseCenter);
	}
	else
	{
		SetUiMousePos(m_LastMousePos.value());
	}
	m_LastMousePos = Ui()->MousePos();
}

void CAdminPanel::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();

	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

bool CAdminPanel::DoEditBoxInUiSpace(CLineInput *pLineInput, const CUIRect *pLocalRect, float FontSize)
{
	const CUIRect *pScreen = Ui()->Screen();
	const float ScaleX = pScreen->w / m_PopupWidth;
	const float ScaleY = pScreen->h / m_PopupHeight;
	CUIRect Rect = *pLocalRect;
	Rect.x *= ScaleX; Rect.y *= ScaleY; Rect.w *= ScaleX; Rect.h *= ScaleY;
	Graphics()->MapScreen(0.0f, 0.0f, pScreen->w, pScreen->h);
	const bool Result = Ui()->DoEditBox(pLineInput, &Rect, FontSize * ScaleY);
	Graphics()->MapScreen(0.0f, 0.0f, m_PopupWidth, m_PopupHeight);
	return Result;
}

void CAdminPanel::SetActive(bool Active)
{
	if(m_Active == Active)
		return;

	m_Active = Active;
	if(m_Active)
	{
		const vec2 OldMousePos = Ui()->MousePos();

		if(m_LastMousePos == std::nullopt)
		{
			vec2 MouseCenter = Ui()->Screen()->Center();
			MouseCenter = {MouseCenter.x / 2.0f, MouseCenter.y / 2.0f};
			SetUiMousePos(MouseCenter);
		}
		else
		{
			SetUiMousePos(m_LastMousePos.value());
		}

		m_LastMousePos = OldMousePos;

		if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
			Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
	}
	else
	{
		OnReset();
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
			Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
	}
}

void CAdminPanel::OnReset()
{
	if(m_Active)
	{
		LockMouse();
		m_PlayerPopup.Reset();
		m_PlayerScreenPos = vec2(0, 0);
		m_ClosestScreenPlayerPos = vec2(0, 0);
		m_HoveredPlayerId = -1;
	}
	m_Active = false;
}

void CAdminPanel::OnRelease()
{
	if(m_Active)
		OnReset();
}

bool CAdminPanel::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive())
		return false;

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CAdminPanel::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;

	if(GameClient()->m_GameConsole.IsActive() || GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_Emoticon.IsActive())
		return false;

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_2)
	{
		// find closest player to mouse
		if(!m_PlayerPopup.m_Visible)
		{
			if(m_HoveredPlayerId != -1)
			{
				m_PlayerPopup.m_PlayerId = m_HoveredPlayerId;
				m_PlayerPopup.m_Visible = true;

				return true;
			}
		}
		else
		{
			m_PlayerPopup.m_Visible = !m_PlayerPopup.m_Visible;
			m_PlayerPopup.Reset();
		}
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		if(m_PlayerPopup.m_Visible)
			m_PlayerPopup.Reset();
		else
			SetActive(false);
		return true;
	}

	if(m_PlayerPopup.m_Visible && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_1 && CLineInput::GetActiveInput() != nullptr &&
	!Hovered(&m_PopupTimerInputRect) &&
	!Hovered(&m_PopupReasonInputRect))
	{
		Ui()->SetActiveItem(&m_PlayerPopup);
		Ui()->SetActiveItem(nullptr);
		Ui()->SetHotItem(nullptr); 
	}

	if(m_PlayerPopup.m_Visible && CLineInput::GetActiveInput() != nullptr)
		Ui()->OnInput(Event);

	return true;
}

void CAdminPanel::OnRender()
{
	if(!IsActive())
		return;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW && !m_PlayerPopup.m_Visible)
	{
		float Speed = 75.0f * 32.0f * (GameClient()->m_Camera.m_Zoom * 6 / g_Config.m_ClDefaultZoom) * (g_Config.m_RcSpectatorMoveSpeed / 100.0f); // Adjusted for frame-time independence
		float FrameTime = Client()->RenderFrameTime();
		if(Input()->KeyIsPressed(KEY_W))
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].y -= Speed * FrameTime;
		if(Input()->KeyIsPressed(KEY_S))
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].y += Speed * FrameTime;
		if(Input()->KeyIsPressed(KEY_A))
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x -= Speed * FrameTime;
		if(Input()->KeyIsPressed(KEY_D))
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x += Speed * FrameTime;
	}

	Ui()->StartCheck();
	Ui()->Update();

	float ClosestDist = 15.0f;
	m_HoveredPlayerId = -1;
	const float ScreenHeight = 300.0f;
	const float ScreenWidth = ScreenHeight * (g_Config.m_RcCustomAspectDisable & RcAspectDisable::ADMINPANEL ? Graphics()->ScreenAspectReal() : Graphics()->ScreenAspect());
	float WorldWidth = 0.0f, WorldHeight = 0.0f;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &WorldWidth, &WorldHeight);
	const vec2 CameraCenter = GameClient()->m_Camera.m_Center;
	const vec2 WorldToScreen = vec2(ScreenWidth / WorldWidth, ScreenHeight / WorldHeight);
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;

		vec2 PlayerPos = GameClient()->m_aClients[i].m_RenderPos;
		if(PlayerPos.x == 0 && PlayerPos.y == 0)
			continue;
		m_PlayerScreenPos = vec2(
			ScreenWidth / 2 + (PlayerPos.x - CameraCenter.x) * WorldToScreen.x,
			ScreenHeight / 2 + (PlayerPos.y - CameraCenter.y) * WorldToScreen.y);

		float Dist = distance(Ui()->MousePos() / 2, m_PlayerScreenPos);
		if(Dist < ClosestDist)
		{
			ClosestDist = Dist;
			m_HoveredPlayerId = i;
			m_ClosestScreenPlayerPos = m_PlayerScreenPos;
		}
	}

	if(m_HoveredPlayerId != -1)
	{
		// Draw a highlight circle
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.5f);
		Graphics()->DrawCircle(m_ClosestScreenPlayerPos.x, m_ClosestScreenPlayerPos.y, 10.0f, 32);
		Graphics()->QuadsEnd();
	}

	if(m_PlayerPopup.m_Visible)
	{
		RenderPlayerPanelPopUp();
	}
	else
	{
		RenderPlayerPanelPlayersList();
	}

	RenderTools()->RenderCursor(Ui()->MousePos() / 2, 12.0f);

	Ui()->FinishCheck();
}

void CAdminPanel::RenderPlayerPanelPopUp()
{
	const char *pPlayerName = GameClient()->m_aClients[m_PlayerPopup.m_PlayerId].m_aName;

	CUIRect Base, Label, ButtonToggle, UpperButton, LowerButton;

	m_PopupHeight = 300.0f;
	m_PopupWidth = 300.0f * (g_Config.m_RcCustomAspectDisable & RcAspectDisable::ADMINPANEL ? Graphics()->ScreenAspectReal() : Graphics()->ScreenAspect());

	if(!m_PlayerPopup.m_LastConfirm)
	{
		Base.h = m_PopupHeight / 1.5;
		Base.w = m_PopupWidth / 1.5 + SAdminPanelProperties::ms_ButtonHeight;
	}
	else
	{
		Base.h = m_PopupHeight / 4;
		Base.w = m_PopupWidth / 3;
	}
	Base.x = m_PopupWidth / 2 - Base.w / 2 - (!m_PlayerPopup.m_LastConfirm ? SAdminPanelProperties::ms_ButtonHeight / 2 : 0);
	Base.y = m_PopupHeight / 2 - Base.h / 2;

	Graphics()->MapScreen(0.0f, 0.0f, m_PopupWidth, m_PopupHeight);

	Base.VSplitRight(SAdminPanelProperties::ms_ButtonHeight, &Base, &ButtonToggle);
	if(!m_PlayerPopup.m_LastConfirm)
	{
		ButtonToggle.HSplitTop(ButtonToggle.h / 2 - 20.0f, nullptr, &ButtonToggle);
		ButtonToggle.HSplitTop(40.0f, &ButtonToggle, nullptr);
		ButtonToggle.HSplitMid(&UpperButton, &LowerButton);
		if(Hovered(&UpperButton))
			UpperButton.Draw(SAdminPanelProperties::WindowColor().WithMultipliedAlpha(1.5f), IGraphics::CORNER_TR, SAdminPanelProperties::ms_Rounding);
		else
			UpperButton.Draw(SAdminPanelProperties::WindowColor(), IGraphics::CORNER_TR, SAdminPanelProperties::ms_Rounding);
		DoIconButton(&UpperButton, m_PlayerPopup.m_ReadyButtons ? FontIcon::CHEVRON_RIGHT : FontIcon::CHEVRON_LEFT, SAdminPanelProperties::ms_IconFontSize * (Hovered(&UpperButton) ? 1.2 : 1), TextRender()->DefaultTextColor());
		if(DoButtonLogic(&UpperButton))
		{
			m_PlayerPopup.m_ReadyButtons = 0;
		}

		if(Hovered(&LowerButton))
			LowerButton.Draw(SAdminPanelProperties::WindowColorDark().WithMultipliedAlpha(1.5f), IGraphics::CORNER_BR, SAdminPanelProperties::ms_Rounding);
		else
			LowerButton.Draw(SAdminPanelProperties::WindowColorDark(), IGraphics::CORNER_BR, SAdminPanelProperties::ms_Rounding);
		DoIconButton(&LowerButton, !m_PlayerPopup.m_ReadyButtons ? FontIcon::CHEVRON_RIGHT : FontIcon::CHEVRON_LEFT, SAdminPanelProperties::ms_IconFontSize * (Hovered(&LowerButton) ? 1.2 : 1), TextRender()->DefaultTextColor());
		if(DoButtonLogic(&LowerButton))
		{
			m_PlayerPopup.m_ReadyButtons = 1;
		}
	}

	Base.Draw(!m_PlayerPopup.m_ReadyButtons ? SAdminPanelProperties::WindowColor() : SAdminPanelProperties::WindowColorDark(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	Base.Margin(SAdminPanelProperties::ms_Padding, &Base);

	Base.HSplitTop(SAdminPanelProperties::ms_HeadlineFontSize + 5, &Label, &Base);
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "Nick: %s, ID: %i", pPlayerName, m_PlayerPopup.m_PlayerId);
	Ui()->DoLabel(&Label, aBuf, 12, TEXTALIGN_MC);
	Base.HSplitTop(5, nullptr, &Base);

	if(!m_PlayerPopup.m_LastConfirm)
	{
		if(!m_PlayerPopup.m_ReadyButtons)
		{
			RenderPlayerPanelPopUpActionButtons(&Base);
			RenderPlayerPanelPopUpTimers(&Base);
		}
		else
			RenderPlayerPanelPopUpReadyButtons(&Base);
		RenderPlayerPanelPopUpInputs(&Base);
		RenderPlayerPanelPopUpCommand(&Base);
	}
	else
		RenderPlayerPanelPopUpLastConfirm(&Base);
}

void CAdminPanel::RenderPlayerPanelPopUpActionButtons(CUIRect *pBase)
{
	CUIRect Container, Action;

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_RconActionHeight, &Container, pBase);

	float ActionSpacing = (pBase->w - (4 * SAdminPanelProperties::ms_RconActionWidth)) / 3;

	Container.VSplitLeft(SAdminPanelProperties::ms_RconActionWidth, &Action, &Container);

	// Kill
	if(Hovered(&Action))
	{
		Action.Draw(m_PlayerPopup.m_ChosenActionButton == 1 ? SAdminPanelProperties::ActionKillAltButtonColor() : SAdminPanelProperties::ActionKillButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconLabeledButtonDown(&Action, "kill", FontIcon::RC_SCULL, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f, TextRender()->DefaultTextColor());
	}
	else
	{
		Action.Draw(m_PlayerPopup.m_ChosenActionButton == 1 ? SAdminPanelProperties::ActionKillButtonColor() : SAdminPanelProperties::ActionKillAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconLabeledButtonDown(&Action, "kill", FontIcon::RC_SCULL, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f, TextRender()->DefaultTextColor());
	}
	if(DoButtonLogic(&Action))
	{
		if(m_PlayerPopup.m_ChosenActionButton == 1)
			m_PlayerPopup.m_ChosenActionButton = 0;
		else
			m_PlayerPopup.m_ChosenActionButton = 1;
	}

	Container.VSplitLeft(ActionSpacing, nullptr, &Container);
	Container.VSplitLeft(SAdminPanelProperties::ms_RconActionWidth, &Action, &Container);

	// kick
	if(Hovered(&Action))
	{
		Action.Draw(m_PlayerPopup.m_ChosenActionButton == 2 ? SAdminPanelProperties::ActionKickAltButtonColor() : SAdminPanelProperties::ActionKickButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconLabeledButtonDown(&Action, "kick", FontIcon::RC_DOOR_OPEN, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f, TextRender()->DefaultTextColor());
	}
	else
	{
		Action.Draw(m_PlayerPopup.m_ChosenActionButton == 2 ? SAdminPanelProperties::ActionKickButtonColor() : SAdminPanelProperties::ActionKickAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconLabeledButtonDown(&Action, "kick", FontIcon::RC_DOOR_OPEN, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f, TextRender()->DefaultTextColor());
	}
	if(DoButtonLogic(&Action))
	{
		if(m_PlayerPopup.m_ChosenActionButton == 2)
			m_PlayerPopup.m_ChosenActionButton = 0;
		else
			m_PlayerPopup.m_ChosenActionButton = 2;
	}

	Container.VSplitLeft(ActionSpacing, nullptr, &Container);
	Container.VSplitLeft(SAdminPanelProperties::ms_RconActionWidth, &Action, &Container);

	// mute
	if(Hovered(&Action))
	{
		Action.Draw(m_PlayerPopup.m_ChosenActionButton == 3 ? SAdminPanelProperties::ActionMuteAltButtonColor() : SAdminPanelProperties::ActionMuteButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconLabeledButtonDown(&Action, "mute", FontIcon::COMMENT_SLASH, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f, TextRender()->DefaultTextColor());
	}
	else
	{
		Action.Draw(m_PlayerPopup.m_ChosenActionButton == 3 ? SAdminPanelProperties::ActionMuteButtonColor() : SAdminPanelProperties::ActionMuteAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconLabeledButtonDown(&Action, "mute", FontIcon::COMMENT_SLASH, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f, TextRender()->DefaultTextColor());
	}
	if(DoButtonLogic(&Action))
	{
		if(m_PlayerPopup.m_ChosenActionButton == 3)
			m_PlayerPopup.m_ChosenActionButton = 0;
		else
			m_PlayerPopup.m_ChosenActionButton = 3;
	}

	// ban
	Container.VSplitLeft(ActionSpacing, nullptr, &Container);
	Container.VSplitLeft(SAdminPanelProperties::ms_RconActionWidth, &Action, &Container);

	if(Hovered(&Action))
	{
		Action.Draw(m_PlayerPopup.m_ChosenActionButton == 4 ? SAdminPanelProperties::ActionBanAltButtonColor() : SAdminPanelProperties::ActionBanButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconLabeledButtonDown(&Action, "local ban", FontIcon::RC_GAVEL, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f, TextRender()->DefaultTextColor());
	}
	else
	{
		Action.Draw(m_PlayerPopup.m_ChosenActionButton == 4 ? SAdminPanelProperties::ActionBanButtonColor() : SAdminPanelProperties::ActionBanAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		DoIconLabeledButtonDown(&Action, "local ban", FontIcon::RC_GAVEL, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f, TextRender()->DefaultTextColor());
	}
	if(DoButtonLogic(&Action))
	{
		if(m_PlayerPopup.m_ChosenActionButton == 4)
		{
			m_PlayerPopup.m_ChosenActionButton = 0;
		}
		else
		{
			m_PlayerPopup.m_ChosenActionButton = 4;
		}
	}
}

void CAdminPanel::RenderPlayerPanelPopUpTimers(CUIRect *pBase)
{
	CUIRect Container, Button;

	static const struct
	{
		const char *pTime;
		int Minutes;
	} s_aElems[] = {
		{"1m", 1}, {"5m", 5}, {"10m", 10}, {"15m", 15}, {"30m", 30}, {"45m", 45},
		{"1h", 60}, {"3h", 180}, {"12h", 720}, {"1d", 1440}, {"3d", 4320}, {"5d", 7200},
		{"1w", 10080}, {"2w", 20160}, {"3w", 30240}, {"1mo", 43200}, {"2mo", 86400}, {"3m", 129600},
		{"6m", 259200}, {"9m", 388800}, {"1y", 518400}, {"2y", 1036800}};

	float ItemSpacingW = (pBase->w - (6 * SAdminPanelProperties::ms_RconTimersWidth)) / 5;

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	Ui()->DoLabel(&Container, ("Parameters"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	for(int i = 0; i < 22; i++)
	{
		if(i == 6 || i == 12 || i == 18)
		{
			pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
			pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
		}
		if(i == 18)
			Container.VSplitLeft(SAdminPanelProperties::ms_RconTimersWidth + ItemSpacingW, nullptr, &Container);

		Container.VSplitLeft(SAdminPanelProperties::ms_RconTimersWidth, &Button, &Container);
		if(Hovered(&Button))
			Button.Draw(m_PlayerPopup.m_MinutesTimers == s_aElems[i].Minutes ? SAdminPanelProperties::ActionBanAltButtonColor() : SAdminPanelProperties::GeneralActiveButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		else
			Button.Draw(m_PlayerPopup.m_MinutesTimers == s_aElems[i].Minutes ? SAdminPanelProperties::GeneralActiveButtonColor() : SAdminPanelProperties::GeneralButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
		Ui()->DoLabel(&Button, s_aElems[i].pTime, SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
		if(DoButtonLogic(&Button))
		{
			if(m_PlayerPopup.m_MinutesTimers == s_aElems[i].Minutes)
				m_PlayerPopup.m_MinutesTimers = 0;
			else
				m_PlayerPopup.m_MinutesTimers = s_aElems[i].Minutes;
		}
		Container.VSplitLeft(ItemSpacingW, nullptr, &Container);
	}
}

void CAdminPanel::RenderPlayerPanelPopUpReadyButtons(CUIRect *pBase)
{
	CUIRect Label, Button, Column;
	static const struct
	{
		const char *Reason;
		int Minutes;
	} s_aReadyMute[] = {
		{"Insult", 15}, {"Spam", 5}, {"Advertising", 15}};
	static const struct
	{
		const char *Reason;
		int Minutes;
	} s_aReadyBan[] = {
		{"Block", 60}, {"Bot Client", 2660}, {"Behaviour Inappropriate", 120}, {"Advertising bot client", 2660}};
	static const struct
	{
		const char *Reason;
	} s_aReadyKick[] = {
		{"Block"}, {"Spam"}};

	// Calculate the maximum height needed for the columns
	char aBuf[128];
	float MaxHeight = SAdminPanelProperties::ms_ButtonHeight + (3 * (SAdminPanelProperties::ms_RconActionHeight + SAdminPanelProperties::ms_ItemSpacing));
	CUIRect ReadyButtonsArea;
	// Reserve a horizontal slice for this entire section
	pBase->HSplitTop(MaxHeight, &ReadyButtonsArea, pBase);
	// Add some spacing after this section
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);

	float AreaWithoutBan = ReadyButtonsArea.w - (SAdminPanelProperties::ms_ReadyButtonsWidth * 2);

	float ItemSpacingW = (AreaWithoutBan - (2 * SAdminPanelProperties::ms_ReadyButtonsWidth)) / 2;

	ReadyButtonsArea.VSplitLeft(SAdminPanelProperties::ms_ReadyButtonsWidth, &Column, &ReadyButtonsArea);
	Column.HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Label, &Column);
	Ui()->DoLabel(&Label, ("Kick"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	for(unsigned i = 0; i < std::size(s_aReadyKick); i++)
	{
		Column.HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, &Column);
		Column.HSplitTop(SAdminPanelProperties::ms_RconActionHeight, &Button, &Column);

		if(Hovered(&Button))
		{
			Button.Draw(m_PlayerPopup.m_ChosenActionButton == 2 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyKick[i].Reason) ? SAdminPanelProperties::ActionKickAltButtonColor() : SAdminPanelProperties::ActionKickButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
			DoLabelLabeledButtonDown(&Button, "", s_aReadyKick[i].Reason, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f);
		}
		else
		{
			Button.Draw(m_PlayerPopup.m_ChosenActionButton == 2 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyKick[i].Reason) ? SAdminPanelProperties::ActionKickButtonColor() : SAdminPanelProperties::ActionKickAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
			DoLabelLabeledButtonDown(&Button, "", s_aReadyKick[i].Reason, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f);
		}
		if(DoButtonLogic(&Button))
		{
			if(m_PlayerPopup.m_ChosenActionButton == 2 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyKick[i].Reason))
			{
				m_PlayerPopup.m_ChosenActionButton = 0;
				m_PlayerPopup.m_InputReason[0] = '\0';
				m_PlayerPopup.m_MinutesTimers = 0;
			}
			else
			{
				m_PlayerPopup.m_ChosenActionButton = 2;
				str_copy(m_PlayerPopup.m_InputReason, s_aReadyKick[i].Reason, sizeof(m_PlayerPopup.m_InputReason));
				m_PlayerPopup.m_MinutesTimers = 0;
			}
		}
	}

	ReadyButtonsArea.VSplitLeft(ItemSpacingW, nullptr, &ReadyButtonsArea);
	ReadyButtonsArea.VSplitLeft(SAdminPanelProperties::ms_ReadyButtonsWidth, &Column, &ReadyButtonsArea);
	Column.HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Label, &Column);
	Ui()->DoLabel(&Label, ("Mute"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	for(unsigned i = 0; i < std::size(s_aReadyMute); i++)
	{
		Column.HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, &Column);
		Column.HSplitTop(SAdminPanelProperties::ms_RconActionHeight, &Button, &Column);
		str_format(aBuf, sizeof(aBuf), "Minutes: %i", s_aReadyMute[i].Minutes);
		if(Hovered(&Button))
		{
			Button.Draw(m_PlayerPopup.m_ChosenActionButton == 3 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyMute[i].Reason) ? SAdminPanelProperties::ActionMuteAltButtonColor() : SAdminPanelProperties::ActionMuteButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
			DoLabelLabeledButtonDown(&Button, aBuf, s_aReadyMute[i].Reason, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f);
		}
		else
		{
			Button.Draw(m_PlayerPopup.m_ChosenActionButton == 3 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyMute[i].Reason) ? SAdminPanelProperties::ActionMuteButtonColor() : SAdminPanelProperties::ActionMuteAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
			DoLabelLabeledButtonDown(&Button, aBuf, s_aReadyMute[i].Reason, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f);
		}
		if(DoButtonLogic(&Button))
		{
			if(m_PlayerPopup.m_ChosenActionButton == 3 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyMute[i].Reason))
			{
				m_PlayerPopup.m_ChosenActionButton = 0;
				m_PlayerPopup.m_InputReason[0] = '\0';
				m_PlayerPopup.m_MinutesTimers = 0;
			}
			else
			{
				m_PlayerPopup.m_ChosenActionButton = 3;
				str_copy(m_PlayerPopup.m_InputReason, s_aReadyMute[i].Reason, sizeof(m_PlayerPopup.m_InputReason));
				m_PlayerPopup.m_MinutesTimers = s_aReadyMute[i].Minutes;
			}
		}
	}

	CUIRect LeftView, RightView;
	ReadyButtonsArea.VSplitRight(SAdminPanelProperties::ms_ReadyButtonsWidth * 2, &ReadyButtonsArea, &Column);
	Column.HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Label, &Column);
	Ui()->DoLabel(&Label, ("Ban"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	Column.VSplitMid(&LeftView, &RightView, SAdminPanelProperties::ms_ItemSpacing);
	int IsLeft = 1;
	for(unsigned i = 0; i < std::size(s_aReadyBan); i++)
	{
		str_format(aBuf, sizeof(aBuf), "Minutes: %i", s_aReadyBan[i].Minutes);

		if(IsLeft == 1)
		{
			LeftView.HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, &LeftView);
			LeftView.HSplitTop(SAdminPanelProperties::ms_RconActionHeight, &Button, &LeftView);
		}
		else
		{
			RightView.HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, &RightView);
			RightView.HSplitTop(SAdminPanelProperties::ms_RconActionHeight, &Button, &RightView);
		}

		if(Hovered(&Button))
		{
			Button.Draw(m_PlayerPopup.m_ChosenActionButton == 4 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyBan[i].Reason) ? SAdminPanelProperties::ActionBanAltButtonColor() : SAdminPanelProperties::ActionBanButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
			DoLabelLabeledButtonDown(&Button, aBuf, s_aReadyBan[i].Reason, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f);
		}
		else
		{
			Button.Draw(m_PlayerPopup.m_ChosenActionButton == 4 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyBan[i].Reason) ? SAdminPanelProperties::ActionBanButtonColor() : SAdminPanelProperties::ActionBanAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
			DoLabelLabeledButtonDown(&Button, aBuf, s_aReadyBan[i].Reason, SAdminPanelProperties::ms_IconFontSize, SAdminPanelProperties::ms_FontSize, 18.0f, 0.0f);
		}
		if(DoButtonLogic(&Button))
		{
			if(m_PlayerPopup.m_ChosenActionButton == 4 && !str_comp(m_PlayerPopup.m_InputReason, s_aReadyBan[i].Reason))
			{
				m_PlayerPopup.m_ChosenActionButton = 0;
				m_PlayerPopup.m_InputReason[0] = '\0';
				m_PlayerPopup.m_MinutesTimers = 0;
			}
			else
			{
				m_PlayerPopup.m_ChosenActionButton = 4;
				str_copy(m_PlayerPopup.m_InputReason, s_aReadyBan[i].Reason, sizeof(m_PlayerPopup.m_InputReason));
				m_PlayerPopup.m_MinutesTimers = s_aReadyBan[i].Minutes;
			}
		}

		if(IsLeft == 1)
			IsLeft = 0;
		else
			IsLeft = 1;
	}
}

void CAdminPanel::RenderPlayerPanelPopUpInputs(CUIRect *pBase)
{
	CUIRect Container, Label;

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	Container.VSplitLeft(35, &Label, &m_PopupTimerInputRect);
	Ui()->DoLabel(&Label, ("Minutes:"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	m_PopupTimerInputRect.VSplitLeft(SAdminPanelProperties::ms_ItemSpacing, nullptr, &m_PopupTimerInputRect);
	m_PlayerPopup.m_TimerInput.SetEmptyText("0");
	if(!m_PlayerPopup.m_TimerInput.IsActive())
		m_PlayerPopup.m_TimerInput.SetInteger(m_PlayerPopup.m_MinutesTimers);
	if(DoEditBoxInUiSpace(&m_PlayerPopup.m_TimerInput, &m_PopupTimerInputRect, SAdminPanelProperties::ms_FontSize))
		m_PlayerPopup.m_MinutesTimers = m_PlayerPopup.m_TimerInput.GetInteger();

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	Container.VSplitLeft(35, &Label, &m_PopupReasonInputRect);
	Ui()->DoLabel(&Label, ("Reason:"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	m_PopupReasonInputRect.VSplitLeft(SAdminPanelProperties::ms_ItemSpacing, nullptr, &m_PopupReasonInputRect);
	m_PlayerPopup.m_LineInput.SetEmptyText("Insult");
	DoEditBoxInUiSpace(&m_PlayerPopup.m_LineInput, &m_PopupReasonInputRect, SAdminPanelProperties::ms_FontSize);
}

void CAdminPanel::RenderPlayerPanelPopUpCommand(CUIRect *pBase)
{
	CUIRect Container;

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	Ui()->DoLabel(&Container, ("bye bye"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	char aBuf[256];
	char aCommand[256];
	switch(m_PlayerPopup.m_ChosenActionButton)
	{
	case 0:
		str_copy(aBuf, "Click buttons pls", sizeof(aBuf));
		break;
	case 1:
		str_format(aBuf, sizeof(aBuf), "rcon kill_pl %i", m_PlayerPopup.m_PlayerId);
		break;
	case 2:
		str_format(aBuf, sizeof(aBuf), "rcon kick %i %s", m_PlayerPopup.m_PlayerId, m_PlayerPopup.m_InputReason);
		break;
	case 3:
		str_format(aBuf, sizeof(aBuf), "rcon muteid %i %i %s", m_PlayerPopup.m_PlayerId, m_PlayerPopup.m_MinutesTimers * 60, m_PlayerPopup.m_InputReason);
		break;
	case 4:
		str_format(aBuf, sizeof(aBuf), "rcon ban %i %i %s", m_PlayerPopup.m_PlayerId, m_PlayerPopup.m_MinutesTimers, m_PlayerPopup.m_InputReason);
		break;
	default:
		GameClient()->Echo("Something gone wrong sorry");
	}
	str_format(aCommand, sizeof(aCommand), "Command: %s", aBuf);
	Ui()->DoLabel(&Container, aCommand, SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	if(Hovered(&Container))
		Container.Draw(SAdminPanelProperties::ActionBanButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	else
		Container.Draw(SAdminPanelProperties::ActionBanAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	Ui()->DoLabel(&Container, ("Execute"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	if(DoButtonLogic(&Container))
	{
		if(m_PlayerPopup.m_ChosenActionButton != 0)
			m_PlayerPopup.m_LastConfirm = 1;
		else
			GameClient()->Echo("Click buttons pls");
	}
}

void CAdminPanel::RenderPlayerPanelPopUpLastConfirm(CUIRect *pBase)
{
	CUIRect Container, Button;

	char aBuf[256];
	char aCommand[256];
	switch(m_PlayerPopup.m_ChosenActionButton)
	{
	case 0:
		str_copy(aBuf, "Click buttons pls", sizeof(aBuf));
		break;
	case 1:
		str_format(aBuf, sizeof(aBuf), "rcon kill_pl %i", m_PlayerPopup.m_PlayerId);
		break;
	case 2:
		str_format(aBuf, sizeof(aBuf), "rcon kick %i %s", m_PlayerPopup.m_PlayerId, m_PlayerPopup.m_InputReason);
		break;
	case 3:
		str_format(aBuf, sizeof(aBuf), "rcon muteid %i %i %s", m_PlayerPopup.m_PlayerId, m_PlayerPopup.m_MinutesTimers * 60, m_PlayerPopup.m_InputReason);
		break;
	case 4:
		str_format(aBuf, sizeof(aBuf), "rcon ban %i %i %s", m_PlayerPopup.m_PlayerId, m_PlayerPopup.m_MinutesTimers, m_PlayerPopup.m_InputReason);
		break;
	default:
		GameClient()->Echo("Something gone wrong sorry");
	}
	str_format(aCommand, sizeof(aCommand), "Command: %s", aBuf);

	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	Ui()->DoLabel(&Container, ("Do you want do this?"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	Ui()->DoLabel(&Container, aCommand, SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	pBase->HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, pBase);
	pBase->HSplitTop(SAdminPanelProperties::ms_ButtonHeight, &Container, pBase);
	Container.VSplitLeft(pBase->w / 2 - 5, &Button, &Container);
	if(Hovered(&Button))
		Button.Draw(SAdminPanelProperties::ActionBanButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	else
		Button.Draw(SAdminPanelProperties::ActionBanAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	Ui()->DoLabel(&Button, ("No"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	if(DoButtonLogic(&Button))
	{
		m_PlayerPopup.m_LastConfirm = 0;
	}
	Container.VSplitLeft(10, nullptr, &Container);
	Container.VSplitLeft(pBase->w / 2 - 5, &Button, &Container);
	if(Hovered(&Button))
		Button.Draw(SAdminPanelProperties::ActionBanButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	else
		Button.Draw(SAdminPanelProperties::ActionBanAltButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
	Ui()->DoLabel(&Button, ("Yes"), SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
	if(DoButtonLogic(&Button))
	{
		GameClient()->Console()->ExecuteLine(aBuf, IConsole::CLIENT_ID_UNSPECIFIED);
		if(g_Config.m_RcAdminPanelPlaySounds)
			GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_GRENADE_EXPLODE, 1.0f);
		OnReset();
	}
}

void CAdminPanel::RenderPlayerPanelPlayersList()
{
	CUIRect Base, Label, OneButton;

	const float ScreenHeight = 300.0f;
	const float ScreenWidth = ScreenHeight * (g_Config.m_RcCustomAspectDisable & RcAspectDisable::ADMINPANEL ? Graphics()->ScreenAspectReal() : Graphics()->ScreenAspect());

	Base.h = ScreenHeight / 1.5 - 10.0f;
	Base.w = 20.0f;
	if(!m_PlayerList.m_Active)
		Base.x = -Base.w + 10;
	else
	{
		Base.w = (SAdminPanelProperties::ms_PlayerBtnWidth + SAdminPanelProperties::ms_ItemSpacing) * m_PlayerList.m_ColumnCount + 20.0f;
		Base.x = 0;
	}
	Base.y = ScreenHeight / 2 - Base.h / 2;

	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);

	Base.VSplitRight(SAdminPanelProperties::ms_ButtonHeight, &Base, &OneButton);
	OneButton.HSplitTop(OneButton.h / 2 - 10.0f, nullptr, &OneButton);
	OneButton.HSplitTop(20.0f, &OneButton, nullptr);
	if(Hovered(&OneButton))
		OneButton.Draw(SAdminPanelProperties::WindowColorDark(), IGraphics::CORNER_R, SAdminPanelProperties::ms_Rounding);
	else
		OneButton.Draw(SAdminPanelProperties::WindowColorDark(), IGraphics::CORNER_R, SAdminPanelProperties::ms_Rounding);
	DoIconButton(&OneButton, !m_PlayerList.m_Active ? FontIcon::CHEVRON_RIGHT : FontIcon::CHEVRON_LEFT, SAdminPanelProperties::ms_IconFontSize * (Hovered(&OneButton) ? 1.2 : 1), TextRender()->DefaultTextColor());
	if(DoButtonLogic(&OneButton))
	{
		m_PlayerList.m_Active = !m_PlayerList.m_Active;
	}

	Base.Draw(SAdminPanelProperties::WindowColorDark(), IGraphics::CORNER_R, SAdminPanelProperties::ms_Rounding);
	Base.Margin(SAdminPanelProperties::ms_Padding, &Base);

	if(IsActivePlrList())
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
			GameClient()->m_Spectator.Spectate(SPEC_FREEVIEW);

		int PlayersSum = GameClient()->m_Snap.m_NumPlayers;

		Base.HSplitTop(SAdminPanelProperties::ms_HeadlineFontSize + 5, &Label, &Base);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Players Count: %i", PlayersSum);
		Ui()->DoLabel(&Label, aBuf, 12, TEXTALIGN_MC);
		Base.HSplitTop(5, nullptr, &Base);

		CUIRect PlayerRow, Player;

		int IsNeedNextLayer = 1;
		m_PlayerList.m_ColumnCount = PlayersSum / 16 + (PlayersSum % 16 != 0 ? 1 : 0);

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i])
				continue;

			str_format(aBuf, sizeof(aBuf), "%i: %s", i, GameClient()->m_aClients[i].m_aName);

			if(IsNeedNextLayer == 1)
			{
				Base.VSplitLeft(SAdminPanelProperties::ms_PlayerBtnWidth, &PlayerRow, &Base);
				Base.VSplitLeft(SAdminPanelProperties::ms_ItemSpacing, nullptr, &Base);
			}

			PlayerRow.HSplitTop(SAdminPanelProperties::ms_ItemSpacing, nullptr, &PlayerRow);
			PlayerRow.HSplitTop(SAdminPanelProperties::ms_FontSize, &Player, &PlayerRow);

			if(Hovered(&Player))
				Player.Draw(SAdminPanelProperties::ActionActiveButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
			else
				Player.Draw(SAdminPanelProperties::ActionGeneralButtonColor(), IGraphics::CORNER_ALL, SAdminPanelProperties::ms_Rounding);
			Ui()->DoLabel(&Player, aBuf, SAdminPanelProperties::ms_FontSize, TEXTALIGN_MC);
			if(DoButtonLogic(&Player))
			{
				GameClient()->m_Spectator.Spectate(i);
			}
			if(IsNeedNextLayer == 16)
				IsNeedNextLayer = 1;
			else
				IsNeedNextLayer++;
		}
	}
}
