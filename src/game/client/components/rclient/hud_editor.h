#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_HUD_EDITOR_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_HUD_EDITOR_H

#include "engine/console.h"
#include "game/client/component.h"
#include "game/client/ui.h"

class CHudEditor : public CComponent
{
	static void ConToggleHudEditor(IConsole::IResult *pResult, void *pUserData);

	enum
	{
		ELEM_NONE = -1,
		ELEM_CHAT = 0,
		ELEM_HUDTIMER,
		ELEM_DUMACTIONS,
		ELEM_PLPOS,
		ELEM_SPECCOUNT,
		ELEM_PLAYERSTATE,
		ELEM_COUNT,
	};

	// To add a new element: add an ELEM_ index, one entry to m_aElements in
	// the constructor and a position computation in ComputeElementBox().
	// Everything else (box, label, settings, reset, drag) is handled generically.
	struct SElement
	{
		const char *m_pName;
		int *m_pConfigX = nullptr;
		int *m_pConfigY = nullptr;
		float m_DragScaleX = 1.0f; // extra horizontal drag scale, updated by ComputeElementBox()
	};

	SElement m_aElements[ELEM_COUNT];
	CUIRect m_aBoxes[ELEM_COUNT];
	CButtonContainer m_aResetButtons[ELEM_COUNT];

	void ComputeElementBox(int Idx);
	void RenderElementSettings(int Idx);

	std::optional<vec2> m_LastMousePos;
	void SetUiMousePos(vec2 Pos);
	void LockMouse();

	int m_DragElement = ELEM_NONE;
	vec2 m_DragPos;
	bool m_MouseWasPressed = false;
	int64_t m_TimeLatestPressedNeed = 0;
	int m_OpenedSettings = 0;

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
