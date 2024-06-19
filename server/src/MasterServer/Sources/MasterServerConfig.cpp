#include "r3dPCH.h"
#include "r3d.h"

#include "MasterServerConfig.h"

	CMasterServerConfig* gServerConfig = NULL;

static const char* configFile = "MasterServer.cfg";

CMasterServerConfig::CMasterServerConfig()
{
  const char* group      = "MasterServer";

  if(_access(configFile, 0) != 0) {
    r3dError("can't open config file %s\n", configFile);
  }

  masterPort_  = r3dReadCFG_I(configFile, group, "masterPort", SBNET_MASTER_PORT);
  clientPort_  = r3dReadCFG_I(configFile, group, "clientPort", GBNET_CLIENT_PORT);
  masterCCU_   = r3dReadCFG_I(configFile, group, "masterCCU",  3000);

  #define CHECK_I(xx) if(xx == 0)  r3dError("missing %s value in %s", #xx, configFile);
  #define CHECK_S(xx) if(xx == "") r3dError("missing %s value in %s", #xx, configFile);
  CHECK_I(masterPort_);
  CHECK_I(clientPort_);
  #undef CHECK_I
  #undef CHECK_S

  serverId_    = r3dReadCFG_I(configFile, group, "serverId", 0);
  if(serverId_ == 0)
  {
	MessageBox(NULL, "you must define serverId in MasterServer.cfg", "", MB_OK);
	r3dError("no serverId");
  }
  if(serverId_ > 255 || serverId_ < 1)
  {
	MessageBox(NULL, "bad serverId", "", MB_OK);
	r3dError("bad serverId");
  }

  minSupersToStartGame_ = r3dReadCFG_I(configFile, group, "minServers", 10);
  
  LoadConfig();
  
  // give time to spawn our hosted games (except for dev server)
  nextGameServerListCheck_ = r3dGetTime() + 5.0f;
  if(serverId_ >= MASTERSERVER_DEV_ID) nextGameServerListCheck_ = r3dGetTime() + 5.0f;
  
  return;
}

void CMasterServerConfig::LoadConfig()
{
  r3dCloseCFG_Cur();
  
  numPermGames_ = 0;

  LoadPermGamesConfig();
  Temp_Load_WarZGames();
  
  OnGameListUpdated();
}

void CMasterServerConfig::Temp_Load_WarZGames()
{
  char group[128];
  sprintf(group, "WarZGames");

  int numGames    = r3dReadCFG_I(configFile, group, "numGames", 0);
  int numPVEGames    = r3dReadCFG_I(configFile, group, "numPVEGames", 0);
  int numPVPGames    = r3dReadCFG_I(configFile, group, "numPVPGames", 0);
  int maxPlayers  = r3dReadCFG_I(configFile, group, "maxPlayers", 32);
  int maxPlayersBR  = r3dReadCFG_I(configFile, group, "maxPlayersBR", 10);
  int numCliffGames = r3dReadCFG_I(configFile, group, "numCliffGames", 0);
  int numTrialGames = r3dReadCFG_I(configFile, group, "numTrialGames", 0);
  int numPremiumGames = r3dReadCFG_I(configFile, group, "numPremiumGames", 0);
  int numVeteranGames = r3dReadCFG_I(configFile, group, "numVeteranGames", 0);
  int numPTEGamesColorado = r3dReadCFG_I(configFile, group, "numPTEGames", 0);
  //int numBambiCaliWood = r3dReadCFG_I(configFile, group, "numBambiCaliWood", 0);
  int numPTEGamesStrongholds = r3dReadCFG_I(configFile, group, "numPTEGamesStronghold", 0);
  int numAircraftCarrier = r3dReadCFG_I(configFile, group, "numAircraftCarrier", 0);
  int numAircraftCarrierSNPMode = r3dReadCFG_I(configFile, group, "numAircraftCarrierSNPMode", 0);
  int numAircraftCarrierNoSNPMode = r3dReadCFG_I(configFile, group, "numAircraftCarrierNoSNPMode", 0);
  int numTradeZone = r3dReadCFG_I(configFile, group, "numTradeZone", 0);
  int numNoDropAir = r3dReadCFG_I(configFile, group, "numNoDropAir", 0);
  int numNoDropWarehouse = r3dReadCFG_I(configFile, group, "numNoDropWarehouse", 0);
#ifdef ENABLE_BATTLE_ROYALE
  int numBRmap = r3dReadCFG_I(configFile, group, "numBRmap", 0);
#endif //ENABLE_BATTLE_ROYALE
  int numClearviewPublic = r3dReadCFG_I(configFile, group, "numClearviewPublic", 0);
  int numNevadaPublic = r3dReadCFG_I(configFile, group, "numNevadaPublic", 0);
  int numRockyFordPublic = r3dReadCFG_I(configFile, group, "numRockyFordPublic", 0);
  int numAircraftPublic = r3dReadCFG_I(configFile, group, "numAircraftPublic", 0);
  int numWarehousePublic = r3dReadCFG_I(configFile, group, "numWarehousePublic", 0);


  int numPTEPlayersColorado = 200;
  int numPTEPlayersCali = 100;
  int numPTEPlayersStrongholds = 50;
  
  for(int i=0; i<numGames; i++)
  {
    GBGameInfo ginfo;
    ginfo.mapId      = GBGameInfo::MAPID_WZ_Colorado;
    ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
					   GBGameInfo::SFLAGS_CrossHair | 
					   GBGameInfo::SFLAGS_Tracers | 
					   GBGameInfo::SFLAGS_TrialsAllowed;
    ginfo.maxPlayers = maxPlayers;
    ginfo.channel    = 2; // official server	

    sprintf(ginfo.name, "WZ Server - %03d", i + 1);
    AddPermanentGame(10000 + i, ginfo, GBNET_REGION_US_West);
  }

  for(int i=0; i<numPVEGames; i++)
  {
    GBGameInfo ginfo;
    ginfo.mapId      = GBGameInfo::MAPID_WZ_Colorado;
    ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
					   GBGameInfo::SFLAGS_CrossHair | 
					   GBGameInfo::SFLAGS_Tracers | 
					   GBGameInfo::SFLAGS_TrialsAllowed;
    ginfo.maxPlayers = maxPlayers;
    ginfo.channel    = 2; // official server

    sprintf(ginfo.name, "WZ Server PVE - %03d", i + 1);
    AddPermanentGame(11000 + i, ginfo, GBNET_REGION_US_West);
  }

  for(int i=0; i<numPVPGames; i++)
  {
    GBGameInfo ginfo;
    ginfo.mapId      = GBGameInfo::MAPID_WZ_Colorado;
    ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
					   GBGameInfo::SFLAGS_CrossHair | 
					   GBGameInfo::SFLAGS_Tracers | 
					   GBGameInfo::SFLAGS_TrialsAllowed;
    ginfo.maxPlayers = maxPlayers;
    ginfo.channel    = 2; // official server
	ginfo.gameTimeLimit = 10; // X hours limit

    sprintf(ginfo.name, "WZ Server PVP - %03d", i + 1);
    AddPermanentGame(11500 + i, ginfo, GBNET_REGION_US_West);
  }

  for(int i=0; i<numTrialGames; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Colorado;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel	   = 1; // trial server

	  sprintf(ginfo.name, "WZ Trial Server - %03d", i + 1);
	  AddPermanentGame(12000 + i, ginfo, GBNET_REGION_US_West);
  }

  for(int i=0; i<numPremiumGames; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Colorado;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel	   = 4; // premium server

	  sprintf(ginfo.name, "WZ Premium Server - %03d", i + 1);
	  AddPermanentGame(14000 + i, ginfo, GBNET_REGION_US_West);
  }

  for(int i=0; i<numPTEGamesColorado; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Colorado;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = numPTEPlayersColorado;
	  ginfo.channel	   = 6; 

	  sprintf(ginfo.name, "WZ PTE Colorado - %03d", i + 1);
	  AddPermanentGame(15000 + i, ginfo, GBNET_REGION_US_West);
  }

  /*for(int i=0; i<numBambiCaliWood; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Caliwood;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers |
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = numPTEPlayersCali;
	  ginfo.channel    = 6; // dev event server

	  sprintf(ginfo.name, "BAMBI MODE - %02d", i + 1);
	  AddPermanentGame(15100 + i, ginfo, GBNET_REGION_US_West);
  }*/

  for(int i=0; i<numPTEGamesStrongholds; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Cliffside;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = numPTEPlayersStrongholds;
	  ginfo.channel    = 6; 

	  sprintf(ginfo.name, "WZ PTE Stronghold - %03d", i + 1);
	  AddPermanentGame(15200 + i, ginfo, GBNET_REGION_US_West);
  }

  for(int i=0; i<numVeteranGames; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Colorado;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.gameTimeLimit = 50; // X hours limit
	  ginfo.channel    = 7; // veteran server

	  sprintf(ginfo.name, "WZ Veteran Server - %03d", i + 1);
	  AddPermanentGame(16000 + i, ginfo, GBNET_REGION_US_West);
  }

  // stronghold cliffside games
  for(int i=0; i<numCliffGames; ++i) 
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Cliffside;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = 20;
	  ginfo.channel	   = 5; // strongholds

	  sprintf(ginfo.name, "WZ Cliffside - %03d", i+1);
	  AddPermanentGame(18000+i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_WZ_AircraftCarrier
  for(int i=0; i<numAircraftCarrier; i++)
  {
    GBGameInfo ginfo;
    ginfo.mapId      = GBGameInfo::MAPID_WZ_AircraftCarrier;
    ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
					   GBGameInfo::SFLAGS_CrossHair | 
					   GBGameInfo::SFLAGS_Tracers | 
					   GBGameInfo::SFLAGS_TrialsAllowed;
    ginfo.maxPlayers = maxPlayers;
    ginfo.channel    = 6; // dev event server

    sprintf(ginfo.name, "CLASSIC MODE - %02d", i + 1);
    AddPermanentGame(19000 + i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_WZ_AircraftCarrier Snipers Mode
  for (int i = 0; i<numAircraftCarrierSNPMode; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId = GBGameInfo::MAPID_WZ_AircraftCarrier;
	  ginfo.flags = GBGameInfo::SFLAGS_Nameplates | 
					GBGameInfo::SFLAGS_CrossHair | 
					GBGameInfo::SFLAGS_Tracers | 
					GBGameInfo::SFLAGS_TrialsAllowed | 
					GBGameInfo::SFLAGS_DisableASR;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel = 6;// dev event server

	  sprintf(ginfo.name, "SNIPERS MODE - %02d", i + 1);
	  AddPermanentGame(19500 + i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_WZ_AircraftCarrier No Snipers Mode
  for (int i = 0; i<numAircraftCarrierNoSNPMode; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId = GBGameInfo::MAPID_WZ_AircraftCarrier;
	  ginfo.flags = GBGameInfo::SFLAGS_Nameplates | 
					GBGameInfo::SFLAGS_CrossHair | 
					GBGameInfo::SFLAGS_Tracers | 
					GBGameInfo::SFLAGS_TrialsAllowed | 
					GBGameInfo::SFLAGS_DisableSNP;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel = 6;// dev event server

	  sprintf(ginfo.name, "NO SNIPERS MODE - %02d", i + 1);
	  AddPermanentGame(20000 + i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_WZ_Trade_Map
  for(int i=0; i<numTradeZone; i++)
  {
    GBGameInfo ginfo;
    ginfo.mapId      = GBGameInfo::MAPID_WZ_Trade_Map;
    ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						GBGameInfo::SFLAGS_CrossHair | 
						GBGameInfo::SFLAGS_Tracers | 
						GBGameInfo::SFLAGS_TrialsAllowed;
    ginfo.maxPlayers = 100;
    ginfo.channel    = 7; // trade server

    sprintf(ginfo.name, "WZ Trade Zone");
    AddPermanentGame(21000 + i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_WZ_AircraftCarrier No Drop Mode
  for(int i=0; i<numNoDropAir; i++)
  {
    GBGameInfo ginfo;
    ginfo.mapId      = GBGameInfo::MAPID_WZ_AircraftCarrier;
    ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						GBGameInfo::SFLAGS_CrossHair | 
						GBGameInfo::SFLAGS_Tracers | 
						GBGameInfo::SFLAGS_TrialsAllowed;
    ginfo.maxPlayers = 32;
    ginfo.channel    = 6; // dev event server

    sprintf(ginfo.name, "NO DROP MODE - %03d", i + 1);
    AddPermanentGame(22000 + i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_ZH_Warehouse No Drop Mode
  for(int i=0; i<numNoDropWarehouse; i++)
  {
    GBGameInfo ginfo;
    ginfo.mapId      = GBGameInfo::MAPID_ZH_Warehouse;
    ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						GBGameInfo::SFLAGS_CrossHair | 
						GBGameInfo::SFLAGS_Tracers | 
						GBGameInfo::SFLAGS_TrialsAllowed;
    ginfo.maxPlayers = 32;
    ginfo.channel    = 6; // dev event server

    sprintf(ginfo.name, "NO DROP MODE - %03d", i + 1);
    AddPermanentGame(23000 + i, ginfo, GBNET_REGION_US_West);
  }

#ifdef ENABLE_BATTLE_ROYALE
  //MAPID_WZ_BRmap BR Mode
  for(int i=0; i<numBRmap; i++)
  {
    GBGameInfo ginfo;
    ginfo.mapId      = GBGameInfo::MAPID_WZ_Quarantine/*MAPID_WZ_BRmap*/;
    ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						GBGameInfo::SFLAGS_CrossHair | 
						GBGameInfo::SFLAGS_Tracers | 
						GBGameInfo::SFLAGS_TrialsAllowed;
    ginfo.maxPlayers = maxPlayersBR;
	//ginfo.gameTimeLimit = 5; // 5 hours limit 
    ginfo.channel    = 1; 

    sprintf(ginfo.name, "BR SERVER - %03d", i + 1);
    AddPermanentGame(24000 + i, ginfo, GBNET_REGION_US_West);
  } 
#endif //ENABLE_BATTLE_ROYALE

  //MAPID_WZ_Clearview_V2 public
  for(int i=0; i<numClearviewPublic; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Clearview_V2;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel    = 2; // official server

	  sprintf(ginfo.name, "WZ Server - %03d", i + 1);
	  AddPermanentGame(25000 + i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_WZ_Nevada public
  for(int i=0; i<numNevadaPublic; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_Nevada;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel    = 2; // official server

	  sprintf(ginfo.name, "WZ Server - %03d", i + 1);
	  AddPermanentGame(26000 + i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_WZ_RockyFord public
  for(int i=0; i<numRockyFordPublic; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_RockyFord;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel    = 2; // official server

	  sprintf(ginfo.name, "WZ Server - %03d", i + 1);
	  AddPermanentGame(27000 + i, ginfo, GBNET_REGION_US_West);
  }

   //MAPID_WZ_AircraftCarrier public
  for(int i=0; i<numAircraftPublic; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_WZ_AircraftCarrier;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel    = 2; // official server

	  sprintf(ginfo.name, "WZ Server - %03d", i + 1);
	  AddPermanentGame(28000 + i, ginfo, GBNET_REGION_US_West);
  }

  //MAPID_ZH_Warehouse public
  for(int i=0; i<numWarehousePublic; i++)
  {
	  GBGameInfo ginfo;
	  ginfo.mapId      = GBGameInfo::MAPID_ZH_Warehouse;
	  ginfo.flags      = GBGameInfo::SFLAGS_Nameplates | 
						 GBGameInfo::SFLAGS_CrossHair | 
						 GBGameInfo::SFLAGS_Tracers | 
						 GBGameInfo::SFLAGS_TrialsAllowed;
	  ginfo.maxPlayers = maxPlayers;
	  ginfo.channel    = 2; // official server

	  sprintf(ginfo.name, "WZ Server - %03d", i + 1);
	  AddPermanentGame(29000 + i, ginfo, GBNET_REGION_US_West);
  }  

  return;
}

void CMasterServerConfig::LoadPermGamesConfig()
{
  numPermGames_ = 0;

//#ifdef _DEBUG
//  r3dOutToLog("Permanet games disabled in DEBUG");
//  return;
//#endif
  
  for(int i=0; i<250; i++)
  {
    char group[128];
    sprintf(group, "PermGame%d", i+1);

    char map[512] = "";
    char data[512] = "";
    char name[512];
    r3dscpy(map,  r3dReadCFG_S(configFile, group, "map", ""));
    r3dscpy(data, r3dReadCFG_S(configFile, group, "data", ""));
    r3dscpy(name, r3dReadCFG_S(configFile, group, "name", ""));
    if(name[0] == 0)
      sprintf(name, "PermGame%d", i+1);

    if(*map == 0)
      continue;
    
    ParsePermamentGame(i, name, map, data);
  }

  return;  
}

static int StringToGBMapID(char* str)
{
  if(stricmp(str, "MAPID_WZ_Colorado") == 0)
    return GBGameInfo::MAPID_WZ_Colorado;
  if(stricmp(str, "MAPID_WZ_Cliffside") == 0)
    return GBGameInfo::MAPID_WZ_Cliffside;
  if(stricmp(str, "MAPID_WZ_California") == 0)
    return GBGameInfo::MAPID_WZ_California;
  if(stricmp(str, "MAPID_WZ_Caliwood") == 0)
	  return GBGameInfo::MAPID_WZ_Caliwood;
  if(stricmp(str, "MAPID_WZ_AircraftCarrier") == 0)
	  return GBGameInfo::MAPID_WZ_AircraftCarrier;
  if(stricmp(str, "MAPID_WZ_Trade_Map") == 0)
	  return GBGameInfo::MAPID_WZ_Trade_Map;
  if(stricmp(str, "MAPID_ZH_Warehouse") == 0)
	  return GBGameInfo::MAPID_ZH_Warehouse;
  if(stricmp(str, "MAPID_WZ_BRmap") == 0)
	  return GBGameInfo::MAPID_WZ_BRmap;
  if(stricmp(str, "MAPID_WZ_Clearview_V2") == 0)
	  return GBGameInfo::MAPID_WZ_Clearview_V2;
  if(stricmp(str, "MAPID_WZ_Nevada") == 0)
	  return GBGameInfo::MAPID_WZ_Nevada;
  if(stricmp(str, "MAPID_WZ_RockyFord") == 0)
	  return GBGameInfo::MAPID_WZ_RockyFord;
  if (stricmp(str, "MAPID_WZ_Quarantine") == 0)
	  return GBGameInfo::MAPID_WZ_Quarantine;

  if(stricmp(str, "MAPID_Editor_Particles") == 0)
    return GBGameInfo::MAPID_Editor_Particles;
  if(stricmp(str, "MAPID_ServerTest") == 0)
    return GBGameInfo::MAPID_ServerTest;
    
  r3dError("bad GBMapID %s\n", str);
  return 0;
}

static EGBGameRegion StringToGBRegion(const char* str)
{
  if(stricmp(str, "GBNET_REGION_US_West") == 0)
    return GBNET_REGION_US_West;

  else if(stricmp(str, "GBNET_REGION_US_East") == 0)
    return GBNET_REGION_US_East;

   else if(stricmp(str, "GBNET_REGION_Europe") == 0)
    return GBNET_REGION_Europe;

   else if(stricmp(str, "GBNET_REGION_Russia") == 0)
    return GBNET_REGION_Russia;

   else if(stricmp(str, "GBNET_REGION_SouthAmerica") == 0)
    return GBNET_REGION_SouthAmerica;

  r3dError("bad GBGameRegion %s\n", str);
  return GBNET_REGION_Unknown;
}

void CMasterServerConfig::ParsePermamentGame(int gameServerId, const char* name, const char* map, const char* data)
{
  char mapid[128];
  char maptype[128];
  char region[128];
  int minGames;
  int maxGames;
  if(5 != sscanf(map, "%s %s %s %d %d", mapid, maptype, region, &minGames, &maxGames)) {
    r3dError("bad map format: %s\n", map);
  }

  int maxPlayers;
  int maxPlayersBR;
  int minLevel = 0;
  int maxLevel = 0;
  int channel = 0;
  int gameTimeLimit = 0;
  if(6 != sscanf(data, "%d %d %d %d %d %d", &maxPlayers, &minLevel, &maxLevel, &channel, &gameTimeLimit, &maxPlayersBR)) {
    r3dError("bad data format: %s\n", data);
  }

  GBGameInfo ginfo;
  ginfo.mapId        = StringToGBMapID(mapid);
  ginfo.maxPlayers   = maxPlayers;
  ginfo.flags        = GBGameInfo::SFLAGS_Nameplates | GBGameInfo::SFLAGS_CrossHair | GBGameInfo::SFLAGS_Tracers;
  if(channel == 1)
	  ginfo.flags |= GBGameInfo::SFLAGS_TrialsAllowed;
  ginfo.channel		 = channel; 
  ginfo.gameTimeLimit = gameTimeLimit;
  r3dscpy(ginfo.name, name);

  r3dOutToLog("permgame: ID:%d, %s, %s\n", 
    gameServerId, name, mapid);
  
  EGBGameRegion eregion = StringToGBRegion(region);
  AddPermanentGame(gameServerId, ginfo, eregion);
}

void CMasterServerConfig::AddPermanentGame(int gameServerId, const GBGameInfo& ginfo, EGBGameRegion region)
{
  r3d_assert(numPermGames_ < R3D_ARRAYSIZE(permGames_));
  permGame_s& pg = permGames_[numPermGames_++];

  r3d_assert(gameServerId);
  pg.ginfo = ginfo;
  pg.ginfo.gameServerId = gameServerId;
  pg.ginfo.region       = region;
  
  return;
}

void CMasterServerConfig::OnGameListUpdated()
{
  memset(&numGamesHosted, 0, sizeof(numGamesHosted));
  memset(&numGamesRented, 0, sizeof(numGamesRented));
  memset(&numStrongholdsRented, 0, sizeof(numStrongholdsRented));

  for(int i=0; i<numPermGames_; i++)
  {
    const GBGameInfo& ginfo = permGames_[i].ginfo;

    int regIdx = 0;
    if(ginfo.region == GBNET_REGION_US_West) regIdx = 0;
    else if(ginfo.region == GBNET_REGION_Europe) regIdx = 1;
    else if(ginfo.region == GBNET_REGION_Russia) regIdx = 2;
	else if(ginfo.region == GBNET_REGION_SouthAmerica) regIdx = 3;
    
    numGamesHosted[regIdx]++;
  }
  
  for(size_t i=0; i<GameList_.size(); i++)
  {
    const GBGameInfo& ginfo = GameList_[i].ginfo;

    int regIdx = 0;
    if(ginfo.region == GBNET_REGION_US_West) regIdx = 0;
    else if(ginfo.region == GBNET_REGION_Europe) regIdx = 1;
    else if(ginfo.region == GBNET_REGION_Russia) regIdx = 2;
	else if(ginfo.region == GBNET_REGION_SouthAmerica) regIdx = 3;
    
    if(ginfo.IsGameworld())
      numGamesRented[regIdx]++;
    else
		numGamesHosted[regIdx]++;
      //numStrongholdsRented[regIdx]++;
  }
  
  // create gameinfo<->gameServerId map
  rentByGameServerId_.clear();
  for(size_t i=0; i<GameList_.size(); i++)
  {
    CMasterServerConfig::GameList_s* rg = &gServerConfig->GameList_[i];
    rentByGameServerId_.insert(TRentedGamesList::value_type(rg->ginfo.gameServerId, rg));
  }
  
  return;
}

CMasterServerConfig::GameList_s* CMasterServerConfig::GetRentedGameInfo(DWORD gameServerId)
{
  TRentedGamesList::iterator it = rentByGameServerId_.find(gameServerId);
  if(it == rentByGameServerId_.end())
    return NULL;

  return it->second;
}
