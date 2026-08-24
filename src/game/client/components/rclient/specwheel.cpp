#include "specwheel.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include "../rclient/rclient_include.h"

CSpecWheel::CSpecWheel()
{
	CSpecWheel::OnReset();
}

void CSpecWheel::ConSpecwheelExecuteHover(IConsole::IResult *pResult, void *pUserData)
{
	CSpecWheel *pThis = (CSpecWheel *)pUserData;
	pThis->ExecuteHoveredBind();
}

void CSpecWheel::ConOpenSpecwheel(IConsole::IResult *pResult, void *pUserData)
{
	CSpecWheel *pThis = (CSpecWheel *)pUserData;
	if(pThis->Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(!pThis->GameClient()->m_Snap.m_SpecInfo.m_Active || pThis->GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
		{
			pThis->GameClient()->m_Chat.Echo("U are not spec player");
			pThis->m_Active = false;
		}
		if(pThis->GameClient()->m_Emoticon.IsActive())
			pThis->m_Active = false;
		else
			pThis->m_Active = pResult->GetInteger(0) != 0;
	}
}

void CSpecWheel::ConAddSpecwheelLegacy(IConsole::IResult *pResult, void *pUserData)
{
	int BindPos = pResult->GetInteger(0);
	if(BindPos < 0 || BindPos >= SPECWHEEL_MAX_BINDS)
		return;

	const char *aName = pResult->GetString(1);
	const char *aCommand = pResult->GetString(2);

	CSpecWheel *pThis = static_cast<CSpecWheel *>(pUserData);
	if(pThis->m_vSpecBinds.size() <= (size_t)BindPos)
		pThis->m_vSpecBinds.resize((size_t)BindPos + 1);

	str_copy(pThis->m_vSpecBinds[BindPos].m_aName, aName);
	str_copy(pThis->m_vSpecBinds[BindPos].m_aCommand, aCommand);
}

void CSpecWheel::ConAddSpecwheel(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	const char *aCommand = pResult->GetString(1);

	CSpecWheel *pThis = static_cast<CSpecWheel *>(pUserData);
	pThis->AddBind(aName, aCommand);
}

void CSpecWheel::ConRemoveSpecwheel(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	const char *aCommand = pResult->GetString(1);

	CSpecWheel *pThis = static_cast<CSpecWheel *>(pUserData);
	pThis->RemoveBind(aName, aCommand);
}

void CSpecWheel::ConRemoveAllSpecwheelBinds(IConsole::IResult *pResult, void *pUserData)
{
	CSpecWheel *pThis = static_cast<CSpecWheel *>(pUserData);
	pThis->RemoveAllBinds();
}

void CSpecWheel::AddBind(const char *pName, const char *pCommand)
{
	if((pName[0] == '\0' && pCommand[0] == '\0') || m_vSpecBinds.size() >= SPECWHEEL_MAX_BINDS)
		return;

	CBind Bind;
	str_copy(Bind.m_aName, pName);
	str_copy(Bind.m_aCommand, pCommand);
	m_vSpecBinds.push_back(Bind);
}

void CSpecWheel::RemoveBind(const char *pName, const char *pCommand)
{
	CBind Bind;
	str_copy(Bind.m_aName, pName);
	str_copy(Bind.m_aCommand, pCommand);
	auto It = std::find(m_vSpecBinds.begin(), m_vSpecBinds.end(), Bind);
	if(It != m_vSpecBinds.end())
		m_vSpecBinds.erase(It);
}

void CSpecWheel::RemoveBind(int Index)
{
	if(Index >= static_cast<int>(m_vSpecBinds.size()) || Index < 0)
		return;
	auto Pos = m_vSpecBinds.begin() + Index;
	m_vSpecBinds.erase(Pos);
}

void CSpecWheel::RemoveAllBinds()
{
	m_vSpecBinds.clear();
}

void CSpecWheel::OnConsoleInit()
{
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::TCLIENT);

	Console()->Register("+specwheel", "", CFGFLAG_CLIENT, ConOpenSpecwheel, this, "Open specwheel selector");
	Console()->Register("+specwheel_execute_hover", "", CFGFLAG_CLIENT, ConSpecwheelExecuteHover, this, "Execute hovered specwheel bind");

	Console()->Register("specwheel", "i[index] s[name] s[command]", CFGFLAG_CLIENT, ConAddSpecwheelLegacy, this, "DONT USE THIS! USE add_specwheel INSTEAD!");
	Console()->Register("add_specwheel", "s[name] s[command]", CFGFLAG_CLIENT, ConAddSpecwheel, this, "Add a bind to the specwheel");
	Console()->Register("remove_specwheel", "s[name] s[command]", CFGFLAG_CLIENT, ConRemoveSpecwheel, this, "Remove a bind from the specwheel");
	Console()->Register("delete_all_specwheel_binds", "", CFGFLAG_CLIENT, ConRemoveAllSpecwheelBinds, this, "Removes all specwheel binds");
}

void CSpecWheel::OnReset()
{
	m_WasActive = false;
	m_Active = false;
	m_SelectedBind = -1;
}

void CSpecWheel::OnRelease()
{
	m_Active = false;
}

bool CSpecWheel::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	GameClient()->m_Emoticon.m_SelectorMouse += vec2(x, y);
	return true;
}

bool CSpecWheel::OnInput(const IInput::CEvent &Event)
{
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		OnRelease();
		return true;
	}
	return false;
}

void CSpecWheel::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
		return;

	static const auto QuadEaseInOut = [](float t) -> float {
		if(t == 0.0f)
			return 0.0f;
		if(t == 1.0f)
			return 1.0f;
		return (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2) / 2.0f);
	};
	static const auto PositiveMod = [](float x, float y) -> float {
		return std::fmod(x + y, y);
	};

	static const float s_InnerOuterMouseBoundaryRadius = 110.0f;
	static const float s_OuterMouseLimitRadius = 170.0f;
	static const float s_OuterItemRadius = 140.0f; // 10.0f less than emoticons for extra text space
	static const float s_OuterCircleRadius = 190.0f;
	// static const float s_InnerCircleRadius = 100.0f;
	static const float s_FontSize = 12.0f;
	static const float s_FontSizeSelected = 18.0f;

	const float AnimationTime = (float)g_Config.m_TcAnimateWheelTime / 1000.0f;
	const float ItemAnimationTime = AnimationTime / 2.0f;

	if(AnimationTime != 0.0f)
	{
		for(float &Time : m_aAnimationTimeItems)
			Time = std::max(0.0f, Time - Client()->RenderFrameTime());
	}

	if(!m_Active)
	{
		if(m_WasActive)
		{
			if(g_Config.m_TcResetBindWheelMouse)
				GameClient()->m_Emoticon.m_SelectorMouse = vec2(0.0f, 0.0f);
			if(m_SelectedBind != -1)
				ExecuteBind(m_SelectedBind);
		}
		m_WasActive = false;

		if(AnimationTime == 0.0f)
			return;

		m_AnimationTime -= Client()->RenderFrameTime() * 3.0f; // Close animation 3x faster
		if(m_AnimationTime <= 0.0f)
		{
			m_AnimationTime = 0.0f;
			return;
		}
	}
	else
	{
		m_AnimationTime += Client()->RenderFrameTime();
		if(m_AnimationTime > AnimationTime)
			m_AnimationTime = AnimationTime;
		m_WasActive = true;
	}

	Ui()->m_RcForceRealAspect = g_Config.m_RcCustomAspectDisable & RcAspectDisable::WHEELS;
	const CUIRect Screen = *Ui()->Screen();

	const bool WasTouchPressed = GameClient()->m_Emoticon.m_TouchState.m_AnyPressed;
	Ui()->UpdateTouchState(GameClient()->m_Emoticon.m_TouchState);
	if(GameClient()->m_Emoticon.m_TouchState.m_AnyPressed)
	{
		const vec2 TouchPos = (GameClient()->m_Emoticon.m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * Screen.Size();
		const float TouchCenterDistance = length(TouchPos);
		if(TouchCenterDistance <= s_OuterMouseLimitRadius)
		{
			GameClient()->m_Emoticon.m_SelectorMouse = TouchPos;
		}
		else if(TouchCenterDistance > s_OuterCircleRadius)
		{
			GameClient()->m_Emoticon.m_TouchPressedOutside = true;
		}
	}
	else if(WasTouchPressed)
	{
		m_Active = false;
	}

	std::array<float, 2> aAnimationPhase;
	if(AnimationTime == 0.0f)
	{
		aAnimationPhase.fill(1.0f);
	}
	else
	{
		aAnimationPhase[0] = QuadEaseInOut(m_AnimationTime / AnimationTime);
		aAnimationPhase[1] = aAnimationPhase[0] * aAnimationPhase[0];
	}

	if(length(GameClient()->m_Emoticon.m_SelectorMouse) > s_OuterMouseLimitRadius)
		GameClient()->m_Emoticon.m_SelectorMouse = normalize(GameClient()->m_Emoticon.m_SelectorMouse) * s_OuterMouseLimitRadius;

	int SegmentCount = m_vSpecBinds.size();
	if(SegmentCount == 0)
	{
		m_SelectedBind = -1;
	}
	else
	{
		const float SelectedAngle = angle(GameClient()->m_Emoticon.m_SelectorMouse);
		if(length(GameClient()->m_Emoticon.m_SelectorMouse) > s_InnerOuterMouseBoundaryRadius)
			m_SelectedBind = PositiveMod(std::round(SelectedAngle / (2.0f * pi) * SegmentCount), SegmentCount);
		else
			m_SelectedBind = -1;
	}

	if(m_SelectedBind != -1)
	{
		m_aAnimationTimeItems[m_SelectedBind] += Client()->RenderFrameTime() * 2.0f; // To counteract earlier decrement
		if(m_aAnimationTimeItems[m_SelectedBind] >= ItemAnimationTime)
			m_aAnimationTimeItems[m_SelectedBind] = ItemAnimationTime;
	}

	Ui()->MapScreen();

	Graphics()->BlendNormal();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f * aAnimationPhase[0]);
	Graphics()->DrawCircle(Screen.w / 2.0f, Screen.h / 2.0f, s_OuterCircleRadius * aAnimationPhase[0], 64);
	Graphics()->QuadsEnd();

	Graphics()->WrapClamp();
	const float Theta = pi * 2.0f / std::max<float>(1.0f, m_vSpecBinds.size()); // Prevent divide by 0
	for(int i = 0; i < static_cast<int>(m_vSpecBinds.size()); i++)
	{
		const CBind &Bind = m_vSpecBinds[i];
		const float Angle = Theta * i;
		const float Phase = ItemAnimationTime == 0.0f ? (i == m_SelectedBind ? 1.0f : 0.0f) : QuadEaseInOut(m_aAnimationTimeItems[i] / ItemAnimationTime);
		const float FontSize = (s_FontSize + Phase * (s_FontSizeSelected - s_FontSize)) * aAnimationPhase[1];
		const char *pName = Bind.m_aName;
		if(pName[0] == '\0')
		{
			pName = "Empty";
			TextRender()->TextColor(0.7f, 0.7f, 0.7f, aAnimationPhase[1]);
		}
		else
		{
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, aAnimationPhase[1]);
		}
		const vec2 Pos = vec2(Screen.x, Screen.y) + vec2(Screen.w, Screen.h) / 2.0f + direction(Angle) * s_OuterItemRadius * aAnimationPhase[1];
		const CUIRect Rect = CUIRect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
		Ui()->DoLabel(&Rect, pName, FontSize, TEXTALIGN_MC);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->WrapNormal();

	// For future middle circle usage
	// Graphics()->TextureClear();
	// Graphics()->QuadsBegin();
	// Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.3f * AnimationPhase3);
	// DrawCircle(Screen.w / 2.0f, Screen.h / 2.0f, s_InnerCircleRadius * AnimationPhase3, 64);
	// Graphics()->QuadsEnd();

	RenderTools()->RenderCursor(GameClient()->m_Emoticon.m_SelectorMouse + vec2(Screen.w, Screen.h) / 2.0f, 24.0f, aAnimationPhase[0]);

	Ui()->m_RcForceRealAspect = false;
}

void CSpecWheel::ExecuteBind(int Bind)
{
	if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
		return;
	char aEscapedName[MAX_NAME_LENGTH * 2] = "";
	char *pDst = aEscapedName;
	str_escape(&pDst, GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId].m_aName, aEscapedName + sizeof(aEscapedName));

	std::string Command{m_vSpecBinds[Bind].m_aCommand};
	std::string old_str{"%plnick%"};
	size_t startnick {Command.find(old_str)};
	while (startnick != std::string::npos)
	{
		Command.replace(startnick, old_str.length(), aEscapedName);
		startnick = Command.find(old_str, startnick + str_length(aEscapedName));
	}
	old_str = {"%plid%"};
	size_t startid {Command.find(old_str)};
	while (startid != std::string::npos)
	{
		Command.replace(startid, old_str.length(), std::to_string(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId));
		startid = Command.find(old_str, startid + std::to_string(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId).length());
	}
	Console()->ExecuteLine(Command.c_str(), IConsole::CLIENT_ID_UNSPECIFIED);
}
void CSpecWheel::ExecuteHoveredBind()
{
	if(m_SelectedBind >= 0)
		ExecuteBind(m_SelectedBind);
}

void CSpecWheel::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CSpecWheel *pThis = (CSpecWheel *)pUserData;

	for(CBind &Bind : pThis->m_vSpecBinds)
	{
		char aBuf[SPECWHEEL_MAX_CMD * 2] = "";
		char *pEnd = aBuf + sizeof(aBuf);
		char *pDst;
		str_append(aBuf, "add_specwheel \"");
		// Escape name
		pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, Bind.m_aName, pEnd);
		str_append(aBuf, "\" \"");
		// Escape command
		pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, Bind.m_aCommand, pEnd);
		str_append(aBuf, "\"");
		pConfigManager->WriteLine(aBuf, ConfigDomain::RCLIENT);
	}
}
