#include "r3dPCH.h"
#include "r3d.h"

#include "GameCommon.h"
#include "ServerBackpack.h"

BackPack::BackPack(const BackpackConfig* conf) : m_pConfig(conf)
{
	Reset();
}

BackPack::~BackPack()
{
}

void BackPack::Reset()
{
}
