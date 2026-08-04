#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSInteractable.generated.h"

UINTERFACE(BlueprintType)
class CONVENIENCESTORE_API UCSInteractable : public UInterface
{
	GENERATED_BODY()
};

class CONVENIENCESTORE_API ICSInteractable
{
	GENERATED_BODY()

public:
	// 플레이어 등이 상호작용을 요청할 때 실행합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void Interact(AActor* Interactor);

	// 화면에 표시할 상호작용 안내 문구를 반환합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FText GetInteractionText() const;
};
