#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_ADMINPANEL_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_ADMINPANEL_H

#include "game/client/lineinput.h"
#include "game/client/ui.h"

#include <engine/console.h>

#include <game/client/component.h>
class CAdminPanel : public CComponent
{
	bool m_Active = false;

	static void ConToggleAdminPanel(IConsole::IResult *pResult, void *pUserData);

	float m_PopupHeight;
	float m_PopupWidth;

	CUIRect m_PopupTimerInputRect;
	CUIRect m_PopupReasonInputRect;

	//RClient
	std::optional<vec2> m_LastMousePos;
	void SetUiMousePos(vec2 Pos);
	void LockMouse();

	vec2 m_PlayerScreenPos;
	vec2 m_ClosestScreenPlayerPos;
	int m_HoveredPlayerId = -1;

	struct SPlayerPopup
	{
		bool m_Visible = false;
		int m_PlayerId = -1;
		int m_ChosenActionButton = 0; //1-kill 2-kick 3-mute 4-local ban
		CLineInput m_LineInput;
		char m_InputReason[128];
		CLineInputNumber m_TimerInput;
		int m_MinutesTimers = 0;
		int m_LastConfirm = 0;
		int m_ReadyButtons = 0;
		void Reset()
		{
			m_Visible = false;
			m_PlayerId = -1;
			m_ChosenActionButton = 0; //1-kill 2-kick 3-mute 4-local ban
			m_LineInput.Deactivate();
			m_TimerInput.Deactivate();
			m_TimerInput.SetInteger(0);
			m_InputReason[0] = '\0';
			m_MinutesTimers = 0;
			m_LastConfirm = 0;
			m_ReadyButtons = 0;
		}
	} m_PlayerPopup;

	struct SPlayerList
	{
		bool m_Active = false;
		int m_ColumnCount = 0;
	} m_PlayerList;

	void RenderPlayerPanelPopUp();
	void RenderPlayerPanelPopUpActionButtons(CUIRect *pBase);
	void RenderPlayerPanelPopUpReadyButtons(CUIRect *pBase);
	void RenderPlayerPanelPopUpTimers(CUIRect *pBase);
	void RenderPlayerPanelPopUpInputs(CUIRect *pBase);
	void RenderPlayerPanelPopUpCommand(CUIRect *pBase);
	void RenderPlayerPanelPopUpLastConfirm(CUIRect *pBase);
	void RenderPlayerPanelPlayersList();

	void DoIconButton(CUIRect *pRect, const char *pIcon, float TextSize, ColorRGBA IconColor) const;
	void DoIconLabeledButton(CUIRect *pRect, const char *pTitle, const char *pIcon, float TextSize, float Height, ColorRGBA IconColor) const;
	void DoIconLabeledButtonDown(CUIRect *pRect, const char *pTitle, const char *pIcon, float IconSize, float TextSize, float Height, float Dif, ColorRGBA IconColor) const;
	void DoLabelLabeledButtonDown(CUIRect *pRect, const char *pTitleDown, const char *pTitle, float TextSize, float TextSizeDown, float Height, float Dif) const;
	bool DoEditBoxInUiSpace(CLineInput *pLineInput, const CUIRect *pLocalRect, float FontSize);

	bool Hovered(const CUIRect *pRect) const
	{
		return pRect->Inside(Ui()->MousePos() / 2.0f);
	}

	bool DoButtonLogic(const CUIRect *pRect)
	{
		return pRect->Inside(Ui()->MousePos() / 2.0f) && Ui()->MouseButtonClicked(0);
	}

public:
	CAdminPanel();
	int Sizeof() const override { return sizeof(*this); }

	void SetActive(bool Active);

	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsActive() const { return m_Active; }
	bool IsActivePopup() const { return m_PlayerPopup.m_Visible; }
	bool IsActivePlrList() const { return m_PlayerList.m_Active; }
};

#endif
