#pragma once

#include "../../ServerNetPackets/NetPacketsGameInfo.h"

class CMasterServerConfig
{
  public:
	#define MASTERSERVER_DEV_ID 200

	int		masterPort_;
	int		clientPort_;
	int		serverId_;
	int		masterCCU_;	// max number of connected peers
	size_t		minSupersToStartGame_;

	//
	// permanent games groups
	//
	struct permGame_s
	{
	  GBGameInfo	ginfo;
	  
	  permGame_s()
	  {
	  }
	};
	permGame_s	permGames_[4096];
	int		numPermGames_;
	
	struct GameList_s
	{
	  GBGameInfo	ginfo;
	  char		pwd[16];
	  DWORD		OwnerCustomerID;
	  DWORD		AdminKey;
	  
	  int		RentHours;
	  int		WorkHours;
	};
	std::vector<GameList_s> GameList_;
	float		nextGameServerListCheck_;

	typedef stdext::hash_map<DWORD, GameList_s*> TRentedGamesList;
	TRentedGamesList rentByGameServerId_;
	
	// number of games per region
	int		numGamesHosted[4];
	int		numGamesRented[4];
	int		numStrongholdsRented[4];
	void		OnGameListUpdated();

	void		LoadConfig();

	void		Temp_Load_WarZGames();

	void		LoadPermGamesConfig();
	void		 ParsePermamentGame(int gameServerId, const char* name, const char* map, const char* data);
	void		 AddPermanentGame(int gameServerId, const GBGameInfo& ginfo, EGBGameRegion region);
	
	GameList_s*	GetRentedGameInfo(DWORD gameServerId);
	
  public:
	CMasterServerConfig();
};
extern CMasterServerConfig* gServerConfig;
