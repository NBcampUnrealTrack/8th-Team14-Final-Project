#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSLedgerTypes.h"
#include "CSStoreLedgerWidget.generated.h"

class UCSLedgerProductRowWidget;
class UCSLedgerLeftPageWidget;
class UCSLedgerRightPageWidget;
class UCSProductVisualRegistry;
class UDataTable;
class UScrollBox;

// 월드의 장부 액터가 카메라와 입력을 복원할 수 있도록 닫기 의도만 전달합니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnStoreLedgerCloseRequested);

UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSStoreLedgerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Store|Ledger")
	void OpenLedger();

	UFUNCTION(BlueprintCallable, Category = "Store|Ledger")
	void SelectCategory(ECSLedgerCategoryFilter NewCategory);

	UFUNCTION(BlueprintCallable, Category = "Store|Ledger")
	void SelectProduct(const FDataTableRowHandle& ProductRow);

	UFUNCTION(BlueprintCallable, Category = "Store|Ledger|Pricing")
	void ChangePendingSalePrice(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Store|Ledger|Pricing")
	void SetPendingSalePrice(int32 NewSalePrice);

	UFUNCTION(BlueprintCallable, Category = "Store|Ledger")
	void RequestClose();

	// 자식 오른쪽 페이지의 닫기 이벤트와 구분되는 이름으로 액터 BP의 잘못된 바인딩을 방지합니다.
	UPROPERTY(BlueprintAssignable, Category = "Store|Ledger")
	FCSOnStoreLedgerCloseRequested OnLedgerCloseRequested;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 상품의 정적 정보는 기존 마스터 테이블을 그대로 사용하고 장부 전용 데이터를 중복 생성하지 않습니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Data")
	TObjectPtr<UDataTable> ProductDataTable;

	// 상품 아이콘은 기존 비주얼 레지스트리에서 조회하여 게임 월드와 장부가 같은 이미지를 공유합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Data")
	TObjectPtr<UCSProductVisualRegistry> ProductVisualRegistry;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Rows")
	TSubclassOf<UCSLedgerProductRowWidget> ProductRowWidgetClass;

	// 통합 WBP는 두 페이지를 조립하고 메인 C++ 클래스는 페이지 사이의 상태 전달만 조율합니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCSLedgerLeftPageWidget> LeftPage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCSLedgerRightPageWidget> RightPage;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Ledger")
	ECSLedgerCategoryFilter SelectedCategory = ECSLedgerCategoryFilter::All;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Ledger")
	ECSLedgerDetailState DetailState = ECSLedgerDetailState::NoSelection;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Ledger")
	FCSLedgerProductViewData SelectedProduct;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Ledger|Pricing")
	int32 PendingSalePrice = 0;

private:
	void RebuildProductList();
	bool BuildProductViewData(FName ProductId, FCSLedgerProductViewData& OutData) const;
	bool MatchesSelectedCategory(ECSProductCategory ProductCategory) const;
	void ClearSelection();

	UFUNCTION() void HandleCategorySelected(ECSLedgerCategoryFilter Category);
	UFUNCTION() void HandleProductRowSelected(FDataTableRowHandle ProductRow);
	UFUNCTION() void HandleSalePriceEdited(int32 NewSalePrice);
	UFUNCTION() void HandleRightPageCloseRequested();
};
