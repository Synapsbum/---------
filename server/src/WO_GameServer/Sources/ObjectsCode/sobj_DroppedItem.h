#pragma once

#include "GameCommon.h"
#include "NetworkHelper.h"

class obj_DroppedItem : public GameObject, INetworkHelper
{
	DECLARE_CLASS(obj_DroppedItem, GameObject)

public:
	wiInventoryItem	m_Item;	

public:
	obj_DroppedItem();
	~obj_DroppedItem();
	
	virtual BOOL	OnCreate();
	virtual BOOL	OnDestroy();
	virtual BOOL	Update();
	
	r3dPoint3D	AirDropPos;
	bool	m_isRegistered;
	BYTE	m_FirstTime;
	float	ExpireFirstTime;
	bool	m_isUseAirDrop;
	
	INetworkHelper*	GetNetworkHelper() { return dynamic_cast<INetworkHelper*>(this); }
	DefaultPacket*	INetworkHelper::NetGetCreatePacket(int* out_size);

	// we no longer save dropped items. int		GetServerObjectSerializationType() { return 2; } // hibernating object
	void		INetworkHelper::LoadServerObjectData();
	void		INetworkHelper::SaveServerObjectData();
};
