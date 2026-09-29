#include "Features/Facilities/CSSaveIdComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectSaveContext.h"

UCSSaveIdComponent::UCSSaveIdComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = false;
}

void UCSSaveIdComponent::OnRegister()
{
	Super::OnRegister();

	const UWorld* World = GetWorld();
	if (World == nullptr || SaveId.IsValid())
	{
		return;
	}

#if WITH_EDITOR
	// 에디터 월드에서는 레벨 로드 중 인스턴스 값이 적용되기 전일 수 있어 여기서 발급하지 않음
	// 발급은 PreSave에서 처리하고 저장이 필요하다는 표시만 설정
	if (!World->IsGameWorld())
	{
		MarkPackageDirty();
		return;
	}
#endif

	// 게임 월드에서 식별자가 비어 있으면 런타임 스폰 액터로 판단
	SaveId = FGuid::NewGuid();
	bWasSpawnedAtRuntime = true;
}

#if WITH_EDITOR
void UCSSaveIdComponent::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);

	// 템플릿·CDO에는 발급하지 않고 레벨 인스턴스에만 발급
	if (SaveId.IsValid() || IsTemplate())
	{
		return;
	}

	SaveId = FGuid::NewGuid();
}

void UCSSaveIdComponent::PostEditImport()
{
	Super::PostEditImport();

	if (!IsTemplate())
	{
		SaveId = FGuid::NewGuid();
	}
}
#endif

void UCSSaveIdComponent::OverrideSaveId(const FGuid& InSaveId, bool bInWasSpawnedAtRuntime)
{
	if (InSaveId.IsValid())
	{
		SaveId = InSaveId;
	}

	bWasSpawnedAtRuntime = bInWasSpawnedAtRuntime;
}

FGuid UCSSaveIdComponent::FindSaveId(const AActor* InActor)
{
	if (!IsValid(InActor))
	{
		return FGuid();
	}

	if (const UCSSaveIdComponent* Component = InActor->FindComponentByClass<UCSSaveIdComponent>())
	{
		return Component->GetSaveId();
	}

	return FGuid();
}

UCSSaveIdComponent* UCSSaveIdComponent::FindOrAddComponent(AActor* InActor)
{
	if (!IsValid(InActor))
	{
		return nullptr;
	}

	if (UCSSaveIdComponent* Existing = InActor->FindComponentByClass<UCSSaveIdComponent>())
	{
		return Existing;
	}

	UCSSaveIdComponent* Component = NewObject<UCSSaveIdComponent>(InActor);
	if (!IsValid(Component))
	{
		return nullptr;
	}

	Component->RegisterComponent();
	InActor->AddInstanceComponent(Component);
	return Component;
}
