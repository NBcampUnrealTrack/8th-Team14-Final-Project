#pragma once
#include "CoreMinimal.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSStoreClosingBellActor.generated.h"
class UStaticMeshComponent;
class ACSStoreGameState;
class UCSStoreClosingBellConfirmWidget;
class ACSStorePlayerController;
class ACSPlayerWakePoint;
class UCSStoreSleepTransitionWidget;
class UCSWakeUpWidget;
// 검은 화면 동안 기존 정산/다음 날 처리를 실행하는 레벨 배치용 종입니다.
UCLASS()
class CONVENIENCESTORE_API ACSStoreClosingBellActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()
public:
	ACSStoreClosingBellActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> BellMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Closing") TSubclassOf<UCSStoreClosingBellConfirmWidget> ConfirmWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UCSStoreClosingBellConfirmWidget> ActiveConfirmWidget;
	UPROPERTY(Transient) TObjectPtr<ACSStoreGameState> StoreGameState;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Store|Sleep", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<ACSPlayerWakePoint> WakePoint;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Sleep", meta = (ClampMin = "0.1"))
	float SleepFadeDuration = 0.75f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Sleep", meta = (ClampMin = "0.01"))
	float SleepBlackHoldDuration = 0.15f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Sleep")
	TSubclassOf<UCSStoreSleepTransitionWidget> SleepTransitionWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSStoreSleepTransitionWidget> ActiveSleepTransitionWidget;
	// CSWakeUpWidget 기반 위젯을 지정하시고 해당 위젯의 시간과 곡선으로 기상 연출을 조정해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Sleep|Wake Up")
	bool bUseEyeOpening = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Sleep|Wake Up")
	TSubclassOf<UCSWakeUpWidget> WakeUpWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSWakeUpWidget> ActiveWakeUpWidget;
	TWeakObjectPtr<ACSStorePlayerController> SleepingController;
	FTimerHandle SleepTimer;
	int32 DayBeforeSleep = INDEX_NONE;
	UFUNCTION() void HandleConfirmed();
	UFUNCTION() void HandleCancelled();
	void CloseConfirmWidget();
	void AdvanceDayWhileBlack();
	UFUNCTION() void HandleSleepNextDayRequested();
	UFUNCTION() void HandleSleepSummaryRequested();
	void CloseSleepTransitionWidget();
	void WakeAndFadeIn();
	UFUNCTION() void FinishSleep();
};
