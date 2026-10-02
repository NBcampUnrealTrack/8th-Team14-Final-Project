#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSWeatherEnvironmentTypes.h"
#include "Data/CSWeatherTypes.h"
#include "CSWeatherEnvironmentController.generated.h"

class ACSTimeOfDayLightingController;
class ADirectionalLight;
class AExponentialHeightFog;
class ASkyAtmosphere;
class ASkyLight;
class AVolumetricCloud;
class UCSWeatherSubsystem;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UVolumetricCloudComponent;

// 날씨에 맞춰 레벨 환경 액터(해, SkyLight, Sky Atmosphere, 안개, 구름)와 달빛 배수를 조절하는 레벨 배치 액터
// 날씨별 값은 현재 날씨 이펙트 액터의 EnvironmentProfile에서 읽음
// 색감은 이펙트 액터의 포스트 프로세스가 담당
UCLASS()
class CONVENIENCESTORE_API ACSWeatherEnvironmentController : public AActor
{
	GENERATED_BODY()

public:
	ACSWeatherEnvironmentController();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 같은 영업일 안에서 날씨가 바뀔 때의 보간 시간(초)
	// 0이면 즉시 적용
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BlendSeconds = 3.0f;

	// 밝기와 색을 조절할 태양
	// 회전은 낮밤 컨트롤러가 담당하므로 변경하지 않음
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment")
	TObjectPtr<ADirectionalLight> SunLight;

	// 밝기를 조절할 SkyLight
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment")
	TObjectPtr<ASkyLight> SkyLight;

	// 산란 값을 조절할 Sky Atmosphere
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment")
	TObjectPtr<ASkyAtmosphere> SkyAtmosphere;

	// 밀도와 색을 조절할 안개
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment")
	TObjectPtr<AExponentialHeightFog> HeightFog;

	// 달빛 배수를 전달할 낮밤 컨트롤러
	// 달빛은 낮밤 컨트롤러가 게임 시각마다 다시 계산하므로 직접 변경하지 않음
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment")
	TObjectPtr<ACSTimeOfDayLightingController> TimeOfDayController;

	// 구름 양을 조절할 VolumetricCloud
	// 머티리얼 에셋은 수정하지 않고 런타임 MID로 교체해 파라미터를 변경
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment")
	TObjectPtr<AVolumetricCloud> VolumetricCloud;

	// 구름 머티리얼의 Coverage 스칼라 파라미터 이름
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment")
	FName CloudCoverageParameterName = TEXT("Cloud_GlobalCoverage");

	// 구름 머티리얼의 Density 스칼라 파라미터 이름
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weather|Environment")
	FName CloudDensityParameterName = TEXT("Cloud_GlobalDensity");

	// 날씨 변경 알림을 처리
	UFUNCTION()
	void HandleWeatherChanged(ECSWeatherType NewWeather, ECSWeatherType PreviousWeather);

private:
	// BeginPlay 시점의 레벨 원래 값
	// 항상 원래 값에 배수를 곱해 적용해 값 누적을 방지
	struct FBaseline
	{
		float SunIntensity = 0.0f;
		FLinearColor SunColor = FLinearColor::White;
		float SkyLightIntensity = 0.0f;
		float RayleighScatteringScale = 0.0f;
		float MieScatteringScale = 0.0f;
		float FogDensity = 0.0f;
		FLinearColor FogInscatteringColor = FLinearColor::Black;

		// 안개 색에 반영되는 Sky Atmosphere 하늘빛 배율
		// 안개 자체 색이 검정인 맵에서도 틴트가 적용되도록 함께 저장
		FLinearColor FogSkyAtmosphereContribution = FLinearColor::White;

		// 구름 머티리얼의 Coverage와 Density 원래 값
		float CloudCoverage = 0.0f;
		float CloudDensity = 0.0f;
	};

	// 지정된 환경 액터의 현재 값을 원래 값으로 저장
	// 구름은 머티리얼을 런타임 MID로 교체한 뒤 원래 값을 저장
	void CaptureBaseline();

	// 원래 값에 프로필을 곱해 환경 액터와 달빛 배수에 적용
	// 구름은 덮어쓴 값만 적용하고 나머지는 원래 값을 사용
	void ApplyProfile(const FCSWeatherEnvironmentProfile& Profile);

	// 현재 날씨 이펙트 액터의 프로필을 반환하고 액터가 없으면 기본 프로필을 반환
	// 덮어쓰지 않은 구름 값은 원래 값으로 채워 반환
	FCSWeatherEnvironmentProfile GetTargetProfile() const;

	// 덮어쓰지 않은 구름 값을 원래 값으로 채우고 덮어쓰기 상태로 설정
	// 보간 시작점과 목표가 항상 실제 머티리얼 값을 갖도록 처리
	void ResolveCloudValues(FCSWeatherEnvironmentProfile& Profile) const;

	// 지정된 VolumetricCloud의 구름 컴포넌트를 조회하고 없으면 nullptr을 반환
	UVolumetricCloudComponent* GetCloudComponent() const;

	FBaseline Baseline;

	// 구름 컴포넌트에 지정한 런타임 MID
	// 구름 머티리얼이 없으면 nullptr이고 구름 처리를 건너뜀
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMID;

	// MID로 교체하기 전 구름 컴포넌트의 머티리얼
	// EndPlay에서 원래 머티리얼로 되돌릴 때 사용
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OriginalCloudMaterial;

	// 보간 시작 프로필
	FCSWeatherEnvironmentProfile FromProfile;

	// 보간 목표 프로필
	FCSWeatherEnvironmentProfile ToProfile;

	// 마지막으로 화면에 적용한 프로필
	// 보간 중 날씨가 다시 바뀌면 이 값에서 이어서 보간
	FCSWeatherEnvironmentProfile AppliedProfile;

	// 보간 경과 시간(초)
	float BlendElapsed = 0.0f;

	// 마지막으로 낮밤 컨트롤러에 전달한 달빛 배수
	// 값이 바뀔 때만 전달해 불필요한 달빛 재계산을 방지하고 음수는 미전달 상태
	float SentMoonIntensityScale = -1.0f;

	// 마지막으로 적용한 날씨와 영업일
	// 같은 영업일 안의 변경은 보간하고 영업일이 바뀌면 즉시 적용
	ECSWeatherType AppliedWeather = ECSWeatherType::None;
	int32 AppliedBusinessDay = INDEX_NONE;

	// 원래 값 저장 여부
	// 저장 전에는 EndPlay에서 복구하지 않음
	bool bBaselineCaptured = false;

	// 날씨 변경 알림을 구독한 서브시스템
	TWeakObjectPtr<UCSWeatherSubsystem> WeatherSubsystem;
};
