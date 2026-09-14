#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSWeatherEffectActor.generated.h"

class USceneComponent;

// 날씨 연출 액터의 베이스입니다.
// 나이아가라 시스템,사운드,포스트 프로세스 상속 BP(BP_WeatherEffect_Rain 등)가 소유
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API ACSWeatherEffectActor : public AActor
{
	GENERATED_BODY()

public:
	ACSWeatherEffectActor();

	// 시작 연출입니다. 기본 구현은 아무것도 하지 않으므로 BP가 파티클을 Activate 합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weather")
	void StartEffect();

	// 남아 있던 파티클까지 그 자리에서 즉시 지웁니다.
	// 이 호출은 정산 UI를 닫아 하루가 넘어가는 지점에서만 일어나므로
	// 잦아드는 연출 없이 바로 다음 날 날씨로 교체되는 편이 맞습니다.
	// BP에서 재정의할 때는 반드시 Parent 노드를 호출해야 액터가 지워집니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weather")
	void StopEffect();

	// 캐릭터 주위 날씨 파티클의 표시 여부를 변경
	// 파티클 활성화 상태와 사운드, 포스트프로세스는 변경하지 않음
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weather")
	void SetLocalEffectActive(bool bActive);

	// 캐릭터 주위 날씨 파티클의 표시 설정을 반환
	UFUNCTION(BlueprintPure, Category = "Weather")
	bool IsLocalEffectActive() const { return bLocalEffectActive; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// StopEffect가 두 번 불려도 Destroy를 두 번 부르지 않게 합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	bool bStopRequested = false;

	// 캐릭터 주위 날씨 파티클의 표시 상태
	// 생성 시 보이는 상태를 기준으로 true로 초기화
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	bool bLocalEffectActive = true;
};
