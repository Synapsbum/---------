#pragma once

#include "MasterServer.h"
#include "MasterGameServer.h"

#include "../../ServerNetPackets/NetPacketsMaster.h"

#include "Backend\ServerUserProfile.h"
using namespace NetPacketsMaster;

class CMasterUserServer : public r3dNetCallback
{
public:
	// peer-status array for each peer
	enum EPeerStatus
	{
		PEER_Free,
		PEER_Waiting,
		PEER_Connected,
		PEER_InLobby,
	};
	struct peer_s
	{
		EPeerStatus	status;
		DWORD		peerUniqueId;	// index that is unique per each peer, 16bits: peerId, 16bits: curPeerUniqueId_
		DWORD		PeerID;

		float		connectTime;
		float		lastReqTime;

		// user id and it profile
		DWORD		CustomerID;
		DWORD		SessionID;
		volatile DWORD haveProfile;
		CServerUserProfile profile_;
		HANDLE	getProfileH;

		// for lobby system
		int LobbyId;

		peer_s()
		{
			status = PEER_Free;
			CustomerID = 0;
			SessionID = 0;
			haveProfile = 0;
			getProfileH = NULL;

			LobbyId = 0;
		}

		void clear()
		{
			status = PEER_Free;
			CustomerID = 0;
			SessionID = 0;
			haveProfile = 0;
			getProfileH = NULL;

			LobbyId = 0;
		}
	};
	peer_s*		peers_;
	int		MAX_PEERS_COUNT;
	int		numConnectedPeers_;
	int		maxConnectedPeers_;
	DWORD		curPeerUniqueId_;	// counter for unique peer checking

	void		DisconnectCheatPeer(DWORD peerId, const char* message, ...);

	// callbacks from r3dNetwork
	void		OnNetPeerConnected(DWORD peerId);
	void		OnNetPeerDisconnected(DWORD peerId);
	void		OnNetData(DWORD peerId, const r3dNetPacketHeader* packetData, int packetSize);

	bool		Validate(const GBPKT_C2M_SetSession_s& n) { return true; }
	void		OnGBPKT_C2M_SetSession(DWORD peerId, const GBPKT_C2M_SetSession_s& n);

	bool		Validate(const GBPKT_C2M_RefreshList_s& n) { return true; }
	bool		Validate(const GBPKT_C2M_JoinGameReq_s& n);
	bool		Validate(const GBPKT_C2M_QuickGameReq_s& n) { return true; }
	bool		Validate(const GBPKT_C2M_MyServerInfoReq_s& n) { return true; }
	bool		Validate(const GBPKT_C2M_MyServerKickPlayer_s& n) { return true; };
	bool		Validate(const GBPKT_C2M_MyServerSetParams_s& n);

	bool		Validate(const GBPKT_C2M_ReportInjection_s& n) { return true; };

	void		OnGBPKT_C2M_RefreshList(DWORD peerId, const GBPKT_C2M_RefreshList_s& n);
	void		OnGBPKT_C2M_JoinGameReq(DWORD peerId, const GBPKT_C2M_JoinGameReq_s& n);
	void		OnGBPKT_C2M_QuickGameReq(DWORD peerId, const GBPKT_C2M_QuickGameReq_s& n);
	void		OnGBPKT_C2M_MyServerInfoReq(DWORD peerId, const GBPKT_C2M_MyServerInfoReq_s& n);
	void		OnGBPKT_C2M_MyServerKickPlayer(DWORD peerId, const GBPKT_C2M_MyServerKickPlayer_s& n);
	void		OnGBPKT_C2M_MyServerSetParams(DWORD peerId, const GBPKT_C2M_MyServerSetParams_s& n);

	void		OnGBPKT_C2M_ReportInjection(DWORD peerId, const GBPKT_C2M_ReportInjection_s& n);

	bool		IsGameFiltered(const GBPKT_C2M_RefreshList_s& n, const GBGameInfo& ginfo, const char* pwd, int curPlayers);

	void		DoJoinGame(CServerG* game, DWORD PeerID, const char* pwd, GBPKT_M2C_JoinGameAns_s& ans);

	void		PrintStats();

public:
	CMasterUserServer();
	~CMasterUserServer();

	void		Start(int port, int in_maxPeerCount);
	void		Tick();
	void		Stop();
};

extern	CMasterUserServer gMasterUserServer;
