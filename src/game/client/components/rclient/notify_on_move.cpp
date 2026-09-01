#include "notify_on_move.h"

#include "game/client/gameclient.h"
#include "engine/shared/config.h"
#include "rclient_include.h"

CNotifyOnMove::CNotifyOnMove()
{
	CNotifyOnMove::OnReset();
}

void CNotifyOnMove::OnReset()
{
	m_SoundPlayedWindow = false;
	m_SoundPlayedSpec = false;
	m_SpecNotifyMoved = false;
	m_SpecNotifyAnim = 0.0f;
	m_SpecHasLastPos = false;
	m_SpecLastTargetId = -1000;
}

void CNotifyOnMove::OnInit()
{
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
}

bool CNotifyOnMove::OnInput(const IInput::CEvent &Event)
{
	if(!m_SpecNotifyMoved)
		return false;

	if(!g_Config.m_RcTextOnMoveInSpecRemoveOnInput)
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		m_SpecNotifyMovedRemove = true;
	}

	return false;
}

void CNotifyOnMove::OnRender()
{
	if(g_Config.m_RcPlayOnMoveNonInactive)
	{
		if(!m_pGraphics->WindowActive())
		{
			const bool LocalCharacterMoved = GameClient()->m_Snap.m_pLocalCharacter &&
			GameClient()->m_Snap.m_pLocalPrevCharacter &&
			(GameClient()->m_Snap.m_pLocalCharacter->m_X != GameClient()->m_Snap.m_pLocalPrevCharacter->m_X ||
				GameClient()->m_Snap.m_pLocalCharacter->m_Y != GameClient()->m_Snap.m_pLocalPrevCharacter->m_Y);

			if(!m_SoundPlayedWindow && LocalCharacterMoved)
			{
				switch(g_Config.m_RcSoundOnMoveNonInactive)
				{
				case 0:
					GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_RI_INACTIVE_MOVE_WAKEUP, 1.0f);
					break;
				case 1:
					GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_GRENADE_EXPLODE, 1.0f);
					break;
				case 2:
					GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
					break;
				default:;
				}
				m_SoundPlayedWindow = true;
			}
		}
		else
		{
			m_SoundPlayedWindow = false;
		}
	}

	if(g_Config.m_RcNotifyOnMoveInSpec)
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const int ClientId = GameClient()->m_Snap.m_LocalClientId;
			const int SpecTargetId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
			const bool TargetChanged = SpecTargetId != m_SpecLastTargetId;
			m_SpecLastTargetId = SpecTargetId;

			bool LocalCharacterMoved = false;
			if(ClientId >= 0 && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			{
				const vec2 Pos(GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_X,
					GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Y);
				if(m_SpecHasLastPos && !TargetChanged)
					LocalCharacterMoved = Pos != m_SpecLastPos;
				m_SpecLastPos = Pos;
				m_SpecHasLastPos = true;
			}
			else
			{
				m_SpecHasLastPos = false;
			}

			if(LocalCharacterMoved)
			{
				m_SpecNotifyMoved = true;

				if(!m_SoundPlayedSpec && g_Config.m_RcPlayOnMoveInSpec)
				{
					switch(g_Config.m_RcSoundOnMoveInSpec)
					{
					case 0:
						GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_RI_INACTIVE_MOVE_WAKEUP, 1.0f);
						break;
					case 1:
						GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_GRENADE_EXPLODE, 1.0f);
						break;
					case 2:
						GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
						break;
					default:;
					}
					m_SoundPlayedSpec = true;
				}
			}
		}
		else
		{
			m_SpecNotifyMoved = false;
			m_SoundPlayedSpec = false;
			m_SpecHasLastPos = false;
			m_SpecNotifyMovedRemove = false;
		}

		if((m_SpecNotifyMoved || m_SpecNotifyAnim > 0.0f) && g_Config.m_RcTextOnMoveInSpec)
		{
			const float AnimSpeed = 0.1f;
			if(m_SpecNotifyMoved && !m_SpecNotifyMovedRemove)
				m_SpecNotifyAnim += Client()->RenderFrameTime() / AnimSpeed;
			else
				m_SpecNotifyAnim -= Client()->RenderFrameTime() / AnimSpeed * 2.0f;
			m_SpecNotifyAnim = std::clamp(m_SpecNotifyAnim, 0.0f, 1.0f);

			if(m_SpecNotifyAnim > 0.0f)
			{
				float T = m_SpecNotifyAnim;
				float Phase = (T < 0.5f) ? (2.0f * T * T) : (1.0f - std::pow(-2.0f * T + 2.0f, 2) / 2.0f);

				int m_Height = 300.0f;
				int m_Width = m_Height * (g_Config.m_RcCustomAspectDisable & RcAspectDisable::NOTIFYINSPEC ? Graphics()->ScreenAspectReal() : Graphics()->ScreenAspect());
				Graphics()->MapScreenToSize((float)m_Width, (float)m_Height);
				CUIRect NotifyBox;
				NotifyBox.w = 60.0f;
				NotifyBox.h = 12.0f;
				NotifyBox.x = (m_Width - NotifyBox.w) / 100.0f * g_Config.m_RcTextOnMoveInSpecPosX;
				NotifyBox.y = (m_Height - NotifyBox.h) / 100.0f * g_Config.m_RcTextOnMoveInSpecPosY;
				NotifyBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f * Phase), IGraphics::CORNER_ALL, 5.0f);
				NotifyBox.Margin(1.0f, &NotifyBox);
				TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, Phase));
				Ui()->DoLabel(&NotifyBox, "Moved in game", 5.0f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
	}
}