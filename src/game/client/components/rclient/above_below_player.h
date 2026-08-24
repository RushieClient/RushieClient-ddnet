#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_ABOVE_BELOW_PLAYER_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_ABOVE_BELOW_PLAYER_H
#include "game/client/component.h"

class CAboveBelowPlayer : public CComponent
{
	const float m_HideAnim = -0.25f;
	float m_AboveAnim = 0.0f;
	float m_SameAnim = 0.0f;
	float m_BelowAnim = 0.0f;
	bool m_PlayerAbove = false;
	bool m_PlayerSame = false;
	bool m_PlayerBelow = false;
public:
	CAboveBelowPlayer();
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;
};

#endif //GAME_CLIENT_COMPONENTS_RCLIENT_ABOVE_BELOW_PLAYER_H
