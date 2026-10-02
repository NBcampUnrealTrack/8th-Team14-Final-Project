#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSWeatherEnvironmentTypes.h"
#include "CSWeatherEffectActor.generated.h"

class UPostProcessComponent;
class USceneComponent;

// 날씨 연출 액터의 베이스입니다.
// 나이아가라 시스템,사운드,포스트 프로세스 상속 BP(BP_WeatherEffect_Rain 등)가 소유
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API ACSWeatherEffectActor : public AActor
{
	GENERATED_BODY()

public:
	ACSWeatherEffectActor();

	virtual void Tick(float DeltaSeconds) override;

	// 날씨 색감 포스트 프로세스를 활성화하고 실내외 상태에 맞는 가중치를 적용
	// 서브시스템이 실내외 판정을 마친 뒤 호출
	// BP의 StartEffect 재정의와 관계없이 색감이 적용되도록 C++ 함수로 분리
	// bFade가 false면 목표 가중치를 즉시 적용하고 true면 PostProcessFadeSeconds 기준으로 서서히 적용
	void ActivatePostProcess(bool bFade);

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
	// 파티클 활성화 상태와 사운드는 변경하지 않음
	// 포스트 프로세스는 끄지 않고 실외/실내 가중치로 전환
	// BP에서 재정의하면 Parent 노드를 호출해야 파티클과 색감 전환을 유지
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weather")
	void SetLocalEffectActive(bool bActive);

	// 캐릭터 주위 날씨 파티클의 표시 설정을 반환
	UFUNCTION(BlueprintPure, Category = "Weather")
	bool IsLocalEffectActive() const { return bLocalEffectActive; }

	// 이 날씨에서 레벨 환경 액터에 적용할 배수와 틴트를 반환
	const FCSWeatherEnvironmentProfile& GetEnvironmentProfile() const { return EnvironmentProfile; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 날씨 색감을 화면 전체에 적용하는 포스트 프로세스 컴포넌트
	// 레벨 PostProcessVolume보다 높은 Priority로 겹쳐 적용하고 기존 볼륨 설정은 유지
	// 날씨별 색감 값은 각 BP에서 이 컴포넌트의 Override 항목을 켜고 설정
	// 레벨 볼륨의 M_PP_ShelfOutline 중복 적용을 방지하도록 Blendables는 비워 둠
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPostProcessComponent> WeatherPostProcess;

	// 실외에서 적용할 색감 가중치
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weather|PostProcess",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OutdoorPostProcessWeight = 1.0f;

	// 실내에서 적용할 색감 가중치
	// 실내 조명 색이 크게 바뀌지 않도록 실외보다 낮은 값을 사용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weather|PostProcess",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float IndoorPostProcessWeight = 0.4f;

	// 가중치가 0에서 1까지 변하는 데 걸리는 시간(초)
	// 영업 중 날씨 변경과 실내외 전환에 같은 속도를 사용하고 0이면 즉시 적용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weather|PostProcess",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PostProcessFadeSeconds = 2.0f;

	// 이 날씨에서 레벨 환경 액터(해, SkyLight, Sky Atmosphere, 안개)와 달빛에 적용할 배수와 틴트
	// 날씨 환경 컨트롤러가 읽어 적용하고 이 액터는 환경 액터를 직접 변경하지 않음
	// 기본값은 원래 값을 유지하므로 설정하지 않은 날씨는 환경 변화가 없음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weather|Environment")
	FCSWeatherEnvironmentProfile EnvironmentProfile;

	// StopEffect가 두 번 불려도 Destroy를 두 번 부르지 않게 합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	bool bStopRequested = false;

	// 캐릭터 주위 날씨 파티클의 표시 상태
	// 생성 시 보이는 상태를 기준으로 true로 초기화
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	bool bLocalEffectActive = true;

private:
	// 현재 실내외 상태에 맞는 색감 목표 가중치를 반환
	// ActivatePostProcess 호출 전이거나 종료 중이면 0
	float GetTargetPostProcessWeight() const;

	// 목표 가중치를 다시 계산해 즉시 적용하거나 페이드가 필요하면 Tick을 활성화
	void RefreshPostProcessWeight(bool bFade);

	// ActivatePostProcess 호출 여부
	// 호출 전에는 실내외 전환이 발생해도 색감 가중치를 0으로 유지
	bool bPostProcessActivated = false;
};
