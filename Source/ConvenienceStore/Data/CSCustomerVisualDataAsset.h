// 손님 캐릭터의 외형(메시, 애니메이션)만 담당하는 데이터 에셋

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSCustomerVisualDataAsset.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimInstance;
class UPhysicsAsset;

UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSCustomerVisualDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// 이 종족이 사용할 스켈레탈 메시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<USkeletalMesh> CharacterMesh;

	// 애니메이션 블루프린트 클래스 (채워져 있으면 이걸 우선 사용)
	// 나중에 ABP를 만들면 여기에 연결하기만 하면 됩니다!
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TSubclassOf<UAnimInstance> AnimBlueprintClass;

	// ABP가 아직 없을 때 쓰는 간이 대기 애니메이션
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UAnimSequence> FallbackIdleAnimation;

	// ABP가 아직 없을 때 쓰는 간이 걷기 애니메이션
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UAnimSequence> FallbackWalkAnimation;

	// 상품을 고르기 전에 한 번 재생하는 간이 고민 애니메이션
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UAnimSequence> FallbackThinkingAnimation;

	// 구매 확정 상품을 가져가기 전에 한 번 재생하는 간이 집기 애니메이션
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UAnimSequence> FallbackPickAnimation;

	// Thinking/Pick 액션 중에만 적용할 월드 Z축 Mesh 높이 보정입니다. 음수는 아래, 양수는 위입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual|Animation")
	float ActionMeshWorldZOffset = -30.0f;
};
