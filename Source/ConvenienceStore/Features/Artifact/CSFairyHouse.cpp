#include "Features/Artifact/CSFairyHouse.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"
#include "Features/Artifact/CSCleaningFairy.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/HUD/CSFairyDialogueWidget.h"
#include "HAL/PlatformTime.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

ACSFairyHouse::ACSFairyHouse()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	HouseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HouseMesh"));
	SetRootComponent(HouseMesh);
	HouseMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Demo/KJW/FairyHouse/SM_ConvenienceStore_FairyHouse.SM_ConvenienceStore_FairyHouse")));
	// 지정된 카운터 위의 장식이므로 손님 동선과 상호작용 시야를 막지 않도록 합니다.
	HouseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DoorPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("DoorPoint"));
	DoorPoint->SetupAttachment(HouseMesh);
	DoorPoint->SetRelativeLocation(FVector(0, -80, 40));
	FlightPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("FlightPoint"));
	FlightPoint->SetupAttachment(HouseMesh);
	FlightPoint->SetRelativeLocation(FVector(0, -200, 150));
	CleaningArea = CreateDefaultSubobject<UBoxComponent>(TEXT("CleaningArea"));
	CleaningArea->SetupAttachment(HouseMesh);
	CleaningArea->SetBoxExtent(FVector(1500, 1500, 500));
	CleaningArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FairyClass = ACSCleaningFairy::StaticClass();
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(HouseMesh);
	InteractionBox->SetRelativeLocation(FVector(0, 0, 50));
	InteractionBox->SetBoxExtent(FVector(65, 65, 65));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetCanEverAffectNavigation(false);
	SpeechBubble = CreateDefaultSubobject<UWidgetComponent>(TEXT("SpeechBubble"));
	SpeechBubble->SetupAttachment(HouseMesh);
	SpeechBubble->SetRelativeLocation(FVector(0, 0, 150));
	SpeechBubble->SetWidgetSpace(EWidgetSpace::Screen);
	SpeechBubble->SetDrawSize(FVector2D(520, 230));
	SpeechBubble->SetDrawAtDesiredSize(true);
	SpeechBubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpeechBubble->SetVisibility(false);
	IntroductionCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("IntroductionCamera"));
	IntroductionCamera->SetupAttachment(HouseMesh);
	IntroductionCamera->SetRelativeLocation(FVector(0, -550, 230));
	IntroductionCamera->SetRelativeRotation((FVector(0, 0, 60) - IntroductionCamera->GetRelativeLocation()).Rotation());
	IntroductionCamera->SetFieldOfView(60.f);
}

void ACSFairyHouse::BeginPlay()
{
	Super::BeginPlay();
	BubbleWidget = CreateWidget<UCSFairyDialogueWidget>(GetWorld(), SpeechBubbleWidgetClass ? SpeechBubbleWidgetClass.Get() : UCSFairyDialogueWidget::StaticClass());
	if (BubbleWidget) BubbleWidget->SetVisibility(ESlateVisibility::Collapsed);
	SpeechBubble->SetWidget(BubbleWidget);
	CameraRestTransform = IntroductionCamera->GetRelativeTransform();
	CameraRestFOV = IntroductionCamera->FieldOfView;
	LastRealTime = FPlatformTime::Seconds();
	if (UCSArtifactSubsystem* Artifacts = GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>())
	{
		Artifacts->OnArtifactsChanged.AddUniqueDynamic(this, &ACSFairyHouse::RefreshContractState);
		RefreshContractState();
	}
}

bool ACSFairyHouse::SpawnFairy()
{
	if (IsValid(ActiveFairy)) return true;
	if (!FairyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CleaningFairy] %s의 FairyClass가 비어 있어 요정을 생성할 수 없습니다."), *GetName());
		return false;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActiveFairy = GetWorld()->SpawnActor<ACSCleaningFairy>(FairyClass, DoorPoint->GetComponentLocation(), GetActorRotation(), Params);
	if (ActiveFairy) ActiveFairy->Initialize(this);
	else UE_LOG(LogTemp, Warning, TEXT("[CleaningFairy] %s에서 요정 생성에 실패했습니다."), *GetName());
	return IsValid(ActiveFairy);
}

void ACSFairyHouse::RefreshContractState()
{
	const UCSArtifactSubsystem* Artifacts = GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>();
	if (Artifacts && Artifacts->IsFairyContractSigned())
	{
		if (!bIntroducing) SpawnFairy();
	}
	else
	{
		FinishIntroduction();
		if (IsValid(ActiveFairy)) ActiveFairy->Destroy();
		ActiveFairy = nullptr;
		HideSpeechBubble();
	}
}

TArray<FCSFairyDialogueRow> ACSFairyHouse::GetDialogues(ECSFairyDialogueGroup Group) const
{
	TArray<FCSFairyDialogueRow> Result;
	if (!IsValid(DialogueTable) || DialogueTable->GetRowStruct() != FCSFairyDialogueRow::StaticStruct())
	{
		UE_LOG(LogTemp, Warning, TEXT("[CleaningFairy] %s의 대사 테이블이 없거나 행 구조가 올바르지 않아 대화를 생략합니다. FCSFairyDialogueRow 테이블을 지정해 주세요."), *GetName());
		return Result;
	}
	// 첫 등장 대사는 행 이름 순서로 재생하므로 01, 02처럼 순서를 지정해 주세요.
	TArray<FName> Names = DialogueTable->GetRowNames();
	Names.Sort(FNameLexicalLess());
	for (FName Name : Names)
	{
		const auto* Row = DialogueTable->FindRow<FCSFairyDialogueRow>(Name, TEXT("Fairy dialogue"));
		if (Row && Row->Group == Group && !Row->Text.IsEmpty()) Result.Add(*Row);
	}
	if (Result.IsEmpty())
		UE_LOG(LogTemp, Warning, TEXT("[CleaningFairy] %s의 대사 테이블 %s에 %s 그룹의 대사가 없어 대화를 생략합니다."),
			*GetName(), *DialogueTable->GetName(), *StaticEnum<ECSFairyDialogueGroup>()->GetNameStringByValue(static_cast<int64>(Group)));
	return Result;
}

bool ACSFairyHouse::ChooseRandomDialogue(ECSFairyDialogueGroup Group, FCSFairyDialogueRow& OutLine)
{
	const auto Lines = GetDialogues(Group);
	if (Lines.IsEmpty()) return false;
	const int32* Previous = LastDialogueIndices.Find(Group);
	int32 Index = FMath::RandRange(0, Lines.Num() - 1);
	if (Lines.Num() > 1 && Previous && *Previous >= 0 && *Previous < Lines.Num())
	{
		Index = FMath::RandRange(0, Lines.Num() - 2);
		if (Index >= *Previous) ++Index;
	}
	LastDialogueIndices.Add(Group, Index);
	OutLine = Lines[Index];
	return true;
}

void ACSFairyHouse::ShowRandomDialogue(ECSFairyDialogueGroup Group)
{
	if (!BubbleWidget || bIntroducing || FPlatformTime::Seconds() < BubbleUntil) return;
	FCSFairyDialogueRow Line;
	if (!ChooseRandomDialogue(Group, Line)) return;
	BubbleWidget->SetDialogue(Line.Text);
	BubbleWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	SpeechBubble->SetVisibility(true);
	BubbleUntil = FPlatformTime::Seconds() + FMath::Max(1.f, Line.DisplayDuration);
}

void ACSFairyHouse::HideSpeechBubble()
{
	// 화면 공간 위젯 자체도 숨겨 컷씬 대사와 일반 말풍선이 겹치지 않도록 합니다.
	SpeechBubble->SetVisibility(false);
	if (BubbleWidget) BubbleWidget->SetVisibility(ESlateVisibility::Collapsed);
	BubbleUntil = 0.;
}

void ACSFairyHouse::HideOtherUI()
{
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), false);
	for (UUserWidget* Widget : Widgets)
	{
		// 컷씬 디자인 안에 포함된 자식 UserWidget도 계속 표시되도록 제외합니다.
		if (!IsValid(Widget) || Widget == IntroductionWidget || Widget->IsIn(IntroductionWidget)) continue;
		// 처음 표시 상태를 보관하고, 컷씬 중 새로 만들어지는 UI도 숨깁니다.
		if (!HiddenUIVisibilities.Contains(Widget)) HiddenUIVisibilities.Add(Widget, Widget->GetVisibility());
		if (Widget->GetVisibility() != ESlateVisibility::Collapsed) Widget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ACSFairyHouse::RestoreOtherUI()
{
	for (const auto& Pair : HiddenUIVisibilities)
		if (UUserWidget* Widget = Pair.Key.Get()) Widget->SetVisibility(Pair.Value);
	HiddenUIVisibilities.Reset();
}

void ACSFairyHouse::Interact_Implementation(AActor* Interactor)
{
	const ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(Interactor);
	if (!Player || !Player->IsLocallyControlled()) return;
	const UCSArtifactSubsystem* Artifacts = GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>();
	if (!Artifacts) return;
	if (bIntroducing || (Artifacts->IsFairyContractSigned() && (!IsValid(ActiveFairy) || !ActiveFairy->IsAtHome()))) return;
	ShowRandomDialogue(Artifacts->IsFairyContractSigned() ? ECSFairyDialogueGroup::AfterContract
		: Artifacts->HasFairyContract() ? ECSFairyDialogueGroup::ContractReady : ECSFairyDialogueGroup::BeforeContract);
}

FText ACSFairyHouse::GetInteractionText_Implementation() const
{
	if (bIntroducing) return FText::GetEmpty();
	const UCSArtifactSubsystem* Artifacts = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>() : nullptr;
	if (Artifacts && Artifacts->IsFairyContractSigned() && (!IsValid(ActiveFairy) || !ActiveFairy->IsAtHome()))
		return NSLOCTEXT("Fairy", "AwayFromHome", "요정이 청소하러 나갔습니다.");
	return NSLOCTEXT("Fairy", "Talk", "미화의 요정과 대화하기");
}

bool ACSFairyHouse::BeginContractIntroduction(ACSStorePlayerController* Controller)
{
	if (bIntroducing || !IsValid(Controller) || !Controller->IsLocalPlayerController() || Controller->IsPaused()) return false;
	IntroductionWidget = CreateWidget<UCSFairyDialogueWidget>(Controller, IntroductionDialogueWidgetClass ? IntroductionDialogueWidgetClass.Get() : UCSFairyDialogueWidget::StaticClass());
	if (!IntroductionWidget || !SpawnFairy()) return false;
	IntroductionController = Controller;
	PreviousViewTarget = Controller->GetViewTarget();
	bPausedByIntroduction = Controller->SetPause(true);
	if (!bPausedByIntroduction)
	{
		ActiveFairy->Destroy();
		ActiveFairy = nullptr;
		IntroductionWidget = nullptr;
		IntroductionController.Reset();
		return false;
	}
	bPreviousFullTickWhenPaused = Controller->SetFairyIntroductionTickWhenPaused(true);
	Controller->CloseJournal();
	bIntroducing = true;
	IntroductionElapsed = 0.f;
	bCloseupStarted = false;
	IntroductionLineIndex = INDEX_NONE;
	IntroductionLines = GetDialogues(ECSFairyDialogueGroup::Introduction);
	LastRealTime = FPlatformTime::Seconds();
	HideSpeechBubble();
	ActiveFairy->SetIntroductionActive(true);
	IntroductionWidget->SetDialogue(FText::GetEmpty(), true);
	IntroductionWidget->OnSkip.AddUniqueDynamic(this, &ACSFairyHouse::FinishIntroduction);
	IntroductionWidget->OnAdvance.AddUniqueDynamic(this, &ACSFairyHouse::AdvanceIntroductionDialogue);
	IntroductionWidget->AddToViewport(1200);
	if (!IntroductionDialogueWidgetClass)
	{
		// 기본 UI에만 기존 배치를 적용하고, 지정된 WBP는 디자이너의 앵커와 배치를 사용합니다.
		IntroductionWidget->SetDesiredSizeInViewport(FVector2D(540, 260));
		IntroductionWidget->SetAlignmentInViewport(FVector2D(.5f, 1.f));
		int32 Width = 0, Height = 0;
		Controller->GetViewportSize(Width, Height);
		IntroductionWidget->SetPositionInViewport(FVector2D(Width * .5f, Height - 35.f));
	}
	Controller->SetGameplayUIInputLock(IntroductionWidget, true);
	HideOtherUI();
	IntroductionWidget->SetKeyboardFocus();
	Controller->SetViewTarget(this);
	return true;
}

void ACSFairyHouse::AdvanceIntroductionDialogue()
{
	if (!bIntroducing || !IntroductionWidget) return;
	++IntroductionLineIndex;
	if (!IntroductionLines.IsValidIndex(IntroductionLineIndex))
	{
		FinishIntroduction();
		return;
	}
	IntroductionWidget->StartTypewriterDialogue(IntroductionLines[IntroductionLineIndex].Text, TypewriterCharactersPerSecond);
}

void ACSFairyHouse::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	const float RealDelta = FMath::Min(.1f, static_cast<float>(Now - LastRealTime));
	LastRealTime = Now;
	if (SpeechBubble->IsVisible() && (Now >= BubbleUntil || (IsValid(ActiveFairy) && !ActiveFairy->IsAtHome()))) HideSpeechBubble();
	if (!bIntroducing) return;
	HideOtherUI();
	if (!IntroductionController.IsValid() || !IsValid(ActiveFairy))
	{
		FinishIntroduction();
		return;
	}
	// 월드는 일시정지하되 연출은 실제 경과 시간으로 진행하여 영업 손실을 막습니다.
	IntroductionElapsed += RealDelta;
	const float EmergeStart = FMath::Max(.1f, HouseShotDuration);
	const float EmergeEnd = EmergeStart + FMath::Max(.1f, EmergenceDuration);
	const float DialogueStart = EmergeEnd + FMath::Max(.1f, CloseupDuration);
	if (IntroductionElapsed >= EmergeStart)
	{
		const float Alpha = FMath::Clamp((IntroductionElapsed - EmergeStart) / FMath::Max(.1f, EmergenceDuration), 0.f, 1.f);
		const FVector Start = DoorPoint->GetComponentLocation();
		const FVector End = FlightPoint->GetComponentLocation();
		const FVector Control = (Start + End) * .5f + FVector(0, 0, 60);
		ActiveFairy->SetActorLocation(FMath::Lerp(FMath::Lerp(Start, Control, Alpha), FMath::Lerp(Control, End, Alpha), Alpha));
		ActiveFairy->UpdateIntroductionOrb(Alpha, RealDelta);
	}
	if (IntroductionElapsed >= EmergeEnd)
	{
		if (!bCloseupStarted)
		{
			// 상대 좌표의 부모나 집의 크기가 달라도 실제 요정 위치를 바라보도록 월드 좌표로 계산합니다.
			CloseupStartTransform = IntroductionCamera->GetComponentTransform();
			CloseupFocus = ActiveFairy->FairyMesh->Bounds.Origin;
			FVector CameraSide = (CloseupStartTransform.GetLocation() - CloseupFocus).GetSafeNormal();
			if (CameraSide.IsNearlyZero()) CameraSide = -IntroductionCamera->GetForwardVector();
			// 처음 집을 비추던 카메라와 같은 쪽에서 접근하여 요정을 가로지르지 않도록 합니다.
			CloseupEndLocation = CloseupFocus + CameraSide * FMath::Max(30.f, CloseupDistance);
			bCloseupStarted = true;
		}
		const float Alpha = FMath::SmoothStep(0.f, 1.f, (IntroductionElapsed - EmergeEnd) / FMath::Max(.1f, CloseupDuration));
		const FVector Location = FMath::Lerp(CloseupStartTransform.GetLocation(), CloseupEndLocation, Alpha);
		IntroductionCamera->SetWorldLocation(Location);
		IntroductionCamera->SetWorldRotation(FQuat::Slerp(CloseupStartTransform.GetRotation(), (CloseupFocus - Location).Rotation().Quaternion(), Alpha));
		IntroductionCamera->SetFieldOfView(FMath::Lerp(CameraRestFOV, CloseupFieldOfView, Alpha));
		ActiveFairy->SetActorRotation((IntroductionCamera->GetComponentLocation() - ActiveFairy->GetActorLocation()).GetSafeNormal2D().Rotation());
	}
	if (IntroductionElapsed >= DialogueStart && IntroductionLineIndex == INDEX_NONE) AdvanceIntroductionDialogue();
}

void ACSFairyHouse::FinishIntroduction()
{
	if (!bIntroducing) return;
	bIntroducing = false;
	RestoreOtherUI();
	if (ACSStorePlayerController* Controller = IntroductionController.Get())
	{
		// 자연 종료와 건너뛰기 모두 같은 경로로 카메라, 입력, 일시정지를 복구합니다.
		Controller->SetViewTarget(PreviousViewTarget.IsValid() ? PreviousViewTarget.Get() : Controller->GetPawn());
		Controller->SetFairyIntroductionTickWhenPaused(bPreviousFullTickWhenPaused);
		if (bPausedByIntroduction) Controller->SetPause(false);
		Controller->SetGameplayUIInputLock(IntroductionWidget, false);
	}
	if (IntroductionWidget) IntroductionWidget->RemoveFromParent();
	IntroductionWidget = nullptr;
	IntroductionController.Reset();
	PreviousViewTarget.Reset();
	bPausedByIntroduction = false;
	IntroductionCamera->SetRelativeTransform(CameraRestTransform);
	IntroductionCamera->SetFieldOfView(CameraRestFOV);
	if (IsValid(ActiveFairy)) ActiveFairy->SetIntroductionActive(false);
}

void ACSFairyHouse::EndPlay(const EEndPlayReason::Type Reason)
{
	FinishIntroduction();
	if (GetGameInstance())
		if (UCSArtifactSubsystem* Artifacts = GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>())
			Artifacts->OnArtifactsChanged.RemoveDynamic(this, &ACSFairyHouse::RefreshContractState);
	if (IsValid(ActiveFairy)) ActiveFairy->Destroy();
	Super::EndPlay(Reason);
}

ACSFairyHousePlacementPoint::ACSFairyHousePlacementPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	PlacementPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("PlacementPoint"));
	PlacementPoint->SetupAttachment(RootComponent);
	PlacementPoint->SetHiddenInGame(true);
}
