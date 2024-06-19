#pragma once

#include "../EclipseStudio/Sources/ObjectsCode/weapons/GearConfig.h"

class BackPack
{
	friend class WeaponArmory;
public:
	BackPack(const BackpackConfig* conf);
	~BackPack();

	void Reset();

	const BackpackConfig* getConfig() const { return m_pConfig; }
	STORE_CATEGORIES getCategory() const { return m_pConfig->category; }

private:
	const BackpackConfig* m_pConfig;
};
