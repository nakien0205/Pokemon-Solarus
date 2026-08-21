#pragma once

#include "CoreMinimal.h"

/** Physical, Special, or non-damaging Status move behavior. */
enum class EBattleMoveCategory : uint8
{
	Physical = 0,
	Special = 1,
	Status = 2,
	Invalid = 255
};
