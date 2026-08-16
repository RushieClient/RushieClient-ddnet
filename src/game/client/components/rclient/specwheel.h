#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_SPECWHEEL_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_SPECWHEEL_H

#include <base/str.h>

#include <engine/console.h>

#include <game/client/component.h>

#include <vector>

class IConfigManager;

enum
{
	SPECWHEEL_MAX_NAME = 64,
	SPECWHEEL_MAX_CMD = 1024,
	SPECWHEEL_MAX_BINDS = 64
};

class CSpecWheel : public CComponent
{
	float m_AnimationTime = 0.0f;
	float m_aAnimationTimeItems[SPECWHEEL_MAX_BINDS] = {0};

	bool m_Active = false;
	bool m_WasActive = false;

	int m_SelectedBind;

	static void ConOpenSpecwheel(IConsole::IResult *pResult, void *pUserData);
	static void ConAddSpecwheelLegacy(IConsole::IResult *pResult, void *pUserData);
	static void ConAddSpecwheel(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveSpecwheel(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveAllSpecwheelBinds(IConsole::IResult *pResult, void *pUserData);
	static void ConSpecwheelExecuteHover(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

public:
	class CBind
	{
	public:
		char m_aName[SPECWHEEL_MAX_NAME] = "EMPTY";
		char m_aCommand[SPECWHEEL_MAX_CMD] = "";

		bool operator==(const CBind &Other) const
		{
			return str_comp(m_aName, Other.m_aName) == 0 && str_comp(m_aCommand, Other.m_aCommand) == 0;
		}
	};

	std::vector<CBind> m_vSpecBinds;

	CSpecWheel();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	void AddBind(const char *Name, const char *Command);
	void RemoveBind(const char *Name, const char *Command);
	void RemoveBind(int Index);
	void RemoveAllBinds();

	void ExecuteHoveredBind();
	void ExecuteBind(int Bind);

	bool IsActive() const { return m_Active; }
};

#endif
