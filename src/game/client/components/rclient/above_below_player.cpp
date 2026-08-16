#include "above_below_player.h"

#include "game/client/gameclient.h"
#include "engine/shared/config.h"
#include "rclient_include.h"

CAboveBelowPlayer::CAboveBelowPlayer()
{
	CAboveBelowPlayer::OnReset();
}

void CAboveBelowPlayer::OnReset()
{
	m_AboveAnim = 0.0f;
	m_SameAnim = 0.0f;
	m_BelowAnim = 0.0f;
	m_PlayerAbove = false;
	m_PlayerSame = false;
	m_PlayerBelow = false;
}

void CAboveBelowPlayer::OnRender()
{
	if(!g_Config.m_RcNotifyWhenAbovePosPlayer && !g_Config.m_RcNotifyWhenBelowPosPlayer && !g_Config.m_RcNotifyWhenSamePosPlayer)
		return;

	const int LocalClientId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
	m_PlayerAbove = false;
	m_PlayerSame = false;
	m_PlayerBelow = false;

	if(LocalClientId == SPEC_FREEVIEW)
	{
		return;
	}

	vec2 m_LocalPos;
	int m_LinesNum = 0;

	if(GameClient()->m_aClients[LocalClientId].m_SpecCharPresent)
	{
		m_LocalPos = GameClient()->m_aClients[LocalClientId].m_SpecChar / 32.0f;
	}
	else
	{
		const CNetObj_Character *pLocalChar = &GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Cur;
		m_LocalPos = vec2(pLocalChar->m_X, pLocalChar->m_Y) / 32.0f;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_aClients[i].m_Active)
			continue;

		if(i == LocalClientId)
			continue;

		if(!GameClient()->m_Teams.CanCollide(LocalClientId, i))
			continue;

		const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[i].m_Cur;
		vec2 m_PlayerPos = vec2(pCurChar->m_X, pCurChar->m_Y) / 32.0f;
		if(m_PlayerPos.x == m_LocalPos.x)
		{
			if(m_PlayerPos.y == m_LocalPos.y && g_Config.m_RcNotifyWhenSamePosPlayer)
				m_PlayerSame = true;
			if(m_PlayerPos.y < m_LocalPos.y && g_Config.m_RcNotifyWhenBelowPosPlayer)
				m_PlayerBelow = true;
			if(m_PlayerPos.y > m_LocalPos.y && g_Config.m_RcNotifyWhenAbovePosPlayer)
				m_PlayerAbove = true;
		}
	}

	m_LinesNum = (m_PlayerAbove ? 1 : m_AboveAnim > 0.0f ? 1 : 0) + (m_PlayerSame ? 1 : m_SameAnim > 0.0f ? 1 : 0) + (m_PlayerBelow ? 1 : m_BelowAnim > 0.0f ? 1 : 0);

	if(!m_LinesNum)
		return;

	Ui()->m_RcForceRealAspect = g_Config.m_RcCustomAspectDisable & RcAspectDisable::NOTIFYINSPEC;
	Ui()->MapScreen();
	const CUIRect *pScreen = Ui()->Screen();
	const float LineSize = 20.0f;
	const float FontSize = 14.0f;
	const float Margin = 5.0f;
	float LineHeight = 20 * m_LinesNum + 5 * (m_LinesNum - 1);
	// Line screen/4 - w, 20px - h, Marign - 5px
	CUIRect Line, CurLine;
	Line.w = pScreen->w / 6.0f;
	Line.h = LineHeight;
	Line.x = (pScreen->w - Line.w) * g_Config.m_RcNotifyWhenPosPlayerPosX / 100.0f;
	Line.y = (pScreen->h - Line.h) * g_Config.m_RcNotifyWhenPosPlayerPosY / 100.0f;
	Ui()->m_RcForceRealAspect = false;

	if(m_PlayerAbove || m_AboveAnim > 0.0f)
	{
		Line.HSplitTop(LineSize, &CurLine, &Line);
		const float AnimSpeed = 0.2f;
		if(m_PlayerAbove)
			m_AboveAnim += Client()->RenderFrameTime() / AnimSpeed;
		else
			m_AboveAnim -= Client()->RenderFrameTime() / AnimSpeed * 2.0f;
		m_AboveAnim = std::clamp(m_AboveAnim, 0.0f, 1.0f);
		CurLine.Draw(ColorRGBA(0.25f, 0.70f, 0.40f, 0.5f * CRClient::EaseInOutQuad(m_AboveAnim)), IGraphics::CORNER_ALL, CurLine.h / 3.0f);
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f * CRClient::EaseInOutQuad(m_AboveAnim)));
		Ui()->DoLabel(&CurLine, "/\\ You Above /\\", FontSize, TEXTALIGN_MC, Props);
		Line.HSplitTop(Margin, nullptr, &Line);
	}
	if(m_PlayerSame || m_SameAnim > 0.0f)
	{
		Line.HSplitTop(LineSize, &CurLine, &Line);
		const float AnimSpeed = 0.2f;
		if(m_PlayerSame)
			m_SameAnim += Client()->RenderFrameTime() / AnimSpeed;
		else
			m_SameAnim -= Client()->RenderFrameTime() / AnimSpeed * 2.0f;
		m_SameAnim = std::clamp(m_SameAnim, 0.0f, 1.0f);
		CurLine.Draw(ColorRGBA(0.90f, 0.65f, 0.20f, 0.5f * CRClient::EaseInOutQuad(m_SameAnim)), IGraphics::CORNER_ALL, CurLine.h / 3.0f);
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f * CRClient::EaseInOutQuad(m_SameAnim)));
		Ui()->DoLabel(&CurLine, "= Same =", FontSize, TEXTALIGN_MC, Props);
		Line.HSplitTop(Margin, nullptr, &Line);
	}
	if(m_PlayerBelow || m_BelowAnim > 0.0f)
	{
		Line.HSplitTop(LineSize, &CurLine, &Line);
		const float AnimSpeed = 0.2f;
		if(m_PlayerBelow)
			m_BelowAnim += Client()->RenderFrameTime() / AnimSpeed;
		else
			m_BelowAnim -= Client()->RenderFrameTime() / AnimSpeed * 2.0f;
		m_BelowAnim = std::clamp(m_BelowAnim, 0.0f, 1.0f);
		CurLine.Draw(ColorRGBA(0.85f, 0.30f, 0.30f, 0.5f * CRClient::EaseInOutQuad(m_BelowAnim)), IGraphics::CORNER_ALL, CurLine.h / 3.0f);
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f * CRClient::EaseInOutQuad(m_BelowAnim)));
		Ui()->DoLabel(&CurLine, "\\/ You Below \\/", FontSize, TEXTALIGN_MC, Props);
	}
}