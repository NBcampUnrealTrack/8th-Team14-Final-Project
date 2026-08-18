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
struct FCSSelectProduct;

UCLASS()
class CONVENIENCESTORE_API ACSCustomerCharacter : public ACharacter, public ICSHitReactable
{
	GENERATED_BODY()

public:
	ACSCustomerCharacter();

	UCSCustomerStateComponent* GetCustomerState() const;

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


	// 계산을 포기했을 때 상품을 내려놓기 시작할 지점 SideIndex: 0 = 왼쪽, 1 = 오른쪽
	const UArrowComponent* GetDropOrigin(const int32 SideIndex) const;

protected:
	virtual void BeginPlay() override;

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

};
