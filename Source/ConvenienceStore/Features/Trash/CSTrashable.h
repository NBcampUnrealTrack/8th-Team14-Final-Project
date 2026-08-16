#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSTrashable.generated.h"

UINTERFACE(BlueprintType)
class CONVENIENCESTORE_API UCSTrashable : public UInterface
{
	GENERATED_BODY()
};

// 쓰레기통에 폐기할 수 있는 Actor임을 나타내는 Marker Interface입니다.
class CONVENIENCESTORE_API ICSTrashable
{
	GENERATED_BODY()
};
