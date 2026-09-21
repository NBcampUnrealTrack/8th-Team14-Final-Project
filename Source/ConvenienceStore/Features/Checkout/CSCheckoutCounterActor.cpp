#include "Features/Checkout/CSCheckoutCounterActor.h"

#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "Features/Items/CSProductPickupActor.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Data/CSProductTypes.h"
#include "UI/Checkout/CSCheckoutWidget.h"

namespace
{
void PlayCheckoutGameplaySFX(const AActor* WorldContextActor, ECSAudioEvent AudioEvent)
{
	if (!IsValid(WorldContextActor))
	{
		return;
	}

	if (UGameInstance* GameInstance = WorldContextActor->GetGameInstance();
		IsValid(GameInstance))
	{
		if (UCSAudioSubsystem* AudioSubsystem =
			GameInstance->GetSubsystem<UCSAudioSubsystem>(); IsValid(AudioSubsystem))
		{
			AudioSubsystem->PlaySFX(AudioEvent);
		}
	}
}
}

ACSCheckoutCounterActor::ACSCheckoutCounterActor()
{
	// 플레이어 계산 UI와 달리 직원 계산은 계산대 위 상품을 시간차로 자동 스캔하므로 Tick을 사용합니다.
	PrimaryActorTick.bCanEverTick = true;

	// 계산대의 모든 구성 요소가 공유할 루트 컴포넌트를 생성합니다.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 계산대 외형은 블루프린트에서 자유롭게 지정할 수 있도록 빈 메시 컴포넌트로 둡니다.
	CounterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CounterMesh"));
	CounterMesh->SetupAttachment(SceneRoot);

	// 실제 구도는 계산대 블루프린트에서 미리 보며 조정하고, C++은 전환 시점만 관리합니다.
	CheckoutCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CheckoutCamera"));
	CheckoutCamera->SetupAttachment(SceneRoot);
	CheckoutCamera->SetAutoActivate(false);

	// 직원 구역은 블루프린트에서 계산대 뒷편으로 옮겨 조정할 수 있도록 별도 BoxCollision Component로 구성합니다.
	CashierInteractionZone = CreateDefaultSubobject<UBoxComponent>(TEXT("CashierInteractionZone"));
	CashierInteractionZone->SetupAttachment(SceneRoot);
	CashierInteractionZone->SetBoxExtent(FVector(150.0f, 160.0f, 120.0f));
	CashierInteractionZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CashierInteractionZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	CashierInteractionZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CashierInteractionZone->SetGenerateOverlapEvents(true);

	// 화살표의 시작점은 계산 중인 첫 번째 손님의 위치이며, 화살표 정면은 대기열 방향입니다.
	QueueStartPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("QueueStartPoint"));
	QueueStartPoint->SetupAttachment(SceneRoot);
	QueueStartPoint->SetHiddenInGame(true);

	// 화살표 위치를 계산대 상판에 맞추면 상품 표시 위치와 배치 방향을 에디터에서 조정할 수 있습니다.
	CheckoutDisplayOrigin = CreateDefaultSubobject<UArrowComponent>(TEXT("CheckoutDisplayOrigin"));
	CheckoutDisplayOrigin->SetupAttachment(SceneRoot);
	CheckoutDisplayOrigin->SetHiddenInGame(true);

	// 대기줄 자리를 건축 판정에 알리기 위한 박스입니다. 진열대 BrowseZoneBox와 같은 구성입니다.
	QueueZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("QueueZoneBox"));
	QueueZoneBox->SetupAttachment(SceneRoot);
	QueueZoneBox->ComponentTags.Add(TEXT("BuildPlacementAccessZone"));
	// 충돌에는 일절 관여하지 않습니다. 조준·상호작용 트레이스가 이 박스를 잡으면 안 됩니다.
	QueueZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	QueueZoneBox->SetGenerateOverlapEvents(false);
	QueueZoneBox->SetCanEverAffectNavigation(false);
	// 편집 중에만 보이는 표시물입니다.
	QueueZoneBox->SetHiddenInGame(true);
	// 에디터에서 QueueStartPoint와 같은 색으로 두어 어느 화살표에서 파생된 영역인지 알 수 있게 합니다.
	QueueZoneBox->ShapeColor = FColor(255, 0, 0);
}

void ACSCheckoutCounterActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 간격이 제일 큰 손님(오크) 지름 150보다 좁으면 캐릭터가 겹침
	QueueSlotSpacing = FMath::Max(QueueSlotSpacing, 150.0f);
	// 인접 슬롯 영역이 겹치면 1번이 0번자리 도착으로 판정될 수 있으므로 간격의 45%로 반경 제한
	QueueSlotRadius = FMath::Clamp(QueueSlotRadius, 31.0f, QueueSlotSpacing * 0.45f);

	// 간격/정원 변경 시 건축 영역 박스도 즉시 갱신
	SyncQueueZoneBox();
}

void ACSCheckoutCounterActor::BeginPlay()
{
	Super::BeginPlay();

	// 월드에서 손님 흐름 Subsystem을 찾아 현재 계산대를 대기열 관리 대상으로 등록합니다.
	if (UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>())
	{
		CustomerSubsystem->RegisterCounter(this);
	}

	// 영업 단계가 바뀌었을 때 진행 중인 계산을 정리할 수 있도록 GameState 이벤트를 구독합니다.
	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnStorePhaseChanged.AddDynamic(
			this, &ACSCheckoutCounterActor::HandleStorePhaseChanged);
	}

	// 계산대는 시설 BP가 BeginPlay에서 스폰한 뒤 소켓에 붙이므로, 이 시점에는 아직 부착 전이라
	// 부모의 배율이 반영되지 않았습니다. 부착이 끝난 다음 틱에 영역 박스를 다시 계산합니다.
	GetWorld()->GetTimerManager().SetTimerForNextTick(
		this, &ACSCheckoutCounterActor::SyncQueueZoneBox);
}

void ACSCheckoutCounterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		// 계산대가 제거될 때 손님 Subsystem이 사라진 계산대를 계속 참조하지 않게 합니다.
		if (UCSCustomerRuntimeSubsystem* CustomerSubsystem =
			World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			CustomerSubsystem->UnregisterCounter(this);
		}
	}

	if (IsValid(StoreGameState))
	{
		StoreGameState->OnStorePhaseChanged.RemoveDynamic(
			this, &ACSCheckoutCounterActor::HandleStorePhaseChanged);
	}

	// 레벨 종료나 계산대 제거 시 UI와 입력 상태가 남지 않도록 정리합니다.
	EndWaitForNextCustomer();
	CancelEmployeeCheckoutPresentation();
	CloseCheckoutWidget(true);

	Super::EndPlay(EndPlayReason);
}

void ACSCheckoutCounterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateEmployeeCheckoutPresentation(DeltaSeconds);
}

FVector ACSCheckoutCounterActor::GetQueueSlotLocation(const int32 Index) const
{
	return GetQueueSlotLocationForTransform(Index, GetActorTransform());
}

FVector ACSCheckoutCounterActor::GetQueueSlotLocationForTransform(const int32 Index, const FTransform& CounterTransform) const
{
	// 음수 순서 또는 누락된 대기열 시작점은 유효하지 않으므로 영벡터를 반환
	if (Index < 0 || !IsValid(QueueStartPoint))
	{
		return FVector::ZeroVector;
	}

	// 화살표를 계산대 기준 상대 트랜스폼으로 풀어, 액터 이동 없이도 건축 미리보기 판정 가능
	const FTransform StartTransform =
		QueueStartPoint->GetComponentTransform().GetRelativeTransform(GetActorTransform())
		* CounterTransform;

	// 0번 손님은 QueueStartPoint 위치에 서고, 이후 손님은 화살표 정면 방향으로 일정 간격만큼 배치합니다.
	// 전방 벡터는 정규화된 값이므로 간격은 계산대 배율의 영향을 받지 않습니다.
	return StartTransform.GetLocation()+ StartTransform.GetRotation().GetForwardVector() * QueueSlotSpacing * static_cast<float>(Index);
}

bool ACSCheckoutCounterActor::IsQueueNavigationTrustworthy() const
{
	// GetCurrent는 UWorld* / UObject* 오버로드만 있어서 const 포인터로는 매칭되지 않습니다.
	UWorld* World = GetWorld();
	const UNavigationSystemV1* NavSys =
		World != nullptr ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	if (NavSys == nullptr)
	{
		return false;
	}

	// 에디터 화면의 "*** NAVMESH NEEDS TO BE REBUILT ***"와 같은 조건입니다.
	// 타일 데이터가 아예 없거나 리빌드가 밀려 있으면 투영 결과를 신뢰할 수 없습니다.
	const ANavigationData* NavData = NavSys->GetDefaultNavDataInstance();
	if (NavData == nullptr || NavData->NeedsRebuild())
	{
		return false;
	}

	// 생성 작업이 아직 남아 있으면 그 자리의 타일이 비어 있을 수 있습니다.
	return NavSys->GetNumRemainingBuildTasks() <= 0 &&
		NavSys->GetNumRunningBuildTasks() <= 0;
}

bool ACSCheckoutCounterActor::TryProjectQueueSlotToFloor(const FVector& SlotLocation,float& OutFloorZ) const
{
	OutFloorZ = SlotLocation.Z;


	UWorld* World = GetWorld();
	const UNavigationSystemV1* NavSys = nullptr;
	if (World != nullptr)
	{
		NavSys = UNavigationSystemV1::GetCurrent(World);
	}
	else
	{
		NavSys = nullptr;
	}
	if (NavSys == nullptr || SlotLocation.ContainsNaN())
	{
		return false;
	}


    // XY 허용치를 도착 판정 반경과 똑같이 줌
	const float HorizontalTolerance = FMath::Max(QueueSlotRadius, 1.0f);
	const FVector QueryExtent(HorizontalTolerance, HorizontalTolerance, 300.0f);

	FNavLocation Projected;
	if (!NavSys->ProjectPointToNavigation(SlotLocation, Projected, QueryExtent))
	{
		// 바닥이 없으면 도달 불가 슬롯
		return false;
	}

	OutFloorZ = Projected.Location.Z;
	return true;
}

bool ACSCheckoutCounterActor::TryGetQueueSlotZone(
	const int32 Index,
	FVector& OutCenter,
	float& OutRadius) const
{
	OutCenter = FVector::ZeroVector;
	OutRadius = 0.0f;

	if (Index < 0 || !IsValid(QueueStartPoint))
	{
		return false;
	}

	const FVector SlotLocation = GetQueueSlotLocation(Index);

	// 슬롯의 XY는 고정하고 Z만 NavMesh 높이로 맞춥니다.
	// 진열대 도착 판정과 같은 이유입니다. 화살표 높이와 손님 발바닥 높이가 다를 수 있습니다.
	float FloorZ = 0.0f;
	if (!TryProjectQueueSlotToFloor(SlotLocation, FloorZ))
	{
		return false;
	}

	OutCenter = FVector(SlotLocation.X, SlotLocation.Y, FloorZ);
	OutRadius = QueueSlotRadius;
	return true;
}

bool ACSCheckoutCounterActor::IsPointInsideQueueSlotZone(
	const int32 Index,
	const FVector& WorldPoint) const
{
	FVector Center;
	float Radius = 0.0f;
	if (!TryGetQueueSlotZone(Index, Center, Radius))
	{
		// 실패 원인이 두 가지라 구분
		// 1) NavMesh를 신뢰할 수 없는 상태
		// 2) NavMesh는 정상인데 그 자리에 바닥이 없음
		return !IsQueueNavigationTrustworthy();
	}

    // 종족마다 캡슐 높이가 달라 Z축 거리가 흔들리는 것을 막기 위해 XY 평면 거리만 판정
	return FVector::Dist2D(WorldPoint, Center) <= Radius;
}

bool ACSCheckoutCounterActor::TryGetQueueSlotFacing(
	const int32 Index,
	FRotator& OutFacing) const
{
	OutFacing = FRotator::ZeroRotator;
	if (Index < 0 || !IsValid(QueueStartPoint))
	{
		return false;
	}

	// 0번 손님은 계산 카메라에 정면이 잡히도록 계산대를 보게 할 수 있습니다.
	// 기준점은 손님의 상품이 올라가는 자리라서, 계산대 BP가 상판을 옮겨도 시선이 따라갑니다.
	if (Index == 0 && bFrontCustomerFacesCounter)
	{
		const USceneComponent* FacingTarget = CheckoutDisplayOrigin;
		if (!IsValid(FacingTarget))
		{
			FacingTarget = SceneRoot;
		}
		if (IsValid(FacingTarget))
		{
			const FVector ToCounter =
				FacingTarget->GetComponentLocation() - GetQueueSlotLocation(0);
			// 상판이 슬롯 바로 위에 있는 비정상 배치에서는 방향을 만들 수 없으므로 전진 방향으로 폴백합니다.
			if (ToCounter.SizeSquared2D() > UE_KINDA_SMALL_NUMBER)
			{
				OutFacing = FRotator(0.0f, ToCounter.Rotation().Yaw, 0.0f);
				return true;
			}
		}
	}

	// 나머지 손님은 줄이 줄어드는 쪽, 즉 화살표 전방의 역방향을 봅니다.
	const FVector ForwardDirection = QueueStartPoint->GetForwardVector();
	OutFacing = FRotator(0.0f, (-ForwardDirection).Rotation().Yaw, 0.0f);
	return true;
}

float ACSCheckoutCounterActor::GetQueueSlotStopRadius() const
{
	// 제일 큰 손님(오크)의 몸 반지름입니다. QueueSlotSpacing 최소값 150을 정한 근거와 같은 값입니다.
	constexpr float MaxCustomerBodyRadius = 75.0f;
	// 상한이 0까지 내려가면 이동 명령이 성립하지 않으므로 최소한의 여유를 남깁니다.
	constexpr float MinStopRadius = 10.0f;

	// 지켜야 하는 불변식은 두 가지입니다.
	//
	// 1) 정지 반경 <= 도착 판정 반경(QueueSlotRadius)
	//    반대가 되면 "이동은 완료인데 도착 판정은 안 되는" 구간이 생겨 손님이 영구히 멈춥니다.
	//
	// 2) 정지 반경 <= QueueSlotSpacing / 2 - 손님 몸 반지름
	//    앞뒤 손님이 서로를 향해 각각 R만큼 치우쳐 멈추면 사이 간격이 Spacing - 2R까지 좁아집니다.
	//    이 값이 몸 지름(150) 아래로 내려가면 몸이 겹칩니다.
	//    손님끼리는 충돌도 회피도 하지 않아 겹침을 막는 장치가 간격 하나뿐이라 여기서 막아야 합니다.
	//
	// 간격과 판정 반경 모두 에디터에서 바뀌는 값이라 상한을 상수로 두지 않고 호출 때마다 유도합니다.
	const float SpacingLimit = QueueSlotSpacing * 0.5f - MaxCustomerBodyRadius;
	const float UpperBound = FMath::Max(FMath::Min(QueueSlotRadius, SpacingLimit), MinStopRadius);

	return FMath::Clamp(QueueSlotStopRadius, MinStopRadius, UpperBound);
}

bool ACSCheckoutCounterActor::TryGetQueueEntryZone(const int32 CommittedCount,FVector& OutCenter,float& OutRadius) const
{
	OutCenter = FVector::ZeroVector;
	OutRadius = 0.0f;

	// 확정된 인원 다음의 빈 슬롯을 집결 지점으로 선택
	// 슬롯 범위를 벗어나지 않도록 인덱스를 제한
	const int32 EntryIndex = FMath::Clamp(CommittedCount, 0, GetConfiguredQueueSlotCount() - 1);

	float SlotRadius = 0.0f;
	if (!TryGetQueueSlotZone(EntryIndex, OutCenter, SlotRadius))
	{
		// 슬롯 영역을 구하지 못하면 집결 지점 조회 실패로 처리
		return false;
	}

	// 슬롯 반경을 최소값으로 보장하고 슬롯 간격의 절반을 상한으로 설정
	// 슬롯 반경이 간격의 절반보다 크면 상한도 슬롯 반경으로 조정
	const float UpperBound = FMath::Max(QueueSlotSpacing * 0.5f, SlotRadius);
	OutRadius = FMath::Clamp(QueueEntryRadius, SlotRadius, UpperBound);
	return true;
}

bool ACSCheckoutCounterActor::IsPointInsideQueueEntryZone(const int32 CommittedCount,const FVector& WorldPoint) const
{
	FVector Center;
	float Radius = 0.0f;
	if (!TryGetQueueEntryZone(CommittedCount, Center, Radius))
	{
		// 내비게이션을 신뢰할 수 없으면 순번 확정이 막히지 않도록 판정을 통과
		// 내비게이션이 정상인데 영역 조회에 실패하면 도달 불가로 처리
		return !IsQueueNavigationTrustworthy();
	}

	// 높이 차이를 제외한 XY 평면 거리로 집결 지점 내부 여부를 판정
	return FVector::Dist2D(WorldPoint, Center) <= Radius;
}

int32 ACSCheckoutCounterActor::GetConfiguredQueueSlotCount() const
{
	return FMath::Max(MaxQueueSlots + BonusQueueSlots, 1);
}

int32 ACSCheckoutCounterActor::GetEffectiveMaxQueueLength() const
{
	// 요청한 만큼만 검사합니다. 매 프레임 도는 함수가 아니라 비용은 문제되지 않습니다.
	const int32 Requested = GetConfiguredQueueSlotCount();
	return FMath::Min(Requested, CountUsableQueueSlots(Requested, GetActorTransform()));
}

void ACSCheckoutCounterActor::AddBonusQueueSlots(const int32 Delta)
{
	BonusQueueSlots = FMath::Max(BonusQueueSlots + Delta, 0);

	// 정원이 바뀌면 건축 금지 영역도 같이 바뀌어야 판정이 어긋나지 않습니다.
	SyncQueueZoneBox();
}

bool ACSCheckoutCounterActor::AreQueueSlotsUsableAt(const FTransform& CounterTransform) const
{
	const int32 Requested = GetConfiguredQueueSlotCount();
	return CountUsableQueueSlots(Requested, CounterTransform) >= Requested;
}

int32 ACSCheckoutCounterActor::CountUsableQueueSlots(
	const int32 SearchLimit,
	const FTransform& CounterTransform) const
{
	if (!IsValid(QueueStartPoint) || !IsQueueNavigationTrustworthy())
	{
		// NavMesh 신뢰 불가 시 데이터 요구값 그대로 반환
		return SearchLimit;
	}

	for (int32 Index = 0; Index < SearchLimit; ++Index)
	{
		const FVector SlotLocation = GetQueueSlotLocationForTransform(Index, CounterTransform);

		float FloorZ = 0.0f;
		if (!TryProjectQueueSlotToFloor(SlotLocation, FloorZ))
		{
			// 걸을 수 있는 바닥이 끊기면 즉시 중단 (중간이 끊긴 줄은 무의미)
			return Index;
		}

		// 다른 시설물 몸통(BuildPlacementCollision)에 막혀있으면 즉시 중단
		if (IsQueueSlotBlockedByFacility(FVector(SlotLocation.X, SlotLocation.Y, FloorZ)))
		{
			return Index;
		}
	}

	return SearchLimit;
}

bool ACSCheckoutCounterActor::IsQueueSlotBlockedByFacility(const FVector& SlotCenter) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	// 충돌 검사 파라미터 설정 (자기 자신은 기본적으로 제외)
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CSQueueSlotBlocked), false);
	QueryParams.AddIgnoredActor(this);

	// 부모 시설물 및 부착된 모든 액터 제외 (CounterDesk에 있어서)
	// 0번 자리는 항상 계산대 자신의 충돌체 안쪽에 있으므로, 자기 시설과 딸린 액터들을 모두 예외 처리하지 않으면 항상 막힌 것으로 오판합니다.
	const AActor* OwningFacility = this;
	while (OwningFacility->GetAttachParentActor() != nullptr)
	{
		OwningFacility = OwningFacility->GetAttachParentActor(); // 최상위 부모(시설 본체) 찾기
	}
	if (OwningFacility != this)
	{
		QueryParams.AddIgnoredActor(OwningFacility); // 본체 제외
		TArray<AActor*> AttachedActors;
		OwningFacility->GetAttachedActors(AttachedActors, true, true);
		QueryParams.AddIgnoredActors(AttachedActors); // 본체에 붙은 모든 자식 액터 제외
	}

	// 구형(Sphere) 오버랩 검사 실행
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		SlotCenter,
		FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
		FCollisionShape::MakeSphere(FMath::Max(QueueSlotRadius, 1.0f)),
		QueryParams);

	// 검색된 결과 필터링
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UBoxComponent* OtherBox = Cast<UBoxComponent>(Overlap.Component.Get());
		const AActor* OtherFacility = IsValid(OtherBox) ? OtherBox->GetOwner() : nullptr;
		// 겹친 컴포넌트가 Box이고, "BuildPlacementCollision" 태그가 있으며,
		// 그 주인이 "UCSBuildableFacility" 이라면 자리가 막힌 것으로 확정
		if (IsValid(OtherBox) && OtherBox->ComponentHasTag(TEXT("BuildPlacementCollision")) && IsValid(OtherFacility) && OtherFacility->Implements<UCSBuildableFacility>())
		{
			return true;
		}
	}

	return false;
}

void ACSCheckoutCounterActor::SyncQueueZoneBox()
{
	if (!IsValid(QueueZoneBox) || !IsValid(QueueStartPoint))
	{
		return;
	}

	// 대기줄의 총 길이와 반폭(HalfLength) 계산
	// 진열대가 원형 도착 판정에 외접하는 박스를 만드는 것과 같은 계산
	const int32 SlotCount = GetConfiguredQueueSlotCount();
	const float LineLength = QueueSlotSpacing * static_cast<float>(SlotCount - 1);
	const float HalfLength = LineLength * 0.5f + QueueZoneHalfWidth;

	// 화살표의 상대 위치와 회전값 가져오기
	const FVector StartLocal = QueueStartPoint->GetRelativeLocation();
	const FRotator StartRotationLocal = QueueStartPoint->GetRelativeRotation();

	// 부모의 Scale 보정
	const USceneComponent* ZoneParent = QueueZoneBox->GetAttachParent();

	FVector ParentScale;

	// ZoneParent 컴포넌트가 유효한지(nullptr이 아니며 소멸 대기 상태가 아닌지) 검사합니다.
	if (IsValid(ZoneParent))
	{
		// 유효하다면 부모 컴포넌트의 배율(Scale)을 가져와 절대값(Abs)으로 만듭니다.
		ParentScale = ZoneParent->GetComponentScale().GetAbs();
	}
	else
	{
		// 유효하지 않다면(부모가 없다면) 기본 배율인 (1.0, 1.0, 1.0)을 적용합니다.
		ParentScale = FVector::OneVector;
	}

	const FVector SafeScale(
		ParentScale.X > UE_KINDA_SMALL_NUMBER ? ParentScale.X : 1.0f,
		ParentScale.Y > UE_KINDA_SMALL_NUMBER ? ParentScale.Y : 1.0f,
		ParentScale.Z > UE_KINDA_SMALL_NUMBER ? ParentScale.Z : 1.0f);

	// QueueSlotSpacing은 정규화된 전방 벡터에 곱하는 "월드" 길이
	// 상대 좌표로 옮길 때 배율로 나눠 주지 않으면 박스가 슬롯 좌표보다 배율만큼 멀리 나감
	// 로컬 전방 벡터를 구해 박스 중심점을 계산 (배율로 나눠서 월드 길이를 로컬 길이로 환산)
	const FVector ForwardLocal = StartRotationLocal.Vector();
	const FVector HalfLineLocal = ForwardLocal * (LineLength * 0.5f) / SafeScale;


	// 박스 밑면을 손님이 서는 '바닥'으로 내리기
	const float BottomLocalZ = StartLocal.Z - GetQueueZoneFloorDropLocal(SafeScale.Z);

	// 상대 좌표(Relative)로만 박스의 위치, 회전, 크기 지정
	QueueZoneBox->SetRelativeLocation(FVector(
		StartLocal.X + HalfLineLocal.X,
		StartLocal.Y + HalfLineLocal.Y,
		BottomLocalZ + (QueueZoneHeight * 0.5f) / SafeScale.Z));
	QueueZoneBox->SetRelativeRotation(FRotator(0.0f, StartRotationLocal.Yaw, 0.0f));

	// 월드 길이로 계산한 값을 배율로 되돌립니다. 박스의 상대 배율은 1이므로
	// 부모 배율이 곧 컴포넌트 배율입니다.
	QueueZoneBox->SetBoxExtent(
		FVector(HalfLength, QueueZoneHalfWidth, QueueZoneHeight * 0.5f) / SafeScale);
}

float ACSCheckoutCounterActor::GetQueueZoneFloorDropLocal(const float ParentScaleZ) const
{
	// 계산대 상판에 있는 화살표 높이와 실제 NavMesh 바닥 높이의 차이를 구하는 함수
	if (!IsValid(QueueStartPoint) || ParentScaleZ <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// 0번 슬롯의 NavMesh 바닥 높이를 구함
	float FloorZ = 0.0f;
	if (!TryProjectQueueSlotToFloor(GetQueueSlotLocation(0), FloorZ))
	{
		return 0.0f;
	}

	const float ArrowWorldZ = QueueStartPoint->GetComponentLocation().Z;
	// 월드 높이 차이를 부모 Z 스케일로 나눠 상대 좌표계의 길이로 변환
	return (ArrowWorldZ - FloorZ) / ParentScaleZ;
}

bool ACSCheckoutCounterActor::CanProcessCheckout() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	// 매장 단계 규칙은 GameState가 관리하므로, 계산대는 결제 허용 여부만 조회합니다.
	const ACSStoreGameState* CurrentStoreGameState = World->GetGameState<ACSStoreGameState>();
	return IsValid(CurrentStoreGameState) && CurrentStoreGameState->bCanCheckout;
}

void ACSCheckoutCounterActor::SetCustomerBeingServed(ACSCustomerCharacter* Customer,const bool bIsServed)
{
	UCSCustomerStateComponent* CustomerState =
		IsValid(Customer) ? Customer->GetCustomerState() : nullptr;
	if (IsValid(CustomerState))
	{
		CustomerState->bIsBeingServed = bIsServed;
	}
}

bool ACSCheckoutCounterActor::CanEmployeeProcessCheckout() const
{
	const UWorld* World = GetWorld();
	const UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;

	// 직원 계산은 플레이어 UI 계산과 동시에 실행되면 안 되므로, UI 전환 중인 경우도 명시적으로 제외합니다.
	// 손님이 0번 자리에 도착하기 전에는 계산 대상이 아닙니다(빈 계산대 앞에서 스캔 연출이 돌면 안 됩니다).
	return CanProcessCheckout() && !IsValid(ActiveCheckoutWidget) && !bIsCheckoutTransitioning &&
		!bIsEmployeeCheckoutPresenting &&
		IsValid(CustomerSubsystem) && IsValid(CustomerSubsystem->GetCheckoutReadyCustomer());
}

bool ACSCheckoutCounterActor::TryProcessEmployeeCheckout()
{
	// 계산 연출은 Tick에서 한 품목씩 진행한다. 진행 중인 계산을 다시 호출해
	// 즉시 완료시키지 않도록 새 계산 시작 요청만 허용한다.
	return !bIsEmployeeCheckoutPresenting && BeginEmployeeCheckoutPresentation();
}

bool ACSCheckoutCounterActor::BeginEmployeeCheckoutPresentation()
{
	if (!CanEmployeeProcessCheckout())
	{
		return false;
	}

	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	ACSStoreGameState* CurrentStoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	ACSCustomerCharacter* FrontCustomer =
		IsValid(CustomerSubsystem) ? CustomerSubsystem->GetCheckoutReadyCustomer() : nullptr;
	if (!IsValid(FrontCustomer) || !IsValid(CurrentStoreGameState) ||
		!IsCustomerOrderCheckoutEligible(FrontCustomer, CurrentStoreGameState))
	{
		return false;
	}

	int32 TotalSalePrice = 0;
	if (!BuildFrontCustomerOrder(ActiveEmployeeCheckoutOrder, TotalSalePrice) ||
		ActiveEmployeeCheckoutOrder.IsEmpty())
	{
		ActiveEmployeeCheckoutOrder.Reset();
		return false;
	}

	ActiveCheckoutCustomer = FrontCustomer;
	bCheckoutPaymentCompleted = false;
	bLastEmployeeCheckoutSucceeded = false;
	bIsEmployeeCheckoutPresenting = true;
	EmployeeScanElapsedSeconds = 0.0f;
	// 응대가 시작된 손님은 스캔이 늦어져도 인내심 만료로 이탈하지 않아야 합니다.
	SetCustomerBeingServed(ActiveCheckoutCustomer, true);
	SetCheckoutCustomerBasketVisible(false);
	SpawnCheckoutDisplayProducts();

	UE_LOG(LogTemp, Log,
		TEXT("[CheckoutCounter] 직원 계산 연출을 시작했습니다: 손님=%s, 표시 상품=%d개"),
		*FrontCustomer->GetName(), CheckoutDisplayProducts.Num());
	return true;
}

void ACSCheckoutCounterActor::UpdateEmployeeCheckoutPresentation(float DeltaSeconds)
{
	if (!bIsEmployeeCheckoutPresenting)
	{
		return;
	}

	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	if (!CanProcessCheckout() || !IsValid(CustomerSubsystem) ||
		CustomerSubsystem->GetFrontCustomer() != ActiveCheckoutCustomer)
	{
		// 매장 단계 변경, 손님 이탈 등으로 연출을 끝낼 수 없으면 장바구니를 복원하고 거래를 남기지 않습니다.
		CancelEmployeeCheckoutPresentation();
		return;
	}

	EmployeeScanElapsedSeconds += DeltaSeconds;
	if (EmployeeScanElapsedSeconds < EmployeeScanIntervalSeconds)
	{
		return;
	}
	EmployeeScanElapsedSeconds = 0.0f;

	if (!CheckoutDisplayProductIds.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("[CheckoutCounter] 직원 계산 스캔: 고객=%s, 상품=%s, 남은 표시 상품=%d개"),
			*GetNameSafe(ActiveCheckoutCustomer.Get()),
			*CheckoutDisplayProductIds[0].ToString(),
			CheckoutDisplayProductIds.Num() - 1);
		HandleCheckoutItemScanned(CheckoutDisplayProductIds[0]);
		return;
	}

	CompleteEmployeeCheckoutPresentation();
}

bool ACSCheckoutCounterActor::CompleteEmployeeCheckoutPresentation()
{
	if (!bIsEmployeeCheckoutPresenting)
	{
		return bLastEmployeeCheckoutSucceeded;
	}

	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	ACSStoreGameState* CurrentStoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	ACSCustomerCharacter* FrontCustomer =
		IsValid(CustomerSubsystem) ? CustomerSubsystem->GetFrontCustomer() : nullptr;
	if (!CanProcessCheckout() || !IsValid(CurrentStoreGameState) ||
		!IsValid(FrontCustomer) || FrontCustomer != ActiveCheckoutCustomer ||
		ActiveEmployeeCheckoutOrder.IsEmpty() ||
		!IsCustomerOrderCheckoutEligible(FrontCustomer, CurrentStoreGameState) ||
		!CurrentStoreGameState->RecordCheckoutTransaction(ActiveEmployeeCheckoutOrder))
	{
		CancelEmployeeCheckoutPresentation();
		return false;
	}

	ClearCheckoutDisplayProducts();
	FrontCustomer->ShowShoppingBag();
	bCheckoutPaymentCompleted = true;
	bLastEmployeeCheckoutSucceeded = true;
	bIsEmployeeCheckoutPresenting = false;
	EmployeeScanElapsedSeconds = 0.0f;
	ActiveEmployeeCheckoutOrder.Reset();
	SetCustomerBeingServed(ActiveCheckoutCustomer, false);
	CustomerSubsystem->CompleteFrontCheckout();
	ActiveCheckoutCustomer = nullptr;
	PlayCheckoutGameplaySFX(this, ECSAudioEvent::CheckoutComplete);

	UE_LOG(LogTemp, Log, TEXT("[CheckoutCounter] 직원 계산 연출과 결제를 완료했습니다."));
	return true;
}

void ACSCheckoutCounterActor::CancelEmployeeCheckoutPresentation()
{
	ClearCheckoutDisplayProducts();
	if (!bCheckoutPaymentCompleted)
	{
		SetCheckoutCustomerBasketVisible(true);
	}

	SetCustomerBeingServed(ActiveCheckoutCustomer, false);
	ActiveCheckoutCustomer = nullptr;
	bCheckoutPaymentCompleted = false;
	bIsEmployeeCheckoutPresenting = false;
	bLastEmployeeCheckoutSucceeded = false;
	EmployeeScanElapsedSeconds = 0.0f;
	ActiveEmployeeCheckoutOrder.Reset();
}

bool ACSCheckoutCounterActor::IsCustomerOrderCheckoutEligible(
	const ACSCustomerCharacter* Customer,
	const ACSStoreGameState* CurrentStoreGameState) const
{
	const UWorld* World = GetWorld();
	const UCSCustomerStateComponent* CustomerState =
		IsValid(Customer) ? Customer->GetCustomerState() : nullptr;
	if (!IsValid(World) || !IsValid(CustomerState) || !IsValid(CurrentStoreGameState))
	{
		return false;
	}

	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		CurrentStoreGameState->GetFreshnessSettings();
	for (const FCSSelectProduct& SelectedProduct : CustomerState->SelectProducts)
	{
		if (SelectedProduct.ProductRuntimeState.IsExpired(CurrentStoreGameState->CurrentBusinessDay) ||
			(IsValid(FreshnessSettings) &&
				!SelectedProduct.ProductRuntimeState.HasFreshnessRemaining(
					World->GetTimeSeconds(),
					FreshnessSettings->DecayIntervalSeconds,
					FreshnessSettings->DecayAmountPerInterval)))
		{
			return false;
		}
	}

	return !CustomerState->SelectProducts.IsEmpty();
}

bool ACSCheckoutCounterActor::IsInCashierInteractionZone(const AActor* Actor) const
{
	// 계산대 블루프린트에서 구역 컴포넌트를 제거한 경우에는 실수로 어디서나 계산되지 않도록 막습니다.
	return IsValid(CashierInteractionZone) && IsValid(Actor) &&
		CashierInteractionZone->IsOverlappingActor(Actor);
}

bool ACSCheckoutCounterActor::TryGetCashierInteractionPoint(FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;
	if (!IsValid(CashierInteractionZone))
	{
		return false;
	}
	// 계산대가 시설의 자식 액터인 구성에서도, 플레이어가 들고 있는 계산대로 직원을 보내지 않습니다.
	for (const AActor* Facility = this; IsValid(Facility); Facility = Facility->GetAttachParentActor())
	{
		if (Facility->IsActorBeingDestroyed() || (Facility->Implements<UCSBuildableFacility>() &&
			!ICSBuildableFacility::Execute_IsFacilityInstalled(Facility)))
		{
			return false;
		}
	}

	// MoveToLocation에서 NavMesh에 투영하므로, 여기서는 BP에서 조정한 직원 구역의 중심을 그대로 제공합니다.
	OutLocation = CashierInteractionZone->GetComponentLocation();
	return true;
}

void ACSCheckoutCounterActor::GetCashierWaitingPoints(TArray<FVector>& OutLocations) const
{
	OutLocations.Reset();
	FVector Center;
	if (!TryGetCashierInteractionPoint(Center))
	{
		return;
	}
	OutLocations.Add(Center);
	const FVector Extent = CashierInteractionZone->GetUnscaledBoxExtent();
	const FTransform Transform = CashierInteractionZone->GetComponentTransform();
	for (int32 X = -2; X <= 2; ++X)
	{
		for (int32 Y = -2; Y <= 2; ++Y)
		{
			if (X != 0 || Y != 0)
			{
				OutLocations.Add(Transform.TransformPosition(FVector(Extent.X * X * 0.4f, Extent.Y * Y * 0.4f, 0.0f)));
			}
		}
	}
}

bool ACSCheckoutCounterActor::TryGetCashierInteractionFacing(
	const FVector& CashierLocation, FRotator& OutFacing) const
{
	OutFacing = FRotator::ZeroRotator;
	// 손님의 계산 방향과 마찬가지로 BP에서 조정한 상품 표시 위치를 기준으로 삼습니다.
	const FVector FacingLocation = IsValid(CheckoutDisplayOrigin)
		? CheckoutDisplayOrigin->GetComponentLocation()
		: GetActorLocation();
	FVector ToCounter = FacingLocation - CashierLocation;
	ToCounter.Z = 0.0f;
	if (ToCounter.IsNearlyZero())
	{
		return false;
	}

	OutFacing = ToCounter.Rotation();
	return true;
}

void ACSCheckoutCounterActor::Interact_Implementation(AActor* Interactor)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (!IsValid(CustomerSubsystem))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 손님 흐름 Subsystem을 찾을 수 없어 계산을 완료할 수 없습니다."));
		return;
	}

	// 준비·정산 등 계산이 허용되지 않은 단계에서는 손님 대기열을 변경하지 않습니다.
	if (!CanProcessCheckout())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CheckoutCounter] 현재 매장 단계에서는 계산을 진행할 수 없습니다."));
		return;
	}
	if (bIsEmployeeCheckoutPresenting)
	{
		UE_LOG(LogTemp, Log, TEXT("[CheckoutCounter] 직원이 계산 중이어서 플레이어 계산을 시작할 수 없습니다."));
		return;
	}

	// 손님 대기열 쪽이나 계산대 옆면에서는 계산 UI를 열 수 없게 직원 구역을 확인합니다.
	if (!IsInCashierInteractionZone(Interactor))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CheckoutCounter] 직원 구역 밖에서는 계산을 시작할 수 없습니다."));
		return;
	}

	// 계산할 손님이 없을 때는 대기열과 손님 상태를 변경하지 않습니다.
	if (!IsValid(CustomerSubsystem->GetFrontCustomer()))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CheckoutCounter] 계산 대기 중인 손님이 없습니다."));
		return;
	}

	// 줄 순번상 맨 앞이어도 0번 자리에 도착하기 전에는 계산 불가
	// 이 검사가 없으면 매장 반대편에서 걸어오는 손님의 결제가 바로 끝나버림
	if (!IsValid(CustomerSubsystem->GetCheckoutReadyCustomer()))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CheckoutCounter] 손님이 아직 계산대로 오는 중이라 계산을 시작할 수 없습니다."));
		return;
	}

	OpenCheckoutWidget(Interactor);
}

FText ACSCheckoutCounterActor::GetInteractionText_Implementation() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return FText::GetEmpty();
	}

	// 현재 단계에서 결제가 허용되지 않으면, 손님 유무와 관계없이 제한 안내를 표시합니다.
	if (!CanProcessCheckout())
	{
		return NSLOCTEXT("CheckoutCounter", "CheckoutUnavailable", "현재는 계산할 수 없습니다.");
	}
	if (bIsEmployeeCheckoutPresenting)
	{
		return NSLOCTEXT("CheckoutCounter", "EmployeeCheckoutInProgress", "직원이 계산 중입니다.");
	}

	// 문구는 예전처럼 두 가지뿐입니다. 계산이 가능할 때만 "계산 시작"으로 바뀝니다.
	// 줄에 손님이 있어도 아직 0번 자리에 도착하지 않았다면 "손님이 없습니다"와 같은 안내를 유지합니다.
	// 걸어오는 중이라는 별도 문구는 두지 않습니다.
	const UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (!IsValid(CustomerSubsystem) || !IsValid(CustomerSubsystem->GetCheckoutReadyCustomer()))
	{
		return NSLOCTEXT("CheckoutCounter", "NoCustomer", "계산 대기 중인 손님이 없습니다.");
	}

	return NSLOCTEXT("CheckoutCounter", "StartCheckout", "계산 시작");
}

FText ACSCheckoutCounterActor::GetInteractionTextForPlayer(const AActor* PlayerActor) const
{
	// 계산 불가 단계와 손님 부재 안내는 위치와 관계없이 기존 우선순위를 유지합니다.
	const FText DefaultText = GetInteractionText_Implementation();
	if (bIsEmployeeCheckoutPresenting)
	{
		return DefaultText;
	}

	if (!CanProcessCheckout())
	{
		return DefaultText;
	}

	const UWorld* World = GetWorld();
	const UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	// 손님이 없거나 아직 도착하지 않은 경우에는 그 사실이 위치 안내보다 먼저입니다.
	// 기존의 "손님 부재 안내가 위치 안내보다 우선"과 같은 우선순위입니다.
	if (!IsValid(CustomerSubsystem) || !IsValid(CustomerSubsystem->GetCheckoutReadyCustomer()))
	{
		return DefaultText;
	}

	if (!IsInCashierInteractionZone(PlayerActor))
	{
		return NSLOCTEXT("CheckoutCounter", "CashierZoneRequired", "계산대 반대편 직원 구역에 서야 합니다.");
	}

	return DefaultText;
}

bool ACSCheckoutCounterActor::BuildFrontCustomerOrder(
	TArray<FCSCheckoutLineItem>& OutLineItems,
	int32& OutTotalSalePrice) const
{
	OutLineItems.Reset();
	OutTotalSalePrice = 0;

	if (!IsValid(ProductMasterDataTable))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 상품 마스터 데이터 테이블이 설정되지 않아 주문을 생성할 수 없습니다."));
		return false;
	}

	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	// 여기가 막히면 OpenCheckoutWidget이 false를 반환하므로, 상호작용 검사를 우회하는
	// 새 경로가 나중에 생기더라도 도착하지 않은 손님의 결제가 성립하지 않습니다.
	const UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	ACSCustomerCharacter* FrontCustomer =
		IsValid(CustomerSubsystem) ? CustomerSubsystem->GetCheckoutReadyCustomer() : nullptr;
	if (!IsValid(FrontCustomer))
	{
		return false;
	}

	const UCSCustomerStateComponent* CustomerState = FrontCustomer->GetCustomerState();
	if (!IsValid(CustomerState) || !CustomerState->HasSelectedItems()) //[무송]이 부분 CustomerState->SelectItemIds.IsEmpty()에서 변경해서 State에서 함수를 만들어서 사용했습니다.
	{
		return false;
	}


	for (const FCSSelectProduct& SelectedProduct : CustomerState->SelectProducts)
	{
		const FName ProductId = SelectedProduct.ProductRow.RowName;
		// 비어 있는 상품 ID는 데이터 오류이므로 결제를 진행하지 않습니다.
		if (ProductId.IsNone() || SelectedProduct.SalePriceAtPickup < 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CheckoutCounter] 손님 구매 목록에 유효하지 않은 상품 ID 또는 판매가가 포함되어 있습니다."));
			OutLineItems.Reset();
			OutTotalSalePrice = 0;
			return false;
		}

		// 상품 ID는 상품 마스터 데이터 테이블의 RowName과 일치해야 합니다.
		const FCSProductDataRow* ProductData = ProductMasterDataTable->FindRow<FCSProductDataRow>(
			ProductId,
			TEXT("BuildFrontCustomerOrder"));
		if (ProductData == nullptr)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CheckoutCounter] 상품 마스터에서 RowName을 찾을 수 없습니다: %s"),
				*ProductId.ToString());
			OutLineItems.Reset();
			OutTotalSalePrice = 0;
			return false;
		}

		FCSCheckoutLineItem* LineItem = OutLineItems.FindByPredicate(
			[ProductId, &SelectedProduct](const FCSCheckoutLineItem& ExistingLineItem)
			{
				return ExistingLineItem.ProductId == ProductId &&
					ExistingLineItem.UnitSalePrice == SelectedProduct.SalePriceAtPickup;
			});
		if (LineItem != nullptr)
		{
			++LineItem->Quantity;
			LineItem->LineTotalSalePrice += SelectedProduct.SalePriceAtPickup;
			OutTotalSalePrice += SelectedProduct.SalePriceAtPickup;
			continue;
		}

		LineItem = &OutLineItems.AddDefaulted_GetRef();
		LineItem->ProductId = ProductId;
		LineItem->ItemName = ProductData->DisplayName.IsEmpty()
			? FText::FromName(ProductId)
			: ProductData->DisplayName;
		LineItem->Quantity = 1;
		LineItem->UnitSalePrice = SelectedProduct.SalePriceAtPickup;
		LineItem->UnitPurchasePrice = ProductData->PurchasePrice;
		LineItem->LineTotalSalePrice = SelectedProduct.SalePriceAtPickup;

		OutTotalSalePrice += SelectedProduct.SalePriceAtPickup;
	}

	return !OutLineItems.IsEmpty();
}

bool ACSCheckoutCounterActor::OpenCheckoutWidget(AActor* Interactor)
{
	// 이미 열린 계산 UI가 있으면 같은 주문을 중복으로 열지 않습니다.
	if (IsValid(ActiveCheckoutWidget))
	{
		return true;
	}
	if (bIsEmployeeCheckoutPresenting)
	{
		return false;
	}

	if (CheckoutWidgetClass == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 계산 UI 클래스가 설정되지 않아 계산을 시작할 수 없습니다."));
		return false;
	}

	TArray<FCSCheckoutLineItem> OrderItems;
	int32 TotalSalePrice = 0;
	if (!BuildFrontCustomerOrder(OrderItems, TotalSalePrice))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 손님 주문을 생성할 수 없어 계산을 시작하지 않습니다."));
		return false;
	}

	APawn* InteractingPawn = Cast<APawn>(Interactor);
	APlayerController* PlayerController =
		IsValid(InteractingPawn) ? Cast<APlayerController>(InteractingPawn->GetController()) : nullptr;
	if (!IsValid(PlayerController))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 계산 UI를 표시할 플레이어 컨트롤러를 찾을 수 없습니다."));
		return false;
	}

	// 계산대가 만든 주문 데이터를 위젯에 전달한 뒤 화면에 표시합니다.
	ActiveCheckoutWidget = CreateWidget<UCSCheckoutWidget>(PlayerController, CheckoutWidgetClass);
	if (!IsValid(ActiveCheckoutWidget))
	{
		return false;
	}

	ActiveCheckoutWidget->SetCheckoutOrder(OrderItems, TotalSalePrice);
	ActiveCheckoutWidget->OnPaymentRequested.AddDynamic(
		this, &ACSCheckoutCounterActor::HandleCheckoutPaymentRequested);
	ActiveCheckoutWidget->OnCheckoutCancelled.AddDynamic(
		this, &ACSCheckoutCounterActor::HandleCheckoutCancelled);
	ActiveCheckoutWidget->OnItemScanned.AddDynamic(
		this, &ACSCheckoutCounterActor::HandleCheckoutItemScanned);
	ActiveCheckoutWidget->OnReceiptExitFinished.AddDynamic(
		this, &ACSCheckoutCounterActor::HandleReceiptExitFinished);
	ActiveCheckoutWidget->AddToViewport();
	ActivateCheckoutCamera(PlayerController);
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	// 여기까지 왔다면 도착 게이트를 이미 통과했으므로 순번상 맨 앞 손님을 그대로 응대 대상으로 둡니다.
	ActiveCheckoutCustomer = IsValid(CustomerSubsystem)
		? CustomerSubsystem->GetFrontCustomer()
		: nullptr;
	bCheckoutPaymentCompleted = false;
	bIsCheckoutTransitioning = false;
	// 응대가 시작된 손님은 스캔이 늦어져도 인내심 만료로 이탈하지 않아야 합니다.
	SetCustomerBeingServed(ActiveCheckoutCustomer, true);
	SetCheckoutCustomerBasketVisible(false);

	// UI가 열린 주문과 같은 손님의 상품을 계산대 위에 표시합니다.
	SpawnCheckoutDisplayProducts();

	// 계산 UI가 열린 사실을 컨트롤러에 등록하여 단계 변경이 UI 입력을 게임 입력으로 덮어쓰지 않게 합니다.
	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(PlayerController))
	{
		StorePlayerController->SetStoreHUDHidden(this, true);
		StorePlayerController->SetGameplayUIInputLock(ActiveCheckoutWidget, true);
	}
	else
	{
		// 편의점 전용 컨트롤러가 아닌 테스트 환경에서도 계산 UI를 조작할 수 있게 기존 입력 전환을 유지합니다.
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(ActiveCheckoutWidget->TakeWidget());
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(true);
	}

	return true;
}

void ACSCheckoutCounterActor::HandleCheckoutPaymentRequested()
{
	// 완료 연출 중이거나 마지막 바코드음까지 끝나지 않은 위젯의 요청은 처리하지 않습니다.
	if (bIsCheckoutTransitioning || !IsValid(ActiveCheckoutWidget) ||
		!ActiveCheckoutWidget->CanCompleteCheckout())
	{
		return;
	}

	if (!CanProcessCheckout())
	{
		if (bIsEmployeeCheckoutPresenting)
		{
			CancelEmployeeCheckoutPresentation();
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 결제 도중 매장 단계가 변경되어 계산을 완료할 수 없습니다."));
		return;
	}

	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	if (!IsValid(CustomerSubsystem) || !IsValid(CustomerSubsystem->GetCheckoutReadyCustomer()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 결제를 완료할 대기 손님을 찾을 수 없습니다."));
		CloseCheckoutWidget();
		return;
	}

	// 결제 대상은 "지금 줄 맨 앞"이 아니라 "이 UI를 연 그 손님"이어야 합니다.
	// 스캔 도중 응대 중이던 손님이 강제 퇴장·제압·영업 종료로 줄을 떠나면,
	// 뒷사람이 앞사람의 주문 금액으로 결제되고 자기 물건은 공짜로 나가게 됩니다.
	if (!IsValid(ActiveCheckoutCustomer) ||
		CustomerSubsystem->GetFrontCustomer() != ActiveCheckoutCustomer)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 계산 중이던 손님이 대기열을 떠나 결제를 취소합니다."));
		CloseCheckoutWidget();
		return;
	}

	ACSStoreGameState* CurrentStoreGameState = World->GetGameState<ACSStoreGameState>();
	if (!IsValid(CurrentStoreGameState))
	{
		return;
	}

	ACSCustomerCharacter* FrontCustomer = CustomerSubsystem->GetFrontCustomer();
	const UCSCustomerStateComponent* CustomerState =
		IsValid(FrontCustomer) ? FrontCustomer->GetCustomerState() : nullptr;
	if (!IsValid(CustomerState))
	{
		return;
	}

	for (const FCSSelectProduct& SelectedProduct : CustomerState->SelectProducts)
	{
		if (SelectedProduct.ProductRuntimeState.IsExpired(
			CurrentStoreGameState->CurrentBusinessDay))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CheckoutCounter] An expired product cannot be checked out: %s"),
				*SelectedProduct.ProductRow.RowName.ToString());
			return;
		}

		const UCSFreshnessSettingsDataAsset* FreshnessSettings =
			CurrentStoreGameState->GetFreshnessSettings();
		if (IsValid(FreshnessSettings) &&
			!SelectedProduct.ProductRuntimeState.HasFreshnessRemaining(
				World->GetTimeSeconds(),
				FreshnessSettings->DecayIntervalSeconds,
				FreshnessSettings->DecayAmountPerInterval))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CheckoutCounter] Freshness가 0인 상품은 결제할 수 없습니다: %s"),
				*SelectedProduct.ProductRow.RowName.ToString());
			return;
		}
	}

	if (!CurrentStoreGameState->RecordCheckoutTransaction(ActiveCheckoutWidget->OrderItems))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 거래 기록에 실패하여 손님 결제를 완료하지 않습니다."));
		return;
	}
	PlayCheckoutGameplaySFX(this, ECSAudioEvent::CheckoutComplete);

	// 거래를 먼저 기록한 뒤 손님 Subsystem에 완료를 알려 다음 손님이 연출 중 앞으로 이동하게 합니다.
	if (IsValid(FrontCustomer))
	{
		FrontCustomer->ShowShoppingBag();
	}
	bCheckoutPaymentCompleted = true;
	bIsCheckoutTransitioning = true;
	SetCustomerBeingServed(ActiveCheckoutCustomer, false);
	CustomerSubsystem->CompleteFrontCheckout();

	// UI는 닫지 않고 최종 합계·도장·흔들림·영수증 퇴장 연출을 시작합니다.
	ActiveCheckoutWidget->BeginCheckoutCompletionSequence();
}

void ACSCheckoutCounterActor::HandleCheckoutCancelled()
{
	// 다음 손님을 기다리는 중에는 결제가 잠겨 있지만, 플레이어가 직접 나가는 것은 허용합니다.
	if (bIsWaitingForNextCustomer)
	{
		EndWaitForNextCustomer();
		CloseCheckoutWidget();
		return;
	}

	if (bIsCheckoutTransitioning)
	{
		return;
	}

	// 취소 시에는 판매 기록과 손님 대기열을 변경하지 않고 계산 UI만 닫습니다.
	CloseCheckoutWidget();
}

void ACSCheckoutCounterActor::HandleReceiptExitFinished()
{
	if (!bIsCheckoutTransitioning || !IsValid(ActiveCheckoutWidget))
	{
		return;
	}

	// 다음 손님이 0번 자리에 이미 도착했으면 즉시 새 영수증 띄움
	if (CanProcessCheckout() && PrepareNextCustomerCheckout())
	{
		bIsCheckoutTransitioning = false;
		ActiveCheckoutWidget->BeginNextCustomerReceipt();
		return;
	}


	const UWorld* World = GetWorld();
	const UCSCustomerRuntimeSubsystem* CustomerSubsystem = IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;

	// "다음 손님이 아직 걸어오는 중이다" -> 카메라 유지하고 기다림
	// 이걸 빼면 손님이 연속으로 서 있어도 매번 계산 세션이 닫혀서 카메라가 튕김
	if (CanProcessCheckout() &&IsValid(CustomerSubsystem) &&IsValid(CustomerSubsystem->GetFrontCustomer()))
	{
		BeginWaitForNextCustomer(); // 타이머를 돌려 도착을 폴링
		return;
	}

	// 대기열이 비었으면 종료
	CloseCheckoutWidget();
}

void ACSCheckoutCounterActor::BeginWaitForNextCustomer()
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(ActiveCheckoutWidget))
	{
		CloseCheckoutWidget();
		return;
	}

	// 이전 손님의 표시 상품과 주문을 비워 빈 계산대 상태로 대기합니다.
	// bIsCheckoutTransitioning은 켜 둔 채라서 대기 중 결제 요청은 들어오지 않습니다.
	ClearCheckoutDisplayProducts();
	ActiveCheckoutCustomer = nullptr;
	ActiveCheckoutWidget->SetCheckoutOrder(TArray<FCSCheckoutLineItem>(), 0);

	bIsWaitingForNextCustomer = true;
	NextCustomerWaitElapsed = 0.0f;

	// 액터 Tick을 늘리지 않도록 기존 계산대 방식대로 타이머로 폴링합니다.
	const float PollInterval = FMath::Max(NextCustomerPollInterval, 0.05f);
	World->GetTimerManager().SetTimer(
		NextCustomerPollTimer,
		this,
		&ACSCheckoutCounterActor::TickWaitForNextCustomer,
		PollInterval,
		true);
}

void ACSCheckoutCounterActor::TickWaitForNextCustomer()
{
	if (!bIsWaitingForNextCustomer)
	{
		return;
	}

	NextCustomerWaitElapsed += FMath::Max(NextCustomerPollInterval, 0.05f);

	if (!CanProcessCheckout() || !IsValid(ActiveCheckoutWidget))
	{
		EndWaitForNextCustomer();
		CloseCheckoutWidget();
		return;
	}

	// 다음 손님이 0번 자리에 도착했으면 카메라를 유지한 채 새 영수증을 띄웁니다.
	if (PrepareNextCustomerCheckout())
	{
		EndWaitForNextCustomer();
		bIsCheckoutTransitioning = false;
		ActiveCheckoutWidget->BeginNextCustomerReceipt();
		return;
	}

	const UWorld* World = GetWorld();
	const UCSCustomerRuntimeSubsystem* CustomerSubsystem = nullptr;

	if (IsValid(World))
	{
		// 유효하다면 서브시스템을 가져와 할당
		CustomerSubsystem = World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	}
	else
	{
		// 유효하지 않다면 nullptr을 할당
		CustomerSubsystem = nullptr;
	}

	const bool bQueueEmpty = !IsValid(CustomerSubsystem) || !IsValid(CustomerSubsystem->GetFrontCustomer());
	// 경로가 막혀 손님이 영원히 못 오는 경우에 플레이어가 계산 화면에 갇히지 않게 하는 안전장치
	const bool bTimedOut =
		NextCustomerWaitTimeout > 0.0f && NextCustomerWaitElapsed >= NextCustomerWaitTimeout;
	if (bQueueEmpty || bTimedOut)
	{
		EndWaitForNextCustomer();
		CloseCheckoutWidget();
	}
}

void ACSCheckoutCounterActor::EndWaitForNextCustomer()
{
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(NextCustomerPollTimer);
	}

	bIsWaitingForNextCustomer = false;
	NextCustomerWaitElapsed = 0.0f;
}

bool ACSCheckoutCounterActor::PrepareNextCustomerCheckout()
{
	if (!IsValid(ActiveCheckoutWidget))
	{
		return false;
	}

	TArray<FCSCheckoutLineItem> NextOrderItems;
	int32 NextTotalSalePrice = 0;
	if (!BuildFrontCustomerOrder(NextOrderItems, NextTotalSalePrice))
	{
		return false;
	}

	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	// 다음 손님도 0번 자리에 도착한 뒤에야 주문을 띄웁니다.
	// 아직 걸어오는 중이면 실패로 두고, 호출부(BeginWaitForNextCustomer)가 도착을 기다립니다.
	ACSCustomerCharacter* NextCustomer =
		IsValid(CustomerSubsystem) ? CustomerSubsystem->GetCheckoutReadyCustomer() : nullptr;
	if (!IsValid(NextCustomer))
	{
		return false;
	}

	// 이전 손님의 표시 액터를 정리한 뒤 다음 손님을 현재 계산 대상으로 교체합니다.
	ClearCheckoutDisplayProducts();
	ActiveCheckoutCustomer = NextCustomer;
	bCheckoutPaymentCompleted = false;
	SetCustomerBeingServed(ActiveCheckoutCustomer, true);
	SetCheckoutCustomerBasketVisible(false);
	ActiveCheckoutWidget->SetCheckoutOrder(NextOrderItems, NextTotalSalePrice);
	SpawnCheckoutDisplayProducts();
	return true;
}

void ACSCheckoutCounterActor::HandleCheckoutItemScanned(FName ProductId)
{
	if (ProductId.IsNone())
	{
		return;
	}

	// 주문은 같은 상품을 묶어 스캔하므로, 아직 남아 있는 동일 상품 중 첫 번째 표시 액터를 제거합니다.
	for (int32 ProductIndex = 0; ProductIndex < CheckoutDisplayProductIds.Num(); ++ProductIndex)
	{
		if (CheckoutDisplayProductIds[ProductIndex] != ProductId)
		{
			continue;
		}

		if (CheckoutDisplayProducts.IsValidIndex(ProductIndex) &&
			IsValid(CheckoutDisplayProducts[ProductIndex]))
		{
			CheckoutDisplayProducts[ProductIndex]->Destroy();
		}

		CheckoutDisplayProducts.RemoveAt(ProductIndex);
		CheckoutDisplayProductIds.RemoveAt(ProductIndex);
		return;
	}
}

void ACSCheckoutCounterActor::HandleStorePhaseChanged(ECStorePhase NewPhase)
{
	(void)NewPhase;

	// 결제 완료 뒤 영수증 퇴장 연출이 진행 중이라면 이미 기록된 거래의 마무리까지 보여 줍니다.
	// 연출 완료 콜백에서는 CanProcessCheckout()이 false이므로 다음 손님으로 넘어가지 않고 UI를 닫습니다.
	if (!CanProcessCheckout())
	{
		if (bCheckoutPaymentCompleted && bIsCheckoutTransitioning)
		{
			return;
		}

		// 결제 완료 전 강제 마감이라면 거래를 기록하지 않고 장바구니·표시 상품을 복원합니다.
		CancelEmployeeCheckoutPresentation();
		CloseCheckoutWidget();
	}
}

void ACSCheckoutCounterActor::CloseCheckoutWidget(const bool bRestorePawnImmediately)
{
	APlayerController* PlayerController = IsValid(ActiveCheckoutWidget)
		? ActiveCheckoutWidget->GetOwningPlayer()
		: CheckoutPlayerController.Get();

	// 다음 손님 대기 폴링이 남아 있으면 닫힌 UI를 다시 건드리게 되므로 먼저 정리합니다.
	EndWaitForNextCustomer();

	// 위젯이 이미 사라진 예외 상황에서도 계산대 카메라에 시점이 남지 않도록 먼저 복원합니다.
	RestoreCheckoutCamera(bRestorePawnImmediately);

	// UI가 어떤 이유로 닫히더라도 표시용 상품이 월드에 남지 않게 정리합니다.
	ClearCheckoutDisplayProducts();
	if (!bCheckoutPaymentCompleted)
	{
		// 취소 또는 강제 종료라면 계산 시작 전에 숨긴 장바구니를 다시 보여 줍니다.
		SetCheckoutCustomerBasketVisible(true);
	}
	// 응대가 끝났으므로 인내심을 다시 흐르게 합니다.
	SetCustomerBeingServed(ActiveCheckoutCustomer, false);
	ActiveCheckoutCustomer = nullptr;
	bCheckoutPaymentCompleted = false;
	bIsCheckoutTransitioning = false;
	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(PlayerController))
	{
		StorePlayerController->SetStoreHUDHidden(this, false);
	}

	if (!IsValid(ActiveCheckoutWidget))
	{
		return;
	}

	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(PlayerController))
	{
		// 위젯을 닫기 전에 입력 잠금을 해제하여 플레이어 조작 상태를 정상으로 돌립니다.
		StorePlayerController->SetGameplayUIInputLock(ActiveCheckoutWidget, false);
	}
	ActiveCheckoutWidget->RemoveFromParent();
	ActiveCheckoutWidget = nullptr;

	if (IsValid(PlayerController) &&
		!PlayerController->IsA<ACSStorePlayerController>())
	{
		// 계산 UI가 닫힌 뒤에는 플레이어가 다시 캐릭터를 조작할 수 있게 복구합니다.
		FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(false);
	}
}

void ACSCheckoutCounterActor::ActivateCheckoutCamera(
	APlayerController* PlayerController)
{
	if (!IsValid(PlayerController) || !IsValid(CheckoutCamera) || bCheckoutCameraActive)
	{
		return;
	}

	CheckoutPlayerController = PlayerController;
	PreviousCheckoutViewTarget = PlayerController->GetViewTarget();

	// 이전 계산의 지연 복원이 남아 있더라도 먼저 원상복구한 뒤 이번 Pawn의 상태를 새로 저장합니다.
	RestoreCheckoutPlayerVisibility(true);
	if (APawn* PlayerPawn = PlayerController->GetPawn(); IsValid(PlayerPawn))
	{
		CheckoutPlayerPawn = PlayerPawn;
		bCheckoutPlayerPawnWasHidden = PlayerPawn->IsHidden();
		bCheckoutPlayerVisibilityOverridden = true;
		PlayerPawn->SetActorHiddenInGame(true);
	}

	CheckoutCamera->SetActive(true);
	PlayerController->SetViewTargetWithBlend(
		this,
		CheckoutCameraBlendTime,
		EViewTargetBlendFunction::VTBlend_Cubic,
		0.0f,
		true);
	bCheckoutCameraActive = true;
}

void ACSCheckoutCounterActor::RestoreCheckoutCamera(const bool bRestorePawnImmediately)
{
	if (!bCheckoutCameraActive)
	{
		if (bRestorePawnImmediately)
		{
			RestoreCheckoutPlayerVisibility(true);
		}
		return;
	}

	bool bStartedReturnBlend = false;
	APlayerController* PlayerController = CheckoutPlayerController.Get();
	if (IsValid(PlayerController))
	{
		AActor* RestoreTarget = PreviousCheckoutViewTarget.Get();
		if (!IsValid(RestoreTarget))
		{
			RestoreTarget = PlayerController->GetPawn();
		}

		if (IsValid(RestoreTarget))
		{
			PlayerController->SetViewTargetWithBlend(
				RestoreTarget,
				CheckoutCameraBlendTime,
				EViewTargetBlendFunction::VTBlend_Cubic,
				0.0f,
				true);
			bStartedReturnBlend = CheckoutCameraBlendTime > 0.0f;
		}
	}

	if (IsValid(CheckoutCamera))
	{
		CheckoutCamera->SetActive(false);
	}

	CheckoutPlayerController = nullptr;
	PreviousCheckoutViewTarget = nullptr;
	bCheckoutCameraActive = false;

	// 정상 종료는 시점 Blend가 끝난 뒤 복원하고, 전환 불가/예외 종료는 즉시 원래 상태로 되돌립니다.
	RestoreCheckoutPlayerVisibility(
		bRestorePawnImmediately || !bStartedReturnBlend);
}

void ACSCheckoutCounterActor::RestoreCheckoutPlayerVisibility(const bool bImmediate)
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		World->GetTimerManager().ClearTimer(CheckoutPlayerVisibilityRestoreTimer);
	}

	if (!bCheckoutPlayerVisibilityOverridden)
	{
		return;
	}

	if (!bImmediate && IsValid(World) && CheckoutCameraBlendTime > 0.0f)
	{
		World->GetTimerManager().SetTimer(
			CheckoutPlayerVisibilityRestoreTimer,
			this,
			&ACSCheckoutCounterActor::HandleCheckoutPlayerVisibilityRestore,
			CheckoutCameraBlendTime,
			false);
		return;
	}

	if (APawn* PlayerPawn = CheckoutPlayerPawn.Get(); IsValid(PlayerPawn))
	{
		PlayerPawn->SetActorHiddenInGame(bCheckoutPlayerPawnWasHidden);
	}

	CheckoutPlayerPawn = nullptr;
	bCheckoutPlayerPawnWasHidden = false;
	bCheckoutPlayerVisibilityOverridden = false;
}

void ACSCheckoutCounterActor::HandleCheckoutPlayerVisibilityRestore()
{
	RestoreCheckoutPlayerVisibility(true);
}

void ACSCheckoutCounterActor::SetCheckoutCustomerBasketVisible(bool bShouldBeVisible)
{
	if (IsValid(ActiveCheckoutCustomer))
	{
		ActiveCheckoutCustomer->SetBasketDisplayVisible(bShouldBeVisible);
	}
}

void ACSCheckoutCounterActor::SpawnCheckoutDisplayProducts()
{
	ClearCheckoutDisplayProducts();

	if (!IsValid(CheckoutDisplayOrigin) || CheckoutDisplayProductsPerRow < 1)
	{
		return;
	}

	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	ACSCustomerCharacter* FrontCustomer =
		IsValid(CustomerSubsystem) ? CustomerSubsystem->GetFrontCustomer() : nullptr;
	const UCSCustomerStateComponent* CustomerState =
		IsValid(FrontCustomer) ? FrontCustomer->GetCustomerState() : nullptr;
	if (!IsValid(World) || !IsValid(CustomerState))
	{
		return;
	}

	const FVector OriginLocation = CheckoutDisplayOrigin->GetComponentLocation();
	const FVector RightDirection = CheckoutDisplayOrigin->GetRightVector();
	const FVector ForwardDirection = CheckoutDisplayOrigin->GetForwardVector();
	const FVector UpDirection = CheckoutDisplayOrigin->GetUpVector();
	const FQuat OriginRotation = CheckoutDisplayOrigin->GetComponentQuat();

	for (int32 ProductIndex = 0; ProductIndex < CustomerState->SelectProducts.Num(); ++ProductIndex)
	{
		const FCSSelectProduct& SelectedProduct = CustomerState->SelectProducts[ProductIndex];
		if (SelectedProduct.ProductClass == nullptr ||
			SelectedProduct.ProductRow.DataTable == nullptr ||
			SelectedProduct.ProductRow.RowName.IsNone())
		{
			continue;
		}

		const int32 RowIndex = ProductIndex / CheckoutDisplayProductsPerRow;
		const int32 ColumnIndex = ProductIndex % CheckoutDisplayProductsPerRow;
		const FVector BaseLocation = OriginLocation +
			RightDirection * (CheckoutDisplayRightSpacing * static_cast<float>(ColumnIndex)) +
			ForwardDirection * (CheckoutDisplayForwardSpacing * static_cast<float>(RowIndex));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* SpawnedActor = World->SpawnActor<AActor>(
			SelectedProduct.ProductClass,
			BaseLocation,
			OriginRotation.Rotator(),
			SpawnParameters);
		ACSProductPickupActor* DisplayProduct = Cast<ACSProductPickupActor>(SpawnedActor);
		if (!IsValid(DisplayProduct) ||
			!DisplayProduct->InitializeProduct(SelectedProduct.ProductRow))
		{
			if (IsValid(SpawnedActor))
			{
				SpawnedActor->Destroy();
			}
			continue;
		}

		// 표시 전용 액터는 플레이어가 다시 집거나 충돌에 밀리지 않도록 비상호작용 상태로 둡니다.
		DisplayProduct->SetActorEnableCollision(false);
		// 계산대 위 상품은 주문 연출용 복제본이므로 월드 재고 집계에서 제외합니다.
		DisplayProduct->SetExcludedFromStockCount(true);
		const float ProductHalfHeight =
			DisplayProduct->GetComponentsBoundingBox(false).GetExtent().Z;
		DisplayProduct->SetActorLocation(BaseLocation + UpDirection * ProductHalfHeight);
		DisplayProduct->AttachToComponent(
			CheckoutDisplayOrigin,
			FAttachmentTransformRules::KeepWorldTransform);

		CheckoutDisplayProducts.Add(DisplayProduct);
		CheckoutDisplayProductIds.Add(SelectedProduct.ProductRow.RowName);
	}
}

void ACSCheckoutCounterActor::ClearCheckoutDisplayProducts()
{
	for (ACSProductPickupActor* DisplayProduct : CheckoutDisplayProducts)
	{
		if (IsValid(DisplayProduct))
		{
			DisplayProduct->Destroy();
		}
	}

	CheckoutDisplayProducts.Reset();
	CheckoutDisplayProductIds.Reset();
}
