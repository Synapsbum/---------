#pragma once

#include "GameCommon.h"

class obj_ServerGasArea : public GameObject
{
	DECLARE_CLASS(obj_ServerGasArea, GameObject)

public:
	float		defaultRadius;
	float		useRadius;

public:
	obj_ServerGasArea();
	~obj_ServerGasArea();

	virtual BOOL	OnCreate();
	virtual	void	ReadSerializedData(pugi::xml_node& node);
};

class GasAreaMgr
{
public:
	enum { MAX_GAS_AREA = 256 }; // 256 should be more than enough, if not, will redo into vector
	obj_ServerGasArea* GasArea_[MAX_GAS_AREA];
	int		numGasArea_;

	void RegisterGasArea(obj_ServerGasArea* rbox) 
	{
		r3d_assert(numGasArea_ < MAX_GAS_AREA);
		GasArea_[numGasArea_++] = rbox;
	}

public:
	GasAreaMgr() { numGasArea_ = 0; }
	~GasAreaMgr() {}
};

extern	GasAreaMgr gGasAreaMngr;
