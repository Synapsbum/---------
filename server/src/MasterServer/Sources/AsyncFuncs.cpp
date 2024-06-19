#include "r3dPCH.h"
#include "r3d.h"
#include "r3dNetwork.h"

#include "AsyncFuncs.h"
#include "MasterUserServer.h"

#include "Backend\ServerUserProfile.h"

extern	__int64 cfg_sessionId;

DWORD WINAPI GetProfileDataThread(void* in_ptr)
{
	CMasterUserServer::peer_s& peer = *(CMasterUserServer::peer_s*)in_ptr;

	CServerUserProfile prof;
	prof.CustomerID = peer.CustomerID;
	prof.SessionID = peer.SessionID;
	if (prof.GetProfile() != 0)
	{
		r3dOutToLog("[USER SERVER] Failed to GetProfile of Peer %d (CustomerID: %d)\n", peer.PeerID, prof.CustomerID);
		peer.haveProfile = 2;
		// Disconnect Peer from the Master Server
		gMasterUserServer.DisconnectCheatPeer(peer.PeerID, "Peer verification failed...");
		return 0;
	}

	// set profile
	peer.profile_ = prof;
	peer.haveProfile = 1;
	peer.getProfileH = NULL;
	peer.status = gMasterUserServer.PEER_Connected;

	r3dOutToLog("[USER SERVER] Peer: %d has been verified as CustomerID: %d and SessionID: %d\n", peer.PeerID, prof.CustomerID, prof.SessionID); CLOG_INDENT;
	return 0;
}