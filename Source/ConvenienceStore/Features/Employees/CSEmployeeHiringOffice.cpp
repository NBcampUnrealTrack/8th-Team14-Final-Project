#include "Features/Employees/CSEmployeeHiringOffice.h"

#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "UI/Store/CSEmployeeHiringWidget.h"
#include "Systems/Employees/CSEmployeeHiringSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ACSEmployeeHiringOffice::ACSEmployeeHiringOffice()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	BuildingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuildingMesh"));
	BuildingMesh->SetupAttachment(SceneRoot);
	BuildingMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	FirstRestockPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("FirstRestockPoint"));
	FirstRestockPoint->SetupAttachment(SceneRoot);
	FirstRestockPoint->SetRelativeLocation(FVector(0, -190, 96));
	CheckoutPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("CheckoutPoint"));
	CheckoutPoint->SetupAttachment(SceneRoot);
	CheckoutPoint->SetRelativeLocation(FVector(0, 0, 96));
	SecondRestockPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("SecondRestockPoint"));
	SecondRestockPoint->SetupAttachment(SceneRoot);
	SecondRestockPoint->SetRelativeLocation(FVector(0, 190, 96));
}

void ACSEmployeeHiringOffice::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) return;
	SpawnApplicant(ECSEmployeeHireSlot::FirstRestock, FirstRestockPoint, RestockEmployeeClass);
	SpawnApplicant(ECSEmployeeHireSlot::Checkout, CheckoutPoint, CheckoutEmployeeClass);
	SpawnApplicant(ECSEmployeeHireSlot::SecondRestock, SecondRestockPoint, RestockEmployeeClass);
}

void ACSEmployeeHiringOffice::SpawnApplicant(ECSEmployeeHireSlot Slot, UArrowComponent* Point,
	TSubclassOf<ACSStoreEmployeeCharacter> EmployeeClass)
{
	UCSEmployeeHiringSubsystem* Hiring = GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>();
	if (!Hiring || Hiring->IsSlotHired(Slot)) return;
	// 고용소를 여러 개 배치하거나 지원자를 수동 배치해도 같은 자리를 복제하지 않습니다.
	for (TActorIterator<ACSStoreEmployeeCharacter> It(GetWorld()); It; ++It)
	{
		if (It->RequiresHiring() && It->GetHiringSlot() == Slot) return;
	}
	if (!EmployeeClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[EmployeeHiring] 고용소 %s의 직원 BP 클래스를 지정해 주세요."), *GetName());
		return;
	}
	const FTransform Transform(Point->GetComponentRotation(), Point->GetComponentLocation());
	ACSStoreEmployeeCharacter* Applicant = GetWorld()->SpawnActorDeferred<ACSStoreEmployeeCharacter>(
		EmployeeClass, Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
	if (!IsValid(Applicant))
	{
		UE_LOG(LogTemp, Error, TEXT("[EmployeeHiring] 지원자 생성 실패: %s. 고용소 화살표 주변 공간을 확인해 주세요."), *GetName());
		return;
	}
	Applicant->ConfigureAsApplicant(Slot, HiringWidgetClass);
	Applicant->FinishSpawning(Transform);
}
