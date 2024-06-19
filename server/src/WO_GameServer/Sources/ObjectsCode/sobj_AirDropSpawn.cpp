#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"
#include "XMLHelpers.h"
#include "ServerGameLogic.h"

#include "sobj_AirDropSpawn.h"

IMPLEMENT_CLASS(obj_AirDropSpawn, "obj_AirDropSpawn", "Object");
AUTOREGISTER_CLASS(obj_AirDropSpawn);

obj_AirDropSpawn::obj_AirDropSpawn()
	: spawnRadius(20)
{
	serializeFile = SF_ServerData;	
}

obj_AirDropSpawn::~obj_AirDropSpawn()
{
}

BOOL obj_AirDropSpawn::OnCreate()
{

	ServerGameLogic::AirDropConfigs AirDrop;
	AirDrop.m_radius	= spawnRadius;
	AirDrop.m_location	= GetPosition();
	AirDrop.m_airDropItemID = m_AirDropItemID;
	gServerLogic.SetAirDrop( AirDrop );

	return parent::OnCreate();
}

BOOL obj_AirDropSpawn::OnDestroy()
{
	return parent::OnDestroy();
}

BOOL obj_AirDropSpawn::Update()
{
	return TRUE;
}

// copy from client version
void obj_AirDropSpawn::ReadSerializedData(pugi::xml_node& node)
{
	parent::ReadSerializedData(node);
	pugi::xml_node AirDropSpawnNode = node.child("LootID_parameters");
	GetXMLVal("spawn_radius", AirDropSpawnNode, &spawnRadius);
	GetXMLVal("m_AirDropItemID", AirDropSpawnNode, &m_AirDropItemID);
}
