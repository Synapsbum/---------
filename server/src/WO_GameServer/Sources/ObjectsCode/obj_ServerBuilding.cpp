#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"

#include "multiplayer/P2PMessages.h"
#include "ServerGameLogic.h"

#include "obj_ServerBuilding.h"

IMPLEMENT_CLASS(obj_ServerBuilding, "obj_Building", "Object");
AUTOREGISTER_CLASS(obj_ServerBuilding);

obj_ServerBuilding::obj_ServerBuilding()
{
	ObjFlags |= OBJFLAG_AddToDummyWorld;
}

obj_ServerBuilding::~obj_ServerBuilding()
{
}

BOOL obj_ServerBuilding::Load(const char* name)
{
	if (parent::Load(name))
	{
		// set temp bbox
		//r3dOutToLog("Creating: %s\n", name);
		r3dBoundBox bboxLocal;
		bboxLocal.Org = r3dPoint3D(-0.5, -0.5, -0.5);
		bboxLocal.Size = r3dPoint3D(+0.5, +0.5, +0.5);
		SetBBoxLocal(bboxLocal);

		return TRUE;
	}

	return FALSE;
}

BOOL obj_ServerBuilding::OnCreate()
{
	distToCreateSq = 600 * 600; // 512 * 512;
	distToDeleteSq = 256 * 256;
	parent::OnCreate();
	NetworkLocal = true; // true

	// Sleep(25);

	return 1;
}

BOOL obj_ServerBuilding::Update()
{
	return parent::Update();
}

BOOL obj_ServerBuilding::OnDestroy()
{
	return parent::OnDestroy();
}

void obj_ServerBuilding::ReadSerializedData(pugi::xml_node& node)
{
	parent::ReadSerializedData(node);
}

DefaultPacket* obj_ServerBuilding::NetGetCreatePacket(int* out_size)
{
	static PKT_S2C_CreateScenario_s n;
	n.spawnID = 0;
	n.pos = GetPosition();
	r3dscpy(n.fname, FileName.c_str());
	n.angle = GetRotationVector();
	n.scale = GetScale();
	n.objflags = GetObjFlags();
	n.Illum = GetSelfIllumMultiplier();
	n.bulletP = GetBulletPierceable();
	n.isPhys = GetPhysEnable() ? true : false;
	n.minQ = GetMinQuality();

	*out_size = sizeof(n);

	//RakSleep(10);

	return &n;
}