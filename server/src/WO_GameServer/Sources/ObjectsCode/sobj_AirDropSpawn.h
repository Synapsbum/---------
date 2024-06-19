#pragma once

#include "GameCommon.h"

class obj_AirDropSpawn : public GameObject
{
	DECLARE_CLASS(obj_AirDropSpawn, GameObject)

public:
	float		spawnRadius;
	uint32_t	m_AirDropItemID; // used to select which item to spawn

public:
	obj_AirDropSpawn();
	~obj_AirDropSpawn();

	virtual BOOL	OnCreate();
	virtual BOOL	OnDestroy();
	virtual BOOL	Update();
	virtual	void	ReadSerializedData(pugi::xml_node& node);

};
