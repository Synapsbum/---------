#include "r3dpch.h"
#include "r3d.h"

#include "MasterServerLogic.h"
#include "obj_ServerPlayer.h"

#include "ServerWeapons/ServerWeapon.h"
#include "ServerWeapons/ServerGear.h"
#include "../EclipseStudio/Sources/ObjectsCode/weapons/WeaponArmory.h"
#include "../EclipseStudio/Sources/ObjectsCode/Gameplay/ZombieStates.h"

#include "multiplayer/P2PMessages.h"
#include "ServerGameLogic.h"

#include "ObjectsCode/obj_ServerPostBox.h"
#include "ObjectsCode/sobj_DroppedItem.h"
#include "ObjectsCode/sobj_Note.h"
#include "ObjectsCode/obj_ServerBarricade.h"
#include "ObjectsCode/obj_ServerGravestone.h"
#include "ObjectsCode/obj_ServerGrenade.h"
#include "ObjectsCode/obj_ServerLockbox.h"
#include "ObjectsCode/obj_ServerAirdrop.h"
#include "ObjectsCode/obj_ServerFarmBlock.h"
#include "ObjectsCode/obj_ServerNPC.h"
#include "ObjectsCode/Zombies/sobj_Zombie.h"
#include "ObjectsCode/obj_ServerDamageArea.h"
#include "ObjectsCode/obj_ServerGasZone.h"//AlexRedd:: BR mode
#include "ObjectsCode/obj_ServerPlayerSpawnPoint.h"//AlexRedd:: BR mode
#include "../EclipseStudio/Sources/Backend/WOBackendAPI.h"//AlexRedd:: Vip items (nodrop)

#include "loadout.h"

#ifdef VEHICLES_ENABLED
#include "ObjectsCode/Vehicles/obj_Vehicle.h"
#endif

#include "AsyncFuncs.h"
#include "Async_ServerObjects.h"

#include "../EclipseStudio/Sources/Gameplay_Params.h"
extern CGamePlayParams		GPP_Data;

extern bool IsNullTerminated(const char* data, int size);
extern wiInventoryItem RollItem(const LootBoxConfig* lootCfg, int depth); //AirDrop //AlexRedd:: BR mode

extern float getWaterDepthAtPos(const r3dPoint3D& pos);

IMPLEMENT_CLASS(obj_ServerPlayer, "obj_ServerPlayer", "Object");
AUTOREGISTER_CLASS(obj_ServerPlayer);

CVAR_COMMENT("_ai_", "AI variables");

static __int64 tempInventoryID = 0x0F00000000000000;

obj_ServerPlayer::obj_ServerPlayer()
	: netMover(this, 0.2f, (float)PKT_C2C_MoveSetCell_s::PLAYER_CELL_RADIUS)
#ifdef MISSIONS
	, m_MissionsProgress(NULL)
#endif
#ifdef VEHICLES_ENABLED
	, isInVehicle(false)
	, currentVehicleId(-1)
	, seatPosition(-1)
#endif
{
	ObjTypeFlags = OBJTYPE_Human;

	wasDeleted = false;

	peerId_ = -1;
	startPlayTime_ = r3dGetTime();

	m_SpawnProtectedUntil = 0;

	r3dscpy(aggressor, "unknown");
	killedBy = obj_ServerGravestone::KilledBy_Unknown;

	security_utcGameTimeSent = false;
	security_GameTimeSent = false;
	security_NVGSent = false;
	security_screenshotRequestSentAt = 0;

	m_PlayerFlyingAntiCheatTimer = 0.0f;
	m_PlayerSuperJumpDetection = 0;
	m_PlayerUndergroundAntiCheatTimer = 0.0f;

	lastPickupNotifyTime = 0;
	m_LeaveGroupAtTime = -1;

	wasDisconnected_ = false;
	isHighPing = false;//AlexRedd:: High Ping	

	groupID = 0;
	isGroupLeader = false;

	lastCamPos.Assign(0, 0, 0);
	lastCamDir.Assign(0, 0, 1);

	m_isAdmin_GodMode = false;

	moveInited = false;

	r3dscpy(userName, "unknown");
	m_SelectedWeapon = 0;
	m_clipAttmChanged = false;

	for (int i = 0; i < NUM_WEAPONS_ON_PLAYER; i++)
		m_WeaponArray[i] = NULL;

	for (int i = 0; i < SLOT_Max; i++)
		gears_[i] = NULL;

	lastTimeHit = 0;
	lastHitBodyPart = 0;
	lastTimeHitItemID = 0;

	lastChatTime_ = -1;
	numChatMessages_ = 0;

	m_PlayerRotation = 0;

	lastCharUpdateTime_ = -999; // so first update will be launched immidiately after player creation
	lastWorldUpdateTime_ = -1;
	lastWorldFlags_ = -1;
	lastVisUpdateTime_ = -1;

	haveBadBackpack_ = 0;
	inventoryOpActive_ = false;

	m_isFlashlightOn = true;
}

static unsigned int WINAPI UpdateVisData(void* param)
{
	// r3dOutToLog("doing this in a thread!\n");
	obj_ServerPlayer* plr = (obj_ServerPlayer*)param;
	/*while (!plr->isDestroy)
	{*/
	if (plr)
	{
		gServerLogic.UpdateNetObjVisData(plr);
		// r3dOutToLog("Returning UpdateVisData Thread!\n");
		return 1;
	}
	else
	{
		plr = NULL;
		r3dOutToLog("UpdateVisData: player is NULL!\n");
		return 1;
	}

	// Sleep(1000);
	// }
	gServerLogic.ResetNetObjVisData(plr);
	r3dOutToLog("ResetNetObjVisData Teleport");
	plr = NULL;

	r3dOutToLog("Thread ending\n");

	return 0;
}

static unsigned int WINAPI UpdateVisDataTeleport(void* param)
{
	// r3dOutToLog("doing this in a thread!\n");
	obj_ServerPlayer* plr = (obj_ServerPlayer*)param;
	/*while (!plr->isDestroy)
	{*/
	if (plr)
	{
		gServerLogic.UpdateNetObjVisData(plr);
		// r3dOutToLog("Returning Thread!\n");
		return 1;
	}
	else
	{
		plr = NULL;
		return 1;
	}

	// Sleep(1000);
	// }
	gServerLogic.ResetNetObjVisData(plr);
	r3dOutToLog("ResetNetObjVisData Teleport");
	plr = NULL;

	r3dOutToLog("Thread ending\n");

	return 0;
}


BOOL obj_ServerPlayer::OnCreate()
{
	parent::OnCreate();
	DrawOrder = OBJ_DRAWORDER_FIRST;

	SetVelocity(r3dPoint3D(0, 0, 0));

	r3d_assert(!NetworkLocal);

	myPacketSequence = 0;
	clientPacketSequence = 0;
	packetBarrierReason = "";

	tradeRequestTo = 0;
	tradeStatus = 0;
	tradeTargetId = 0;
	tradeLastChangeTime = -9999999.0f;

	airdropId = 0;

	lastCallForHelp = -99999999.0f;

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode
	isGameBattleRoyale = gServerLogic.ginfo_.IsGameBR();	
#endif //ENABLE_BATTLE_ROYALE

	FireHitCount = 0;

	m_lastTimeUsedConsumable = -99999.0f;
	m_currentConsumableCooldownTime = 0;

	numKillWithoutDying = 0;
	LastEnemyKillTime = 0;
	Killstreaks = 0;
	LastKilledBy = invalidGameObjectID;
	m_InDamageAreaTime = 0;

#ifdef MISSIONS
	m_MissionsProgress = loadout_->missionsProgress;
	m_MissionsProgress->m_player = this;
#endif

	weapDataReqSent = r3dGetTime();
	weapDataReqExp = -1;
	weapCheatReported = false;

	lastPlayerAction_ = r3dGetTime();
	m_PlayerState = 0;

	m_Stamina = GPP_Data.c_fSprintMaxEnergy;

	// set that character is alive
	loadout_->Alive = 1;
	loadout_->GamePos = GetPosition();
	loadout_->GameDir = m_PlayerRotation;

	// invalidate last sended vitals
	lastVitals_.Health = 0xFF;
	lastVitals_.Hunger = 0xFF;

	if (profile_.ProfileData.isDevAccount & wiUserProfile::DAA_INVISIBLE)
	{
		m_isAdmin_GodMode = true;
		distToCreateSq = 0.0001f;
		distToDeleteSq = 0.0001f;
	}

	// aLca :: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//		:: THIS SHIT FUCKED ALL OVER, AS ITS _NOT_ NEEDED!

	// gServerLogic.NetRegisterObjectToPeers(this);
	// detect what objects is visible right now
	// gServerLogic.UpdateNetObjVisData(this);
	// UpdateVisThread = (HANDLE)_beginthreadex(NULL, 0, UpdateVisData, (void*)this, 0, NULL);

	lastVisUpdateTime_ = r3dGetTime();

	// for initing cellMover there
	TeleportPlayer(GetPosition());

	// aLca :: FastloadV3
	isDestroy = false;

	if (gServerLogic.AirDropsPos.size() > 0)
	{
		PKT_S2C_AirDropOnMap_s AirDrop;
		AirDrop.NumbeAirDrop = gServerLogic.AirDropsPos.size();
		gServerLogic.p2pSendRawToPeer(peerId_, &AirDrop, sizeof(AirDrop));
	}

	// AlexRedd:: for minimap icons
	{
		int actID = 0;
		for (int i = 0; i < gDamageAreaMngr.numDamageArea_; i++)
		{
			obj_ServerDamageArea* dbox = gDamageAreaMngr.DamageArea_[i];
			if (dbox)
			{
				PKT_S2C_MissionMapUpdate_s n;
				n.actionID = actID;
				n.actionIcon = 8;
				n.location = dbox->GetPosition();
				n.active = true;

				gServerLogic.p2pSendRawToPeer(peerId_, &n, sizeof(n), true);
				actID++;
			}
		}
	}
	///////////////////////////////////////////////////

#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	//AlexRedd:: No drop mode
	if (gServerLogic.ginfo_.IsDevEvent() && gServerLogic.ginfo_.IsNoDropGame())
	{
		devEventNoDropItemsCount = -1;
		for (int i = 0; i < loadout_->BackpackSize; i++)
		{
			wiInventoryItem tempItem;
			tempItem.InventoryID = 0;
			tempItem.itemID = loadout_->Items[i].itemID;
			tempItem.quantity = loadout_->Items[i].quantity;
			tempItem.Var2 = loadout_->Items[i].Var2;

			if (loadout_->Items[i].itemID>0)
				devEventNoDropItems[++devEventNoDropItemsCount] = tempItem;
		}
	}
#endif


	distToCreateSq = 600 * 600; // 1024
	distToDeleteSq = 650 * 650; // 1500

	return TRUE;
}


obj_ServerPlayer::~obj_ServerPlayer()
{
}

BOOL obj_ServerPlayer::OnDestroy()
{
	Trade_Close();

	AirDrop_Unlock();

	return parent::OnDestroy();
}

BOOL obj_ServerPlayer::Load(const char *fname)
{
	if (!parent::Load(fname))
		return FALSE;

	// Object won't be saved when level saved
	bPersistent = 0;

	Height = SRV_WORLD_SCALE(1.8f);

	bzOutTime = -1;//AlexRedd:: BZ

	RecalcBoundBox();

	return TRUE;
}

void obj_ServerPlayer::SetProfile(const CServerUserProfile& in_profile)
{
	profile_ = in_profile;
	loadout_ = &profile_.ProfileData.ArmorySlots[0];
	savedLoadout_ = *loadout_;
	savedGameDollars_ = profile_.ProfileData.GameDollars;

	// those was already checked in GetProfileData, but make sure about that  
	r3d_assert(profile_.ProfileData.ArmorySlots[0].LoadoutID);
	r3d_assert(profile_.ProfileData.NumSlots == 1);
	r3d_assert(loadout_->LoadoutID > 0);
	r3d_assert(loadout_->Alive > 0);

	r3dscpy(userName, loadout_->Gamertag);

	boostXPBonus_ = 0.0f; // % to add	
	boostWPBonus_ = 0.0f; // % to add

	//AlexRedd:: +50% XP boosts (marketplace item)
	if (profile_.ProfileData.XP50Boosts > 0)
	{
		if (profile_.ProfileData.PremiumAcc > 0)
			boostXPBonus_ += 1.0f;  // % to add	
		else
			boostXPBonus_ += 0.5f;  // % to add		
	}

	//r3dOutToLog("SetProfile %s\n", userName); CLOG_INDENT;

	ValidateBackpack();
	ValidateAttachments();
	SetLoadoutData();

	// create martial arts weapon
	SetWeaponSlot(HANDS_WEAPON_IDX, WeaponConfig::ITEMID_UnarmedMelee, wiWeaponAttachment());

	// (SERVER CODE SYNC POINT) override to hands weapon if there is no selected weapon
	if (m_WeaponArray[m_SelectedWeapon] == NULL)
		m_SelectedWeapon = HANDS_WEAPON_IDX;
}
//new add skin
void obj_ServerPlayer::SetLasSknUsed(int itemID, int SkinID)
{
	for (int i = 0; i<MAX_WEAPONS_SKINEABLED; i++)
	{
		WeaponSkins& w = profile_.ProfileData.WpnSKIN[i];

		if (w.itemID == itemID)
		{
			w.LastSKINused = SkinID;
			break;
		}
	}
}

bool obj_ServerPlayer::SetItemSkinDB(int itemID, int SkinID)
{
	//	r3dOutToLog("##### ENTRANDO 1\n");
	for (int i = 0; i<MAX_WEAPONS_SKINEABLED; i++)
	{
		WeaponSkins& w = profile_.ProfileData.WpnSKIN[i];
		if (w.itemID == itemID)
		{
			bool ExistSkin = false;
			int SkinCount = 0;

			if (w.Skins[SkinID] != SkinID)
			{
				w.Skins[0] = 0;//test Odozi skin
				w.Skins[SkinID] = SkinID;
				return false;
			}
			else {
				return true;
			}
		}
	}

	for (int a = 0; a<MAX_WEAPONS_SKINEABLED; a++)
	{
		WeaponSkins& w = profile_.ProfileData.WpnSKIN[a];
		if (w.itemID == 0)
		{
			w.Reset();
			w.itemID = itemID;
			w.LastSKINused = 0;
			w.Skins[0] = 0;
			w.Skins[SkinID] = SkinID;
			break;
		}
	}

	return false;
}

bool obj_ServerPlayer::CheckSkinGear(int itemID, int SkinID)
{
	if (!SetItemSkinDB(itemID, SkinID))
	{
		SetLasSknUsed(itemID, SkinID);

		PKT_C2S_LearnSkinItem_s n2;
		n2.ItemID = itemID;
		n2.SkinID = SkinID;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));


		PKT_S2C_UpdateWpnSkinSelected_s n;
		n.targetId = toP2pNetId(GetNetworkID());
		n.itemID = itemID;
		n.Selected = SkinID;
		n.Set = itemID;
		gServerLogic.p2pBroadcastToAll(&n, sizeof(n), true);

		gServerLogic.ApiPlayerUpdateWeaponSkins(this);
		return true;
	}
	else {
		SetLasSknUsed(itemID, SkinID);

		PKT_S2C_UpdateWpnSkinSelected_s n;
		n.targetId = toP2pNetId(GetNetworkID());
		n.itemID = itemID;
		n.Selected = SkinID;
		n.Set = itemID;
		gServerLogic.p2pBroadcastToAll(&n, sizeof(n), true);

		gServerLogic.ApiPlayerUpdateWeaponSkins(this);
		return true;
	}

	return false;
}
//new add skin
bool obj_ServerPlayer::isNVGEquipped() const
{
	if (loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20188 ||
		loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20187 ||
		loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20007 ||
		loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20067 ||
		loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20217 ||
		loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID == 20068
		)
		return true;

	return false;
}

//AlexRedd:: BR mode
void obj_ServerPlayer::DoRespawn(const r3dPoint3D& pos, float dir, float spawnProtection)
{
	SetLatePacketsBarrier("respawn");

	tradeRequestTo = 0;
	tradeStatus = 0;
	tradeTargetId = 0;
	tradeLastChangeTime = -9999999.0f;
	airdropId = 0;

	lastCallForHelp = -99999999.0f;

	numKillWithoutDying = 0;
	LastEnemyKillTime = 0;
	Killstreaks = 0;
	bzOutTime = -1;//AlexRedd:: BZ	

	m_lastTimeUsedConsumable = -99999.0f;
	m_currentConsumableCooldownTime = 0;

	m_InDamageAreaTime = 0;

#ifdef MISSIONS
	m_MissionsProgress = loadout_->missionsProgress;
	m_MissionsProgress->m_player = this;
#endif

	weapDataReqSent = r3dGetTime();
	weapDataReqExp = -1;
	weapCheatReported = false;

	lastPlayerAction_ = r3dGetTime();
	m_PlayerState = 0;

	// set that character is alive
	loadout_->Alive = 1;
	loadout_->Health = 100.0f;
	loadout_->Hunger = 0.0f;
	loadout_->Thirst = 0.0f;
	loadout_->Toxic = 0.0f;
	loadout_->MedBleeding = 0.0f;
	loadout_->MedBloodInfection = 0.0f;
	m_Stamina = GPP_Data.c_fSprintMaxEnergy;

	m_SelectedWeapon = HANDS_WEAPON_IDX;

	for (int i = 0; i < loadout_->BackpackSize; i++)
	{
		wiInventoryItem& wi = loadout_->Items[i];
		if (wi.itemID != WeaponConfig::ITEMID_UnarmedMelee)
			wi.Reset();
	}

	for (int i = 0; i < NUM_WEAPONS_ON_PLAYER; i++)
	{
		if (i != HANDS_WEAPON_IDX)
		{
			//SAFE_DELETE(m_WeaponArray[i]);
			m_WeaponArray[i] = NULL;
		}
	}

	for (int i = 0; i < SLOT_Max; i++)
		gears_[i] = NULL;

	loadout_->Attachment[0].Reset();
	loadout_->Attachment[1].Reset();

	if (!gServerLogic.ginfo_.IsDevEvent())
	{
#ifdef ENABLE_BATTLE_ROYALE
		if(!gServerLogic.ginfo_.IsGameBR())
		{
#endif //ENABLE_BATTLE_ROYALE
			if (loadout_->BackpackID == WeaponConfig::ITEMID_NoDrop_Backpack)//AlexRedd:: no drop backpack
				loadout_->BackpackID = WeaponConfig::ITEMID_NoDrop_Backpack;
			else{
				loadout_->BackpackID = 20176;
				loadout_->BackpackSize = 12;
			}
#ifdef ENABLE_BATTLE_ROYALE
		}
#endif //ENABLE_BATTLE_ROYALE
	}

	// Create the gravestone.
	//	if( !IsSwimming() && killedBy != obj_ServerGravestone::KilledBy_Unknown &&
	//#ifdef ENABLE_BATTLE_ROYALE
	//		!gServerLogic.ginfo_.IsGameBR() && 
	//#endif //ENABLE_BATTLE_ROYALE
	//		!gServerLogic.ginfo_.IsDevEvent() && gServerLogic.ginfo_.channel != 7)
	//	{			
	//		obj_ServerGravestone* gravestone = (obj_ServerGravestone*)srv_CreateGameObject("obj_ServerGravestone", "obj_ServerGravestone", GetPosition());
	//		gravestone->Init(this);
	//	}

	m_SpawnProtectedUntil = r3dGetTime() + spawnProtection;
	lastVisUpdateTime_ = r3dGetTime();

	TeleportPlayer(pos);
	m_PlayerRotation = dir;

	gServerLogic.ApiPlayerUpdateChar(this);

	//r3dOutToLog("Player: %s -- respawning\n", userName); CLOG_INDENT;
}

void obj_ServerPlayer::ValidateBackpack()
{
	for (int i = 0; i < loadout_->CHAR_MAX_BACKPACK_SIZE; i++)
	{
		wiInventoryItem& wi = loadout_->Items[i];
		if (wi.itemID == 0)
			continue;

		if (g_pWeaponArmory->getConfig(wi.itemID) == NULL)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, false, "ValidateBackpack",
				"%d", wi.itemID);
			wi.Reset();

			haveBadBackpack_ = 1;
			continue;
		}

		wi.ResetClipIfFull(); // in case when full clip was saved before 2013-4-18
	}
}

void obj_ServerPlayer::ValidateAttachments()
{
	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < WPN_ATTM_MAX; j++)
		{
			uint32_t itm = loadout_->Attachment[i].attachments[j];
			if (itm == 0)
				continue;

			if (g_pWeaponArmory->getAttachmentConfig(itm) == NULL)
			{
				gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, false, "ValidateAttachments",
					"%d", itm);

				loadout_->Attachment[i].attachments[j] = 0;

				haveBadBackpack_ = 1;
			}
		}
	}
}

void obj_ServerPlayer::DoDeath()
{
	r3d_assert(!inventoryOpActive_);

	gServerLogic.LogInfo(peerId_, "Death", ""); CLOG_INDENT;

	deathTime = r3dGetTime();
	weapDataReqExp = -1;

	Trade_Close();

	float waterDepth;
	float allowedDepth = 1.5f; // Starts swimming at 1.5
	bool isOverWater = IsOverWater(waterDepth);
	bool isSwimming = IsSwimming();

	// drop all items	
	for (int i = 0; i < loadout_->BackpackSize; i++)
	{
		const wiInventoryItem& wi = loadout_->Items[i];
		if (wi.itemID > 0)
		{
			//AlexRedd:: Vip items (nodrop)
			bool vipItem = GetVipItem(i);
			if (vipItem)
			{
				if (gServerLogic.ginfo_.channel != 6 && //check for channels
					!gServerLogic.ginfo_.IsDevEvent() && //check for modes
#ifdef ENABLE_BATTLE_ROYALE
					!gServerLogic.ginfo_.IsGameBR() && //check for modes
#endif //ENABLE_BATTLE_ROYALE
					//gServerLogic.ginfo_.mapId != GBGameInfo::MAPID_WZ_Colorado && //check for maps
					gServerLogic.ginfo_.gameServerId != 0)//check for gameserverid (replace 0 to your id)
				{
					CJobVipItem* job = new CJobVipItem(this);
					job->ItemID = wi.itemID;
					g_AsyncApiMgr->AddJob(job);
					r3dOutToLog("VIP ITEM: itemID [%i] CustomerID [%i] gameServerId [%i] channel [%i]\n", wi.itemID, this->profile_.CustomerID, gServerLogic.ginfo_.gameServerId, gServerLogic.ginfo_.channel);
				}
				BackpackRemoveItem(wi);
			}/////
			else if (isSwimming || (isOverWater && waterDepth > allowedDepth) || gServerLogic.ginfo_.IsDevEvent()
#ifdef ENABLE_BATTLE_ROYALE
				|| gServerLogic.ginfo_.IsGameBR() && !gServerLogic.m_isGameHasStarted
#endif //ENABLE_BATTLE_ROYALE
				)
				BackpackRemoveItem(wi);
			else
				BackpackDropItem(i);
		}
	}
	// drop not-default backpack as well
	if (loadout_->BackpackID != WeaponConfig::ITEMID_NoDrop_Backpack && //AlexRedd:: no drop backpack
		loadout_->BackpackID != 20176 && !isSwimming && (!isOverWater || (isOverWater && waterDepth < allowedDepth)) && !gServerLogic.ginfo_.IsDevEvent())
	{
#ifdef ENABLE_BATTLE_ROYALE
		if(gServerLogic.ginfo_.IsGameBR())
		{
			if(gServerLogic.m_isGameHasStarted) //AlexRedd:: BR mode
			{		
				// create network object
				obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", GetRandomPosForItemDrop());
				SetupPlayerNetworkItem(obj);
				// vars
				obj->m_Item.itemID   = loadout_->BackpackID;
				obj->m_Item.quantity = 1;
			}
			else {	
				// do nothing..
			}
		}
		else{	
#endif //ENABLE_BATTLE_ROYALE
			// create network object
			obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", GetRandomPosForItemDrop());
			SetupPlayerNetworkItem(obj);
			// vars
			obj->m_Item.itemID = loadout_->BackpackID;
			obj->m_Item.quantity = 1;
#ifdef ENABLE_BATTLE_ROYALE
		}
#endif //ENABLE_BATTLE_ROYALE
	}

	// set that character is dead
	loadout_->Alive = 0;
	loadout_->GamePos = GetPosition();
	loadout_->Health = 0;
	loadout_->MedBleeding = 0.0f;
	loadout_->MedFeverCold = 0.0f;
	loadout_->MedBloodInfection = 0.0f;
	// clear attachments
	loadout_->Attachment[0].Reset();
	loadout_->Attachment[1].Reset();
	loadout_->Stats.Deaths++;
	Killstreaks = 0;

	//NOTE: server WZ_Char_SRV_SetStatus will clear player backpack, so make that CJobUpdateChar::Exec() won't update it
	savedLoadout_ = *loadout_;

	gServerLogic.ApiPlayerUpdateChar(this);

	SetLatePacketsBarrier("death");

	return;
}

wiStatsTracking obj_ServerPlayer::AddReward(const wiStatsTracking& in_rwd)
{
	float XPBonus = boostXPBonus_;
	float WPBonus = boostWPBonus_;

	if (profile_.ProfileData.PremiumAcc > 0)
	{
		XPBonus += 1.0f; // see below, will double XP
		WPBonus += 1.0f;
	}

	wiStatsTracking rwd = in_rwd;
	// round up. basically if we award only 2 points, with +25% it would give us 0, so, let's make users more happy by round up
	// in case if that will be a balancing problem - we can always round it down with floorf
	if (rwd.RewardID >= RWD_ZombieKill && rwd.RewardID <= RWD_RevengeKill)
	{
		rwd.XP += int(ceilf(R3D_ABS(rwd.XP)*XPBonus));
		rwd.GP += int(ceilf(rwd.GP*WPBonus));
		rwd.GD += int(ceilf(rwd.GD*WPBonus));
	}

#ifdef ENABLE_BATTLE_ROYALE
	if(gServerLogic.ginfo_.IsGameBR() && gServerLogic.m_isGameHasStarted)
	{
		rwd.GD += gServerLogic.GetItemGDPrice(301400);
	}
#endif //ENABLE_BATTLE_ROYALE

	// adjust player stats
	profile_.ProfileData.GamePoints += rwd.GP;
	profile_.ProfileData.GameDollars += rwd.GD;
	loadout_->Stats.XP += rwd.XP;

	return rwd;
}

wiNetWeaponAttm	obj_ServerPlayer::GetWeaponNetAttachment(int wid)
{
	wiNetWeaponAttm atm;

	const ServerWeapon* wpn = m_WeaponArray[wid];
	if (!wpn)
		return atm;

	if (wpn->m_Attachments[WPN_ATTM_LEFT_RAIL])
		atm.LeftRailID = wpn->m_Attachments[WPN_ATTM_LEFT_RAIL]->m_itemID;
	if (wpn->m_Attachments[WPN_ATTM_MUZZLE])
		atm.MuzzleID = wpn->m_Attachments[WPN_ATTM_MUZZLE]->m_itemID;
	if (wpn->m_Attachments[WPN_ATTM_UPPER_RAIL])
		atm.UpperRailID = wpn->m_Attachments[WPN_ATTM_UPPER_RAIL]->m_itemID;
	if (wpn->m_Attachments[WPN_ATTM_BOTTOM_RAIL])
		atm.BottomRailID = wpn->m_Attachments[WPN_ATTM_BOTTOM_RAIL]->m_itemID;
	if (wpn->m_Attachments[WPN_ATTM_CLIP])
		atm.ClipID = wpn->m_Attachments[WPN_ATTM_CLIP]->m_itemID;

	return atm;
}


bool obj_ServerPlayer::FireWeapon(int wid, int fireSeqNo, gobjid_t localId)
{
	//r3dOutToLog("FireWeapon: %d %d %d\n", bullets_.size(), FireHitCount, localId);
	r3d_assert(loadout_->Alive);

	lastPlayerAction_ = r3dGetTime();

	if (wid < 0 || wid >= NUM_WEAPONS_ON_PLAYER)
	{
		gServerLogic.LogInfo(peerId_, "wid invalid", "%d", wid);
		return false;
	}

	ServerWeapon* wpn = m_WeaponArray[wid];
	if (wpn == NULL)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, true, "no weapon",
			"wid: %d", wid);
		return false;
	}

	// can't fire in safe zones - nope! we should allow firing to maintain correct HitCount tracking.
	// if(loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox) return false;
	// we may skip relaying packet, but it'll look weird seeing bullet hits, but without actual firing

	if (localId == invalidGameObjectID && wpn->getCategory() != storecat_MELEE)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, false, "!localId",
			"itemid: %d", wpn->getConfig()->m_itemID);
		return false;
	}

	if ((gServerLogic.ginfo_.flags & GBGameInfo::SFLAGS_DisableASR) && wpn->getCategory() == storecat_ASR)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, true, "ASR not allowed",
			"itemid: %d", wpn->getConfig()->m_itemID);
		return false;
	}
	if ((gServerLogic.ginfo_.flags & GBGameInfo::SFLAGS_DisableSNP) && wpn->getCategory() == storecat_SNP)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, true, "SNP not allowed",
			"itemid: %d", wpn->getConfig()->m_itemID);
		return false;
	}

	// if this is sequential bullet (only for shotguns, check if we have previous bullet)
	if (fireSeqNo > 0 && wpn->getCategory() != storecat_SHTG)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, false, "fireSeqNo !SHTG",
			"seq:%d itemid:%d", fireSeqNo, wpn->getConfig()->m_itemID);
		return false;
	}
	// prevent shooting too much from shotgun
	if (fireSeqNo > 7)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, false, "fireSeqNo big",
			"seq:%d", fireSeqNo);
		return false;
	}
	// last bullets in sequence should have correct no
	if (fireSeqNo > 0)
	{
		size_t last = bullets_.size();
		if (last == 0 || bullets_[last - 1].fireSeqNo != fireSeqNo - 1)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, false, "fireSeqNo mismatch",
				"seq:%d", fireSeqNo);
			return false;
		}
	}

	// incr fire count (decremented on hit event) and track usage.
	FireHitCount++;
	gServerLogic.TrackWeaponUsage(wpn->getConfig()->m_itemID, 1, 0, 0);

	// if this is melee, we're done here. no ammo check, etc.
	if (wpn->getCategory() == storecat_MELEE)
		return true;

	// track ShotsFired
	loadout_->Stats.ShotsFired++;

	if (fireSeqNo == 0 && gServerLogic.weaponDataUpdates_ < 2)
	{
		// check if don't have ammo
		if (wpn->getClipConfig() == NULL)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, true, "no ammo");
			return false;
		}

		// check if we fired more that we was able
		wiInventoryItem& wi = wpn->getPlayerItem();
		if (wi.Var1 <= 0)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, true, "bullethack",
				"%d/%d clip:%d(%s)",
				wi.Var1,
				wpn->getClipConfig()->m_Clipsize,
				wpn->getClipConfig()->m_itemID,
				wpn->getClipConfig()->m_StoreName
				);
			return false;
		}

		wpn->getPlayerItem().Var1--;
	}

#ifdef MISSIONS
	// Weapons are used from the backpack, and no HashID exists for these items,
	// so we can only count the number of uses.
	m_MissionsProgress->PerformItemAction(Mission::ITEM_Use, wpn->getConfig()->m_itemID, 0, Mission::ITEMUSEON_NotSpecified);
#endif

	// store this bullet on player
	bullets_s blt;
	blt.fireSeqNo = fireSeqNo;
	blt.localId = localId;
	blt.wid = wid;
	blt.ItemID = wpn->getConfig()->m_itemID;
	bullets_.push_back(blt);

	// adjust durability last, as after this gun might be broken
	// adjust durability (SERVER_SYNC_POINT DUR), melee durability will be adjusted on hit
	bool wpnIsValid = true;
	if (wpn->getCategory() != storecat_MELEE)
	{
		// hack for shotguns, that fire 8 pellets, so for them adjust durability on the last pelet, otherwise if done on first one and weapon destroys itself, server will kick you on second pelet, as weapon doesn't exist anymore
		if ((wpn->getCategory() == storecat_SHTG && fireSeqNo == 7) || (wpn->getCategory() != storecat_SHTG && fireSeqNo == 0))
			wpnIsValid = AdjustWeaponDurability(wpn);
	}
	if (!wpnIsValid) // weapon was destroyed
		return false;

	return true;
}

bool obj_ServerPlayer::AdjustWeaponDurability(ServerWeapon* wpn)
{
	if (wpn->m_BackpackIdx == HANDS_WEAPON_IDX)
		return true;

	wpn->getPlayerItem().adjustDurability(-wpn->getConfig()->m_DurabilityUse);
	//r3dOutToLog("%s, dur:%d\n", wpn->getConfig()->m_StoreName, wpn->getPlayerItem().Var3);	

	// adjust durability for all attachments except clip
	for (int atm = 0; atm < WPN_ATTM_MAX; atm++)
	{
		if (atm == WPN_ATTM_CLIP)
			continue;

		const WeaponAttachmentConfig* wac = wpn->m_Attachments[atm];
		if (wac == NULL)
			continue;

		// search for that attachment in player inventory
		for (int i = 0; i < loadout_->BackpackSize; i++)
		{
			wiInventoryItem& itm = loadout_->Items[i];
			if (itm.itemID == wac->m_itemID)
			{
				// important semi-hack, do not adjust durability for OLD stacked attachmnts. 
				if (itm.quantity > 1)
					continue;

				itm.adjustDurability(-wac->m_DurabilityUse);

				// and destroy attachment if expired
				if (itm.Var3 == 0)
				{
					AdjustBackpackSlotQuantity(i, -itm.quantity, false);
				}

				break;
			}
		}
	}

	if (wpn->getPlayerItem().Var3 == 0) // weapon is destroyed
	{
		AdjustBackpackSlotQuantity(wpn->m_BackpackIdx, -wpn->getPlayerItem().quantity);
		return false;
	}

	return true;
}

ServerWeapon* obj_ServerPlayer::OnBulletHit(gobjid_t localId, const char* pktName, bool hitSomething)
{
	//r3dOutToLog("%s: %d %d\n", pktName, localId, bullets_.size());
	FireHitCount--;

	ServerWeapon* wpn = NULL;

	if (localId == invalidGameObjectID)
	{
		if (FireHitCount < 0)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, true, "hitcountMelee",
				"%s", pktName);
			return NULL;
		}

		// melee case, weapon can be in 2nd slot or in HANDS_WEAPON_IDX
		wpn = m_WeaponArray[m_SelectedWeapon];
		if (wpn == NULL || wpn->getCategory() != storecat_MELEE && wpn->getCategory() != storecat_SUPPORT)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, false, "MeleeHitNoWeapon",
				"%s idx:%d wpn:%d", pktName, m_SelectedWeapon, wpn ? wpn->getConfig()->m_itemID : 0);
			return NULL;
		}

		// adjust durability for melee on hit
		bool wpnIsValid = true;
		if (wpn && hitSomething)
			wpnIsValid = AdjustWeaponDurability(wpn);
		// check if weapon is still present after durability check
		if (!wpnIsValid)
			return NULL;
	}
	else
	{
		// find bullet and validate it
		bool found = false;
		bullets_s blt;
		std::vector<bullets_s>::iterator it;
		for (it = bullets_.begin(); it != bullets_.end(); it++)
		{
			if (it->localId == localId)
			{
				found = true;
				blt = *it;
				bullets_.erase(it);
				break;
			}
		}

		if (!found)
		{
			// find previously flying bullets
			int bid1 = bullets_.size() > 0 ? bullets_[0].localId.get() : 0;
			int bid2 = bullets_.size() > 1 ? bullets_[1].localId.get() : 0;
			int bid3 = bullets_.size() > 2 ? bullets_[2].localId.get() : 0;
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, false, "localId",
				"%s %08x %x %x %x", pktName, localId.get(), bid1, bid2, bid3);
			return NULL;
		}

		// weapon might be already changed, in that case skip that hit
		wpn = m_WeaponArray[blt.wid];
		if (wpn == NULL || wpn->getConfig()->m_itemID != blt.ItemID)
		{
			return NULL;
		}

		if (FireHitCount < 0)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, false, "hitcount",
				"%s, wpn:%d", pktName, blt.ItemID);
			return NULL;
		}
	}

	return wpn;
}

float obj_ServerPlayer::ReduceDamageByGear(int bodyPart, float damage)
{
	int gslot = bodyPart == 1 ? SLOT_Headgear : SLOT_Armor;
	if (gears_[gslot] == NULL)
		return damage;

	const GearConfig* gc = gears_[gslot]->getConfig();

	wiInventoryItem& itm = loadout_->Items[gslot == SLOT_Headgear ? wiCharDataFull::CHAR_LOADOUT_HEADGEAR : wiCharDataFull::CHAR_LOADOUT_ARMOR];
	r3d_assert(itm.itemID);

	// broken armor
	if (itm.Var3 == 0)
		return damage;

	// chance to fully absorb damage on headgears
	if (gslot == SLOT_Headgear && u_GetRandom(0, 100) < gc->m_bulkiness && !wasDisconnected_)
	{
		if (r3dGetTime() < m_SpawnProtectedUntil)//AlexRedd:: no damage when player in spawnprotection
		{
			return gc->m_bulkiness;
		}
		else
			// set durability to 10%
			if (itm.Var3 > 10 * 100)
				itm.Var3 = 10 * 100;

		BackpackDropItem(wiCharDataFull::CHAR_LOADOUT_HEADGEAR);
		OnBackpackChanged(wiCharDataFull::CHAR_LOADOUT_HEADGEAR);

		return 0.0f;
	}

	// adjust durability (SERVER_SYNC_POINT ARMOR_DUR)
	//float absorb = damage * gc->m_damagePerc;
	//float left   = damage - absorb;

	// adjust durability (SERVER_SYNC_POINT ARMOR_DUR)
	float left;
	float absorb = damage * gc->m_damagePerc;
	if (loadout_->Skills[CUserSkills::SKILL_WeaponDegradation1])//AlexRedd:: new skill
		left = (damage - absorb) * 0.95f;
	else
		left = damage - absorb;

	// armor durability is mapped from [0..m_damageMax] to [0..100]
	float durLoss = left / gc->m_damageMax * 100.0f;
	itm.adjustDurability(-durLoss);

	// inform player about damage so he can adjust armor durability
	{
		PKT_S2C_PlayerRawDamage_s n;
		n.bodyPart = bodyPart;
		n.damage = damage;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
	}

	return left;
}

// params should be in [0..360] range
float getMinimumAngleDistance(float from, float to)
{
	float d = to - from;
	if (d < -180.0f)	d += 360.0f;
	if (d > 180.0f)	d -= 360.0f;
	return d;
}


float obj_ServerPlayer::ApplyDamage(float damage, GameObject* fromObj, int bodyPart, STORE_CATEGORIES damageSource, uint32_t dmgItemID, bool canApplyBleeding)
{
	lastTimeHit = r3dGetTime();
	lastHitBodyPart = bodyPart;
	lastTimeHitItemID = dmgItemID;

#ifdef ENABLE_BATTLE_ROYALE
	if (isGameBattleRoyale && gServerLogic.isCountingDown) //AlexRedd:: BR mode
		damage = 0;
#endif //ENABLE_BATTLE_ROYALE

	if (m_isAdmin_GodMode)
		damage = 0;

	if (r3dGetTime() < m_SpawnProtectedUntil)
		damage = 0;

	//r3dOutToLog("Player(%s) received damage\n", userName); CLOG_INDENT;
	//r3dOutToLog("raw damage(%.2f) at part (%d), isGodMode (%d), isSpawnProtected(%d)\n", damage, bodyPart, m_isAdmin_GodMode?1:0, r3dGetTime() < m_SpawnProtectedUntil?1:0);

	// adjust damage based on hit part
	if (damageSource != storecat_MELEE)
	{
		switch (bodyPart)
		{
		case 1: // head
			damage *= 2;
			if (IsServerPlayer(fromObj))
				IsServerPlayer(fromObj)->loadout_->Stats.ShotsHeadshots++;
			break;

			// case 2: // hands
		case 3: // legs
			damage *= 0.75f;
			break;
		}
	}

	// reduce damage by armor		
	damage = ReduceDamageByGear(bodyPart, damage);

	//r3dOutToLog("gear adjusted damage(%.2f)\n", damage);

	// Increased health 1
	if (loadout_->Skills[CUserSkills::SKILL_Physical1])
		damage *= 0.95f;
	// Increased health 2
	if (loadout_->Skills[CUserSkills::SKILL_Physical8])
		damage *= 0.9f;

	if (loadout_->Skills[CUserSkills::SKILL_Survival11])
		damage *= 0.85f;

	//r3dOutToLog("skill adjusted damage(%.2f)\n", damage);

	if (damage < 0)
		damage = 0;

	//r3dOutToLog("current health=%.2f\n", loadout_->Health);
	// reduce health
	loadout_->Health -= damage;

	if (damage > 0 && canApplyBleeding)
	{
		if (loadout_->MedBleeding < 1.0f)
		{
			if (damageSource == storecat_MELEE)
			{
				if (u_GetRandom(0.0f, 1.0f) <= 0.2f) // 20% chance
					loadout_->MedBleeding = 100.0f;
			}
			else // bullet\grenade
				if (u_GetRandom(0.0f, 1.0f) <= 0.5f) // 50% chance
					loadout_->MedBleeding = 100.0f;
		}
	}

	return damage;
}

void obj_ServerPlayer::SetWeaponSlot(int wslot, uint32_t weapId, const wiWeaponAttachment& attm)
{
	r3d_assert(wslot < NUM_WEAPONS_ON_PLAYER);

	// remember previously used weapon
	m_dbg_PreviousWeapon[wslot] = m_WeaponArray[wslot] ? m_WeaponArray[wslot]->getConfig()->m_itemID : 0;

	SAFE_DELETE(m_WeaponArray[wslot]);

	if (weapId == 0)
		return;

	const WeaponConfig* weapCfg = g_pWeaponArmory->getWeaponConfig(weapId);
	if (weapCfg == NULL) {
		r3dOutToLog("!!! %s does not have weapon id %d\n", userName, weapId);
		return;
	}

	//r3dOutToLog("Creating wpn %s\n", weapCfg->m_StoreName); CLOG_INDENT;
	m_WeaponArray[wslot] = new ServerWeapon(weapCfg, this, wslot, attm);

	if (weapCfg->category != storecat_MELEE)
	{
		if (m_WeaponArray[wslot]->getClipConfig() == NULL) {
			r3dOutToLog("!!! weapon id %d does not have default clip attachment\n", weapId);
		}
	}

	return;
}

void obj_ServerPlayer::SetGearSlot(int gslot, uint32_t gearId)
{
	r3d_assert(gslot >= 0 && gslot < SLOT_Max);
	SAFE_DELETE(gears_[gslot]);

	if (gearId == 0)
		return;

	if (g_pWeaponArmory->getGearConfig(gearId) == NULL) {
		r3dOutToLog("!!! %s does not have gear id %d\n", userName, gearId);
		return;
	}

	gears_[gslot] = g_pWeaponArmory->createGear(gearId);
	return;
}

void obj_ServerPlayer::SetLoadoutData()
{
	wiCharDataFull& slot = profile_.ProfileData.ArmorySlots[0];

	//@ FOR NOW, attachment are RESET on entry. need to detect if some of them was dropped
	// (SERVER CODE SYNC POINT)
	slot.Attachment[0] = wiWeaponAttachment();
	if (slot.Items[0].Var2 > 0)
		slot.Attachment[0].attachments[WPN_ATTM_CLIP] = slot.Items[0].Var2;

	slot.Attachment[1] = wiWeaponAttachment();
	if (slot.Items[1].Var2 > 0)
		slot.Attachment[1].attachments[WPN_ATTM_CLIP] = slot.Items[1].Var2;

	SetWeaponSlot(0, slot.Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].itemID, slot.Attachment[0]);
	SetWeaponSlot(1, slot.Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].itemID, slot.Attachment[1]);

	//SetGearSlot(SLOT_Char,     slot.HeroItemID);
	SetGearSlot(SLOT_Armor, slot.Items[wiCharDataFull::CHAR_LOADOUT_ARMOR].itemID);
	SetGearSlot(SLOT_Headgear, slot.Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID);

	m_dbg_PreviousWeapon[0] = m_WeaponArray[0] ? m_WeaponArray[0]->getConfig()->m_itemID : 0;
	m_dbg_PreviousWeapon[1] = m_WeaponArray[1] ? m_WeaponArray[1]->getConfig()->m_itemID : 0;

	return;
}
#include <thread>
#include <iostream>
BOOL obj_ServerPlayer::Update()
{
	parent::Update();

	const float timePassed = r3dGetFrameTime();
	const float curTime = r3dGetTime();

	// std::thread threadObj1(thread_function, (void*)this);
	// std::thread threadObj2(thread_function, (void*)param);

	// pereodically update network objects visibility
	if (curTime > lastVisUpdateTime_ + 0.3f)
	{
		lastVisUpdateTime_ = r3dGetTime();

		gServerLogic.UpdateNetObjVisData(this);
		// UpdateVisThread = (HANDLE)_beginthreadex(NULL, 0, UpdateVisData, (void*)this, 0, NULL);

		/*if (threadObj1.get_id() != threadObj2.get_id())
		std::cout << "Both Threads have different IDs" << std::endl;

		std::cout << "From Main Thread :: ID of Thread 1 = " << threadObj1.get_id() << std::endl;
		std::cout << "From Main Thread :: ID of Thread 2 = " << threadObj2.get_id() << std::endl;*/

		// threadObj1.join();
		// threadObj2.join();
		// return 0;
	}

	if (loadout_->Alive == 0)
	{
		return TRUE;
	}

	if (wasDisconnected_)
		return TRUE;

	// disconnect player after few ticks if he had bad items in inventory		
	if (haveBadBackpack_)
	{
		if (++haveBadBackpack_ > 5)
		{
			haveBadBackpack_ = 0;
			gServerLogic.DisconnectPeer(peerId_, false, "haveBadBackpack");
			return TRUE;
		}
	}

	// check for network protocol hacks (player firing lots of fire events for some reasons)
	if (bullets_.size() > 200) // 200 is double value of shotgun fires for 5 sec (time when bullets dissapear on client)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NumShots, true, "bullets_.size()");
		return TRUE;
	}

	// NOTE: if global inventory operation is active, do not check for health, kill player, etc, etc
	if (inventoryOpActive_)
		return TRUE;

	if (security_screenshotRequestSentAt > 0)
	{
		if ((r3dGetTime() - security_screenshotRequestSentAt) > 10.0f) // client ignored screenshot request
		{
			security_screenshotRequestSentAt = 0; // reset
		}
	}

	// request weapon data report from client once in a while
	if (curTime > weapDataReqSent + PKT_S2C_PlayerWeapDataRepReq_s::REPORT_PERIOD)
	{
		COMPILE_ASSERT(15 < PKT_S2C_PlayerWeapDataRepReq_s::REPORT_PERIOD);
		weapDataReqSent = curTime;
		weapDataReqExp = curTime + 15;	// expected time should be above rakpeer timeout (10sec in release)

		PKT_S2C_PlayerWeapDataRepReq_s n;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
	}
	// if we didnt' received weap data rep in expected time
#if DEV
	{/*...*/}
#else
	if (weapDataReqExp > 0 && curTime > weapDataReqExp)
	{
		//gServerLogic.DisconnectPeer(peerId_, true, "no weapdatarep");
		gServerLogic.DisconnectPeer(peerId_, false, "no weapdatarep (without disconnect)");
		return TRUE;
	}
#endif
	if (m_LeaveGroupAtTime > 0 && r3dGetTime() > m_LeaveGroupAtTime)
		gServerLogic.leavePlayerFromGroup(this);

	// STAT LOGIC
	{
		if (loadout_->Toxic < 100)
		{
			if (loadout_->Toxic > GPP_Data.c_fBloodToxicIncLevel2)
				loadout_->Toxic += timePassed*GPP_Data.c_fBloodToxicIncLevel2Value;
			else if (loadout_->Toxic > GPP_Data.c_fBloodToxicIncLevel1)
				loadout_->Toxic += timePassed*GPP_Data.c_fBloodToxicIncLevel1Value;
		}

		if (loadout_->Thirst < 100)
		{
			if (m_PlayerState == PLAYER_MOVE_SPRINT
				|| m_PlayerState == PLAYER_SWIM_FAST
				)
				loadout_->Thirst += timePassed*GPP_Data.c_fThirstSprintInc;
			else
				loadout_->Thirst += timePassed*GPP_Data.c_fThirstInc;
			if (loadout_->Toxic > GPP_Data.c_fThirstHighToxicLevel)
				loadout_->Thirst += timePassed*GPP_Data.c_fThirstHighToxicLevelInc;
		}
		if (loadout_->Hunger < 100)
		{
			float d = 0;
			if (m_PlayerState == PLAYER_MOVE_SPRINT
				|| m_PlayerState == PLAYER_SWIM_FAST
				)
				d += timePassed*GPP_Data.c_fHungerSprintInc;
			else if (m_PlayerState == PLAYER_MOVE_RUN)
				d += timePassed*GPP_Data.c_fHungerRunInc;
			else
				d += timePassed*GPP_Data.c_fHungerInc;
			if (loadout_->Toxic > GPP_Data.c_fHungerHighToxicLevel)
				d += timePassed*GPP_Data.c_fHungerHighToxicLevelInc;

			if (loadout_->Skills[CUserSkills::SKILL_Survival1])
				d *= 0.95f;
			if (loadout_->Skills[CUserSkills::SKILL_Survival6])
				d *= 0.9f;

			loadout_->Hunger += d;
		}

		if (loadout_->Toxic > GPP_Data.c_fBloodToxicLevel3)
			loadout_->Health -= timePassed*GPP_Data.c_fBloodToxicLevel3_HPDamage;
		else if (loadout_->Toxic > GPP_Data.c_fBloodToxicLevel2)
			loadout_->Health -= timePassed*GPP_Data.c_fBloodToxicLevel2_HPDamage;
		else if (loadout_->Toxic > GPP_Data.c_fBloodToxicLevel1)
			loadout_->Health -= timePassed*GPP_Data.c_fBloodToxicLevel1_HPDamage;
		{
			float d = 0;
			if (loadout_->Hunger > GPP_Data.c_fHungerLevel1)
				d += timePassed*GPP_Data.c_fHungerLevel_HPDamage;
			if (loadout_->Thirst > GPP_Data.c_fThirstLevel1)
				d += timePassed*GPP_Data.c_fThirstLevel_HPDamage;

			if (loadout_->Skills[CUserSkills::SKILL_Survival4])
				d *= 0.95f;
			if (loadout_->Skills[CUserSkills::SKILL_Survival8])
				d *= 0.9f;

			loadout_->Health -= d;
		}

		// Med System logic
		if (loadout_->MedBleeding > 0)
		{
			loadout_->MedBleeding = R3D_MAX(loadout_->MedBleeding - timePassed*GPP_Data.c_fMedSys_Bleeding_Decay, 0.0f);
			loadout_->Health -= timePassed*GPP_Data.c_fMedSys_Bleeding_Health;
			loadout_->Hunger += timePassed*GPP_Data.c_fMedSys_Bleeding_Food;
			loadout_->Thirst += timePassed*GPP_Data.c_fMedSys_Bleeding_Thirst;
			if (m_Stamina > 0)// !!!!!client\server sync stamina!!!!
				m_Stamina -= timePassed*GPP_Data.c_fMedSys_Bleeding_Stamina; // decrease stamina only if it is positive, as not to trigger cheat detection

			if (loadout_->MedBleeding <= 0.0f && loadout_->MedBloodInfection <= 0.0f) // if bleeding stopped on timer, then there is a 5% chance that it will cause blood infection
			{
				if (u_GetRandom(0.0f, 1.0f) <= 0.05f) // 5% chance
					loadout_->MedBloodInfection = 100.0f;
			}
		}
		if (loadout_->MedFeverCold > 0)
		{
			loadout_->MedFeverCold = R3D_MAX(loadout_->MedFeverCold - timePassed*GPP_Data.c_fMedSys_Fever_Decay, 0.0f);
			loadout_->Health -= timePassed*GPP_Data.c_fMedSys_Fever_Health;
			loadout_->Hunger += timePassed*GPP_Data.c_fMedSys_Fever_Food;
			loadout_->Thirst += timePassed*GPP_Data.c_fMedSys_Fever_Thirst;
			if (m_Stamina > 0)// !!!!!client\server sync stamina!!!!
				m_Stamina -= timePassed*GPP_Data.c_fMedSys_Fever_Stamina; // decrease stamina only if it is positive, as not to trigger cheat detection
		}
		if (loadout_->MedBloodInfection > 0)
		{
			loadout_->MedBloodInfection = R3D_MAX(loadout_->MedBloodInfection - timePassed*GPP_Data.c_fMedSys_BloodInfection_Decay, 0.0f);
			loadout_->Health -= timePassed*GPP_Data.c_fMedSys_BloodInfection_Health;
			loadout_->Hunger += timePassed*GPP_Data.c_fMedSys_BloodInfection_Food;
			loadout_->Thirst += timePassed*GPP_Data.c_fMedSys_BloodInfection_Thirst;
			if (m_Stamina > 0) // !!!!!client\server sync stamina!!!!
				m_Stamina -= timePassed*GPP_Data.c_fMedSys_Bleeding_Stamina; // decrease stamina only if it is positive, as not to trigger cheat detection
		}

		if (loadout_->Health <= 0.0f)
		{
			r3dOutToLog("Player '%s' died as health dropped below zero\n", this->userName);
			gServerLogic.DoKillPlayer(this, this, storecat_INVALID, false);
			return TRUE;
		}
	}

	// STAMINA LOGIC SHOULD BE SYNCED WITH CLIENT CODE!
	// (stamina penalty and bOnGround is not synced with server, as it will not cause desync for non cheating client)
	{
		const float TimePassed = R3D_MIN(r3dGetFrameTime(), 0.1f);
		if (m_PlayerState == PLAYER_MOVE_SPRINT
			|| m_PlayerState == PLAYER_SWIM_FAST
			)
		{
			float d = TimePassed;
			if (loadout_->Skills[CUserSkills::SKILL_Physical2])
				d *= 0.95f;
			if (loadout_->Skills[CUserSkills::SKILL_Physical5])
				d *= 0.9f;

			m_Stamina -= d;
			if (m_Stamina < -60.0f) // allow one minute of stamina cheating
			{
				gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Stamina, true, "stamina");
			}
		}
		else
		{
			float regen_rate = loadout_->Health < 50 ? GPP_Data.c_fSprintRegenRateLowHealth : GPP_Data.c_fSprintRegenRateNormal;
			if (loadout_->Skills[CUserSkills::SKILL_Physical4])
				regen_rate *= 1.05f;
			if (loadout_->Skills[CUserSkills::SKILL_Physical6])
				regen_rate *= 1.10f;
			m_Stamina += TimePassed*regen_rate; // regeneration rate
		}
		m_Stamina = R3D_CLAMP((float)m_Stamina, 0.0f, GPP_Data.c_fSprintMaxEnergy);
	}

	// send vitals if they're changed
	PKT_S2C_SetPlayerVitals_s vitals;
	vitals.FromChar(loadout_);
	if (vitals != lastVitals_)
	{
		gServerLogic.p2pBroadcastToActive(this, &vitals, sizeof(vitals));
		lastVitals_.FromChar(loadout_);
	}

	const float CHAR_UPDATE_INTERVAL = 90;
	if (curTime > lastCharUpdateTime_ + CHAR_UPDATE_INTERVAL)
	{
		lastCharUpdateTime_ = curTime;
		gServerLogic.ApiPlayerUpdateChar(this);
		gServerLogic.ApiPlayerUpdateWeaponSkins(this);//new add skin
	}

	const float WORLD_UPDATE_INTERVAL = 0.5f;
	if (curTime > lastWorldUpdateTime_ + WORLD_UPDATE_INTERVAL)
	{
		lastWorldUpdateTime_ = curTime;
		UpdateGameWorldFlags();
	}

	if (CheckForFastMove())
		return TRUE;

	// anti cheat: player is under the ground, or player is flying above the ground
	//if(profile_.ProfileData.isDevAccount == 0) // do not disconnect DEVs for that
	{
		//PxRaycastHit hit;
		PxSweepHit hit;
		PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_PLAYER_COLLIDABLE_MASK, 0, 0, 0), PxSceneQueryFilterFlag::eSTATIC);
		r3dVector pos = GetPosition();
		PxBoxGeometry boxg(0.5f, 0.1f, 0.5f);
		PxTransform pose(PxVec3(pos.x, pos.y + 0.1f, pos.z));
		if (!g_pPhysicsWorld->PhysXScene->sweepSingle(boxg, pose, PxVec3(0, -1, 0), 2000.0f, PxSceneQueryFlag::eDISTANCE | PxSceneQueryFlag::eINITIAL_OVERLAP | PxSceneQueryFlag::eINITIAL_OVERLAP_KEEP, hit, filter))
		{
			m_PlayerUndergroundAntiCheatTimer += r3dGetFrameTime();
			if (m_PlayerUndergroundAntiCheatTimer > 1.0f)
			{
				r3dPoint3D pos = gServerLogic.AdjustPositionToFloor(GetPosition());
				gServerLogic.admin_TeleportPlayer(this, pos.x, pos.y, pos.z);
				//gServerLogic.DoKillPlayer(this, this, storecat_INVALID, 0, 0, true);
				gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Flying, false, "PLAYER UNDERGROUND!", "Player: %s under ground! %.2f, %.2f, %.2f", loadout_->Gamertag, pos.x, pos.y, pos.z);
				m_PlayerUndergroundAntiCheatTimer = 0.0f;

				//PKT_S2C_CheatWarning_s sn;
				//sn.cheatId = 253;
				//gServerLogic.p2pSendToPeer(peerId_, this, &sn, sizeof(sn),true);
			}
		}
		else
		{
			if (!IsSwimming()
#ifdef VEHICLES_ENABLED 
				&& !IsInVehicle()
#endif
				)
			{
				if (m_PlayerUndergroundAntiCheatTimer > 0)
					m_PlayerUndergroundAntiCheatTimer -= r3dGetFrameTime();

				float dist = hit.distance;
				//r3dOutToLog("@@@@ dist=%.2f\n", dist);
				if (dist > 2.1f) // higher than 1.6 meter above ground
				{
					// check if he is not falling, with some safe margin in case if he is walking down the hill
					if ((oldstate.Position.y - GetPosition().y) < 0.1f)
					{
						m_PlayerFlyingAntiCheatTimer += r3dGetFrameTime();
						if (m_PlayerFlyingAntiCheatTimer > 5.0f)
						{
							r3dPoint3D pos = gServerLogic.AdjustPositionToFloor(GetPosition());
							gServerLogic.admin_TeleportPlayer(this, pos.x, pos.y, pos.z);
							gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Flying, false, "player flying", "dist=%.2f, pos=%.2f, %.2f, %.2f", dist, pos.x, pos.y, pos.z);
							m_PlayerFlyingAntiCheatTimer = 0.0f;
						}
					}
					else if (m_PlayerFlyingAntiCheatTimer > 0.0f)
						m_PlayerFlyingAntiCheatTimer -= r3dGetFrameTime(); // slowly decrease timer
				}
			}
		}

		//AlexRedd:: AFK kick 
		const float AFK_TIME_ALLOWED = 600.0f;
		if ((curTime > lastPlayerAction_ + AFK_TIME_ALLOWED))
		{
			if (profile_.ProfileData.isDevAccount == 0 && !IsInVehicle())
			{
				char tmpStr[256];
				PKT_S2C_CustomKickMsg_s n;
				sprintf(tmpStr, "AFK TIME EXPIRED");
				r3dscpy(n.msg, tmpStr);
				gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
				gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_AFK, true, tmpStr);
				return TRUE;
			}
		}

		// check if player is moving UP too fast (super jump)
		if (GetPosition().y - oldstate.Position.y > 10.0f) // player moved up more than 10 meters within one frame, he is probably super jumping
		{
			m_PlayerSuperJumpDetection++;
			if (m_PlayerSuperJumpDetection > u_GetRandom(3.0f, 10.0f)) // give some margin of error and some random
				gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Flying, true, "player super jump", "dist=%.2f, pos=%.2f, %.2f, %.2f", GetPosition().y - oldstate.Position.y, pos.x, pos.y, pos.z);
		}
	}

	return TRUE;
}

void obj_ServerPlayer::RecalcBoundBox()
{
	float	x_size = 0.8f;
	float	z_size = x_size;
	float	y_size = Height;

	r3dPoint3D pos = GetPosition();
	r3dBoundBox bboxlocal;
	bboxlocal.Org.Assign(pos.X - x_size / 2, pos.Y, pos.Z - z_size / 2);
	bboxlocal.Size.Assign(x_size, y_size, z_size);
	SetBBoxLocal(bboxlocal);

	return;
}

BOOL obj_ServerPlayer::OnCollide(GameObject *tobj, CollisionInfo &trace)
{
	return TRUE;
}

void obj_ServerPlayer::UpdateGameWorldFlags()
{
	loadout_->GameFlags = 0;

	// scan for near postboxes
	for (int i = 0; i < gPostBoxesMngr.numPostBoxes_; i++)
	{
		obj_ServerPostBox* pbox = gPostBoxesMngr.postBoxes_[i];
		float dist = (GetPosition() - pbox->GetPosition()).Length();
		if (dist < pbox->useRadius)
		{
			loadout_->GameFlags |= wiCharDataFull::GAMEFLAG_NearPostBox;
			break;
		}
	}

	/*===============================================================================*/
	//AlexRedd:: radioactive zone
	// inside death area
	for (int i = 0; i < gDamageAreaMngr.numDamageArea_; i++)
	{
		obj_ServerDamageArea* dbox = gDamageAreaMngr.DamageArea_[i];
		float dist = (GetPosition() - dbox->GetPosition()).Length();
		if (dist < dbox->m_Radius)
		{
			loadout_->GameFlags |= wiCharDataFull::GAMEFLAG_DamageArea;
			break;
		}
	}
	// add 10 meter buffer zone
	for (int i = 0; i < gDamageAreaMngr.numDamageArea_; i++)
	{
		obj_ServerDamageArea* dbox = gDamageAreaMngr.DamageArea_[i];
		float dist = (GetPosition() - dbox->GetPosition()).Length();
		if (dist < dbox->m_Radius + 10.0f)
		{
			loadout_->GameFlags |= wiCharDataFull::GAMEFLAG_DamageAreaNear;
			break;
		}
	}
#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode
	if(isGameBattleRoyale && gServerLogic.m_isGameHasStarted && !gServerLogic.isHasWinningPlayer)
	{
		for (int i = 0; i<gGasAreaMngr.numGasArea_; i++)
		{
			obj_ServerGasArea* rbox = gGasAreaMngr.GasArea_[i];
			float dist = (GetPosition() - rbox->GetPosition()).Length();
			if (dist > rbox->useRadius)
			{
				loadout_->GameFlags |= wiCharDataFull::GAMEFLAG_GasArea;
				gServerLogic.ApplyDamage(this, this, GetPosition(), 1, true, storecat_DamageArea, 0);
				break;
			}
		}
	}
#endif //ENABLE_BATTLE_ROYALE
	/*===============================================================================*/

	if (lastWorldFlags_ != loadout_->GameFlags)
	{
		lastWorldFlags_ = loadout_->GameFlags;

		PKT_S2C_SetPlayerWorldFlags_s n;
		n.flags = loadout_->GameFlags;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
	}

	return;
}

bool obj_ServerPlayer::CaseAddItem(const PKT_C2S_OpenCase_s& n2, const wiInventoryItem& wi1)
{
	r3d_assert(!inventoryOpActive_);

	// SPECIAL case - GOLD item
	if (wi1.itemID == 'GOLD')
	{
		//r3dOutToLog("%s BackpackAddItem %d GameDollars\n", userName, wi1.quantity); CLOG_INDENT;

		wiInventoryItem wi2 = wi1;
		if (profile_.ProfileData.PremiumAcc > 0) // premium users should pick up double GD (the only way they can pickup GD is from spawns or zombie kills, so it should be save to double it here)
			wi2.quantity *= 2;
		profile_.ProfileData.GameDollars += wi2.quantity;

		// report to client
		PKT_S2C_BackpackAddNew_s n;
		n.SlotTo = 0;
		n.Item = wi2;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return true;
	}

	//r3dOutToLog("%s BackpackAddItem %dx%d\n", userName, wi1.itemID, wi1.quantity); CLOG_INDENT;
	r3d_assert(wi1.itemID > 0);
	r3d_assert(wi1.quantity > 0);

	const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(wi1.itemID);
	if (!itemCfg) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, false, "BackpackAddItem",
			"%d", wi1.itemID);
		return false;
	}

	int slot = GetBackpackSlotForItem(wi1);
	if (slot == -1)
	{
		PKT_S2C_BackpackModify_s n;
		n.SlotTo = 0xFF;

		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return false;
	}

	// check weight
	float totalWeight = loadout_->getTotalWeight();
	if (slot != 0 && slot != 1 && slot != 6 && slot != 7) // skip weapon & armor
	{
		totalWeight += itemCfg->m_Weight * wi1.quantity;

		if (loadout_->Skills[CUserSkills::SKILL_Physical3])
			totalWeight *= 0.95f;
		if (loadout_->Skills[CUserSkills::SKILL_Physical7])
			totalWeight *= 0.9f;
	}

	const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
	r3d_assert(bc);
	if (totalWeight > bc->m_maxWeight)
	{
		PKT_S2C_BackpackModify_s n;
		n.SlotTo = 0xFE;

		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return false;
	}

	AddItemToBackpackSlot(n2.WeaponSlot, wi1);
	return true;
}

bool obj_ServerPlayer::BackpackAddItem(const wiInventoryItem& wi1)
{
	r3d_assert(!inventoryOpActive_);

	// SPECIAL case - GOLD item
	if (wi1.itemID == 'GOLD')
	{
		//r3dOutToLog("%s BackpackAddItem %d GameDollars\n", userName, wi1.quantity); CLOG_INDENT;

		wiInventoryItem wi2 = wi1;
		if (profile_.ProfileData.PremiumAcc > 0) // premium users should pick up double GD (the only way they can pickup GD is from spawns or zombie kills, so it should be save to double it here)
			wi2.quantity *= 2;
		profile_.ProfileData.GameDollars += wi2.quantity;

		// report to client
		PKT_S2C_BackpackAddNew_s n;
		n.SlotTo = 0;
		n.Item = wi2;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return true;
	}

	//r3dOutToLog("%s BackpackAddItem %dx%d\n", userName, wi1.itemID, wi1.quantity); CLOG_INDENT;
	r3d_assert(wi1.itemID > 0);
	r3d_assert(wi1.quantity > 0);

	const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(wi1.itemID);
	if (!itemCfg) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, false, "BackpackAddItem",
			"%d", wi1.itemID);
		return false;
	}

	int slot = GetBackpackSlotForItem(wi1);
	if (slot == -1)
	{
		PKT_S2C_BackpackModify_s n;
		n.SlotTo = 0xFF;

		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return false;
	}

	// check weight
	float totalWeight = loadout_->getTotalWeight();
	if (slot != 0 && slot != 1 && slot != 6 && slot != 7) // skip weapon & armor
	{
		totalWeight += itemCfg->m_Weight * wi1.quantity;

		if (loadout_->Skills[CUserSkills::SKILL_Physical3])
			totalWeight *= 0.95f;
		if (loadout_->Skills[CUserSkills::SKILL_Physical7])
			totalWeight *= 0.9f;
	}

	const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
	r3d_assert(bc);
	if (totalWeight > bc->m_maxWeight)
	{
		PKT_S2C_BackpackModify_s n;
		n.SlotTo = 0xFE;

		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return false;
	}

	AddItemToBackpackSlot(slot, wi1);
	return true;
}

r3dPoint3D obj_ServerPlayer::GetRandomPosForItemDrop()
{
	// create random position around player
	r3dPoint3D pos = GetPosition();
	pos.y += 0.4f;
	pos.x += u_GetRandom(-1, 1);
	pos.z += u_GetRandom(-1, 1);

	return pos;
}

bool obj_ServerPlayer::GetVipItem(int idx) //AlexRedd:: no drop items 
{
	bool vipItem = false;
	wiInventoryItem& wi = loadout_->Items[idx];
	r3d_assert(wi.itemID);

	switch (wi.itemID)
	{
		/*case WeaponConfig::ITEMID_NoDrop_Weapon:
			vipItem = true;
			break;*/
	case WeaponConfig::ITEMID_NoDrop_Backpack:
		vipItem = true;
		break;
		/*case WeaponConfig::ITEMID_NoDrop_Armor:
			vipItem = true;
			break;
			case WeaponConfig::ITEMID_NoDrop_Helmet:
			vipItem = true;
			break;
			case WeaponConfig::ITEMID_NoDrop_Melee:
			vipItem = true;
			break;*/
	}
	return vipItem;
}

void obj_ServerPlayer::BackpackDropItem(int idx)
{
	r3d_assert(!inventoryOpActive_);

	wiInventoryItem& wi = loadout_->Items[idx];
	r3d_assert(wi.itemID);

	// create network object
	obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", GetRandomPosForItemDrop());
	SetupPlayerNetworkItem(obj);
	// vars
	obj->m_Item = wi;
#ifdef MISSIONS
	// As long as the player hasn't been killed when this method is called, then perform the item action.
	if (killedBy == obj_ServerGravestone::KilledBy_Unknown && m_MissionsProgress)
	{
		m_MissionsProgress->PerformItemAction(Mission::ITEM_Drop, obj->m_Item.itemID, obj->GetHashID());
	}
#endif

	// remove from remote inventory
	PKT_S2C_BackpackModify_s n;
	n.SlotTo = idx;
	n.Quantity = 0;
	n.dbg_ItemID = wi.itemID;
	gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));

	// remove from local inventory
	wi.Reset();
}

int obj_ServerPlayer::BackpackRemoveItem(const wiInventoryItem& wi1)
{
	int count = wi1.quantity;
	for (int idx = 0; count > 0 && idx < loadout_->BackpackSize; ++idx)
	{
		int prevCount = count;
		if (wi1.itemID == loadout_->Items[idx].itemID)
		{
			count -= loadout_->Items[idx].quantity;
			if (count < 0)
			{
				loadout_->Items[idx].quantity = -count;
				count = 0;
			}
			else
			{
				loadout_->Items[idx].Reset();
			}

			// remove from remote inventory
			PKT_S2C_BackpackModify_s n;
			n.SlotTo = idx;
			n.Quantity = loadout_->Items[idx].quantity;
			n.dbg_ItemID = wi1.itemID;
			gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));

			OnBackpackChanged(idx);
		}
	}

	// Returns how many of the item it was unable to find.
	return count;
}

int obj_ServerPlayer::BackpackRemoveItemSlot(int SlotID)
{
	if (loadout_->Items[SlotID].itemID != 0)
	{
		int ItemID = loadout_->Items[SlotID].itemID;

		loadout_->Items[SlotID].quantity--;
		if (loadout_->Items[SlotID].quantity <= 0)
			loadout_->Items[SlotID].Reset();

		PKT_S2C_BackpackModify_s n;
		n.SlotTo = SlotID;
		n.Quantity = loadout_->Items[SlotID].quantity;
		n.dbg_ItemID = ItemID;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));

		OnBackpackChanged(SlotID);
	}

	// Returns how many of the item it was unable to find.
	return 1;
}
void obj_ServerPlayer::OnBackpackChanged(int idx)
{
	// if slot changed is related to loadout - relay to other players
	switch (idx)
	{
	case wiCharDataFull::CHAR_LOADOUT_WEAPON1:
	case wiCharDataFull::CHAR_LOADOUT_WEAPON2:
		// attachments are reset on item change (SERVER CODE SYNC POINT)
		loadout_->Attachment[idx].Reset();
		if (loadout_->Items[idx].Var2 > 0)
			loadout_->Attachment[idx].attachments[WPN_ATTM_CLIP] = loadout_->Items[idx].Var2;

		OnRemoveAttachments(idx);

		SetWeaponSlot(idx, loadout_->Items[idx].itemID, loadout_->Attachment[idx]);
		OnLoadoutChanged();
		break;

	case wiCharDataFull::CHAR_LOADOUT_ARMOR:
		SetGearSlot(SLOT_Armor, loadout_->Items[idx].itemID);
		OnLoadoutChanged();
		break;
	case wiCharDataFull::CHAR_LOADOUT_HEADGEAR:
		SetGearSlot(SLOT_Headgear, loadout_->Items[idx].itemID);
		OnLoadoutChanged();
		break;

	case wiCharDataFull::CHAR_LOADOUT_ITEM1:
	case wiCharDataFull::CHAR_LOADOUT_ITEM2:
	case wiCharDataFull::CHAR_LOADOUT_ITEM3:
	case wiCharDataFull::CHAR_LOADOUT_ITEM4:
		OnLoadoutChanged();
		break;
	}
}

void obj_ServerPlayer::OnRemoveAttachments(int idx)
{
	PKT_S2C_PlayerRemoveAttachments_s n;
	n.idx = idx;

	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n));
}

void obj_ServerPlayer::OnLoadoutChanged()
{
	// (SERVER CODE SYNC POINT) override to hands weapon if there is no selected weapon
	if (m_WeaponArray[m_SelectedWeapon] == NULL)
		m_SelectedWeapon = HANDS_WEAPON_IDX;

	PKT_S2C_SetPlayerLoadout_s n;
	n.WeaponID0 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].itemID;
	n.quantity0 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].quantity;
	n.Var10 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].Var1;
	n.Var20 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].Var2;
	n.Var30 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].Var3;
	n.WeaponID1 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].itemID;
	n.quantity1 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].quantity;
	n.Var11 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].Var1;
	n.Var21 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].Var2;
	n.Var31 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].Var3;
	n.QuickSlot1 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ITEM1].itemID;
	n.QuickSlot2 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ITEM2].itemID;
	n.QuickSlot3 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ITEM3].itemID;
	n.QuickSlot4 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ITEM4].itemID;
	n.ArmorID = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ARMOR].itemID;
	n.HeadGearID = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID;
	n.BackpackID = loadout_->BackpackID;

	//TODO: for network traffic optimization (do not send to us) - change to RelayPacket (and add preparePacket there)
	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n), true);
}

void obj_ServerPlayer::OnAttachmentChanged(int wid, int atype)
{
	PKT_S2C_SetPlayerAttachments_s n;
	n.wid = (BYTE)wid;
	n.Attm = GetWeaponNetAttachment(wid);
	gServerLogic.p2pBroadcastToActive(this, &n, sizeof(n), true);
}

void obj_ServerPlayer::OnChangeBackpackSuccess(const std::vector<wiInventoryItem>& droppedItems)
{
	// backpack change was successful, drop items to the ground
	for (size_t i = 0; i < droppedItems.size(); i++)
	{
		// create network object
		obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", GetRandomPosForItemDrop());
		SetupPlayerNetworkItem(obj);
		// vars
		obj->m_Item = droppedItems[i];
#ifdef MISSIONS
		if (m_MissionsProgress)
		{
			m_MissionsProgress->PerformItemAction(Mission::ITEM_Drop, obj->m_Item.itemID, obj->GetHashID());
		}
#endif
	}
}

void obj_ServerPlayer::StartInventoryOp()
{
	// check if items was changed
	bool invChanged = false;
	for (int i = 0; i < wiCharDataFull::CHAR_MAX_BACKPACK_SIZE; i++)
	{
		const wiInventoryItem& w1 = loadout_->Items[i];
		const wiInventoryItem& w2 = savedLoadout_.Items[i];
		if (w1 == w2)
			continue;

		invChanged = true;
		break;
	}

	if (invChanged)
	{
		gServerLogic.ApiPlayerUpdateChar(this); // force update player inventory before working with global inventory
	}

	weapDataReqExp = -1;   // reset waiting for weapon data report, as potentially we have race condition here
	inventoryOpActive_ = true; // set flag that we're working with global inventory 
}

int obj_ServerPlayer::GetBackpackSlotForItem(const wiInventoryItem& itm)
{
	int slot_exist = -1;
	int slot_free = -1;

	const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(itm.itemID);

	// remap array so armor will be checked first before items
	static int slotsPriority[] = {
		wiCharDataFull::CHAR_LOADOUT_WEAPON1, wiCharDataFull::CHAR_LOADOUT_WEAPON2,
		wiCharDataFull::CHAR_LOADOUT_ARMOR, wiCharDataFull::CHAR_LOADOUT_HEADGEAR,
		wiCharDataFull::CHAR_LOADOUT_ITEM1, wiCharDataFull::CHAR_LOADOUT_ITEM2, wiCharDataFull::CHAR_LOADOUT_ITEM3, wiCharDataFull::CHAR_LOADOUT_ITEM4
	};
	COMPILE_ASSERT(ARRAYSIZE(slotsPriority) == wiCharDataFull::CHAR_REAL_BACKPACK_IDX_START);

	for (int k = 0; k < loadout_->BackpackSize; ++k)
	{
		// dirty hack due to fucked up indexing of quickslots\headarmor
		int i = k < wiCharDataFull::CHAR_REAL_BACKPACK_IDX_START ? slotsPriority[k] : k;
		const wiInventoryItem& bcpItm = loadout_->Items[i];

		// can stack only same items
		if (bcpItm.CanStackWith(itm)) {
			slot_exist = i;
			break;
		}

		// check if we can place that item to loadout slot
		bool canPlace = storecat_CanPlaceItemToSlot(itemCfg, i);
		if (canPlace && bcpItm.itemID == 0 && slot_free == -1) {
			slot_free = i;
		}
	}

	if (slot_exist >= 0)
		return slot_exist;
	return slot_free;
}

void obj_ServerPlayer::AddItemToBackpackSlot(int slot, const wiInventoryItem& itm)
{
	r3d_assert(slot >= 0 && slot <= loadout_->BackpackSize);
	r3d_assert(itm.quantity > 0); // should never be zero :)
	r3d_assert(itm.itemID > 0); // should never be zero :)

	if (loadout_->Items[slot].itemID > 0)
	{
		// modify existing slot in backpack, we ignore Var1 here as it should be checked before.
		int slot_exist = slot;
		r3d_assert(loadout_->Items[slot_exist].itemID == itm.itemID);
		if (loadout_->Items[slot_exist].InventoryID == 0)
			r3dOutToLog("InventoryID == 0 for %d\n", itm.itemID);
		r3d_assert(loadout_->Items[slot_exist].InventoryID > 0);

		AdjustBackpackSlotQuantity(slot_exist, itm.quantity);
	}
	else
	{
		// add into new slot
		int slot_free = slot;
		r3d_assert(loadout_->Items[slot_free].itemID == 0);
		loadout_->Items[slot_free] = itm;

		// generate temporary unique InventoryID for added item
		if (loadout_->Items[slot_free].InventoryID == 0)
		{
			loadout_->Items[slot_free].InventoryID = tempInventoryID++;
		}

		// report to client
		PKT_S2C_BackpackAddNew_s n;
		n.SlotTo = (BYTE)slot_free;
		n.Item = loadout_->Items[slot_free];
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));

		OnBackpackChanged(slot_free);
	}
}

void obj_ServerPlayer::AdjustBackpackSlotQuantity(int slot, int quantity, bool isAttachmentReplyRequired)
{
	wiInventoryItem& bckItm = loadout_->Items[slot];
	bckItm.quantity += quantity;

	// report to client
	PKT_S2C_BackpackModify_s n;
	n.SlotTo = (BYTE)slot;
	n.Quantity = (WORD)bckItm.quantity;
	n.dbg_ItemID = bckItm.itemID;
	n.IsAttachmentReplyReq = isAttachmentReplyRequired;
	gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));

	if (bckItm.quantity <= 0)
		bckItm.Reset();

	OnBackpackChanged(slot);
}

bool obj_ServerPlayer::IsHaveBackpackItem(uint32_t itemID, int quantity, bool remove)
{
	int numItems = 0;
	int left = quantity;

	for (int i = 0; i < loadout_->BackpackSize; i++)
	{
		const wiInventoryItem& wi = loadout_->Items[i];
		if (wi.itemID != itemID)
			continue;

		numItems += wi.quantity;
		if (remove && left > 0)
		{
			int n = R3D_MIN(left, wi.quantity);
			left -= n;
			AdjustBackpackSlotQuantity(i, -n);
		}
	}

	return numItems >= quantity;
}

void obj_ServerPlayer::AddItemToInventory(__int64 InventoryID, const wiInventoryItem& itm)
{
	wiInventoryItem* invItm = profile_.getInventorySlot(InventoryID);
	if (invItm)
	{
		r3d_assert(invItm->itemID == itm.itemID);

		// modify quantity
		invItm->quantity += itm.quantity;

		PKT_S2C_InventoryModify_s n2;
		n2.InventoryID = invItm->InventoryID;
		n2.Quantity = invItm->quantity;
		n2.dbg_ItemID = invItm->itemID;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	}
	else
	{
		// new item in global inventory
		invItm = profile_.getFreeInventorySlot();
		r3d_assert(invItm && "must check for free slots before initiaing global inventory op");
		*invItm = itm;
		invItm->InventoryID = InventoryID;

		PKT_S2C_InventoryAddNew_s n2;
		n2.Item = *invItm;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	}
}

bool obj_ServerPlayer::UseItem_CreateNote(const PKT_C2S_CreateNote_s& n)
{
	if (!r3d_vector_isFinite(n.pos))
	{
		return false;
	}

	if (n.SlotFrom >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "PKT_C2S_CreateNote_s",
			"slot: %d", n.SlotFrom);
		return false;
	}
	wiInventoryItem& wi = loadout_->Items[n.SlotFrom];
	uint32_t usedItemId = wi.itemID;

	if (wi.itemID != WeaponConfig::ITEMID_PieceOfPaper) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "PKT_C2S_CreateNote_s",
			"itemid: %d vs %d", wi.itemID, WeaponConfig::ITEMID_PieceOfPaper);
		return false;
	}
	if (wi.quantity <= 0) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "PKT_C2S_CreateNote_s",
			"%d", wi.quantity);
		return false;
	}

	// remove used item
	wi.quantity--;
	if (wi.quantity <= 0) {
		wi.Reset();
		OnBackpackChanged(n.SlotFrom);
	}

	// create network object
	obj_Note* obj = (obj_Note*)srv_CreateGameObject("obj_Note", "obj_Note", n.pos);
	SetupPlayerNetworkItem(obj);
	// create params
	obj->srvObjParams_.ItemID = WeaponConfig::ITEMID_PieceOfPaper;
	obj->srvObjParams_.Var1 = n.TextFrom;
	obj->srvObjParams_.Var2 = n.TextSubj;
	obj->OnCreate();

	CJobAddServerObject* job = new CJobAddServerObject(obj);
	g_AsyncApiMgr->AddJob(job);

	return true;
}

bool obj_ServerPlayer::IsSwimming()
{
	PxRaycastHit hit;
	PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_STATIC_MASK, 0, 0, 0), PxSceneQueryFilterFlags(PxSceneQueryFilterFlag::eSTATIC | PxSceneQueryFilterFlag::eDYNAMIC));
	bool hitResult = g_pPhysicsWorld->raycastSingle(PxVec3(GetPosition().x, GetPosition().y + 0.5f, GetPosition().z), PxVec3(0, -1, 0), 500.0f, PxSceneQueryFlags(PxSceneQueryFlag::eIMPACT), hit, filter);
	r3dPoint3D posForWater = GetPosition();
	if (hitResult)
		posForWater = r3dPoint3D(hit.impact.x, hit.impact.y, hit.impact.z);	// This is the ground position underwater.

	float waterDepth = getWaterDepthAtPos(posForWater);

	const float allowedDepth = 1.5f;
	if (waterDepth > allowedDepth) // too deep, start swimming
	{
		float waterLevel = hit.impact.y + waterDepth;
		if (waterLevel >= GetPosition().y)
		{
			// Keep the player from sinking too far into the water,
			// especially for first person view.
			if ((waterLevel - GetPosition().y) < (waterLevel - allowedDepth))
				SetPosition(r3dPoint3D(GetPosition().x, waterLevel - allowedDepth, GetPosition().z));
			return true;
		}
	}

	return false;
}

//bool obj_ServerPlayer::IsOverWater(float& waterDepth)
//{
//	PxRaycastHit hit;
//	PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_STATIC_MASK,0,0,0), PxSceneQueryFilterFlags(PxSceneQueryFilterFlag::eSTATIC|PxSceneQueryFilterFlag::eDYNAMIC));
//	bool hitResult = g_pPhysicsWorld->raycastSingle(PxVec3(GetPosition().x, GetPosition().y + 0.5f, GetPosition().z), PxVec3(0, -1, 0), 500.0f, PxSceneQueryFlags(PxSceneQueryFlag::eIMPACT), hit, filter);
//
//	// If can't find the ground, then the answer is indeterminable, assumption is not over water.
//	if( !hitResult )
//		return false;
//
//	r3dPoint3D posForWater = r3dPoint3D(hit.impact.x, hit.impact.y, hit.impact.z);	// This is the ground position over/underwater.
//	waterDepth = getWaterDepthAtPos(posForWater);
//	if( waterDepth < 0 )
//		return false;
//	float waterLevel = hit.impact.y + waterDepth;
//
//	return GetPosition().y >= waterLevel;
//}

void obj_ServerPlayer::TeleportPlayer(const r3dPoint3D& pos)
{
	SetPosition(pos);
	netMover.SrvSetCell(GetPosition());
	loadout_->GamePos = GetPosition();

	moveInited = false;
	gServerLogic.UpdateNetObjVisData(this);
	// UpdateVisThread = (HANDLE)_beginthreadex(NULL, 0, UpdateVisDataTeleport, (void*)this, 0, NULL);
}

bool obj_ServerPlayer::CheckForFastMove()
{
	if (!moveInited)
		return false;

	// check every 5 sec and check against sprint speed with bigger delta
	if (m_PlayerState >= PLAYER_MOVE_CROUCH && m_PlayerState <= PLAYER_MOVE_CROUCH_AIM)
		moveAccumTime[3] += r3dGetFrameTime();
	else if (m_PlayerState >= PLAYER_MOVE_PRONE && m_PlayerState <= PLAYER_PRONE_IDLE)
		moveAccumTime[2] += r3dGetFrameTime();
	else if (m_PlayerState == PLAYER_MOVE_SPRINT) // to allow moving with sprint speed only in sprint mode
		moveAccumTime[1] += r3dGetFrameTime();
	else if (m_PlayerState == PLAYER_SWIM_SLOW)
		moveAccumTime[3] += r3dGetFrameTime();
	else if (m_PlayerState == PLAYER_SWIM_FAST)
		moveAccumTime[1] += r3dGetFrameTime();
#ifdef VEHICLES_ENABLED
	else if (m_PlayerState == PLAYER_VEHICLE_DRIVER || m_PlayerState == PLAYER_VEHICLE_PASSENGER)
		moveAccumTime[4] += r3dGetFrameTime();
#endif
	else
		moveAccumTime[0] += r3dGetFrameTime();

	if ((moveAccumTime[0] + moveAccumTime[1] + moveAccumTime[2] + moveAccumTime[3]) < 5.0f)
		return false;

	float avgSpeedCrouch = moveAccumDist[3] / moveAccumTime[3];
	float avgSpeedProne = moveAccumDist[2] / moveAccumTime[2];
	float avgSpeedSprint = moveAccumDist[1] / moveAccumTime[1];
	float avgSpeedReg = moveAccumDist[0] / moveAccumTime[0];

#ifdef VEHICLES_ENABLED
	float avgSpeedVehicle = moveAccumDist[4] / moveAccumTime[4];
#endif

	// fix for when time is less than one second. happens when during 5 seconds check period, we have 0.5second in run state and small distance. division result in huge speed
	if (moveAccumTime[0] < 1.0f)
		avgSpeedReg = 0.0f;
	if (moveAccumTime[1] < 1.0f)
		avgSpeedSprint = 0.0f;
	if (moveAccumTime[2] < 1.0f)
		avgSpeedProne = 0.0f;
	if (moveAccumTime[3] < 1.0f)
		avgSpeedCrouch = 0.0f;
#ifdef VEHICLES_ENABLED
	if (moveAccumTime[4] < 1.0f)
		avgSpeedVehicle = 0.0f;
#endif

	bool isCheat = false;
	//r3dOutToLog("avgSpeed: %f vs %f\n", avgSpeed, GPP_Data.AI_SPRINT_SPEED);

	float maxRegSpeed = GPP_Data.AI_RUN_SPEED * 1.4f;
	float maxSprintSpeed = GPP_Data.AI_SPRINT_SPEED * 1.4f;
	float maxCrouchSpeed = GPP_Data.AI_RUN_SPEED * 1.4f * 0.4f;
	float maxProneSpeed = GPP_Data.AI_RUN_SPEED * 1.4f * 0.2f;
#ifdef VEHICLES_ENABLED
	float maxVehicleSpeed = 45.0f;
#endif
	if (loadout_->Alive && profile_.ProfileData.isDevAccount == 0) // don't kick devs for speed hack :)
	{
		if (avgSpeedReg > maxRegSpeed)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_FastMove, true, "CheatFastMove Reg",
				"dist: %f for %f, speed:%f\n",
				moveAccumDist[0], moveAccumTime[0], avgSpeedReg);
			isCheat = true;
		}
		if (avgSpeedSprint > maxSprintSpeed)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_FastMove, true, "CheatFastMove Sprint",
				"dist: %f for %f, speed:%f\n",
				moveAccumDist[1], moveAccumTime[1], avgSpeedSprint);
			isCheat = true;
		}
		if (avgSpeedProne > maxProneSpeed)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_FastMove, true, "CheatFastMove Prone",
				"dist: %f for %f, speed:%f\n",
				moveAccumDist[2], moveAccumTime[2], avgSpeedProne);
			isCheat = true;
		}
		if (avgSpeedCrouch > maxCrouchSpeed)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_FastMove, true, "CheatFastMove Crouch",
				"dist: %f for %f, speed:%f\n",
				moveAccumDist[3], moveAccumTime[3], avgSpeedCrouch);
			isCheat = true;
		}
#ifdef VEHICLES_ENABLED
		if (avgSpeedVehicle > maxVehicleSpeed)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_FastMove, true, "CheatFastMove Vehicle",
				"dist: %f for %f, speed:%f\n",
				moveAccumDist[4], moveAccumTime[4], avgSpeedVehicle);
			isCheat = true;
		}
#endif
	}

	// reset accomulated vars
	moveAccumTime[0] = 0;
	moveAccumTime[1] = 0;
	moveAccumTime[2] = 0;
	moveAccumTime[3] = 0;
#ifdef VEHICLES_ENABLED
	moveAccumTime[4] = 0;
#endif
	moveAccumDist[0] = 0;
	moveAccumDist[1] = 0;
	moveAccumDist[2] = 0;
	moveAccumDist[3] = 0;
#ifdef VEHICLES_ENABLED
	moveAccumDist[4] = 0;
#endif
	return isCheat;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PacketBarrier_s& n)
{
	// client switched to next sequence
	clientPacketSequence++;
	r3dOutToLog("peer%02d PKT_C2C_PacketBarrier_s %s %d vs %d\n", peerId_, packetBarrierReason, myPacketSequence, clientPacketSequence);
	packetBarrierReason = "";

	// reset move cheat detection
	moveInited = false;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_MoveSetCell_s& n)
{
	// if by some fucking unknown method you appeared at 0,0,0 - don't do that!
	if (gServerLogic.ginfo_.mapId != GBGameInfo::MAPID_ServerTest && n.pos.Length() < 10
#ifdef ENABLE_BATTLE_ROYALE
		//AlexRedd:: BR mode
		&& !isGameBattleRoyale
#endif //ENABLE_BATTLE_ROYALE
		)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "ZeroTeleport",
			"%f %f %f",
			n.pos.x, n.pos.y, n.pos.z);
		return;
	}

	if (!r3d_vector_isFinite(n.pos))
	{
		return;
	}


	if (moveInited)
	{
		// for now we will check ONLY ZX, because somehow players is able to fall down
		r3dPoint3D p1 = netMover.SrvGetCell();
		r3dPoint3D p2 = n.pos;
		p1.y = 0;
		p2.y = 0;
		float dist = (p1 - p2).Length();

		//r3dOutToLog("MoveSetCell(%d): plrPos=%.2f, %.2f, %2.f\n", peerId_, n.pos.x, n.pos.y, n.pos.z);

		/*if(m_PlayerState >= PLAYER_MOVE_CROUCH && m_PlayerState <= PLAYER_MOVE_CROUCH_AIM)
			moveAccumDist[3] += dist;
			else if(m_PlayerState >= PLAYER_MOVE_PRONE && m_PlayerState <= PLAYER_PRONE_IDLE)
			moveAccumDist[2] += dist;
			else if(m_PlayerState == PLAYER_MOVE_SPRINT) // to allow moving with sprint speed only in sprint mode
			moveAccumDist[1] += dist;
			else if(m_PlayerState == PLAYER_SWIM_SLOW)
			moveAccumDist[3] += dist;
			else if(m_PlayerState == PLAYER_SWIM_FAST)
			moveAccumDist[1] += dist;
			else
			moveAccumDist[0] += dist;*/

		// check for teleport - more that 3 sec of sprint speed. MAKE sure that max dist is more that current netMover.cellSize
		if (loadout_->Alive && dist > GPP_Data.AI_SPRINT_SPEED * 3.0f && profile_.ProfileData.isDevAccount == 0) // don't kick devs for speed hack :)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_FastMove, true, (dist > 500.0f ? "huge_teleport" : "teleport"),
				"%f, srvGetCell: %.2f, %.2f, %.2f; n.pos: %.2f, %.2f, %.2f",
				dist,
				netMover.SrvGetCell().x, netMover.SrvGetCell().y, netMover.SrvGetCell().z,
				n.pos.x, n.pos.y, n.pos.z
				);
			return;
		}
	}

	netMover.SetCell(n);
	SetPosition(n.pos);

	// keep them guaranteed
	RelayPacket(&n, sizeof(n), true);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_MoveRel_s& n)
{
	// decode move
	const CNetCellMover::moveData_s& md = netMover.DecodeMove(n);

	m_PlayerRotation = md.turnAngle;
	int prevPlayerState = m_PlayerState;
	m_PlayerState = md.state & 0xFF; // PlayerState&0xFF

	// ptumik (8/1/2013)
	// check for state being the same, as otherwise you can instantly disconnect by crouching up and down very quickly.
	// problem with that check, is that hacker can write a program that would allow him to move with any speed by changing his playerState from crouch and back each frame
	// original reason why this check was added is to fight with "combatlogging", when player can instantly disconnect himself from the game and save himself from being killed
	if (moveInited && m_PlayerState == prevPlayerState)
	{
		// for now we will check ONLY ZX, because somehow players is able to fall down
		r3dPoint3D p1 = GetPosition();
		r3dPoint3D p2 = md.pos;
		p1.y = 0;
		p2.y = 0;
		float dist = (p1 - p2).Length();

		//r3dOutToLog("MoveRel(%d): plrPos=%.2f, %.2f, %2.f\n", peerId_, md.pos.x, md.pos.y, md.pos.z);

		if (m_PlayerState >= PLAYER_MOVE_CROUCH && m_PlayerState <= PLAYER_MOVE_CROUCH_AIM)
			moveAccumDist[3] += dist;
		else if (m_PlayerState >= PLAYER_MOVE_PRONE && m_PlayerState <= PLAYER_PRONE_IDLE)
			moveAccumDist[2] += dist;
		else if (m_PlayerState == PLAYER_MOVE_SPRINT) // to allow moving with sprint speed only in sprint mode
			moveAccumDist[1] += dist;
		else if (m_PlayerState == PLAYER_SWIM_SLOW)
			moveAccumDist[3] += dist;
		else if (m_PlayerState == PLAYER_SWIM_FAST)
			moveAccumDist[1] += dist;
#ifdef VEHICLES_ENABLED
		else if (m_PlayerState == PLAYER_VEHICLE_DRIVER || m_PlayerState == PLAYER_VEHICLE_PASSENGER)
			moveAccumDist[4] += dist;
#endif
		else
			moveAccumDist[0] += dist;
	}

	// check if we need to reset accomulated speed
	if (!moveInited)
	{
		moveInited = true;
		moveAccumTime[4] = 0.0f;
		moveAccumTime[3] = 0.0f;
		moveAccumTime[2] = 0.0f;
		moveAccumTime[1] = 0.0f;
		moveAccumTime[0] = 0.0f;
		moveAccumDist[4] = 0.0f;
		moveAccumDist[3] = 0.0f;
		moveAccumDist[2] = 0.0f;
		moveAccumDist[1] = 0.0f;
		moveAccumDist[0] = 0.0f;
	}

	// update last action if we moved or rotated
	if ((GetPosition() - md.pos).Length() > 0.01f || m_PlayerRotation != md.turnAngle)
	{
		lastPlayerAction_ = r3dGetTime();
	}

	SetPosition(md.pos);

	loadout_->GamePos = GetPosition();
	loadout_->GameDir = m_PlayerRotation;

	RelayPacket(&n, sizeof(n), false);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_MoveCameraLocation_s& n)
{
	r3dPoint3D camDir;
	camDir.x = n.cam_dir_x;
	camDir.y = n.cam_dir_y;
	camDir.z = n.cam_dir_z;

	r3dPoint3D camPos;
	camPos.x = n.cam_loc_x;
	camPos.y = n.cam_loc_y;
	camPos.z = n.cam_loc_z;

	lastCamPos = camPos;
	lastCamDir = camDir;

	//	r3dOutToLog("delta: %.3f, %.3f, %.3f;\n", (((float(n.cam_rel_x)/255.0f)*10.0f)-5.0f), (((float(n.cam_rel_y)/255.0f)*10.0f)-5.0f), (((float(n.cam_rel_z)/255.0f)*10.0f)-5.0f));
	//	r3dOutToLog("camPos: %.3f, %.3f, %.3f; dir: %.3f, %.3f, %.3f\n", camPos.x, camPos.y, camPos.z, camDir.x, camDir.y, camDir.z);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerJump_s& n)
{
	RelayPacket(&n, sizeof(n), true);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_PlayerEquipAttachment_s& n)
{
	if (n.wid >= 2) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"wid: %d", n.wid);
		return;
	}
	if (m_WeaponArray[n.wid] == NULL) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"nowpn: %d", n.wid);
		return;
	}
	if (n.AttmSlot >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"slot: %d", n.AttmSlot);
		return;
	}
	if (n.dbg_WeaponID != m_WeaponArray[n.wid]->getConfig()->m_itemID) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"wid: %d %d", n.dbg_WeaponID, m_WeaponArray[n.wid]->getConfig()->m_itemID);
		return;
	}
	if (n.dbg_AttmID != loadout_->Items[n.AttmSlot].itemID) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"aid: %d %d", n.dbg_AttmID, loadout_->Items[n.AttmSlot].itemID);
		return;
	}

	// get attachment config
	wiInventoryItem& wi = loadout_->Items[n.AttmSlot];
	const WeaponAttachmentConfig* attachCfg = g_pWeaponArmory->getAttachmentConfig(wi.itemID);
	if (!attachCfg) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"bad itemid: %d", wi.itemID);
		return;
	}

	ServerWeapon* wpn = m_WeaponArray[n.wid];
	// verify that attachment is legit and can go into this weapon
	if (!wpn->m_pConfig->isAttachmentValid(attachCfg))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, false, "attachment",
			"attmid: %d, wpn:%d, slot:%d", wi.itemID, wpn->m_pConfig->m_itemID, n.AttmSlot);
		return;
	}

	r3dOutToLog("%s: equip attachment %s for %s\n", userName, attachCfg->m_StoreName, wpn->getConfig()->m_StoreName); CLOG_INDENT;

	// set wpn attachment
	wpn->m_Attachments[attachCfg->m_type] = attachCfg;
	wpn->recalcAttachmentsStats();

	loadout_->Attachment[n.wid].attachments[attachCfg->m_type] = attachCfg->m_itemID;

	m_clipAttmChanged = (attachCfg->m_type == WPN_ATTM_CLIP);

	// report new loadout in case if flashlight/laser was changed
	OnLoadoutChanged();

	// report to other players
	OnAttachmentChanged(n.wid, attachCfg->m_type);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_PlayerRemoveAttachment_s& n)
{
	if (n.wid >= 2) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"wid: %d", n.wid);
		return;
	}
	if (m_WeaponArray[n.wid] == NULL) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"nowpn: %d", n.wid);
		return;
	}
	if (n.WpnAttmType >= WPN_ATTM_MAX) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "attachment",
			"WpnAttmType: %d", n.WpnAttmType);
		return;
	}

	ServerWeapon* wpn = m_WeaponArray[n.wid];

	// remove wpn attachment, equip default if have
	wpn->m_Attachments[n.WpnAttmType] = g_pWeaponArmory->getAttachmentConfig(wpn->m_pConfig->FPSDefaultID[n.WpnAttmType]);
	wpn->recalcAttachmentsStats();

	loadout_->Attachment[n.wid].attachments[n.WpnAttmType] = 0;

	// report new loadout in case if flashlight/laser was changed
	OnLoadoutChanged();

	// report to other players
	OnAttachmentChanged(n.wid, n.WpnAttmType);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_CallForHelpReq_s& n)
{
	if (r3dGetTime() < (lastCallForHelp + 600)) // 10min
	{
		PKT_S2C_CallForHelpAns_s n2;
		n2.ansCode = PKT_S2C_CallForHelpAns_s::CFH_Duplicate;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}
	if (r3dGetTime() < (lastCallForHelp + 3600)) // one hour
	{
		PKT_S2C_CallForHelpAns_s n2;
		n2.ansCode = PKT_S2C_CallForHelpAns_s::CFH_Time;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	r3dscpy(CallForHelp_distress, n.distress);
	r3dscpy(CallForHelp_reward, n.reward);
	lastCallForHelp = r3dGetTime();
	lastCallForHelpLocation = GetPosition();

	PKT_S2C_CallForHelpAns_s n2;
	n2.ansCode = PKT_S2C_CallForHelpAns_s::CFH_OK;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

	// send call for help to everyone
	PKT_S2C_CallForHelpEvent_s n3;
	n3.playerIdx = (WORD)(GetNetworkID() - NETID_PLAYERS_START);
	r3dscpy(n3.distress, CallForHelp_distress);
	r3dscpy(n3.reward, CallForHelp_reward);
	n3.timeLeft = 600;
	n3.locX = lastCallForHelpLocation.x;
	n3.locZ = lastCallForHelpLocation.z;
	for (int i = 0; i < gServerLogic.curPlayers_; ++i)
	{
		obj_ServerPlayer* pl = gServerLogic.plrList_[i];
		gServerLogic.p2pSendRawToPeer(pl->peerId_, &n3, sizeof(n3));
	}
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerSwitchFlashlight_s& n)
{
	m_isFlashlightOn = n.isFlashlightOn == 1 ? true : false;
	RelayPacket(&n, sizeof(n));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerSwitchWeapon_s& n)
{
	if (n.wid >= NUM_WEAPONS_ON_PLAYER) {
		gServerLogic.LogInfo(peerId_, "SwitchWeapon", "wrong weaponslot %d", n.wid);
		return;
	}

	// ptumik: because server creating weapons only for 1 and 2 slots, user can switch to usable items. 
	// so, having m_WeaponArray[n.wid] == NULL here is totally legitimate. 
	// also, because of this, before using m_WeaponArray[m_SelectedWeapon] we need to check that it is not NULL

	m_SelectedWeapon = n.wid;

	RelayPacket(&n, sizeof(n));
}

void obj_ServerPlayer::Trade_Request(const PKT_C2C_TradeRequest_s& n)
{
	obj_ServerPlayer* target = IsServerPlayer(GameWorld().GetNetworkObject(n.targetId));
	if (!target || (GetPosition() - target->GetPosition()).Length() > 5.0f) { // can only initiate or request within 5 meters of each other
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, true, "dist_req",
			"");
		return;
	}

	if (target->tradeTargetId > 0)
	{
		// player is trading with other player
		PKT_C2C_TradeRequest_s n2(PKT_C2C_TradeRequest_s::TRADE_Busy, target);
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	if (tradeRequestTo == n.targetId) // prevent spam
	{
		return;
	}

	tradeRequestTo = n.targetId;

	// relay that packet to target now
	PKT_C2C_TradeRequest_s n2(n.code, this);
	gServerLogic.p2pSendToPeer(target->peerId_, target, &n2, sizeof(n2));
}

void obj_ServerPlayer::Trade_Answer(const PKT_C2C_TradeRequest_s& n)
{
	obj_ServerPlayer* target = IsServerPlayer(GameWorld().GetNetworkObject(n.targetId));
	if (!target || (GetPosition() - target->GetPosition()).Length() > 5.0f) { // can only initiate or request within 5 meters of each other
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, true, "dist_ans",
			"");
		return;
	}

	if (target->tradeRequestTo != GetNetworkID() || target->tradeTargetId > 0 || tradeTargetId > 0)
	{
		// player is trying to trade with another player
		PKT_C2C_TradeRequest_s n2(PKT_C2C_TradeRequest_s::TRADE_Busy, target);
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}
	target->tradeRequestTo = 0;
	tradeRequestTo = 0;

	// iniate trade for both players
	{
		PKT_C2C_TradeRequest_s n2(PKT_C2C_TradeRequest_s::TRADE_Open, this);
		gServerLogic.p2pSendToPeer(target->peerId_, target, &n2, sizeof(n2));
		PKT_C2C_TradeRequest_s n3(PKT_C2C_TradeRequest_s::TRADE_Open, target);
		gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));

		target->tradeTargetId = toP2pNetId(GetNetworkID());
		tradeTargetId = toP2pNetId(target->GetNetworkID());

		tradeStatus = 0;
		target->tradeStatus = 0;
		tradeLastChangeTime = -9999.0f;
		target->tradeLastChangeTime = -9999.0f;

		memset(&tradeSlots, 0, sizeof(tradeSlots));
		memset(&target->tradeSlots, 0, sizeof(tradeSlots));
	}
}

void obj_ServerPlayer::Trade_Close()
{
	// send close packet and clear trade vars
	PKT_C2C_TradeRequest_s n2(PKT_C2C_TradeRequest_s::TRADE_Close, this);
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

	// do same for other side if it still exists
	obj_ServerPlayer* target = IsServerPlayer(GameWorld().GetNetworkObject(tradeTargetId));
	if (target)
	{
		gServerLogic.p2pSendToPeer(target->peerId_, target, &n2, sizeof(n2));
		target->tradeTargetId = 0;
		target->tradeStatus = 0;
		target->tradeLastChangeTime = -9999.0f;
	}

	tradeTargetId = 0;
	tradeStatus = 0;
	tradeLastChangeTime = -9999.0f;
}

void obj_ServerPlayer::AirDrop_Unlock()
{
	GameObject* obj = GameWorld().GetNetworkObject(airdropId);
	if (obj == NULL)
		return;
	if (obj->Class->Name != "obj_ServerAirdrop")
		return;
	obj_ServerAirdrop* airdrop = (obj_ServerAirdrop*)obj;
	if (airdrop)
		airdrop->LockAirDrop(this, 0);
}

void obj_ServerPlayer::Trade_Confirm()
{
	obj_ServerPlayer* target = IsServerPlayer(GameWorld().GetNetworkObject(tradeTargetId));
	if (!target || (GetPosition() - target->GetPosition()).Length() > 5.0f) { // can only initiate or request within 5 meters of each other
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, true, "dist_confirm",
			"");
		return;
	}

	if (!Trade_CheckCanPlace(target))
		return;

	if ((r3dGetTime() - tradeLastChangeTime) < 4.5f) // do not allow to confirm trade within 5 seconds of any trade changes. todo: send confirm packet to client, that his accept was accepted by server?
		return;

	// prevent hack when player accepted trade and then initiated character disconnect (via some program)
	if (wasDisconnected_ || target->wasDisconnected_)
	{
		Trade_Close();
		return;
	}

	if (isHighPing) // AlexRedd:: fixing new lag to prevent duping with trade! 
	{
		Trade_Close();

		char chatmessage[128] = { 0 };
		PKT_C2C_ChatMessage_s n1;
		sprintf(chatmessage, "Your internet connection is very unstable for trade items");
		r3dscpy(n1.gamertag, "[System]");
		r3dscpy(n1.msg, chatmessage);
		n1.msgChannel = 1;
		n1.userFlag = 2;
		gServerLogic.net_->SendToPeer(&n1, sizeof(n1), peerId_, true);
		return;
	}

	// confirm our trade
	tradeStatus = 1;

	// and relay it to opposite side
	PKT_C2C_TradeRequest_s n2(PKT_C2C_TradeRequest_s::TRADE_Confirm, this);
	gServerLogic.p2pSendToPeer(target->peerId_, target, &n2, sizeof(n2));

	// if both sides confirmed, initiate trade
	if (tradeStatus && target->tradeStatus)
	{
		// and do one more check, inventory maybe was changed (example: dropped headgear on hit)
		if (!Trade_CheckCanPlace(target) || !target->Trade_CheckCanPlace(this))
		{
			Trade_Close();
			return;
		}

		Trade_Commit(target);
		target->Trade_Commit(this);
		Trade_Close();
		return;
	}
}

int obj_ServerPlayer::Trade_CheckCanPlace(obj_ServerPlayer* target)
{
	r3d_assert(tradeTargetId == target->GetNetworkID());

	r3dOutToLog("Trade_CheckCanPlace %s vs %s\n", Name.c_str(), target->Name.c_str()); CLOG_INDENT;

	// make a copy of backpack before modifications (we have to simulate item addings). must be restored after function exit
	wiInventoryItem ItemsCopy[wiCharDataFull::CHAR_MAX_BACKPACK_SIZE];
	COMPILE_ASSERT(sizeof(ItemsCopy) == sizeof(target->loadout_->Items));
	memcpy(&ItemsCopy, &target->loadout_->Items, sizeof(target->loadout_->Items));

	// validate that we can place trade things to other player
	for (int i = 0; i < MAX_TRADE_SIZE; i++)
	{
		const tradeSlot_s& ts = tradeSlots[i];
		if (ts.Item.itemID == 0)
			continue;

		//AlexRedd:: Vip items (nodrop) 
		bool vipItem = GetVipItem(ts.SlotFrom);
		if (vipItem)
		{
			PKT_C2C_TradeRequest_s n3(PKT_C2C_TradeRequest_s::TRADE_Forbidden, target);
			gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
			return 0;
		}
		//

		const wiInventoryItem& wi1 = loadout_->Items[ts.SlotFrom];
		if (wi1.itemID != ts.Item.itemID)
		{
			memcpy(&target->loadout_->Items, &ItemsCopy, sizeof(target->loadout_->Items));

			// not sure yet how it happens (now we know! because of chance of headgear being dropped on headshot)
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, false, "trade_itm", "%d %d vs %d", ts.SlotFrom, wi1.itemID, ts.Item.itemID);
			gServerLogic.LogCheat(target->peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, false, "trade_itm", "%d %d vs %d", ts.SlotFrom, wi1.itemID, ts.Item.itemID);

			// and close the trade
			Trade_Close();
			return 0;
		}

		int SlotTo = target->GetBackpackSlotForItem(ts.Item);
		r3dOutToLog("%d to %d\n", ts.Item.itemID, SlotTo);
		if (SlotTo == -1)
		{
			PKT_C2C_TradeRequest_s n3(PKT_C2C_TradeRequest_s::TRADE_NoSpace, target);
			gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));

			tradeStatus = 0;
			memcpy(&target->loadout_->Items, &ItemsCopy, sizeof(target->loadout_->Items));
			return 0;
		}

		// same logic as AddItemToBackpackSlot but without network messages
		if (target->loadout_->Items[SlotTo].itemID > 0)
		{
			target->loadout_->Items[SlotTo].quantity += ts.Item.quantity;
		}
		else
		{
			target->loadout_->Items[SlotTo] = ts.Item;
		}
	}

	// check target weight (now their loadout contain our trade offers) and adjust with their trade offers to us
	float totalWeight = target->loadout_->getTotalWeight();
	for (int i = 0; i < MAX_TRADE_SIZE; i++)
	{
		const tradeSlot_s& ts = target->tradeSlots[i];
		if (ts.Item.itemID == 0)
			continue;

		const BaseItemConfig* bic = g_pWeaponArmory->getConfig(ts.Item.itemID);
		if (bic)
			totalWeight -= bic->m_Weight * ts.Item.quantity;
	}
	if (target->loadout_->Skills[CUserSkills::SKILL_Physical3])
		totalWeight *= 0.95f;
	if (target->loadout_->Skills[CUserSkills::SKILL_Physical7])
		totalWeight *= 0.9f;

	const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(target->loadout_->BackpackID);
	if (totalWeight > bc->m_maxWeight)
	{
		PKT_C2C_TradeRequest_s n3(PKT_C2C_TradeRequest_s::TRADE_NoWeight, target);
		gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));

		tradeStatus = 0;
		memcpy(&target->loadout_->Items, &ItemsCopy, sizeof(target->loadout_->Items));
		return 0;
	}

	memcpy(&target->loadout_->Items, &ItemsCopy, sizeof(target->loadout_->Items));
	return 1;
}

void obj_ServerPlayer::Trade_Commit(obj_ServerPlayer* target)
{
	r3d_assert(tradeTargetId == target->GetNetworkID());
	r3d_assert(tradeStatus > 0);

	r3dOutToLog("Trade_Commit %s vs %s\n", Name.c_str(), target->Name.c_str()); CLOG_INDENT;

	for (int i = 0; i < MAX_TRADE_SIZE; i++)
	{
		const tradeSlot_s& ts = tradeSlots[i];
		if (ts.Item.itemID == 0) continue;

		const wiInventoryItem& wi1 = loadout_->Items[ts.SlotFrom];
		r3d_assert(wi1.itemID == ts.Item.itemID);
		//r3d_assert(wi1.Var1 == ts.Item.Var1 && wi1.Var2 == ts.Item.Var2);// disabled for now
		r3d_assert(wi1.quantity >= ts.Item.quantity);

		// it was validated before in Trade_CheckCanPlace so it shoudn't assert
		int SlotTo = target->GetBackpackSlotForItem(ts.Item);
		r3d_assert(SlotTo != -1);

		target->AddItemToBackpackSlot(SlotTo, ts.Item);
		AdjustBackpackSlotQuantity(ts.SlotFrom, -ts.Item.quantity);
	}
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_TradeRequest_s& n)
{
#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())
	{
		return;
	}
#endif
#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode	
	if(isGameBattleRoyale)	
		return;
#endif //ENABLE_BATTLE_ROYALE

	switch (n.code)
	{
	default:
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, true, "code",
			"code: %d", n.code);
		break;

	case PKT_C2C_TradeRequest_s::TRADE_Request:
		Trade_Request(n);
		break;

	case PKT_C2C_TradeRequest_s::TRADE_Answer:
		Trade_Answer(n);
		break;

	case PKT_C2C_TradeRequest_s::TRADE_Close:
		Trade_Close();
		break;

	case PKT_C2C_TradeRequest_s::TRADE_Confirm:
		Trade_Confirm();
		break;
	}
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_TradeItem_s& n)
{
	obj_ServerPlayer* target = IsServerPlayer(GameWorld().GetNetworkObject(tradeTargetId));
	if (!target || (GetPosition() - target->GetPosition()).Length() > 5.0f) { // can only initiate or request within 5 meters of each other
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, true, "dist_offer",
			"");
		return;
	}

	if (n.BckSlot >= loadout_->BackpackSize || n.TradeSlot >= MAX_TRADE_SIZE) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, true, "Slots",
			"%d -> %d", n.BckSlot, n.TradeSlot);
		return;
	}

	wiInventoryItem& wi = loadout_->Items[n.BckSlot];

	if (n.Item.itemID > 0)
	{
		// see if player reported correct item to us
		if (wi.itemID != n.Item.itemID /*|| wi.Var1 != n.Item.Var1 || wi.Var2 != n.Item.Var2 || wi.Var3 != n.Item.Var3*/) {
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, true, "desync1 itm",
				"%d %d %d %d vs %d %d %d %d", wi.itemID, wi.Var1, wi.Var2, wi.Var3, n.Item.itemID, n.Item.Var1, n.Item.Var2, n.Item.Var3);
			return;
		}
		if (n.Item.quantity > wi.quantity) {
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, true, "desync1 q",
				"%d %d", wi.quantity, n.Item.quantity);
			return;
		}

		// fill expected trade slot
		tradeSlot_s& ts = tradeSlots[n.TradeSlot];
		ts.SlotFrom = n.BckSlot;
		ts.Item = n.Item;
	}
	else
	{
		// cleared slot
		tradeSlot_s& ts = tradeSlots[n.TradeSlot];
		ts.SlotFrom = -1;
		ts.Item.Reset();
	}

	// reset confirmation trade status
	tradeStatus = 0;
	target->tradeStatus = 0;
	tradeLastChangeTime = r3dGetTime();
	target->tradeLastChangeTime = r3dGetTime();

	// report trade to other side
	PKT_C2C_TradeItem_s n2;
	memcpy(&n2, &n, sizeof(n));
	gServerLogic.p2pSendToPeer(target->peerId_, target, &n2, sizeof(n2));
}

bool obj_ServerPlayer::UseItem_Barricade(const r3dPoint3D& pos, float rotX, uint32_t itemID)
{
	const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itemID);
	if (!wc)
		return false;

	if ((GetPosition() - pos).Length() > 5.0f)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "barricade distance > 5.0f",
			"%d",
			itemID
			);
		return false;
	}

	bool strongholdItem = (itemID == WeaponConfig::ITEMID_WoodenDoorBlock ||
		itemID == WeaponConfig::ITEMID_MetalWallBlock ||
		itemID == WeaponConfig::ITEMID_TallBrickWallBlock ||
		itemID == WeaponConfig::ITEMID_WoodenWallPiece ||
		itemID == WeaponConfig::ITEMID_ShortBrickWallPiece ||
		itemID == WeaponConfig::ITEMID_FarmBlock ||
		itemID == WeaponConfig::ITEMID_SolarWaterPurifier ||
		itemID == WeaponConfig::ITEMID_PlaceableLight ||
		itemID == WeaponConfig::ITEMID_SmallPowerGenerator ||
		itemID == WeaponConfig::ITEMID_BigPowerGenerator);

	// those items are only for stronghold
	if (gServerLogic.ginfo_.mapId != GBGameInfo::MAPID_WZ_Cliffside && strongholdItem && profile_.ProfileData.isDevAccount == 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "stronghold item",
			"%d",
			itemID
			);
		return false;
	}

	// spawn
	obj_ServerBarricade* shield = NULL;
	if (!strongholdItem)
		shield = (obj_ServerBarricade*)srv_CreateGameObject("obj_ServerBarricade", "barricade", pos);
	else
		shield = (obj_ServerBarricade*)srv_CreateGameObject("obj_StrongholdServerBarricade", "barricade", pos);

	SetupPlayerNetworkItem(shield);
	shield->m_ItemID = itemID;
	shield->m_Health = wc->m_AmmoDamage;
	shield->SetRotationVector(r3dPoint3D(rotX, 0, 0));

	CJobAddServerObject* job = new CJobAddServerObject(shield);
	g_AsyncApiMgr->AddJob(job);

	return true;
}

bool obj_ServerPlayer::UseItem_FarmBlock(const r3dPoint3D& pos, float rotX, uint32_t itemID)
{
	const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itemID);
	if (!wc)
		return false;

	if ((GetPosition() - pos).Length() > 5.0f)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "distance",
			"%d",
			itemID
			);
		return false;
	}

	// spawn
	obj_ServerFarmBlock* block = (obj_ServerFarmBlock*)srv_CreateGameObject("obj_ServerFarmBlock", "farmblock", pos);
	SetupPlayerNetworkItem(block);
	block->m_ItemID = itemID;
	block->SetRotationVector(r3dPoint3D(rotX, 0, 0));

	CJobAddServerObject* job = new CJobAddServerObject(block);
	g_AsyncApiMgr->AddJob(job);

	return true;
}

bool obj_ServerPlayer::UseItem_Lockbox(const r3dPoint3D& pos, float rotX, uint32_t itemID)
{
	const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itemID);
	if (!wc)
		return false;

#ifdef DISABLE_GI_ACCESS_ON_PTE_MAP
	if(gServerLogic.ginfo_.channel == 6) // no lockboxes on PTE map due to dupe
		return false;
#endif
#ifdef DISABLE_GI_ACCESS_ON_PTE_STRONGHOLD_MAP
	if(gServerLogic.ginfo_.channel == 6 && gServerLogic.ginfo_.mapId==GBGameInfo::MAPID_WZ_Cliffside) // no lockboxes on PTE map due to dupe
		return false;
#endif
#ifdef DISABLE_GI_ACCESS_FOR_CALI_SERVER
	if(gServerLogic.ginfo_.mapId==GBGameInfo::MAPID_WZ_California)
		return false;
#endif
#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())
		return false;
#endif

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode	
	if(isGameBattleRoyale)	
		return false;
#endif //ENABLE_BATTLE_ROYALE

	if ((GetPosition() - pos).Length() > 5.0f)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "distance",
			"%d",
			itemID
			);
		return false;
	}

	// spawn
	obj_ServerLockbox* lockbox = (obj_ServerLockbox*)srv_CreateGameObject("obj_ServerLockbox", "lockbox", pos);
	SetupPlayerNetworkItem(lockbox);
	lockbox->m_ItemID = itemID;
	lockbox->SetRotationVector(r3dPoint3D(rotX + 180, 0, 0));
	lockbox->lockboxOwnerId = profile_.CustomerID;

	CJobAddServerObject* job = new CJobAddServerObject(lockbox);
	g_AsyncApiMgr->AddJob(job);

	return true;
}

bool obj_ServerPlayer::UseItem_CarSpawner(const r3dPoint3D& pos, float rotX, uint32_t itemID, const char* ownerUserName)//AlexRedd:: car spawner
{
	const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itemID);
	if (!wc)
		return false;

	if ((GetPosition() - pos).Length() > 5.0f)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "veh distance",
			"%d",
			itemID
			);
		return false;
	}

	int vehicleType = 0;
	if (itemID == WeaponConfig::ITEMID_VehicleSpawner_Buggy)
		vehicleType = obj_Vehicle::VEHICLETYPE_BUGGY;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_Stryker)
		vehicleType = obj_Vehicle::VEHICLETYPE_STRYKER;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_ZombieKiller)
		vehicleType = obj_Vehicle::VEHICLETYPE_ZOMBIEKILLER;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_ScoutMilitary)
		vehicleType = obj_Vehicle::VEHICLETYPE_SCOUTMILITARY;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_BuggyGun)
		vehicleType = obj_Vehicle::VEHICLETYPE_BUGGYGUN;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_ScoutSurvivor)
		vehicleType = obj_Vehicle::VEHICLETYPE_SCOUTSURVIVOR;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_BoneCrusher)
		vehicleType = obj_Vehicle::VEHICLETYPE_BONECRUSHER;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_ScavengersScout)
		vehicleType = obj_Vehicle::VEHICLETYPE_SCAVENGERSSCOUT;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_Cubus)
		vehicleType = obj_Vehicle::VEHICLETYPE_CUBUS;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_Hippy)
		vehicleType = obj_Vehicle::VEHICLETYPE_HIPPY;
	else if (itemID == WeaponConfig::ITEMID_VehicleSpawner_Convertable)
		vehicleType = obj_Vehicle::VEHICLETYPE_CONVERTABLE;

	if (vehicleType < 0 || vehicleType > 10)
		return false;

	// spawn
	obj_Vehicle* vehicle = (obj_Vehicle*)srv_CreateGameObject("obj_Vehicle", "vehicle", pos);
	if (vehicle)
	{
		// create network object
		SetupPlayerNetworkItem(vehicle);
		vehicle->spawnObject = 0;
		vehicle->spawnIndex = -1;
		vehicle->SetVehicleType((obj_Vehicle::VehicleTypes)vehicleType);
		vehicle->SetRotationVector(r3dPoint3D(rotX - 90, 0, 0));

		// create params
		uint32_t VehItemID = 0;
		if (vehicleType == obj_Vehicle::VEHICLETYPE_BUGGY)
			VehItemID = 101412;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_STRYKER)
			VehItemID = 101413;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_ZOMBIEKILLER)
			VehItemID = 101414;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_SCOUTMILITARY)
			VehItemID = 101418;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_BUGGYGUN)
			VehItemID = 101419;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_SCOUTSURVIVOR)
			VehItemID = 101420;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_BONECRUSHER)
			VehItemID = 101421;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_SCAVENGERSSCOUT)
			VehItemID = 101422;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_CUBUS)
			VehItemID = 101423;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_HIPPY)
			VehItemID = 101424;
		else if (vehicleType == obj_Vehicle::VEHICLETYPE_CONVERTABLE)
			VehItemID = 101425;

		vehicle->m_ItemID = VehItemID;
		sprintf(vehicle->owner_user_name, "%s", ownerUserName);
		vehicle->OnCreate();

		CJobAddServerObject* job = new CJobAddServerObject(vehicle);
		g_AsyncApiMgr->AddJob(job);
		return true;
	}

	return false;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerUseItem_s& n)
{
	//gServerLogic.LogInfo(peerId_, "UseItem", "%d", n.dbg_ItemID); CLOG_INDENT;

	/*if(loadout_->Health<=0) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "useitem while dead",
		"slot: %d", n.SlotFrom);
		return;
		}*/

	if (!r3d_vector_isFinite(n.pos))
	{
		return;
	}


	if (n.SlotFrom >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "useitem",
			"slot: %d", n.SlotFrom);
		return;
	}
	wiInventoryItem& wi = loadout_->Items[n.SlotFrom];
	uint32_t usedItemId = wi.itemID;

	if (wi.itemID != n.dbg_ItemID) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "useitem",
			"itemid: %d vs %d", wi.itemID, n.dbg_ItemID);
		return;
	}

	if (wi.quantity <= 0) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "quantity",
			"%d", wi.quantity);
		return;
	}

	bool isBarricade = usedItemId == WeaponConfig::ITEMID_BarbWireBarricade || usedItemId == WeaponConfig::ITEMID_WoodShieldBarricade || usedItemId == WeaponConfig::ITEMID_WoodShieldBarricadeZB ||
		usedItemId == WeaponConfig::ITEMID_RiotShieldBarricade || usedItemId == WeaponConfig::ITEMID_RiotShieldBarricadeZB || usedItemId == WeaponConfig::ITEMID_SandbagBarricade || usedItemId == WeaponConfig::ITEMID_Lockbox ||
		usedItemId == WeaponConfig::ITEMID_WoodenDoorBlock || usedItemId == WeaponConfig::ITEMID_MetalWallBlock || usedItemId == WeaponConfig::ITEMID_TallBrickWallBlock || usedItemId == WeaponConfig::ITEMID_LockboxBig ||
		usedItemId == WeaponConfig::ITEMID_WoodenWallPiece || usedItemId == WeaponConfig::ITEMID_ShortBrickWallPiece || usedItemId == WeaponConfig::ITEMID_FarmBlock || usedItemId == WeaponConfig::ITEMID_LockboxSmall ||
		usedItemId == WeaponConfig::ITEMID_SolarWaterPurifier || usedItemId == WeaponConfig::ITEMID_PlaceableLight || usedItemId == WeaponConfig::ITEMID_SmallPowerGenerator ||
		usedItemId == WeaponConfig::ITEMID_BigPowerGenerator || usedItemId == WeaponConfig::ITEMID_SSBarricadeShield;
	if (isBarricade && (loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "barricade in safezone",
			"%d", wi.quantity);
		return;
	}
	if (isBarricade && gMasterServerLogic.shutdownLeft_ < 180 && gMasterServerLogic.shuttingDown_) // do not allow to place any objects when server is closing
	{
		return;
	}

	//
	const BaseItemConfig* itmC = g_pWeaponArmory->getConfig(usedItemId);
	if (!itmC)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "unknown item",
			"%d", usedItemId);
		return;
	}

	bool isMed = usedItemId == WeaponConfig::ITEMID_Antibiotics || usedItemId == WeaponConfig::ITEMID_Bandages || usedItemId == WeaponConfig::ITEMID_Bandages2 ||
		usedItemId == WeaponConfig::ITEMID_Painkillers || usedItemId == WeaponConfig::ITEMID_Medkit || usedItemId == WeaponConfig::ITEMID_CraftedBandages;
	if (isMed)
	{
		if ((r3dGetTime() - m_lastTimeUsedConsumable) < (m_currentConsumableCooldownTime*0.95f)) // 0.95f just to make sure that client and server will not desync a little bit due to net lag
			return;
		else
			m_lastTimeUsedConsumable = GPP_Data.c_fConsumableCooldownTimeTier[itmC->m_LevelRequired];
	}
	if (g_pWeaponArmory->getFoodConfig(usedItemId))
	{
		if ((r3dGetTime() - m_lastTimeUsedConsumable) < (m_currentConsumableCooldownTime*0.95f)) // 0.95f just to make sure that client and server will not desync a little bit due to net lag
			return;
		else
			m_lastTimeUsedConsumable = GPP_Data.c_fConsumableCooldownTimeTier[itmC->m_LevelRequired];
	}
	//new add
	if (usedItemId == WeaponConfig::ITEMID_AirHorn)
	{
		m_SpawnProtectedUntil = 0;
		//RelayPacket(&n, sizeof(n));
		PKT_C2C_PlayerUseItem_s n2;
		n2.SlotFrom = (BYTE)n.SlotFrom;
		n2.dbg_ItemID = n.dbg_ItemID;
		n2.pos = n.pos;
		n2.var1 = n.var1;
		n2.var2 = n.var2;
		n2.var3 = n.var3;
		n2.var4 = n.var4;
		gServerLogic.p2pBroadcastToActive(this, &n2, sizeof(n2));

		if (obj_ServerPlayer* targetPlr = IsServerPlayer(GameWorld().GetNetworkObject(n.FromID)))
		{
			if (this == targetPlr)
				gServerLogic.InformZombiesAboutSoundItemID(targetPlr, n.dbg_ItemID);
		}
		return;
	}
	//new add
	if (usedItemId == WeaponConfig::ITEMID_AerialDelivery)
	{
		wiInventoryItem wi;
		wi.itemID = 'ARDR';
		wi.quantity = 1;
		// create network object
		r3dPoint3D AirDropSpawn(0, 0, 0);

		if (Terrain3)
			AirDropSpawn.y = Terrain3->GetHeight(AirDropSpawn) + 300.0f;

		AirDropSpawn.x = GetPosition().x;
		AirDropSpawn.y = GetPosition().y + 300.0f;
		AirDropSpawn.z = GetPosition().z;

		obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", AirDropSpawn);
		obj->AirDropPos = AirDropSpawn;
		obj->m_FirstTime = 1;
		obj->ExpireFirstTime = r3dGetTime() + 10.0f;
		obj->SetPosition(AirDropSpawn);
		obj->m_isUseAirDrop = true;
		SetupPlayerNetworkItem(obj);
		// vars
		obj->m_Item = wi;
		obj->m_Item.quantity = 1;

		char msg[128] = "";
		sprintf(msg, "Delivering package to: %s", loadout_->Gamertag);
		PKT_C2C_ChatMessage_s n;
		n.userFlag = 2;
		n.msgChannel = 1;
		r3dscpy(n.msg, msg);
		r3dscpy(n.gamertag, "[AIRDROP]");
		gServerLogic.p2pBroadcastToAll(&n, sizeof(n), true);
	}

	RelayPacket(&n, sizeof(n));

	// remove used item
	wi.quantity--;
	if (wi.quantity <= 0) {
		wi.Reset();
		OnBackpackChanged(n.SlotFrom);
	}

	const FoodConfig* foodConfig = g_pWeaponArmory->getFoodConfig(usedItemId);
	if (foodConfig)
	{
		loadout_->Health += foodConfig->Health;   loadout_->Health = R3D_CLAMP(loadout_->Health, 0.0f, 100.0f);
		loadout_->Toxic += foodConfig->Toxicity; loadout_->Toxic = R3D_CLAMP(loadout_->Toxic, 0.0f, 100.0f);
		loadout_->Hunger -= foodConfig->Food;     loadout_->Hunger = R3D_CLAMP(loadout_->Hunger, 0.0f, 100.0f);
		loadout_->Thirst -= foodConfig->Water;    loadout_->Thirst = R3D_CLAMP(loadout_->Thirst, 0.0f, 100.0f);

		m_Stamina += GPP_Data.c_fSprintMaxEnergy*foodConfig->Stamina;
		m_Stamina = R3D_CLAMP((float)m_Stamina, 0.0f, GPP_Data.c_fSprintMaxEnergy);

#ifdef MISSIONS
		// Food items are used from the backpack, and no HashID exists for these items,
		// so we can only count the number we use.
		if (m_MissionsProgress)
		{
			m_MissionsProgress->PerformItemAction(Mission::ITEM_Use, usedItemId, 0, Mission::ITEMUSEON_Self);
		}
#endif
		return;
	}

	bool useOnOtherPlayer = usedItemId == WeaponConfig::ITEMID_Antibiotics || usedItemId == WeaponConfig::ITEMID_Bandages || usedItemId == WeaponConfig::ITEMID_Bandages2 || usedItemId == WeaponConfig::ITEMID_CraftedBandages ||
		usedItemId == WeaponConfig::ITEMID_Painkillers || usedItemId == WeaponConfig::ITEMID_ZombieRepellent || usedItemId == WeaponConfig::ITEMID_C01Vaccine ||
		usedItemId == WeaponConfig::ITEMID_C04Vaccine || usedItemId == WeaponConfig::ITEMID_Medkit;
	if (useOnOtherPlayer && n.var4 != 0)
	{
		GameObject* obj = GameWorld().GetNetworkObject(n.var4);
		if (obj && obj->isObjType(OBJTYPE_Human))
		{
			obj_ServerPlayer* otherPlayer = (obj_ServerPlayer*)obj;
			if (otherPlayer->UseItem_ApplyEffect(n, usedItemId))
			{
#ifdef MISSIONS
				if (m_MissionsProgress)
				{
					GameObject* fromObj = GameWorld().GetNetworkObject(n.FromID);
					m_MissionsProgress->PerformItemAction(Mission::ITEM_Use, usedItemId, (fromObj) ? fromObj->GetHashID() : 0, Mission::ITEMUSEON_OtherPlayer);
				}
#endif
			}
		}
		else
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, false, "otherplayer", "%d", n.var4);
		}
	}
	else
	{
		if (UseItem_ApplyEffect(n, usedItemId))
		{
#ifdef MISSIONS
			if (m_MissionsProgress)
			{
				GameObject* obj = GameWorld().GetNetworkObject(n.FromID);
				m_MissionsProgress->PerformItemAction(Mission::ITEM_Use, usedItemId, (obj) ? obj->GetHashID() : 0, Mission::ITEMUSEON_Self);
			}
#endif
		}
	}

	return;
}

bool obj_ServerPlayer::UseItem_ApplyEffect(const PKT_C2C_PlayerUseItem_s& n, uint32_t itemID)
{
	switch (itemID)
	{
	case WeaponConfig::ITEMID_Medkit:
	case WeaponConfig::ITEMID_Bandages:
	case WeaponConfig::ITEMID_Bandages2:
	case WeaponConfig::ITEMID_CraftedBandages:
	case WeaponConfig::ITEMID_Antibiotics:
	case WeaponConfig::ITEMID_Painkillers:
	{
		const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itemID);
		if (!wc) {
			r3d_assert(false && "bandages must be a weapon");
			return false;
		}

		if (loadout_->MedBleeding > 0 && (itemID == WeaponConfig::ITEMID_Bandages || itemID == WeaponConfig::ITEMID_Bandages2 || itemID == WeaponConfig::ITEMID_CraftedBandages || itemID == WeaponConfig::ITEMID_Medkit))
			loadout_->MedBleeding = 0.0f;
		if (loadout_->MedBloodInfection > 0 && itemID == WeaponConfig::ITEMID_Antibiotics || itemID == WeaponConfig::ITEMID_Medkit)
			loadout_->MedBloodInfection = 0.0f;

		float bandageEffect = wc->m_AmmoDamage;

		if (loadout_->Skills[CUserSkills::SKILL_Survival2])
			bandageEffect *= 1.05f;
		if (loadout_->Skills[CUserSkills::SKILL_Survival7])
			bandageEffect *= 1.10f;
		if (loadout_->Skills[CUserSkills::SKILL_Survival10])
			bandageEffect *= 1.15f;

		loadout_->Health += bandageEffect;
		loadout_->Health = R3D_MIN(loadout_->Health, 100.0f);
		//r3dOutToLog("bandage used, %f\n", bandageEffect);
		return true;
	}
	break;
	case WeaponConfig::ITEMID_C01Vaccine:
	case WeaponConfig::ITEMID_C04Vaccine:
	{
		const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itemID);
		if (!wc) {
			r3d_assert(false && "vaccine must be a weapon");
			return false;
		}

		float vaccineEffect = wc->m_AmmoDamage;
		loadout_->Toxic -= vaccineEffect;
		loadout_->Toxic = R3D_CLAMP(loadout_->Toxic, 0.0f, 100.0f);
		//r3dOutToLog("vaccine used, %f\n", vaccineEffect);
		return true;
	}
	break;
	case WeaponConfig::ITEMID_AerialDelivery:
		break;
	case WeaponConfig::ITEMID_BarbWireBarricade:
	case WeaponConfig::ITEMID_WoodShieldBarricade:
	case WeaponConfig::ITEMID_WoodShieldBarricadeZB:
	case WeaponConfig::ITEMID_RiotShieldBarricade:
	case WeaponConfig::ITEMID_RiotShieldBarricadeZB:
	case WeaponConfig::ITEMID_SandbagBarricade:
	case WeaponConfig::ITEMID_WoodenDoorBlock:
	case WeaponConfig::ITEMID_MetalWallBlock:
	case WeaponConfig::ITEMID_TallBrickWallBlock:
	case WeaponConfig::ITEMID_WoodenWallPiece:
	case WeaponConfig::ITEMID_ShortBrickWallPiece:
	case WeaponConfig::ITEMID_PlaceableLight:
	case WeaponConfig::ITEMID_SmallPowerGenerator:
	case WeaponConfig::ITEMID_BigPowerGenerator:
	case WeaponConfig::ITEMID_SSBarricadeShield:
		return UseItem_Barricade(n.pos, n.var1, itemID);
		break;

	case WeaponConfig::ITEMID_FarmBlock:
	case WeaponConfig::ITEMID_SolarWaterPurifier:
		return UseItem_FarmBlock(n.pos, n.var1, itemID);
		break;

	case WeaponConfig::ITEMID_Lockbox:
	case WeaponConfig::ITEMID_LockboxBig:
	case WeaponConfig::ITEMID_LockboxSmall:
		return UseItem_Lockbox(n.pos, n.var1, itemID);
		break;

	case WeaponConfig::ITEMID_ZombieRepellent:
		//todo
		break;

		//AlexRedd:: car spawner
	case WeaponConfig::ITEMID_VehicleSpawner_Buggy:
	case WeaponConfig::ITEMID_VehicleSpawner_Stryker:
	case WeaponConfig::ITEMID_VehicleSpawner_ZombieKiller:
	case WeaponConfig::ITEMID_VehicleSpawner_ScoutMilitary:
	case WeaponConfig::ITEMID_VehicleSpawner_BuggyGun:
	case WeaponConfig::ITEMID_VehicleSpawner_ScoutSurvivor:
	case WeaponConfig::ITEMID_VehicleSpawner_BoneCrusher:
	case WeaponConfig::ITEMID_VehicleSpawner_ScavengersScout:
	case WeaponConfig::ITEMID_VehicleSpawner_Cubus:
	case WeaponConfig::ITEMID_VehicleSpawner_Hippy:
	case WeaponConfig::ITEMID_VehicleSpawner_Convertable:
		return UseItem_CarSpawner(n.pos, n.var1, itemID, n.TextFrom);
		break;

	default:
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "baditemid",
			"%d",
			itemID
			);
		break;
	}

	return false;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerReload_s& n)
{
	if (n.WeaponSlot != wiCharDataFull::CHAR_LOADOUT_WEAPON1 && n.WeaponSlot != wiCharDataFull::CHAR_LOADOUT_WEAPON2) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "reload",
			"wslot: %d", n.WeaponSlot);
		return;
	}
	if (loadout_->Items[n.WeaponSlot].quantity > 1) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "reload",
			"reload multiple weapons q:%d", loadout_->Items[n.WeaponSlot].quantity);
		return;
	}

	// validate weapon
	ServerWeapon* wpn = m_WeaponArray[n.WeaponSlot];
	if (wpn == NULL) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "reload",
			"wempty: %d", n.WeaponSlot);
		return;
	}
	if (wpn->getClipConfig() == NULL) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "reload",
			"noclip: %d", n.WeaponSlot);
		return;
	}

	loadout_->Items[n.WeaponSlot].MagIsReloading = true;

	RelayPacket(&n, sizeof(n));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerReloaded_s& n)
{
	if (n.WeaponSlot >= loadout_->BackpackSize || n.AmmoSlot >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, false, "reload",
			"slot: %d %d", n.WeaponSlot, n.AmmoSlot);
		return;
	}
	if (n.WeaponSlot != wiCharDataFull::CHAR_LOADOUT_WEAPON1 && n.WeaponSlot != wiCharDataFull::CHAR_LOADOUT_WEAPON2) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "reload",
			"wslot: %d", n.WeaponSlot);
		return;
	}
	if (loadout_->Items[n.WeaponSlot].quantity > 1) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "reload",
			"reload multiple weapons q:%d", loadout_->Items[n.WeaponSlot].quantity);
		return;
	}

	// validate weapon
	ServerWeapon* wpn = m_WeaponArray[n.WeaponSlot];
	if (wpn == NULL) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "reload",
			"wempty: %d", n.WeaponSlot);
		return;
	}
	if (wpn->getClipConfig() == NULL) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "reload",
			"noclip: %d", n.WeaponSlot);
		return;
	}

	// validate ammo slot
	wiInventoryItem& wi = loadout_->Items[n.AmmoSlot];
	if (wi.itemID == 0 || wi.quantity == 0) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "reload",
			"aempty: %d %d", wi.itemID, wi.quantity);
		return;
	}
	if (wpn->getClipConfig()->m_itemID != wi.itemID) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Reload, true, "reload",
			"itemid: %d %d", wi.itemID, wpn->getClipConfig()->m_itemID);
		return;
	}

	// validate if we reloaded correct amount
	int ammoReloaded = wi.Var1 < 0 ? wpn->getClipConfig()->m_Clipsize : wi.Var1;
	if (n.Amount != ammoReloaded) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "reload",
			"amount:%d var1:%d clip:%d", n.Amount, wi.Var1, wpn->getClipConfig()->m_Clipsize);
		return;
	}

	// remove single clip
	wi.quantity--;
	if (wi.quantity <= 0)
		wi.Reset();

	// drop current ammo clip (if have clip speficied and have ammo)
	if (wpn->getPlayerItem().Var1 > 0 && wpn->getPlayerItem().Var2 > 0)
	{
		wiInventoryItem wi;
		wi.itemID = wpn->getPlayerItem().Var2;
		wi.quantity = 1;
		wi.Var1 = wpn->getPlayerItem().Var1;
		wi.ResetClipIfFull();

		// if we changed attachment, do not drop item to the ground, put to backpack
		if (BackpackAddItem(wi) == false)
		{
			obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", GetRandomPosForItemDrop());
			SetupPlayerNetworkItem(obj);
			// vars
			obj->m_Item = wi;
		}
	}
	m_clipAttmChanged = false;

	// reload weapon
	wpn->getPlayerItem().Var1 = ammoReloaded;
	wpn->getPlayerItem().Var2 = wpn->getClipConfig()->m_itemID;

	{
		PKT_C2C_PlayerReloaded_s n2;
		n2.WeaponSlot = (BYTE)n.WeaponSlot;
		n2.AmmoSlot = (BYTE)n.AmmoSlot;
		n2.Amount = (BYTE)ammoReloaded;
		gServerLogic.p2pBroadcastToAll(&n2, sizeof(n2));
	}

	loadout_->Items[n.WeaponSlot].MagIsReloading = false;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_OpenCase_s& n)
{
	// Make sure this packet/item is not emulated #1
	wiInventoryItem& wi = loadout_->Items[n.WeaponSlot];
	if (wi.itemID == 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "open case", "ITEM ID IS 0 - EMULATED!");
		return;
	}

	// Make sure this packet/item is not emulated #2
	if (wi.itemID < WeaponConfig::ITEMID_AssaultCase || wi.itemID > WeaponConfig::ITEMID_MedicCase)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "open case", "IT IS NOT CASE - EMULATED!");
		return;
	}

	if (n.WeaponSlot >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "open case", "wslot: %d", n.WeaponSlot);
		return;
	}

	// AlexRedd:: Case system
	{
		uint32_t CASE_LOOT_BOX_ID = NULL;
		if (wi.itemID == WeaponConfig::ITEMID_AssaultCase) CASE_LOOT_BOX_ID = 301427;//Assault lootbox ID
		if (wi.itemID == WeaponConfig::ITEMID_SniperCase)  CASE_LOOT_BOX_ID = 301428;//Sniper lootbox ID
		if (wi.itemID == WeaponConfig::ITEMID_GearCase)    CASE_LOOT_BOX_ID = 301429;//Gear lootbox ID
		if (wi.itemID == WeaponConfig::ITEMID_MedicCase)   CASE_LOOT_BOX_ID = 301430;//Medic lootbox ID

		LootBoxConfig*	m_LootBoxConfig;
		m_LootBoxConfig = const_cast<LootBoxConfig*>(g_pWeaponArmory->getLootBoxConfig(CASE_LOOT_BOX_ID));

		if (m_LootBoxConfig != NULL)
		{
			if (m_LootBoxConfig->entries.size() != 0)
			{
				wiInventoryItem Item = RollItem(m_LootBoxConfig, 0);
				if (Item.itemID > 0)
				{
					// Medic case quantity items
					if (wi.itemID == WeaponConfig::ITEMID_MedicCase)
					{
						if (Item.itemID == 101261)
							Item.quantity = int(u_GetRandom(2, 6));
						else if (Item.itemID == 101262)
							Item.quantity = int(u_GetRandom(2, 4));
						else if (Item.itemID == 101256)
							Item.quantity = int(u_GetRandom(2, 6));
						else if (Item.itemID == 101300)
							Item.quantity = int(u_GetRandom(2, 6));
						else if (Item.itemID == 101301)
							Item.quantity = int(u_GetRandom(2, 6));
						else if (Item.itemID == 101302)
							Item.quantity = int(u_GetRandom(2, 4));
						else if (Item.itemID == 101304)
							Item.quantity = int(u_GetRandom(2, 4));
						else
							Item.quantity = 1;
					}
					else
						Item.quantity = 1;

					// show hud message
					PKT_C2C_CaseMessageHUD_s msg;
					msg.caseItemId = wi.itemID;
					msg.winItemId = DWORD(Item.itemID);
					msg.quantity = Item.quantity;
					gServerLogic.p2pSendToPeer(peerId_, this, &msg, sizeof(msg));

					// add chat message for all players (only for assault and sniper cases)
					if (wi.itemID == WeaponConfig::ITEMID_AssaultCase || wi.itemID == WeaponConfig::ITEMID_SniperCase)
					{
						PKT_C2C_ChatMessageBox_s box;
						r3dscpy(box.gamertag, loadout_->Gamertag);
						box.itemId = DWORD(Item.itemID);
						gServerLogic.p2pBroadcastToAll(&box, sizeof(box), true);
					}

					// Remove case from player inventory
					AdjustBackpackSlotQuantity(n.WeaponSlot, -1);

					//drop item
					if (BackpackAddItem(Item) == false)
					{
						// create network object
						obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", GetRandomPosForItemDrop());
						SetupPlayerNetworkItem(obj);
						// vars
						obj->m_Item = Item;
					}
				}
				r3dOutToLog("!!! [BOX]: %s opened a box, he received a %d. Quantity: %d\n", loadout_->Gamertag, Item.itemID, Item.quantity);
			}
			else
			{
				// show hud message
				PKT_C2C_CaseMessageHUD_s msg;
				msg.caseItemId = wi.itemID;
				msg.winItemId = 0;
				msg.quantity = 0;
				gServerLogic.p2pSendToPeer(peerId_, this, &msg, sizeof(msg));
			}
		}
	}
	return;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_PlayerUnloadClip_s& n)
{
	if (n.WeaponSlot >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "unload",
			"wslot: %d", n.WeaponSlot);
		return;
	}

	wiInventoryItem& bckItm = loadout_->Items[n.WeaponSlot];
	const WeaponConfig* wcfg = g_pWeaponArmory->getWeaponConfig(bckItm.itemID);
	if (wcfg == NULL || wcfg->category == storecat_MELEE || wcfg->category == storecat_GRENADE || wcfg->category == storecat_UsableItem)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "unload",
			"itemid: %d", bckItm.itemID);
		return;
	}
	if (loadout_->Items[n.WeaponSlot].quantity > 1) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "unload",
			"unload multiple weapons q:%d", loadout_->Items[n.WeaponSlot].quantity);
		return;
	}

	// minor hack, initialize Var1/Var2 for unitialized weapon
	// check if we need to modify starting ammo. (SERVER CODE SYNC POINT)
	if (bckItm.Var1 < 0)
	{
		const WeaponAttachmentConfig* clipCfg = g_pWeaponArmory->getAttachmentConfig(wcfg->FPSDefaultID[WPN_ATTM_CLIP]);
		if (!clipCfg)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "unload",
				"no clip for: %d", bckItm.itemID);
			return;
		}

		bckItm.Var1 = clipCfg->m_Clipsize;
		bckItm.Var2 = clipCfg->m_itemID;
	}

	// place to backpack or drop current ammo clip (if have clip speficied and have ammo)
	if (bckItm.Var1 > 0 && bckItm.Var2 > 0)
	{
		wiInventoryItem clipItm;
		clipItm.itemID = bckItm.Var2;
		clipItm.quantity = 1;
		clipItm.Var1 = bckItm.Var1;
		clipItm.ResetClipIfFull();

		if (!BackpackAddItem(clipItm))
		{
			obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", GetRandomPosForItemDrop());
			SetupPlayerNetworkItem(obj);
			// vars
			obj->m_Item = clipItm;
#ifdef MISSIONS
			if (m_MissionsProgress)
			{
				m_MissionsProgress->PerformItemAction(Mission::ITEM_Drop, obj->m_Item.itemID, obj->GetHashID());
			}
#endif
		}

		// empty clip
		bckItm.Var1 = 0;
	}
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_PlayerCombineClip_s& n)
{
	if (n.SlotFrom >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "combine",
			"wid: %d", n.SlotFrom);
		return;
	}

	const WeaponAttachmentConfig* clipCfg = g_pWeaponArmory->getAttachmentConfig(loadout_->Items[n.SlotFrom].itemID);
	if (clipCfg == NULL || clipCfg->m_type != WPN_ATTM_CLIP)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "combine",
			"noclip: %d", loadout_->Items[n.SlotFrom].itemID);
		return;
	}

	// scan backpack and collect all clips
	int freeSlot = -1;
	int totalBullets = 0;
	std::vector<int> clipSlots;
	for (int i = 0; i < loadout_->BackpackSize; i++)
	{
		const wiInventoryItem& wi = loadout_->Items[i];
		if (!storecat_CanPlaceItemToSlot(clipCfg, i))	// fancy way to skip weapon & armor slots
			continue;

		if (wi.itemID == clipCfg->m_itemID)
		{
			clipSlots.push_back(i);
			totalBullets += wi.quantity * (wi.Var1 < 0 ? clipCfg->m_Clipsize : wi.Var1);
		}

		if (wi.itemID == 0 && freeSlot == -1)
			freeSlot = i;
	}
	r3d_assert(clipSlots.size());

	// some sanity checks
	if (totalBullets == 0 || clipCfg->m_Clipsize == 0)
		return;

	int numFullClips = totalBullets / clipCfg->m_Clipsize;
	int bulletsLeft = totalBullets % clipCfg->m_Clipsize;
	int fullClipVar1 = -1;
	// process case if there is not enough bullets to form full clip 
	if (numFullClips == 0)
	{
		numFullClips = 1;
		fullClipVar1 = bulletsLeft;
		bulletsLeft = 0;
	}

	// we need at least one free slot in case we need to split single stack to 2
	if (bulletsLeft != 0 && (clipSlots.size() == 1 && freeSlot == -1))
	{
		PKT_S2C_BackpackModify_s n;
		n.SlotTo = 0xFF;

		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return;
	}

	// combine - full clips to first found clip
	{
		int slot = clipSlots[0];
		wiInventoryItem& fullClip = loadout_->Items[slot];
		fullClip.Var1 = fullClipVar1;
		fullClip.quantity = numFullClips;

		PKT_S2C_BackpackReplace_s n2;
		n2.SlotTo = slot;
		n2.Item = fullClip;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	}

	// clear all other clips (in first we keep full clips, in second - left bullets)
	for (size_t i = 1; i < clipSlots.size(); i++)
	{
		if (i == 1 && bulletsLeft != 0)	// keep second clip if we have bullets left
			continue;

		int slot = clipSlots[i];
		loadout_->Items[slot].Reset();

		PKT_S2C_BackpackReplace_s n2;
		n2.SlotTo = clipSlots[i];
		n2.Item = loadout_->Items[slot];
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	}

	if (bulletsLeft > 0)
	{
		// create partial item clip
		wiInventoryItem partClip;
		partClip.itemID = clipCfg->m_itemID;
		partClip.quantity = 1;
		partClip.Var1 = bulletsLeft;
		partClip.InventoryID = tempInventoryID++;

		if (clipSlots.size() == 1)
		{
			// we have only one clip. so we must have free item slot
			r3d_assert(freeSlot != -1);
			AddItemToBackpackSlot(freeSlot, partClip);
		}
		else
		{
			// put remaining bullets to second found clip
			int slot = clipSlots[1];
			loadout_->Items[slot] = partClip;

			PKT_S2C_BackpackReplace_s n2;
			n2.SlotTo = slot;
			n2.Item = loadout_->Items[slot];
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		}
	}

	// unlock player backpack, op is complete.
	PKT_S2C_BackpackUnlock_s n3;
	gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_LearnRecipe_s& n)
{
	if (n.slotFrom >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "learn recipe",
			"wid: %d", n.slotFrom);
		return;
	}

	const CraftRecipeConfig* recipeCfg = g_pWeaponArmory->getCraftRecipeConfig(loadout_->Items[n.slotFrom].itemID);
	if (recipeCfg == NULL)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "learn recipe",
			"no recipe: %d", loadout_->Items[n.slotFrom].itemID);
		return;
	}

	if (loadout_->hasRecipe(recipeCfg->m_itemID))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, false, "learn recipe",
			"already learned recipe: %d", loadout_->Items[n.slotFrom].itemID);
		return;
	}

	loadout_->RecipesLearned[loadout_->NumRecipes++] = recipeCfg->m_itemID;
	g_AsyncApiMgr->AddJob(new CJobUpdateCharData(this));

	AdjustBackpackSlotQuantity(n.slotFrom, -1);

	// unlock player backpack, op is complete.
	PKT_S2C_BackpackUnlock_s n3;
	gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
}
//new add skin
void obj_ServerPlayer::OnNetPacket(const PKT_C2S_LearnGearSkinRecipe_s& n)
{
	if (n.slotFrom >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "learn recipe",
			"wid: %d", n.slotFrom);
		return;
	}

	const GearSkinRecipeConfig* recipeCfg = g_pWeaponArmory->getGearSkinRecipeConfig(loadout_->Items[n.slotFrom].itemID);
	if (recipeCfg == NULL)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "learn recipe",
			"no recipe: %d", loadout_->Items[n.slotFrom].itemID);
		return;
	}

	if (loadout_->hasGearSkinRecipe(recipeCfg->m_itemID))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, false, "learn recipe",
			"already learned recipe: %d", loadout_->Items[n.slotFrom].itemID);
		return;
	}

	loadout_->GearSkinRecipesLearned[loadout_->NumGearSkinRecipes++] = recipeCfg->m_itemID;
	CheckSkinGear(recipeCfg->DefaultItemID, recipeCfg->SkinID);
	g_AsyncApiMgr->AddJob(new CJobUpdateCharData(this));
	gServerLogic.ApiPlayerUpdateWeaponSkins(this);
	AdjustBackpackSlotQuantity(n.slotFrom, -1);

	// unlock player backpack, op is complete.
	PKT_S2C_BackpackUnlock_s n3;
	gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
}
//new add skin
void obj_ServerPlayer::OnNetPacket(const PKT_C2S_CraftItem_s& n)
{
	if (!loadout_->hasRecipe(n.recipeID))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "craft item",
			"has no recipe: %d", n.recipeID);
		return;
	}

	const CraftRecipeConfig* recipeCfg = g_pWeaponArmory->getCraftRecipeConfig(n.recipeID);
	if (recipeCfg == NULL)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "craft item",
			"no such recipe (?!?!): %d", n.recipeID);
		return;
	}
	if (recipeCfg->craftedItemID == 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "craft item",
			"recipe %d have no item", n.recipeID);
		return;
	}

	// check for free space
	wiInventoryItem craftItm;
	craftItm.itemID = recipeCfg->craftedItemID;
	craftItm.quantity = 1;
	int SlotTo = GetBackpackSlotForItem(craftItm);
	if (SlotTo == -1)
	{
		PKT_S2C_CraftAns_s n;
		n.ans = 0;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return;
	}

	// check for ingridients
	for (uint32_t i = 0; i < recipeCfg->numComponents; i++)
	{
		if (recipeCfg->components[i].itemID == 0)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "craft item",
				"recipe %d have no component", n.recipeID);
			return;
		}

		bool notEnough = false;
		if (recipeCfg->components[i].itemID == 301386)
			notEnough = profile_.ProfileData.ResMetal < int(recipeCfg->components[i].quantity);
		else if (recipeCfg->components[i].itemID == 301387)
			notEnough = profile_.ProfileData.ResStone < int(recipeCfg->components[i].quantity);
		else if (recipeCfg->components[i].itemID == 301388)
			notEnough = profile_.ProfileData.ResWood < int(recipeCfg->components[i].quantity);
		else if (!IsHaveBackpackItem(recipeCfg->components[i].itemID, recipeCfg->components[i].quantity))
			notEnough = true;

		if (notEnough)
		{
			PKT_S2C_CraftAns_s n;
			n.ans = 0;
			gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
			return;
		}
	}

	// remove
	PKT_S2C_AddResource_s resN;
	resN.ResMetal = resN.ResStone = resN.ResWood = resN.GameDollar = 0;
	for (uint32_t i = 0; i < recipeCfg->numComponents; i++)
	{
		if (recipeCfg->components[i].itemID == 301386)
		{
			profile_.ProfileData.ResMetal -= recipeCfg->components[i].quantity;
			resN.ResMetal -= recipeCfg->components[i].quantity;
		}
		else if (recipeCfg->components[i].itemID == 301387)
		{
			profile_.ProfileData.ResStone -= recipeCfg->components[i].quantity;
			resN.ResStone -= recipeCfg->components[i].quantity;
		}
		else if (recipeCfg->components[i].itemID == 301388)
		{
			profile_.ProfileData.ResWood -= recipeCfg->components[i].quantity;
			resN.ResWood -= recipeCfg->components[i].quantity;
		}
		else
			IsHaveBackpackItem(recipeCfg->components[i].itemID, recipeCfg->components[i].quantity, true);
	}

	// craft
	AddItemToBackpackSlot(SlotTo, craftItm);

	{
		PKT_S2C_CraftAns_s n;
		n.ans = 1;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
	}

	if (resN.ResMetal || resN.ResStone || resN.ResWood)
		gServerLogic.p2pSendToPeer(peerId_, this, &resN, sizeof(resN));

#ifdef MISSIONS
	m_MissionsProgress->PerformItemAction(Mission::ITEM_Craft, craftItm.itemID, 0);
#endif
}
//new add skin
void obj_ServerPlayer::OnNetPacket(const PKT_C2S_GearSkinItem_s& n)
{
	
	if (!loadout_->hasGearSkinRecipe(n.recipeID) && n.SkinID != 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "craft item",
			"has no recipe: %d", n.recipeID);
		return;
	}

	const GearSkinRecipeConfig* recipeCfg = g_pWeaponArmory->getGearSkinRecipeConfig(n.recipeID);
	if (recipeCfg == NULL && n.SkinID != 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "craft item",
			"no such recipe (?!?!): %d", n.recipeID);
		return;
	}
	if (recipeCfg && recipeCfg->craftedItemID == 0 && n.SkinID != 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "craft item",
			"recipe %d have no item", n.recipeID);
		return;
	}

	if (n.SkinID != 0)
		CheckSkinGear(recipeCfg->DefaultItemID, recipeCfg->SkinID);
	else
		CheckSkinGear(n.recipeID, n.SkinID);

	return;
}
//new add skin
void obj_ServerPlayer::OnNetPacket(PKT_C2C_PlayerFired_s& n)
{
	if (!FireWeapon(n.debug_wid, n.fireSeqNo, n.localId))
	{
		return;
	}

	if (!r3d_vector_isFinite(n.fire_from) || !r3d_vector_isFinite(n.fire_to) || !r3d_float_isFinite(n.holding_delay))
	{
		return;
	}

	m_SpawnProtectedUntil = 0;

	if (n.fireSeqNo == 0)
	{
		gServerLogic.InformZombiesAboutSound(this, m_WeaponArray[n.debug_wid]);
	}

	if (m_WeaponArray[n.debug_wid]->m_pConfig &&
		WeaponConfig::ITEMID_FlareGun == m_WeaponArray[n.debug_wid]->m_pConfig->m_itemID &&
		!(profile_.ProfileData.isDevAccount & wiUserProfile::DAA_INVISIBLE))
	{
		// Create the flare
		obj_ServerGrenade* srvGrenade = (obj_ServerGrenade*)srv_CreateGameObject("obj_ServerGrenade", "grenade", n.fire_from);
		srvGrenade->m_TrackedID = n.localId;
		SetupPlayerNetworkItem(srvGrenade);
		srvGrenade->ownerID = GetSafeID();
		srvGrenade->m_ItemID = m_WeaponArray[n.debug_wid]->m_pConfig->m_itemID;
		srvGrenade->m_AddedDelay = n.holding_delay;
		srvGrenade->m_CreationPos = n.fire_from;
		srvGrenade->m_LastCollisionNormal = r3dPoint3D(0, 1, 0);
		srvGrenade->m_FireDirection = (n.fire_to - n.fire_from).NormalizeTo();
		//Let the ObjMan call srvGrenade->OnCreate();

		// Update the packet with the network ID, so it can
		// be connected later with the local network mover
		n.spawnID = toP2pNetId(srvGrenade->GetNetworkID());
	}

	RelayPacket(&n, sizeof(n));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerHitStatic_s& n)
{
	ServerWeapon* wpn = OnBulletHit(n.localId, "PKT_C2C_PlayerHitStatic_s");
	if (!wpn)
		return;

	if (!r3d_vector_isFinite(n.hit_pos) || !r3d_vector_isFinite(n.hit_norm))
	{
		return;
	}

	RelayPacket(&n, sizeof(n), false);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerHitStaticPierced_s& n)
{
	// just relay packet. not a real hit, just identification that we pierced some static geometry, will be followed up by real HIT packet
	RelayPacket(&n, sizeof(n), false);
}


void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerHitNothing_s& n)
{
	ServerWeapon* wpn = OnBulletHit(n.localId, "PKT_C2C_PlayerHitNothing_s", false);
	if (!wpn)
		return;

	//RelayPacket(&n, sizeof(n), false);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerHitDynamic_s& n)
{
	ServerWeapon* wpn = OnBulletHit(n.localId, "PKT_C2C_PlayerHitDynamic_s");
	if (!wpn)
		return;

	// make sure we're shooting to another player
	GameObject* targetObj = GameWorld().GetNetworkObject(n.targetId);
	if (!targetObj)
	{
		gServerLogic.LogInfo(peerId_, "HitBody0", "not valid targetId");
		return;
	}

	if (!r3d_vector_isFinite(n.muzzler_pos) || !r3d_vector_isFinite(n.hit_pos))
	{
		return;
	}


	//r3dOutToLog("hit from %s to %s\n", fromObj->Name.c_str(), targetObj->Name.c_str()); CLOG_INDENT;

	// do not damage anyone when firing from safezone
	if (loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox)
	{
		return;
	}

	if (!gServerLogic.CanDamageThisObject(targetObj))
	{
		gServerLogic.LogInfo(peerId_, "HitBody1", "hit object that is not damageable!");
		return;
	}

	// admin that is in god mode cannot hit anyone else, to make sure that there will be no abuse
	if (m_isAdmin_GodMode)
	{
		gServerLogic.LogInfo(peerId_, "AdminAbuse", "admin player %s tried to shoot another player while in god mode", userName);
		return;
	}

	// validate hit_pos is close to the targetObj, if not, that it is a hack
	if (n.damageFromPiercing == 0) // 0 - bullet didn't pierce anything
	{
		const float dist = (n.hit_pos - targetObj->GetPosition()).Length();
		const float allow = GPP_Data.AI_SPRINT_SPEED*2.0f;
		if (dist > allow) // if more than Xsec of sprint
		{
			// ptumik: disabled cheat report, as we might receive packet for a player that is dead for client, but respawned on server -> distance difference
			//gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_ShootDistance, false, "HitBodyBigDistance",
			//	"hit %s, dist %f vs %f", 
			//	targetObj->Name.c_str(), dist, allow
			//	);
			return;
		}
	}

	// validate melee range
	if (wpn->getCategory() == storecat_MELEE)
	{
		float dist = (GetPosition() - targetObj->GetPosition()).Length();
		if (dist > 3.0f)
		{
			gServerLogic.LogInfo(peerId_, "HitBody0", "knife cheat %f", dist);
			return;
		}
	}

	// validate muzzle position
	{
		if ((GetPosition() - n.muzzler_pos).Length() > 5.0f)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NoGeomtryFiring, true, "muzzle pos cheat");
			return;
		}
	}

	bool checkRayCast = true;
	// note: skip it for barricades, raycastSingle return them as contact point so we can't damage them
	if (targetObj->isObjType(OBJTYPE_Barricade))
		checkRayCast = false;

	// validate ray cast (should work for ballistic bullets too)
	// we shouldn't hit any static geometry, if we did, than probably user is cheating.
	if (checkRayCast)
	{
		PxRaycastHit hit;
		PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_STATIC_MASK, 0, 0, 0), PxSceneQueryFilterFlag::eSTATIC);
		r3dVector pos = n.muzzler_pos;
		r3dVector dir = n.hit_pos - n.muzzler_pos;
		float dirl = dir.Length();
		dir.Normalize();
		if (g_pPhysicsWorld->raycastSingle(PxVec3(pos.x, pos.y, pos.z), PxVec3(dir.x, dir.y, dir.z), dirl, PxSceneQueryFlag::eIMPACT, hit, filter))
		{
			/*
			// so let's discard this packet for now
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NoGeomtryFiring, false,
			"raycast failed", "player pos: %.2f, %.2f, %.2f, muzzler: %.2f, %.2f, %.2f, distance: %.2f, hitpos: %.2f, %.2f, %.2f",
			GetPosition().x, GetPosition().y ,GetPosition().z, pos.x, pos.y, pos.z, dirl, n.hit_pos.x, n.hit_pos.y, n.hit_pos.z);
			*/
			r3dOutToLog("!!! raycast failed (1) (%s): player pos: %.2f, %.2f, %.2f, muzzler: %.2f, %.2f, %.2f, distance: %.2f, hitpos: %.2f, %.2f, %.2f\n",
				profile_.ProfileData.ArmorySlots[0].Gamertag, GetPosition().x, GetPosition().y, GetPosition().z, pos.x, pos.y, pos.z, dirl, n.hit_pos.x, n.hit_pos.y, n.hit_pos.z);
			return;
		}

		// and now validate raycast in opposite direction to check for ppl shooting from inside buildings and what not
		pos = n.hit_pos;
		dir = n.muzzler_pos - n.hit_pos;
		dirl = dir.Length();
		dir.Normalize();
		if (g_pPhysicsWorld->raycastSingle(PxVec3(pos.x, pos.y, pos.z), PxVec3(dir.x, dir.y, dir.z), dirl, PxSceneQueryFlag::eIMPACT, hit, filter))
		{
			/*
			// so let's discard this packet for now
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_NoGeomtryFiring, false,
			"reverse raycast failed", "player pos: %.2f, %.2f, %.2f, muzzler: %.2f, %.2f, %.2f, distance: %.2f, hitpos: %.2f, %.2f, %.2f",
			GetPosition().x, GetPosition().y ,GetPosition().z, pos.x, pos.y, pos.z, dirl, n.hit_pos.x, n.hit_pos.y, n.hit_pos.z);
			*/
			r3dOutToLog("!!! raycast failed (2) (%s): player pos: %.2f, %.2f, %.2f, muzzler: %.2f, %.2f, %.2f, distance: %.2f, hitpos: %.2f, %.2f, %.2f\n",
				profile_.ProfileData.ArmorySlots[0].Gamertag, GetPosition().x, GetPosition().y, GetPosition().z, pos.x, pos.y, pos.z, dirl, n.hit_pos.x, n.hit_pos.y, n.hit_pos.z);
			return;
		}

	}

	RelayPacket(&n, sizeof(n));

	// calc damaged based on weapon, decay damage based from distance from player to target
	float dist = (GetPosition() - targetObj->GetPosition()).Length();
	float damage = wpn->calcDamage(dist);
	if (n.damageFromPiercing > 0)
	{
		float dmod = float(n.damageFromPiercing) / 100.0f;
		damage *= dmod;
	}

	// track ShotsHits
	loadout_->Stats.ShotsHits++;

	if (obj_ServerPlayer* targetPlr = IsServerPlayer(targetObj))
	{
		if (gServerLogic.ApplyDamageToPlayer(this, targetPlr, GetPosition() + r3dPoint3D(0, 1, 0), damage, n.hit_body_bone, n.hit_body_part, false, wpn->getCategory(), wpn->getConfig()->m_itemID))
		{
			//HACK: track Kill here, because we can't pass weapon ItemID to ApplyDamageToPlayer yet
			int isKill = targetPlr->loadout_->Alive == 0 ? 1 : 0;
			gServerLogic.TrackWeaponUsage(wpn->getConfig()->m_itemID, 0, 1, isKill);
		}
	}
	else if (targetObj->isObjType(OBJTYPE_Zombie))
	{
		obj_Zombie* z = (obj_Zombie*)targetObj;
		if (z->ZombieState != EZombieStates::ZState_Dead) // do not apply damage to already dead zombie
		{
			gServerLogic.ApplyDamageToZombie(this, targetObj, GetPosition() + r3dPoint3D(0, 1, 0), damage, n.hit_body_bone, n.hit_body_part, false, wpn->getCategory(), wpn->getConfig()->m_itemID);
		}
	}
#ifdef VEHICLES_ENABLED
	else if (targetObj->isObjType(OBJTYPE_Vehicle))
	{
		obj_Vehicle* vehicle = (obj_Vehicle*)targetObj;
		if (vehicle->GetDurability() > 0)
			gServerLogic.ApplyDamageToVehicle(this, vehicle, GetPosition() + r3dPoint3D(0, 1, 0), damage, false, wpn->getCategory(), wpn->getConfig()->m_itemID);
	}
#endif
	else
	{
		gServerLogic.TrackWeaponUsage(wpn->getConfig()->m_itemID, 0, 1, 0);
		gServerLogic.ApplyDamage(this, targetObj, this->GetPosition() + r3dPoint3D(0, 1, 0), damage, false, wpn->getCategory(), wpn->getConfig()->m_itemID);
	}
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerHitResource_s& n)
{
	// n.ResType is from MaterialType::hasResourcesToCollect

	// add resources to player profile
	profile_.ProfileData.ResWood += (n.ResType == 1) ? 2 : 0;
	profile_.ProfileData.ResStone += (n.ResType == 2) ? 2 : 0;
	profile_.ProfileData.ResMetal += (n.ResType == 3) ? 1 : 0;

	PKT_S2C_AddResource_s n2;
	n2.ResWood = (n.ResType == 1) ? 2 : 0;
	n2.ResStone = (n.ResType == 2) ? 2 : 0;
	n2.ResMetal = (n.ResType == 3) ? 1 : 0;
	n2.GameDollar = 0;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerSwearing_s& n)//AlexRedd:: chat auto ban
{
#ifdef AUTO_BAN_CHAT
	if(profile_.ProfileData.isDevAccount == 0)//skip for dev 
	{
		// add count to player profile
		profile_.ProfileData.SwearingCount += 1;
		gServerLogic.ApiPlayerUpdateChar(this);

		PKT_C2C_AddPlayerSwearsMsg_s n2;
		n2.Count  = profile_.ProfileData.SwearingCount;	
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

		obj_ServerPlayer* fromPlr = gServerLogic.GetPeer(peerId_).player;

		if (profile_.ProfileData.SwearingCount == 5 ||
			profile_.ProfileData.SwearingCount == 10 ||
			profile_.ProfileData.SwearingCount == 15 ||
			profile_.ProfileData.SwearingCount == 20)
		{
			CJobBanChatUser* job = new CJobBanChatUser(fromPlr);
			char tmpStr[256];
			sprintf(tmpStr, "Auto banned chat in-game reason: %d swears", profile_.ProfileData.SwearingCount);
			gServerLogic.LogInfo(peerId_, "Auto banned chat");
			r3dscpy(job->BanReason, tmpStr);
			g_AsyncApiMgr->AddJob(job);
#ifdef VEHICLES_ENABLED
			if (fromPlr->IsInVehicle())
				fromPlr->ExitVehicle(true, true, true);
#endif
			// info player that he was banned from chat
			PKT_S2C_CustomKickMsg_s n3;
			sprintf(tmpStr, "You are banned from chat\nReason: %d swears", profile_.ProfileData.SwearingCount);
			r3dscpy(n3.msg, tmpStr);
			gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, 0, "Auto banned chat in-game", "User: '%s' (CustomerID: %d) reason: %d swears", fromPlr->userName, fromPlr->profile_.CustomerID, profile_.ProfileData.SwearingCount);

		}		
	}
#endif//AUTO_BAN_CHAT
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2C_PlayerReadyGrenade_s& n)
{
	if (n.wid < 0 || n.wid >= NUM_WEAPONS_ON_PLAYER)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "ReadyGrenade",
			"wid %d", n.wid);
		return;
	}
	const WeaponConfig* wcfg = g_pWeaponArmory->getWeaponConfig(loadout_->Items[n.wid].itemID);
	if (wcfg == NULL || wcfg->category != storecat_GRENADE)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "ReadyGrenade",
			"not grenade");
		return;
	}

	m_SpawnProtectedUntil = 0;

	RelayPacket(&n, sizeof(n));
}

void obj_ServerPlayer::OnNetPacket(PKT_C2C_PlayerThrewGrenade_s& n)
{
	r3d_assert(loadout_->Alive);

	if (profile_.ProfileData.isDevAccount & wiUserProfile::DAA_INVISIBLE)
		return;

	if (!r3d_vector_isFinite(n.fire_from) || !r3d_vector_isFinite(n.fire_to) || !r3d_float_isFinite(n.holding_delay))
	{
		return;
	}

	lastPlayerAction_ = r3dGetTime();

	m_SpawnProtectedUntil = 0;

	if (n.wid < 0 || n.wid >= NUM_WEAPONS_ON_PLAYER)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "ThrewGrenade",
			"wid %d", n.wid);
		return;
	}

	// check if this is really a grenade
	wiInventoryItem& wi = loadout_->Items[n.wid];
	const WeaponConfig* wpnCfg = (WeaponConfig*)g_pWeaponArmory->getWeaponConfig(wi.itemID);
	if (!wpnCfg || storecat_GRENADE != wpnCfg->category)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "ThrewGrenade",
			"itemid: %d", wi.itemID);
		return;
	}
	if (wi.quantity <= 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_UseItem, true, "ThrewGrenade",
			"q: %d", wi.quantity);
		return;
	}

	obj_ServerGrenade* srvGrenade = (obj_ServerGrenade*)srv_CreateGameObject("obj_ServerGrenade", "grenade", n.fire_from);
	srvGrenade->m_TrackedID = n.localId;
	SetupPlayerNetworkItem(srvGrenade);
	srvGrenade->ownerID = GetSafeID();
	srvGrenade->m_ItemID = wi.itemID;
	srvGrenade->m_AddedDelay = n.holding_delay;
	srvGrenade->m_CreationPos = n.fire_from;
	srvGrenade->m_LastCollisionNormal = r3dPoint3D(0, 1, 0);
	srvGrenade->m_FireDirection = (n.fire_to - n.fire_from).NormalizeTo();
	//Let the ObjMan call srvGrenade->OnCreate();

	gServerLogic.TrackWeaponUsage(wi.itemID, 1, 0, 0);

	// Update the packet with the network ID, so it can
	// be connected later with the local network mover
	n.spawnID = toP2pNetId(srvGrenade->GetNetworkID());
	RelayPacket(&n, sizeof(n));

#ifdef MISSIONS
	// Weapons are used from the backpack, and no HashID exists for these items,
	// so we can only count the number of uses.
	m_MissionsProgress->PerformItemAction(Mission::ITEM_Use, wi.itemID, 0, Mission::ITEMUSEON_NotSpecified);
#endif

	// remove used item
	wi.quantity--;
	if (wi.quantity <= 0) {
		wi.Reset();
		OnBackpackChanged(n.wid);
	}
}


void obj_ServerPlayer::OnNetPacket(const PKT_C2S_PlayerChangeBackpack_s& n)
{
	if (n.SlotFrom >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"chbp slot: %d", n.SlotFrom);
		return;
	}

	const BackpackConfig* cfg = g_pWeaponArmory->getBackpackConfig(loadout_->Items[n.SlotFrom].itemID);
	if (cfg == NULL) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"chbp item: %d", loadout_->Items[n.SlotFrom].itemID);
		return;
	}
	if (cfg->m_maxSlots != n.BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"chbp slots: %d %d vs %d", loadout_->Items[n.SlotFrom].itemID, cfg->m_maxSlots, n.BackpackSize);
		return;
	}
	// backpack can't be stacked in inventory, fix issue with duping previous backpack as ItemID is replaced
	if (loadout_->Items[n.SlotFrom].quantity > 1) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"chbp quantity: %d", loadout_->Items[n.SlotFrom].quantity);
		return;
	}

	// do not allow to change backpack if new backpack has less slots then current one (to prevent hack when player can drop 300+ chemlights and cause other clients to freeze
	{
		int numItemsCurrently = 0;
		for (int a = 0; a < loadout_->BackpackSize; a++)
		{
			if (loadout_->Items[a].itemID != 0)
				numItemsCurrently++;
		}

		if (numItemsCurrently > cfg->m_maxSlots)
		{
			return;
		}
	}

	gServerLogic.LogInfo(peerId_, "PKT_C2S_PlayerChangeBackpack_s", "%d->%d", loadout_->BackpackSize, cfg->m_maxSlots); CLOG_INDENT;

	// check for same backpack
	//if(loadout_->BackpackID == loadout_->Items[n.SlotFrom].itemID) {//AlexRedd:: disabled now
	//return;
	//}

	// replace backpack in used slot with current one (SERVER CODE SYNC POINT)
	loadout_->Items[n.SlotFrom].itemID = loadout_->BackpackID;

	// remove items that won't fit into backpack and build list of dropped items
	std::vector<wiInventoryItem> droppedItems;
	if (cfg->m_maxSlots < loadout_->BackpackSize)
	{
		for (int i = cfg->m_maxSlots; i < loadout_->BackpackSize; i++)
		{
			wiInventoryItem& wi = loadout_->Items[i];
			if (wi.itemID > 0)
			{
				droppedItems.push_back(wi);

				// remove from remote inventory
				PKT_S2C_BackpackModify_s n;
				n.SlotTo = i;
				n.Quantity = 0;
				n.dbg_ItemID = wi.itemID;
				gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));

				// remove from local inventory
				wi.Reset();
			}
		}
	}

	// update backpack, safe to do here as those params will be updated in api job and if it fails, player will be disconnected
	loadout_->BackpackSize = cfg->m_maxSlots;
	loadout_->BackpackID = cfg->m_itemID;

	// force player inventory update, so items will be deleted
	gServerLogic.ApiPlayerUpdateChar(this);

	// create api job for backpack change
	CJobChangeBackpack* job = new CJobChangeBackpack(this);
	job->BackpackID = cfg->m_itemID;
	job->BackpackSize = cfg->m_maxSlots;
	job->DroppedItems = droppedItems;
	g_AsyncApiMgr->AddJob(job);

	OnLoadoutChanged();

	//AlexRedd:: Send to client, we have sucessfully changed the backpack!
	PKT_S2C_BackpackUnlock_s n3;
	gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
}

void obj_ServerPlayer::RepairItemWithKit(int slot, bool isPremium)
{
	wiInventoryItem& itm = loadout_->Items[slot];
	if (itm.Var3 < 0 || itm.Var3 == wiInventoryItem::MAX_DURABILITY)
		return;

	if (itm.quantity > 1 && !storecat_IsItemStackable(itm.itemID))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "repair",
			"nonstackable item:%d, quantity: %d", itm.itemID, itm.quantity);
		return;
	}

	// get repair percentage
	float repairAmount = -1;
	{
		const GearConfig* gc = g_pWeaponArmory->getGearConfig(itm.itemID);
		if (gc)
			repairAmount = isPremium ? gc->m_PremRepairAmount : gc->m_RepairAmount;

		const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itm.itemID);
		if (wc)
			repairAmount = isPremium ? wc->m_PremRepairAmount : wc->m_RepairAmount;

		const WeaponAttachmentConfig* wac = g_pWeaponArmory->getAttachmentConfig(itm.itemID);
		if (wac)
			repairAmount = isPremium ? wac->m_PremRepairAmount : wac->m_RepairAmount;
	}
	if (repairAmount <= 0.01f)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "repair",
			"item:%d, rep:%f", itm.itemID, repairAmount);
		return;
	}

	// find and use repair kit
	bool found = false;
	for (int i = 0; i < loadout_->BackpackSize; i++)
	{
		if (loadout_->Items[i].itemID == (isPremium ? WeaponConfig::ITEMID_PremRepairKit : WeaponConfig::ITEMID_RepairKit))
		{
			found = true;
			AdjustBackpackSlotQuantity(i, -1);
			break;
		}
	}
	if (!found)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "repair",
			"no kit %d", isPremium);
		return;
	}

	// repair item
	itm.Var3 += (int)(repairAmount * 100);
	itm.Var3 = R3D_MIN((int)wiInventoryItem::MAX_DURABILITY, itm.Var3);

	PKT_S2C_RepairItemAns_s n;
	n.SlotFrom = (BYTE)slot;
	n.Var3 = itm.Var3;
	n.GameDollars = profile_.ProfileData.GameDollars;
	gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
}

void obj_ServerPlayer::RepairItemWithGD(int slot, bool repairAllRequest)
{
	wiInventoryItem& itm = loadout_->Items[slot];
	if (itm.Var3 < 0 || itm.Var3 == wiInventoryItem::MAX_DURABILITY)
		return;

	if (itm.quantity > 1 && !storecat_IsItemStackable(itm.itemID))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "repair",
			"nonstackable item:%d, quantity: %d", itm.itemID, itm.quantity);
		return;
	}

	// get repair price
	int repairPriceGD = 0;
	{
		const GearConfig* gc = g_pWeaponArmory->getGearConfig(itm.itemID);
		if (gc)
			repairPriceGD = gc->m_RepairPriceGD;

		const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itm.itemID);
		if (wc)
			repairPriceGD = wc->m_RepairPriceGD;

		const WeaponAttachmentConfig* wac = g_pWeaponArmory->getAttachmentConfig(itm.itemID);
		if (wac)
			repairPriceGD = wac->m_RepairPriceGD;
	}
	if (repairPriceGD == 0)
	{
		if (!repairAllRequest) // do not kick if player is trying to repair all items in backpack, as some items do not have repair price
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "repair",
			"item:%d, no price", itm.itemID);
		return;
	}

	// calc repair price, repairPriceGD is repair amount from 0 to 100%
	float pricef = float(repairPriceGD) * (1.0f - (itm.Var3 / 100.0f / 100.0f));
	if (profile_.ProfileData.GameDollars < (int)pricef)
	{
		PKT_S2C_RepairItemAns_s n;
		n.SlotFrom = 0xFF;
		n.Var3 = itm.Var3;
		n.GameDollars = profile_.ProfileData.GameDollars;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return;
	}

	// repair item
	itm.Var3 = wiInventoryItem::MAX_DURABILITY;
	profile_.ProfileData.GameDollars -= (int)pricef;

	PKT_S2C_RepairItemAns_s n;
	n.SlotFrom = (BYTE)slot;
	n.Var3 = itm.Var3;
	n.GameDollars = profile_.ProfileData.GameDollars;
	gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
}

int ItemPriceGD(const wiInventoryItem &wi1)
{
	int tmpPrice = 0;
	const wiStoreItem* foundItem = NULL;
	for (uint32_t i = 0; i < g_NumStoreItems; i++)
	{
		const wiStoreItem& itm = g_StoreItems[i];
		int quantity = storecat_GetItemBuyStackSize(itm.itemID);
		if (itm.itemID != wi1.itemID)
			continue;
		if (quantity > 1)// ignore items if shop stack size > 1
			break;

		foundItem = &itm;
		tmpPrice = foundItem->gd_pricePerm;
		break;
	}

	return tmpPrice;
}

void obj_ServerPlayer::SellItemWithGD(int slot, bool repairAllRequest)
{
	wiInventoryItem& itm = loadout_->Items[slot];
	r3d_assert(itm.itemID);

	if (itm.Var3 / 100 > 80)
	{
		// get sell price
		int itemPrice = 0;
		{
			const GearConfig* gc = g_pWeaponArmory->getGearConfig(itm.itemID);
			const WeaponConfig* wc = g_pWeaponArmory->getWeaponConfig(itm.itemID);
			const BackpackConfig* bpc = g_pWeaponArmory->getBackpackConfig(itm.itemID);
			const WeaponAttachmentConfig* wac = g_pWeaponArmory->getAttachmentConfig(itm.itemID);
			const BaseItemConfig* bic = g_pWeaponArmory->getConfig(itm.itemID);

			if ((gc || wc || bpc || wac && wac->m_type != WPN_ATTM_CLIP) && (bic && bic->category != storecat_UsableItem))
			{
				profile_.ProfileData.PremiumAcc > 0 ? itemPrice = (int(ItemPriceGD(itm) *itm.quantity *0.2)) : itemPrice = (int(ItemPriceGD(itm) *itm.quantity *0.1));
			}
		}

		bool vipItem = GetVipItem(slot);//AlexRedd:: no drop items

		if (itemPrice > 0 && !vipItem)
		{
			// add resources to player profile
			profile_.ProfileData.ResWood += 0;
			profile_.ProfileData.ResStone += 0;
			profile_.ProfileData.ResMetal += 0;
			profile_.ProfileData.GameDollars += itemPrice;

			PKT_S2C_AddResource_s n2;
			n2.ResWood = 0;
			n2.ResStone = 0;
			n2.ResMetal = 0;
			n2.GameDollar = (DWORD)itemPrice;

			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

			// remove item
			if (itm.quantity > 1)
				AdjustBackpackSlotQuantity(slot, -itm.quantity);
			else
				AdjustBackpackSlotQuantity(slot, -1);

			// unlock player backpack, op is complete.
			PKT_S2C_BackpackUnlock_s n3;
			gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
		}
	}
	return;
}
//new add skin
void obj_ServerPlayer::OnNetPacket(const PKT_S2C_UpdateWpnSkinSelected_s& n)
{
	SetLasSknUsed(n.itemID, n.Selected);

	PKT_S2C_UpdateWpnSkinSelected_s n2;
	n2.targetId = n.targetId;
	n2.itemID = n.itemID;
	n2.Selected = n.Selected;
	n2.Set = n.Set;
	gServerLogic.p2pBroadcastToAll(&n2, sizeof(n2), true);

	gServerLogic.ApiPlayerUpdateWeaponSkins(this);
}
//new add skin
void obj_ServerPlayer::OnNetPacket(const PKT_C2S_RepairItemReq_s& n)
{
	if (n.SlotFrom >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"repair slot: %d", n.SlotFrom);
		return;
	}

	switch (n.RepairMode)
	{
	case 0: // cash single
		RepairItemWithGD(n.SlotFrom, false);
		break;
	case 1: // cash multiple
		for (int i = 0; i < loadout_->BackpackSize; i++)
		{
			if (loadout_->Items[i].itemID > 0)
				RepairItemWithGD(i, true);
		}
		break;

	case 2: // repair kit
		RepairItemWithKit(n.SlotFrom, false);
		break;
	case 3: // premium repair
		RepairItemWithKit(n.SlotFrom, true);
		break;
	case 4: // sell multiple
		for (int i = 0; i < loadout_->BackpackSize; i++)
		{
			if (loadout_->Items[i].itemID > 0)
				SellItemWithGD(i, true);
		}
		break;
	case 5: // sell stack			
		SellItemWithGD(n.SlotFrom, true);
		break;
	}

	// unlock player backpack, op is complete.
	PKT_S2C_BackpackUnlock_s n3;
	gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));

	return;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_BackpackDrop_s& n)
{
	if (isInVehicle)//AlexRedd:: not allowed in vehicle
		return;

	if (!r3d_vector_isFinite(n.pos))
	{
		return;
	}

	if (n.SlotFrom >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"slot: %d", n.SlotFrom);
		return;
	}

	float dropLength = (GetPosition() - n.pos).Length();
	if (dropLength > 20.0f)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"dlen: %f", dropLength);
		return;
	}

	wiInventoryItem& wi = loadout_->Items[n.SlotFrom];
	if (wi.itemID == 0 || wi.quantity < 1)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, false, "backpack",//TODO: fix me! players use it for fast disconnect (stack clips)
			"id: %d %d %d", n.SlotFrom, wi.itemID, wi.quantity);
		return;
	}

	// create network object
	obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", n.pos);
	SetupPlayerNetworkItem(obj);
	// vars
	obj->m_Item = wi;
	obj->m_Item.quantity = 1;

	// modify backpack (need after item creation)
	wi.quantity--;
	if (wi.quantity <= 0)
		wi.Reset();

	OnBackpackChanged(n.SlotFrom);
#ifdef MISSIONS
	if (m_MissionsProgress)
	{
		m_MissionsProgress->PerformItemAction(Mission::ITEM_Drop, obj->m_Item.itemID, obj->GetHashID());
	}
#endif
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_BackpackDropAll_s& n)
{
	if (isInVehicle)//AlexRedd:: not allowed in vehicle
		return;

	if (!r3d_vector_isFinite(n.pos))
		return;

	if (n.SlotFrom >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"slot: %d", n.SlotFrom);
		return;
	}

	float dropLength = (GetPosition() - n.pos).Length();
	if (dropLength > 20.0f)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"dlen: %f", dropLength);
		return;
	}

	wiInventoryItem& wi = loadout_->Items[n.SlotFrom];
	if (wi.itemID == 0 || wi.quantity < 1)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"id: %d %d %d", n.SlotFrom, wi.itemID, wi.quantity);
		return;
	}

	// create network object
	obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", n.pos);
	SetupPlayerNetworkItem(obj);
	// vars
	obj->m_Item = wi;
	obj->m_Item.quantity = wi.quantity;

	// modify backpack (need after item creation)
	for (; wi.quantity > 0;)
		wi.quantity--;

	if (wi.quantity <= 0)
		wi.Reset();

	OnBackpackChanged(n.SlotFrom);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_BackpackSwap_s& n)
{
	if (n.SlotFrom >= loadout_->BackpackSize || n.SlotTo >= loadout_->BackpackSize)	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"move: %d->%d %d", n.SlotFrom, n.SlotTo, loadout_->BackpackSize);
		return;
	}

	// check if we can place item to slot by type
	const BaseItemConfig* itmFrom = g_pWeaponArmory->getConfig(loadout_->Items[n.SlotFrom].itemID);
	const BaseItemConfig* itmTo = g_pWeaponArmory->getConfig(loadout_->Items[n.SlotTo].itemID);

	// if slot is used to reload
	const WeaponAttachmentConfig* wac = g_pWeaponArmory->getAttachmentConfig(loadout_->Items[n.SlotFrom].itemID);
	if (wac && wac->m_type == WPN_ATTM_CLIP && loadout_->Items[n.SlotFrom].MagIsReloading)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"bad swap: %d->%d | Magazine is used to reload! #1", itmFrom->m_itemID, n.SlotTo);
		return;
	}

	wac = g_pWeaponArmory->getAttachmentConfig(loadout_->Items[n.SlotTo].itemID);
	if (wac && wac->m_type == WPN_ATTM_CLIP && loadout_->Items[n.SlotTo].MagIsReloading)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"bad swap: %d->%d | Magazine is used to reload! #2", itmFrom->m_itemID, n.SlotTo);
		return;
	}

	// check weight
	if (n.SlotFrom == 0 || n.SlotFrom == 1 || n.SlotFrom == 6 || n.SlotFrom == 7 || n.SlotTo == 0 || n.SlotTo == 1 || n.SlotTo == 6 || n.SlotTo == 7)
	{
		if (itmFrom)
		{
			float totalWeight = loadout_->getTotalWeight();
			if (n.SlotTo != 0 && n.SlotTo != 1 && n.SlotTo != 6 && n.SlotTo != 7) // skip weapon & armor
			{
				totalWeight += itmFrom->m_Weight * loadout_->Items[n.SlotFrom].quantity;

				if (loadout_->Skills[CUserSkills::SKILL_Physical3])
					totalWeight *= 0.95f;
				if (loadout_->Skills[CUserSkills::SKILL_Physical7])
					totalWeight *= 0.9f;
			}

			const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
			r3d_assert(bc);
			if (totalWeight > bc->m_maxWeight)
			{
				PKT_S2C_BackpackModify_s n;
				n.SlotTo = 0xFE;
				gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
				return;
			}
		}
		if (itmTo)
		{
			float totalWeight = loadout_->getTotalWeight();
			if (n.SlotFrom != 0 && n.SlotFrom != 1 && n.SlotFrom != 6 && n.SlotFrom != 7) // skip weapon & armor
			{
				totalWeight += itmTo->m_Weight * loadout_->Items[n.SlotTo].quantity;

				if (loadout_->Skills[CUserSkills::SKILL_Physical3])
					totalWeight *= 0.95f;
				if (loadout_->Skills[CUserSkills::SKILL_Physical7])
					totalWeight *= 0.9f;
			}

			const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
			r3d_assert(bc);
			if (totalWeight > bc->m_maxWeight)
			{
				PKT_S2C_BackpackModify_s n;
				n.SlotTo = 0xFE;
				gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
				return;
			}
		}
	}

	if (itmFrom && !storecat_CanPlaceItemToSlot(itmFrom, n.SlotTo))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"bad swap: %d->%d", itmFrom->m_itemID, n.SlotTo);
		return;
	}
	if (itmTo && !storecat_CanPlaceItemToSlot(itmTo, n.SlotFrom))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"bad swap: %d->%d", itmTo->m_itemID, n.SlotFrom);
		return;
	}
	// prevent hackers to equip illegally stacked weapons into weapon slot and then loading ammo there
	if (loadout_->Items[n.SlotFrom].quantity > 1 && (n.SlotTo == wiCharDataFull::CHAR_LOADOUT_WEAPON1 || n.SlotTo == wiCharDataFull::CHAR_LOADOUT_WEAPON2))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"bad swap: weaponslot q:%d", loadout_->Items[n.SlotFrom].quantity);
		return;
	}

	R3D_SWAP(loadout_->Items[n.SlotFrom], loadout_->Items[n.SlotTo]);

	OnBackpackChanged(n.SlotFrom);
	OnBackpackChanged(n.SlotTo);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_BackpackJoin_s& n)
{
	if (n.SlotFrom >= loadout_->BackpackSize || n.SlotTo >= loadout_->BackpackSize)	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"join: %d->%d %d", n.SlotFrom, n.SlotTo, loadout_->BackpackSize);
		return;
	}

	wiInventoryItem& wi1 = loadout_->Items[n.SlotFrom];
	wiInventoryItem& wi2 = loadout_->Items[n.SlotTo];
	if (wi1.itemID == 0 || wi1.itemID != wi2.itemID) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"join: itm %d %d", wi1.itemID, wi2.itemID);
		return;
	}
	if (wi1.Var1 != wi2.Var1 || wi1.Var2 != wi2.Var2 || wi1.Var3 != wi2.Var3) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"join: var %d/%d %d/%d %d/%d", wi1.Var1, wi2.Var1, wi1.Var2, wi2.Var2, wi1.Var3, wi2.Var3);
		return;
	}
	if (!storecat_IsItemStackable(wi1.itemID)) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"join: nostack %d", wi1.itemID);
		return;
	}

	wi2.quantity += wi1.quantity;
	wi1.Reset();

	OnBackpackChanged(n.SlotFrom);
	OnBackpackChanged(n.SlotTo);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_BackpackDisassembleItem_s& n)
{
	if (n.SlotFrom >= loadout_->BackpackSize) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"disassemble: %d %d", n.SlotFrom, loadout_->BackpackSize);
		return;
	}

	wiInventoryItem& wi1 = loadout_->Items[n.SlotFrom];
	const BaseItemConfig* cfg = g_pWeaponArmory->getConfig(wi1.itemID);
	if (cfg == NULL) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "BackpackDisassemble",
			"itemid: %d", wi1.itemID);
		return;
	}

	if (cfg->m_ResWood == 0 && cfg->m_ResStone == 0 && cfg->m_ResMetal == 0) {
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "BackpackDisassemble",
			"no res: %d", wi1.itemID);
		return;
	}

	// add resources to player profile
	profile_.ProfileData.ResWood += cfg->m_ResWood;
	profile_.ProfileData.ResStone += cfg->m_ResStone;
	profile_.ProfileData.ResMetal += cfg->m_ResMetal;

	PKT_S2C_AddResource_s n2;
	n2.ResWood = (BYTE)cfg->m_ResWood;
	n2.ResStone = (BYTE)cfg->m_ResStone;
	n2.ResMetal = (BYTE)cfg->m_ResMetal;
	n2.GameDollar = 0;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

	// remove item
	AdjustBackpackSlotQuantity(n.SlotFrom, -1);

	// unlock player backpack, op is complete.
	PKT_S2C_BackpackUnlock_s n3;
	gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_ShopBuyReq_s& n)
{
	if (n.BuyIdx != 4 && n.BuyIdx != 8)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "ShopOp",
			"BuyIdx: %d", n.BuyIdx);
		return;
	}

#ifdef ENABLE_INGAME_STORE
	// must be in safe zone
	if ((loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox) == 0 && profile_.ProfileData.isDevAccount == 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "ShopOp",
			"not in safe zone");
		return;
	}

	// must be close to Store NPC
	if (!gServerNPCMngr.isCloseToNPC(GetPosition(), "obj_StoreNPC") && profile_.ProfileData.isDevAccount == 0)
	{
		// do not kick for this cheat... instead we will ban wave :)
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "ShopOp", "not close to NPC - F10 CHECK!");

		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoPrice;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}
#endif

#ifdef DISABLE_STORE_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoPrice;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}
#endif

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode	
	if (isGameBattleRoyale)
		return;
#endif //ENABLE_BATTLE_ROYALE	

	// validate item and price
	const wiStoreItem* foundItem = NULL;
	for (uint32_t i = 0; i < g_NumStoreItems; i++)
	{
		const wiStoreItem& itm = g_StoreItems[i];
		if (itm.itemID != n.ItemID)
			continue;
		if (n.BuyIdx == 4 && itm.pricePerm == 0)
			continue;
		if (n.BuyIdx == 8 && itm.gd_pricePerm == 0)
			continue;

		foundItem = &itm;
		break;
}

	if (!foundItem)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoPrice;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	/*if(gServerLogic.ginfo_.mapId==GBGameInfo::MAPID_WZ_Colorado && (foundItem->itemID == WeaponConfig::ITEMID_NoDrop_Weapon || foundItem->itemID == WeaponConfig::ITEMID_NoDrop_Backpack))
	{
	PKT_S2C_InventoryOpAns_s n2;
	n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Forbidden;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	return;
	}*/

	// check if we have enough money
	if ((n.BuyIdx == 4 && profile_.ProfileData.GamePoints < (int)foundItem->pricePerm) ||
		(n.BuyIdx == 8 && profile_.ProfileData.GameDollars < (int)foundItem->gd_pricePerm))
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoMoney;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// check if we have that item in db
	const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(n.ItemID);
	if (!itemCfg)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoItem;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// check that we have free global inventory slot
	if (profile_.haveFreeInventorySlot(false) == false)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoInventorySpace;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// create temp item and check if we have space in backpack for it
	wiInventoryItem wi1;
	wi1.itemID = n.ItemID;
	int slot = GetBackpackSlotForItem(wi1);
	if (slot == -1)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoBackpackSpace;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// check weight
	float totalWeight = loadout_->getTotalWeight();
	if (slot != 0 && slot != 1 && slot != 6 && slot != 7) // skip weapon & armor
	{
		totalWeight += itemCfg->m_Weight * storecat_GetItemBuyStackSize(n.ItemID);

		if (loadout_->Skills[CUserSkills::SKILL_Physical3])
			totalWeight *= 0.95f;
		if (loadout_->Skills[CUserSkills::SKILL_Physical7])
			totalWeight *= 0.9f;
	}

	const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
	if (totalWeight > bc->m_maxWeight)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_WeightLimit;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	StartInventoryOp(); // start working with global inventory

	// buy item
	CJobBuyItem* job = new CJobBuyItem(this);
	job->ItemID = n.ItemID;
	job->BuyIdx = n.BuyIdx;
	job->SlotTo = slot;
	g_AsyncApiMgr->AddJob(job);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_GroupInvitePlayer_s& n)
{
	if (!IsNullTerminated(n.gamertag, sizeof(n.gamertag))) {
		gServerLogic.DisconnectPeer(peerId_, true, "invalid PKT_C2S_GroupInvitePlayer");
		return;
	}

	if (groupID != 0 && !isGroupLeader) // only group leader can invite
	{
		PKT_S2C_GroupAns_s n2;
		n2.result = PKT_S2C_GroupAns_s::GSE_OnlyLeaderCanInvite;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// find our player
	// todo: maybe should replace gamertag with playerIdx?
	obj_ServerPlayer* plr = NULL;
	{
		for (int i = 0; i < gServerLogic.curPlayers_; ++i)
		{
			obj_ServerPlayer* pl = gServerLogic.plrList_[i];
			if (stricmp(pl->userName, n.gamertag) == 0)
			{
				plr = pl;
				break;
			}
		}
	}

	if (!plr)
	{
		PKT_S2C_GroupAns_s n2;
		n2.result = PKT_S2C_GroupAns_s::GSE_NoSuchPlayer;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}
	// check if player is already in a group
	if (plr->groupID != 0)
	{
		PKT_S2C_GroupAns_s n2;
		n2.result = PKT_S2C_GroupAns_s::GSE_PlayerAlreadyInGroup;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// save invite
	GroupInviteStruct ginv;
	ginv.fromID = GetNetworkID();
	ginv.timeOfExpire = r3dGetTime() + 60 * 10;
	plr->groupInvitesFrom.push_back(ginv);

	{
		PKT_S2C_GroupAns_s n2;
		n2.result = PKT_S2C_GroupAns_s::GSE_SuccessSentInvite;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	}
	{
		PKT_S2C_GroupInviteReceived_s n2;
		r3dscpy(n2.gamertag, userName);
		gServerLogic.p2pSendToPeer(plr->peerId_, plr, &n2, sizeof(n2));
	}
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_GroupAcceptInvite_s& n)
{
	if (!IsNullTerminated(n.gamertag, sizeof(n.gamertag))) {
		gServerLogic.DisconnectPeer(peerId_, true, "invalid PKT_C2S_GroupAcceptInvite");
		return;
	}

	if (groupID != 0)
	{
		PKT_S2C_GroupAns_s n2;
		n2.result = PKT_S2C_GroupAns_s::GSE_PlayerAlreadyInGroup;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// find our player
	// todo: maybe should replace gamertag with playerIdx?
	obj_ServerPlayer* plr = NULL;
	{
		for (int i = 0; i < gServerLogic.curPlayers_; ++i)
		{
			obj_ServerPlayer* pl = gServerLogic.plrList_[i];
			if (stricmp(pl->userName, n.gamertag) == 0)
			{
				plr = pl;
				break;
			}
		}
	}

	if (!plr)
	{
		PKT_S2C_GroupAns_s n2;
		n2.result = PKT_S2C_GroupAns_s::GSE_NoSuchPlayer;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// look for invite from plr
	std::vector<GroupInviteStruct>::iterator it;
	for (it = groupInvitesFrom.begin(); it != groupInvitesFrom.end(); ++it)
	{
		if (it->fromID == plr->GetNetworkID())
		{
			if (r3dGetTime() > it->timeOfExpire)
			{
				groupInvitesFrom.erase(it);
				PKT_S2C_GroupAns_s n2;
				n2.result = PKT_S2C_GroupAns_s::GSE_InviteExpired;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				return;
			}
			groupInvitesFrom.erase(it);

			// ok, form or join group
			if (plr->groupID != 0)
			{
				// check if player inviting is a leader of the group
				if (!plr->isGroupLeader)
				{
					r3d_assert(false); // this shouldn't happen
				}

				// check if group has enough slots
				uint32_t curNumOfPlrsInGroup = gServerLogic.getNumPlayersInGroup(plr->groupID);
				if (curNumOfPlrsInGroup >= 10)
				{
					PKT_S2C_GroupAns_s n2;
					n2.result = PKT_S2C_GroupAns_s::GSE_GroupIsFull;
					gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
					return;
				}

				gServerLogic.joinPlayerToGroup(this, plr->groupID);
			}
			else
			{
				gServerLogic.createNewPlayerGroup(plr, this, gServerLogic.getNextGroupID());
			}

			return;
		}
	}

	PKT_S2C_GroupAns_s n2;
	n2.result = PKT_S2C_GroupAns_s::GSE_CannotFindInviteToAccept;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	return;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_GroupLeaveGroup_s& n)
{
	if (groupID == 0) // cheat? or bug?
		return;
	if (m_LeaveGroupAtTime > 0)
		return;

	m_LeaveGroupAtTime = r3dGetTime() + 30.0f;

	// notify everyone in the group that player is about to leave the group
	PKT_S2C_GroupNotify_s n2;
	n2.status = 0;
	r3dscpy(n2.gamertag, userName);
	for (int i = 0; i < gServerLogic.curPlayers_; ++i)
	{
		obj_ServerPlayer* pl = gServerLogic.plrList_[i];
		if (pl->groupID == groupID)
		{
			gServerLogic.p2pSendToPeer(pl->peerId_, pl, &n2, sizeof(n2));
		}
	}

	//	gServerLogic.leavePlayerFromGroup(this);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_GroupKickPlayer_s& n)
{
	if (groupID == 0) // cheat? or bug?
		return;
	if (!isGroupLeader)
		return;

	// find our player
	// todo: maybe should replace gamertag with playerIdx?
	obj_ServerPlayer* plr = NULL;
	{
		for (int i = 0; i < gServerLogic.curPlayers_; ++i)
		{
			obj_ServerPlayer* pl = gServerLogic.plrList_[i];
			if (stricmp(pl->userName, n.gamertag) == 0)
			{
				plr = pl;
				break;
			}
		}
	}

	if (!plr)
	{
		PKT_S2C_GroupAns_s n2;
		n2.result = PKT_S2C_GroupAns_s::GSE_NoSuchPlayer;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	if (plr->groupID != groupID)
	{
		// cheat?
		return;
	}

	if (plr->m_LeaveGroupAtTime > 0)
		return;

	plr->m_LeaveGroupAtTime = r3dGetTime() + 30.0f;

	// notify everyone in the group that player is about to be kicked
	PKT_S2C_GroupNotify_s n2;
	n2.status = 1;
	r3dscpy(n2.gamertag, plr->userName);
	for (int i = 0; i < gServerLogic.curPlayers_; ++i)
	{
		obj_ServerPlayer* pl = gServerLogic.plrList_[i];
		if (pl->groupID == plr->groupID)
		{
			gServerLogic.p2pSendToPeer(pl->peerId_, pl, &n2, sizeof(n2));
		}
	}
	//gServerLogic.leavePlayerFromGroup(plr);
}

void obj_ServerPlayer::OnBuyItemCallback(const CJobBuyItem* job)
{
	inventoryOpActive_ = false;
	if (job->out_OpAns != PKT_S2C_InventoryOpAns_s::ANS_Success)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = (BYTE)job->out_OpAns;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// update balance
	if (job->BuyIdx >= 5 && job->BuyIdx <= 8)
		profile_.ProfileData.GameDollars = job->out_Balance;
	else
		profile_.ProfileData.GamePoints = job->out_Balance;

	// important- sync gamedollars so reducted money won't be updated
	savedGameDollars_ = profile_.ProfileData.GameDollars;

	// check if we bought consumable
	int quantityToAdd = storecat_GetItemBuyStackSize(job->ItemID);

	// place it into inventory first
	wiInventoryItem itm;
	itm.itemID = job->ItemID;
	itm.quantity = quantityToAdd;
	AddItemToInventory(job->out_InventoryID, itm);
	r3d_assert(profile_.getInventorySlot(job->out_InventoryID) != NULL);

	// step 2: now we have to move item to player
	inventoryOpActive_ = true; // continue working with global inventory 

	r3d_assert(loadout_->Items[job->SlotTo].itemID == 0 || loadout_->Items[job->SlotTo].itemID == job->ItemID);
	CJobBackpackFromInventory* job2 = new CJobBackpackFromInventory(this);
	job2->InventoryID = job->out_InventoryID;
	job2->SlotTo = job->SlotTo;
	job2->Amount = quantityToAdd;
	g_AsyncApiMgr->AddJob(job2);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_FromInventoryReq_s& n)
{
	// must be in safe zone
	if ((loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox) == 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
			"not in safe zone");
		return;
	}
	// must be close to Vault NPC
	if (!gServerNPCMngr.isCloseToNPC(GetPosition(), "obj_VaultNPC"))
	{
		// do not kick for this cheat... instead we will ban wave :)
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "ShopOp",
			"not close to NPC");
	}

	if (n.SlotTo != 0xFF && n.SlotTo >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
			"slotto: %d %d", n.SlotTo, loadout_->BackpackSize);
		return;
	}

	const wiInventoryItem* invItm = profile_.getInventorySlot(n.InventoryID);
	if (invItm == NULL)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
			"n.InventoryID: %I64d", n.InventoryID);
		return;
	}

	// debug checks for desync
	if (n.SlotTo != 0xFF && (loadout_->Items[n.SlotTo].itemID != n.dbg_ItemID || loadout_->Items[n.SlotTo].quantity != n.dbg_Quantity))
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
			"d1: %d-%d %d-%d", loadout_->Items[n.SlotTo].itemID, n.dbg_ItemID, loadout_->Items[n.SlotTo].quantity, n.dbg_Quantity);

		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	if (invItm->itemID == WeaponConfig::ITEMID_NoDrop_Backpack)//AlexRedd:: no drop backpack
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Forbidden;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	if (invItm->itemID == 0 || invItm->itemID != n.dbg_InvItemID || invItm->quantity != n.dbg_InvQuantity)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
			"d2: %d-%d %d-%d", invItm->itemID, n.dbg_InvItemID, invItm->quantity, n.dbg_InvQuantity);

		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// check for amount cheat
	if (n.Amount > invItm->quantity || n.Amount <= 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
			"amount: %d %d", invItm->quantity, n.Amount);
		return;
	}

	int SlotTo = (signed int)n.SlotTo;
	if (n.SlotTo == 0xFF)
	{
		SlotTo = GetBackpackSlotForItem(*invItm);
		if (SlotTo == -1)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoBackpackSpace;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}
	}
	else
	{
		// check if we can place that item to this slot
		wiInventoryItem& bckItm = loadout_->Items[SlotTo];
		const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
		r3d_assert(itemCfg);

		bool canPlace =
			itemCfg &&
			storecat_CanPlaceItemToSlot(itemCfg, SlotTo) &&
			(bckItm.itemID == 0 || bckItm.CanStackWith(*invItm));

		if (!canPlace)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_BadTarget;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}
	}
	r3d_assert(SlotTo >= 0 && SlotTo < loadout_->BackpackSize);


	// check weight
	const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
	r3d_assert(itemCfg);
	float totalWeight = loadout_->getTotalWeight();
	if (SlotTo != 0 && SlotTo != 1 && SlotTo != 6 && SlotTo != 7) // skip weapon & armor
	{
		totalWeight += itemCfg->m_Weight * n.Amount;

		if (loadout_->Skills[CUserSkills::SKILL_Physical3])
			totalWeight *= 0.95f;
		if (loadout_->Skills[CUserSkills::SKILL_Physical7])
			totalWeight *= 0.9f;
	}

	const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
	r3d_assert(bc);
	if (totalWeight > bc->m_maxWeight)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_WeightLimit;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	StartInventoryOp(); // start working with global inventory

	CJobBackpackFromInventory* job = new CJobBackpackFromInventory(this);
	job->InventoryID = n.InventoryID;
	job->SlotTo = SlotTo;
	job->Amount = n.Amount;
	g_AsyncApiMgr->AddJob(job);
}

void obj_ServerPlayer::OnFromInventoryCallback(const CJobBackpackFromInventory* job)
{
	inventoryOpActive_ = false;
	if (job->out_OpAns != PKT_S2C_InventoryOpAns_s::ANS_Success)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = (BYTE)job->out_OpAns;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// move success
	wiInventoryItem* invItm = profile_.getInventorySlot(job->InventoryID);
	r3d_assert(invItm);
	r3d_assert(invItm->quantity >= job->Amount);

	// adjust backpack, add item ignoring Var1 - that was already checked in PKT_C2S_FromInventoryReq_s
	{
		wiInventoryItem itm = *invItm;
		itm.quantity = job->Amount;
		itm.InventoryID = job->out_InventoryID;
		AddItemToBackpackSlot(job->SlotTo, itm);
	}

	// adjust inventory
	{
		PKT_S2C_InventoryModify_s n2;
		n2.InventoryID = invItm->InventoryID;
		n2.Quantity = invItm->quantity - job->Amount;
		n2.dbg_ItemID = invItm->itemID;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

		invItm->quantity -= job->Amount;
		if (invItm->quantity == 0)
			invItm->Reset();
	}

	// SYNC both versions of profiles
	savedLoadout_ = *loadout_;

	// report to client that async op is complete
	PKT_S2C_InventoryOpAns_s n2;
	n2.OpAns = (BYTE)job->out_OpAns;
	n2.GamePoints = profile_.ProfileData.GamePoints;
	n2.GameDollars = profile_.ProfileData.GameDollars;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_ToInventoryReq_s& n)
{
	bool disableOp = false;
#ifdef DISABLE_GI_ACCESS_ON_PTE_MAP
	if (gServerLogic.ginfo_.channel == 6) // public test environment server, do not save any info about player, as those maps might have game breaking loot\changes, so let's not propagate them to the rest of the game
		disableOp = true;
#endif
#ifdef DISABLE_GI_ACCESS_ON_PTE_STRONGHOLD_MAP
	if (gServerLogic.ginfo_.channel == 6 && gServerLogic.ginfo_.mapId == GBGameInfo::MAPID_WZ_Cliffside) // public test environment server, do not save any info about player, as those maps might have game breaking loot\changes, so let's not propagate them to the rest of the game
		disableOp = true;
#endif
#ifdef DISABLE_GI_ACCESS_FOR_CALI_SERVER
	if(gServerLogic.ginfo_.mapId==GBGameInfo::MAPID_WZ_California)
		disableOp=true;
#endif
#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())
		disableOp = true;
#endif

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode	
	if (isGameBattleRoyale)
		disableOp = true;
#endif //ENABLE_BATTLE_ROYALE

	if (disableOp)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoInventorySpace;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// must be in safe zone
	/*if((loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox) == 0)
	{
	gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "ToInv",
	"not in safe zone");
	return;
	}
	// must be close to Vault NPC
	if(!gServerNPCMngr.isCloseToNPC(GetPosition(), "obj_VaultNPC"))
	{
	// do not kick for this cheat... instead we will ban wave :)
	gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "ShopOp",
	"not close to NPC");
	}*/

	if (n.SlotFrom >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "ToInv",
			"slotfrom: %d %d", n.SlotFrom, loadout_->BackpackSize);
		return;
	}

	const wiInventoryItem& bckItm = loadout_->Items[n.SlotFrom];

	// debug checks for desync
	if (bckItm.itemID == 0 || bckItm.itemID != n.dbg_ItemID || bckItm.quantity != n.dbg_Quantity)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "ToInv",
			"d1: %d-%d %d-%d", bckItm.itemID, n.dbg_ItemID, bckItm.quantity, n.dbg_Quantity);

		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// check for amount cheat
	if (n.Amount > bckItm.quantity || n.Amount <= 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "ToInv",
			"amount: %d %d", bckItm.quantity, n.Amount);
		return;
	}

	// find target InventoryID in inventory
	__int64 InvInventoryID = 0;
	for (uint32_t i = 0; i < profile_.ProfileData.NumItems; i++)
	{
		// can stack only non-modified items
		const wiInventoryItem& wi2 = profile_.ProfileData.Inventory[i];
		if (bckItm.CanStackWith(wi2, true)) // we allow to stack 'non stackable' items in GI
		{
			InvInventoryID = wi2.InventoryID;
			break;
		}
	}

	// check that we have free global inventory slot
	if (InvInventoryID == 0 && profile_.haveFreeInventorySlot() == false)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoInventorySpace;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	StartInventoryOp(); // start working with global inventory

	CJobBackpackToInventory* job = new CJobBackpackToInventory(this);
	job->InventoryID = InvInventoryID;
	job->SlotFrom = n.SlotFrom;
	job->Amount = n.Amount;
	g_AsyncApiMgr->AddJob(job);
	}

void obj_ServerPlayer::loadout_Change_switch(const PKT_C2S_ChangeLoadoutReq_s& n, int function, int slotID)
{
	switch (function)
	{
	case 1:
	{
		bool move_set = false;
		int to_move_quantity = 1;
		if (loadout_->Items[slotID].itemID == n.loadout_.item[slotID].ItemID)
		{
			to_move_quantity = loadout_->Items[slotID].quantity;
			if (to_move_quantity <= 0)
				to_move_quantity = 1;

			to_move_quantity -= n.loadout_.item[slotID].Quantity;
			if (to_move_quantity <= 0)
				break;

			move_set = true;
		}

		const wiInventoryItem& bckItm = loadout_->Items[slotID];
		if (bckItm.itemID <= 0)
			break;

		// find target InventoryID in inventory
		__int64 InvInventoryID = 0;

		for (uint32_t i = 0; i < profile_.ProfileData.NumItems; i++)
		{
			// can stack only non-modified items
			const wiInventoryItem& wi2 = profile_.ProfileData.Inventory[i];
			if (bckItm.CanStackWith(wi2, true)) // we allow to stack 'non stackable' items in GI
			{
				InvInventoryID = wi2.InventoryID;
				break;
			}
		}

		// check that we have free global inventory slot
		if (InvInventoryID == 0 && profile_.haveFreeInventorySlot() == false)
			break;

		StartInventoryOp(); // start working with global inventory

		char strInventoryID[128];
		sprintf(strInventoryID, "%I64d", InvInventoryID);

		CWOBackendReq req("api_CharBackpack.aspx");
		req.AddSessionInfo(profile_.CustomerID, profile_.SessionID);
		req.AddParam("skey1", "KfZnUUwZFJqrPJ1b4DaNZ3CkGnvz6MzPYAvP86wg");
		req.AddParam("CharID", loadout_->LoadoutID);
		req.AddParam("op", 52);		// inventory operation code - ApiBackpackToInventory
		req.AddParam("v1", strInventoryID);	// value 1
		req.AddParam("v2", slotID);	// value 2
		req.AddParam("v3", (move_set ? to_move_quantity : bckItm.quantity));		// value 3
		if (!req.Issue())
		{
			r3dOutToLog("!!!! Loadout Change Thread failed - backpack to inventory, code: %d\n", req.resultCode_);
			break;
		}

		__int64 out_InventoryID = 0;
		int nargs = sscanf(req.bodyStr_, "%I64d", &out_InventoryID);
		if (nargs != 1 || out_InventoryID == 0)
		{
			r3dOutToLog("!!!! Loadout Change Thread failed - backpack to inventory, bad ans: %s\n", req.bodyStr_);
			break;
		}

		inventoryOpActive_ = false;

		// move success
		wiInventoryItem& bcpItm = loadout_->Items[slotID];
		r3d_assert(bcpItm.itemID);
		r3d_assert(bcpItm.quantity >= (move_set ? to_move_quantity : bckItm.quantity));

		// add item to inventory with specified quantity
		{
			wiInventoryItem itm = bcpItm;
			itm.quantity = (move_set ? to_move_quantity : bckItm.quantity);
			AddItemToInventory(out_InventoryID, itm);
		}

		// remove from backpack
		AdjustBackpackSlotQuantity(slotID, -(move_set ? to_move_quantity : bckItm.quantity));

		// SYNC both versions of profiles
		savedLoadout_ = *loadout_;
		break;
	}
	case 2:
	{
		__int64 InventoryID = 0;

		for (uint32_t j = 0; j < profile_.ProfileData.NumItems; ++j)
		{
			if (profile_.ProfileData.Inventory[j].InventoryID != -1 && profile_.ProfileData.Inventory[j].itemID == n.loadout_.BackpackID)
			{
				InventoryID = profile_.ProfileData.Inventory[j].InventoryID;
				break;
			}
		}

		if (InventoryID <= 0)
			break;

		StartInventoryOp(); // start working with global inventory

		// swap backpack
		const BackpackConfig* cfg = g_pWeaponArmory->getBackpackConfig(n.loadout_.BackpackID);
		if (cfg == NULL) {
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
				"chbp item: %d", n.loadout_.BackpackID);
			break;
		}
		if (cfg->m_maxSlots != n.loadout_.Backpacksize) {
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
				"chbp slots: %d %d vs %d", n.loadout_.BackpackID, cfg->m_maxSlots, n.loadout_.Backpacksize);
			break;
		}

		// no need to validate InventoryID - server will do that
		char strInventoryID[128];
		sprintf(strInventoryID, "%I64d", InventoryID);

		CWOBackendReq req("api_CharBackpack.aspx");
		req.AddSessionInfo(profile_.CustomerID, profile_.SessionID);
		req.AddParam("skey1", "KfZnUUwZFJqrPJ1b4DaNZ3CkGnvz6MzPYAvP86wg");
		req.AddParam("CharID", loadout_->LoadoutID);

		req.AddParam("op", 16);
		req.AddParam("v1", strInventoryID);
		req.AddParam("v2", 0);
		req.AddParam("v3", 0);

		if (!req.Issue())
		{
			inventoryOpActive_ = false;
			r3dOutToLog("!!!! Loadout Change Thread failed - swap backpack, code: %d\n", req.resultCode_);
		}
		else
		{
			inventoryOpActive_ = false;

			__int64 out_InventoryID = 0;
			int nargs = sscanf(req.bodyStr_, "%I64d", &out_InventoryID);
			if (nargs != 1 || out_InventoryID == 0)
			{
				r3dOutToLog("!!!! Loadout Change Thread failed - swap backpack on scanning, bad ans: %s\n", req.bodyStr_);
			}
			else
			{
				// update backpack, safe to do here as those params will be updated in api job and if it fails, player will be disconnected
				loadout_->BackpackSize = cfg->m_maxSlots;
				loadout_->BackpackID = cfg->m_itemID;

				// SYNC both versions of profiles
				savedLoadout_ = *loadout_;

				OnLoadoutChanged();

				PKT_C2S_ChangeLoadoutAns_s n2;
				n2.BackpackID = loadout_->BackpackID;
				n2.BackpackSize = loadout_->BackpackSize;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			}
		}
		break;
	}

	case 3:
	{
		if (n.loadout_.item[slotID].ItemID <= 0)
			break;

		int to_move_quantity = n.loadout_.item[slotID].Quantity;
		if (to_move_quantity == 0)
			to_move_quantity = 1;

		to_move_quantity -= loadout_->Items[slotID].quantity;
		if (to_move_quantity <= 0)
			break;

		if (slotID >= loadout_->BackpackSize)
			break;

		// find item in inventory
		__int64	InvInventoryID = 0;
		for (uint32_t j = 0; j < profile_.ProfileData.NumItems; ++j)
		{
			if (profile_.ProfileData.Inventory[j].InventoryID != -1 && profile_.ProfileData.Inventory[j].itemID == n.loadout_.item[slotID].ItemID)
			{
				InvInventoryID = profile_.ProfileData.Inventory[j].InventoryID;
				break;
			}
		}

		if (InvInventoryID <= 0)
			break;

		const wiInventoryItem* invItm = profile_.getInventorySlot(InvInventoryID);
		if (invItm == NULL)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
				"n.InventoryID: %I64d", InvInventoryID);
			break;
		}

		if (invItm->itemID == WeaponConfig::ITEMID_NoDrop_Backpack)//AlexRedd:: no drop backpack
			break;

		// check weight
		const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
		r3d_assert(itemCfg);
		float totalWeight = loadout_->getTotalWeight();
		if (slotID != 0 && slotID != 1 && slotID != 6 && slotID != 7) // skip weapon & armor
		{
			totalWeight += itemCfg->m_Weight * n.loadout_.item[slotID].Quantity;

			if (loadout_->Skills[CUserSkills::SKILL_Physical3])
				totalWeight *= 0.95f;
			if (loadout_->Skills[CUserSkills::SKILL_Physical7])
				totalWeight *= 0.9f;
		}

		const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
		r3d_assert(bc);
		if (totalWeight > bc->m_maxWeight)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_WeightLimit;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			break;
		}

		int SlotTo = n.loadout_.item[slotID].SlotID;
		if (SlotTo == 0xFF)
		{
			SlotTo = GetBackpackSlotForItem(*invItm);
			if (SlotTo == -1)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoBackpackSpace;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				break;
			}
		}
		else
		{
			// check if we can place that item to this slot
			wiInventoryItem& bckItm = loadout_->Items[SlotTo];
			const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
			r3d_assert(itemCfg);

			bool canPlace =
				itemCfg &&
				storecat_CanPlaceItemToSlot(itemCfg, SlotTo) &&
				(bckItm.itemID == 0 || bckItm.CanStackWith(*invItm));

			if (!canPlace)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_BadTarget;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				break;
			}
		}
		r3d_assert(SlotTo >= 0 && SlotTo < loadout_->BackpackSize);

		StartInventoryOp(); // start working with global inventory

		char strInventoryID[128];
		sprintf(strInventoryID, "%I64d", InvInventoryID);

		CWOBackendReq req("api_CharBackpack.aspx");
		req.AddSessionInfo(profile_.CustomerID, profile_.SessionID);
		req.AddParam("skey1", "KfZnUUwZFJqrPJ1b4DaNZ3CkGnvz6MzPYAvP86wg");
		req.AddParam("CharID", loadout_->LoadoutID);
		req.AddParam("op", 53);		// inventory operation code - ApiBackpackFromInventory
		req.AddParam("v1", strInventoryID);	// value 1
		req.AddParam("v2", slotID);		// value 2
		req.AddParam("v3", to_move_quantity);		// value 3
		if (!req.Issue())
		{
			r3dOutToLog("!!!! Loadout Change Thread failed - inventory to backpack, code: %d\n", req.resultCode_);
			break;
		}

		__int64 out_InventoryID = 0;
		int nargs = sscanf(req.bodyStr_, "%I64d", &out_InventoryID);
		if (nargs != 1 || out_InventoryID == 0)
		{
			r3dOutToLog("!!!! Loadout Change Thread failed - inventory to backpack, bad ans: %s\n", req.bodyStr_);
			break;
		}

		inventoryOpActive_ = false;

		// move success
		wiInventoryItem* out_invItm = profile_.getInventorySlot(InvInventoryID);
		if (!out_invItm)
			break;

		if (out_invItm->quantity >= to_move_quantity)
			break;

		// adjust backpack, add item ignoring Var1 - that was already checked in PKT_C2S_FromInventoryReq_s
		{
			wiInventoryItem itm = *out_invItm;
			itm.quantity = to_move_quantity;
			itm.InventoryID = out_InventoryID;
			AddItemToBackpackSlot(slotID, itm);
		}

		// adjust inventory
		{
			PKT_S2C_InventoryModify_s n2;
			n2.InventoryID = out_invItm->InventoryID;
			n2.Quantity = out_invItm->quantity - to_move_quantity;
			n2.dbg_ItemID = out_invItm->itemID;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

			out_invItm->quantity -= to_move_quantity;
			if (out_invItm->quantity == 0)
				out_invItm->Reset();
		}

		// SYNC both versions of profiles
		savedLoadout_ = *loadout_;
		break;
	}

	case 4:
	{
		int to_move_quantity = n.loadout_.item[slotID].Quantity;
		if (to_move_quantity <= 0)
			to_move_quantity = 1;

		to_move_quantity -= loadout_->Items[slotID].quantity;
		if (to_move_quantity <= 0)
			to_move_quantity = 1;

		if (slotID >= loadout_->BackpackSize)
			break;

		// find item in inventory
		__int64	InvInventoryID = 0;
		for (uint32_t j = 0; j < profile_.ProfileData.NumItems; ++j)
		{
			if (profile_.ProfileData.Inventory[j].InventoryID != -1 && profile_.ProfileData.Inventory[j].itemID == n.loadout_.item[slotID].ItemID)
			{
				InvInventoryID = profile_.ProfileData.Inventory[j].InventoryID;
				break;
			}
		}

		if (InvInventoryID <= 0)
			break;

		const wiInventoryItem* invItm = profile_.getInventorySlot(InvInventoryID);
		if (invItm == NULL)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
				"n.InventoryID: %I64d", InvInventoryID);
			break;
		}

		if (invItm->itemID == WeaponConfig::ITEMID_NoDrop_Backpack)//AlexRedd:: no drop backpack
			break;

		// check weight
		const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
		r3d_assert(itemCfg);
		float totalWeight = loadout_->getTotalWeight();
		if (slotID != 0 && slotID != 1 && slotID != 6 && slotID != 7) // skip weapon & armor
		{
			totalWeight += itemCfg->m_Weight * n.loadout_.item[slotID].Quantity;

			if (loadout_->Skills[CUserSkills::SKILL_Physical3])
				totalWeight *= 0.95f;
			if (loadout_->Skills[CUserSkills::SKILL_Physical7])
				totalWeight *= 0.9f;
		}

		const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
		r3d_assert(bc);
		if (totalWeight > bc->m_maxWeight)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_WeightLimit;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			break;
		}

		int SlotTo = n.loadout_.item[slotID].SlotID;
		if (SlotTo == 0xFF)
		{
			SlotTo = GetBackpackSlotForItem(*invItm);
			if (SlotTo == -1)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoBackpackSpace;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				break;
			}
		}
		else
		{
			// check if we can place that item to this slot
			wiInventoryItem& bckItm = loadout_->Items[SlotTo];
			const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
			r3d_assert(itemCfg);

			bool canPlace =
				itemCfg &&
				storecat_CanPlaceItemToSlot(itemCfg, SlotTo) &&
				(bckItm.itemID == 0 || bckItm.CanStackWith(*invItm));

			if (!canPlace)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_BadTarget;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				break;
			}
		}
		r3d_assert(SlotTo >= 0 && SlotTo < loadout_->BackpackSize);

		StartInventoryOp(); // start working with global inventory

		char strInventoryID[128];
		sprintf(strInventoryID, "%I64d", InvInventoryID);

		CWOBackendReq req("api_CharBackpack.aspx");
		req.AddSessionInfo(profile_.CustomerID, profile_.SessionID);
		req.AddParam("skey1", "KfZnUUwZFJqrPJ1b4DaNZ3CkGnvz6MzPYAvP86wg");
		req.AddParam("CharID", loadout_->LoadoutID);
		req.AddParam("op", 53);		// inventory operation code - ApiBackpackFromInventory
		req.AddParam("v1", strInventoryID);	// value 1
		req.AddParam("v2", slotID);		// value 2
		req.AddParam("v3", to_move_quantity);		// value 3
		if (!req.Issue())
		{
			r3dOutToLog("!!!! Loadout Change Thread failed - inventory to backpack, code: %d\n", req.resultCode_);
			break;
		}

		__int64 out_InventoryID = 0;
		int nargs = sscanf(req.bodyStr_, "%I64d", &out_InventoryID);
		if (nargs != 1 || out_InventoryID == 0)
		{
			r3dOutToLog("!!!! Loadout Change Thread failed - inventory to backpack, bad ans: %s\n", req.bodyStr_);
			break;
		}

		inventoryOpActive_ = false;

		// move success
		wiInventoryItem* out_invItm = profile_.getInventorySlot(out_InventoryID);
		if (!out_invItm)
			break;

		if (out_invItm->quantity >= to_move_quantity)
			break;

		// adjust backpack, add item ignoring Var1 - that was already checked in PKT_C2S_FromInventoryReq_s
		{
			wiInventoryItem itm = *out_invItm;
			itm.quantity = to_move_quantity;
			itm.InventoryID = out_InventoryID;
			AddItemToBackpackSlot(slotID, itm);
		}

		// adjust inventory
		{
			PKT_S2C_InventoryModify_s n2;
			n2.InventoryID = out_invItm->InventoryID;
			n2.Quantity = out_invItm->quantity - to_move_quantity;
			n2.dbg_ItemID = out_invItm->itemID;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

			out_invItm->quantity -= to_move_quantity;
			if (out_invItm->quantity == 0)
				out_invItm->Reset();
		}

		// SYNC both versions of profiles
		savedLoadout_ = *loadout_;
		break;
	}
	}
}

void obj_ServerPlayer::loadout_Change_Thread(const PKT_C2S_ChangeLoadoutReq_s& n)
{
	// move all items to GI
	for (int i = 0; i < loadout_->BackpackSize; i++)
		loadout_Change_switch(n, 1, i);

	// change backpack
	if (loadout_->BackpackID != n.loadout_.BackpackID)
		loadout_Change_switch(n, 2, -1);

	// add items from inventory to backpack
	for (int i = 0; i < n.loadout_.Backpacksize; i++)
	{
		if (loadout_->Items[i].itemID == n.loadout_.item[i].ItemID)
			loadout_Change_switch(n, 3, i);
		else
			loadout_Change_switch(n, 4, i);
	}

	// report to client that async op is complete
	PKT_S2C_InventoryOpAns_s n2;
	n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Success;
	n2.GamePoints = profile_.ProfileData.GamePoints;
	n2.GameDollars = profile_.ProfileData.GameDollars;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
}

void obj_ServerPlayer::Move_All_Items(const PKT_C2S_MoveAllItemsToGIReq_s& n, int slotID)
{
	const wiInventoryItem& bckItm = loadout_->Items[slotID];
	if (bckItm.itemID <= 0)
		return;

	StartInventoryOp(); // start working with global inventory

	// find target InventoryID in inventory
	__int64 InvInventoryID = 0;

	for (uint32_t i = 0; i < profile_.ProfileData.NumItems; i++)
	{
		// can stack only non-modified items
		const wiInventoryItem& wi2 = profile_.ProfileData.Inventory[i];
		if (bckItm.CanStackWith(wi2, true)) // we allow to stack 'non stackable' items in GI
		{
			InvInventoryID = wi2.InventoryID;
			break;
		}
	}

	// check that we have free global inventory slot
	if (InvInventoryID == 0 && profile_.haveFreeInventorySlot() == false)
		return;

	char strInventoryID[128];
	sprintf(strInventoryID, "%I64d", InvInventoryID);

	CWOBackendReq req("api_CharBackpack.aspx");
	req.AddSessionInfo(profile_.CustomerID, profile_.SessionID);
	req.AddParam("skey1", "KfZnUUwZFJqrPJ1b4DaNZ3CkGnvz6MzPYAvP86wg");
	req.AddParam("CharID", loadout_->LoadoutID);
	req.AddParam("op", 52);		// inventory operation code - ApiBackpackToInventory
	req.AddParam("v1", strInventoryID);	// value 1
	req.AddParam("v2", slotID);	// value 2
	req.AddParam("v3", bckItm.quantity);		// value 3
	if (!req.Issue())
	{
		r3dOutToLog("!!!! Loadout Change Thread failed - backpack to inventory, code: %d\n", req.resultCode_);
		return;
	}

	__int64 out_InventoryID = 0;
	int nargs = sscanf(req.bodyStr_, "%I64d", &out_InventoryID);
	if (nargs != 1 || out_InventoryID == 0)
	{
		r3dOutToLog("!!!! Loadout Change Thread failed - backpack to inventory, bad ans: %s\n", req.bodyStr_);
		return;
	}

	inventoryOpActive_ = false;

	// move success
	wiInventoryItem& bcpItm = loadout_->Items[slotID];
	r3d_assert(bcpItm.itemID);
	r3d_assert(bcpItm.quantity >= bckItm.quantity);

	// add item to inventory with specified quantity
	{
		wiInventoryItem itm = bcpItm;
		itm.quantity = bckItm.quantity;
		AddItemToInventory(out_InventoryID, itm);
	}

	// remove from backpack
	AdjustBackpackSlotQuantity(slotID, -bckItm.quantity);

	// SYNC both versions of profiles
	savedLoadout_ = *loadout_;
}

void obj_ServerPlayer::Move_All_Items_Thread(const PKT_C2S_MoveAllItemsToGIReq_s& n)
{
	// move all items to GI
	for (int i = 0; i < loadout_->BackpackSize; i++)
		Move_All_Items(n, i);

	// report to client that async op is complete
	PKT_S2C_InventoryOpAns_s n2;
	n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Success;
	n2.GamePoints = profile_.ProfileData.GamePoints;
	n2.GameDollars = profile_.ProfileData.GameDollars;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_ChangeLoadoutReq_s& n)
{
	bool disableOp = false;
#ifdef DISABLE_GI_ACCESS_ON_PTE_MAP
	if (gServerLogic.ginfo_.channel == 6) // public test environment server, do not save any info about player, as those maps might have game breaking loot\changes, so let's not propagate them to the rest of the game
		disableOp = true;
#endif
#ifdef DISABLE_GI_ACCESS_ON_PTE_STRONGHOLD_MAP
	if (gServerLogic.ginfo_.channel == 6 && gServerLogic.ginfo_.mapId == GBGameInfo::MAPID_WZ_Cliffside) // public test environment server, do not save any info about player, as those maps might have game breaking loot\changes, so let's not propagate them to the rest of the game
		disableOp = true;
#endif
#ifdef DISABLE_GI_ACCESS_FOR_CALI_SERVER
	if (gServerLogic.ginfo_.mapId == GBGameInfo::MAPID_WZ_California)
		disableOp = true;
#endif
#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())
		disableOp = true;
#endif

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode	
	if (isGameBattleRoyale)
		disableOp = true;
#endif //ENABLE_BATTLE_ROYALE

	if (disableOp)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoInventorySpace;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// must be in safe zone
	if ((loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox) == 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
			"not in safe zone");
		return;
	}
	// must be close to Vault NPC
	if (!gServerNPCMngr.isCloseToNPC(GetPosition(), "obj_VaultNPC"))
	{
		// do not kick for this cheat... instead we will ban wave :)
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "ShopOp",
			"not close to NPC");
	}

	std::thread loadout_change(&obj_ServerPlayer::loadout_Change_Thread, this, n);
	loadout_change.detach();
	}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_MoveAllItemsToGIReq_s& n)
{
	bool disableOp = false;
#ifdef DISABLE_GI_ACCESS_ON_PTE_MAP
	if (gServerLogic.ginfo_.channel == 6) // public test environment server, do not save any info about player, as those maps might have game breaking loot\changes, so let's not propagate them to the rest of the game
		disableOp = true;
#endif
#ifdef DISABLE_GI_ACCESS_ON_PTE_STRONGHOLD_MAP
	if (gServerLogic.ginfo_.channel == 6 && gServerLogic.ginfo_.mapId == GBGameInfo::MAPID_WZ_Cliffside) // public test environment server, do not save any info about player, as those maps might have game breaking loot\changes, so let's not propagate them to the rest of the game
		disableOp = true;
#endif
#ifdef DISABLE_GI_ACCESS_FOR_CALI_SERVER
	if (gServerLogic.ginfo_.mapId == GBGameInfo::MAPID_WZ_California)
		disableOp = true;
#endif
#ifdef DISABLE_GI_ACCESS_FOR_DEV_EVENT_SERVER
	if (gServerLogic.ginfo_.IsDevEvent())
		disableOp = true;
#endif

#ifdef ENABLE_BATTLE_ROYALE
	//AlexRedd:: BR mode	
	if (isGameBattleRoyale)
		disableOp = true;
#endif //ENABLE_BATTLE_ROYALE

	if (disableOp)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoInventorySpace;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// must be in safe zone
	if ((loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox) == 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromInv",
			"not in safe zone");
		return;
	}
	// must be close to Vault NPC
	if (!gServerNPCMngr.isCloseToNPC(GetPosition(), "obj_VaultNPC"))
	{
		// do not kick for this cheat... instead we will ban wave :)
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "ShopOp",
			"not close to NPC");
	}

	std::thread Move_All_Items(&obj_ServerPlayer::Move_All_Items_Thread, this, n);
	Move_All_Items.detach();
	}

void obj_ServerPlayer::OnToInventoryCallback(const CJobBackpackToInventory* job)
{
	inventoryOpActive_ = false;
	if (job->out_OpAns != PKT_S2C_InventoryOpAns_s::ANS_Success)
	{
		PKT_S2C_InventoryOpAns_s n;
		n.OpAns = (BYTE)job->out_OpAns;
		gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
		return;
	}

	// move success
	wiInventoryItem& bcpItm = loadout_->Items[job->SlotFrom];
	r3d_assert(bcpItm.itemID);
	r3d_assert(bcpItm.quantity >= job->Amount);

	// add item to inventory with specified quantity
	{
		wiInventoryItem itm = bcpItm;
		itm.quantity = job->Amount;
		AddItemToInventory(job->out_InventoryID, itm);
	}

	// remove from backpack
	AdjustBackpackSlotQuantity(job->SlotFrom, -job->Amount);

	// SYNC both versions of profiles
	savedLoadout_ = *loadout_;

	// report to client that async op is complete
	PKT_S2C_InventoryOpAns_s n;
	n.OpAns = (BYTE)job->out_OpAns;
	n.GamePoints = profile_.ProfileData.GamePoints;
	n.GameDollars = profile_.ProfileData.GameDollars;
	gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));
}

obj_Vehicle* obj_ServerPlayer::GetAccessToVehicleBox(gp2pnetid_t VehicleID)
{
	GameObject* obj = GameWorld().GetNetworkObject(VehicleID);
	if (obj == NULL)
		return NULL;
	if (obj->Class->Name != "obj_Vehicle")
		return NULL;
	obj_Vehicle* vehicle = (obj_Vehicle*)obj;

	// must be close to vehiclebox
	if ((GetPosition() - vehicle->GetPosition()).Length() > 5.0f)
		return NULL; // cheat?	

	return vehicle;
}

obj_ServerAirdrop* obj_ServerPlayer::GetAccessToAirdrop(gp2pnetid_t AirdropID)
{
	GameObject* obj = GameWorld().GetNetworkObject(AirdropID);
	if (obj == NULL)
		return NULL;
	if (obj->Class->Name != "obj_ServerAirdrop")
		return NULL;
	obj_ServerAirdrop* airdrop = (obj_ServerAirdrop*)obj;

	// must be close to vehiclebox
	if ((GetPosition() - airdrop->GetPosition()).Length() > 5.0f)
		return NULL; // cheat?	

	/*if(airdrop->IsLockdownActive(this))
	{
	gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Airdrop, false, "Airdrop",
	"inventory op while lockdowned");

	PKT_S2C_InventoryOpAns_s n2;
	n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	return NULL;
	}

	// check if lockbox was used way too fast
	if(airdrop->IsAirdropAbused(this))
	{
	return NULL;
	}*/

	return airdrop;
}

obj_ServerLockbox* obj_ServerPlayer::GetAccessToLockbox(gp2pnetid_t lockboxID, const char* AccessCodeS)
{
	GameObject* obj = GameWorld().GetNetworkObject(lockboxID);
	if (obj == NULL)
		return NULL;
	if (obj->Class->Name != "obj_ServerLockbox")
		return NULL;
	obj_ServerLockbox* lockbox = (obj_ServerLockbox*)obj;

	// must be close to lockbox
	if ((GetPosition() - lockbox->GetPosition()).Length() > 5.0f)
		return NULL; // cheat?

	//if(lockbox->IsLockdownActive(this))
	//{
	//	gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Lockbox, false, "Lockbox",
	//		"inventory op while lockdowned");
	//	
	//	PKT_S2C_InventoryOpAns_s n2;
	//	n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
	//	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	//	return NULL;
	//}
	//if(strcmp(lockbox->m_AccessCodeS, AccessCodeS)!=0)
	//{
	//	// cheat or code was changed by other user
	//	lockbox->SetLockdown(profile_.CustomerID);

	//	PKT_S2C_InventoryOpAns_s n2;
	//	n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
	//	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	//	return NULL;
	//}

	//// check if lockbox was used way too fast
	//if(lockbox->IsLockboxAbused(this))
	//{
	//	return NULL;
	//}

	return lockbox;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_LockboxItemLockboxToBackpack_s& n)
{
	if (!IsNullTerminated(n.AccessCodeS, sizeof(n.AccessCodeS))) {
		gServerLogic.DisconnectPeer(peerId_, true, "invalid PKT_C2S_LockboxItemLockboxToBackpack_s");
		return;
	}

	obj_ServerLockbox* lockbox = GetAccessToLockbox(n.lockboxID, n.AccessCodeS);
	obj_Vehicle* vehicle = GetAccessToVehicleBox(n.lockboxID);
	obj_ServerAirdrop* airdrop = GetAccessToAirdrop(n.lockboxID);

	if (!lockbox && !vehicle && !airdrop)
		return;

	if (n.SlotTo != 0xFF && n.SlotTo >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromLockbox",
			"slotto: %d %d", n.SlotTo, loadout_->BackpackSize);
		return;
	}

	if (lockbox)
	{
		// check if that item still in lockbox and have enough quantity
		const wiInventoryItem* invItm = lockbox->FindItemWithInvID(n.LockboxInventoryID);
		if (invItm == NULL || invItm->itemID != n.dbg_InvItemID || n.Amount > invItm->quantity)
		{
			// this is valid situation if multiple users is accessing lockbox
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// debug checks for desync
		if (n.SlotTo != 0xFF && (loadout_->Items[n.SlotTo].itemID != n.dbg_ItemID || loadout_->Items[n.SlotTo].quantity != n.dbg_Quantity))
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "FromLockbox",
				"d1: %d-%d %d-%d", loadout_->Items[n.SlotTo].itemID, n.dbg_ItemID, loadout_->Items[n.SlotTo].quantity, n.dbg_Quantity);

			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// check for amount cheat
		if (n.Amount <= 0)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromLockbox",
				"amount: %d", n.Amount);
			return;
		}

		int SlotTo = (signed int)n.SlotTo;
		if (n.SlotTo == 0xFF)
		{
			SlotTo = GetBackpackSlotForItem(*invItm);
			if (SlotTo == -1)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoBackpackSpace;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				return;
			}
		}
		else
		{
			// check if we can place that item to this slot
			wiInventoryItem& bckItm = loadout_->Items[SlotTo];
			const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
			r3d_assert(itemCfg);

			bool canPlace =
				itemCfg &&
				storecat_CanPlaceItemToSlot(itemCfg, SlotTo) &&
				(bckItm.itemID == 0 || bckItm.CanStackWith(*invItm));

			if (!canPlace)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_BadTarget;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				return;
			}
		}
		r3d_assert(SlotTo >= 0 && SlotTo < loadout_->BackpackSize);

		// check weight
		const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
		r3d_assert(itemCfg);
		float totalWeight = loadout_->getTotalWeight();
		if (SlotTo != 0 && SlotTo != 1 && SlotTo != 6 && SlotTo != 7) // skip weapon & armor
		{
			totalWeight += itemCfg->m_Weight * n.Amount;

			if (loadout_->Skills[CUserSkills::SKILL_Physical3])
				totalWeight *= 0.95f;
			if (loadout_->Skills[CUserSkills::SKILL_Physical7])
				totalWeight *= 0.9f;
		}

		const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
		r3d_assert(bc);
		if (totalWeight > bc->m_maxWeight)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_WeightLimit;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// adjust backpack, add item ignoring Var1 - that was already checked in PKT_C2S_FromInventoryReq_s
		{
			wiInventoryItem itm = *invItm;
			itm.quantity = n.Amount;
			itm.InventoryID = 0;
			AddItemToBackpackSlot(SlotTo, itm);
		}
	}

	if (vehicle)
	{
		// check if that item still in lockbox and have enough quantity
		const wiInventoryItem* invItm = vehicle->FindVehicleItemWithInvID(n.LockboxInventoryID);
		if (invItm == NULL || invItm->itemID != n.dbg_InvItemID || n.Amount > invItm->quantity)
		{
			// this is valid situation if multiple users is accessing lockbox
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// debug checks for desync
		if (n.SlotTo != 0xFF && (loadout_->Items[n.SlotTo].itemID != n.dbg_ItemID || loadout_->Items[n.SlotTo].quantity != n.dbg_Quantity))
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "FromLockbox",
				"d1: %d-%d %d-%d", loadout_->Items[n.SlotTo].itemID, n.dbg_ItemID, loadout_->Items[n.SlotTo].quantity, n.dbg_Quantity);

			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// check for amount cheat
		if (n.Amount <= 0)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromLockbox",
				"amount: %d", n.Amount);
			return;
		}

		int SlotTo = (signed int)n.SlotTo;
		if (n.SlotTo == 0xFF)
		{
			SlotTo = GetBackpackSlotForItem(*invItm);
			if (SlotTo == -1)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoBackpackSpace;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				return;
			}
		}
		else
		{
			// check if we can place that item to this slot
			wiInventoryItem& bckItm = loadout_->Items[SlotTo];
			const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
			r3d_assert(itemCfg);

			bool canPlace =
				itemCfg &&
				storecat_CanPlaceItemToSlot(itemCfg, SlotTo) &&
				(bckItm.itemID == 0 || bckItm.CanStackWith(*invItm));

			if (!canPlace)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_BadTarget;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				return;
			}
		}
		r3d_assert(SlotTo >= 0 && SlotTo < loadout_->BackpackSize);

		// check weight
		const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
		r3d_assert(itemCfg);
		float totalWeight = loadout_->getTotalWeight();
		if (SlotTo != 0 && SlotTo != 1 && SlotTo != 6 && SlotTo != 7) // skip weapon & armor
		{
			totalWeight += itemCfg->m_Weight * n.Amount;

			if (loadout_->Skills[CUserSkills::SKILL_Physical3])
				totalWeight *= 0.95f;
			if (loadout_->Skills[CUserSkills::SKILL_Physical7])
				totalWeight *= 0.9f;
		}

		const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
		r3d_assert(bc);
		if (totalWeight > bc->m_maxWeight)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_WeightLimit;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// adjust backpack, add item ignoring Var1 - that was already checked in PKT_C2S_FromInventoryReq_s
		{
			wiInventoryItem itm = *invItm;
			itm.quantity = n.Amount;
			itm.InventoryID = 0;
			AddItemToBackpackSlot(SlotTo, itm);
		}
	}

	if (airdrop)
	{
		// check if that item still in lockbox and have enough quantity
		const wiInventoryItem* invItm = airdrop->FindAirdropItemWithInvID(n.LockboxInventoryID);
		if (invItm == NULL || invItm->itemID != n.dbg_InvItemID || n.Amount > invItm->quantity)
		{
			// this is valid situation if multiple users is accessing lockbox
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// debug checks for desync
		if (n.SlotTo != 0xFF && (loadout_->Items[n.SlotTo].itemID != n.dbg_ItemID || loadout_->Items[n.SlotTo].quantity != n.dbg_Quantity))
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "FromLockbox",
				"d1: %d-%d %d-%d", loadout_->Items[n.SlotTo].itemID, n.dbg_ItemID, loadout_->Items[n.SlotTo].quantity, n.dbg_Quantity);

			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// check for amount cheat
		if (n.Amount <= 0)
		{
			gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "FromLockbox",
				"amount: %d", n.Amount);
			return;
		}

		int SlotTo = (signed int)n.SlotTo;
		if (n.SlotTo == 0xFF)
		{
			SlotTo = GetBackpackSlotForItem(*invItm);
			if (SlotTo == -1)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoBackpackSpace;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				return;
			}
		}
		else
		{
			// check if we can place that item to this slot
			wiInventoryItem& bckItm = loadout_->Items[SlotTo];
			const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
			r3d_assert(itemCfg);

			bool canPlace =
				itemCfg &&
				storecat_CanPlaceItemToSlot(itemCfg, SlotTo) &&
				(bckItm.itemID == 0 || bckItm.CanStackWith(*invItm));

			if (!canPlace)
			{
				PKT_S2C_InventoryOpAns_s n2;
				n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_BadTarget;
				gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
				return;
			}
		}
		r3d_assert(SlotTo >= 0 && SlotTo < loadout_->BackpackSize);

		// check weight
		const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(invItm->itemID);
		r3d_assert(itemCfg);
		float totalWeight = loadout_->getTotalWeight();
		if (SlotTo != 0 && SlotTo != 1 && SlotTo != 6 && SlotTo != 7) // skip weapon & armor
		{
			totalWeight += itemCfg->m_Weight * n.Amount;

			if (loadout_->Skills[CUserSkills::SKILL_Physical3])
				totalWeight *= 0.95f;
			if (loadout_->Skills[CUserSkills::SKILL_Physical7])
				totalWeight *= 0.9f;
		}

		const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
		r3d_assert(bc);
		if (totalWeight > bc->m_maxWeight)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_WeightLimit;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}

		// adjust backpack, add item ignoring Var1 - that was already checked in PKT_C2S_FromInventoryReq_s
		{
			wiInventoryItem itm = *invItm;
			itm.quantity = n.Amount;
			itm.InventoryID = 0;
			AddItemToBackpackSlot(SlotTo, itm);
		}
	}

	// adjust lockbox
	if (lockbox)
		lockbox->RemoveItemFromLockbox(n.LockboxInventoryID, n.Amount);

	// adjust vehiclebox
	if (vehicle)
		vehicle->RemoveItemFromVehicleBox(n.LockboxInventoryID, n.Amount);

	// adjust airdropbox
	if (airdrop)
		airdrop->RemoveItemFromAirdrop(n.LockboxInventoryID, n.Amount);

	gServerLogic.ApiPlayerUpdateChar(this);

	if (lockbox)
		lockbox->SendContentToPlayer(this);
	if (vehicle)
		vehicle->SendContentToPlayer(this);
	if (airdrop)
		airdrop->SendContentToPlayer(this);

	// report to client that async op is complete
	PKT_S2C_InventoryOpAns_s n2;
	n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Success;
	n2.GamePoints = profile_.ProfileData.GamePoints;
	n2.GameDollars = profile_.ProfileData.GameDollars;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
	return;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_LockboxPickup_s& n)
{
	if (!IsNullTerminated(n.AccessCodeS, sizeof(n.AccessCodeS))) {
		gServerLogic.DisconnectPeer(peerId_, true, "invalid PKT_C2S_LockboxPickup_s");
		return;
	}

	obj_ServerLockbox* lockbox = GetAccessToLockbox(n.lockboxID, n.AccessCodeS);
	if (!lockbox)
		return;

	// check for double pickup
	if (!lockbox->isActive())
		return;

	// very important: do not allow pickup until lockbox receive it ServerObjectID
	if (lockbox->GetNetworkHelper()->srvObjParams_.ServerObjectID == 0)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// lockbox must be empty (yes, we're too lazy to put items to backpack)
	if (lockbox->items.size() > 0 && !profile_.ProfileData.isDevAccount)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// check owner
	if (profile_.CustomerID != lockbox->GetNetworkHelper()->srvObjParams_.CustomerID && !profile_.ProfileData.isDevAccount)
	{
		PKT_S2C_LockboxOpReq_s n2;
		n2.op = PKT_S2C_LockboxOpReq_s::LBOR_NotOwner;
		n2.lockboxID = toP2pNetId(lockbox->GetNetworkID()); // object is still there so it's safe
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// create lockbox item and add it to backpack
	wiInventoryItem itm;
	if (lockbox->m_ItemID == WeaponConfig::ITEMID_Lockbox)
		itm.itemID = WeaponConfig::ITEMID_Lockbox;
	else if (lockbox->m_ItemID == WeaponConfig::ITEMID_LockboxBig)
		itm.itemID = WeaponConfig::ITEMID_LockboxBig;
	else if (lockbox->m_ItemID == WeaponConfig::ITEMID_LockboxSmall)
		itm.itemID = WeaponConfig::ITEMID_LockboxSmall;
	itm.quantity = 1;

	int SlotTo = GetBackpackSlotForItem(itm);
	if (SlotTo == -1)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoBackpackSpace;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// check weight
	const BaseItemConfig* itemCfg = g_pWeaponArmory->getConfig(WeaponConfig::ITEMID_Lockbox);
	r3d_assert(itemCfg);
	float totalWeight = loadout_->getTotalWeight();
	if (SlotTo != 0 && SlotTo != 1 && SlotTo != 6 && SlotTo != 7) // skip weapon & armor
	{
		totalWeight += itemCfg->m_Weight;

		if (loadout_->Skills[CUserSkills::SKILL_Physical3])
			totalWeight *= 0.95f;
		if (loadout_->Skills[CUserSkills::SKILL_Physical7])
			totalWeight *= 0.9f;
	}

	const BackpackConfig* bc = g_pWeaponArmory->getBackpackConfig(loadout_->BackpackID);
	r3d_assert(bc);
	if (totalWeight > bc->m_maxWeight)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_WeightLimit;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	AddItemToBackpackSlot(SlotTo, itm);
	gServerLogic.ApiPlayerUpdateChar(this);

	// close lockbox
	PKT_S2C_LockboxOpReq_s n2;
	n2.op = PKT_S2C_LockboxOpReq_s::LBOR_Close;
	n2.lockboxID = toP2pNetId(lockbox->GetNetworkID());
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

	// delete server lockbox
	lockbox->DestroyLockbox();
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_LockboxItemBackpackToLockbox_s& n)
{
	if (!IsNullTerminated(n.AccessCodeS, sizeof(n.AccessCodeS))) {
		gServerLogic.DisconnectPeer(peerId_, true, "invalid PKT_C2S_LockboxItemBackpackToLockbox_s");
		return;
	}

	obj_ServerLockbox* lockbox = GetAccessToLockbox(n.lockboxID, n.AccessCodeS);
	obj_Vehicle* vehicle = GetAccessToVehicleBox(n.lockboxID);
	if (!lockbox && !vehicle)
		return;

	if (n.SlotFrom >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "ToLockbox",
			"slotfrom: %d %d", n.SlotFrom, loadout_->BackpackSize);
		return;
	}

	const wiInventoryItem& bckItm = loadout_->Items[n.SlotFrom];

	// debug checks for desync
	if (bckItm.itemID == 0 || bckItm.itemID != n.dbg_ItemID || bckItm.quantity != n.dbg_Quantity)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, false, "ToLockbox",
			"d1: %d-%d %d-%d", bckItm.itemID, n.dbg_ItemID, bckItm.quantity, n.dbg_Quantity);

		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Desync;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// check for amount cheat
	if (n.Amount > bckItm.quantity || n.Amount <= 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_InventoryOps, true, "ToLockbox",
			"amount: %d %d", bckItm.quantity, n.Amount);
		return;
	}

	//AlexRedd:: Vip items (nodrop)
	bool vipItem = GetVipItem(n.SlotFrom);
	if (vipItem)
	{
		PKT_S2C_InventoryOpAns_s n2;
		n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Forbidden;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
		return;
	}

	// for people who trying to move stacks
	int moveAmount = n.Amount;
	if (moveAmount > 1 && !storecat_IsItemStackable(bckItm.itemID))
		moveAmount = 1;

	if (lockbox)
	{
		// add to lockbox
		if (lockbox->AddItemToLockbox(bckItm, moveAmount) == false)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoInventorySpace;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}
	}

	if (vehicle)
	{
		// add to vehiclebox
		if (vehicle->AddItemToVehicleBox(bckItm, moveAmount) == false)
		{
			PKT_S2C_InventoryOpAns_s n2;
			n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_NoInventorySpace;
			gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
			return;
		}
	}

	AdjustBackpackSlotQuantity(n.SlotFrom, -moveAmount);

	gServerLogic.ApiPlayerUpdateChar(this);

	if (lockbox)
		lockbox->SendContentToPlayer(this);
	if (vehicle)
		vehicle->SendContentToPlayer(this);

	// report to client that async op is complete
	PKT_S2C_InventoryOpAns_s n2;
	n2.OpAns = PKT_S2C_InventoryOpAns_s::ANS_Success;
	n2.GamePoints = profile_.ProfileData.GamePoints;
	n2.GameDollars = profile_.ProfileData.GameDollars;
	gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_DisconnectReq_s& n)
{
	r3dOutToLog("PKT_C2S_DisconnectReq for %s\n", userName);

	// ignore disconnect request while we operating with global inventory
	if (inventoryOpActive_)
		return;

	if (loadout_->Alive == 0 || profile_.ProfileData.isDevAccount || loadout_->GameFlags & wiCharDataFull::GAMEFLAG_NearPostBox)
	{
		//		if (!IsSwimming() && killedBy != obj_ServerGravestone::KilledBy_Unknown && loadout_->Alive == 0
		//#ifdef ENABLE_BATTLE_ROYALE
		//			//AlexRedd:: BR mode
		//			&& !isGameBattleRoyale
		//#endif //ENABLE_BATTLE_ROYALE
		//			&& gServerLogic.ginfo_.channel != 7)
		//		{
		//			// Create the gravestone.
		//			obj_ServerGravestone* gravestone = (obj_ServerGravestone*)srv_CreateGameObject("obj_ServerGravestone", "obj_ServerGravestone", GetPosition());
		//			gravestone->Init(this);
		//		}

		PKT_C2S_DisconnectReq_s n2;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

		gServerLogic.DisconnectPeer(peerId_, false, "disconnect request while dead, we already updated profile");
		return;
	}

#ifdef VEHICLES_ENABLED
	if (isInVehicle)
		ExitVehicle(true, true, true);
#endif

	// start update thread, player will disconnect itself when thread is finished
	if (!wasDisconnected_)
	{
		gServerLogic.ApiPlayerUpdateChar(this, true);
		gServerLogic.ApiPlayerUpdateWeaponSkins(this);//new add skin
		wasDisconnected_ = true;
	}
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_SetRespawnData_s& n)
{
	if (loadout_->Alive == 0
#ifdef ENABLE_BATTLE_ROYALE
		//AlexRedd:: BR mode
		&& isGameBattleRoyale && !gServerLogic.m_isGameHasStarted ||
		loadout_->Alive == 0 && !isGameBattleRoyale
#endif //ENABLE_BATTLE_ROYALE
		)
		gServerLogic.RespawnPlayer(this, n.spawnSelected);
	//r3dOutToLog("@@@@@@ RespawnPlayer %s, %d\n", userName, n.spawnSelected); CLOG_INDENT;
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_FallingDamage_s& n)
{
	//r3dOutToLog("Falling damage to %s, damage=%d\n", Name.c_str(), n.damage); CLOG_INDENT;
	if (n.damage == 0)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_FallingDamage, 0, "zero falling damage");
	}

	float damage = (float)n.damage;
	if (loadout_->Skills[CUserSkills::SKILL_Survival5])
		damage *= 0.95f;
	if (loadout_->Skills[CUserSkills::SKILL_Survival9])
		damage *= 0.9f;

	gServerLogic.ApplyDamage(this, this, GetPosition(), damage, false, storecat_INVALID, 0);
}

void obj_ServerPlayer::OnNetPacket(const PKT_C2S_PlayerWeapDataRepAns_s& n)
{
	if (weapDataReqExp < 0)
		return;
	weapDataReqExp = -1;

	// if weapon data was updated more that once it mean that updated happened in middle of the game
	// so skip validation
	if (gServerLogic.weaponDataUpdates_ >= 2)
		return;

	for (int i = 0; i < 2; i++)
	{
		if (m_WeaponArray[i] == NULL)
			continue;
		DWORD hash = m_WeaponArray[i]->getConfig()->GetClientParametersHash();
		if (hash == n.weaponsDataHash[i])
			continue;

		const WeaponConfig& wc1 = *m_WeaponArray[i]->getConfig();
		WeaponConfig wc2(n.debug_wid[i]);
		wc2.copyParametersFrom(n.debug_winfo[i]);

		if (wc1.m_itemID != wc2.m_itemID)
		{
#ifdef ENABLE_BATTLE_ROYALE
			if (!isGameBattleRoyale)
#endif //ENABLE_BATTLE_ROYALE
			{
				gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, false, "weapDataRep different",
					"slot%d c:%06d s:%06d (p:%06d)", i, wc2.m_itemID, wc1.m_itemID, m_dbg_PreviousWeapon[i]);
			}
		}
		else
		{
			// create diffs string for logging
			char diffs[4096] = "";
			if (fabs((float)wc1.m_spread - (float)wc2.m_spread) > 0.01f)
				sprintf(diffs + strlen(diffs), "s:%.2f/%.2f ", (float)wc1.m_spread, (float)wc2.m_spread);
			if (fabs((float)wc1.m_recoil - (float)wc2.m_recoil) > 0.01f)
				sprintf(diffs + strlen(diffs), "r:%.2f/%.2f ", (float)wc1.m_recoil, (float)wc2.m_recoil);
			if (fabs((float)wc1.m_reloadTime - (float)wc2.m_reloadTime) > 0.01f)
				sprintf(diffs + strlen(diffs), "t:%.2f/%.2f ", (float)wc1.m_reloadTime, (float)wc2.m_reloadTime);
			if (fabs((float)wc1.m_fireDelay - (float)wc2.m_fireDelay) > 0.01f)
				sprintf(diffs + strlen(diffs), "d:%.2f/%.2f ", (float)wc1.m_fireDelay, (float)wc2.m_fireDelay);
			if (fabs((float)wc1.m_AmmoSpeed - (float)wc2.m_AmmoSpeed) > 0.01f)
				sprintf(diffs + strlen(diffs), "a:%.2f/%.2f ", (float)wc1.m_AmmoSpeed, (float)wc2.m_AmmoSpeed);

			// report it only once per session (for now, because there is no disconnect yet)
			if (diffs[0] && !weapCheatReported)
			{
#ifdef ENABLE_BATTLE_ROYALE
				if (!isGameBattleRoyale)
#endif //ENABLE_BATTLE_ROYALE
				{
					weapCheatReported = true;
					gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_BadWeapData, false, "weapDataRep different",
						"id:%d, diff:%s", wc1.m_itemID, diffs);
				}
			}

			// automatic ban.
			if (diffs[0])
			{
				gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_BadWeapData, false, "FairFight: Banned '%s' weapDataRep different", "id:%d, diff:%s", profile_.username /*userName*/, wc1.m_itemID, diffs);
				CJobBanUser* job = new CJobBanUser(this);
				r3dscpy(job->BanReason, diffs);
				g_AsyncApiMgr->AddJob(job);

				char chatmessage[128] = { 0 };
				PKT_C2C_ChatMessage_s n1;
				sprintf(chatmessage, "FairFight: Banned '%s'", profile_.username /*userName*/);
				r3dscpy(n1.gamertag, "[SYSTEM]");
				r3dscpy(n1.msg, chatmessage);
				n1.msgChannel = 1;
				n1.userFlag = 5;
				gServerLogic.p2pBroadcastToAll(&n1, sizeof(n1), true);

				gServerLogic.DisconnectPeer(peerId_, true, diffs);
			}
	}
}
}

#ifdef VEHICLES_ENABLED
void obj_ServerPlayer::OnNetPacket(const PKT_C2S_VehicleRepair_s& n)
{
	GameObject* gameObj = GameWorld().GetNetworkObject(n.vehicleId);
	if (!gameObj || !gameObj->isObjType(OBJTYPE_Vehicle))
	{
		PKT_S2C_BackpackUnlock_s n3;
		gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
		return;
	}

	obj_Vehicle* vehicle = (obj_Vehicle*)gameObj;
	if (vehicle->GetDurability() <= 0)
	{
		PKT_S2C_BackpackUnlock_s n3;
		gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
		return;
	}

	if (n.slotId >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"repair vehicle: %d %d", n.slotId, loadout_->BackpackSize);
		return;
	}

	wiInventoryItem& wi = loadout_->Items[n.slotId];

	float repairAmount = 0.0f;
	if (wi.itemID == WeaponConfig::ITEMID_RepairKit)
		repairAmount = 0.3f;
	else if (wi.itemID == WeaponConfig::ITEMID_PremRepairKit)
		repairAmount = 0.6f;
	else
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "vehicle",
			"repair %d", wi.itemID);
		return;
	}

	AdjustBackpackSlotQuantity(n.slotId, -1);

	vehicle->SetDurability((short)(vehicle->maxDurability * repairAmount));

	PKT_S2C_BackpackUnlock_s n3;
	gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
}
//new add skin
void obj_ServerPlayer::OnNetPacket(const PKT_C2S_LearnSkinItem_s& n)
{
	if (!SetItemSkinDB(n.ItemID, n.SkinID))
	{
		PKT_C2S_LearnSkinItem_s n2;
		n2.ItemID = n.ItemID;
		n2.SkinID = n.SkinID;
		gServerLogic.p2pSendToPeer(peerId_, this, &n2, sizeof(n2));

		BackpackRemoveItemSlot(n.SlotID);

		gServerLogic.ApiPlayerUpdateWeaponSkins(this);
		gServerLogic.ApiPlayerUpdateChar(this);
	}

}
//new add skin
void obj_ServerPlayer::OnNetPacket(const PKT_C2S_VehicleRefuel_s& n)
{
	GameObject* gameObj = GameWorld().GetNetworkObject(n.vehicleId);
	if (!gameObj || !gameObj->isObjType(OBJTYPE_Vehicle))
	{
		PKT_S2C_BackpackUnlock_s n3;
		gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
		return;
	}

	obj_Vehicle* vehicle = (obj_Vehicle*)gameObj;
	if (vehicle->GetDurability() <= 0)
	{
		PKT_S2C_BackpackUnlock_s n3;
		gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
		return;
	}

	if (n.slotId >= loadout_->BackpackSize)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Protocol, true, "backpack",
			"refuel vehicle: %d %d", n.slotId, loadout_->BackpackSize);
		return;
	}

	wiInventoryItem& wi = loadout_->Items[n.slotId];

	if (wi.itemID != WeaponConfig::ITEMID_Gas)
	{
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Data, true, "vehicle",
			"refuel %d", wi.itemID);
		return;
	}

	AdjustBackpackSlotQuantity(n.slotId, -1);

	vehicle->AddFuel(vehicle->maxFuel);

	PKT_S2C_BackpackUnlock_s n3;
	gServerLogic.p2pSendToPeer(peerId_, this, &n3, sizeof(n3));
}
#endif

void obj_ServerPlayer::SetLatePacketsBarrier(const char* reason)
{
	r3dOutToLog("peer%02d, SetLatePacketsBarrier: %s\n", peerId_, reason);

	packetBarrierReason = reason;
	myPacketSequence++;
	weapDataReqExp = -1;

	PKT_C2C_PacketBarrier_s n;
	gServerLogic.p2pSendToPeer(peerId_, this, &n, sizeof(n));

	// NOTE:
	// from now on, we'll ignore received packets until client ack us with same barrier packet.
	// so, any fire/move/etc requests that will invalidate logical state of player will be automatically ignored
}

void obj_ServerPlayer::SetupPlayerNetworkItem(GameObject* obj)
{
	obj->SetNetworkID(gServerLogic.GetFreeNetId());
	obj->NetworkLocal = true;
	// set owner
	INetworkHelper* nh = obj->GetNetworkHelper();
	nh->srvObjParams_.CustomerID = profile_.CustomerID;
	nh->srvObjParams_.CharID = loadout_->LoadoutID;
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

#undef DEFINE_GAMEOBJ_PACKET_HANDLER_NON_CONST
#define DEFINE_GAMEOBJ_PACKET_HANDLER_NON_CONST(xxx) \
	case xxx: { \
		xxx##_s&n = *(xxx##_s*)packetData; \
		if(packetSize != sizeof(n)) { \
			r3dOutToLog("!!!!errror!!!! %s packetSize %d != %d\n", #xxx, packetSize, sizeof(n)); \
			return TRUE; \
										} \
		OnNetPacket(n); \
		return TRUE; \
	}

BOOL obj_ServerPlayer::OnNetReceive(DWORD EventID, const void* packetData, int packetSize)
{
	//@TODO somehow check that originator of that packet have playerIdx that match peer

	// packets that ignore packet sequence
	switch (EventID)
	{
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PacketBarrier);
	}

	if (myPacketSequence != clientPacketSequence)
	{
		// we get late packet after late packet barrier, skip it
		r3dOutToLog("peer%02d, CustomerID:%d LatePacket %d %s\n", peerId_, profile_.CustomerID, EventID, packetBarrierReason);
		return TRUE;
	}

	// no packets should be processed while we operating on global inventory
	if (inventoryOpActive_)
	{
		//r3dOutToLog("peer%02d, CustomerID:%d packet %d while inventory operation active\n", peerId_, profile_.CustomerID, EventID);
		return TRUE;
	}

	// packets while dead
	switch (EventID)
	{
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_MoveSetCell);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_MoveRel);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_MoveCameraLocation);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_DisconnectReq);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_PlayerWeapDataRepAns);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_SetRespawnData);//AlexRedd:: BR mode

		// AlexRedd:: packets while respawn			
		//DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_BackpackJoin);
		//DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_ShopBuyReq);
		//DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_FromInventoryReq);		
	}

	if (wasDisconnected_)
		return TRUE;

	if (loadout_->Alive == 0)
	{
		//r3dOutToLog("peer%02d, CustomerID:%d packet %d while dead\n", peerId_, profile_.CustomerID, EventID);
		return TRUE;
	}

	// packets while trade active
	switch (EventID)
	{
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_TradeRequest);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_TradeItem);
	}
	if (tradeTargetId > 0)
	{
		// do not disconnect, we can receive fire packets that was late
		gServerLogic.LogCheat(peerId_, PKT_S2C_CheatWarning_s::CHEAT_Trade, false, "Packet",
			"packet %d", EventID);
		return TRUE;
	}

	switch (EventID)
	{
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerJump);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerReload);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerReloaded);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_PlayerUnloadClip);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_OpenCase);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_PlayerCombineClip);
		DEFINE_GAMEOBJ_PACKET_HANDLER_NON_CONST(PKT_C2C_PlayerFired);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerHitNothing);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerHitStatic);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerHitStaticPierced);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerHitDynamic);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerHitResource);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerSwearing);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerReadyGrenade);
		DEFINE_GAMEOBJ_PACKET_HANDLER_NON_CONST(PKT_C2C_PlayerThrewGrenade);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_PlayerChangeBackpack);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_BackpackDrop);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_BackpackDropAll);//AlexRedd:: drop all		
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_BackpackSwap);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_BackpackJoin);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_BackpackDisassembleItem);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_ShopBuyReq);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_FromInventoryReq);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_ToInventoryReq);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_ChangeLoadoutReq);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_MoveAllItemsToGIReq);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_RepairItemReq);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_FallingDamage);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_PlayerEquipAttachment);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_PlayerRemoveAttachment);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerSwitchWeapon);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerSwitchFlashlight);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2C_PlayerUseItem);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_GroupInvitePlayer);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_GroupAcceptInvite);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_GroupLeaveGroup);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_GroupKickPlayer);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_CallForHelpReq);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_LockboxItemLockboxToBackpack);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_LockboxItemBackpackToLockbox);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_LockboxPickup);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_LearnRecipe);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_CraftItem);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_LearnSkinItem);//new add skin

		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_LearnGearSkinRecipe);//new add skin
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_GearSkinItem);//new add skin
#ifdef VEHICLES_ENABLED
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_VehicleRepair);
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_C2S_VehicleRefuel);
#endif
		DEFINE_GAMEOBJ_PACKET_HANDLER(PKT_S2C_UpdateWpnSkinSelected);//new add skin
	}

	return FALSE;
}
#undef DEFINE_GAMEOBJ_PACKET_HANDLER

DefaultPacket* obj_ServerPlayer::NetGetCreatePacket(int* out_size)
{
	static PKT_S2C_CreatePlayer_s n;

	r3d_assert(GetNetworkID());

	n.CustomerID = profile_.CustomerID ^ 0x54281162; // encode CustomerID so it won't look linear on client side
	n.playerIdx = (WORD)(GetNetworkID() - NETID_PLAYERS_START);
	n.spawnPos = GetPosition();
	n.moveCell = netMover.SrvGetCell();
	n.spawnDir = m_PlayerRotation;
	n.weapIndex = m_SelectedWeapon;
	n.isFlashlightOn = m_isFlashlightOn ? 1 : 0;
	n.PlayerState = m_PlayerState;
	if (r3dGetTime() < m_SpawnProtectedUntil)
		n.spawnProtection = m_SpawnProtectedUntil - r3dGetTime();
	else
		n.spawnProtection = 0.0f;

	// loadout part
	const wiCharDataFull& slot = *loadout_;
	n.HeroItemID = slot.HeroItemID;
	n.HeadIdx = (BYTE)slot.HeadIdx;
	n.BodyIdx = (BYTE)slot.BodyIdx;
	n.LegsIdx = (BYTE)slot.LegsIdx;
	n.WeaponID0 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].itemID;
	n.quantity0 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].quantity;
	n.Var10 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].Var1;
	n.Var20 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].Var2;
	n.Var30 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1].Var3;
	n.WeaponID1 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].itemID;
	n.quantity1 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].quantity;
	n.Var11 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].Var1;
	n.Var21 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].Var2;
	n.Var31 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2].Var3;
	n.QuickSlot1 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ITEM1].itemID;
	n.QuickSlot2 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ITEM2].itemID;
	n.QuickSlot3 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ITEM3].itemID;
	n.QuickSlot4 = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ITEM4].itemID;
	n.ArmorID = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_ARMOR].itemID;
	n.HeadGearID = loadout_->Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID;
	n.BackpackID = slot.BackpackID;
	n.Attm0 = GetWeaponNetAttachment(0);
	n.Attm1 = GetWeaponNetAttachment(1);

	r3dscpy(n.gamertag, slot.Gamertag);
	n.ClanID = slot.ClanID;
	r3dscpy(n.ClanTag, slot.ClanTag);
	n.ClanTagColor = slot.ClanTagColor;

	n.groupID = groupID;

#ifdef VEHICLES_ENABLED
	if (isInVehicle)
	{
		n.vehicleId = currentVehicleId;
		n.seatPosition = (BYTE)seatPosition;
	}
	else
		n.vehicleId = 0;
#endif
//new add skin
	for (int i = 0; i<MAX_WEAPONS_SKINEABLED; i++)
	{
		WeaponSkins& w = profile_.ProfileData.WpnSKIN[i];

		n.WpnSKIN[i] = w;
	}
//new add skin
	*out_size = sizeof(n);
	return &n;
}

void obj_ServerPlayer::RelayPacket(const DefaultPacket* packetData, int packetSize, bool guaranteedAndOrdered)
{
	gServerLogic.RelayPacket(peerId_, this, packetData, packetSize, guaranteedAndOrdered);
}

#ifdef VEHICLES_ENABLED
void obj_ServerPlayer::EnterVehicle(obj_Vehicle* vehicle)
{
	isInVehicle = true;
	m_SpawnProtectedUntil = 0;
	currentVehicleId = vehicle->vehicleId;
	currentVehicleType = vehicle->vehicleType;
	seatPosition = vehicle->AddPlayerToVehicle(this);

	if (seatPosition == 0)
		m_PlayerState = PLAYER_VEHICLE_PASSENGER;
	else
		m_PlayerState = PLAYER_VEHICLE_PASSENGER;
}

void obj_ServerPlayer::ExitVehicle(bool sendPacket, bool isForced, bool isDisconnecting)
{
	obj_Vehicle* vehicle = obj_Vehicle::GetVehicleById(currentVehicleId);
	if (!vehicle)
		return;

	bool isExitSuccessful = vehicle->HasSafeExit(seatPosition);
	if (!isExitSuccessful)
	{
		if (isForced || isDisconnecting)
			loadout_->Alive = 2;

		vehicle->safeExitPosition = r3dVector(vehicle->GetPosition().x, vehicle->GetPosition().y + 4.0f, vehicle->GetPosition().z);
		TeleportPlayer(vehicle->safeExitPosition);
		isExitSuccessful = true;
	}
	else if (isExitSuccessful && isDisconnecting)
	{
		TeleportPlayer(vehicle->safeExitPosition + r3dPoint3D(0, +1.0f, 0));
	}

	if (sendPacket)
	{
		PKT_S2C_VehicleExited_s packet;
		packet.isSuccess = isExitSuccessful;
		packet.vehicleId = toP2pNetId(vehicle->GetNetworkID());
		packet.isDisconnecting = isDisconnecting;
		packet.seat = (BYTE)seatPosition;

		if (packet.isSuccess)
		{
			packet.exitPosition = vehicle->safeExitPosition + r3dPoint3D(0, +1.0f, 0);
			gServerLogic.p2pBroadcastToActive(this, &packet, sizeof(packet), true);
		}
		else
		{
			if (isDisconnecting)
				gServerLogic.p2pBroadcastToActive(this, &packet, sizeof(packet), true);
			else // just the player needs to know of the failure
				gServerLogic.p2pSendToPeer(peerId_, this, &packet, sizeof(packet));
		}
	}

	if (isExitSuccessful || isForced)
	{
		vehicle->RemovePlayerFromVehicle(this);
		//if (vehicle->GetSpeed() > 0.0f)
		//ApplyDamage(vehicle->GetSpeed() * 0.8f, this, 2, storecat_Vehicle, GetNetworkID(), false);

		isInVehicle = false;
		currentVehicleId = -1;
		currentVehicleType = obj_Vehicle::VEHICLETYPE_INVALID;
		seatPosition = -1;

		m_PlayerState = PLAYER_IDLE;
	}
}

bool obj_ServerPlayer::IsInVehicle()
{
	return isInVehicle;
}

int obj_ServerPlayer::GetVehicleType()
{
	return currentVehicleType;
}
#endif
