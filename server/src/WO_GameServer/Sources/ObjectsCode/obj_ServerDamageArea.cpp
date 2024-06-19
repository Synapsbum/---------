#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"

#include "multiplayer/P2PMessages.h"

#include "ObjectsCode/obj_ServerDamageArea.h"
#include "ObjectsCode/obj_ServerPlayer.h"

#include "ServerGameLogic.h"

const float c_fAreaDamageInterval = 1.0f; // hard coded to 1 second, as in editor we set damage per second

IMPLEMENT_CLASS(obj_ServerDamageArea, "obj_DamageArea", "Object");
AUTOREGISTER_CLASS(obj_ServerDamageArea);
DamageAreaMgr gDamageAreaMngr;

obj_ServerDamageArea::obj_ServerDamageArea()
{
	nextScan_   = 0;
	m_Radius = 0;
	m_Damage = 0;	
}

obj_ServerDamageArea::~obj_ServerDamageArea()
{
}

BOOL obj_ServerDamageArea::Load(const char *fname)
{
	return parent::Load(fname);
}

BOOL obj_ServerDamageArea::OnCreate()
{
	parent::OnCreate();
	ObjFlags |= OBJFLAG_SkipCastRay;

	gDamageAreaMngr.RegisterDamageArea(this);
	return 1;
}

BOOL obj_ServerDamageArea::OnDestroy()
{
	return parent::OnDestroy();
}

void obj_ServerDamageArea::ScanForPlayers()
{
	const float curTime = r3dGetTime();

	for(int i = 0; i < gServerLogic.curPlayers_; i++) 
	{
		obj_ServerPlayer* plr = gServerLogic.plrList_[i];
		if(plr == NULL)
			continue;

		if (plr->loadout_->Alive == 0)
			continue;		

		// check radius
		float distSq = (plr->GetPosition() - GetPosition()).LengthSq();		
		if(distSq > (m_Radius * m_Radius))
			continue;
		
		// check for gas mask id's
		if (plr->loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20177 ||
			plr->loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20178 ||
			plr->loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20206 ||
			plr->loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20217)
			continue;
		
		// check for god mode
		if (plr->m_isAdmin_GodMode)
			continue;

		// check for spawn protection
		 if(r3dGetTime() < plr->m_SpawnProtectedUntil)
			continue;
		 
		// apply toxic
		plr->loadout_->Toxic += m_Damage;
		if (plr->loadout_->Toxic > 100.0f)
		{
			plr->loadout_->Toxic = 100.0f;
			plr->loadout_->Health = 0.0f;
		}

		//r3dOutToLog("DamageArea %u: %s got %f damage\n", GetHashID(), plr->userName, m_Damage);
		gServerLogic.ApplyDamageToPlayer(plr, plr, plr->GetPosition(), m_Damage, 0, 0, false, storecat_DamageArea, 0);		
		
		plr->m_InDamageAreaTime = r3dGetTime() + c_fAreaDamageInterval;		
	}

	return;
}

BOOL obj_ServerDamageArea::Update()
{
	const float curTime = r3dGetTime();
	if(curTime > nextScan_)
	{
		nextScan_ = curTime + c_fAreaDamageInterval;
		ScanForPlayers();
	}

	return TRUE;
}

void obj_ServerDamageArea::ReadSerializedData(pugi::xml_node& node)
{
	GameObject::ReadSerializedData(node);
	pugi::xml_node damageAreaNode = node.child("damageArea");
	m_Radius = damageAreaNode.attribute("radius").as_float();
	m_Damage = damageAreaNode.attribute("damage").as_float();	
}
