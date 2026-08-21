#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_RECHARGEBARS_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_RECHARGEBARS_H
#include <game/client/component.h>

class CRechargeBars : public CComponent
{
	void RenderRechargeBar(int ClientId);
	void RenderRechargeBarPos(float x, float y, float Width, float Height, float Progress, float Alpha = 1.0f);
	bool IsPlayerInfoAvailable(int ClientId) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
