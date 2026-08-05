// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CSCustomerAIController.generated.h"

class UStateTreeAIComponent;
/**
 *
 */
UCLASS()
class CONVENIENCESTORE_API ACSCustomerAIController : public AAIController
{
	GENERATED_BODY()
public:
	ACSCustomerAIController();

	UStateTreeAIComponent* GetStateTreeAI() const;

protected:
	// StateTree를 실제로 실행하는 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UStateTreeAIComponent> StateTreeAI;

};
