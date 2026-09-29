#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "CSStorePricingSubsystem.generated.h"

class UCSSaveGame;

// 상품 판매가가 실제로 변경(저장)에 성공했을 때만 방송됨
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnSalePriceChanged,
	FName,
	ProductRowName,
	int32,
	NewSalePrice);

UCLASS()
class CONVENIENCESTORE_API UCSStorePricingSubsystem : public UWorldSubsystem,
	public ICSSaveParticipant
{
	GENERATED_BODY()

public:
	// ICSSaveParticipant
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;

	UFUNCTION(BlueprintPure, Category = "Store|Pricing")
	int32 GetSalePrice(const FDataTableRowHandle& ProductRow) const;

	// 영업 중에는 가격을 못 바꾸게 막음 - 오늘 판매 결과를 보고 마감 후에 정하는 흐름이라
	// 영업 중 변경을 허용하면 그 전제가 깨짐. 막힌 이유는 OutReason으로 돌려줘 UI가 그대로 띄울 수 있게 함
	UFUNCTION(BlueprintPure, Category = "Store|Pricing")
	bool CanChangeSalePrice(FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "Store|Pricing")
	bool SetSalePrice(const FDataTableRowHandle& ProductRow, int32 NewSalePrice);

	// 판매가가 성공적으로 변경될 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "Store|Pricing")
	FCSOnSalePriceChanged OnSalePriceChanged;

private:
	UPROPERTY(Transient)
	TMap<FName, int32> SalePriceOverrides;
};
