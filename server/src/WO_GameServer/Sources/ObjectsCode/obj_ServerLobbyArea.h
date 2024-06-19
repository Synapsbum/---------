#pragma once

#include "GameCommon.h"

class obj_ServerLobbyArea : public GameObject
{
	DECLARE_CLASS(obj_ServerLobbyArea, GameObject)

public:
	float		useRadius;

public:
	obj_ServerLobbyArea();
	virtual ~obj_ServerLobbyArea();

	virtual BOOL	OnCreate();
	virtual	void	ReadSerializedData(pugi::xml_node& node);
};

class LobbyAreaMgr
{
public:
	enum { MAX_LOBBY_AREAS = 32 }; // 32 should be more than enough, if not, will redo into vector
	obj_ServerLobbyArea* lobbyAreas_[MAX_LOBBY_AREAS];
	int		numLobbyAreas_;

	void RegisterLobbyArea(obj_ServerLobbyArea* lbarea) 
	{
		r3d_assert(numLobbyAreas_ < MAX_LOBBY_AREAS);
		lobbyAreas_[numLobbyAreas_++] = lbarea;
	}

public:
	LobbyAreaMgr() { numLobbyAreas_ = 0; }
	~LobbyAreaMgr() {}
};

extern	LobbyAreaMgr gLobbyAreaMngr;
