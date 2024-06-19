#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"

#include "multiplayer/P2PMessages.h"

#include "obj_ServerPlayer.h"
#include "obj_ServerAirdrop.h"
#include "ServerGameLogic.h"
#include "../EclipseStudio/Sources/ObjectsCode/weapons/WeaponArmory.h"
#include "../../GameEngine/ai/AutodeskNav/AutodeskNavMesh.h"
#include "Async_ServerObjects.h"

IMPLEMENT_CLASS(obj_ServerAirdrop, "obj_ServerAirdrop", "Object");
AUTOREGISTER_CLASS(obj_ServerAirdrop);

const static int AIRDROP_EXPIRE_TIME = 20 * 60; // airdrop expire time
const static int AIRDROP_LOCKDOWN_TIME        = 5;

extern wiInventoryItem RollItem(const LootBoxConfig* lootCfg, int depth);

obj_ServerAirdrop::obj_ServerAirdrop()
{
	ObjTypeFlags |= OBJTYPE_GameplayItem;
	ObjFlags |= OBJFLAG_SkipCastRay;

	m_ItemID = 0;
	m_ObstacleId = -1;

	maxItems = 30; // for now hard coded
	nextInventoryID = 1;
	m_IsLocked   = 0;

	m_nextLockdownClear = r3dGetTime() + 60.0f;

	srvObjParams_.ExpireTime = r3dGetTime() + AIRDROP_EXPIRE_TIME;
}

obj_ServerAirdrop::~obj_ServerAirdrop()
{
}

BOOL obj_ServerAirdrop::OnCreate()
{
	r3dOutToLog("obj_ServerAirdrop[%d] created. netID: %d ItemID:%d pos: x: %.2f y: %.2f\n", srvObjParams_.ServerObjectID, GetNetworkID(), m_ItemID, GetPosition().x, GetPosition().y);

	// set FileName based on itemid for ReadPhysicsConfig() in OnCreate() 
	r3dPoint3D bsize(1, 1, 1);
	
	r3d_assert(m_ItemID>0);

	const AirdropConfig* cfg = g_pWeaponArmory->getAirdropConfig(m_ItemID);
	if(m_ItemID == WeaponConfig::ITEMID_AirDrop || m_ItemID == WeaponConfig::ITEMID_AirDrop2)
	{
		if(cfg)		
			FileName = cfg->m_ModelPath;		
		else
			FileName = "Data\\ObjectsDepot\\WZ_Airdrop\\Supplydrop.sco";	
		bsize    = r3dPoint3D(2.0900440f, 2.2519419f, 1.79267800f);
	}
	else
		r3dError("!!! unknown airdrop item %d\n", m_ItemID);

	distToCreateSq = FLT_MAX;
	distToDeleteSq = FLT_MAX;

	parent::OnCreate();

	// add navigational obstacle
	r3dBoundBox obb;
	obb.Size = bsize;
	obb.Org  = r3dPoint3D(GetPosition().x - obb.Size.x/2, GetPosition().y, GetPosition().z - obb.Size.z/2);
	m_ObstacleId = gAutodeskNavMesh.AddObstacle(this, obb, GetRotationVector().x);

	// calc 2d radius
	m_Radius = R3D_MAX(obb.Size.x, obb.Size.z) / 2;

	gServerLogic.NetRegisterObjectToPeers(this);	

	// convert old airdrops with unlimited expire time to current expiration time
	if((int)(srvObjParams_.ExpireTime - r3dGetTime()) > AIRDROP_EXPIRE_TIME)
	{
		UpdateServerData();
	}	

	return 1;
}

BOOL obj_ServerAirdrop::OnDestroy()
{
	if(m_ObstacleId >= 0)
	{
		gAutodeskNavMesh.RemoveObstacle(m_ObstacleId);
	}

	PKT_S2C_AirDropMapUpdate_s n2;
	n2.airdropID	= toP2pNetId(GetNetworkID());
	n2.location		= GetPosition();
	n2.m_time		= 0.0f;
	n2.active		= false;
	gServerLogic.p2pBroadcastToAll(&n2, sizeof(n2), true);

	PKT_S2C_DestroyNetObject_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));

	return parent::OnDestroy();
}

wiInventoryItem* obj_ServerAirdrop::FindAirdropItemWithInvID(__int64 invID)
{
	for(size_t i =0; i<items.size(); ++i)
	{
		if(items[i].InventoryID == invID)
			return &items[i];
	}
	return NULL;
}

void obj_ServerAirdrop::GiveAirDropLoadout(uint32_t itemID)
{
	r3d_assert(itemID>0);

	//get items from lootbox						
	uint32_t Items[30];
	LootBoxConfig*	m_LootBoxConfig;
	if(itemID == WeaponConfig::ITEMID_AirDrop)
	{
		for (int i = 0;i<gServerLogic.airdropItemsCount+1;i++)
		{
			m_LootBoxConfig = const_cast<LootBoxConfig*>(g_pWeaponArmory->getLootBoxConfig(gServerLogic.airdropItems[i].itemID));
			if(m_LootBoxConfig != NULL)
			{
				if(m_LootBoxConfig->entries.size() != 0)
				{
					wiInventoryItem entrieID = RollItem(m_LootBoxConfig, 0);
					Items[i] = entrieID.itemID;
				}	
				else {
					Items[i] = GetItemDefault(i);
				}
			}
			else {
				Items[i] = GetItemDefault(i);
			}		
		}

		//add items to airdrop
		wiInventoryItem tempItem;
		for (int i = 0; i < gServerLogic.airdropItemsCount+1; i++)
		{
			tempItem.itemID = Items[i];
			if (tempItem.itemID == 0)
				continue;
			if(gServerLogic.airdropItems[i].quantity>1)
				tempItem.quantity = int(u_GetRandom(2, float(gServerLogic.airdropItems[i].quantity)));
			else
				tempItem.quantity = gServerLogic.airdropItems[i].quantity;

			AddItemToAirdrop(tempItem, tempItem.quantity);
		}

		r3dOutToLog("obj_ServerAirdrop[%d] netID: %d -- Airdrop Loadout configuration has been added with: %d items. Airdrop ItemID: %d\n", srvObjParams_.ServerObjectID, GetNetworkID(), gServerLogic.airdrop2ItemsCount+1, itemID);
	}
	else if(itemID == WeaponConfig::ITEMID_AirDrop2)
	{
		for (int i = 0;i<gServerLogic.airdrop2ItemsCount+1;i++)
		{
			m_LootBoxConfig = const_cast<LootBoxConfig*>(g_pWeaponArmory->getLootBoxConfig(gServerLogic.airdrop2Items[i].itemID));
			if(m_LootBoxConfig != NULL)
			{
				if(m_LootBoxConfig->entries.size() != 0)
				{
					wiInventoryItem entrieID = RollItem(m_LootBoxConfig, 0);
					Items[i] = entrieID.itemID;
				}	
				else {
					Items[i] = GetItemDefault(i);
				}
			}
			else {
				Items[i] = GetItemDefault(i);
			}		
		}

		//add items to airdrop
		wiInventoryItem tempItem;
		for (int i = 0; i < gServerLogic.airdrop2ItemsCount+1; i++)
		{			
			tempItem.itemID = Items[i];
			if(gServerLogic.airdrop2Items[i].quantity>1)
				tempItem.quantity = int(u_GetRandom(2, float(gServerLogic.airdrop2Items[i].quantity)));
			else
				tempItem.quantity = gServerLogic.airdrop2Items[i].quantity;

			AddItemToAirdrop(tempItem, tempItem.quantity);
		}

		r3dOutToLog("obj_ServerAirdrop[%d] netID: %d -- Airdrop Loadout configuration has been added with: %d items. Airdrop ItemID: %d\n", srvObjParams_.ServerObjectID, GetNetworkID(), gServerLogic.airdrop2ItemsCount+1, itemID);
	}
}

int obj_ServerAirdrop::GetItemDefault(int i)
{
	switch(i)
	{
	case 0:	
			return 20015;// gerilia
	case 1:
			return 101087;// AWM
	case 2:
			return 101172;// SIG 556
	case 3:	
			return 20180;// backpack military 
	case 4:	
			return 20006;// K-Stile
	case 5:	
			return 101304;// medkit
	case 6:	
			return 101284;// Bag MRE
	case 7:	
			return 101318;// riot shield
	}
	r3dOutToLog("obj_ServerAirdrop[%d] netID: %d -- Default items has been added.\n", srvObjParams_.ServerObjectID, GetNetworkID());
	return 0;
}

bool obj_ServerAirdrop::AddItemToAirdrop(const wiInventoryItem& itm, int quantity)
{
	for(size_t i=0; i<items.size(); ++i)
	{
		if(items[i].CanStackWith(itm))
		{
			items[i].quantity += quantity;

			UpdateServerData();
			return true;
		}
	}
	
	if(items.size() < maxItems)
	{
		wiInventoryItem itm2 = itm;
		itm2.InventoryID = nextInventoryID++;
		itm2.quantity    = quantity;
		items.push_back(itm2);

		UpdateServerData();
		return true;
	}
	
	return false;
}

void obj_ServerAirdrop::RemoveItemFromAirdrop(__int64 invID, int amount)
{
	r3d_assert(amount >= 0);
	
	for(size_t i=0; i<items.size(); ++i)
	{
		if(items[i].InventoryID == invID)
		{
			r3d_assert(amount <= items[i].quantity);
			items[i].quantity -= amount;

			// remove from airdrop items array
			if(items[i].quantity <= 0)
			{
				items.erase(items.begin() + i);
			}

			UpdateServerData();	
			return;
		}
	}	
	
	// invId must be validated first
	r3d_assert(false && "no invid in Airdrop");
	return;
}

BOOL obj_ServerAirdrop::Update()
{
	//const float curTime = r3dGetTime();	
	
	// erase entries with expire lockdown. to avoid large memory usage if every player will try to unlock it :)
	/*if(curTime > m_nextLockdownClear)
	{
		m_nextLockdownClear = curTime + 60.0f;
		
		for(std::vector<lock_s>::iterator it = m_lockdowns.begin(); it != m_lockdowns.end(); )
		{
			if(curTime > it->lockEndTime) {
				it = m_lockdowns.erase(it);
			} else {
				++it;
			}
		}

		// keep uses for 5 min, lockEndTime used as airdrop opening time
		for(std::vector<lock_s>::iterator it = m_uses.begin(); it != m_uses.end(); )
		{
			if(curTime > it->lockEndTime + 5 * 60) {
				it = m_uses.erase(it);
			} else {
				++it;
			}
		}
	}*/	

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode
	if (gServerLogic.ginfo_.IsGameBR() && gServerLogic.m_isGameHasStarted && gServerLogic.canFinishMatch)
	{
		DestroyAirdrop();		
	}
#endif //ENABLE_BATTLE_ROYALE

	// delete expire airdrop
	if (srvObjParams_.ExpireTime < r3dGetTime())
		DestroyAirdrop();

	// delete server airdrop
	if(items.size() <= 0)	
		DestroyAirdrop();
	
	return parent::Update();
}

void obj_ServerAirdrop::SendContentToPlayer(obj_ServerPlayer* plr)
{
	PKT_S2C_LockboxOpReq_s n2;
	n2.op = PKT_S2C_LockboxOpReq_s::LBOR_StartingToSendContent;
	n2.lockboxID = toP2pNetId(GetNetworkID());
	n2.isVehicle = false;
	n2.isAirdrop = true;	
	n2.airdropTimeLeft = srvObjParams_.ExpireTime - r3dGetTime();
	gServerLogic.p2pSendToPeer(plr->peerId_, this, &n2, sizeof(n2));	

	PKT_S2C_LockboxContent_s n;
	for(uint32_t i=0; i<items.size(); ++i)
	{
		n.item = items[i];		
		gServerLogic.p2pSendToPeer(plr->peerId_, this, &n, sizeof(n));		
	}	

	n2.op = PKT_S2C_LockboxOpReq_s::LBOR_DoneSendingContent;
	n2.lockboxID = toP2pNetId(GetNetworkID());	
	gServerLogic.p2pSendToPeer(plr->peerId_, this, &n2, sizeof(n2));	
}

bool obj_ServerAirdrop::IsLockdownActive(const obj_ServerPlayer* plr)
{
	/*const float curTime = r3dGetTime();
	
	for(size_t i=0; i<m_lockdowns.size(); i++)
	{
		lock_s& lock = m_lockdowns[i];
		if(lock.CustomerID == plr->profile_.CustomerID && curTime < lock.lockEndTime)
		{
			lock.tries++;
			// technically user can use issue only one attempt per 1.5 sec (item use time)
			// so we check if user issued them faster that 1sec
			if(lock.tries > AIRDROP_LOCKDOWN_TIME)
			{
				gServerLogic.LogCheat(plr->peerId_, PKT_S2C_CheatWarning_s::CHEAT_Airdrop, false, "Airdrop",
					"tries %d", lock.tries);
			}
			return true;
		}
	}*/

	for(size_t i=0; i<m_lockdowns.size(); i++)
	{
		lock_s& lock = m_lockdowns[i];
		if(!m_IsLocked)
			return false;
		else if(m_IsLocked && lock.CustomerID == plr->profile_.CustomerID)
			return false;
		else if(m_IsLocked && lock.CustomerID != plr->profile_.CustomerID)
			return true;
	}

	return false;
}

void obj_ServerAirdrop::LockAirDrop(const obj_ServerPlayer* plr, int set)
{
	m_IsLocked = set;
	UpdateServerData();

	if(plr)
	{
		if(set==0)
		{
			for(size_t i=0; i<m_lockdowns.size(); i++)
			{
				if(m_lockdowns[i].CustomerID == plr->profile_.CustomerID)
				{
					m_lockdowns[i].peerId	   = 0;
					m_lockdowns[i].CustomerID  = 0;
					m_lockdowns[i].lockEndTime = 0;
					m_lockdowns[i].tries       = 0;
					return;
				}
			}		
		}		
	}
}

void obj_ServerAirdrop::SetLockdown(DWORD peerId)
{
	float lockEndTime = r3dGetTime() + AIRDROP_LOCKDOWN_TIME;

	obj_ServerPlayer* plr = gServerLogic.GetPeer(peerId).player;
		if(!plr) {
			return;
		}

	for(size_t i=0; i<m_lockdowns.size(); i++)
	{
		if(m_lockdowns[i].CustomerID == plr->profile_.CustomerID)
		{
			m_lockdowns[i].lockEndTime = lockEndTime;
			m_lockdowns[i].tries       = 0;
			return;
		}
	}
	
	lock_s lock;
	lock.peerId = plr->peerId_;
	lock.CustomerID  = plr->profile_.CustomerID;
	lock.lockEndTime = lockEndTime;
	lock.tries       = 0;
	m_lockdowns.push_back(lock);
}

bool obj_ServerAirdrop::IsAirdropAbused(const obj_ServerPlayer* plr)
{
	const float curTime = r3dGetTime();

	for(size_t i=0; i<m_uses.size(); i++)
	{
		lock_s& lock = m_uses[i];

		if(lock.CustomerID == plr->profile_.CustomerID)
		{
			if(curTime > lock.lockEndTime + 60)
			{
				// used at least minute ago, reset timer (lockEndTime used as lockbox opening time)
				lock.lockEndTime = curTime - 0.001f;
				lock.tries       = 0;
			}

			// there was a 'possible' dupe method that allowed to fire alot of requests to update lockbox
			// hoping that they will be executed in wrong order on API side if was put to different job queues
			lock.tries++;
			float ups = (float)lock.tries / (curTime - lock.lockEndTime);
			if(lock.tries > 10 && ups > 20)
			{
				// on local machine using UI i was able to put about 5 requests per sec, so 20 usages per sec is exploit.
				gServerLogic.LogCheat(plr->peerId_, PKT_S2C_CheatWarning_s::CHEAT_Airdrop, true, "AirdropUsagePerSec",
					"tries %d, ups:%.1f", lock.tries, ups);
				return true;
			}

			return false;
		}
	}

	lock_s lock;
	lock.peerId = plr->peerId_;
	lock.CustomerID  = plr->profile_.CustomerID;
	lock.lockEndTime = curTime;
	lock.tries       = 1;
	m_uses.push_back(lock);
	
	return false;
}


void obj_ServerAirdrop::DestroyAirdrop()
{
	// close airdrop
	for(size_t i=0; i<m_lockdowns.size(); i++)
	{
		obj_ServerPlayer* plr = gServerLogic.GetPeer(m_lockdowns[i].peerId).player;
		if(!plr) {
			break;
		}		
		if(plr)
		{		
			PKT_S2C_LockboxOpReq_s n2;
			n2.op        = PKT_S2C_LockboxOpReq_s::LBOR_Close;
			n2.lockboxID = toP2pNetId(GetNetworkID());
			gServerLogic.p2pSendToPeer(plr->peerId_, this, &n2, sizeof(n2));
		}
	}

	setActiveFlag(0);
	g_AsyncApiMgr->AddJob(new CJobDeleteServerObject(this));
}

void obj_ServerAirdrop::UpdateServerData()
{
	// if airdrop was used, extend expire time for 10 minutes
	srvObjParams_.ExpireTime = srvObjParams_.CreateTime + AIRDROP_EXPIRE_TIME;//r3dGetTime() + AIRDROP_EXPIRE_TIME;
	g_AsyncApiMgr->AddJob(new CJobUpdateServerObject(this));
}

DefaultPacket* obj_ServerAirdrop::NetGetCreatePacket(int* out_size)
{
	static PKT_S2C_CreateNetObject_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	n.itemID  = m_ItemID;
	n.pos     = GetPosition();
	n.var1    = GetRotationVector().x;	

	PKT_S2C_AirDropMapUpdate_s n2;
	n2.airdropID	= toP2pNetId(GetNetworkID());
	n2.location		= GetPosition();
	n2.m_time		= srvObjParams_.ExpireTime;
	n2.active		= true;
	gServerLogic.p2pBroadcastToAll(&n2, sizeof(n2), true);

	*out_size = sizeof(n);
	return &n;
}

void obj_ServerAirdrop::LoadServerObjectData()
{
	m_ItemID = srvObjParams_.ItemID;

	// deserialize from xml
	IServerObject::CSrvObjXmlReader xml(srvObjParams_.Var1);
	m_IsLocked   = xml.xmlObj.attribute("locked").as_int();
	uint32_t numItems = xml.xmlObj.attribute("numItems").as_uint();
	pugi::xml_node xmlItem = xml.xmlObj.child("item");
	for(uint32_t i=0; i<numItems; ++i)
	{
		if(xmlItem.empty()) // should never be empty
		{
			return; // bail out
		}
		wiInventoryItem it;
		it.InventoryID = nextInventoryID++;
		it.itemID = xmlItem.attribute("id").as_uint();
		
		// verify itemID is valid
		if(g_pWeaponArmory->getConfig(it.itemID)==NULL)
			return; // bail out

		it.quantity = xmlItem.attribute("q").as_uint();
		it.Var1 = xmlItem.attribute("v1").as_int();
		it.Var2 = xmlItem.attribute("v2").as_int();
		if(xmlItem.attribute("v3").value()[0])
			it.Var3 = xmlItem.attribute("v3").as_int();
		else
			it.Var3 = wiInventoryItem::MAX_DURABILITY;

		it.ResetClipIfFull(); // in case when full clip was saved before 2013-4-18
		items.push_back(it);

		xmlItem = xmlItem.next_sibling();
	}
}

void obj_ServerAirdrop::SaveServerObjectData()
{
	srvObjParams_.ItemID = m_ItemID;

	IServerObject::CSrvObjXmlWriter xml;
	xml.xmlObj.append_attribute("locked")     = m_IsLocked;
	xml.xmlObj.append_attribute("numItems") = items.size();
	for(size_t i=0; i<items.size(); ++i)
	{
		pugi::xml_node xmlItem = xml.xmlObj.append_child();
		xmlItem.set_name("item");
		xmlItem.append_attribute("id") = items[i].itemID;
		xmlItem.append_attribute("q") = items[i].quantity;
		xmlItem.append_attribute("v1") = items[i].Var1;
		xmlItem.append_attribute("v2") = items[i].Var2;
		xmlItem.append_attribute("v3") = items[i].Var3;
	}
	xml.save(srvObjParams_.Var1);
}

