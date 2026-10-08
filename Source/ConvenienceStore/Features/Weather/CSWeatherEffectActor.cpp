#include "Features/Weather/CSWeatherEffectActor.h"

#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

ACSWeatherEffectActor::ACSWeatherEffectActor()
{
	// 색감 페이드 중이거나 커버 박스 판정이 필요할 때만 Tick을 활성화하도록 시작 시 비활성화
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	WeatherPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("WeatherPostProcess"));
	WeatherPostProcess->SetupAttachment(SceneRoot);
	// 액터 위치와 관계없이 화면 전체에 색감을 적용
	WeatherPostProcess->bUnbound = true;
	// Priority 0인 레벨 PostProcessVolume보다 겹치는 항목을 우선 적용
	WeatherPostProcess->Priority = 1.0f;
	// ActivatePostProcess 호출 전에는 화면에 영향을 주지 않도록 0으로 설정
	WeatherPostProcess->BlendWeight = 0.0f;
}

void ACSWeatherEffectActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 색감 페이드와 커버 박스 판정은 서로 독립적으로 처리
	if (bPostProcessFading)
	{
		TickPostProcessFade(DeltaSeconds);
	}

	if (IsCoverageFadeEnabled())
	{
		UpdateCoverageFadeBypass();
	}

	// 둘 다 필요 없어지면 Tick을 중지
	UpdateTickEnabled();
}

void ACSWeatherEffectActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// MPC 값은 월드에 남으므로 다음 날씨 액터가 이전 값을 물려받지 않도록 페이드 켬 상태로 복원
	SetCoverageFadeBypass(0.0f);

	Super::EndPlay(EndPlayReason);
}

void ACSWeatherEffectActor::TickPostProcessFade(float DeltaSeconds)
{
	// 컴포넌트가 없거나 페이드 시간이 0이면 색감 페이드를 중지
	if (!IsValid(WeatherPostProcess) || PostProcessFadeSeconds <= KINDA_SMALL_NUMBER)
	{
		bPostProcessFading = false;
		return;
	}

	// 초당 1 / PostProcessFadeSeconds만큼 목표 가중치로 이동
	// 페이드 중 실내외가 바뀌면 현재 값에서 새 목표로 이어서 이동
	const float Target = GetTargetPostProcessWeight();
	const float MaxStep = DeltaSeconds / PostProcessFadeSeconds;
	const float Current = WeatherPostProcess->BlendWeight;
	WeatherPostProcess->BlendWeight = Current + FMath::Clamp(Target - Current, -MaxStep, MaxStep);

	// 목표 가중치에 도달하면 값을 고정하고 색감 페이드를 중지
	if (FMath::IsNearlyEqual(WeatherPostProcess->BlendWeight, Target))
	{
		WeatherPostProcess->BlendWeight = Target;
		bPostProcessFading = false;
	}
}

void ACSWeatherEffectActor::UpdateCoverageFadeBypass()
{
	APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (CameraManager == nullptr)
	{
		return;
	}

	// 카메라와 가장 가까운 커버 박스 경계까지의 XY 거리 (박스 안이면 0)
	const FVector CameraLocation = CameraManager->GetCameraLocation();
	const FVector2D CameraXY(CameraLocation.X, CameraLocation.Y);
	float MinDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < CoverageCentersXY.Num(); ++Index)
	{
		// 카메라 위치를 박스 중심 기준으로 옮기고 -Yaw만큼 회전해 박스 로컬 좌표로 변환
		float Sin = 0.0f;
		float Cos = 1.0f;
		FMath::SinCos(&Sin, &Cos, CoverageYawsRad[Index]);
		const FVector2D Delta = CameraXY - CoverageCentersXY[Index];
		const float LocalX = Delta.X * Cos + Delta.Y * Sin;
		const float LocalY = -Delta.X * Sin + Delta.Y * Cos;

		// 각 축에서 박스 밖으로 나간 거리만 사용
		const float OutX = FMath::Max(FMath::Abs(LocalX) - CoverageHalfSizes[Index].X, 0.0f);
		const float OutY = FMath::Max(FMath::Abs(LocalY) - CoverageHalfSizes[Index].Y, 0.0f);
		MinDistance = FMath::Min(MinDistance, FMath::Sqrt(OutX * OutX + OutY * OutY));
	}

	// 박스 안이면 1(페이드 끔), 경계에서 전환 거리 이상 나가면 0(페이드 켬)
	const float TransitionDistance = FMath::Max(CoverageFadeTransitionDistance, 1.0f);
	SetCoverageFadeBypass(1.0f - FMath::Clamp(MinDistance / TransitionDistance, 0.0f, 1.0f));
}

void ACSWeatherEffectActor::SetCoverageFadeBypass(float Value)
{
	static const FName FadeBypassName(TEXT("FadeBypass"));

	UWorld* World = GetWorld();
	if (WeatherParameterCollection == nullptr || World == nullptr)
	{
		return;
	}

	UMaterialParameterCollectionInstance* CollectionInstance = World->GetParameterCollectionInstance(WeatherParameterCollection);
	if (CollectionInstance == nullptr)
	{
		return;
	}

	// 값이 같으면 MPC 내부에서 렌더 상태를 갱신하지 않음
	if (!CollectionInstance->SetScalarParameterValue(FadeBypassName, Value) && !bWarnedMissingFadeBypass)
	{
		bWarnedMissingFadeBypass = true;
		UE_LOG(LogTemp, Warning, TEXT("[Weather] %s에 FadeBypass 스칼라 파라미터가 없어 커버 박스 페이드를 적용하지 못했습니다."),
			*GetNameSafe(WeatherParameterCollection));
	}
}

bool ACSWeatherEffectActor::IsCoverageFadeEnabled() const
{
	return WeatherParameterCollection != nullptr && CoverageCentersXY.Num() > 0;
}

void ACSWeatherEffectActor::UpdateTickEnabled()
{
	SetActorTickEnabled(bPostProcessFading || IsCoverageFadeEnabled());
}

void ACSWeatherEffectActor::ActivatePostProcess(bool bFade)
{
	// 색감 활성화 상태를 기록하고 현재 실내외 상태의 가중치를 적용
	bPostProcessActivated = true;
	RefreshPostProcessWeight(bFade);
}

float ACSWeatherEffectActor::GetTargetPostProcessWeight() const
{
	// 활성화 전이거나 종료 중이면 색감을 적용하지 않음
	if (!bPostProcessActivated || bStopRequested)
	{
		return 0.0f;
	}

	// 파티클 표시 상태를 실외 여부로 사용해 가중치를 반환
	return bLocalEffectActive ? OutdoorPostProcessWeight : IndoorPostProcessWeight;
}

void ACSWeatherEffectActor::RefreshPostProcessWeight(bool bFade)
{
	if (!IsValid(WeatherPostProcess))
	{
		return;
	}

	// 페이드를 요청하지 않았거나 페이드 시간이 0이면 목표 가중치를 즉시 적용
	const float Target = GetTargetPostProcessWeight();
	if (!bFade || PostProcessFadeSeconds <= KINDA_SMALL_NUMBER)
	{
		WeatherPostProcess->BlendWeight = Target;
		bPostProcessFading = false;
		UpdateTickEnabled();
		return;
	}

	// 현재 값이 목표와 다를 때만 Tick에서 페이드를 처리
	bPostProcessFading = !FMath::IsNearlyEqual(WeatherPostProcess->BlendWeight, Target);
	UpdateTickEnabled();
}

void ACSWeatherEffectActor::StartEffect_Implementation()
{
	// 기본 구현은 비어 있습니다. 나이아가라 Activate는 상속 BP가 합니다.
}

void ACSWeatherEffectActor::StopEffect_Implementation()
{
	if (bStopRequested)
	{
		// 이미 종료 절차가 시작되었습니다. Destroy를 다시 부르지 않습니다.
		return;
	}
	bStopRequested = true;

	// 정산 UI를 닫는 순간이 곧 다음 날이므로 잦아들 시간을 두지 않습니다.
	// 액터와 함께 나이아가라 컴포넌트가 지워지면서 남은 파티클도 같은 프레임에 사라집니다.
	Destroy();
}

void ACSWeatherEffectActor::SetLocalEffectActive_Implementation(bool bActive)
{
	// 기존 상태와 같으면 추가 처리 없이 종료
	if (bLocalEffectActive == bActive)
	{
		return;
	}
	// 요청받은 표시 상태를 저장
	bLocalEffectActive = bActive;

	// 블로커가 있는 맵에서는 실내에서도 파티클을 표시하도록 숨김 설정과 함께 판정
	RefreshParticleVisibility();

	// 색감은 끄지 않고 실외/실내 가중치로 전환
	// 문을 드나들 때 색이 한 번에 바뀌지 않도록 항상 페이드로 처리
	RefreshPostProcessWeight(true);
}

void ACSWeatherEffectActor::SetHideParticlesWhenSheltered(bool bHide)
{
	// 기존 설정과 같으면 추가 처리 없이 종료
	if (bHideParticlesWhenSheltered == bHide)
	{
		return;
	}
	bHideParticlesWhenSheltered = bHide;

	// 이미 실내에 있는 상태에서 설정이 바뀌어도 즉시 반영
	RefreshParticleVisibility();
}

void ACSWeatherEffectActor::RefreshParticleVisibility()
{
	// 실외이거나 실내 숨김을 사용하지 않으면 파티클을 표시
	const bool bVisible = bLocalEffectActive || !bHideParticlesWhenSheltered;

	// 액터가 가진 나이아가라 컴포넌트를 수집
	TArray<UNiagaraComponent*> NiagaraComponents;
	GetComponents<UNiagaraComponent>(NiagaraComponents);
	for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
	{
		// 유효하지 않은 컴포넌트는 제외
		if (!IsValid(NiagaraComponent))
		{
			continue;
		}

		// 월드 고정 커버 컴포넌트는 실내외와 관계없이 표시
		if (CoverageComponents.Contains(NiagaraComponent))
		{
			NiagaraComponent->SetVisibility(true);
			continue;
		}

		// 파티클 재생을 다시 시작하지 않고 표시 여부만 변경
		NiagaraComponent->SetVisibility(bVisible);
	}
}

void ACSWeatherEffectActor::ApplyWeatherBlockers(const TArray<FVector>& Centers, const TArray<FVector>& HalfExtents, const TArray<float>& Yaws)
{
	// 나이아가라 에셋의 User 파라미터 이름과 일치해야 전달
	static const FName BlockerCentersName(TEXT("User.BlockerCenters"));
	static const FName BlockerHalfExtentsName(TEXT("User.BlockerHalfExtents"));
	static const FName BlockerYawsName(TEXT("User.BlockerYaws"));

	// 파라미터 존재 여부를 확인할 Position 배열 변수
	const FNiagaraVariableBase CentersVariable(
		FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayPosition::StaticClass()),
		BlockerCentersName);

	// 액터가 가진 나이아가라 컴포넌트를 수집
	TArray<UNiagaraComponent*> NiagaraComponents;
	GetComponents<UNiagaraComponent>(NiagaraComponents);
	for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
	{
		// 유효하지 않은 컴포넌트는 제외
		if (!IsValid(NiagaraComponent))
		{
			continue;
		}

		// 블로커 모듈이 없는 에셋은 파라미터 미존재 경고가 출력되지 않도록 제외
		if (NiagaraComponent->GetOverrideParameters().FindParameterOffset(CentersVariable, true) == nullptr)
		{
			continue;
		}

		// 박스 중심, 절반 크기, Yaw를 같은 인덱스로 맞춰 전달
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(NiagaraComponent, BlockerCentersName, Centers);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComponent, BlockerHalfExtentsName, HalfExtents);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(NiagaraComponent, BlockerYawsName, Yaws);
	}
}

void ACSWeatherEffectActor::ApplyCoverageAreas(const TArray<FVector>& TopCenters, const TArray<FVector2D>& Sizes, const TArray<float>& Yaws)
{
	// 나이아가라 에셋의 User 파라미터 이름과 일치해야 전달
	static const FName SpawnAreaSizeName(TEXT("User.SpawnAreaSize"));
	static const FName SpawnRateScaleName(TEXT("User.SpawnRateScale"));
	static const FName ExcludeCentersName(TEXT("User.ExcludeCenters"));
	static const FName ExcludeHalfExtentsName(TEXT("User.ExcludeHalfExtents"));
	static const FName ExcludeYawsName(TEXT("User.ExcludeYaws"));

	// 캐릭터를 따라다니는 원본 컴포넌트만 수집
	TArray<UNiagaraComponent*> FollowComponents;
	GetComponents<UNiagaraComponent>(FollowComponents);
	FollowComponents.RemoveAll([this](UNiagaraComponent* NiagaraComponent)
	{
		return !IsValid(NiagaraComponent) || CoverageComponents.Contains(NiagaraComponent);
	});

	// 이전에 생성한 커버 컴포넌트를 제거
	for (UNiagaraComponent* OldComponent : CoverageComponents)
	{
		if (IsValid(OldComponent))
		{
			OldComponent->DestroyComponent();
		}
	}
	CoverageComponents.Reset();

	// 커버 박스를 제외 영역 배열로 변환
	// 생성 높이와 관계없이 박스 XY 범위에서 생성된 입자를 소멸하도록 Z 절반 크기는 충분히 크게 설정
	const int32 AreaCount = FMath::Min3(TopCenters.Num(), Sizes.Num(), Yaws.Num());
	TArray<FVector> ExcludeCenters;
	TArray<FVector> ExcludeHalfExtents;
	TArray<float> ExcludeYaws;
	ExcludeCenters.Reserve(AreaCount);
	ExcludeHalfExtents.Reserve(AreaCount);
	ExcludeYaws.Reserve(AreaCount);
	for (int32 Index = 0; Index < AreaCount; ++Index)
	{
		ExcludeCenters.Add(TopCenters[Index]);
		ExcludeHalfExtents.Add(FVector(Sizes[Index].X * 0.5f, Sizes[Index].Y * 0.5f, 100000.0f));
		ExcludeYaws.Add(FMath::DegreesToRadians(Yaws[Index]));
	}

	// 카메라 거리 페이드를 끌지 판정할 커버 박스 XY 범위 저장
	CoverageCentersXY.Reset(AreaCount);
	CoverageHalfSizes.Reset(AreaCount);
	CoverageYawsRad.Reset(AreaCount);
	for (int32 Index = 0; Index < AreaCount; ++Index)
	{
		CoverageCentersXY.Add(FVector2D(TopCenters[Index].X, TopCenters[Index].Y));
		CoverageHalfSizes.Add(Sizes[Index] * 0.5f);
		CoverageYawsRad.Add(ExcludeYaws[Index]);
	}

	if (AreaCount > 0 && WeatherParameterCollection == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Weather] %s에 WeatherParameterCollection이 지정되지 않아 커버 박스 안에서도 카메라 거리 페이드가 적용됩니다."),
			*GetClass()->GetName());
	}

	// 커버 박스가 없어지면 페이드 켬 상태로 되돌리고, 있으면 Tick에서 판정
	if (!IsCoverageFadeEnabled())
	{
		SetCoverageFadeBypass(0.0f);
	}
	UpdateTickEnabled();

	// 커버 영역을 지원하는 에셋인지 확인할 Float 변수
	const FNiagaraVariableBase RateScaleVariable(FNiagaraTypeDefinition::GetFloatDef(), SpawnRateScaleName);
	const float ReferenceArea = ReferenceSpawnAreaSize.X * ReferenceSpawnAreaSize.Y;

	for (UNiagaraComponent* FollowComponent : FollowComponents)
	{
		// 에셋이 없거나 커버 파라미터가 없는 컴포넌트는 파라미터 미존재 경고가 출력되지 않도록 제외
		UNiagaraSystem* System = FollowComponent->GetAsset();
		if (System == nullptr
			|| FollowComponent->GetOverrideParameters().FindParameterOffset(RateScaleVariable, true) == nullptr)
		{
			continue;
		}

		// 원본은 커버 박스 안에서 생성된 입자를 소멸 (나이아가라 Spawn 단계에서만 검사)
		// 커버 영역이 없으면 빈 배열을 전달해 제외 검사를 해제
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(FollowComponent, ExcludeCentersName, ExcludeCenters);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(FollowComponent, ExcludeHalfExtentsName, ExcludeHalfExtents);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(FollowComponent, ExcludeYawsName, ExcludeYaws);

		for (int32 Index = 0; Index < AreaCount; ++Index)
		{
			UNiagaraComponent* AreaComponent = NewObject<UNiagaraComponent>(this, NAME_None, RF_Transient);
			AreaComponent->SetAsset(System);
			AreaComponent->SetAutoActivate(false);

			// 플레이어를 따라가지 않도록 월드 기준 위치·회전·스케일 사용
			AreaComponent->SetupAttachment(GetRootComponent());
			AreaComponent->SetUsingAbsoluteLocation(true);
			AreaComponent->SetUsingAbsoluteRotation(true);
			AreaComponent->SetUsingAbsoluteScale(true);
			AreaComponent->SetWorldLocationAndRotation(TopCenters[Index], FRotator(0.0f, Yaws[Index], 0.0f));
			AreaComponent->SetWorldScale3D(FVector::OneVector);
			AreaComponent->RegisterComponent();

			// 영역 크기와 면적에 비례한 스폰 레이트 배수를 설정한 뒤 재생
			// 캐릭터 주위 파티클과 단위 면적당 밀도가 같도록 배수를 계산
			const FVector2D& Size = Sizes[Index];
			AreaComponent->SetVariableVec2(SpawnAreaSizeName, Size);
			AreaComponent->SetVariableFloat(SpawnRateScaleName,
				ReferenceArea > KINDA_SMALL_NUMBER ? (Size.X * Size.Y) / ReferenceArea : 1.0f);
			AreaComponent->Activate(true);

			CoverageComponents.Add(AreaComponent);
		}
	}
}
