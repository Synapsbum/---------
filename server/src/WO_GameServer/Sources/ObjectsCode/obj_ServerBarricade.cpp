#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"

#include "multiplayer/P2PMessages.h"

#include "obj_ServerBarricade.h"
#include "ServerGameLogic.h"
#include "../EclipseStudio/Sources/ObjectsCode/weapons/WeaponArmory.h"
#include "../../GameEngine/ai/AutodeskNav/AutodeskNavMesh.h"
#include "Async_ServerObjects.h"

IMPLEMENT_CLASS(obj_ServerBarricade, "obj_ServerBarricade", "Object");
AUTOREGISTER_CLASS(obj_ServerBarricade);
IMPLEMENT_CLASS(obj_StrongholdServerBarricade, "obj_StrongholdServerBarricade", "Object");
AUTOREGISTER_CLASS(obj_StrongholdServerBarricade);

std::vector<obj_ServerBarricade*> obj_ServerBarricade::allBarricades;

const static int BARRICADE_EXPIRE_TIME = 3 * 24 * 60 * 60; // barricade will expire in 3 days
const static int STRONGHOLD_EXPIRE_TIME = 30 * 24 * 60 * 60; // stronghold items will expire in 30 days
const static int DEV_EVENT_EXPIRE_TIME = 30 * 60; // dev event items will expire in 30 minutes
const static int PVP_EXPIRE_TIME = 5 * 60; // pvp items will expire in 5 minutes

obj_StrongholdServerBarricade::obj_StrongholdServerBarricade() :
obj_ServerBarricade()
{
	float expireTime = r3dGetTime() + STRONGHOLD_EXPIRE_TIME;

#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())		
		expireTime = r3dGetTime() + DEV_EVENT_EXPIRE_TIME;
#endif

	if(gServerLogic.ginfo_.mapId == GBGameInfo::MAPID_WZ_Quarantine)
		expireTime = r3dGetTime() + PVP_EXPIRE_TIME;

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode	
	if (gServerLogic.ginfo_.IsGameBR())
		expireTime = r3dGetTime() + DEV_EVENT_EXPIRE_TIME;
#endif //ENABLE_BATTLE_ROYALE

	srvObjParams_.ExpireTime = expireTime; //r3dGetTime() + STRONGHOLD_EXPIRE_TIME;
}

obj_StrongholdServerBarricade::~obj_StrongholdServerBarricade()
{
}

obj_ServerBarricade::obj_ServerBarricade()
{
	allBarricades.push_back(this);

	ObjTypeFlags |= OBJTYPE_GameplayItem | OBJTYPE_Barricade;
	ObjFlags |= OBJFLAG_SkipCastRay;
	
	m_ItemID = 0;
	m_Health = 1;
	m_ObstacleId = -1;
	
	float expireTime = r3dGetTime() + BARRICADE_EXPIRE_TIME;

#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())		
		expireTime = r3dGetTime() + DEV_EVENT_EXPIRE_TIME;
#endif

	if(gServerLogic.ginfo_.mapId == GBGameInfo::MAPID_WZ_Quarantine)
		expireTime = r3dGetTime() + PVP_EXPIRE_TIME;

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode	
	if (gServerLogic.ginfo_.IsGameBR())
		expireTime = r3dGetTime() + DEV_EVENT_EXPIRE_TIME;
#endif //ENABLE_BATTLE_ROYALE

	srvObjParams_.ExpireTime = expireTime;
}

obj_ServerBarricade::~obj_ServerBarricade()
{
}

BOOL obj_ServerBarricade::OnCreate()
{
	//r3dOutToLog("obj_ServerBarricade[%d] created. ItemID:%d Health:%.0f, %.0f mins left\n", srvObjParams_.ServerObjectID, m_ItemID, m_Health, (srvObjParams_.ExpireTime - r3dGetTime()) / 60.0f);
	
	// set FileName based on itemid for ReadPhysicsConfig() in OnCreate() 
	r3dPoint3D bsize(1, 1, 1);
	if(m_ItemID == WeaponConfig::ITEMID_BarbWireBarricade)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Item_Barricade_BarbWire_Built.sco";
		bsize    = r3dPoint3D(5.320016f, 1.842704f, 1.540970f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_WoodShieldBarricade || m_ItemID==WeaponConfig::ITEMID_WoodShieldBarricadeZB)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Item_Barricade_WoodShield_Built.sco";
		bsize    = r3dPoint3D(1.582400f, 2.083451f, 0.708091f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_RiotShieldBarricade || m_ItemID==WeaponConfig::ITEMID_RiotShieldBarricadeZB)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Item_Riot_Shield_01.sco";
		bsize    = r3dPoint3D(1.726829f, 2.136024f, 0.762201f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_SandbagBarricade)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\item_barricade_Sandbag_built.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_WoodenDoorBlock)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Block_Door_Wood_2M_01.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_MetalWallBlock)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Block_Wall_Metal_2M_01.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_TallBrickWallBlock)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Block_Wall_Brick_Tall_01.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_WoodenWallPiece)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Block_Wall_Wood_2M_01.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_ShortBrickWallPiece)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Block_Wall_Brick_Short_01.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_PlaceableLight)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Block_Light_01.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_SmallPowerGenerator)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Block_PowerGen_01_Small.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_BigPowerGenerator)
	{
		FileName = "Data\\ObjectsDepot\\Weapons\\Block_PowerGen_01_Industrial.sco";
		bsize    = r3dPoint3D(1.513974f, 1.057301f, 1.111396f);
	}
	else if(m_ItemID == WeaponConfig::ITEMID_SSBarricadeShield)
	{
		FileName = "Data\\ObjectsDepot\\ZH_Items_Gameplay\\SS_Barricade_Shield.sco";
		bsize    = r3dPoint3D(2.726829f, 2.136024f, 1.111396f);
	}
	else
		r3dError("unknown barricade item %d\n", m_ItemID);

	parent::OnCreate();
	
	// add navigational obstacle
	r3dBoundBox obb;
	obb.Size = bsize;
	obb.Org  = r3dPoint3D(GetPosition().x - obb.Size.x/2, GetPosition().y, GetPosition().z - obb.Size.z/2);
	m_ObstacleId = gAutodeskNavMesh.AddObstacle(this, obb, GetRotationVector().x);
	
	// calc 2d radius
	m_Radius = R3D_MAX(obb.Size.x, obb.Size.z) / 2;

	gServerLogic.NetRegisterObjectToPeers(this);
	return 1;
}

BOOL obj_ServerBarricade::OnDestroy()
{
	if(m_ObstacleId >= 0)
	{
		gAutodeskNavMesh.RemoveObstacle(m_ObstacleId);
	}

	PKT_S2C_DestroyNetObject_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));

	for(std::vector<obj_ServerBarricade*>::iterator it = allBarricades.begin(); it != allBarricades.end(); ++it)
	{
		if(*it == this)
		{
			allBarricades.erase(it);
			break;
		}
	}
	
	return parent::OnDestroy();
}

BOOL obj_ServerBarricade::Update()
{
	if (srvObjParams_.ExpireTime < r3dGetTime())
		DestroyBarricade();

	// check for pending delete
	if(isActive() && m_Health <= 0.0f && srvObjParams_.ServerObjectID > 0)
	{
		g_AsyncApiMgr->AddJob(new CJobDeleteServerObject(this));

		setActiveFlag(0);
		return TRUE;
	}

	return parent::Update();
}

DefaultPacket* obj_ServerBarricade::NetGetCreatePacket(int* out_size)
{
	static PKT_S2C_CreateNetObject_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	n.itemID  = m_ItemID;
	n.pos     = GetPosition();
	n.var1    = GetRotationVector().x;
	n.var6	  = m_Health;//AlexRedd:: barricade health

	*out_size = sizeof(n);
	return &n;
}

void obj_ServerBarricade::DoDamage(float dmg)
{
	if(m_Health > 0)
	{
		srvObjParams_.IsDirty = true;
		m_Health -= dmg;
		// do not delete object here, it may still be waiting for assigned ServerObjectID

		//AlexRedd:: barricade health
		PKT_S2C_BarricadeHealth_s n;
		n.barricadeHealth = m_Health;
		gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
	}
}

void obj_ServerBarricade::LoadServerObjectData()
{
	m_ItemID = srvObjParams_.ItemID;
	
	// deserialize from xml
	IServerObject::CSrvObjXmlReader xml(srvObjParams_.Var1);
	m_Health = xml.xmlObj.attribute("Health").as_float();
}

void obj_ServerBarricade::SaveServerObjectData()
{
	srvObjParams_.ItemID = m_ItemID;

	IServerObject::CSrvObjXmlWriter xml;
	xml.xmlObj.append_attribute("Health") = m_Health;
	xml.save(srvObjParams_.Var1);
}

#ifdef VEHICLES_ENABLED
int obj_ServerBarricade::GetDamageForVehicle()
{
	switch( m_ItemID )
	{
	default:
		return 0;
	case WeaponConfig::ITEMID_BarbWireBarricade:
		return 800;
	case WeaponConfig::ITEMID_WoodShieldBarricade:
	case WeaponConfig::ITEMID_WoodShieldBarricadeZB:
		return 100;
	case WeaponConfig::ITEMID_RiotShieldBarricade:
	case WeaponConfig::ITEMID_RiotShieldBarricadeZB:
	case WeaponConfig::ITEMID_SSBarricadeShield:
		return 400;
	case WeaponConfig::ITEMID_SandbagBarricade:
		return 200;
	}
}
#endif

void obj_ServerBarricade::DestroyBarricade()
{
	setActiveFlag(0);
	g_AsyncApiMgr->AddJob(new CJobDeleteServerObject(this));
}
