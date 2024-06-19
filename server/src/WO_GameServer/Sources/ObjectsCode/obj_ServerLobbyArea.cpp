#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"
#include "XMLHelpers.h"

#include "multiplayer/P2PMessages.h"
#include "ServerGameLogic.h"

#include "obj_ServerLobbyArea.h"

IMPLEMENT_CLASS(obj_ServerLobbyArea, "obj_LobbyArea", "Object");
AUTOREGISTER_CLASS(obj_ServerLobbyArea);

LobbyAreaMgr gLobbyAreaMngr;

obj_ServerLobbyArea::obj_ServerLobbyArea()
{
	useRadius = 2.0f;
}

obj_ServerLobbyArea::~obj_ServerLobbyArea()
{
}

BOOL obj_ServerLobbyArea::OnCreate()
{
	parent::OnCreate();

	gLobbyAreaMngr.RegisterLobbyArea(this);
	return 1;
}

// copy from client version
void obj_ServerLobbyArea::ReadSerializedData(pugi::xml_node& node)
{
	parent::ReadSerializedData(node);
	pugi::xml_node objNode = node.child("lobby_area");
	GetXMLVal("useRadius", objNode, &useRadius);
}
