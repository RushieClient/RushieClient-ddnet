#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_HUD_EDITOR_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_HUD_EDITOR_H

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/ui.h>

class CHudEditor : public CComponent
{
public:
	enum EElement
	{
		ELEM_NONE = -1,
		ELEM_CHAT,
		ELEM_HUDTIMER,
		ELEM_DUMACTIONS,
		ELEM_PLPOS,
		ELEM_SPECCOUNT,
		ELEM_PLAYERSTATE,
		ELEM_FROZENHUD,
		ELEM_FROZENTEXT,
		ELEM_FPSTEXT,
		ELEM_LASTTEXT,
		ELEM_VOICEINDICATOR,
		ELEM_VOICEOVERLAY,
		ELEM_VOTING,
		ELEM_ABOVEBELOW,
		ELEM_NOTIFYONMOVE,
		ELEM_EDGEINFO,
		ELEM_SWAPTIMER,
		ELEM_LOCALTIME,
		ELEM_COUNT,
	};

private:
	static void ConToggleHudEditor(IConsole::IResult *pResult, void *pUserData);
	static void ConHudPos(IConsole::IResult *pResult, void *pUserData);
	static void ConHudReset(IConsole::IResult *pResult, void *pUserData);
	static void ConfigSaveCallback(class IConfigManager *pConfigManager, void *pUserData);

	// To add a new element: add an ELEM_ index, one entry to m_aElements in
	// the constructor and a position computation in ComputeElementBox().
	// Everything else (box, label, settings, reset, drag) is handled generically.
	struct SElement
	{
		const char *m_pName;
		const char *m_pId;
		ivec2 m_Position = ivec2(0, 0);
	};

	SElement m_aElements[ELEM_COUNT];
	CUIRect m_aBoxes[ELEM_COUNT];
	CButtonContainer m_aResetButtons[ELEM_COUNT];

	void ComputeElementBox(int Idx);
	void RenderElementSettings(int Idx);
	int FindElement(const char *pId) const;
	void SetElementPosition(int Idx, ivec2 Position);
	void PrintElementPosition(int Idx) const;

	std::optional<vec2> m_LastMousePos;
	void SetUiMousePos(vec2 Pos);
	void LockMouse();

	int m_DragElement = ELEM_NONE;
	vec2 m_DragPos;
	bool m_MouseWasPressed = false;
	int64_t m_TimeLatestPressedNeed = 0;
	int m_OpenedSettings = 0;

	bool m_Active = false;

	static constexpr float MOVEMENT_INFORMATION_LINE_HEIGHT = 8.0f;
	inline float GetMovementInformationBoxHeight();
	inline int GetDigitsIndex(int Value, int Max);

public:
	CHudEditor();
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	void SetActive(bool Active);
	bool IsActive() const { return m_Active; }
	ivec2 GetElementPosition(EElement Element) const { return m_aElements[Element].m_Position; }
};

#endif // GAME_CLIENT_COMPONENTS_RCLIENT_HUD_EDITOR_H
