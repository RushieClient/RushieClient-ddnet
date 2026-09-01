#include "last_saves.h"

#include "base/dbg.h"
#include "engine/shared/config.h"
#include "game/client/gameclient.h"

#include <base/time.h>

CLastSaves::CLastSaves()
{
	CLastSaves::OnReset();
}

void CLastSaves::OnReset()
{
	m_SavesMapCount = 0;
	m_AppearAnim = 0.0f;
	m_DisappearTime = 0.0f;
	m_LookNextServerMsg = 0;
	m_MsgTime = 0.0f;
	m_NeedSendMsg = false;
}

void CLastSaves::OnRender()
{
	if(!g_Config.m_RcShowSavesCount)
		return;

	if(m_NeedSendMsg && m_MsgTime < time_get())
	{
		bool CommandExist = false;
		for(const auto &Command : GameClient()->m_Chat.m_vServerCommands)
			if(str_comp_nocase("load", Command.m_aName) == 0)
				CommandExist = true;

		if(!CommandExist)
		{
			m_SavesMapCount = 0;
			m_AppearAnim = 0.0f;
			m_DisappearTime = time_get();
			m_LookNextServerMsg = 0;
			m_NeedSendMsg = false;
			return;
		}

		m_LookNextServerMsg = 5;
		m_SavesMapCount = 0;
		m_AppearAnim = 0.0f;
		GameClient()->m_Chat.SendChat(0, "/load", true);
		m_NeedSendMsg = false;
		return;
	}

	if(!m_SavesMapCount)
		return;

	bool TimeNotOut = m_DisappearTime > time_get();
	if(TimeNotOut || m_AppearAnim > 0.0f)
	{
		const float AnimSpeed = 0.2f;
		if(TimeNotOut)
			m_AppearAnim += Client()->RenderFrameTime() / AnimSpeed;
		else
			m_AppearAnim -= Client()->RenderFrameTime() / AnimSpeed * 2.0f;
		m_AppearAnim = std::clamp(m_AppearAnim, 0.0f, 1.0f);
		Ui()->MapScreen();
		const CUIRect *pScreen = Ui()->Screen();
		const float LineSize = 20.0f;
		const float FontSize = 14.0f;
		CUIRect Base;
		Base.w = pScreen->w / 6.0f;
		Base.h = LineSize;
		Base.y = 30.0f;
		Base.x = (pScreen->w - Base.w) / 2.0f;
		Base.Draw(ColorRGBA(0.2f, 1.0f, 0.2f, 0.5f * CRClient::EaseInOutQuad(m_AppearAnim)), IGraphics::CORNER_ALL, Base.h / 3.0f);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "You has %d saves", m_SavesMapCount);
		SLabelProperties Props;
		Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f * CRClient::EaseInOutQuad(m_AppearAnim)));
		Ui()->DoLabel(&Base, aBuf, FontSize, TEXTALIGN_MC, Props);
	}
}

void CLastSaves::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(g_Config.m_RcShowSavesCount != 2)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(pMsg->m_ClientId == -1)
		{
			if(m_LookNextServerMsg < 1)
				return;

			m_LookNextServerMsg--;
			// Voix has 0 saves on Linear
			// Voix has 1 save on Reverse Push
			if(str_find(pMsg->m_pMessage, " saves on "))
			{
				const char *pStart = str_find(pMsg->m_pMessage, " has ");
				const char *pEnd = str_find(pMsg->m_pMessage, " saves on ");

				if(!pStart || !pEnd )
					return;

				char aSaveCount[8];
				str_truncate(aSaveCount, sizeof(aSaveCount), pStart + strlen(" has "), pEnd - pStart);
				m_SavesMapCount = str_toint(aSaveCount);
				dbg_msg("charint", "%s %i", aSaveCount, m_SavesMapCount);
				m_DisappearTime = time_get() + 10.0f * time_freq();
			}

			if(str_find(pMsg->m_pMessage, " save on "))
			{
				const char *pStart = str_find(pMsg->m_pMessage, " has ");
				const char *pEnd = str_find(pMsg->m_pMessage, " save on ");

				if(!pStart || !pEnd )
					return;

				char aSaveCount[8];
				str_truncate(aSaveCount, sizeof(aSaveCount), pStart + strlen(" has "), pEnd - pStart);
				m_SavesMapCount = str_toint(aSaveCount);
				dbg_msg("charint", "%s %i", aSaveCount, m_SavesMapCount);
				m_DisappearTime = time_get() + 10.0f * time_freq();
			}
		}
	}
}

void CLastSaves::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE)
	{
		if(g_Config.m_RcShowSavesCount == 1)
		{
			m_SavesMapCount = GameClient()->m_RClient.GetSavesAmount(GameClient()->Map()->BaseName());
			m_AppearAnim = 0.0f;
			m_DisappearTime = time_get() + 10.0f * time_freq();
		}
		else if(g_Config.m_RcShowSavesCount == 2)
		{
			m_MsgTime = time_get() + 1.0f * time_freq();
			m_NeedSendMsg = true;
		}
	}
}