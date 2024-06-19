#include "r3dpch.h"
#include "r3d.h"

#include "multiplayer/P2PMessages.h"

#include "obj_Vehicle.h"
#include "obj_VehicleSpawnPoint.h"
#include "GameObjects/VehicleDescriptor.h"
#include "../obj_ServerPlayer.h"
#include "../obj_ServerBarricade.h"
#include "../../GameEngine/ai/AutodeskNav/AutodeskNavMesh.h"
#include "Async_ServerObjects.h"
#include "ObjectsCode/sobj_DroppedItem.h"

#ifdef VEHICLES_ENABLED

IMPLEMENT_CLASS(obj_Vehicle, "obj_Vehicle", "Object");
AUTOREGISTER_CLASS(obj_Vehicle);

CVAR_COMMENT("_vehicles_", "Vehicles");

const float _NetLagSpeedCompensation	= 3.0f;

float _vehicles_MaxSpawnDelay			= 60.0f;
ushort	_vehicles_NumVehicles			= 0;
float _vehicles_VehicleSpawnProtect		= 15.0f;
const static int		VEHICLE_NOT_USED_EXPIRE_TIME = 21 * 24 * 60 * 60; // vehicle will expire if not opened for three weeks
const static int		VEHICLEBOX_LOCKDOWN_TIME = 5;

extern float getWaterDepthAtPos(const r3dPoint3D& pos);

std::vector<obj_Vehicle*> obj_Vehicle::s_ListOfAllActiveVehicles;

obj_Vehicle* obj_Vehicle::GetVehicleById(DWORD vehicleId)
{
	for (std::vector<obj_Vehicle*>::iterator it = obj_Vehicle::s_ListOfAllActiveVehicles.begin(); it != obj_Vehicle::s_ListOfAllActiveVehicles.end(); ++it)
	{
		if (!*it)
			continue;

		if ((*it)->vehicleId == vehicleId)
		{
			return *it;
		}
	}
	
	return NULL;
}

obj_Vehicle::obj_Vehicle()
	: spawnObject(NULL)
	, netMover(this, 0.2f, (float)PKT_C2C_MoveSetCell_s::VEHICLE_CELL_RADIUS)
	, isPlayerDriving(false)
	, vehicleType(VEHICLETYPE_INVALID)
	, vd(NULL)
	, accumDistance(0)
	, curDurability(2000)
	, maxDurability(2000)
	, lastExitTime(0)
	, explosionTime(0)
	, despawnAfterExplosionTime(30)
	, forceExplosionTime(60 * 30)
	, hasExploded(false)
	, destructionRate(1)
	, despawnStarted(false)
	, lastDestructionTick(0)
	, armorExteriorValue(0.0f)
	, armorInteriorValue(0.0f)
	, speedCheckWaitTime(1.0f)
	, movementSpeed(0)
	, lastFuelCheckTime(0.0f)
	, fuelCheckWaitTime(1.0f)
	, maxFuel(0)
	, curFuel(0)
	, hasRunOutOfFuel(false)
	, isRemovingPlayers(false)
	, isEntryAllowed(false)
	, lastDamageTime(0.0f)
	, damageWaitTime(2.0f)
	, isHeadlightsEnabled(false)
	, seatOccupancy(0)
	, lastDriverId(0)
	, lastFuelSendTime(0.0f)
	, fuelSendTimeWait(3.0f)
	, flyingStartTime(0.0f)
	, flyingAllowedTime(15.0f)
	, isFlying(false)
	, lastUnstuckAttemptTime(0.0f)
	, unstuckAttemptTimeWait(2.0f)
	, isReady(false) // this is needed in order for flying check to not false positive on load
	, hasFlyingBeenHandled(false)
	, obstacleId(-1)
	, spawnIndex(-1)
	, vehicleOwnerId(0)
	, m_ItemID(0)
	, m_isLockedByOwner(0)	
	, nextInventoryID(1)
	, maxBoxItems(30) // for now hard coded
	, m_nextVLockdownClear(r3dGetTime() + 60.0f)	
{
	s_ListOfAllActiveVehicles.push_back(this);

	_vehicles_NumVehicles++;	
}

obj_Vehicle::~obj_Vehicle()
{
	for (std::vector<obj_Vehicle*>::iterator it = s_ListOfAllActiveVehicles.begin(); it < s_ListOfAllActiveVehicles.end(); ++it)
	{
		if ((*it)->GetNetworkID() == this->GetNetworkID())
		{
			s_ListOfAllActiveVehicles.erase(it);
			break;
		}
	}

	if (vd)
		delete vd;

	memset( playersInVehicle, 0, sizeof( obj_ServerPlayer* ) * MAX_SEATS );
}

BOOL obj_Vehicle::OnCreate()
{
	ObjTypeFlags |= OBJTYPE_Vehicle;

	r3d_assert(NetworkLocal);
	r3d_assert(GetNetworkID());

	vehicleId = _vehicles_NumVehicles;
	NetworkLocal = false;	
	lastExitTime = r3dGetTime();
	if (srvObjParams_.CustomerID > 0)
		vehicleOwnerId = srvObjParams_.CustomerID;
	else
		vehicleOwnerId = 0;

	if (m_ItemID!=0)
	{		
		if (m_ItemID == 101412)
			vehicleType = obj_Vehicle::VEHICLETYPE_BUGGY;
		else if (m_ItemID == 101413)
			vehicleType = obj_Vehicle::VEHICLETYPE_STRYKER;	
		else if (m_ItemID == 101414)
			vehicleType = obj_Vehicle::VEHICLETYPE_ZOMBIEKILLER;
		else if (m_ItemID == 101418)
			vehicleType = obj_Vehicle::VEHICLETYPE_SCOUTMILITARY;
		else if (m_ItemID == 101419)
			vehicleType = obj_Vehicle::VEHICLETYPE_BUGGYGUN;
		else if (m_ItemID == 101420)
			vehicleType = obj_Vehicle::VEHICLETYPE_SCOUTSURVIVOR;
		else if (m_ItemID == 101421)
			vehicleType = obj_Vehicle::VEHICLETYPE_BONECRUSHER;
		else if (m_ItemID == 101422)
			vehicleType = obj_Vehicle::VEHICLETYPE_SCAVENGERSSCOUT;
		else if (m_ItemID == 101423)
			vehicleType = obj_Vehicle::VEHICLETYPE_CUBUS;
		else if (m_ItemID == 101424)
			vehicleType = obj_Vehicle::VEHICLETYPE_HIPPY;
		else if (m_ItemID == 101425)
			vehicleType = obj_Vehicle::VEHICLETYPE_CONVERTABLE;		
	}	

	if (vehicleType == VEHICLETYPE_STRYKER)
	{
		maxPlayerCount = 9;
		vic = g_pWeaponArmory->getVehicleConfig(101413);
	}
	else if (vehicleType == VEHICLETYPE_BUGGY)
	{
		maxPlayerCount = 3;
		vic = g_pWeaponArmory->getVehicleConfig(101412);
	}
	else if (vehicleType == VEHICLETYPE_ZOMBIEKILLER)
	{
		maxPlayerCount = 2;
		vic = g_pWeaponArmory->getVehicleConfig(101414);
	}
	else if (vehicleType == VEHICLETYPE_SCOUTMILITARY)
	{
		maxPlayerCount = 4;
		vic = g_pWeaponArmory->getVehicleConfig(101418);
	}
	else if (vehicleType == VEHICLETYPE_BUGGYGUN)
	{
		maxPlayerCount = 3;
		vic = g_pWeaponArmory->getVehicleConfig(101419);
	}
	else if (vehicleType == VEHICLETYPE_SCOUTSURVIVOR)
	{
		maxPlayerCount = 2;
		vic = g_pWeaponArmory->getVehicleConfig(101420);
	}
	else if (vehicleType == VEHICLETYPE_BONECRUSHER)
	{
		maxPlayerCount = 6;
		vic = g_pWeaponArmory->getVehicleConfig(101421);
	}
	else if (vehicleType == VEHICLETYPE_SCAVENGERSSCOUT)
	{
		maxPlayerCount = 2;
		vic = g_pWeaponArmory->getVehicleConfig(101422);
	}	
	else if (vehicleType == VEHICLETYPE_CUBUS)
	{
		maxPlayerCount = 2;
		vic = g_pWeaponArmory->getVehicleConfig(101423);
	}
	else if (vehicleType == VEHICLETYPE_HIPPY)
	{
		maxPlayerCount = 8;
		vic = g_pWeaponArmory->getVehicleConfig(101424);
	}
	else if (vehicleType == VEHICLETYPE_CONVERTABLE)
	{
		maxPlayerCount = 4;
		vic = g_pWeaponArmory->getVehicleConfig(101425);
	}

	memset( playersInVehicle, 0, sizeof( obj_ServerPlayer* ) * MAX_SEATS );

	vd = game_new VehicleDescriptor;
	if( vd )
	{
		vd->owner = this;
		LoadXML();
		//char ratios[128];
		//int offset = 0;
		//for(uint32_t i = 0; i < vd->gearsData.mNumRatios; ++i)
		//	offset += sprintf( ratios + offset, "%.3f ", vd->gearsData.mRatios[ i ] );
		//r3dOutToLog("!!! Vehicle Descriptor:\nNum Ratios:%d, Ratios:%s, Final Ratio:%.3f, Peak Torque:%.3f, Max Omega:%.3f, Wheel Radius:%.4f, MaxSpeed:%.3f\n", vd->gearsData.mNumRatios, ratios, vd->gearsData.mFinalRatio, vd->engineData.mPeakTorque, vd->engineData.mMaxOmega, vd->wheelsData[0].wheelData.mRadius, vd->GetMaxSpeed());
		armorExteriorValue = (100.0f - (float)vd->armorExterior) / 100.0f;
		armorInteriorValue = (100.0f - (float)vd->armorExterior) / 100.0f;
		
		maxDurability = vd->durability;
		maxFuel = vd->maxFuel;

		if(vehicleOwnerId!=0)
		{
			curFuel = (int)maxFuel;			
			curDurability = maxDurability;//maxDurability*2;
			distToCreateSq = FLT_MAX;
			distToDeleteSq = FLT_MAX;
		}
		else{
			curFuel = (int)u_GetRandom((float)(maxFuel * 0.75f), (float)maxFuel);
			curDurability = maxDurability; //(int)u_GetRandom((float)maxDurability / 2.0f, (float)maxDurability);
		}
	}
	else
	{
		r3dOutToLog("!!! Failed to allocate Vehicle Descriptor!\n");
	}

	netMover.SetStartCell(GetPosition());
	lastMovementPos = GetPosition();

	CreateNavMeshObstacle();

	gServerLogic.NetRegisterObjectToPeers(this);

	isReady = true;

	return parent::OnCreate();
}

BOOL obj_Vehicle::OnDestroy()
{
	PKT_S2C_DestroyNetObject_s packet;
	packet.spawnID = toP2pNetId(GetNetworkID());
	gServerLogic.p2pBroadcastToActive(this, &packet, sizeof(packet));

	DeleteNavMeshObstacle();	

	//AlexRedd:: vehicle owner system
	if(vehicleOwnerId != 0)
	{
		for (int i = 0; i<ServerGameLogic::MAX_PEERS_COUNT; ++i)
		{
			ServerGameLogic::peerInfo_s& peer = gServerLogic.GetPeer(i);
			if (peer.CustomerID == vehicleOwnerId)
			{
				PKT_S2C_CarMapUpdate_s n2;
				n2.carID = toP2pNetId(GetNetworkID());
				n2.location = GetPosition();
				n2.m_time = 0.0f;
				n2.active = false;
				gServerLogic.p2pSendToPeer(peer.player->peerId_, this, &n2, sizeof(n2));
				break;
			}
		}
	}

	return parent::OnDestroy();
}

DefaultPacket* obj_Vehicle::NetGetCreatePacket(int* out_size)
{
	// r3dOutToLog("!!! Vehicle Info: PeakTorque:%.4f, MaxOmega:%.4f, Armor:%d, Durability:%d, Max Fuel:%d, Weight:%.4f\n", vd->engineData.mPeakTorque, vd->engineData.mMaxOmega, vd->armor, vd->durability, vd->maxFuel, vd->weight);	

	static PKT_S2C_VehicleSpawn_s n;
	n.spawnID = toP2pNetId(GetNetworkID());
	n.position = GetPosition();
	n.rotationX = GetRotationVector().x;
	n.rotationY = GetRotationVector().y;
	n.rotationZ = GetRotationVector().z;
	n.moveCell = netMover.SrvGetCell();
	n.vehicleType = vehicleType;
	n.maxSpeed = 35;
	n.vehicleId = vehicleId;
	n.isCarDrivable = (BYTE)1;

	n.maxDurability = maxDurability;
	n.curDurability = curDurability;
	n.armor	= (int)vd->armorExterior;
	n.peakTorque = (int)vd->engineData.mPeakTorque;
	n.maxOmega = (int)vd->engineData.mMaxOmega;
	n.curFuel = curFuel;
	n.maxFuel = maxFuel;
	n.weight = (int)vd->weight;

	n.isHeadlightsEnabled = isHeadlightsEnabled;

	uint32_t playerCount = 0;
	for (uint32_t i = 0; i < maxPlayerCount; ++i)
	{
		obj_ServerPlayer* player = playersInVehicle[i];
		if (!player)
			continue;

		++playerCount;
		n.playersInVehicle[i] = toP2pNetId(player->GetNetworkID());
	}
	n.hasPlayers = playerCount > 0;
	n.playerCount = (BYTE)playerCount;

	//AlexRedd:: vehicle owner system	
	if (vehicleOwnerId != 0)
	{
		n.vehicleOwnerId = vehicleOwnerId;
		r3dscpy(n.car_owner_user_name, owner_user_name);
		n.isLockedByOwner = m_isLockedByOwner;
		n.isHasItems = vitems.size()>0;

		for (int i = 0; i<ServerGameLogic::MAX_PEERS_COUNT; ++i)
		{
			ServerGameLogic::peerInfo_s& peer = gServerLogic.GetPeer(i);			
			if (peer.CustomerID == vehicleOwnerId && peer.status_ >= ServerGameLogic::PEER_PLAYING)
			{
				PKT_S2C_CarMapUpdate_s n2;
				n2.carID = toP2pNetId(GetNetworkID());
				n2.location = GetPosition();
				n2.m_time = srvObjParams_.ExpireTime;
				n2.active = true;
				gServerLogic.p2pSendToPeer(peer.player->peerId_, this, &n2, sizeof(n2));
				break;
			}
		}
	}
	else
	{
		n.vehicleOwnerId = 0;
		r3dscpy(n.car_owner_user_name, "");
		n.isLockedByOwner = 0;
		n.isHasItems = false;
	}

	*out_size = sizeof(n);

	return &n;
}

wiInventoryItem* obj_Vehicle::FindVehicleItemWithInvID(__int64 invID)
{
	for(size_t i =0; i<vitems.size(); ++i)
	{
		if(vitems[i].InventoryID == invID)
			return &vitems[i];
	}
	return NULL;
}

bool obj_Vehicle::AddItemToVehicleBox(const wiInventoryItem& itm, int quantity)
{
	for(size_t i=0; i<vitems.size(); ++i)
	{
		if(vitems[i].CanStackWith(itm))
		{
			vitems[i].quantity += quantity;

			UpdateVehicleOwnerServerData();
			return true;
		}
	}
	
	if(vitems.size() < maxBoxItems)
	{
		wiInventoryItem itm2 = itm;
		itm2.InventoryID = nextInventoryID++;
		itm2.quantity    = quantity;
		vitems.push_back(itm2);

		UpdateVehicleOwnerServerData();
		return true;
	}

	return false;
}

void obj_Vehicle::RemoveItemFromVehicleBox(__int64 invID, int amount)
{
	r3d_assert(amount >= 0);
	
	for(size_t i=0; i<vitems.size(); ++i)
	{
		if(vitems[i].InventoryID == invID)
		{
			r3d_assert(amount <= vitems[i].quantity);
			vitems[i].quantity -= amount;

			// remove from vehiclebox items array
			if(vitems[i].quantity <= 0)
			{
				vitems.erase(vitems.begin() + i);
			}

			UpdateVehicleOwnerServerData();	
			return;
		}
	}
	
	// invId must be validated first
	r3d_assert(false && "no invid in vehiclebox");
	return;
}

BOOL obj_Vehicle::Update()
{
	parent::Update();

	if (!isActive() || !isReady)
		return TRUE;

	CheckFlying();

	ApplyAutoDamage();

	CheckSpeed();

	if (!HasPlayers())
		return TRUE;

	if (IsInWater())
		return TRUE;

	if (!CheckFuel())
		return TRUE;

	if (!hasExploded && movementSpeed > 0.0f)
		ScanForGeometry();	

	const float curTime = r3dGetTime();
	
	// erase entries with expire lockdown. to avoid large memory usage if every player will try to unlock it :)
	if(curTime > m_nextVLockdownClear)
	{
		m_nextVLockdownClear = curTime + 60.0f;
		
		for(std::vector<vlock_s>::iterator it = m_vlockdowns.begin(); it != m_vlockdowns.end(); )
		{
			if(curTime > it->vlockEndTime) {
				it = m_vlockdowns.erase(it);
			} else {
				++it;
			}
		}

		// keep uses for 5 min, lockEndTime used as vehiclebox opening time
		for(std::vector<vlock_s>::iterator it = m_uses2.begin(); it != m_uses2.end(); )
		{
			if(curTime > it->vlockEndTime + 5 * 60) {
				it = m_uses2.erase(it);
			} else {
				++it;
			}
		}
	}

	return TRUE;
}

void obj_Vehicle::SendContentToPlayer(obj_ServerPlayer* plr)
{
	PKT_S2C_LockboxOpReq_s n2;
	n2.op = PKT_S2C_LockboxOpReq_s::LBOR_StartingToSendContent;
	n2.lockboxID = toP2pNetId(GetNetworkID());
	n2.isVehicle = vehicleOwnerId>0;
	n2.isAirdrop = false;
	n2.airdropTimeLeft = 0.0f;
	gServerLogic.p2pSendToPeer(plr->peerId_, this, &n2, sizeof(n2));	

	PKT_S2C_LockboxContent_s n;
	for(uint32_t i=0; i<vitems.size(); ++i)
	{
		n.item = vitems[i];		
		gServerLogic.p2pSendToPeer(plr->peerId_, this, &n, sizeof(n));
	}

	n2.op = PKT_S2C_LockboxOpReq_s::LBOR_DoneSendingContent;
	n2.lockboxID = toP2pNetId(GetNetworkID());
	gServerLogic.p2pSendToPeer(plr->peerId_, this, &n2, sizeof(n2));	
}

bool obj_Vehicle::IsLockdownActive(const obj_ServerPlayer* plr)
{
	const float curTime = r3dGetTime();
	
	for(size_t i=0; i<m_vlockdowns.size(); i++)
	{
		vlock_s& lock = m_vlockdowns[i];
		if(lock.CustomerID == plr->profile_.CustomerID && curTime < lock.vlockEndTime)
		{
			lock.tries2++;
			// technically user can use issue only one attempt per 1.5 sec (item use time)
			// so we check if user issued them faster that 1sec
			if(lock.tries2 > VEHICLEBOX_LOCKDOWN_TIME)
			{
				gServerLogic.LogCheat(plr->peerId_, PKT_S2C_CheatWarning_s::CHEAT_VehicleBox, false, "VehicleBox",
					"tries %d", lock.tries2);
			}
			return true;
		}
	}

	return false;
}

void obj_Vehicle::SetVLockdown(DWORD CustomerID)
{
	float lockEndTime = r3dGetTime() + VEHICLEBOX_LOCKDOWN_TIME;

	for(size_t i=0; i<m_vlockdowns.size(); i++)
	{
		if(m_vlockdowns[i].CustomerID == CustomerID)
		{
			m_vlockdowns[i].vlockEndTime = lockEndTime;
			m_vlockdowns[i].tries2       = 0;
			return;
		}
	}
	
	vlock_s lock;
	lock.CustomerID  = CustomerID;
	lock.vlockEndTime = lockEndTime;
	lock.tries2       = 0;
	m_vlockdowns.push_back(lock);
}

bool obj_Vehicle::IsVehicleAbused(const obj_ServerPlayer* plr)
{
	const float curTime = r3dGetTime();
	
	for(size_t i=0; i<m_uses2.size(); i++)
	{
		vlock_s& lock = m_uses2[i];
		if(lock.CustomerID == plr->profile_.CustomerID)
		{
			if(curTime > lock.vlockEndTime + 60)
			{
				// used at least minute ago, reset timer (lockEndTime used as vehiclebox opening time)
				lock.vlockEndTime = curTime - 0.001f;
				lock.tries2       = 0;
			}

			// there was a 'possible' dupe method that allowed to fire alot of requests to update vehiclebox
			// hoping that they will be executed in wrong order on API side if was put to different job queues
			lock.tries2++;
			float ups = (float)lock.tries2 / (curTime - lock.vlockEndTime);
			if(lock.tries2 > 10 && ups > 20)
			{
				// on local machine using UI i was able to put about 5 requests per sec, so 20 usages per sec is exploit.
				gServerLogic.LogCheat(plr->peerId_, PKT_S2C_CheatWarning_s::CHEAT_VehicleBox, true, "UsagePerSec (VehicleBox)",
					"tries %d, ups:%.1f", lock.tries2, ups);
				return true;
			}

			return false;
		}
	}

	vlock_s lock;
	lock.CustomerID  = plr->profile_.CustomerID;
	lock.vlockEndTime = curTime;
	lock.tries2       = 1;
	m_uses2.push_back(lock);
	
	return false;
}

bool obj_Vehicle::IsInWater()
{
	float allowedDepth = 1.5f; 
	float waterDepth = getWaterDepthAtPos(GetPosition());
	if (waterDepth > allowedDepth)
	{
		StartDespawn();
		RemovePlayers();
		return true;
	}

	return false;
}

void obj_Vehicle::RemovePlayers()
{
	isRemovingPlayers = true;

	for (uint32_t i = 0; i < maxPlayerCount; ++i) 
	{
		if( !playersInVehicle[i] )
			continue;

		if (!playersInVehicle[i]->IsInVehicle())
		{
			playersInVehicle[i] = 0;
		}
		else
		{
			playersInVehicle[i]->ExitVehicle(true);
		}
	}

	if (!HasPlayers())
		CreateNavMeshObstacle();

	isRemovingPlayers = false;
}

bool obj_Vehicle::CheckFuel()
{
	// let's keep the fuel guage for the players more in sync with the server
	// this is sent every 3 seconds, it's only going to tick down if players are in it
	if (r3dGetTime() > lastFuelSendTime + fuelSendTimeWait)
	{
		lastFuelSendTime = r3dGetTime();
		SendFuelToClients();
	}

	if (r3dGetTime() > lastFuelCheckTime + fuelCheckWaitTime)
	{
		lastFuelCheckTime = r3dGetTime();

		if (curFuel > 0)
		{
			if (movementSpeed > 0)
				curFuel -= 2;
			else
				curFuel -= 1;

			if (curFuel <= 0)
			{
				curFuel = 0;
				OnRunOutOfFuel();
			}
		}
	}

	return curFuel > 0;
}

void obj_Vehicle::CheckSpeed()
{
	if (r3dGetTime() > lastSpeedCheckTime + speedCheckWaitTime)
	{
		lastSpeedCheckTime = r3dGetTime();

		movementSpeed = accumDistance;
		accumDistance = 0.0f;

		if (movementSpeed > vd->GetMaxSpeed() * 1.5f)
		{
			obj_ServerPlayer* player = playersInVehicle[0];
			if (player)
				gServerLogic.LogCheat(player->peerId_, PKT_S2C_CheatWarning_s::CHEAT_FastMove, false, "vehicle_speedcheat", "%f", movementSpeed);
		}
	}
}

void obj_Vehicle::CheckFlying()
{
	float distanceFromGround = CheckDistanceFromGround(GetPosition());
	if (distanceFromGround > 3.0f || distanceFromGround == -1.0f) // if our distance is -1 our distance check went over 2000 units, meaning vehicle is definitely flying, no vehicle is flush on the ground at 0.
	{
		if (!isFlying)
		{
			isFlying = true;
			flyingStartTime = r3dGetTime();
		}
	}
	else 
	{
		isFlying = false;
	}

	if (isFlying && !hasFlyingBeenHandled)
	{
		if (r3dGetTime() > flyingStartTime + flyingAllowedTime)
		{
			hasFlyingBeenHandled = true;

			ForceExplosion();

			obj_ServerPlayer* player = playersInVehicle[0];
			if (!player)
				return;

			gServerLogic.LogCheat(player->peerId_, PKT_S2C_CheatWarning_s::CHEAT_Flying, true, "VehicleFlying",
				"%f %f %f", GetPosition().x, GetPosition().y, GetPosition().z);
		}
	}
}

float obj_Vehicle::CheckDistanceFromGround(r3dPoint3D position)
{
	PxVec3 pos(position.x, position.y + 0.5f, position.z);
	PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_PLAYER_COLLIDABLE_MASK, 0, 0, 0), PxSceneQueryFilterFlag::eSTATIC|PxSceneQueryFilterFlag::eDYNAMIC);

	float MAX_CASTING_DISTANCE = 2000.0f;

	PxRaycastHit hit;
	if (g_pPhysicsWorld->PhysXScene->raycastSingle(pos, PxVec3(0, -1, 0), MAX_CASTING_DISTANCE, PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eINITIAL_OVERLAP|PxSceneQueryFlag::eINITIAL_OVERLAP_KEEP, hit, filter))
		return hit.distance;

	// this should never be the case with a max distance of 2000... 
	// if so, our check flying method kicks for cheating or our unstuck method fails for being under terrain.
	return -1.0f; 
}

void obj_Vehicle::ApplyAutoDamage()
{
	if (!hasExploded)
	{
		// if vehicle has not been exited in N time, start despawn process
		if (vehicleOwnerId==0 && !despawnStarted && r3dGetTime() > lastExitTime + forceExplosionTime && !HasPlayers())
		{
			StartDespawn();
		}
		//AlexRedd:: car spawner
		if (vehicleOwnerId!=0 && !despawnStarted && r3dGetTime() > lastExitTime + VEHICLE_NOT_USED_EXPIRE_TIME && !HasPlayers())
		{
			StartDespawn();
		}

		if (despawnStarted && curDurability >= 300)
			curDurability = 300;

		int dura = GetDurabilityAsPercent();

		// when vehicle starts smoking, continue damaging the vehicle
		if (r3dGetTime() > lastDestructionTick + destructionRate && !hasExploded)
		{
			lastDestructionTick = r3dGetTime();

			if (dura <= 10)
			{
				SetDurability(-15);
			}
			else if (dura <= 20)
			{
				SetDurability(-10);
			}
			else if (dura <= 30 && !HasPlayers())
			{
				SetDurability(-5);
			}
		}
	}	

	if (hasExploded && r3dGetTime() > explosionTime + despawnAfterExplosionTime)
	{
		if (spawnObject)
			spawnObject->OnVehicleDestroyed(this);

		setActiveFlag(0);
	}
}

void obj_Vehicle::ScanForGeometry()
{
	IsHittingGeomtry(GetvForw());
}

void obj_Vehicle::IsHittingGeomtry(r3dPoint3D dir)
{	
	PxBoxGeometry boxTest(1.1f, 0.2f, 2.0f);
	PxTransform boxPose(PxVec3(GetPosition().x, GetPosition().y + 0.7f, GetPosition().z), PxQuat(0, 0, 0, 1));
	PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_PLAYER_COLLIDABLE_MASK, 0, 0, 0), PxSceneQueryFilterFlag::eSTATIC|PxSceneQueryFilterFlag::eDYNAMIC);

	r3dPoint3D testDir = dir.Normalize();
	float MAX_CASTING_DISTANCE = R3D_MIN(3.5f, 3.5f * GetSpeed() / 35.0f);
	if (MAX_CASTING_DISTANCE == 0) MAX_CASTING_DISTANCE = 0.1f;

	bool isBlockHit;
	PxSweepHit sweepHits[32];
	int results = g_pPhysicsWorld->PhysXScene->sweepMultiple(boxTest, boxPose, PxVec3(testDir.x, testDir.y, testDir.z), MAX_CASTING_DISTANCE, PxSceneQueryFlag::eINITIAL_OVERLAP|PxSceneQueryFlag::eINITIAL_OVERLAP_KEEP|PxSceneQueryFlag::eNORMAL, sweepHits, 32, isBlockHit, filter);

	for (int i = 0; i < results; ++i)
	{
		PhysicsCallbackObject* target = NULL;

		if (sweepHits[i].shape && (target = static_cast<PhysicsCallbackObject*>(sweepHits[i].shape->getActor().userData)))
		{
			GameObject* gameObj = target->isGameObject();
			if (!gameObj)
				continue;

			if ( (movementSpeed + _NetLagSpeedCompensation) > 10.0f )
			{
				if( gameObj->isObjType(OBJTYPE_Barricade) )
				{
					obj_ServerBarricade* barricade = (obj_ServerBarricade*)gameObj;
					if (barricade->m_Health > 0.0f)
					{
						SetDurability(-barricade->GetDamageForVehicle(), false);
						barricade->DoDamage( barricade->m_Health + 1 );
					}
				}
				else if (gameObj->isObjType(OBJTYPE_Terrain) && sweepHits[i].distance > 0.1f)
					continue;

				const float damageMultiplier = 50.0f;// from 100.0f;
				float damage = ((movementSpeed / vd->GetMaxSpeed()) * damageMultiplier) * 0.7f;
				SetDurability(-(int)damage, false);
			}
		}
	}
}

obj_ServerPlayer* obj_Vehicle::GetPlayerById(DWORD networkId)
{
	return IsServerPlayer(GameWorld().GetNetworkObject(networkId));
}

bool obj_Vehicle::HasPlayers() const
{
	uint32_t playerCount = 0;
	for(uint32_t i = 0; i < maxPlayerCount; ++i)
	{
		if( playersInVehicle[ i ] )
		{
			r3d_assert(playersInVehicle[i]->IsInVehicle());

			++playerCount;
		}
	}
	return playerCount > 0;
}

void obj_Vehicle::SetVehicleType(VehicleTypes vt)
{
	vehicleType = vt;
}

bool obj_Vehicle::IsVehicleSeatsFull() const
{	
	uint32_t playerCount = 0;
	for(uint32_t i = 0; i < maxPlayerCount; ++i)
	{
		if( playersInVehicle[ i ] )
		{
			r3d_assert(playersInVehicle[i]->IsInVehicle());			
			++playerCount;
		}
	}
	return playerCount == maxPlayerCount;
}

int obj_Vehicle::ReserveFirstAvailableSeat(obj_ServerPlayer* player)
{
	if(!player)
		return -1;

	for(uint32_t i = 0; i < maxPlayerCount; ++i)
	{
		if( !playersInVehicle[ i ] )
		{
			playersInVehicle[ i ] = player;
			return i;
		}
	}

	return -1;
}

void obj_Vehicle::ReleaseSeat(int seatPosition)
{
	r3d_assert(seatPosition>=0);
	if( seatPosition < maxPlayerCount )
		playersInVehicle[ seatPosition ] = 0;
}

bool obj_Vehicle::HasSafeExit(int startAtSeat)
{
	int checks = 0;
	int currentSeat = startAtSeat;
	while (checks < maxPlayerCount)
	{
		if (currentSeat >= maxPlayerCount)
			currentSeat = 0;

		if (IsExitSafe(currentSeat, safeExitPosition))
		{
			return true;
		}

		++currentSeat;
		++checks;
	}

	if (IsExitSafe(4, safeExitPosition))
	{
		safeExitPosition = r3dVector(GetPosition().x, GetPosition().y + 4.0f, GetPosition().z);
		return true;
	}
	return false;
}

bool obj_Vehicle::IsExitSafe(int seatPosition, r3dPoint3D& outPosition)
{
	outPosition = GetExitPosition(seatPosition);

	{
		r3dVector testDirection;
		if (seatPosition == 0)
			testDirection = -GetvRight();
		else if (seatPosition == 1)
			testDirection = GetvRight();
		else
			testDirection = -GetvForw();

		PxVec3 dir(testDirection.x, testDirection.y, testDirection.z);

		PxSweepHit hit;

		r3dVector rootPos = GetPosition();
		PxTransform rootPose(PxVec3(rootPos.x, rootPos.y + 0.5f, rootPos.z));

		r3dVector pos = outPosition;
		PxTransform pose(PxVec3(pos.x, pos.y+1.8f, pos.z));
		PxBoxGeometry boxg(0.5f, 0.1f, 0.5f);

		PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_PLAYER_COLLIDABLE_MASK, 0, 0, 0), PxSceneQueryFilterFlag::eSTATIC);

		if(g_pPhysicsWorld->PhysXScene->sweepSingle(boxg, rootPose, dir, 3.0f, PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eINITIAL_OVERLAP|PxSceneQueryFlag::eINITIAL_OVERLAP_KEEP, hit, filter))
		{
			PhysicsCallbackObject* target = NULL;

			if (hit.shape && (target = static_cast<PhysicsCallbackObject*>(hit.shape->getActor().userData)))
			{
				GameObject* gameObj = target->isGameObject();
				if (!gameObj || !gameObj->isObjType(OBJTYPE_Terrain))
					return false;
			}
			else						
				return false;
		}
		if(!g_pPhysicsWorld->PhysXScene->sweepSingle(boxg, pose, PxVec3(0,-1,0), 50.0f, PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eINITIAL_OVERLAP|PxSceneQueryFlag::eINITIAL_OVERLAP_KEEP, hit, filter))
		{
			return false;
		}
		else
		{
			outPosition = r3dPoint3D(hit.impact.x, hit.impact.y + 0.25f, hit.impact.z);
		}
	}

	return true;
}

void obj_Vehicle::TryUnstuck()
{
	if (r3dGetTime() < lastUnstuckAttemptTime + unstuckAttemptTimeWait || HasPlayers())
		return;

	lastUnstuckAttemptTime = r3dGetTime();

	PxBoxGeometry box(3.0f, 3.0f, 3.0f); 
	PxTransform pose(PxVec3(GetPosition().x, GetPosition().y - 0.5f, GetPosition().z), PxQuat(0,0,0,1));
	PxTransform testPose = pose;
	PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_STATIC_MASK, 0, 0, 0), PxSceneQueryFilterFlag::eSTATIC|PxSceneQueryFilterFlag::eDYNAMIC);

	// do initial test to ensure we are actually stuck.
	if (!DoesPoseOverlapGeometry(box, pose, filter))
		return;

	const float RADIUS = 25.0f;
	const int MAX_TESTS = 10;
	r3dPoint3D pos;
	bool hasTestFailed = false;
	float distanceFromGround;

	// it's possible the vehicle is stuck, attempt to unstuck.
	for (int i = 0; i < MAX_TESTS; ++i)
	{
		hasTestFailed = false;

		float theta = R3D_DEG2RAD(float(i) / float(MAX_TESTS)) * 360.0f;
		pos.Assign(cosf(theta) * RADIUS, 0, sinf(theta) * RADIUS);
		pos += r3dPoint3D(pose.p.x, pose.p.y, pose.p.z);

		testPose.p.x = pos.x;
		testPose.p.y = pos.y;
		testPose.p.z = pos.z;

		hasTestFailed = DoesPoseOverlapGeometry(box, testPose, filter);

		if (!hasTestFailed)
		{
			distanceFromGround = CheckDistanceFromGround(pos);
			if (distanceFromGround == -1.0f)
				hasTestFailed = true;
		}

		if (!hasTestFailed)
			break;
	}

	// we have a valid spot to move to, move it.
	if (!hasTestFailed)
	{
		SetPosition(r3dPoint3D(testPose.p.x, testPose.p.y - distanceFromGround + 0.2f, testPose.p.z));
		netMover.SrvSetCell(GetPosition());

		PKT_S2C_VehicleUnstuck_s n;
		n.isSuccess = true;
		n.position = GetPosition();
		gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
	}
	else
	{
		PKT_S2C_VehicleUnstuck_s n;
		n.isSuccess = false;
		gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
	}
}

bool obj_Vehicle::DoesPoseOverlapGeometry(PxBoxGeometry box, PxTransform pose, PxSceneQueryFilterData filter)
{
	const PxU32 maxHits = 64;
	PxShape* hits[maxHits];

	PxI32 results = g_pPhysicsWorld->PhysXScene->overlapMultiple(box, pose, hits, maxHits, filter);
	if (results == 0)
		return false;
	
	PhysicsCallbackObject* target = NULL;

	for (PxI32 i = 0; i < results; ++i)
	{
		if (hits[i]->userData && (target = static_cast<PhysicsCallbackObject*>(hits[i]->userData)))
		{
			GameObject* gameObj = target->isGameObject();
			// we don't care if our box overlaps with the terrain or the ground plane on devmap
			// todo: this could be cleaned up a bit in order to set the rotation of the vehicle to the current slope angle of the terrain
			if (gameObj && (gameObj->isObjType(OBJTYPE_Terrain) || gameObj->FileName == "data/objectsdepot/editor/groundplane_100x100.sco"))
				continue;
			
			return true;
		}
	}

	return false;
}

int obj_Vehicle::AddPlayerToVehicle(obj_ServerPlayer* player)
{
	r3d_assert(!IsVehicleSeatsFull());

	gServerLogic.LogInfo(player->peerId_, "AddPlayerToVehicle", ""); CLOG_INDENT;
		
	int seatPosition = ReserveFirstAvailableSeat(player);
	if (seatPosition >= 0)
	{
		//r3dOutToLog("!!! Adding player(%u:%s) to seat '%d'\n", player->GetNetworkID(), player->Name.c_str(), seatPosition);
	}

	// we save this information in the event of car bombing. (jumping out of car and letting it explode to kill zombies or players).
	if (seatPosition == 0)
		lastDriverId = player->GetNetworkID();

	despawnStarted = false;
		
	DeleteNavMeshObstacle();

	if(vehicleOwnerId!=0)
		UpdateVehicleOwnerServerData();

	return seatPosition; // driver will be seat position 0
}

void obj_Vehicle::RemovePlayerFromVehicle(obj_ServerPlayer* player)
{
	//r3dOutToLog("!!! Removing player(%u:%s) from seat '%d'\n", player->GetNetworkID(), player->Name.c_str(), player->seatPosition);
	r3d_assert( player );
	r3d_assert( player->IsInVehicle() );
	r3d_assert( player->seatPosition < maxPlayerCount );
	r3d_assert( playersInVehicle[player->seatPosition] == player );

	ReleaseSeat( player->seatPosition );

	lastExitTime = r3dGetTime();
	despawnStarted = false;

	if (!HasPlayers())
		CreateNavMeshObstacle();

	if(vehicleOwnerId!=0)
		UpdateVehicleOwnerServerData();
}

#undef DEFINE_GAMEOBJ_PACKET_HANDLER
#define DEFINE_GAMEOBJ_PACKET_HANDLER(xxx) \
	case xxx: { \
	const xxx##_s&n = *(xxx##_s*)packetData; \
	if(packetSize != sizeof(n)) { \
	r3dOutToLog("!!!!errror!!!! %s packetSize %d != %d\n", #xxx, packetSize, sizeof(n)); \
	return TRUE; \
	} \
	OnNetPacket(n); \
	return TRUE; \
}

#undef DEFINE_GAMEOBJ_PACKET_HANDLER_NOCONST
#define DEFINE_GAMEOBJ_PACKET_HANDLER_NOCONST(xxx) \
	case xxx: { \
	xxx##_s&n = *(xxx##_s*)packetData; \
	if(packetSize != sizeof(n)) { \
	r3dOutToLog("!!!!errror!!!! %s packetSize %d != %d\n", #xxx, packetSize, sizeof(n)); \
	return TRUE; \
	} \
	OnNetPacket(n); \
	return TRUE; \
}



BOOL obj_Vehicle::OnNetReceive(DWORD EventId, const void* packetData, int packetSize)
{
	r3d_assert(!(ObjFlags & OBJFLAG_JustCreated));

	switch(EventId)
	{
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_VehicleMoveSetCell);
		DEFINE_GAMEOBJ_PACKET_HANDLER_NOCONST(PKT_C2C_VehicleMoveRel);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_VehicleAction);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_VehicleHeadlights);
	}

	return FALSE;
}

int obj_Vehicle::GetRandomVehicleTypeForSpawn()
{
	return obj_Vehicle::VEHICLETYPE_BUGGY;
}

void obj_Vehicle::OnNetPacket(const PKT_C2C_VehicleMoveSetCell_s& n)
{
	if(gServerLogic.ginfo_.mapId != GBGameInfo::MAPID_ServerTest && n.pos.Length() < 10)
	{
		obj_ServerPlayer* player = playersInVehicle[0];
		if (player)
		{			
			gServerLogic.LogCheat(player->peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "ZeroTeleport",
				"%f %f %f", 
				n.pos.x, n.pos.y, n.pos.z);
		}
		return;
	}
	
	lastMovementPos = netMover.SrvGetCell();

	// for now we will check ONLY ZX, because somehow players is able to fall down
	r3dPoint3D p1 = netMover.SrvGetCell();
	r3dPoint3D p2 = n.pos;
	p1.y = 0;
	p2.y = 0;
	float dist = (p1 - p2).Length();
	
	if (dist > vd->GetMaxSpeed() * 3.0f) // temporary value, this number does need to be better balanced throughout testing.
	{
		obj_ServerPlayer* player = playersInVehicle[0];
		if (player && player->loadout_->Alive)
		{	
			gServerLogic.LogCheat(player->peerId_, PKT_S2C_CheatWarning_s::CHEAT_FastMove, true, (dist > 500.0f ? "huge_vehicle_teleport" : "vehicle_teleport"),
				"%f, srvGetCell: %.2f, %.2f, %.2f; n.pos: %.2f, %.2f, %.2f", 
				dist, 
				netMover.SrvGetCell().x, netMover.SrvGetCell().y, netMover.SrvGetCell().z, 
				n.pos.x, n.pos.y, n.pos.z
			);
		}
	}
	
	netMover.SetCell(n);

	SetPosition(n.pos);
	UpdatePlayers(n.pos);

	// keep them guaranteed
	RelayPacket(&n, sizeof(n), true);
}

void obj_Vehicle::OnNetPacket(PKT_C2C_VehicleMoveRel_s& n)
{
	const CVehicleNetCellMover::moveData_s& md = netMover.DecodeMove(n);
	
	lastMovementPos = md.pos;

	r3dPoint3D p1 = GetPosition();
	r3dPoint3D p2 = md.pos;
	p1.y = 0;
	p2.y = 0;
	float dist = (p1 - p2).Length();

	n.speed = (int)movementSpeed;

	accumDistance += dist;
		
	SetPosition(md.pos);
	UpdatePlayers(md.pos);
	SetRotationVector(r3dVector(md.rot.x, md.rot.y, md.rot.z));
	
	RelayPacket(&n, sizeof(n), false);
}

uint32_t obj_Vehicle::GetSpawnerItemID(uint32_t ItemId)
{
	r3d_assert(ItemId>0);
	uint32_t ItemID = 0;

	if (ItemId == 101412)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_Buggy;
	else if (ItemId == 101413)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_Stryker;	
	else if (ItemId == 101414)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_ZombieKiller;
	else if (ItemId == 101418)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_ScoutMilitary;
	else if (ItemId == 101419)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_BuggyGun;
	else if (ItemId == 101420)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_ScoutSurvivor;
	else if (ItemId == 101421)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_BoneCrusher;
	else if (ItemId == 101422)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_ScavengersScout;
	else if (ItemId == 101423)		
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_Cubus;
	else if (ItemId == 101424)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_Hippy;
	else if (ItemId == 101425)
		ItemID = WeaponConfig::ITEMID_VehicleSpawner_Convertable;

	return ItemID;
}

void obj_Vehicle::OnNetPacket(const PKT_C2C_VehicleAction_s& n)
{	
	if (n.action == 1)
		TryUnstuck();
	
	//AlexRedd:: vehicle owner system
	else if (n.action == 2) //  lock/unlock
	{
		GameObject* targetObj = GameWorld().GetNetworkObject(n.PlayerID);
		if(obj_ServerPlayer* targetPlr = IsServerPlayer(targetObj))
		{
			if(vehicleOwnerId!=targetPlr->profile_.CustomerID)
				return;

			if(m_isLockedByOwner!=1)
				m_isLockedByOwner = 1;
			else 
				m_isLockedByOwner = 0;		

			PKT_C2C_VehicleAction_s n;
			n.action = 4;
			n.isLock = m_isLockedByOwner;
			gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));

			UpdateVehicleOwnerServerData();
		}
	}
	else if (n.action == 3)// pick up
	{		
		if(vehicleOwnerId!=0)
		{
			GameObject* targetObj = GameWorld().GetNetworkObject(n.PlayerID);
			if(obj_ServerPlayer* targetPlr = IsServerPlayer(targetObj))
			{
				if(vehicleOwnerId!=targetPlr->profile_.CustomerID)
					return;

				if(vitems.size()>0)	
				{
					PKT_C2C_VehicleAction_s n;
					n.action = 7;
					n.isHasItemsInsideBox = true;
					gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
					return;
				}

				wiInventoryItem wi;
				wi.itemID   = GetSpawnerItemID(m_ItemID);
				wi.quantity = 1;
				wi.Var3 = wiInventoryItem::MAX_DURABILITY;//GetDurability(); //todo: durability and fuel converter
				if(targetPlr->BackpackAddItem(wi))
				{
					if (spawnObject)
						spawnObject->OnVehicleDestroyed(this);

					DestroyVehicle();

					PKT_C2C_VehicleAction_s n;
					n.action = 5;	
					gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));					
				}
			}
		}
	}
	else
		RelayPacket(&n, sizeof(n), false);	
}

void obj_Vehicle::OnNetPacket(const PKT_C2C_VehicleHeadlights_s& n)
{
	isHeadlightsEnabled = n.isHeadlightsEnabled;

	RelayPacket(&n, sizeof(n), false);
}

void obj_Vehicle::RelayPacket(const DefaultPacket* packetData, int packetSize, bool guaranteedAndOrdered)
{
	DWORD peerId = -1;
	obj_ServerPlayer* player = playersInVehicle[0];
	if (player)
		peerId = player->peerId_;
	gServerLogic.RelayPacket(peerId, this, packetData, packetSize, guaranteedAndOrdered);
}


void obj_Vehicle::UpdatePlayers(r3dPoint3D pos)
{
	for (uint32_t i = 0; i < maxPlayerCount; ++i)
	{
		if (playersInVehicle[i])
		{
			r3d_assert(playersInVehicle[i]->IsInVehicle());
			playersInVehicle[i]->UpdatePosition(pos);
			playersInVehicle[i]->netMover.SrvSetCell(netMover.SrvGetCell());
			playersInVehicle[i]->lastPlayerAction_ = r3dGetTime();
		}
	}
}

r3dPoint3D obj_Vehicle::GetExitPosition(int seat)
{
	r3dVector position;
	
	if (seat == 0)
		position = GetPosition() + -(GetvRight() * 2.7f);
	else if (seat == 1)
		position = GetPosition() + (GetvRight() * 2.7f);
	else
		position = GetPosition() + -(GetvForw() * 3.4f);

	return position;
}

int obj_Vehicle::GetDurability()
{
	return curDurability;
}

int obj_Vehicle::GetDurabilityAsPercent()
{
	return (int)((float)curDurability / (float)maxDurability * 100.0f);
}

void obj_Vehicle::SetDurability(int val, bool isForced)
{
	if (!isForced && val < 0)
	{
		if(r3dGetTime() > lastDamageTime + damageWaitTime)
			lastDamageTime = r3dGetTime();
		else
			return;
	}

	curDurability += val;

	if (curDurability > maxDurability)
		curDurability = maxDurability;

	if (curDurability <= 0)
	{
		curDurability = 0;
		OnExplode();
	}

	PKT_S2C_VehicleDurability_s n;
	n.durability = curDurability;

	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));

	if(vehicleOwnerId!=0)
		UpdateVehicleOwnerServerData();
}


void obj_Vehicle::StartDespawn()
{
	if (despawnStarted)
		return;

	despawnStarted = true;

	if (curDurability > 300)
		curDurability = 300;

	PKT_S2C_VehicleDurability_s n;
	n.durability = curDurability;

	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
}

void obj_Vehicle::ForceExplosion()
{
	if (despawnStarted)
		return;

	despawnStarted = true;

	curDurability = 5;

	PKT_S2C_VehicleDurability_s n;
	n.durability = curDurability;

	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
}

void obj_Vehicle::OnExplode()
{
	hasExploded = true;
	explosionTime = r3dGetTime();

	for (uint32_t i = 0; i < maxPlayerCount; ++i)
	{
		obj_ServerPlayer* player = playersInVehicle[i];
		if (player)
		{
			gServerLogic.ApplyDamage(this, player, GetPosition(), 1000.0f, true, storecat_Vehicle, vehicleId);
			player->ExitVehicle(true, true);
		}
	}

	gServerLogic.DoExplosion(this, this, R3D_ZERO_VECTOR, R3D_ZERO_VECTOR, 360.0f, 10.0f, 250.0f, storecat_Vehicle, vehicleId, true);
	gServerLogic.InformZombiesAboutVehicleExplosion(this);

	if(vehicleOwnerId!=0)
	{
		if(vitems.size()>0 && !despawnStarted)	
		{
			// drop all items	
			for(size_t i=0; i<vitems.size(); ++i)
			{
				const wiInventoryItem& wi = vitems[i];
				if(wi.itemID > 0)
				{
					r3d_assert(wi.itemID);
					// create network object
					obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", GetRandomPosForVehicleItemDrop());
					obj->SetNetworkID(gServerLogic.GetFreeNetId());
					obj->NetworkLocal = true;
					//vars
					obj->m_Item       = wi;
					obj->m_Item.quantity = wi.quantity;
				}
			}
		}
		g_AsyncApiMgr->AddJob(new CJobDeleteServerObject(this));
	}
}

r3dPoint3D obj_Vehicle::GetRandomPosForVehicleItemDrop()
{
	// create random position around player
	r3dPoint3D pos = GetPosition();
	pos.y += 0.4f;
	pos.x += u_GetRandom(-1, 1);
	pos.z += u_GetRandom(-1, 1);
	
	return pos;
}

bool obj_Vehicle::ApplyDamage(GameObject* fromObj, float damage, STORE_CATEGORIES damageSource)
{
	//if(gServerLogic.ginfo_.gameServerId >=11000 && gServerLogic.ginfo_.gameServerId < 12000)
		//return false;

	if (damageSource == storecat_GRENADE || damageSource == storecat_SUPPORT) // double damage for grenades/rockets against vehicles 
	{
		if(vehicleOwnerId!=0 && m_isLockedByOwner!=0)
		{
			gServerLogic.InformZombiesAboutVehicleExplosion(this);//call the zombies
			//spawn alarm effects
			PKT_C2C_VehicleAction_s n;
			n.action = 6;
			gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
			return false;
		}

		float modifier = damageSource == storecat_SUPPORT ? 10.0f:20.0f;
		if (vehicleType == VEHICLETYPE_STRYKER)
			modifier = damageSource == storecat_SUPPORT ? 5.0f:10.0f;
		if(vehicleOwnerId!=0)
			modifier = damageSource == storecat_SUPPORT ? 2.0f:5.0f;
		SetDurability(-(int)(damage * modifier));
	}	
	else
	{
		if(vehicleOwnerId!=0)		
			damage = damage*0.5f;
		
		SetDurability(-(int)(damage * armorExteriorValue));

		if(vehicleOwnerId!=0)
			UpdateVehicleOwnerServerData();
	}

	if (GetDurability() <= 0)
		return true; // return true if destroyed

	// do not cause any damage to players inside a stryker, or on vehicles that have health above their ignore threshold
	if (vehicleType == VEHICLETYPE_STRYKER || 
		GetDurabilityAsPercent() > vd->thresholdIgnoreMelee || 
		GetDurabilityAsPercent() > vd->thresholdIgnoreMelee)
		return false;

	for (uint32_t i = 0; i < maxPlayerCount; ++i)
	{
		if (!playersInVehicle[i])
			continue;

		// players should take more damage from other players, but less from zombies
		obj_ServerPlayer* player = playersInVehicle[i];	
		if (player)
		{
			r3d_assert(player->currentVehicleId == vehicleId);
			r3d_assert(player->IsInVehicle());

			float appliedDamage = 0;
			if (fromObj->isObjType(OBJTYPE_Zombie))
				appliedDamage = u_GetRandom(0.0f, 2.0f);
			else if (fromObj->isObjType(OBJTYPE_Human))
				appliedDamage = u_GetRandom(0.0f, 4.0f);
			
			gServerLogic.ApplyDamage(fromObj, player, GetPosition(), appliedDamage, true, damageSource, vehicleId, false);
		}
	}

	return false;
}

void obj_Vehicle::DestroyVehicle()
{
	setActiveFlag(0);
	g_AsyncApiMgr->AddJob(new CJobDeleteServerObject(this));
}

int obj_Vehicle::GetVehicleType()
{
	return vehicleType;
}

void obj_Vehicle::OnRunOutOfFuel()
{
	if (hasRunOutOfFuel)
		return;

	hasRunOutOfFuel = true;

	SendFuelToClients();
}

void obj_Vehicle::AddFuel(int amount)
{
	curFuel += amount;

	if (curFuel > maxFuel)
		curFuel	 = maxFuel;

	if (curFuel > 0)
		hasRunOutOfFuel = false;

	SendFuelToClients();

	if(vehicleOwnerId!=0)
		UpdateVehicleOwnerServerData();
}

void obj_Vehicle::SendFuelToClients()
{
	PKT_S2C_VehicleFuel_s n;
	n.fuel = curFuel;
	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
}

int obj_Vehicle::GetFuel()
{
	return curFuel;
}

int obj_Vehicle::GetSpeed()
{
	return (int)movementSpeed;
}

void obj_Vehicle::LoadXML()
{
	if( !vic )
	{
		r3dOutToLog("!!! Failed to load vehicle definition file!  Vehicle Info Config is un-initialized or not found!\n");
		return;
	}
	if( !vd )
	{
		r3dOutToLog("!!! Failed to load vehicle definition file!  Vehicle Descriptor is un-initialized!\n");
		return;
	}

	vd->driveFileDefinitionPath = "Data/ObjectsDepot/Vehicles/";
	vd->driveFileDefinitionPath += vic->m_FNAME;
	vd->driveFileDefinitionPath += "_DriveData.xml";

	r3dFile* f = r3d_open(vd->driveFileDefinitionPath.c_str(), "rb");
	if ( !f )
	{
		r3dOutToLog("Failed to open vehicle definition file: %s\n", vd->driveFileDefinitionPath.c_str());
		return;
	}

	char* fileBuffer = game_new char[f->size + 1];
	fread(fileBuffer, f->size, 1, f);
	fileBuffer[f->size] = 0;

	pugi::xml_document xmlDoc;
	pugi::xml_parse_result parseResult = xmlDoc.load_buffer_inplace(fileBuffer, f->size);
	fclose(f);
	if (!parseResult)
		r3dError("Failed to parse XML, error: %s", parseResult.description());
	vd->ReadSerializedData( xmlDoc );
	delete [] fileBuffer;
}

void obj_Vehicle::CreateNavMeshObstacle()
{
	if (obstacleId >= 0)
		return;

	r3dBoundBox obb;
	obb.Size = r3dPoint3D(3.5f, 2.0f, 3.5f); // this is enough space to allow a player to at least have a few extra seconds to get into a "parked" vehicle.
	obb.Org  = r3dPoint3D(GetPosition().x - obb.Size.x/2, GetPosition().y, GetPosition().z - obb.Size.z/2);
	obstacleId = gAutodeskNavMesh.AddObstacle(this, obb, GetRotationVector().y);

	obstacleRadius = R3D_MAX(obb.Size.x, obb.Size.z) / 2;
}

void obj_Vehicle::DeleteNavMeshObstacle()
{
	if(obstacleId >= 0)
	{
		gAutodeskNavMesh.RemoveObstacle(obstacleId);
		obstacleId = -1;
	}
}

//AlexRedd:: car spawner
void obj_Vehicle::LoadServerObjectData()
{
	m_ItemID = srvObjParams_.ItemID;
	vehicleOwnerId = srvObjParams_.CustomerID;	
	r3dscpy(owner_user_name, srvObjParams_.Var2.c_str());	

	// deserialize from xml
	IServerObject::CSrvObjXmlReader xml(srvObjParams_.Var1);	
	curDurability = xml.xmlObj.attribute("Durability").as_int();
	curFuel = xml.xmlObj.attribute("Fuel").as_int();
	m_isLockedByOwner   = xml.xmlObj.attribute("isLocked").as_int();	
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
		vitems.push_back(it);

		xmlItem = xmlItem.next_sibling();		
	}	
}

void obj_Vehicle::SaveServerObjectData()
{
	srvObjParams_.ItemID = m_ItemID;		
	srvObjParams_.Var2 = owner_user_name;
	
	IServerObject::CSrvObjXmlWriter xml;	
	xml.xmlObj.append_attribute("Durability") = curDurability;
	xml.xmlObj.append_attribute("Fuel") = curFuel;
	xml.xmlObj.append_attribute("isLocked") = m_isLockedByOwner;
	xml.xmlObj.append_attribute("numItems") = vitems.size();
	for(size_t i=0; i<vitems.size(); ++i)
	{
		pugi::xml_node xmlItem = xml.xmlObj.append_child();
		xmlItem.set_name("item");
		xmlItem.append_attribute("id") = vitems[i].itemID;
		xmlItem.append_attribute("q") = vitems[i].quantity;
		xmlItem.append_attribute("v1") = vitems[i].Var1;
		xmlItem.append_attribute("v2") = vitems[i].Var2;
		xmlItem.append_attribute("v3") = vitems[i].Var3;		
	}
	xml.save(srvObjParams_.Var1);	
}

void obj_Vehicle::UpdateVehicleOwnerServerData()
{
	srvObjParams_.ExpireTime = lastExitTime + VEHICLE_NOT_USED_EXPIRE_TIME;
	g_AsyncApiMgr->AddJob(new CJobUpdateServerObjectVehicle(this));
}
////
#endif
