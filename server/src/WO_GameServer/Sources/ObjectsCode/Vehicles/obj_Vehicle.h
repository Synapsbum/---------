#pragma once

#include "GameCommon.h"
#include "multiplayer/NetCellMover.h"
#include "NetworkHelper.h"
#include "obj_Vehicle.h"
#include "ObjectsCode/WEAPONS/WeaponArmory.h"

#ifdef VEHICLES_ENABLED

class obj_VehicleSpawnPoint;
class obj_ServerPlayer;
struct VehicleDescriptor;

const uint32_t MAX_SEATS = 9;

class VehicleSeat
{
public:
	VehicleSeat();
	~VehicleSeat();

	DWORD playerId;
	int id;
	r3dVector offset;
};

class obj_Vehicle : public GameObject, INetworkHelper
{
	DECLARE_CLASS(obj_Vehicle, GameObject)

	const VehicleInfoConfig* vic;
	//AlexRedd:: car spawner
	DWORD		vehicleOwnerId;// OwnerId of Vehicle
	uint32_t	m_ItemID; // itemID of Vehicle	
	char		owner_user_name[128];
	int			m_isLockedByOwner;	

	std::vector<wiInventoryItem> vitems; // all items in this car
	uint32_t	maxBoxItems; // max items this car can hold

	int		nextInventoryID;
	///

	float wheel0Rotation[2];
	float wheel1Rotation[2];
	float wheel2Rotation[2];
	float wheel3Rotation[2];

	float wheelPosition[4];

	bool hasExploded;
	float forceExplosionTime;
	float lastExitTime;
	float explosionTime;
	float despawnAfterExplosionTime;

	float lastSpeedCheckTime;
	float speedCheckWaitTime;
	float movementSpeed;

	r3dVector lastMovementPos;

	float lastFuelCheckTime;
	float fuelCheckWaitTime;
	int maxFuel;
	int curFuel;
	bool hasRunOutOfFuel;

	bool isRemovingPlayers;
	bool isEntryAllowed;

	float lastDamageTime;
	float damageWaitTime;

	bool isHeadlightsEnabled;

	uint32_t seatOccupancy;

	float lastFuelSendTime;
	float fuelSendTimeWait;

	bool isReady;
	bool hasFlyingBeenHandled;

	float lastUnstuckAttemptTime;
	float unstuckAttemptTimeWait;
	bool DoesPoseOverlapGeometry(PxBoxGeometry box, PxTransform pose, PxSceneQueryFilterData filter);
	void ForceExplosion();

	void CreateNavMeshObstacle();
	void DeleteNavMeshObstacle();
public:
	// This must match client for loading of vehicles
	enum VehicleTypes
	{
		VEHICLETYPE_INVALID = -1,
		VEHICLETYPE_BUGGY = 0,
		VEHICLETYPE_STRYKER,
		VEHICLETYPE_ZOMBIEKILLER,
		VEHICLETYPE_SCOUTMILITARY,
		VEHICLETYPE_BUGGYGUN,
		VEHICLETYPE_SCOUTSURVIVOR,
		VEHICLETYPE_BONECRUSHER,
		VEHICLETYPE_SCAVENGERSSCOUT,		
		VEHICLETYPE_CUBUS,
		VEHICLETYPE_HIPPY,
		VEHICLETYPE_CONVERTABLE

	};

	static r3dgameVector(obj_Vehicle*) s_ListOfAllActiveVehicles;	
	obj_ServerPlayer* playersInVehicle[MAX_SEATS];	

	obj_VehicleSpawnPoint* spawnObject;
	int spawnIndex;

	CVehicleNetCellMover netMover;
	
	int obstacleId;
	float obstacleRadius;
	uint32_t vehicleIdForFF;
	DWORD lastDriverId;
	DWORD vehicleId;
	uint16_t maxPlayerCount;
	bool isPlayerDriving;
	uint32_t GetSpawnerItemID(uint32_t ItemId);
	
	VehicleTypes vehicleType;

	VehicleDescriptor* vd;

	float maxVelocity;
	float accumDistance;

	void SetVehicleType(VehicleTypes vt);
	int GetVehicleType();

	bool HasPlayers() const;
	bool IsVehicleSeatsFull() const;
	int ReserveFirstAvailableSeat(obj_ServerPlayer* player);
	void ReleaseSeat(int seatPosition);
	r3dVector safeExitPosition;
	bool HasSafeExit(int seatPosition);
	bool IsExitSafe(int seatPosition, r3dPoint3D& outPosition);
	
	int GetSpeed();

	int AddPlayerToVehicle(obj_ServerPlayer* player);
	void RemovePlayerFromVehicle(obj_ServerPlayer* player);
	void RemovePlayers();

	void StartEngine();
	void StopEngine();

	void StartMoving();
	void StopMoving();
	void StartBreaking();
	void StopBreaking();

	void CheckSpeed();
	bool ApplyDamage(GameObject* fromObj, float damage, STORE_CATEGORIES damageSource);

	int GetRandomVehicleTypeForSpawn();

	obj_ServerPlayer* GetPlayerById(DWORD playerId);

	r3dPoint3D GetExitPosition(int seat);

	int maxDurability;
	int curDurability;
	int GetDurability();
	int GetDurabilityAsPercent();
	void SetDurability(int val, bool isForced = true);

	float destructionRate;
	float lastDestructionTick;
	bool despawnStarted;
	void StartDespawn();

	float armorExteriorValue;
	float armorInteriorValue;

	bool CheckFuel();
	void AddFuel(int amount);
	int GetFuel();
	void SendFuelToClients();

	r3dVector GetvForw() const 
	{ 
		D3DXMATRIX m = GetRotationMatrix();
		return r3dVector(m._31, m._32, m._33); 
	}
	r3dVector GetvRight() const 
	{ 
		D3DXMATRIX m = GetRotationMatrix();
		return r3dVector(m._11, m._12, m._13); 
	}
	
	void OnDamage();
	void OnExplode();
	void OnRepair();
	void OnRunOutOfFuel();

	void ApplyAutoDamage();
	void ScanForGeometry();

	void IsHittingGeomtry(r3dPoint3D dir);

	bool IsInWater();

	bool isFlying;
	float flyingStartTime;
	float flyingAllowedTime;
	void CheckFlying();
	float CheckDistanceFromGround(r3dPoint3D position);

	void UpdatePlayers(r3dPoint3D pos);

	void TryUnstuck();

	// security lockdown list. per user.
	struct vlock_s
	{
	  DWORD		CustomerID;
	  float		vlockEndTime;
	  int		tries2;
	};
	std::vector<vlock_s> m_vlockdowns;
	std::vector<vlock_s> m_uses2;		// used for tracking usage-per-seconds for vehicleboxes, vlockEndTime used as vehiclebox opening time
	float		m_nextVLockdownClear;

	r3dPoint3D	GetRandomPosForVehicleItemDrop();

public:
	
	static obj_Vehicle* GetVehicleById(DWORD vehicleId);

	obj_Vehicle();
	~obj_Vehicle();

	virtual BOOL OnCreate();
	virtual BOOL OnDestroy();
	virtual BOOL Update();

	void		SendContentToPlayer(obj_ServerPlayer* plr);
	bool		IsLockdownActive(const obj_ServerPlayer* plr);
	void		SetVLockdown(DWORD CustomerID);
	bool		IsVehicleAbused(const obj_ServerPlayer* plr);

	wiInventoryItem* FindVehicleItemWithInvID(__int64 invID);
	bool		AddItemToVehicleBox(const wiInventoryItem& itm, int quantity);
	void		RemoveItemFromVehicleBox(__int64 invID, int amount);

	INetworkHelper* GetNetworkHelper() { return dynamic_cast<INetworkHelper*>(this); }
	DefaultPacket* INetworkHelper::NetGetCreatePacket(int* out_size);
	int		GetServerObjectSerializationType() { return 3; } 
	void INetworkHelper::LoadServerObjectData();// { r3dError("not implemented."); }
	void INetworkHelper::SaveServerObjectData();// { r3dError("not implemented."); }
	void		DestroyVehicle();
	void		UpdateVehicleOwnerServerData();	

	virtual BOOL OnNetReceive(DWORD EventId, const void* packetData, int packetSize);
	void		OnNetPacket(const PKT_C2C_VehicleMoveSetCell_s& n);
	void		OnNetPacket(PKT_C2C_VehicleMoveRel_s& n);
	void		OnNetPacket(const PKT_C2C_VehicleAction_s& n);
	void		OnNetPacket(const PKT_C2C_VehicleHeadlights_s& n);

	void		RelayPacket(const DefaultPacket* packetData, int packetSize, bool guaranteedAndOrdered = true);

	void LoadXML();
};

#endif
