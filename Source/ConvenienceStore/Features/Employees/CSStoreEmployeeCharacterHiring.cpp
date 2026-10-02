#include "Features/Employees/CSStoreEmployeeCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Systems/Employees/CSEmployeeHiringSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/Store/CSEmployeeHiringWidget.h"
#include "Engine/World.h"
#include "Engine/Font.h"

void ACSStoreEmployeeCharacter::ConfigureAsApplicant(ECSEmployeeHireSlot Slot, TSubclassOf<UCSEmployeeHiringWidget> WidgetClass)
{
	check(!HasActorBegunPlay());
	bRequiresHiring = true;
	HiringSlot = Slot;
	if (WidgetClass) HiringWidgetClass = WidgetClass;
}

bool ACSStoreEmployeeCharacter::CanWork() const
{
	if (!bRequiresHiring) return true;
	const UCSEmployeeHiringSubsystem* Hiring = GetWorld() ? GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>() : nullptr;
	return Hiring && Hiring->CanEmployeeWork(this);
}

FText ACSStoreEmployeeCharacter::GetHiringRoleName() const
{
	switch (HiringSlot)
	{
	case ECSEmployeeHireSlot::FirstRestock: return NSLOCTEXT("EmployeeHiring", "FirstRestock", "첫 번째 진열 직원");
	case ECSEmployeeHireSlot::Checkout: return NSLOCTEXT("EmployeeHiring", "Checkout", "계산 직원");
	case ECSEmployeeHireSlot::SecondRestock: return NSLOCTEXT("EmployeeHiring", "SecondRestock", "두 번째 진열 직원");
	default: return FText::GetEmpty();
	}
}

void ACSStoreEmployeeCharacter::RefreshEmploymentPresentation()
{
	const UCSEmployeeHiringSubsystem* Hiring = GetWorld() ? GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>() : nullptr;
	const bool bApplicant = bRequiresHiring && !(Hiring && Hiring->IsSlotHired(HiringSlot));
	bAwaitingEmploymentStart = bRequiresHiring && !bApplicant && !CanWork();
	// 고용 전 지원자는 모자를 숨기고, 고용 완료/저장 복원 시 즉시 표시합니다.
	HatMesh->SetHiddenInGame(bApplicant);
	if (bRequiresHiring) GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, bApplicant ? ECR_Block : ECR_Ignore);
	if (HiringNameplateFont) HiringNameplate->SetFont(HiringNameplateFont);
	HiringNameplate->SetText(bAwaitingEmploymentStart
		? NSLOCTEXT("EmployeeHiring", "StartsTomorrowNameplate", "내일부터 근무") : GetHiringRoleName());
	HiringNameplate->SetHiddenInGame(!bApplicant && !bAwaitingEmploymentStart);
}

void ACSStoreEmployeeCharacter::Interact_Implementation(AActor* Interactor)
{
	if (!bRequiresHiring || (IsValid(ActiveHiringWidget) && ActiveHiringWidget->IsInViewport())) return;
	const UCSEmployeeHiringSubsystem* Hiring = GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>();
	if (!Hiring || Hiring->IsSlotHired(HiringSlot)) return;
	const APawn* Pawn = Cast<APawn>(Interactor);
	ACSStorePlayerController* PlayerController = Pawn ? Cast<ACSStorePlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController() || PlayerController->IsGameplayUIInputLocked()) return;
	UClass* WidgetClass = HiringWidgetClass.Get();
	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmployeeHiring] %s: HiringWidgetClass에 CSEmployeeHiringWidget 기반 WBP를 지정해 주세요."), *GetName());
		return;
	}
	ActiveHiringWidget = CreateWidget<UCSEmployeeHiringWidget>(PlayerController, WidgetClass);
	if (!IsValid(ActiveHiringWidget)) return;
	ActiveHiringWidget->SetApplicant(this);
	ActiveHiringWidget->AddToViewport(20);
}

FText ACSStoreEmployeeCharacter::GetInteractionText_Implementation() const
{
	if (!bRequiresHiring) return FText::GetEmpty();
	const UCSEmployeeHiringSubsystem* Hiring = GetWorld() ? GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>() : nullptr;
	if (Hiring && Hiring->IsSlotHired(HiringSlot)) return FText::GetEmpty();
	return FText::Format(NSLOCTEXT("EmployeeHiring", "Talk", "{0}와 대화하기"), GetHiringRoleName());
}
