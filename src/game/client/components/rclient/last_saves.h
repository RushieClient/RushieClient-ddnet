#ifndef GAME_CLIENT_COMPONENTS_RCLIENT_LASTSAVES_H
#define GAME_CLIENT_COMPONENTS_RCLIENT_LASTSAVES_H
#include "game/client/component.h"

class CLastSaves : public CComponent
{
	int64_t m_MsgTime = 0;
	bool m_NeedSendMsg = false;
	int m_LookNextServerMsg = 0;
	int m_SavesMapCount = 0;
	float m_AppearAnim = 0.0f;
	int64_t m_DisappearTime = 0;

public:
	CLastSaves();
	int Sizeof() const override { return sizeof(*this); }
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnReset() override;
	void OnRender() override;
};

#endif //GAME_CLIENT_COMPONENTS_RCLIENT_LASTSAVES_H
