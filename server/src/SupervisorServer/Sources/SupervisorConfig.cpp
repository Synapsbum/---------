#include "r3dPCH.h"
#include "r3d.h"
#include "r3dNetwork.h"

#include "SupervisorConfig.h"
#include "../../MasterServer/Sources/NetPacketsServerBrowser.h"

CSupervisorConfig* gSupervisorConfig;

CSupervisorConfig::CSupervisorConfig()
{
  const char* configFile = "SupervisorServer.cfg";
  const char* group      = "SupervisorServer";

  if(_access(configFile, 4) != 0) {
    r3dError("can't open config file %s", configFile);
  }

  masterPort_  = r3dReadCFG_I(configFile, group, "masterPort", SBNET_MASTER_PORT);
  masterIp_    = r3dReadCFG_S(configFile, group, "masterIp", "");
  
  serverGroup_ = r3dReadCFG_I(configFile, group, "serverGroup", GBNET_REGION_Unknown);
  serverType_  = r3dReadCFG_I(configFile, group, "serverType", SBNET_SERVER_Gameworld);
  serverName_  = r3dReadCFG_S(configFile, group, "serverName", "");
  mapId_       = 0xFF; //r3dReadCFG_I(configFile, group, "mapId", GBGameInfo::MAPID_WZ_Colorado);
  
  // override serverName_ with our machine name
  char  sname[256] = {0};
  DWORD ssize = sizeof(sname);
  ::GetComputerName(sname, &ssize);
  serverName_ = sname;

  maxPlayers_  = r3dReadCFG_I(configFile, group, "maxPlayers", 1024);
  maxGames_    = r3dReadCFG_I(configFile, group, "maxGames", 32);
  portStart_   = r3dReadCFG_I(configFile, group, "portStart", SBNET_GAME_PORT);
  gameServerExe_ = r3dReadCFG_S(configFile, group, "gameServerExe", "WZ_GameServer.exe");
  externalIpStr_ = r3dReadCFG_S(configFile, group, "externalIp", "");
  externalIpAddr_= 0;
  
  // enable upload logs by default, it can be disabled by setting it to 0
  uploadLogs_ = r3dReadCFG_I(configFile, group, "uploadLogs", 1);
  
  #define CHECK_I(xx) if(xx == 0)  r3dError("missing %s value", #xx);
  #define CHECK_S(xx) if(xx == "") r3dError("missing %s value", #xx);
  CHECK_I(masterPort_);
  CHECK_S(masterIp_);

  CHECK_I(serverGroup_);
  CHECK_S(serverName_);
  CHECK_I(maxPlayers_);
  CHECK_I(maxGames_);
  CHECK_I(portStart_);
  CHECK_S(gameServerExe_);
  #undef CHECK_I
  #undef CHECK_S
  
  if(_access(gameServerExe_.c_str(), 4) != 0) {
    r3dError("can't access game server '%s'\n", gameServerExe_.c_str());
  }
  
  ParseExternalIpAddr();
  
  return;
}

void CSupervisorConfig::ParseExternalIpAddr()
{
  // call WSAStartup()
  WORD versionRequested = MAKEWORD(2, 2);
  WSADATA wsaData;
  WSAStartup(versionRequested, &wsaData);

  if(externalIpStr_.length() == 0) {
    externalIpAddr_ = 0;
    return;
  }
  
  const char* ipname = externalIpStr_.c_str();
#pragma warning( disable : 4996)
  struct hostent* hostEntry = gethostbyname(ipname);
  if(hostEntry == NULL || hostEntry->h_addrtype != AF_INET) 
  {
#pragma warning( disable : 4996)
    unsigned long host = inet_addr(ipname);
    if(host == INADDR_NONE) {
      r3dError("can not parse external ip address %s\n", ipname);
    }

    externalIpAddr_ = host;
    return;
  }
  
  DWORD* ip2 = (DWORD*)hostEntry->h_addr_list[1];
  if(ip2 != NULL) 
    r3dError("external ip %s resolved to multiple IP addresses\n", ipname);
    
  externalIpAddr_ = *(DWORD*)hostEntry->h_addr_list[0];
#pragma warning( disable : 4996)
  r3dOutToLog("External Ip: %s\n", inet_ntoa(*(in_addr*)&externalIpAddr_));
  return;
}
