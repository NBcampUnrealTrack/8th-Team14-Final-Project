#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CSPedestrianCharacter.generated.h"

class UCSCustomerVisualDataAsset;

// 거리를 이동하는 배경용 행인 캐릭터
// 손님 행동과 통계에 참여하지 않으며, 이동 입력은 스포너에서 처리
UCLASS()
class CONVENIENCESTORE_API ACSPedestrianCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// 컨트롤러 없이 이동하도록 설정하고 충돌과 애니메이션 갱신 방식을 초기화
	ACSPedestrianCharacter();

	// 풀에서 꺼낸 행인의 크기, 위치, 외형, 속도를 설정하고 활성화
	// AnimStartRatio는 단일 애니메이션의 시작 위치를 정하는 비율(0~1)
	void ActivatePedestrian(
		const UCSCustomerVisualDataAsset* Visual,
		const FTransform& SpawnTransform,
		float WalkSpeed,
		float AnimStartRatio);

	// 행인을 숨기고 이동, 애니메이션 갱신, 충돌을 비활성화
	// 액터를 파괴하지 않고 풀에서 재사용할 수 있는 상태로 전환
	void DeactivatePedestrian();

	// 행인이 풀에서 꺼내져 활성 상태인지 반환
	bool IsPedestrianActive() const { return bPedestrianActive; }

private:
	// 외형 데이터의 메시와 애니메이션을 적용
	// 애니메이션 블루프린트를 우선 사용하고, 없으면 걷기 또는 대기 애니메이션을 반복 재생
	void ApplyVisual(
		const UCSCustomerVisualDataAsset* Visual,
		float AnimStartRatio);

	// 현재 행인의 활성 상태
	bool bPedestrianActive = false;
};
