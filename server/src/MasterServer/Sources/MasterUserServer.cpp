#include "r3dPCH.h"
#include "r3d.h"
#include "r3dNetwork.h"
#include <shellapi.h>

#pragma warning(disable: 4065)	// switch statement contains 'default' but no 'case' labels

#include "MasterUserServer.h"
#include "MasterGameServer.h"
#include "NetPacketsServerBrowser.h"

#include "AsyncFuncs.h"

#include "..\..\..\src\EclipseStudio\Sources\Backend\WOBackendAPI.h"

extern char* g_ServerApiKey;

using namespace NetPacketsMaster;

static	r3dNetwork	clientNet;
CMasterUserServer gMasterUserServer;

const float SpamProtection = 0.1f;

static bool IsNullTerminated(const char* data, int size)
{
  for(int i=0; i<size; i++) {
    if(data[i] == 0)
      return true;
  }

  return false;
}

CMasterUserServer::CMasterUserServer()
{
}

CMasterUserServer::~CMasterUserServer()
{
  SAFE_DELETE_ARRAY(peers_);
}

void CMasterUserServer::Start(int port, int in_maxPeerCount)
{
  r3d_assert(in_maxPeerCount);
  MAX_PEERS_COUNT = in_maxPeerCount;
  peers_          = new peer_s[MAX_PEERS_COUNT];

  numConnectedPeers_ = 0;
  maxConnectedPeers_ = 0;
  curPeerUniqueId_   = 0;

  clientNet.Initialize(this, "clientNet");
  if(!clientNet.CreateHost(port, MAX_PEERS_COUNT)) {
    r3dError("CreateHost failed\n");
  }
  
  r3dOutToLog("[USER SERVER] - started at port %d, %d CCU\n", port, MAX_PEERS_COUNT);
}

static bool SupervisorsSortByName(const CServerS* d1, const CServerS* d2)
{
  return strcmp(d1->GetName(), d2->GetName()) < 0;
}

void CMasterUserServer::PrintStats()
{
  const float curTime = r3dGetTime();
  
  static float nextShutLog_ = 0;
  if(gMasterGameServer.shuttingDown_ && curTime > nextShutLog_) {
    nextShutLog_ = curTime + 1.0f;
    r3dOutToLog("SHUTDOWN in %.0f\n", gMasterGameServer.shutdownLeft_);
  }
  
  // dump some useful statistics
  static float nextDebugLog_ = 0;
  static int   outToLogCnt   = 0;
  if(curTime < nextDebugLog_) 
    return;

  nextDebugLog_ = curTime + 10.0f;
  bool bToLog = (++outToLogCnt % 12) == 0; // output to r3dLog every 120 sec

  // calc number of games and CCU
  int numGames     = 0;
  int numGamesH[4] = {0}; // hosted games per region
  int numGamesR[4] = {0}; // renged games per region
  int numHoldsR[4] = {0}; // strongholds per region
  int numCCU       = 0;
  int maxCCU       = 0;
  for(CMasterGameServer::TGamesList::const_iterator it = gMasterGameServer.games_.begin(); 
      it != gMasterGameServer.games_.end();
      ++it)
  {
    const CServerG* game = it->second;
    const GBGameInfo& ginfo = game->getGameInfo();

    numCCU += game->curPlayers_;
    maxCCU += ginfo.maxPlayers;
    numGames++;
    
    int regIdx = 0;
    if(ginfo.region == GBNET_REGION_US_West) regIdx = 0;
    else if(ginfo.region == GBNET_REGION_Europe) regIdx = 1;
    else if(ginfo.region == GBNET_REGION_Russia) regIdx = 2;
	else if(ginfo.region == GBNET_REGION_SouthAmerica) regIdx = 3;
    
    if(!game->getGameInfo().IsRentedGame())
      numGamesH[regIdx]++;
    //else if(game->getGameInfo().IsGameworld())
	else if(game->getGameInfo().IsRentedGame())
      numGamesR[regIdx]++;
    else
      numHoldsR[regIdx]++;
  }

  // find games capacity per region
  int maxGames[4] = {0};
  int maxHolds[4] = {0};
  for(CMasterGameServer::TSupersList::const_iterator it = gMasterGameServer.supers_.begin(); it != gMasterGameServer.supers_.end(); ++it) 
  {
    const CServerS* super = it->second;

    int regIdx = 0;
    if(super->region_ == GBNET_REGION_US_West) regIdx = 0;
    else if(super->region_ == GBNET_REGION_Europe) regIdx = 1;
    else if(super->region_ == GBNET_REGION_Russia) regIdx = 2;
	else if(super->region_ == GBNET_REGION_SouthAmerica) regIdx = 3;
    
    if(super->IsStrongholdServer())
      maxHolds[regIdx] += super->maxGames_;
    else
      maxGames[regIdx] += super->maxGames_;
  }

    
  static int peakCCU = 0;
  if(numCCU > peakCCU) peakCCU = numCCU;

  FILE* f = fopen("MasterServer_ccu.txt", "wt");

  char buf[1024];    
  sprintf(buf, "MSINFO: %d (%d max) peers, %d CCU in %d games, PeakCCU: %d, MaxCCU:%d\n",
    numConnectedPeers_,
    maxConnectedPeers_,
    numCCU,
    numGames,
    peakCCU,
    maxCCU
    ); 
  if(bToLog) r3dOutToLog(buf);
  if(f) fprintf(f, buf);

   // list of spawned/required games per region
  for(int i=0; i<4; i++)
  {
    const static char* regName[4] = { "TH", "EU", "RU", "SA" };
    sprintf(buf, " [%s] Hosted:%d/%d\tRented:%d/%d\tCapacity:%d\tBalance:%+d\n",
      regName[i],
      numGamesH[i], gServerConfig->numGamesHosted[i],
      gServerConfig->GameList_.size(), gServerConfig->numGamesRented[i],
      maxGames[i], 
      maxGames[i] - (gServerConfig->numGamesHosted[i] + gServerConfig->numGamesRented[i])
    );
    if(bToLog) r3dOutToLog(buf);
    if(f) fprintf(f, buf);
  }

  // list of spawned/required games per region
  for(int i=0; i<4; i++)
  {
    const static char* regName[4] = { "TH", "EU", "RU", "SA" };
    sprintf(buf, " [%s] Strongholds\tRented:%d/%d\tCapacity:%d\tBalance:%+d\n",
      regName[i],
      numHoldsR[i], gServerConfig->numStrongholdsRented[i],
      maxHolds[i], 
      maxHolds[i] - (gServerConfig->numStrongholdsRented[i])
    );
    if(bToLog) r3dOutToLog(buf);
    if(f) fprintf(f, buf);
  }

  // list of supervisors
  sprintf(buf, "Supervisors: %d%s, Games:%d|%d\n", 
    gMasterGameServer.supers_.size(),
    gMasterGameServer.supers_.size() < gServerConfig->minSupersToStartGame_ ? "(need more to start)" : "",
    gMasterGameServer.games_.size(), 
    gMasterGameServer.gamesByGameServerId_.size());
  r3dOutToLog(buf);
  if(f) fprintf(f, buf);
  CLOG_INDENT;
  
  static std::vector<const CServerS*> supers;
  supers.clear();
  for(CMasterGameServer::TSupersList::const_iterator it = gMasterGameServer.supers_.begin(); it != gMasterGameServer.supers_.end(); ++it)
    supers.push_back(it->second);
    
  std::sort(supers.begin(), supers.end(), SupervisorsSortByName);
  
  // log supervisors status by region
  int regionsToLog[4] = {GBNET_REGION_US_West, GBNET_REGION_Europe, GBNET_REGION_Russia, GBNET_REGION_SouthAmerica};
  const char* regionsName[4] = {"TH", "EU", "RU", "SA"};
  for(int curRegion=0; curRegion<4; ++curRegion)
  {
	  sprintf(buf, "REGION: %s\n", regionsName[curRegion]);
	  if(bToLog) r3dOutToLog(buf);
	  if(f) fprintf(f, buf);

	  for(size_t i=0; i<supers.size(); ++i)
	  {
		  const CServerS* super = supers[i];
		  if(super->region_ == regionsToLog[curRegion])
		  {
#pragma warning( disable : 4996)
			  sprintf(buf, "%s(%s), games:%d/%d, players:%d/%d %s %s\n", 
				  super->GetName(),
				  inet_ntoa(*(in_addr*)&super->ip_),
				  super->GetExpectedGames(), super->maxGames_,
				  super->GetExpectedPlayers(), super->maxPlayers_,
				  super->IsStrongholdServer() ? "(STRONGHOLD)" : "",
				  super->disabledSlots_ > 0 ? "(HAVE DISABLED SLOTS)" : "");

			  if(bToLog) r3dOutToLog(buf);
			  if(f) fprintf(f, buf);
		  }
	  }
  }

  if(f) fclose(f);
}

void CMasterUserServer::Tick()
{
	net_->Update();
	PrintStats();
}

void CMasterUserServer::Stop()
{
  if(net_)
    net_->Deinitialize();
}

void CMasterUserServer::DisconnectCheatPeer(DWORD peerId, const char* message, ...)
{
	char buf[2048] = { 0 };

	if (message)
	{
		va_list ap;
		va_start(ap, message);
		StringCbVPrintfA(buf, sizeof(buf), message, ap);
		va_end(ap);
	}

	DWORD ip = net_->GetPeerIp(peerId);
	if (message)
	{
		r3dOutToLog("!!! cheat: peer%d[%s], reason: %s\n",
			peerId,
			inet_ntoa(*(in_addr*)&ip),
			buf);
	}

	net_->DisconnectPeer(peerId);

	// fire up disconnect event manually, enet might skip if if other peer disconnect as well
	OnNetPeerDisconnected(peerId);
}

bool CMasterUserServer::Validate(const GBPKT_C2M_JoinGameReq_s& n)
{
  if(!IsNullTerminated(n.pwd, sizeof(n.pwd)))
    return false;

  return true;    
}

bool CMasterUserServer::Validate(const GBPKT_C2M_MyServerSetParams_s& n)
{
  if(!IsNullTerminated(n.pwd, sizeof(n.pwd)))
    return false;
  if(n.gameTimeLimit < 0)
	  return false;

  return true;    
}

void CMasterUserServer::OnNetPeerConnected(DWORD peerId)
{
	peer_s& peer = peers_[peerId];
	r3d_assert(peer.status == PEER_Free);

	curPeerUniqueId_++;
	peer.peerUniqueId = (peerId << 16) | (curPeerUniqueId_ & 0xFFFF);
	peer.PeerID = peerId;
	peer.status = PEER_Waiting;
	peer.connectTime = r3dGetTime();
	peer.lastReqTime = r3dGetTime() - 1.0f; // minor hack to avoid check for 'too many requests'

	numConnectedPeers_++;
	maxConnectedPeers_ = R3D_MAX(maxConnectedPeers_, numConnectedPeers_);

	r3dOutToLog("[USER SERVER] Peer %d Connected waiting for verification...\n", peer.PeerID);

	// send validate packet, so client can check version right now
	GBPKT_C2M_SetSession_s n;
	n.CustomerID = 0;
	n.SessionID = 0;
	n.version = GBNET_VERSION;
	n.key1 = 0;
	net_->SendToPeer(&n, sizeof(n), peerId, true);
}

void CMasterUserServer::OnNetPeerDisconnected(DWORD peerId)
{
	peer_s& peer = peers_[peerId];

	if (peer.status == PEER_InLobby)
	{
		//CServerL* lobby = gMasterGameServer.GetLobbyByLobbyId(peer.LobbyId);
		//if (lobby)
		//{
		//	GBPKT_M2C_LeaveLobbyAns_s ans;
		//	DoLeaveLobby(lobby, peer.PeerID, ans);

		//	UpdateClientLobbys();
		//}
	}

	if (peer.status != PEER_Free)
		numConnectedPeers_--;

	if (peer.haveProfile > 0)
		r3dOutToLog("[USER SERVER] (Peer: %d, CustomerID: %d, SessionID: %d) Disconnected\n", peer.PeerID, peer.profile_.CustomerID, peer.profile_.SessionID);
	else
		r3dOutToLog("[USER SERVER] Unverified Peer %d Disconnected\n", peer.PeerID);

	peer.clear();
}

#define DEFINE_PACKET_HANDLER_MUS(xxx) \
    case xxx: \
    { \
      const xxx##_s& n = *(xxx##_s*)PacketData; \
      if(sizeof(n) != PacketSize) { \
        DisconnectCheatPeer(peerId, "wrong %s size %d vs %d", #xxx, sizeof(n), PacketSize); \
        break; \
      } \
      if(!Validate(n)) { \
        DisconnectCheatPeer(peerId, "invalid %s", #xxx); \
        break; \
      } \
      On##xxx(peerId, n); \
      break; \
    }

void CMasterUserServer::OnNetData(DWORD peerId, const r3dNetPacketHeader* PacketData, int PacketSize)
{
	if (PacketSize < sizeof(r3dNetPacketHeader)) {
		DisconnectCheatPeer(peerId, "too small packet");
		return;
	}

	peer_s& peer = peers_[peerId];

	if (r3dGetTime() < peer.lastReqTime + SpamProtection)
		return;
	else
		peer.lastReqTime = r3dGetTime();

	if (peer.status == PEER_Waiting)
	{
		switch (PacketData->EventID)
		{
			DEFINE_PACKET_HANDLER_MUS(GBPKT_C2M_SetSession);

		default:
			DisconnectCheatPeer(peerId, "invalid packet id");
			break;
		}
	}
	else
	{
	  switch(PacketData->EventID) 
	  {
		DEFINE_PACKET_HANDLER_MUS(GBPKT_C2M_RefreshList)
		DEFINE_PACKET_HANDLER_MUS(GBPKT_C2M_JoinGameReq);
		DEFINE_PACKET_HANDLER_MUS(GBPKT_C2M_QuickGameReq);
		DEFINE_PACKET_HANDLER_MUS(GBPKT_C2M_MyServerInfoReq);
		DEFINE_PACKET_HANDLER_MUS(GBPKT_C2M_MyServerKickPlayer);
		DEFINE_PACKET_HANDLER_MUS(GBPKT_C2M_MyServerSetParams);
		DEFINE_PACKET_HANDLER_MUS(GBPKT_C2M_ReportInjection);

	  default:
		  DisconnectCheatPeer(peerId, "invalid packet id");
		  break;
	  }
	}
}

void CMasterUserServer::OnGBPKT_C2M_SetSession(DWORD peerId, const GBPKT_C2M_SetSession_s& n)
{
	peer_s& peer = peers_[peerId];

	if (peer.haveProfile != 1)
	{
		peer.CustomerID = n.CustomerID;
		peer.SessionID = n.SessionID;

		// start thread for profile loading
		peer.getProfileH = CreateThread(NULL, 0, GetProfileDataThread, &peer, 0, NULL);
	}
	else
		r3dOutToLog("Peer already has been verificated!\n");
}

bool CMasterUserServer::IsGameFiltered(const GBPKT_C2M_RefreshList_s& n, const GBGameInfo& ginfo, const char* pwd, int curPlayers)
{
	if(!ginfo.isSameChannel(n.browseChannel))
		return true;

	if(n.enable_options)
	{
		if(!((n.tracers2 && (ginfo.flags & GBGameInfo::SFLAGS_Tracers)) ||
			(!n.tracers2 && !(ginfo.flags & GBGameInfo::SFLAGS_Tracers))))
			return true;

		if(!((n.crosshair2 && (ginfo.flags & GBGameInfo::SFLAGS_CrossHair)) ||
			(!n.crosshair2 && !(ginfo.flags & GBGameInfo::SFLAGS_CrossHair))))
			return true;

		if(!((n.nameplates2 && (ginfo.flags & GBGameInfo::SFLAGS_Nameplates)) ||
			(!n.nameplates2 && !(ginfo.flags & GBGameInfo::SFLAGS_Nameplates))))
			return true;

		if(!((n.password && (pwd[0])) ||
			(!n.password && !(pwd[0]))))
			return true;
		if(ginfo.gameTimeLimit > n.timeLimit)
			return true;
	}
	
	if(n.hideempty && curPlayers == 0)
		return true;

	if(n.hidefull && curPlayers >= ginfo.maxPlayers)
		return true;
		
	return false;
}

void CMasterUserServer::OnGBPKT_C2M_RefreshList(DWORD peerId, const GBPKT_C2M_RefreshList_s& n)
{
  //r3dOutToLog("sending session list to client%d\n", peerId);

  { // start list
    CREATE_PACKET(GBPKT_M2C_StartGamesList, n);
    net_->SendToPeer(&n, sizeof(n), peerId);
  }
  
  // send supervisors data
  for(CMasterGameServer::TSupersList::iterator it = gMasterGameServer.supers_.begin(); it != gMasterGameServer.supers_.end(); ++it)
  {
    const CServerS* super = it->second;
    
    GBPKT_M2C_SupervisorData_s n;
    n.ID     = WORD(super->id_);
    n.ip     = super->ip_;
    n.region = super->region_;
    net_->SendToPeer(&n, sizeof(n), peerId);
  }
  
  DWORD numFiltered = 0;

  // send games
  for(CMasterGameServer::TGamesList::iterator it = gMasterGameServer.games_.begin(); it != gMasterGameServer.games_.end(); ++it) 
  {
    const CServerG* game = it->second;
    if(game->isValid() == false)
      continue;
      
    if(game->isFinished())
      continue;
      
    int curPlayers = game->curPlayers_ + game->GetJoiningPlayers();
      
    // filter our region and games based on filters
    if(n.region != game->info_.ginfo.region) {
      continue;
    }
    if(IsGameFiltered(n, game->info_.ginfo, game->info_.pwd, curPlayers)) {
      numFiltered++;
      continue;
    }
      
    CREATE_PACKET(GBPKT_M2C_GameData, n);
    n.superId    = (game->id_ >> 16);
    n.info       = game->info_.ginfo;
	n.status     = game->isBRStatus_;//AlexRedd:: BR mode	
    n.curPlayers = (WORD)curPlayers;		
    // override passworded flag, as it can be changed right now for every type of servers
    n.info.flags &= ~GBGameInfo::SFLAGS_Passworded;
    if(game->info_.pwd[0])
      n.info.flags |= GBGameInfo::SFLAGS_Passworded;	

    net_->SendToPeer(&n, sizeof(n), peerId);
  }
  
  // send not started rented games
  for(size_t i=0; i<gServerConfig->GameList_.size(); i++)
  {
    const CMasterServerConfig::GameList_s* rg = &gServerConfig->GameList_[i];

	//AlexRedd:: [FIX] Rent servers
    // filter games for dev servers
    /*if(gMasterGameServer.masterServerId_ == MASTERSERVER_DEV_ID) {
      break;
    }
    if(gMasterGameServer.masterServerId_ == MASTERSERVER_DEV_ID + 1) {
      if(rg->ginfo.gameServerId != 102386 && rg->ginfo.gameServerId != 133613)	// "azzy server", owner 1000003
        continue;
    }*/

    // skip started games
    if(gMasterGameServer.GetGameByGameServerId(rg->ginfo.gameServerId))
      continue;

    // filter our region and games based on filters
    if(n.region != rg->ginfo.region) {
      continue;
    }
    if(IsGameFiltered(n, rg->ginfo, rg->pwd, 0)) {
      numFiltered++;
      continue;
    }

    CREATE_PACKET(GBPKT_M2C_GameData, n);
    n.superId    = 0;
    n.info       = rg->ginfo;
    n.status     = 0;
    n.curPlayers = 0;
    // override passworded flag, as it can be changed right now for every type of servers
    n.info.flags &= ~GBGameInfo::SFLAGS_Passworded;
    if(rg->pwd[0])
      n.info.flags |= GBGameInfo::SFLAGS_Passworded;

    net_->SendToPeer(&n, sizeof(n), peerId);
  }

  { // end list
    CREATE_PACKET(GBPKT_M2C_EndGamesList, n);
    n.numFiltered = numFiltered;
    net_->SendToPeer(&n, sizeof(n), peerId);
  }
}

void CMasterUserServer::OnGBPKT_C2M_JoinGameReq(DWORD peerId, const GBPKT_C2M_JoinGameReq_s& n)
{
  GBPKT_M2C_JoinGameAns_s ans;
  do 
  {
    if(gMasterGameServer.IsServerStartingUp())
    {
      ans.result = GBPKT_M2C_JoinGameAns_s::rMasterStarting;
      break;
    }

    CServerG* game = NULL;
    int gameStatus = gMasterGameServer.IsGameServerIdStarted(n.gameServerId, &game);
    if(game) {
		DoJoinGame(game, peerId, n.pwd, ans);
      break;
    }
    
    // check if this is rented game
    const CMasterServerConfig::GameList_s* rg = gServerConfig->GetRentedGameInfo(n.gameServerId);
    if(rg == 0) {
      ans.result = GBPKT_M2C_JoinGameAns_s::rGameNotFound;
      break;
    }

    // check if user can enter passworded game to prevent game starting for user with invalid password
    // do not check password for GM, we allow GMs to enter any game
	if (rg->pwd[0] && !peers_[peerId].profile_.ProfileData.isDevAccount) {
      if(strcmp(rg->pwd, n.pwd) != 0) {
        ans.result = GBPKT_M2C_JoinGameAns_s::rWrongPassword;
        break;
      }
    }
    
    // check if we that game in starting status 
    if(gameStatus == 2) {
      ans.result = GBPKT_M2C_JoinGameAns_s::rGameStarting;
      break;
    }	

    // spawn new game by user request
    CMSNewGameData ngd(rg->ginfo, rg->pwd, rg->OwnerCustomerID);

    DWORD ip;
    DWORD port;
    __int64 sessionId;
    if(!gMasterGameServer.CreateNewGame(ngd, &ip, &port, &sessionId)) {
      r3dOutToLog("!!! unable to spawn user requested map %d - no free slots at regioin %d\n", ngd.ginfo.mapId, ngd.ginfo.region);
      ans.result = GBPKT_M2C_JoinGameAns_s::rNoGames;
      break;
    }

    ans.result = GBPKT_M2C_JoinGameAns_s::rGameStarting;
    break;
  } while (0);

  r3d_assert(ans.result != GBPKT_M2C_JoinGameAns_s::rUnknown);
  net_->SendToPeer(&ans, sizeof(ans), peerId, true);
}

void CMasterUserServer::OnGBPKT_C2M_QuickGameReq(DWORD peerId, const GBPKT_C2M_QuickGameReq_s& n)
{
  GBPKT_M2C_JoinGameAns_s ans;

  if(gMasterGameServer.IsServerStartingUp())
  {
    ans.result = GBPKT_M2C_JoinGameAns_s::rMasterStarting;
    net_->SendToPeer(&ans, sizeof(ans), peerId, true);
    return;
  }
  
  CServerG* game = gMasterGameServer.GetQuickJoinGame(n.gameMap, (EGBGameRegion)n.region, n.browseChannel, n.playerGameTime);
  // in case some region game wasn't available, repeat search without specifying filter
  if(game == NULL && n.region != GBNET_REGION_Unknown)
  {
    CLOG_INDENT;
    game = gMasterGameServer.GetQuickJoinGame(n.gameMap, GBNET_REGION_Unknown, n.browseChannel, n.playerGameTime);
  }
  
  if(!game) {
    ans.result = GBPKT_M2C_JoinGameAns_s::rGameNotFound;
    net_->SendToPeer(&ans, sizeof(ans), peerId, true);
    return;
  }

  game->AddJoiningPlayer(n.CustomerID);
  
  ans.result    = GBPKT_M2C_JoinGameAns_s::rOk;
  ans.ip        = game->ip_;
  ans.port      = game->info_.port;
  ans.sessionId = game->info_.sessionId;
  net_->SendToPeer(&ans, sizeof(ans), peerId, true);
}

void CMasterUserServer::DoJoinGame(CServerG* game, DWORD PeerID, const char* pwd, GBPKT_M2C_JoinGameAns_s& ans)
{
  r3d_assert(game); 

  if (game->isFull() && peers_[PeerID].profile_.ProfileData.isDevAccount) {
    ans.result = GBPKT_M2C_JoinGameAns_s::rGameFull;	
    return;
  }
#ifdef ENABLE_BATTLE_ROYALE
  //AlexRedd:: BR mode   
  if(game->getGameInfo().IsGameBR() && game->isStarted()) {
    ans.result = GBPKT_M2C_JoinGameAns_s::rGameIsStarted;	
    return;	
  }
#endif //ENABLE_BATTLE_ROYALE

  if(game->isFinished()) {
    ans.result = GBPKT_M2C_JoinGameAns_s::rGameFinished;
    return;
  }

  // do not check password for GM, we allow GMs to enter any game
  if (game->isPassworded() && !peers_[PeerID].profile_.ProfileData.isDevAccount) {
    if(strcmp(game->info_.pwd, pwd) != 0) {
      ans.result = GBPKT_M2C_JoinGameAns_s::rWrongPassword;
      return;
    }
  }
  
  game->AddJoiningPlayer(peers_[PeerID].profile_.CustomerID);

  ans.result    = GBPKT_M2C_JoinGameAns_s::rOk;
  ans.ip        = game->ip_;
  ans.port      = game->info_.port;
  ans.sessionId = game->info_.sessionId;
}

void CMasterUserServer::OnGBPKT_C2M_MyServerInfoReq(DWORD peerId, const GBPKT_C2M_MyServerInfoReq_s& n)
{
  // check if this is rented game
  const CMasterServerConfig::GameList_s* rg = gServerConfig->GetRentedGameInfo(n.gameServerId);
  if(rg == NULL)
  {
    // not setup yet
    CREATE_PACKET(GBPKT_M2C_MyServerInfoAns, n);
    n.status = 1;
    net_->SendToPeer(&n, sizeof(n), peerId);
    return;
  }

  if(rg->AdminKey != n.AdminKey)
  {
    r3dOutToLog("!!!! info bad AdminKey: %d vs %d\n", rg->AdminKey, n.AdminKey);
    CREATE_PACKET(GBPKT_M2C_MyServerInfoAns, n);
    n.status = 0;
    net_->SendToPeer(&n, sizeof(n), peerId);
    return;
  }

  // we can have game as NULL here. it only mean that game isn't spawned. we still need to process this request
  const CServerG* game = gMasterGameServer.GetGameByGameServerId(n.gameServerId);
  if(!game) {
    CREATE_PACKET(GBPKT_M2C_MyServerInfoAns, n);
    n.status = gMasterGameServer.IsServerStartingUp() ? 4 : 2; // starting : offline
    net_->SendToPeer(&n, sizeof(n), peerId);
    return;
  }
  
  // players
  for(int i=0; i<CServerG::MAX_NUM_PLAYERS_MS; i++)
  {
    const SBPKT_G2M_AddPlayer_s& plr = game->playerList_[i].plr;
    if(plr.CharID == 0) 
      continue;

    CREATE_PACKET(GBPKT_M2C_MyServerAddPlayer, n);
    n.CharID     = plr.CharID;
    r3dscpy(n.gamertag, plr.gamertag);
    n.reputation = plr.reputation;
    n.XP         = plr.XP;

    net_->SendToPeer(&n, sizeof(n), peerId);
  }

  // end list 
  {
    CREATE_PACKET(GBPKT_M2C_MyServerInfoAns, n);
    n.status = 3; // online
    net_->SendToPeer(&n, sizeof(n), peerId);
  }
}

void CMasterUserServer::OnGBPKT_C2M_MyServerKickPlayer(DWORD peerId, const GBPKT_C2M_MyServerKickPlayer_s& n)
{
  const CServerG* game = gMasterGameServer.GetGameByGameServerId(n.gameServerId);
  if(!game)
    return;
  
  if(game->AdminKey_ != n.AdminKey || game->AdminKey_ == 0)
  {
    r3dOutToLog("!!!! kick bad AdminKey: %d vs %d\n", game->AdminKey_, n.AdminKey);
    return;
  }
  
  // pass kick request to game
  NetPacketsServerBrowser::SBPKT_M2G_KickPlayer_s n2(game->id_);
  n2.CharID = n.CharID;
  gMasterGameServer.net_->SendToPeer(&n2, sizeof(n2), game->peer_);
}

void CMasterUserServer::OnGBPKT_C2M_MyServerSetParams(DWORD peerId, const GBPKT_C2M_MyServerSetParams_s& n)
{
  CMasterServerConfig::GameList_s* rg = gServerConfig->GetRentedGameInfo(n.gameServerId);
  if(rg == NULL)
    return;

  if(rg->AdminKey != n.AdminKey)
  {
    r3dOutToLog("!!!! setpwd AdminKey: %d vs %d\n", rg->AdminKey, n.AdminKey);
    return;
  }
    
  // set password in game info
  strcpy(rg->pwd, n.pwd);
  rg->ginfo.flags = n.flags;
  rg->ginfo.gameTimeLimit = n.gameTimeLimit;

  // and in actual game if it started
  CServerG* game = gMasterGameServer.GetGameByGameServerId(n.gameServerId);
  if(game)
  {
    r3dscpy(game->info_.pwd, n.pwd);
    game->info_.ginfo.flags = n.flags;
    game->info_.ginfo.gameTimeLimit = n.gameTimeLimit;

    // pass request to game
    NetPacketsServerBrowser::SBPKT_M2G_SetGameFlags_s n2(game->id_);
    n2.flags = n.flags;
    n2.gametimeLimit = n.gameTimeLimit;
    gMasterGameServer.net_->SendToPeer(&n2, sizeof(n2), game->peer_);
  }
}

void CMasterUserServer::OnGBPKT_C2M_ReportInjection(DWORD peerId, const GBPKT_C2M_ReportInjection_s& n)
{
	peer_s& peer = peers_[peerId];

	// convert PVOID to char
	char BaseModule[128];
	sprintf(BaseModule, "%p", n.ModuleBase);

	// API call
	CWOBackendReq req("api_DBG_Injection_Detection.aspx");
	req.AddSessionInfo(peer.CustomerID, peer.SessionID);
	req.AddParam("skey1", g_ServerApiKey);
	req.AddParam("path", n.filename);
	req.AddParam("BaseModule", BaseModule);
	if (!req.Issue())
		r3dOutToLog("api_DBG_Injection_Detection failed: %d", req.resultCode_);

	// Log
	r3dOutToLog("INJECTION DETECTED OF USER %s\n", peer.profile_.username);
	r3dOutToLog("FILENAME: %s\nModuleBase: %p\n", n.filename, n.ModuleBase);
}

