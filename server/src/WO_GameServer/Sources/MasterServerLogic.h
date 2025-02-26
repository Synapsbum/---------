#pragma once

#include "r3dNetwork.h"
#include "multiplayer/P2PMessages.h"

#include "../../ServerNetPackets/NetPacketsGameInfo.h"

class GameObject;
class obj_ServerPlayer;

class MasterServerLogic : public r3dNetCallback
{
  protected:
	r3dNetwork	g_net;
	bool		disconnected_;

	// r3dNetCallback virtuals
virtual	void		OnNetPeerConnected(DWORD peerId);
virtual	void		OnNetPeerDisconnected(DWORD peerId);
virtual	void		OnNetData(DWORD peerId, const r3dNetPacketHeader* packetData, int packetSize);

  protected:
	DWORD		gameId_;
	
  protected:
	typedef bool (MasterServerLogic::*fn_wait)();
	int		WaitFunc(fn_wait fn, float timeout, const char* msg);
	
	// wait functions
	bool		wait_IsConnected() {
	  return net_->IsConnected();
	}
	
  public:
	bool		IsMasterDisconnected() const {
	  return disconnected_;
	}
	
	bool		gotWeaponUpdate_;
	
	DWORD		kickReqCharID_;	// CharID of requested kick
	
	int		shuttingDown_;	// 1: master server shutdown, 2: supervisor shutdown
	float		shutdownLeft_;
  
  public:
	MasterServerLogic();
	virtual ~MasterServerLogic();

	void		Init(DWORD gameId);
	int		Connect(const char* host, int port, int listen_port);
	void		Disconnect();
	
	void		RegisterGame();
	void		FinishGame();
	void		CloseGame();
	void		SetGameBRinfo(BYTE status);//AlexRedd:: BR mode

	void		Tick();
	
	void		RequestDataUpdate();
	void		AddPlayer(int playerIdx, DWORD CustomerID, const wiCharDataFull* loadout);
	void		RemovePlayer(int playerIdx);
};

extern	MasterServerLogic	gMasterServerLogic;
