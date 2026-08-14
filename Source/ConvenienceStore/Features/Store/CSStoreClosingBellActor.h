#pragma once
#include "CoreMinimal.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSStoreClosingBellActor.generated.h"
class UStaticMeshComponent;
class ACSStoreGameState;
class UCSStoreClosingBellConfirmWidget;
// 통계 확인 후 마감 활동을 끝내고 다음 날을 시작하는 레벨 배치용 종입니다.
UCLASS()
class CONVENIENCESTORE_API ACSStoreClosingBellActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()
public:
	ACSStoreClosingBellActor();
	virtual void BeginPlay() override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> BellMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Closing") TSubclassOf<UCSStoreClosingBellConfirmWidget> ConfirmWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UCSStoreClosingBellConfirmWidget> ActiveConfirmWidget;
	UPROPERTY(Transient) TObjectPtr<ACSStoreGameState> StoreGameState;
	UFUNCTION() void HandleConfirmed();
	UFUNCTION() void HandleCancelled();
	void CloseConfirmWidget();
};
