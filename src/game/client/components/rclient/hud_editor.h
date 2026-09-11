#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_HUD_EDITOR_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_HUD_EDITOR_H

#include "engine/console.h"
#include "game/client/component.h"
#include "game/client/ui.h"

class CHudEditor : public CComponent
{
	static void ConToggleHudEditor(IConsole::IResult *pResult, void *pUserData);
	vec2 m_ChatPos;
	vec2 m_HudTimerPos;
	vec2 m_DumActionsPos;
	vec2 m_PlPosPos;
	vec2 m_SpecCountPos;

	std::optional<vec2> m_LastMousePos;
	void SetUiMousePos(vec2 Pos);
	void LockMouse();

	int m_DragElement = 0; // 1-chat 2-hudtimer 3-dumactions 4-plpos 5-SpecCount
	vec2 m_DragPos;
	bool m_MouseWasPressed = false;
	int64_t m_TimeLatestPressedNeed = 0;
	int m_OpenedSettings = 0;

	CButtonContainer m_ResetButtonChat;
	CButtonContainer m_ResetButtonHudTimer;
	CButtonContainer m_ResetButtonDumActions;
	CButtonContainer m_ResetButtonPlPos;
	CButtonContainer m_ResetButtonSpecCount;

	bool m_Active = false;
	void SetActive(bool Active);
public:
	CHudEditor();
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	bool IsActive() const { return m_Active; }
};

#endif // GAME_CLIENT_COMPONENTS_RCLIENT_HUD_EDITOR_H
