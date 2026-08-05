// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CSCustomerCharacter.generated.h"

class UCSCustomerStateComponent;

UCLASS()
class CONVENIENCESTORE_API ACSCustomerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACSCustomerCharacter();

	UCSCustomerStateComponent* GetCustomerState() const;

protected:

	// 파괴될 때 AIController도 같이 정리
	virtual void Destroyed() override;

	//이 손님 개인의 상태(고른 상품, 방문한 진열대, 계산 완료 여부, 퇴장 지점)를 담고 있는 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TObjectPtr<UCSCustomerStateComponent> CustomerState;

};
