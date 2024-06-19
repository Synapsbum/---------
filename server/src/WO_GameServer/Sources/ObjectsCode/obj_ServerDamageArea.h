#pragma once

#include "GameCommon.h"

class obj_ServerPlayer;

class obj_ServerDamageArea : public GameObject
{
	DECLARE_CLASS(obj_ServerDamageArea, GameObject)
	
	float		m_Radius;
private:
	float		nextScan_;	
	float		m_Damage;
	void		ScanForPlayers();

public:
	obj_ServerDamageArea();
	~obj_ServerDamageArea();	

	virtual	BOOL		Load(const char *name);

	virtual	BOOL		OnCreate();
	virtual	BOOL		OnDestroy();

	virtual	BOOL		Update();

	virtual	void		ReadSerializedData(pugi::xml_node& node);
};

class DamageAreaMgr
{
public:
	enum { MAX_DAMAGE_AREA = 256 }; // 256 should be more than enough, if not, will redo into vector
	obj_ServerDamageArea* DamageArea_[MAX_DAMAGE_AREA];
	int		numDamageArea_;

	void RegisterDamageArea(obj_ServerDamageArea* dbox) 
	{
	r3d_assert(numDamageArea_ < MAX_DAMAGE_AREA);
		DamageArea_[numDamageArea_++] = dbox;
	}

public:
	DamageAreaMgr() { numDamageArea_ = 0; }
	~DamageAreaMgr() {}
};

extern	DamageAreaMgr gDamageAreaMngr;
