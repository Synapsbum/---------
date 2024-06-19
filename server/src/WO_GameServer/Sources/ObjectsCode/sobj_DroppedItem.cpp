#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"

#include "multiplayer/P2PMessages.h"
#include "ServerGameLogic.h"
#include "../EclipseStudio/Sources/ObjectsCode/weapons/WeaponArmory.h"

#include "sobj_DroppedItem.h"
#include "obj_ServerAirdrop.h"
#include "AsyncFuncs.h"
#include "Async_ServerObjects.h"

const float DROPPED_ITEM_EXPIRE_TIME = 10.0f * 60.0f; // 10 min
const static int DEV_EVENT_EXPIRE_TIME = 3 * 60; // dev event items will expire in 3 minutes

IMPLEMENT_CLASS(obj_DroppedItem, "obj_DroppedItem", "Object");
AUTOREGISTER_CLASS(obj_DroppedItem);

obj_DroppedItem::obj_DroppedItem()
{
	srvObjParams_.ExpireTime = r3dGetTime() + DROPPED_ITEM_EXPIRE_TIME;	// setup here, as it can be overwritten
	AirDropPos = r3dPoint3D(0,0,0);
	m_isRegistered = false;
	m_FirstTime = 0;	
	ExpireFirstTime = r3dGetTime();
	m_isUseAirDrop = false;

#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())
	{
		srvObjParams_.ExpireTime = r3dGetTime() + DEV_EVENT_EXPIRE_TIME; //3.0f * 60.0f; // 3 minutes on Dev Event Servers
	}
#endif
#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode
	if (gServerLogic.ginfo_.IsGameBR())
	{
		srvObjParams_.ExpireTime = r3dGetTime() + DEV_EVENT_EXPIRE_TIME; // 3 minutes on BR Servers
	}
#endif //ENABLE_BATTLE_ROYALE
}

obj_DroppedItem::~obj_DroppedItem()
{
}

BOOL obj_DroppedItem::OnCreate()
{
	//r3dOutToLog("obj_DroppedItem %p created. %d, %f sec left\n", this, m_Item.itemID, srvObjParams_.ExpireTime - r3dGetTime());

	r3d_assert(NetworkLocal);
	r3d_assert(GetNetworkID());
	r3d_assert(m_Item.itemID);

	m_Item.ResetClipIfFull();
	
	if (m_Item.itemID == 'ARDR') // Airdrop Parachute
	{		
		distToCreateSq = FLT_MAX;
		distToDeleteSq = FLT_MAX;
		// raycast down to earth in case world was changed or trying to spawn item in the air (player killed during jump)
		//r3dPoint3D pos = gServerLogic.AdjustPositionToFloor(AirDropPos);
		//SetPosition(AirDropPos);
	}
	else {
	// overwrite object network visibility
		if (m_Item.itemID == 'FLPS') // Airdrop Falre
		{
			distToCreateSq = FLT_MAX;
			distToDeleteSq = FLT_MAX;
		}
		else
		{
			distToCreateSq = 130 * 130;
			distToDeleteSq = 150 * 150;
		}
		// raycast down to earth in case world was changed or trying to spawn item in the air (player killed during jump)
		r3dPoint3D pos = gServerLogic.AdjustPositionToFloor(GetPosition());
		SetPosition(pos);
	}

	gServerLogic.NetRegisterObjectToPeers(this);	

	return parent::OnCreate();
}

BOOL obj_DroppedItem::OnDestroy()
{
	//r3dOutToLog("obj_DroppedItem %p destroyed\n", this);

	if (m_Item.itemID == 'ARDR')
	{		
		PKT_S2C_AirDropMapUpdate_s n2;
		n2.airdropID	= toP2pNetId(GetNetworkID());
		n2.location		= AirDropPos;
		n2.m_time		= 0.0f;
		n2.active		= false;
		gServerLogic.p2pBroadcastToAll(&n2, sizeof(n2), true);
	}

	PKT_S2C_DestroyNetObject_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
	
	return parent::OnDestroy();
}

BOOL obj_DroppedItem::Update()
{
	if(r3dGetTime() > srvObjParams_.ExpireTime)
	{
		setActiveFlag(0);
	}
#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode
	if (gServerLogic.ginfo_.IsGameBR() && gServerLogic.m_isGameHasStarted && gServerLogic.canFinishMatch)
	{
		setActiveFlag(0);
		//r3dOutToLog("[BattleRoyale] -- Dropped item removed!\n");
	}
#endif //ENABLE_BATTLE_ROYALE

	if (m_FirstTime == 1 && r3dGetTime()>ExpireFirstTime)
	{
		m_FirstTime = 0;
	}	

	if (m_isRegistered == false && m_Item.itemID == 'ARDR') 
	{ 
		if(m_FirstTime == 1)
			AirDropPos.y-=0.01f; //speed of decent of plane..
		else
			AirDropPos.y-=0.1f; //speed of decent of container.. // AirDropPos.y-=SRV_WORLD_SCALE(0.1f)

		
		SetPosition(AirDropPos);

		PKT_S2C_DropItemYPosition_s n;
		n.YPos = AirDropPos.y;
		n.spawnID = toP2pNetId(GetNetworkID());
		gServerLogic.p2pBroadcastToAll(&n, sizeof(n), true);

		R3DPROFILE_START("RayCast");
		PhysicsCallbackObject* target = NULL;
		const MaterialType *mt = 0;

		PxRaycastHit hit;
		PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_STATIC_MASK,0,0,0), PxSceneQueryFilterFlags(PxSceneQueryFilterFlag::eSTATIC|PxSceneQueryFilterFlag::eDYNAMIC));

		float Ypos = 0;
		if(m_FirstTime == 1) Ypos=0.5f;
		else				 Ypos=0.05f;

		bool hitResult = g_pPhysicsWorld->raycastSingle(PxVec3(GetPosition().x, GetPosition().y + Ypos, GetPosition().z), PxVec3(0, -1, 0), 1.0f, PxSceneQueryFlags(PxSceneQueryFlag::eIMPACT), hit, filter);
		if( hitResult )
		{			
			if( hit.shape && (target = static_cast<PhysicsCallbackObject*>(hit.shape->getActor().userData)))
			{
				r3dMaterial* material = 0;
				GameObject *gameObj = target->isGameObject();
				
				if(gameObj)
				{					
					//if(gameObj->isObjType(OBJTYPE_Terrain))
					{						
						m_isRegistered = true;
						//r3dOutToLog("!!!!!!!!!! gameObj: %s\n", gameObj->Name.c_str());
						// spawn airdrop container
						// create network object
						obj_ServerAirdrop* airdrop= (obj_ServerAirdrop*)srv_CreateGameObject("obj_ServerAirdrop", "obj_ServerAirdrop", GetPosition());						
						airdrop->SetNetworkID(gServerLogic.GetFreeNetId());						
						r3dPoint3D pos = gServerLogic.AdjustPositionToFloor(GetPosition());
						airdrop->SetPosition(pos);
						airdrop->NetworkLocal = true;
						// create params						
						airdrop->m_ItemID = WeaponConfig::ITEMID_AirDrop;//m_isUseAirDrop?gServerLogic.airdropItemID:gServerLogic.airdrop2ItemID;
						airdrop->SetRotationVector(r3dPoint3D(0, 0, 0));
						airdrop->OnCreate();							
						airdrop->GiveAirDropLoadout(WeaponConfig::ITEMID_AirDrop);
						//airdrop->GiveAirDropLoadout(m_isUseAirDrop?gServerLogic.airdropItemID:gServerLogic.airdrop2ItemID);					

						CJobAddServerObject* job = new CJobAddServerObject(airdrop);
						g_AsyncApiMgr->AddJob(job);						

						// spawn flare
						wiInventoryItem Flare;
						Flare.itemID   = 'FLPS';
						Flare.quantity = 1;
						// create network object
						r3dPoint3D PosObjects = AirDropPos+r3dPoint3D(2,0,0);
						obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", PosObjects);
						r3dPoint3D flarepos = gServerLogic.AdjustPositionToFloor(PosObjects);
						obj->SetPosition(flarepos);						
						obj->SetNetworkID(gServerLogic.GetFreeNetId());
						obj->NetworkLocal = true;
						obj->m_Item          = Flare;
						obj->m_Item.quantity = 1;						
						setActiveFlag(0);					
					}
				}
			}
		}
		R3DPROFILE_END("RayCast");
	}

	return parent::Update();
}


DefaultPacket* obj_DroppedItem::NetGetCreatePacket(int* out_size)
{
	if (m_Item.itemID == 'ARDR')
	{		
		PKT_S2C_AirDropMapUpdate_s n2;
		n2.airdropID	= toP2pNetId(GetNetworkID());
		n2.location		= AirDropPos;
		n2.m_time		= r3dGetTime() + (1.0f * 60.0f); // 1 min to expire
		n2.active		= true;
		gServerLogic.p2pBroadcastToAll(&n2, sizeof(n2), true);
	}

	static PKT_S2C_CreateDroppedItem_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	n.pos     = GetPosition();
	n.Item    = m_Item;
	n.FirstTime = m_FirstTime;	
	
	*out_size = sizeof(n);
	return &n;
}

void obj_DroppedItem::LoadServerObjectData()
{
	// deserialize from xml
	IServerObject::CSrvObjXmlReader xml(srvObjParams_.Var1);
	m_Item.itemID      = srvObjParams_.ItemID;
	m_Item.InventoryID = xml.xmlObj.attribute("iid").as_int64();
	m_Item.quantity    = xml.xmlObj.attribute("q").as_int();
	m_Item.Var1        = xml.xmlObj.attribute("v1").as_int();
	m_Item.Var2        = xml.xmlObj.attribute("v2").as_int();
	m_Item.Var3        = xml.xmlObj.attribute("v3").as_int();
}

void obj_DroppedItem::SaveServerObjectData()
{
	srvObjParams_.ItemID     = m_Item.itemID;

	char strInventoryID[64];
	sprintf(strInventoryID, "%I64d", m_Item.InventoryID);

	IServerObject::CSrvObjXmlWriter xml;
	xml.xmlObj.append_attribute("iid") = strInventoryID;
	xml.xmlObj.append_attribute("q")   = m_Item.quantity;
	xml.xmlObj.append_attribute("v1")  = m_Item.Var1;
	xml.xmlObj.append_attribute("v2")  = m_Item.Var2;
	xml.xmlObj.append_attribute("v3")  = m_Item.Var3;
	xml.save(srvObjParams_.Var1);
}
