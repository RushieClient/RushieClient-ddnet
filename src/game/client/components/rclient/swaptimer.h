#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_SWAPTIMER_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_SWAPTIMER_H
#include "game/client/component.h"

class CSwapTimer : public CComponent
{
	struct SSwapList
	{
		int m_FromClientId;
		int m_ToClientId;
		int m_SwapTick;
	};
	std::vector<SSwapList> m_vSwapList;

	void AddNewSwapEntry(int FromClientId, int ToClientId);
	void RemoveSwapEntrySwapped(int ToClientId, int FromClientId);
	void RemoveSwapEntryId(int FromClientId, int ToClientId);
	void RemoveSwapEntryIdAll(int ClientId);

	int FindClientId(const char *pName) const;

public:
	CSwapTimer();
	int Sizeof() const override { return sizeof(*this); }
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnReset() override;
	void OnRender() override;

	void GameClientMessage(int MsgType, void *pRawMsg, bool Dummy);
};

#endif // GAME_CLIENT_COMPONENTS_RCLIENT_SWAPTIMER_H
