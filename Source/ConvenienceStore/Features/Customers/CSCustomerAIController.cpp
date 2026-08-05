// Fill out your copyright notice in the Description page of Project Settings.


#include "Features/Customers/CSCustomerAIController.h"
#include "Components/StateTreeAIComponent.h"

ACSCustomerAIController::ACSCustomerAIController()
{
	// BP로 승격시켜서 Start Logic Automatically를 체크해서 폰에 Possess되는 즉시 트리가 돌게 한다.
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
}

UStateTreeAIComponent* ACSCustomerAIController::GetStateTreeAI() const
{
	return StateTreeAI;
}
