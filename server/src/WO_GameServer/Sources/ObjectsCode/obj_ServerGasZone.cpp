#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"
#include "XMLHelpers.h"

#include "multiplayer/P2PMessages.h"
#include "ServerGameLogic.h"

#include "obj_ServerGasZone.h"

IMPLEMENT_CLASS(obj_ServerGasArea, "obj_GasArea", "Object");
AUTOREGISTER_CLASS(obj_ServerGasArea);

GasAreaMgr gGasAreaMngr;

obj_ServerGasArea::obj_ServerGasArea()
{
	useRadius = 2.0f;
}

obj_ServerGasArea::~obj_ServerGasArea()
{
}

BOOL obj_ServerGasArea::OnCreate()
{
	parent::OnCreate();
	ObjTypeFlags |= OBJTYPE_GasArea;

	gGasAreaMngr.RegisterGasArea(this);
	return 1;
}

// copy from client version
void obj_ServerGasArea::ReadSerializedData(pugi::xml_node& node)
{
	parent::ReadSerializedData(node);
	pugi::xml_node objNode = node.child("Gas_Area");
	GetXMLVal("useRadius", objNode, &useRadius);
	defaultRadius = useRadius;
}
