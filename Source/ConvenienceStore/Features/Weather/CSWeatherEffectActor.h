#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSWeatherEnvironmentTypes.h"
#include "CSWeatherEffectActor.generated.h"

class UMaterialParameterCollection;
class UNiagaraComponent;
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	// 실내에 있을 때 캐릭터 주위 파티클을 숨길지 설정하고 현재 표시 상태에 반영
	// 블로커가 있는 맵은 false로 설정해 실내에서도 파티클을 표시
	void SetHideParticlesWhenSheltered(bool bHide);

	// 블로커 박스 목록을 모든 나이아가라 컴포넌트의 User 배열 파라미터로 전달
	// BlockerCenters 파라미터가 없는 나이아가라 컴포넌트는 제외
	// Yaws는 라디안 단위
	void ApplyWeatherBlockers(const TArray<FVector>& Centers, const TArray<FVector>& HalfExtents, const TArray<float>& Yaws);

	// 커버 영역마다 원본 나이아가라 컴포넌트와 같은 에셋의 컴포넌트를 월드 고정 위치에 생성
	// 이전에 생성한 커버 컴포넌트는 제거하고 다시 생성
	// 원본 컴포넌트에는 커버 박스를 제외 영역으로 전달해 그 안에서 생성된 입자를 소멸
	// SpawnRateScale 파라미터가 없는 에셋은 제외
	// TopCenters는 박스 윗면 중심(생성 높이), Sizes는 XY 전체 크기, Yaws는 도 단위
	void ApplyCoverageAreas(const TArray<FVector>& TopCenters, const TArray<FVector2D>& Sizes, const TArray<float>& Yaws);

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

	// 나이아가라 에셋의 User.SpawnAreaSize 기본값 (캐릭터 주위 파티클 생성 범위)
	// 커버 컴포넌트 스폰 레이트 배수 = 영역 면적 / 이 면적
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weather|Coverage")
	FVector2D ReferenceSpawnAreaSize = FVector2D(4000.0f, 4000.0f);

	// 날씨 파티클 머티리얼(M_WeatherParticle)이 읽는 Material Parameter Collection (MPC_Weather)
	// 카메라가 커버 박스 안에 있으면 FadeBypass를 1로 써서 카메라 거리 페이드를 끔
	// 비어 있으면 FadeBypass를 쓰지 않으므로 페이드가 항상 켜진 상태로 동작
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weather|Coverage")
	TObjectPtr<UMaterialParameterCollection> WeatherParameterCollection;

	// 카메라가 커버 박스 경계에서 이 거리(XY)만큼 나가면 페이드가 완전히 켜짐
	// 박스 경계를 넘을 때 먼 파티클이 한 번에 사라지지 않도록 이 구간에서 서서히 전환
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weather|Coverage",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float CoverageFadeTransitionDistance = 1500.0f;

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

	// 색감 가중치를 목표 쪽으로 한 프레임만큼 이동하고 목표에 도달하면 bPostProcessFading을 해제
	void TickPostProcessFade(float DeltaSeconds);

	// 카메라와 가장 가까운 커버 박스 경계까지의 XY 거리로 FadeBypass를 계산해 MPC에 기록
	void UpdateCoverageFadeBypass();

	// MPC의 FadeBypass 값을 설정. MPC가 없거나 파라미터가 없으면 아무것도 하지 않음
	void SetCoverageFadeBypass(float Value);

	// 커버 박스 기준 페이드 판정을 사용하는지 (커버 박스가 있고 MPC가 지정됨)
	bool IsCoverageFadeEnabled() const;

	// 색감 페이드 중이거나 커버 박스 판정이 필요할 때만 Tick을 활성화
	void UpdateTickEnabled();

	// 실내외 상태와 숨김 설정에 맞춰 나이아가라 컴포넌트의 표시 여부를 갱신
	void RefreshParticleVisibility();

	// 실내에 있을 때 캐릭터 주위 파티클을 숨길지 여부
	// 블로커가 없는 맵은 기존 문 앞 영역 방식을 유지하도록 true로 초기화
	bool bHideParticlesWhenSheltered = true;

	// ApplyCoverageAreas가 생성한 월드 고정 나이아가라 컴포넌트
	// 실내 숨김 대상에서 제외하고 다시 적용할 때 제거
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> CoverageComponents;

	// ActivatePostProcess 호출 여부
	// 호출 전에는 실내외 전환이 발생해도 색감 가중치를 0으로 유지
	bool bPostProcessActivated = false;

	// 색감 가중치가 목표로 이동하는 중인지 여부
	bool bPostProcessFading = false;

	// ApplyCoverageAreas가 받은 커버 박스의 XY 중심, XY 절반 크기, Yaw(라디안)
	// 카메라가 박스 안에 있는지 판정할 때 사용
	TArray<FVector2D> CoverageCentersXY;
	TArray<FVector2D> CoverageHalfSizes;
	TArray<float> CoverageYawsRad;

	// MPC에 FadeBypass 파라미터가 없을 때 경고를 한 번만 출력
	bool bWarnedMissingFadeBypass = false;
};
