#include "Features/Weather/CSWeatherZoneActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "Systems/Weather/CSWeatherSubsystem.h"

ACSWeatherZoneActor::ACSWeatherZoneActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 비와 눈의 생성 범위를 지정할 연출용 박스
	// 충돌, 오버랩, 내비게이션에는 영향을 주지 않음
	VisualBox = CreateDefaultSubobject<UBoxComponent>(TEXT("VisualBox"));
	VisualBox->SetupAttachment(SceneRoot);
	VisualBox->SetBoxExtent(FVector(800.0f, 800.0f, 500.0f));
	VisualBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualBox->SetGenerateOverlapEvents(false);
	VisualBox->SetCanEverAffectNavigation(false);
	VisualBox->ShapeColor = FColor::Cyan;

	// Pawn과의 오버랩만 감지하는 판정용 박스
	// 실제 플레이어 여부는 날씨 서브시스템에서 확인
	JudgeBox = CreateDefaultSubobject<UBoxComponent>(TEXT("JudgeBox"));
	JudgeBox->SetupAttachment(SceneRoot);
	JudgeBox->SetBoxExtent(FVector(400.0f, 400.0f, 500.0f));
	JudgeBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	JudgeBox->SetCollisionObjectType(ECC_WorldDynamic);
	JudgeBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	JudgeBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	JudgeBox->SetGenerateOverlapEvents(true);
	JudgeBox->SetCanEverAffectNavigation(false);
	JudgeBox->ShapeColor = FColor::Orange;

	// 판정 박스 진입과 이탈 이벤트를 연결
	JudgeBox->OnComponentBeginOverlap.AddDynamic(
		this,
		&ACSWeatherZoneActor::HandleJudgeBoxBeginOverlap);
	JudgeBox->OnComponentEndOverlap.AddDynamic(
		this,
		&ACSWeatherZoneActor::HandleJudgeBoxEndOverlap);

	// 영역에 표시할 날씨 파티클 생성
	ZoneNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ZoneNiagara"));
	ZoneNiagara->SetupAttachment(SceneRoot);

	// 서브시스템에서 날씨 에셋을 적용하기 전에는 자동 재생하지 않음
	ZoneNiagara->SetAutoActivate(false);
}

// 액터 구성 시 연출 박스에 맞춰 파티클 생성 위치와 크기를 갱신
void ACSWeatherZoneActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);


	UpdateZoneNiagaraTransform();
}

void ACSWeatherZoneActor::BeginPlay()
{
	Super::BeginPlay();

	UpdateZoneNiagaraTransform();

	// 등록 시 현재 날씨 에셋과 표시 상태를 전달받고 플레이어 위치를 재판정
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->RegisterZone(this);
		}
	}
}

void ACSWeatherZoneActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->UnregisterZone(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ACSWeatherZoneActor::HandleJudgeBoxBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	// 손님과 직원 등 다른 Pawn은 서브시스템에서 제외
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->NotifyShelterOverlapChanged(OtherActor);
		}
	}
}

void ACSWeatherZoneActor::HandleJudgeBoxEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	// 다른 판정 영역에도 걸쳐 있는지는 서브시스템에서 확인
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->NotifyShelterOverlapChanged(OtherActor);
		}
	}
}

bool ACSWeatherZoneActor::IsActorInJudgeBox(const AActor* Actor) const
{
	return IsValid(JudgeBox) && IsValid(Actor) && JudgeBox->IsOverlappingActor(Actor);
}

void ACSWeatherZoneActor::ApplyZoneSystem(UNiagaraSystem* System)
{
	if (!IsValid(ZoneNiagara))
	{
		return;
	}

	if (System == nullptr)
	{
		ZoneNiagara->DeactivateImmediate();
		return;
	}

	// 현재 에셋과 다를 때만 에셋을 교체
	if (ZoneNiagara->GetAsset() != System)
	{
		ZoneNiagara->SetAsset(System);
	}

	// 같은 에셋이어도 호출 시 재생을 초기화
	// 표시 여부는 SetZoneVisible에서 별도로 제어
	ZoneNiagara->Activate(true);
}

void ACSWeatherZoneActor::SetZoneVisible(bool bVisible)
{
	if (IsValid(ZoneNiagara))
	{
		ZoneNiagara->SetVisibility(bVisible);
	}
}

void ACSWeatherZoneActor::UpdateZoneNiagaraTransform()
{
	if (!IsValid(VisualBox) || !IsValid(ZoneNiagara))
	{
		return;
	}

	// 연출 박스의 월드 변환과 스케일이 반영된 반쪽 크기를 조회
	const FTransform& BoxTransform = VisualBox->GetComponentTransform();
	const FVector ScaledExtent = VisualBox->GetScaledBoxExtent();

	// 박스 로컬 윗면 중앙을 월드 좌표로 변환
	// 박스의 위치, 회전, 스케일을 함께 반영
	const FVector TopCenter = BoxTransform.TransformPosition(
		FVector(0.0, 0.0, VisualBox->GetUnscaledBoxExtent().Z));

	// 박스 전체 너비를 원본 생성 평면 크기로 나누어 X·Y 배율을 계산
	// 분모는 최소 1로 제한하고 Z 배율은 1로 유지
	const FVector PlaneScale(
		ScaledExtent.X * 2.0 / FMath::Max(ZoneAssetPlaneSize.X, 1.0),
		ScaledExtent.Y * 2.0 / FMath::Max(ZoneAssetPlaneSize.Y, 1.0),
		1.0);

	// 생성 평면에 계산한 월드 위치, 회전, 스케일을 적용

	ZoneNiagara->SetWorldLocationAndRotation(TopCenter, BoxTransform.GetRotation());
	ZoneNiagara->SetWorldScale3D(PlaneScale);
}
