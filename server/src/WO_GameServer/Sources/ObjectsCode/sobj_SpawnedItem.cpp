#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"

#include "multiplayer/P2PMessages.h"
#include "ServerGameLogic.h"

#include "sobj_SpawnedItem.h"
#include "obj_ServerItemSpawnPoint.h"
#include "../EclipseStudio/Sources/ObjectsCode/weapons/WeaponArmory.h"

IMPLEMENT_CLASS(obj_SpawnedItem, "obj_SpawnedItem", "Object");
AUTOREGISTER_CLASS(obj_SpawnedItem);

obj_SpawnedItem::obj_SpawnedItem()
{
	m_SpawnIdx = -1;
	m_DestroyIn = 0.0f;
	m_isHackerDecoy = false;
	m_SpawnLootCrateOnClient = false;	
}

obj_SpawnedItem::~obj_SpawnedItem()
{
}

void obj_SpawnedItem::AdjustDroppedWeaponAmmo()
{
	const WeaponConfig* wcfg = g_pWeaponArmory->getWeaponConfig(m_Item.itemID);
	if(wcfg == NULL)
		return;
	const WeaponAttachmentConfig* clipCfg = g_pWeaponArmory->getAttachmentConfig(wcfg->FPSDefaultID[WPN_ATTM_CLIP]);
	if(!clipCfg)
		return;
	
	float clipSize = (float)clipCfg->m_Clipsize;
	
	if (gServerLogic.ginfo_.channel == 3 || gServerLogic.ginfo_.channel == 4) //AlexRedd:: get full ammo for private & premium servers
	{
		m_Item.Var1 = clipCfg->m_Clipsize;
		m_Item.Var2 = clipCfg->m_itemID;		
	}
	else if(int(u_GetRandom(0.0f, 10.0f))<3) // 30% chance to get brand new
	{
		m_Item.Var1 = clipCfg->m_Clipsize;
		m_Item.Var2 = clipCfg->m_itemID;
	}
	else
	{
		m_Item.Var1 = R3D_MAX(1, (int)u_GetRandom(clipSize * 0.25f, clipSize * 1.0f));
		m_Item.Var2 = clipCfg->m_itemID;
	}
}

BOOL obj_SpawnedItem::OnCreate()
{
	//r3dOutToLog("obj_SpawnedItem %p created. %dx%d\n", this, m_Item.itemID, m_Item.quantity);
	
	r3d_assert(NetworkLocal);
	r3d_assert(GetNetworkID());
	r3d_assert(m_SpawnObj.get() > 0);
	r3d_assert(m_SpawnIdx >= 0);
	r3d_assert(m_Item.itemID);
	
	m_Item.ResetClipIfFull();
	AdjustDroppedWeaponAmmo();

	// raycast down to earth in case world was changed
	r3dPoint3D pos = gServerLogic.AdjustPositionToFloor(GetPosition());
	SetPosition(pos);

	// overwrite object network visibility
	if (m_Item.itemID >= WeaponConfig::ITEMID_AssaultCase && m_Item.itemID <= WeaponConfig::ITEMID_GearCase)//AlexRedd:: rare box
	{		
		distToCreateSq = FLT_MAX;
		distToDeleteSq = FLT_MAX;		
	}
	else
	{	// overwrite object network visibility
		distToCreateSq = 130 * 130;
		distToDeleteSq = 150 * 150;
	}
	
	gServerLogic.NetRegisterObjectToPeers(this);
	
	obj_ServerItemSpawnPoint* owner = (obj_ServerItemSpawnPoint*)GameWorld().GetObject(m_SpawnObj);

	return parent::OnCreate();
}

BOOL obj_SpawnedItem::OnDestroy()
{
	//r3dOutToLog("obj_SpawnedItem %p destroyed\n", this);

	obj_ServerItemSpawnPoint* owner = (obj_ServerItemSpawnPoint*)GameWorld().GetObject(m_SpawnObj);
	r3d_assert(owner);
	owner->PickUpObject(m_SpawnIdx, GetSafeID());
	
	PKT_S2C_DestroyNetObject_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));

	//AlexRedd:: rare box
	if (m_Item.itemID >= WeaponConfig::ITEMID_AssaultCase && m_Item.itemID <= WeaponConfig::ITEMID_GearCase)
	{		
		PKT_S2C_RareBoxUpdate_s box;
		box.boxID       = toP2pNetId(GetNetworkID());
		box.location	= GetPosition();
		box.m_time		= 0.0f;
		box.active		= false;
		gServerLogic.p2pBroadcastToAll(&box, sizeof(box), true);
	}

	return parent::OnDestroy();
}

BOOL obj_SpawnedItem::Update()
{
	if(m_DestroyIn > 0 && r3dGetTime() > m_DestroyIn)
		return FALSE; // force item de-spawn

	return parent::Update();
}

DefaultPacket* obj_SpawnedItem::NetGetCreatePacket(int* out_size)
{
	//AlexRedd:: rare box
	if (m_Item.itemID >= WeaponConfig::ITEMID_AssaultCase && m_Item.itemID <= WeaponConfig::ITEMID_GearCase)
	{		
		PKT_S2C_RareBoxUpdate_s box;
		box.boxID       = toP2pNetId(GetNetworkID());
		box.itemID		= m_Item.itemID;
		box.location	= GetPosition();
		box.m_time		= m_DestroyIn;
		box.active		= true;
		gServerLogic.p2pBroadcastToAll(&box, sizeof(box), true);		
	}

	static PKT_S2C_CreateDroppedItem_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	n.pos     = GetPosition();
	n.Item    = m_Item;	

	if(m_SpawnLootCrateOnClient)
	{
		n.Item.itemID = 'LOOT';
		n.Item.quantity = 1;
	}

	*out_size = sizeof(n);
	return &n;
}