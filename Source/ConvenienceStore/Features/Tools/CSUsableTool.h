#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSUsableTool.generated.h"

UINTERFACE(BlueprintType)
class CONVENIENCESTORE_API UCSUsableTool : public UInterface
{
	GENERATED_BODY()
};

// 플레이어가 장착한 도구의 공통 사용 입력을 처리하기 위한 인터페이스입니다.
class CONVENIENCESTORE_API ICSUsableTool
{
	GENERATED_BODY()

public:
	// 좌클릭 등 주 사용 입력이 들어왔을 때 도구 고유 동작을 실행합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tool")
	bool UsePrimary(AActor* User);
};
