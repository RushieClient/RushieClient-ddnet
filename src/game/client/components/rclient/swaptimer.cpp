#include "swaptimer.h"

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

CSwapTimer::CSwapTimer()
{
	CSwapTimer::OnReset();
}

void CSwapTimer::OnReset()
{
	m_vSwapList.clear();
}

int CSwapTimer::FindClientId(const char *pName) const
{
	int ClientId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameClient()->m_aClients[i].m_Active)
		{
			if(!str_utf8_comp_nocase(GameClient()->m_aClients[i].m_aName, pName))
			{
				ClientId = i;
				break;
			}
		}
	}

	return ClientId;
}

void CSwapTimer::AddNewSwapEntry(int FromClientId, int ToClientId, int Delay)
{
	if(FromClientId < 0 || ToClientId < 0 || FromClientId == ToClientId)
		return;
	for(auto &Swap : m_vSwapList)
	{
		if(Swap.m_FromClientId != FromClientId)
			continue;
		if(Swap.m_ToClientId == ToClientId)
		{
			if(Delay >= 0)
				Swap.m_Delay = Delay;
			return;
		}
		Swap = {FromClientId, ToClientId, Client()->GameTick(0), Delay >= 0 ? Delay : g_Config.m_SvSaveSwapGamesDelay};
		return;
	}
	m_vSwapList.push_back({FromClientId, ToClientId, Client()->GameTick(0), Delay >= 0 ? Delay : g_Config.m_SvSaveSwapGamesDelay});
}

void CSwapTimer::RemoveSwapEntryId(int FromClientId, int ToClientId)
{
	for(size_t i = 0; i < m_vSwapList.size(); i++)
	{
		if(FromClientId == m_vSwapList[i].m_FromClientId && ToClientId == m_vSwapList[i].m_ToClientId)
		{
			m_vSwapList.erase(m_vSwapList.begin() + i);
			return;
		}
	}
}

void CSwapTimer::RemoveSwapEntrySwapped(int ToClientId, int FromClientId)
{
	for(size_t i = 0; i < m_vSwapList.size(); i++)
	{
		if(FromClientId == m_vSwapList[i].m_FromClientId || ToClientId == m_vSwapList[i].m_FromClientId)
		{
			m_vSwapList.erase(m_vSwapList.begin() + i);
			i--;
		}
	}
}

void CSwapTimer::RemoveSwapEntryIdAll(int ClientId)
{
	for(size_t i = 0; i < m_vSwapList.size(); i++)
	{
		if(ClientId == m_vSwapList[i].m_FromClientId ||
			ClientId == m_vSwapList[i].m_ToClientId)
		{
			m_vSwapList.erase(m_vSwapList.begin() + i);
			i--;
		}
	}
}

void CSwapTimer::GameClientMessage(int MsgType, void *pRawMsg, bool Dummy)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(!g_Config.m_RcEnableSwapTimer)
		return;

	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		const CNetMsg_Sv_KillMsg *pMsg = static_cast<CNetMsg_Sv_KillMsg *>(pRawMsg);
		RemoveSwapEntryIdAll(pMsg->m_Victim);
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		const CNetMsg_Sv_KillMsgTeam *pMsg = static_cast<CNetMsg_Sv_KillMsgTeam *>(pRawMsg);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Teams.Team(i) == pMsg->m_Team)
				RemoveSwapEntryIdAll(i);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		const CNetMsg_Sv_RaceFinish *pMsg = static_cast<CNetMsg_Sv_RaceFinish *>(pRawMsg);
		RemoveSwapEntryIdAll(pMsg->m_ClientId);
	}
	else if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(pMsg->m_ClientId == -1)
		{
			if(str_utf8_find_nocase(pMsg->m_pMessage, " has requested to swap with you. To complete the swap process please wait "))
			{
				const char *pStart = str_find(pMsg->m_pMessage, " has requested to swap with you. To complete the swap process please wait ");
				const char *pEnd = str_find(pMsg->m_pMessage, " seconds and then type ");

				if(!pStart || !pEnd)
					return;

				char aName[16];
				str_truncate(aName, sizeof(aName), pMsg->m_pMessage, pStart - pMsg->m_pMessage);
				int ClientIdName = FindClientId(aName);
				const int Delay = str_toint(pStart + str_length(" has requested to swap with you. To complete the swap process please wait "));
				AddNewSwapEntry(ClientIdName, GameClient()->m_aLocalIds[g_Config.m_ClDummy ^ (int)Dummy], Delay);
			}

			if(str_utf8_find_nocase(pMsg->m_pMessage, "You have requested to swap with "))
			{
				const char *pStart = str_find(pMsg->m_pMessage, "You have requested to swap with ");
				const char *pEnd = str_find(pMsg->m_pMessage, ". Use /cancelswap to cancel the request.");

				if(!pStart || !pEnd)
					return;

				char aName[16];
				int pStartLen = strlen("You have requested to swap with ");
				str_truncate(aName, sizeof(aName), pStart + pStartLen, pEnd - pStart - pStartLen);

				int ClientIdName = FindClientId(aName);
				AddNewSwapEntry(GameClient()->m_aLocalIds[g_Config.m_ClDummy ^ (int)Dummy], ClientIdName);
			}

			if(str_utf8_find_nocase(pMsg->m_pMessage, " has canceled swap with you."))
			{
				const char *pStart = str_find(pMsg->m_pMessage, " has canceled swap with you.");

				if(!pStart)
					return;

				char aName[16];
				str_truncate(aName, sizeof(aName), pMsg->m_pMessage, pStart - pMsg->m_pMessage);

				RemoveSwapEntryId(FindClientId(aName), GameClient()->m_aLocalIds[g_Config.m_ClDummy ^ (int)Dummy]);
			}

			if(str_utf8_find_nocase(pMsg->m_pMessage, "You have canceled swap with "))
			{
				const char *pStart = str_find(pMsg->m_pMessage, "You have canceled swap with ");

				if(!pStart)
					return;

				char aName[16];
				int pStartLen = strlen("You have canceled swap with ");
				str_truncate(aName, sizeof(aName), pStart + pStartLen, strlen(pMsg->m_pMessage) - pStartLen - 1);

				RemoveSwapEntryId(GameClient()->m_aLocalIds[g_Config.m_ClDummy ^ (int)Dummy], FindClientId(aName));
			}
			if(str_utf8_find_nocase(pMsg->m_pMessage, " has swapped with "))
			{
				const char *pStart = str_find(pMsg->m_pMessage, " has swapped with ");

				if(!pStart)
					return;

				char aName[16];
				int pStartLen = strlen(" has swapped with ");
				str_truncate(aName, sizeof(aName), pMsg->m_pMessage, pStart - pMsg->m_pMessage);

				char aName2[16];
				str_truncate(aName2, sizeof(aName2), pStart + pStartLen, strlen(pMsg->m_pMessage) - (pStart - pMsg->m_pMessage) - pStartLen - 1);

				RemoveSwapEntrySwapped(FindClientId(aName), FindClientId(aName2));
			}
		}
	}
}

void CSwapTimer::OnRender()
{
	if(!g_Config.m_RcEnableSwapTimer)
	{
		if(!m_vSwapList.empty())
			OnReset();
		return;
	}

	for(size_t i = 0; i < m_vSwapList.size();)
	{
		const auto &Swap = m_vSwapList[i];
		const int Seconds = (Client()->GameTick(0) - Swap.m_SwapTick) / Client()->GameTickSpeed();
		if(!GameClient()->m_aClients[Swap.m_FromClientId].m_Active ||
			!GameClient()->m_aClients[Swap.m_ToClientId].m_Active ||
			Seconds >= Swap.m_Delay + g_Config.m_SvSwapTimeout)
			m_vSwapList.erase(m_vSwapList.begin() + i);
		else
			i++;
	}

	if(m_vSwapList.empty())
		return;

	Ui()->MapScreen();
	int SwapSize = m_vSwapList.size();
	const CUIRect Screen = *Ui()->Screen();
	const float LineSize = 12.0f;
	const float FontSize = 8.0f;
	const float Margin = 2.0f;
	CUIRect Base;
	Base.w = Screen.w / 4.0f;
	Base.h = 12.0f * SwapSize;
	const ivec2 Offset = GameClient()->m_RcHudEditor.GetElementPosition(CHudEditor::ELEM_SWAPTIMER);
	Base.x = (g_Config.m_RcEnableSwapTimerOnLeftSide ? 0.0f : (Screen.w - Base.w)) + Offset.x * 2.0f;
	Base.y = (Screen.h - Base.h) * 0.5f + Offset.y * 2.0f;

	for(size_t i = 0; i < m_vSwapList.size(); i++)
	{
		CUIRect Line;
		if(i != 0)
			Base.HSplitBottom(Margin, &Base, nullptr);
		Base.HSplitBottom(LineSize, &Base, &Line);
		const int Seconds = (Client()->GameTick(0) - m_vSwapList[i].m_SwapTick) / Client()->GameTickSpeed();
		const int SwapTime = m_vSwapList[i].m_Delay - Seconds;
		const int ExpTime = m_vSwapList[i].m_Delay + g_Config.m_SvSwapTimeout - Seconds;

		char aBuf[128];
		if(SwapTime > 0.0f)
			str_format(aBuf, sizeof(aBuf), "%s → %s. In %d", GameClient()->m_aClients[m_vSwapList[i].m_FromClientId].m_aName, GameClient()->m_aClients[m_vSwapList[i].m_ToClientId].m_aName, SwapTime);
		else
			str_format(aBuf, sizeof(aBuf), "%s → %s. Exp %d", GameClient()->m_aClients[m_vSwapList[i].m_FromClientId].m_aName, GameClient()->m_aClients[m_vSwapList[i].m_ToClientId].m_aName, ExpTime);

		Ui()->DoLabel(&Line, aBuf, FontSize, g_Config.m_RcEnableSwapTimerOnLeftSide ? TEXTALIGN_ML : TEXTALIGN_MR);
	}
}
