#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/DataTable.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSShelfConfigurationWidget.generated.h"

class ACSShelfActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnShelfConfigurationClosed);

// 진열대의 상·중·하단 품목을 관리하는 UMG 블루프린트의 공통 기반입니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCShelfConfigurationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 진열대가 UI를 열 때 대상과 현재 단별 설정을 전달합니다.
	UFUNCTION(BlueprintCallable, Category = "Shelf|Configuration")
	void SetShelfTarget(ACSShelfActor* InShelf);

	UFUNCTION(BlueprintPure, Category = "Shelf|Configuration")
	ACSShelfActor* GetShelfTarget() const { return ShelfTarget.Get(); }

	// UI의 단 카드가 현재 설정을 새로 그릴 수 있도록 방송합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Shelf|Configuration")
	void RefreshConfigurationView();

	// 상품 변경 결과를 UI에 표시합니다. 기존 상품이 있으면 직원 정리 대기 수가 함께 전달됩니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Shelf|Configuration")
	void ShowConfigurationStatus(const FText& Message, int32 PendingReorganizationCount);

	UFUNCTION(BlueprintPure, Category = "Shelf|Configuration")
	FText GetLastConfigurationStatus() const { return LastConfigurationStatus; }

	// 상품 선택 버튼에서 호출하면 해당 단을 플레이어 지정 상품 상태로 바꿉니다.
	UFUNCTION(BlueprintCallable, Category = "Shelf|Configuration")
	bool ConfigureLevelProduct(ECSShelfSlotLevel SlotLevel, FDataTableRowHandle ProductRow);

	// 설정 해제 버튼에서 호출하면 해당 단을 다시 자동 배치 대상으로 돌립니다.
	UFUNCTION(BlueprintCallable, Category = "Shelf|Configuration")
	bool ClearLevelProduct(ECSShelfSlotLevel SlotLevel);

	// 닫기 버튼에서 호출하면 진열대가 입력 모드를 게임 조작으로 복원합니다.
	UFUNCTION(BlueprintCallable, Category = "Shelf|Configuration")
	void CloseConfiguration();

	UPROPERTY(BlueprintAssignable, Category = "Shelf|Configuration")
	FCSOnShelfConfigurationClosed OnConfigurationClosed;

private:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Shelf|Configuration", meta = (AllowPrivateAccess = "true"))
	FText LastConfigurationStatus;

	UPROPERTY(Transient)
	TWeakObjectPtr<ACSShelfActor> ShelfTarget;
};
