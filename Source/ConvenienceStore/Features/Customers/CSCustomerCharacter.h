// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Features/Tools/CSHitReactable.h"
#include "GameFramework/Character.h"
#include "CSCustomerCharacter.generated.h"

class UCSCustomerStateComponent;
class UArrowComponent;
class USceneComponent;
class ACSCustomerBasketActor;
class ACSCustomerShoppingBagActor;
class UCSPurchaseDecisionComponent;
class UPrimitiveComponent;
class UCSInnerThoughtWidget;
class UStaticMeshComponent;
class UCSCustomerVisualDataAsset;
class UAnimSequence;
struct FCSSelectProduct;

UCLASS()
class CONVENIENCESTORE_API ACSCustomerCharacter : public ACharacter, public ICSHitReactable
{
	GENERATED_BODY()

public:
	ACSCustomerCharacter();
	void RefreshArtifactMoveSpeed();

	UCSCustomerStateComponent* GetCustomerState() const;

	// 입장이 확정됐거나 상품을 처음 담을 때 장바구니가 없으면 만든다. 여러 번 불러도 안전
	void EnsureBasketSpawned();

	// 고객이 상품을 선택했을 때 장바구니에 표시용 상품을 추가합니다.
	void AddBasketDisplayProduct(const FCSSelectProduct& SelectedProduct);
	// 결제, 반품, 이탈 시 장바구니에 표시된 상품을 정리합니다.
	void ClearBasketDisplayProducts();
	// 계산이 시작되면 기존 장바구니와 내부 상품을 숨깁니다.
	void SetBasketDisplayVisible(bool bShouldBeVisible);
	// 결제가 확정된 고객에게 장바구니 대신 봉투를 보여 줍니다.
	void ShowShoppingBag();

	// 마법 투사체에 맞았을 때 공격이 진행한 방향으로 고객을 밀어냅니다.
	virtual void ApplyMagicHit_Implementation(
		FVector HitLocation,
		FVector ImpactDirection,
		float KnockbackStrength) override;

	// TODO(제압 연출): 랙돌/몬타주 작업 시 주석 해제.
	//UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Customer|Subdue")
	//void BeginSubduedReaction(FVector ImpactDirection, float KnockbackStrength);
	//virtual void BeginSubduedReaction_Implementation(FVector ImpactDirection, float KnockbackStrength);

	// TODO(제압 연출): 소멸 직전 1회. 랙돌 정리나 사라지는 연출을 붙일 자리
	//UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Customer|Subdue")
	//void EndSubduedReaction();
	//virtual void EndSubduedReaction_Implementation();


	// 계산을 포기했을 때 상품을 내려놓기 시작할 지점 SideIndex: 0 = 왼쪽, 1 = 오른쪽
	const UArrowComponent* GetDropOrigin(const int32 SideIndex) const;

	// 절도를 시작한 빌런을 추적할 수 있게 빨간 외곽선을 켜고 끕니다.
	// 켜는 곳: CSCustomerRuntimeSubsystem::CommitShoplift()
	// 끄는 곳: CSCustomerRuntimeSubsystem::HandleMagicHitOnCustomer() (제압 성공)
	void SetVillainHighlight(bool bEnabled);

	// AnimBlueprint가 없는 종족의 Idle/Walk 및 일회성 상품 행동 애니메이션을 초기화합니다.
	void InitializeFallbackAnimations(const UCSCustomerVisualDataAsset* Visual);
	bool PlayThinkingAnimation();
	bool PlayPickAnimation();
	bool IsActionAnimationPlaying() const;
	void CancelActionAnimation();
	void ReleaseActionTransitionHold();

	// exclamation_point 메시를 손님 머리 위에 Duration초 동안 띄움 - DrawDebugString 자리를 대체하는
	// 실제 표시 경로. bShowIcon은 Tier.Icon != nullptr을 그대로 넘겨받는 표시 여부 스위치일 뿐,
	// 값 자체(어떤 텍스처인지)는 안 씀 - 아이콘이 exclamation_point 메시 하나로 통일됐기 때문
	void ShowHeadIcon(bool bShowIcon, float Duration);
	// 표시 중인 머리 위 아이콘을 즉시 숨김
	void HideHeadIcon();

protected:
	float LastArtifactSpeedMultiplier = 1.0f;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 파괴될 때 AIController도 같이 정리
	virtual void Destroyed() override;

	//이 손님 개인의 상태(고른 상품, 방문한 진열대, 계산 완료 여부, 퇴장 지점)를 담고 있는 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TObjectPtr<UCSCustomerStateComponent> CustomerState;

	// [백록담님 확인해야하는 상황] C++ 생성자에서 만드는 이유
	//   스포너가 SpawnActorDeferred로 BeginPlay를 미룬 뒤 초기값을 주입하는데,
	//   BP 컴포넌트는 그 보류 구간에 아직 존재하지 않는다.
	//   BP에 두면 Archetype / 시드 / 빌런 배정이 통째로 건너뛰어진다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TObjectPtr<UCSPurchaseDecisionComponent> PurchaseDecision;

	// 장바구니를 손 위치에 붙이기 위한 기준 컴포넌트입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Basket")
	TObjectPtr<USceneComponent> BasketAnchor;

	// 장바구니 메시와 표시 슬롯을 구성한 블루프린트 클래스를 지정합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|Basket")
	TSubclassOf<ACSCustomerBasketActor> BasketActorClass;

	UPROPERTY(Transient)
	TObjectPtr<ACSCustomerBasketActor> BasketActor;

	// 오른손 Bone을 따라가는 봉투 전용 기준점입니다. BP에서 상대 위치와 회전을 조정합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Shopping Bag")
	TObjectPtr<USceneComponent> ShoppingBagAnchor;

	// 계산 후 고객이 들고 나갈 내용물이 보이지 않는 봉투 블루프린트입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|Shopping Bag")
	TSubclassOf<ACSCustomerShoppingBagActor> ShoppingBagActorClass;

	UPROPERTY(Transient)
	TObjectPtr<ACSCustomerShoppingBagActor> ShoppingBagActor;

	//떨어질 위치 화살표 컴포넌트 (왼쪽)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Drop")
	TObjectPtr<UArrowComponent> DropOriginLeft;
	//떨어질 위치 화살표 컴포넌트 (오른쪽)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Drop")
	TObjectPtr<UArrowComponent> DropOriginRight;

	// 넉백 시 약간 위로 뜨도록 더하는 수직 속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|Magic Hit",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MagicKnockbackUpwardStrength = 250.0f;

	// Post Process 외곽선 머티리얼에서 빨강으로 설정하여 사용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|Villain",
		meta = (ClampMin = "0"))
	int32 VillainOutlineStencilValue = 4;

	// 속마음 위젯
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|InnerThought")
	TSubclassOf<UCSInnerThoughtWidget> InnerThoughtWidgetClass;

	// 구매 거절/가격 반응 시 머리 위에 뜨는 아이콘 메시 컴포넌트 (기존 DrawDebugString 자리 대체)
	// exclamation_point 스태틱 메시로 통일
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|HeadIcon")
	TObjectPtr<UStaticMeshComponent> HeadIconMeshComponent;

	// Thinking/Pick 방향 보정 중 Mesh를 월드 Z축으로 이동할 거리입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|Animation")
	float ActionMeshWorldZOffset = -30.0f;

	// Thinking/Pick의 첫 T-Pose 프레임을 건너뛰기 위한 공통 시작 시간입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|Animation",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ActionAnimationStartOffsetSeconds = 0.04f;

	// Visual crossfade only; does not change action completion or AI timing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|Animation",
		meta = (ClampMin = "0.0", ClampMax = "0.5", Units = "s"))
	float AnimationTransitionBlendSeconds = 0.18f;

private:
	// UCSCustomerStateComponent::OnEnteredStore 구독 함수. 입장이 확정되면 장바구니를 만든다
	void HandleEnteredStore(UCSCustomerStateComponent* State);
	FDelegateHandle EnteredStoreHandle;

	void UpdateLocomotionAnimation(bool bForce = false);
	void RefreshLocomotionAnimationPose();
	bool PlayActionAnimation(UAnimSequence* Animation, bool bHoldTransformAfterFinish);
	void HandleActionAnimationFinished();
	void ApplyActionMeshRotation();
	void RestoreActionMeshRotation();

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FallbackIdleAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FallbackWalkAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FallbackThinkingAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FallbackPickAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentFallbackLoopAnimation;

	FTimerHandle ActionAnimationTimerHandle;
	// ShowHeadIcon()이 예약한 자동 숨김 타이머
	FTimerHandle HeadIconHideTimerHandle;
	bool bFallbackAnimationsInitialized = false;
	float ShoppingBagScaleMultiplier = 1.0f;
	bool bFallbackWalking = false;
	bool bActionAnimationPlaying = false;
	bool bHoldActionTransformOnFinish = false;
	bool bActionTransitionHoldActive = false;
	FTransform SavedActionMeshRelativeTransform = FTransform::Identity;
	FTransform SavedActionBasketAnchorRelativeTransform = FTransform::Identity;
	bool bActionMeshRotationApplied = false;
	bool bActionBasketAnchorTransformCached = false;

	// 외곽선을 켜기 전 원래 상태를 기억해뒀다가 끌 때 복원
	TMap<TObjectPtr<UPrimitiveComponent>, bool> OriginalCustomDepthStates;
	TMap<TObjectPtr<UPrimitiveComponent>, int32> OriginalCustomStencilValues;

};
