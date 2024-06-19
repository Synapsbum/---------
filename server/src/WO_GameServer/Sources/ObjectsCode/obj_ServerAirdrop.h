#pragma once

#include "GameCommon.h"
#include "NetworkHelper.h"

class obj_ServerAirdrop: public GameObject, INetworkHelper
{
	DECLARE_CLASS(obj_ServerAirdrop, GameObject)
public:
	uint32_t	m_ItemID; // itemID of airdrop
	int		m_ObstacleId;
	float		m_Radius;
	int		m_IsLocked;

	std::vector<wiInventoryItem> items; // all items in this airdrop
	uint32_t	maxItems; // max items this airdrop can hold	

	int		nextInventoryID;	
	
	// security lockdown list. per user.
	struct lock_s
	{
		DWORD	peerId;
		DWORD	CustomerID;
		float		lockEndTime;
		int		tries;
	};
	std::vector<lock_s> m_lockdowns;
	std::vector<lock_s> m_uses;		// used for tracking usage-per-seconds for airdrop, lockEndTime used as airdrop opening time
	float		m_nextLockdownClear;

public:
	obj_ServerAirdrop();
	~obj_ServerAirdrop();

	virtual	BOOL	OnCreate();
	virtual	BOOL	OnDestroy();

	virtual	BOOL	Update();	

	void		LockAirDrop(const obj_ServerPlayer* plr, int set);
	int			GetItemDefault(int i);
	void		GiveAirDropLoadout(uint32_t itemID);
	void		SendContentToPlayer(obj_ServerPlayer* plr);
	bool		IsLockdownActive(const obj_ServerPlayer* plr);
	void		SetLockdown(DWORD peerId);
	bool		IsAirdropAbused(const obj_ServerPlayer* plr);
	void		DestroyAirdrop();
	void		UpdateServerData();

	wiInventoryItem* FindAirdropItemWithInvID(__int64 invID);
	bool		AddItemToAirdrop(const wiInventoryItem& itm, int quantity);
	void		RemoveItemFromAirdrop(__int64 invID, int amount);

	INetworkHelper*	GetNetworkHelper() { return dynamic_cast<INetworkHelper*>(this); }
	DefaultPacket*	INetworkHelper::NetGetCreatePacket(int* out_size);

	int			GetServerObjectSerializationType() { return 4; } // airdrop object
	void		INetworkHelper::LoadServerObjectData();
	void		INetworkHelper::SaveServerObjectData();
};
