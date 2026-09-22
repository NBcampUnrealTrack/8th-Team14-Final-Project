#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSStorePhaseTypes.h"
#include "CSEmployeeHiringWidget.generated.h"

class ACSStoreEmployeeCharacter;
class ACSStoreGameState;
class UCSEmployeeHiringSubsystem;
class UButton;
class UTextBlock;

USTRUCT(BlueprintType)
struct FCSEmployeeHiringViewData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText RoleName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Duties;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RequiredStoreLevel = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CurrentStoreLevel = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 HireCost = 2500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DailyWage = 50;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TotalDailyWage = 70;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CurrentFunds = 4000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StartBusinessDay = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bStartsToday = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCanHire = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText UnavailableReason;
};

// 레이아웃과 스타일은 자식 WBP에서 편집합니다. C++은 데이터와 버튼 동작만 연결합니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSEmployeeHiringWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UCSEmployeeHiringWidget(const FObjectInitializer& ObjectInitializer);
	UFUNCTION(BlueprintCallable, Category = "Employee|Hiring") void SetApplicant(ACSStoreEmployeeCharacter* InApplicant);
	UFUNCTION(BlueprintCallable, Category = "Employee|Hiring") void Close();
	UFUNCTION(BlueprintCallable, Category = "Employee|Hiring") bool ConfirmHire();
	UFUNCTION(BlueprintCallable, Category = "Employee|Hiring") void RefreshView();
	UPROPERTY(BlueprintReadOnly, Category = "Employee|Hiring") FCSEmployeeHiringViewData ViewData;
protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	// 디자이너에서 잔액 부족, 당일/다음 날 출근 상태를 확인할 샘플입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Employee|Preview") FCSEmployeeHiringViewData PreviewData;
	// 아래 버튼은 이름과 타입만 맞추면 연결됩니다. BP에서 OnClicked를 중복 연결하지 않습니다.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CancelButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> RoleNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DutiesText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HireCostText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailyWageText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TotalDailyWageText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CurrentFundsText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StartDayText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> AvailabilityText;
	// 커스텀 UI/애니메이션은 이 이벤트에서 ViewData를 읽습니다. 디자이너에서도 호출됩니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Employee|Hiring") void OnHiringViewUpdated();
private:
	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleFundsChanged(int32 NewFunds);
	UFUNCTION() void HandlePhaseChanged(ECStorePhase NewPhase);
	UFUNCTION() void HandleBusinessDayChanged(int32 NewDay);
	UFUNCTION() void HandleEmployeeHired();
	UPROPERTY(Transient) TWeakObjectPtr<ACSStoreEmployeeCharacter> Applicant;
	UPROPERTY(Transient) TObjectPtr<ACSStoreGameState> StoreState;
	UPROPERTY(Transient) TObjectPtr<UCSEmployeeHiringSubsystem> HiringSubsystem;
	FText PaymentError;
	bool bViewConstructed = false;
};
