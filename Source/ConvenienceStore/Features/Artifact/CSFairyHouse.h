#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "Data/CSFairyDialogueTypes.h"
#include "Components/SlateWrapperTypes.h"
#include "CSFairyHouse.generated.h"

class UArrowComponent;
class UStaticMeshComponent;
class UBoxComponent;
class ACSCleaningFairy;
class UWidgetComponent;
class UCameraComponent;
class ACSStorePlayerController;
class UCSFairyDialogueWidget;
class UUserWidget;

UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSFairyHouse : public AActor, public ICSInteractable
{
	GENERATED_BODY()
public:
	ACSFairyHouse();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	bool BeginContractIntroduction(ACSStorePlayerController* Controller);
	bool ChooseRandomDialogue(ECSFairyDialogueGroup Group, FCSFairyDialogueRow& OutLine);
	UFUNCTION() void RefreshContractState();
	UFUNCTION(BlueprintCallable, Category="Fairy|Introduction") void FinishIntroduction();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy|Interaction") TObjectPtr<UBoxComponent> InteractionBox;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy|Dialogue") TObjectPtr<UWidgetComponent> SpeechBubble;
	// 집과 상호작용할 때 사용할 말풍선 WBP를 지정해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Dialogue") TSubclassOf<UCSFairyDialogueWidget> SpeechBubbleWidgetClass;
	// 화면 전체의 Canvas Panel을 기준으로 컷씬 대화 상자의 위치와 크기를 디자인해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Introduction") TSubclassOf<UCSFairyDialogueWidget> IntroductionDialogueWidgetClass;
	// 집과 요정이 화면에 들어오도록 이 카메라의 상대 위치를 조절해 주세요.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy|Introduction") TObjectPtr<UCameraComponent> IntroductionCamera;
	// 대사는 이 테이블에서만 관리합니다. 추가할 대사의 그룹과 표시 시간을 지정해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Dialogue", meta=(RowType="/Script/ConvenienceStore.CSFairyDialogueRow")) TObjectPtr<UDataTable> DialogueTable;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Introduction", meta=(ClampMin="0.1")) float HouseShotDuration = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Introduction", meta=(ClampMin="0.1")) float EmergenceDuration = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Introduction", meta=(ClampMin="0.1")) float CloseupDuration = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Introduction", meta=(ClampMin="10", ClampMax="90")) float CloseupFieldOfView = 30.f;
	// 집의 크기와 관계없이 요정에서 카메라까지 유지할 월드 거리(cm)를 지정해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Introduction", meta=(ClampMin="30")) float CloseupDistance = 170.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Introduction", meta=(ClampMin="1")) float TypewriterCharactersPerSecond = 30.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<UArrowComponent> DoorPoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<UArrowComponent> FlightPoint;
	// BP_StoreInterior가 없는 레벨에서만 사용합니다. 매장 내부에 맞춰 범위와 위치를 조절해 주세요.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<UBoxComponent> CleaningArea;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy") TSubclassOf<ACSCleaningFairy> FairyClass;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Fairy") TObjectPtr<ACSCleaningFairy> ActiveFairy;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy House")
	TObjectPtr<UStaticMeshComponent> HouseMesh;
private:
	bool SpawnFairy();
	void ShowRandomDialogue(ECSFairyDialogueGroup Group);
	void HideSpeechBubble();
	void HideOtherUI();
	void RestoreOtherUI();
	UFUNCTION() void AdvanceIntroductionDialogue();
	TArray<FCSFairyDialogueRow> GetDialogues(ECSFairyDialogueGroup Group) const;
	UPROPERTY(Transient) TObjectPtr<UCSFairyDialogueWidget> BubbleWidget;
	UPROPERTY(Transient) TObjectPtr<UCSFairyDialogueWidget> IntroductionWidget;
	TWeakObjectPtr<ACSStorePlayerController> IntroductionController;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	TMap<ECSFairyDialogueGroup, int32> LastDialogueIndices;
	TArray<FCSFairyDialogueRow> IntroductionLines;
	TMap<TWeakObjectPtr<UUserWidget>, ESlateVisibility> HiddenUIVisibilities;
	FTransform CameraRestTransform;
	FTransform CloseupStartTransform;
	FVector CloseupFocus = FVector::ZeroVector;
	FVector CloseupEndLocation = FVector::ZeroVector;
	bool bCloseupStarted = false;
	float CameraRestFOV = 60.f;
	float IntroductionElapsed = 0.f;
	int32 IntroductionLineIndex = INDEX_NONE;
	double LastRealTime = 0.;
	double BubbleUntil = 0.;
	bool bIntroducing = false;
	bool bPausedByIntroduction = false;
	bool bPreviousFullTickWhenPaused = false;
};

// 매장에 하나만 배치해 주세요. 화살표의 월드 변환을 요정의 집 설치 위치로 사용합니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSFairyHousePlacementPoint : public AActor
{
	GENERATED_BODY()
public:
	ACSFairyHousePlacementPoint();
	// 블루프린트에서 이 컴포넌트의 위치, 회전과 크기를 조절해 주세요.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy House")
	TObjectPtr<UArrowComponent> PlacementPoint;
};
