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

	// 진열할 빈 슬롯이 없을 때 HUD에 빨간 배치 불가 안내를 표시합니다.
	void ShowPlacementUnavailablePrompt();

	// 조준한 대상의 상호작용 안내를 HUD에 전달합니다.
	void SetInteractionPrompt(const FText& Message, const FLinearColor& TextColor);

	// 조준한 상품의 판매가를 HUD에 전달합니다.
	void SetProductPrice(const FText& Message);

	// 진열 보관 환경이 맞지 않을 때 HUD에 경고를 전달합니다.
	void SetStorageWarning(const FText& Message);

	// 배치 대상이 바뀌거나 상품을 내려놓았을 때 HUD의 조준 안내를 지웁니다.
	void ClearInteractionContext();

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
