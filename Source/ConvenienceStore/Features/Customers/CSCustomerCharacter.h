// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CSCustomerCharacter.generated.h"

class UCSCustomerStateComponent;
class UArrowComponent;
class USceneComponent;
class ACSCustomerBasketActor;
class ACSCustomerShoppingBagActor;
struct FCSSelectProduct;

UCLASS()
class CONVENIENCESTORE_API ACSCustomerCharacter : public ACharacter
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


	// 계산을 포기했을 때 상품을 내려놓기 시작할 지점 SideIndex: 0 = 왼쪽, 1 = 오른쪽
	const UArrowComponent* GetDropOrigin(const int32 SideIndex) const;

protected:
	virtual void BeginPlay() override;

	// 파괴될 때 AIController도 같이 정리
	virtual void Destroyed() override;

	//이 손님 개인의 상태(고른 상품, 방문한 진열대, 계산 완료 여부, 퇴장 지점)를 담고 있는 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TObjectPtr<UCSCustomerStateComponent> CustomerState;

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

};
