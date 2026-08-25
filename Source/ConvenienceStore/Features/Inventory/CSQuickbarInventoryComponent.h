#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSQuickbarInventoryComponent.generated.h"

class ACSUsableToolActor;

// 퀵바 슬롯 하나에 보관되는 도구입니다.
USTRUCT(BlueprintType)
struct FCSQuickbarSlot
{
	GENERATED_BODY()

	// 현재 슬롯에 보관 중인 도구 액터입니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Quickbar")
	TObjectPtr<ACSUsableToolActor> ToolActor = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnQuickbarChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnQuickbarSelectionChanged, int32, SelectedSlotIndex);

// 플레이어가 보관하는 장착형 도구 전용 퀵바입니다.
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class CONVENIENCESTORE_API UCSQuickbarInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSQuickbarInventoryComponent();

	// 슬롯 수를 기준으로 비어 있는 퀵바 배열을 준비합니다.
	void InitializeQuickbar();
	// 첫 빈 칸에 도구를 보관하고, 성공한 슬롯 번호를 반환합니다.
	int32 TryAddTool(ACSUsableToolActor* ToolActor);
	// 도구를 퀵바에서 제거합니다.
	bool RemoveTool(ACSUsableToolActor* ToolActor);
	// 지정한 슬롯을 현재 장착 대상으로 선택합니다.
	bool SelectSlot(int32 SlotIndex);
	// 마우스 휠용으로 다음 또는 이전 슬롯을 순환 선택합니다.
	bool CycleSelection(int32 Direction);

	UFUNCTION(BlueprintPure, Category = "Quickbar")
	int32 GetSlotCount() const { return Slots.Num(); }
	UFUNCTION(BlueprintPure, Category = "Quickbar")
	int32 GetSelectedSlotIndex() const { return SelectedSlotIndex; }
	UFUNCTION(BlueprintPure, Category = "Quickbar")
	ACSUsableToolActor* GetToolAt(int32 SlotIndex) const;
	UFUNCTION(BlueprintPure, Category = "Quickbar")
	bool ContainsTool(const ACSUsableToolActor* ToolActor) const;

	UPROPERTY(BlueprintAssignable, Category = "Quickbar")
	FCSOnQuickbarChanged OnQuickbarChanged;
	UPROPERTY(BlueprintAssignable, Category = "Quickbar")
	FCSOnQuickbarSelectionChanged OnQuickbarSelectionChanged;

protected:
	// 현재는 2칸으로 시작하며, 이후 이 값만 늘리면 퀵바가 확장됩니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quickbar", meta = (ClampMin = "1", UIMin = "1"))
	int32 InventorySlotCount = 2;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Quickbar")
	TArray<FCSQuickbarSlot> Slots;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Quickbar")
	int32 SelectedSlotIndex = 0;
};
