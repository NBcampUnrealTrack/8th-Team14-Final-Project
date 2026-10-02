#pragma once

#include "CoreMinimal.h"
#include "CSWeatherEnvironmentTypes.generated.h"

// 날씨별 레벨 환경 액터 조절 값
// 레벨 원래 값에 곱하는 배수와 틴트, 구름 머티리얼에 넣는 절대값으로 구성하고 기본값은 원래 값을 유지
// 각 날씨 이펙트 BP에서 설정하고 날씨 환경 컨트롤러가 읽어 적용
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherEnvironmentProfile
{
	GENERATED_BODY()

	// 태양 밝기 배수
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SunIntensityScale = 1.0f;

	// 원래 태양 색에 곱하는 색
	// 흰색이면 원래 색을 유지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun")
	FLinearColor SunColorTint = FLinearColor::White;

	// SkyLight 밝기 배수
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SkyLight",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SkyLightIntensityScale = 1.0f;

	// 달빛 밝기 배수
	// 낮밤 컨트롤러가 시간대별 달빛 밝기에 곱해 적용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MoonIntensityScale = 1.0f;

	// Sky Atmosphere Rayleigh 산란 배수
	// 낮추면 하늘의 파란색이 약해짐
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RayleighScatteringScale = 1.0f;

	// Sky Atmosphere Mie 산란 배수
	// 올리면 하늘과 먼 곳이 희뿌옇게 보임
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MieScatteringScale = 1.0f;

	// 안개 밀도 배수
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FogDensityScale = 1.0f;

	// 원래 안개 색과 Sky Atmosphere 하늘빛 배율에 곱하는 색
	// 흰색이면 원래 색을 유지
	// 절대값 대신 틴트를 사용해 기본값에서 안개가 검게 바뀌는 상황을 방지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog")
	FLinearColor FogInscatteringTint = FLinearColor::White;

	// 구름 Coverage 값 덮어쓰기 여부
	// 끄면 레벨 구름 머티리얼의 원래 값을 유지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud", meta = (InlineEditConditionToggle))
	bool bOverrideCloudCoverage = false;

	// 구름 머티리얼 Coverage 파라미터에 넣을 값
	// 양수면 구름이 늘고 음수면 줄어들며 원래 값이 음수일 수 있어 최솟값을 제한하지 않음
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud",
		meta = (EditCondition = "bOverrideCloudCoverage", UIMin = "-2.0", UIMax = "2.0"))
	float CloudCoverage = 0.0f;

	// 구름 Density 값 덮어쓰기 여부
	// 끄면 레벨 구름 머티리얼의 원래 값을 유지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud", meta = (InlineEditConditionToggle))
	bool bOverrideCloudDensity = false;

	// 구름 머티리얼 Density 파라미터에 넣을 값
	// 높을수록 구름이 불투명해짐
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud",
		meta = (EditCondition = "bOverrideCloudDensity", ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float CloudDensity = 0.0f;

	// 두 프로필을 Alpha 비율로 보간한 결과를 반환
	// Alpha가 0이면 From, 1이면 To와 같고 0~1 범위 제한은 호출부에서 처리
	// 프로필에 필드를 추가하면 보간 중 기본값이 적용되지 않도록 여기에도 추가
	static FCSWeatherEnvironmentProfile Lerp(
		const FCSWeatherEnvironmentProfile& From,
		const FCSWeatherEnvironmentProfile& To,
		float Alpha)
	{
		// 모든 필드를 아래에서 덮어쓰므로 기본값으로 생성
		FCSWeatherEnvironmentProfile Result;

		// 태양 밝기 배수와 색 틴트를 보간
		Result.SunIntensityScale = FMath::Lerp(From.SunIntensityScale, To.SunIntensityScale, Alpha);
		Result.SunColorTint = FMath::Lerp(From.SunColorTint, To.SunColorTint, Alpha);

		// SkyLight와 달빛 밝기 배수를 보간
		Result.SkyLightIntensityScale = FMath::Lerp(From.SkyLightIntensityScale, To.SkyLightIntensityScale, Alpha);
		Result.MoonIntensityScale = FMath::Lerp(From.MoonIntensityScale, To.MoonIntensityScale, Alpha);

		// Sky Atmosphere 산란 배수를 보간
		Result.RayleighScatteringScale = FMath::Lerp(From.RayleighScatteringScale, To.RayleighScatteringScale, Alpha);
		Result.MieScatteringScale = FMath::Lerp(From.MieScatteringScale, To.MieScatteringScale, Alpha);

		// 안개 밀도 배수와 색 틴트를 보간
		Result.FogDensityScale = FMath::Lerp(From.FogDensityScale, To.FogDensityScale, Alpha);
		Result.FogInscatteringTint = FMath::Lerp(From.FogInscatteringTint, To.FogInscatteringTint, Alpha);

		// 구름 값을 보간
		// 컨트롤러가 보간 전에 덮어쓰지 않은 값을 원래 값으로 채우므로 값만 보간하고 덮어쓰기 여부는 목표를 따름
		Result.bOverrideCloudCoverage = To.bOverrideCloudCoverage;
		Result.CloudCoverage = FMath::Lerp(From.CloudCoverage, To.CloudCoverage, Alpha);
		Result.bOverrideCloudDensity = To.bOverrideCloudDensity;
		Result.CloudDensity = FMath::Lerp(From.CloudDensity, To.CloudDensity, Alpha);
		return Result;
	}
};
