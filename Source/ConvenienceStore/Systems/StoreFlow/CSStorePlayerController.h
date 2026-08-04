#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Data/CSStorePhaseTypes.h"
#include "CSStorePlayerController.generated.h"

class ACSStoreGameState;
class UCSStoreHUDWidget;

// 로컬 플레이어의 편의점 HUD와 마감 UI 입력 상태를 관리합니다.
UCLASS()
class CONVENIENCESTORE_API ACSStorePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ToggleClock();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask);

	UFUNCTION()
	void HandleStorePhaseChanged(ECStorePhase NewPhase);

	void UpdateClosingInputMode();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UCSStoreHUDWidget> StoreHUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UCSStoreHUDWidget> StoreHUDWidget;

	TObjectPtr<ACSStoreGameState> StoreGameState;
};
