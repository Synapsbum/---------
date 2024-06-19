#pragma once

#include "GameCommon.h"
#include "NetworkHelper.h"

class obj_ServerBuilding : public GameObject, INetworkHelper
{
	DECLARE_CLASS(obj_ServerBuilding, GameObject)

public:
	obj_ServerBuilding();
	~obj_ServerBuilding();

	INetworkHelper*	GetNetworkHelper() { return dynamic_cast<INetworkHelper*>(this); }
	DefaultPacket*	INetworkHelper::NetGetCreatePacket(int* out_size);
	void		INetworkHelper::LoadServerObjectData() { r3dError("not implemented"); }
	void		INetworkHelper::SaveServerObjectData() { r3dError("not implemented"); }
	virtual BOOL		Load(const char* name);
	virtual BOOL		OnCreate();
	virtual	BOOL		Update();
	virtual	BOOL		OnDestroy();
	void		ReadSerializedData(pugi::xml_node& node);
};
