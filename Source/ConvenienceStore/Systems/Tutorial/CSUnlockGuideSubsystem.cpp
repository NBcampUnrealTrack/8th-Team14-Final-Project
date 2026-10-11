#include "Systems/Tutorial/CSUnlockGuideSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/SaveLoad/CSSaveSubsystem.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Features/Store/CSUnlockGuideCamera.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Ending/CSEndingActors.h"
#include "UI/Tutorial/CSUnlockGuideWidget.h"
#include "UI/Tutorial/CSLevelPresentationWidget.h"
#include "UI/Store/CSWakeUpWidget.h"
#include "Camera/CameraTypes.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "MovieSceneSequencePlaybackSettings.h"

namespace
{
void DebugSkipOpeningCommand(UWorld* World)
{
	if (UCSUnlockGuideSubsystem* Guide = World ? World->GetSubsystem<UCSUnlockGuideSubsystem>() : nullptr)
	{
		Guide->DebugSkipOpening();
	}
}

FAutoConsoleCommandWithWorld CmdSkipOpening(
	TEXT("cs.SkipOpening"),
	TEXT("현재 재생 중인 새 게임 오프닝을 종료하고 게임으로 진행한다. 디버그용."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&DebugSkipOpeningCommand));
}

TStatId UCSUnlockGuideSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCSUnlockGuideSubsystem, STATGROUP_Tickables);
}

void UCSUnlockGuideSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	LastRealTime = FPlatformTime::Seconds();
	StoreState = InWorld.GetGameState<ACSStoreGameState>();
	if (StoreState && ObservedLevel == INDEX_NONE) ObservedLevel = StoreState->GetStoreLevel();
	if (StoreState) StoreState->OnStoreLevelChanged.AddUniqueDynamic(this, &ThisClass::HandleLevelChanged);
}

void UCSUnlockGuideSubsystem::Configure()
{
	if (bConfigured || !StoreState) return;
	Controller = Cast<ACSStorePlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Controller) return;
	// 행 순서는 진행 순서로 사용하지 않습니다. 레벨 값과 장면·대사 배열 순서로 재생합니다.
	StoryEvents.Reset();
	if (const UDataTable* Table = Controller->LevelStoryTable)
	{
		if (Table->GetRowStruct() == FCSLevelStoryEvent::StaticStruct())
		{
			TArray<FCSLevelStoryEvent*> Rows;
			Table->GetAllRows<FCSLevelStoryEvent>(TEXT("LevelStory"), Rows);
			for (const FCSLevelStoryEvent* Row : Rows)
			{
				if (!Row || Row->StoreLevel < 1 || Row->StoreLevel > 10 || FindStoryEvent(Row->StoreLevel))
				{
					UE_LOG(LogTemp, Warning, TEXT("[LevelStory] 레벨이 범위를 벗어나거나 중복된 행은 제외합니다."));
					continue;
				}
				StoryEvents.Add(*Row);
			}
		}
		else UE_LOG(LogTemp, Warning, TEXT("[LevelStory] CSLevelStoryEvent 행 구조의 데이터 테이블을 지정해 주세요."));
	}
	auto AddEntry = [this](const FCSUnlockGuideEntry& Entry)
	{
		if (Entry.Id.IsNone()) return;
		Entries.RemoveAll([&](const FCSUnlockGuideEntry& Item) { return Item.Id == Entry.Id; });
		Entries.Add(Entry);
	};
	if (Controller->UnlockGuideCatalog)
		for (const FCSUnlockGuideEntry& Entry : Controller->UnlockGuideCatalog->Entries) AddEntry(Entry);
	for (TActorIterator<ACSUnlockGuideCamera> It(GetWorld()); It; ++It)
	{
		if (It->Catalog)
			for (const FCSUnlockGuideEntry& Entry : It->Catalog->Entries)
				if (!FindEntry(Entry.Id)) AddEntry(Entry);
	}
	for (TActorIterator<ACSUnlockGuideCamera> It(GetWorld()); It; ++It)
	{
		if (It->Entry.Id.IsNone()) continue;
		if (Cameras.Contains(It->Entry.Id))
		{
			UE_LOG(LogTemp, Warning, TEXT("[UnlockGuide] 안내 ID가 중복되어 첫 카메라만 사용합니다: %s"), *It->Entry.Id.ToString());
			continue;
		}
		AddEntry(It->Entry);
		Cameras.Add(It->Entry.Id, *It);
	}
	Entries.Sort([](const FCSUnlockGuideEntry& A, const FCSUnlockGuideEntry& B)
	{
		return A.DisplayOrder != B.DisplayOrder ? A.DisplayOrder < B.DisplayOrder : A.Id.LexicalLess(B.Id);
	});
	bConfigured = true;
	Pending.RemoveAll([this](FName Id) { return !FindEntry(Id) || Seen.Contains(Id); });
	NotificationIds = Pending;
	// 새 게임과 구버전 저장은 현재 레벨을 기준점으로 삼아 기존 해금을 반복 안내하지 않습니다.
	if (ObservedLevel == INDEX_NONE) ObservedLevel = StoreState->GetStoreLevel();
	if (UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get())
		WidgetAddedHandle = Viewport->OnWidgetAdded.AddUObject(this, &ThisClass::HandleWidgetAdded);
	RefreshWidget();
}

const FCSUnlockGuideEntry* UCSUnlockGuideSubsystem::FindEntry(FName Id) const
{
	return Entries.FindByPredicate([Id](const FCSUnlockGuideEntry& Entry) { return Entry.Id == Id; });
}

const FCSLevelStoryEvent* UCSUnlockGuideSubsystem::FindStoryEvent(int32 Level) const
{
	return StoryEvents.FindByPredicate([Level](const FCSLevelStoryEvent& Event) { return Event.StoreLevel == Level; });
}

void UCSUnlockGuideSubsystem::HandleLevelChanged(int32 NewLevel)
{
	const UCSSaveSubsystem* Save = GetWorld()->GetGameInstance()->GetSubsystem<UCSSaveSubsystem>();
	if (Save && Save->HasPendingLoad()) return;
	Configure();
	if (!bConfigured) return;
	if (ObservedLevel == INDEX_NONE) { ObservedLevel = NewLevel; return; }
	if (NewLevel > ObservedLevel)
	{
		for (const FCSLevelStoryEvent& Event : StoryEvents)
			if (Event.StoreLevel >= 2 && Event.StoreLevel > ObservedLevel && Event.StoreLevel <= NewLevel && !CompletedLevels.Contains(Event.StoreLevel))
				PendingLevels.AddUnique(Event.StoreLevel);
		PendingLevels.Sort();
		for (const FCSUnlockGuideEntry& Entry : Entries)
		{
			if (Entry.RequiredStoreLevel > ObservedLevel && Entry.RequiredStoreLevel <= NewLevel && !Seen.Contains(Entry.Id))
			{
				Pending.AddUnique(Entry.Id);
				if (Stage == EStage::Idle) NotificationIds.AddUnique(Entry.Id);
			}
		}
		if (Stage == EStage::Idle) { bDeferred = false; AutoCollapseTime = 0.0f; RefreshWidget(); }
	}
	ObservedLevel = FMath::Max(ObservedLevel, NewLevel);
}

void UCSUnlockGuideSubsystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	const float RealDelta = FMath::Clamp(static_cast<float>(Now - LastRealTime), 0.0f, 0.1f);
	LastRealTime = Now;
	Configure();
	if (!bConfigured || !IsValid(Controller)) return;
	if (Stage == EStage::Idle && !PendingLevels.IsEmpty())
	{
		StableTime = CanStart() ? StableTime + RealDelta : 0.0f;
		if (StableTime >= 0.35f) BeginNextLevel();
	}
	else if (Stage == EStage::Story)
	{
		HideOtherWidgets();
		if (ActiveScenes.IsValidIndex(SceneIndex) && ActiveScenes[SceneIndex].Lines.IsValidIndex(LineIndex))
		{
			const FCSStoryLine& Line = ActiveScenes[SceneIndex].Lines[LineIndex];
			TextElapsed += RealDelta;
			const int32 Count = FMath::Max(VisibleCharacters, FMath::Min(Line.Text.ToString().Len(), FMath::FloorToInt(TextElapsed * FMath::Max(1.0f, Line.CharactersPerSecond))));
			if (Count != VisibleCharacters) { VisibleCharacters = Count; RefreshPresentation(); }
		}
	}
	else if (Stage == EStage::StoryFadeOut || Stage == EStage::StoryFadeIn)
	{
		HideOtherWidgets();
		UpdateStoryTransition(RealDelta);
	}
	else if (Stage == EStage::LevelUp)
	{
		HideOtherWidgets();
		Elapsed += RealDelta;
		const float Delay = Controller ? FMath::Max(0.1f, Controller->LevelUpAutoAdvanceDelay) : 3.0f;
		if (Elapsed >= Delay) BeginStory();
	}
	else if (Stage == EStage::Goal || Stage == EStage::EyeOpening || Stage == EStage::CameraHold) HideOtherWidgets();
	else if (Stage == EStage::Waiting)
	{
		// 운반 여부는 차단 조건으로 쓰지 않고, 작업과 이동이 잠깐 끝난 시점에 시작합니다.
		StableTime = CanStart() ? StableTime + RealDelta : 0.0f;
		if (StableTime >= 0.35f) BeginGuide();
	}
	else if (Stage == EStage::Camera || Stage == EStage::Returning)
	{
		HideOtherWidgets();
		UpdateCamera(RealDelta);
	}
	else if (Stage == EStage::Summary) HideOtherWidgets();
	else if (!bDeferred && !NotificationIds.IsEmpty())
	{
		AutoCollapseTime += RealDelta;
		if (AutoCollapseTime >= 8.0f) { bDeferred = true; RefreshWidget(); }
	}
}

bool UCSUnlockGuideSubsystem::CanStart() const
{
	if (!Controller || !Controller->CanStartUnlockGuide() || !StoreState) return false;
	if (StoreState->CurrentPhase == ECStorePhase::Settlement || StoreState->CurrentPhase == ECStorePhase::Finished ||
		StoreState->bIsDailySummaryPending || StoreState->CurrentEndOfDayTask != ECEndOfDayTask::None) return false;
	const ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(Controller->GetPawn());
	if (!Player || Player->IsBuildMode() || !Player->GetCharacterMovement()->IsMovingOnGround() ||
		Player->GetVelocity().SizeSquared() > 25.0f) return false;
	if (Controller->GetViewTarget() != Player) return false;
	for (TActorIterator<ACSEndingManager> It(GetWorld()); It; ++It)
		if (It->bEndingStarted) return false;
	return true;
}

void UCSUnlockGuideSubsystem::ViewGuide()
{
	if (Stage != EStage::Idle || !PendingLevels.IsEmpty() || Pending.IsEmpty()) return;
	if (bDeferred) { bDeferred = false; AutoCollapseTime = 0.0f; NotificationIds = Pending; RefreshWidget(); return; }
	RequestIds = NotificationIds;
	bHistory = false;
	Stage = EStage::Waiting;
	StableTime = 0.0f;
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::DeferGuide()
{
	if (Stage == EStage::Waiting) { CancelRequest(); return; }
	if (Stage != EStage::Idle) return;
	bDeferred = true;
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::CancelRequest()
{
	if (Stage != EStage::Waiting) return;
	Stage = EStage::Idle;
	RequestIds.Reset();
	bHistory = false;
	bLevelEvent = false;
	ActiveScenes.Reset();
	ActiveLevel = INDEX_NONE;
	bDeferred = true;
	NotificationIds = Pending;
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::DismissGuide()
{
	if (Stage != EStage::Idle || bDeferred) return;
	for (FName Id : NotificationIds) { Seen.AddUnique(Id); Pending.Remove(Id); }
	NotificationIds = Pending;
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::ShowGuideHistory()
{
	Configure();
	if (Stage != EStage::Idle || !PendingLevels.IsEmpty() || !StoreState) return;
	RequestIds.Reset();
	for (const FCSUnlockGuideEntry& Entry : Entries)
		if (Entry.RequiredStoreLevel <= StoreState->GetStoreLevel()) RequestIds.Add(Entry.Id);
	if (RequestIds.IsEmpty()) return;
	bHistory = true;
	Stage = EStage::Waiting;
	StableTime = 0.0f;
	// 안전한 상태에서는 대기 알림을 표시하지 않고 바로 설명을 읽으실 수 있도록 합니다.
	if (CanStart()) BeginGuide();
	else RefreshWidget();
}

void UCSUnlockGuideSubsystem::BeginGuide()
{
	if (!CanStart()) return;
	Batch.Reset();
	for (FName Id : RequestIds) if (FindEntry(Id)) Batch.AddUnique(Id);
	if (Batch.IsEmpty() && !bLevelEvent) { CancelRequest(); return; }
	if (bLevelEvent && !EnsurePresentationWidget()) { bLevelEvent = false; CancelRequest(); return; }
	// 다시 읽기는 플레이어 시점을 유지하므로 연출 카메라를 생성하지 않습니다.
	if (!bHistory)
	{
		PlaybackCamera = GetWorld()->SpawnActor<ACameraActor>();
		if (!PlaybackCamera) { CancelRequest(); return; }
	}
	PreviousViewTarget = Controller->GetViewTarget();
	bOwnsPause = Controller->SetPause(true);
	if (!bOwnsPause)
	{
		if (PlaybackCamera) PlaybackCamera->Destroy();
		PlaybackCamera = nullptr;
		CancelRequest();
		return;
	}
	bPreviousFullTick = Controller->SetFairyIntroductionTickWhenPaused(true);
	if (ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(Controller->GetPawn()))
	{
		Player->StopJumping();
		Player->ConsumeMovementInputVector();
		Player->GetCharacterMovement()->StopMovementImmediately();
	}
	if (bLevelEvent) Controller->SetGameplayUIInputLock(PresentationWidget, true);
	else Controller->SetGameplayUIInputLock(Widget, true);
	Controller->SetStoreHUDHidden(this, true);
	Controller->SetStoreTopBarHidden(this, true);
	Controller->FlushPressedKeys();
	HideOtherWidgets();
	if (bLevelEvent)
	{
		Stage = EStage::LevelUp;
		Elapsed = 0.0f;
		RefreshWidget();
		return;
	}
	if (bHistory)
	{
		// 카메라 복귀 단계를 거치지 않고 설명 팝업만 표시해 드립니다.
		Stage = EStage::Summary;
		RefreshWidget();
		return;
	}
	BuildPlaybackShots();
	ShotIndex = INDEX_NONE;
	NextShot();
}

bool UCSUnlockGuideSubsystem::EnsurePresentationWidget()
{
	if (PresentationWidget) return true;
	UClass* Class = Controller->LevelPresentationWidgetClass ? Controller->LevelPresentationWidgetClass.Get() : UCSLevelPresentationWidget::StaticClass();
	if (Class->HasAnyClassFlags(CLASS_Abstract)) Class = UCSLevelPresentationWidget::StaticClass();
	PresentationWidget = CreateWidget<UCSLevelPresentationWidget>(Controller, Class);
	if (!PresentationWidget) return false;
	PresentationWidget->AddToViewport(3500);
	PresentationWidget->SetVisibility(ESlateVisibility::Collapsed);
	return true;
}

bool UCSUnlockGuideSubsystem::StartOpeningStory()
{
	Configure();
	if (!Controller || Stage != EStage::Idle || Controller->IsPaused() || !EnsurePresentationWidget()) return false;
	PreviousViewTarget = Controller->GetViewTarget();
	UClass* EyeClass = Controller->OpeningEyeWidgetClass ? Controller->OpeningEyeWidgetClass.Get() : UCSWakeUpWidget::StaticClass();
	if (EyeClass->HasAnyClassFlags(CLASS_Abstract)) EyeClass = UCSWakeUpWidget::StaticClass();
	EyeWidget = CreateWidget<UCSWakeUpWidget>(Controller, EyeClass);
	if (!EyeWidget) return false;
	// 다른 경로에서 이미 시작된 BGM도 프롤로그 동안에는 확실히 정지합니다.
	if (UCSAudioSubsystem* AudioSubsystem = UCSAudioSubsystem::Get(this)) AudioSubsystem->StopBGM();
	bOwnsPause = Controller->SetPause(true);
	if (!bOwnsPause) { EyeWidget = nullptr; return false; }
	bOpening = true;
	bPreviousFullTick = Controller->SetFairyIntroductionTickWhenPaused(true);
	ActiveScenes.Reset();
	ActiveGoal = FText::GetEmpty();
	if (const FCSLevelStoryEvent* Opening = FindStoryEvent(1))
	{
		ActiveScenes = Opening->Scenes;
		ActiveGoal = Opening->NextGoal;
	}
	Stage = EStage::EyeOpening;
	Controller->SetGameplayUIInputLock(PresentationWidget, true);
	Controller->SetStoreHUDHidden(this, true);
	Controller->SetStoreTopBarHidden(this, true);
	Controller->FlushPressedKeys();
	EyeWidget->OnEyeOpeningFinished.AddUniqueDynamic(this, &ThisClass::FinishEyeOpening);
	EyeWidget->AddToViewport(4000);
	HideOtherWidgets();
	return true;
}

void UCSUnlockGuideSubsystem::DebugSkipOpening()
{
	if (!bOpening) return;
	// 정상 눈뜨기 완료 경로와 마찬가지로, 미뤄 둔 기본 BGM을 시작한 뒤 모든 잠금과 UI를 정리합니다.
	if (UCSAudioSubsystem* AudioSubsystem = UCSAudioSubsystem::Get(this)) AudioSubsystem->PlayBGM();
	Finish(true);
}

void UCSUnlockGuideSubsystem::FinishEyeOpening()
{
	if (EyeWidget)
	{
		EyeWidget->OnEyeOpeningFinished.RemoveDynamic(this, &ThisClass::FinishEyeOpening);
		EyeWidget->RemoveFromParent();
		EyeWidget = nullptr;
	}
	// 프롤로그와 눈뜨기 연출이 모두 끝난 뒤에만 상점 기본 BGM을 시작합니다.
	if (UCSAudioSubsystem* AudioSubsystem = UCSAudioSubsystem::Get(this)) AudioSubsystem->PlayBGM();
	if (Stage == EStage::EyeOpening) BeginStory();
}

void UCSUnlockGuideSubsystem::BeginNextLevel()
{
	if (PendingLevels.IsEmpty()) return;
	ActiveLevel = PendingLevels[0];
	const FCSLevelStoryEvent* Event = FindStoryEvent(ActiveLevel);
	if (!Event) { PendingLevels.RemoveAt(0); return; }
	bLevelEvent = true;
	bHistory = false;
	ActiveScenes = Event->Scenes;
	ActiveGoal = Event->NextGoal;
	RequestIds.Reset();
	for (const FCSUnlockGuideEntry& Entry : Entries)
		if (Entry.RequiredStoreLevel == ActiveLevel) RequestIds.AddUnique(Entry.Id);
	Stage = EStage::Waiting;
	BeginGuide();
}

void UCSUnlockGuideSubsystem::BeginStory()
{
	SceneIndex = INDEX_NONE;
	BeginStoryFadeOut();
}

void UCSUnlockGuideSubsystem::BeginStoryFadeOut()
{
	Stage = EStage::StoryFadeOut;
	Elapsed = 0.0f;
	if (PresentationWidget) PresentationWidget->SetStoryFadeOpacity(0.0f);
}

void UCSUnlockGuideSubsystem::NextStoryScene()
{
	++SceneIndex;
	while (ActiveScenes.IsValidIndex(SceneIndex) && ActiveScenes[SceneIndex].Lines.IsEmpty()) ++SceneIndex;
	if (!ActiveScenes.IsValidIndex(SceneIndex))
	{
		if (AActor* Target = PreviousViewTarget.Get()) Controller->SetViewTarget(Target);
		if (bOpening) ShowGoal();
		else
		{
			BuildPlaybackShots();
			ShotIndex = INDEX_NONE;
			if (PlaybackShots.IsEmpty()) ShowGoal();
			else NextShot();
		}
		if (PresentationWidget) PresentationWidget->SetStoryFadeOpacity(0.0f);
		return;
	}
	const FCSStoryScene& Scene = ActiveScenes[SceneIndex];
	AActor* Target = PreviousViewTarget.Get();
	if (Scene.Background == ECSStoryBackground::TaggedCamera && !Scene.CameraTag.IsNone())
	{
		for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
			if (It->ActorHasTag(Scene.CameraTag)) { Target = *It; break; }
	}
	if (Target) Controller->SetViewTarget(Target);
	Stage = EStage::StoryFadeIn;
	Elapsed = 0.0f;
	LineIndex = 0;
	EnterStoryLine();
}

void UCSUnlockGuideSubsystem::UpdateStoryTransition(float RealDelta)
{
	if (!PresentationWidget) { Finish(false); return; }
	Elapsed += RealDelta;
	if (Stage == EStage::StoryFadeOut)
	{
		const float Duration = Controller ? FMath::Max(0.05f, Controller->StoryFadeOutDuration) : 0.35f;
		const float Alpha = FMath::SmoothStep(0.0f, Duration, Elapsed);
		PresentationWidget->SetStoryFadeOpacity(Alpha);
		if (Elapsed >= Duration)
		{
			PresentationWidget->SetStoryFadeOpacity(1.0f);
			NextStoryScene();
		}
		return;
	}
	if (Stage == EStage::StoryFadeIn)
	{
		const float Duration = Controller ? FMath::Max(0.05f, Controller->StoryFadeInDuration) : 0.45f;
		const float Alpha = FMath::SmoothStep(0.0f, Duration, Elapsed);
		PresentationWidget->SetStoryFadeOpacity(1.0f - Alpha);
		if (Elapsed >= Duration)
		{
			PresentationWidget->SetStoryFadeOpacity(0.0f);
			Stage = EStage::Story;
		}
	}
}

void UCSUnlockGuideSubsystem::EnterStoryLine()
{
	VisibleCharacters = 0;
	TextElapsed = 0.0f;
	RefreshPresentation();
}

void UCSUnlockGuideSubsystem::ContinuePresentation()
{
	if (Stage == EStage::LevelUp) { BeginStory(); return; }
	if (Stage == EStage::Goal) { Finish(true); return; }
	if (Stage == EStage::CameraHold) { NextShot(); return; }
	// 화면에 대사가 이미 보이는데 페이드인 단계라 입력이 버려지는 느낌이 없도록 즉시 연출을 완료합니다.
	if (Stage == EStage::StoryFadeIn)
	{
		if (PresentationWidget) PresentationWidget->SetStoryFadeOpacity(0.0f);
		Stage = EStage::Story;
		return;
	}
	if (Stage != EStage::Story || !ActiveScenes.IsValidIndex(SceneIndex)) return;
	const FCSStoryScene& Scene = ActiveScenes[SceneIndex];
	if (!Scene.Lines.IsValidIndex(LineIndex)) return;
	const FCSStoryLine& Line = Scene.Lines[LineIndex];
	const int32 Length = Line.Text.ToString().Len();
	if (VisibleCharacters < Length)
	{
		VisibleCharacters = Length;
		TextElapsed = Length / FMath::Max(1.0f, Line.CharactersPerSecond);
		RefreshPresentation();
		return;
	}
	++LineIndex;
	if (Scene.Lines.IsValidIndex(LineIndex)) EnterStoryLine();
	else BeginStoryFadeOut();
}

void UCSUnlockGuideSubsystem::SkipStoryScene()
{
	if (Stage == EStage::Story && ActiveScenes.IsValidIndex(SceneIndex))
	{
		const FCSStoryScene& Current = ActiveScenes[SceneIndex];
		if (Current.Background == ECSStoryBackground::Illustration && Current.Illustration)
		{
			UTexture2D* Image = Current.Illustration;
			// 같은 그림을 연속된 여러 장면에 나누어 작성하셔도 그림 단위로 전부 건너뜁니다.
			while (ActiveScenes.IsValidIndex(SceneIndex + 1) &&
				ActiveScenes[SceneIndex + 1].Background == ECSStoryBackground::Illustration &&
				ActiveScenes[SceneIndex + 1].Illustration == Image) ++SceneIndex;
		}
		BeginStoryFadeOut();
	}
	else SkipCamera();
}

void UCSUnlockGuideSubsystem::ShowGoal()
{
	// 목표가 비어 있는 행은 빈 팝업을 표시하지 않고 플레이로 이어집니다.
	if (ActiveGoal.IsEmpty()) { Finish(true); return; }
	Stage = EStage::Goal;
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::RefreshPresentation()
{
	if (!PresentationWidget) return;
	FText Heading, Body, Speaker, Caption;
	UTexture2D* Illustration = nullptr;
	UTexture2D* SpeakerPortrait = nullptr;
	ECSLevelPresentationPhase Phase = ECSLevelPresentationPhase::LevelUp;
	ECSStorySpeaker SpeakerType = ECSStorySpeaker::Narration;
	if (Stage == EStage::LevelUp)
	{
		Heading = NSLOCTEXT("LevelPresentation", "LevelUpHeading", "레벨 업");
		if (const FCSLevelStoryEvent* Event = FindStoryEvent(ActiveLevel)) Body = Event->LevelUpMessage;
		if (StoreState)
		{
			PresentationWidget->SetLevelUpDetails(
				FMath::Max(1, ActiveLevel - 1), ActiveLevel,
				StoreState->GetStoreLevelDisplayNameForLevel(ActiveLevel - 1),
				StoreState->GetStoreLevelDisplayNameForLevel(ActiveLevel));
		}
	}
	else if ((Stage == EStage::Story || Stage == EStage::StoryFadeIn || Stage == EStage::StoryFadeOut) && ActiveScenes.IsValidIndex(SceneIndex))
	{
		Phase = ECSLevelPresentationPhase::Story;
		const FCSStoryScene& Scene = ActiveScenes[SceneIndex];
		Caption = Scene.Caption;
		if (Scene.Background == ECSStoryBackground::Illustration) Illustration = Scene.Illustration;
		if (Scene.Lines.IsValidIndex(LineIndex))
		{
			const FCSStoryLine& Line = Scene.Lines[LineIndex];
			SpeakerType = Line.Speaker;
			SpeakerPortrait = Line.SpeakerPortrait;
			Body = FText::FromString(Line.Text.ToString().Left(VisibleCharacters));
			if (Line.Speaker == ECSStorySpeaker::Other) Speaker = Line.SpeakerName;
		}
	}
	else if (Stage == EStage::Camera || Stage == EStage::CameraHold || Stage == EStage::Returning)
	{
		Phase = ECSLevelPresentationPhase::Unlock;
		if ((Stage == EStage::Camera || Stage == EStage::CameraHold) && PlaybackShots.IsValidIndex(ShotIndex))
		{
			Heading = PlaybackShots[ShotIndex].Title;
			Body = PlaybackShots[ShotIndex].Description;
		}
		else { PresentationWidget->SetVisibility(ESlateVisibility::Collapsed); return; }
	}
	else if (Stage == EStage::Goal)
	{
		Phase = ECSLevelPresentationPhase::Goal;
		Heading = FText::FromString(TEXT("다음 목표"));
		Body = ActiveGoal;
	}
	else { PresentationWidget->SetVisibility(ESlateVisibility::Collapsed); return; }
	PresentationWidget->Present(Phase, SpeakerType, Heading, Body, Speaker, Caption,
		Illustration, SpeakerPortrait, Stage == EStage::CameraHold);
	// 눈뜨기 위젯이 제거된 뒤 포커스가 비는 경우가 있으므로 현재 연출 위젯으로 확실히 넘깁니다.
	// GameAndUI를 유지해야 컨트롤러의 Space/Enter 백업 입력도 동작합니다.
	if (Controller && !PresentationWidget->HasUserFocus(Controller))
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetWidgetToFocus(PresentationWidget->TakeWidget());
		Controller->SetInputMode(InputMode);
		PresentationWidget->SetUserFocus(Controller);
	}
}

void UCSUnlockGuideSubsystem::BuildPlaybackShots()
{
	PlaybackShots.Reset();
	for (FName Id : Batch)
	{
		const TWeakObjectPtr<ACSUnlockGuideCamera>* Found = Cameras.Find(Id);
		ACSUnlockGuideCamera* OwnerCamera = Found ? Found->Get() : nullptr;
		const FCSUnlockGuideEntry* Entry = FindEntry(Id);
		if (!OwnerCamera || !Entry) continue;
		auto AddShot = [this, Entry](ACSUnlockGuideCamera* Camera, const FText& Title, const FText& Message, float Duration)
		{
			// 미지정되거나 삭제된 장소는 건너뛰며, 해금 설명은 마지막 팝업에 그대로 제공합니다.
			if (!IsValid(Camera) || Camera->GetWorld() != GetWorld() || !Camera->GuideCamera || !Camera->EndPoint || Camera->IsHidden()) return;
			FPlaybackShot& Shot = PlaybackShots.AddDefaulted_GetRef();
			Shot.Camera = Camera;
			Shot.Title = Title.IsEmpty() ? Entry->Title : Title;
			Shot.Description = Message.IsEmpty() ? Entry->Description : Message;
			Shot.Duration = FMath::Max(0.1f, Duration > 0.0f ? Duration : Camera->ShotDuration);
		};
		if (OwnerCamera->Shots.IsEmpty())
		{
			// 기존 안내 액터를 수정하지 않으셔도 단일 카메라 연출이 그대로 동작합니다.
			AddShot(OwnerCamera, Entry->Title, FText::GetEmpty(), 0.0f);
		}
		else
		{
			for (const FCSUnlockGuideShot& Shot : OwnerCamera->Shots)
				AddShot(Shot.Camera, Shot.Title, Shot.Message, Shot.Duration);
		}
	}
}

void UCSUnlockGuideSubsystem::NextShot()
{
	ClearSequence();
	ACSUnlockGuideCamera* Camera = nullptr;
	while (++ShotIndex < PlaybackShots.Num())
	{
		Camera = PlaybackShots[ShotIndex].Camera.Get();
		if (Camera && Camera->GuideCamera && Camera->EndPoint && !Camera->IsHidden()) break;
		Camera = nullptr;
	}
	if (!Camera) { BeginReturn(); return; }
	Stage = EStage::Camera;
	Elapsed = 0.0f;
	ShotStart = Camera->GuideCamera->GetComponentTransform();
	ShotEnd = Camera->EndPoint->GetComponentTransform();
	ShotFOV = Camera->GuideCamera->FieldOfView;
	PlaybackCamera->SetActorTransform(ShotStart);
	PlaybackCamera->GetCameraComponent()->SetFieldOfView(ShotFOV);
	Controller->SetViewTarget(PlaybackCamera);
	// 멀리 떨어진 위치는 벽을 통과하는 이동 대신 실제 시간으로 계산하는 암전으로 연결합니다.
	Controller->PlayerCameraManager->SetManualCameraFade(1.0f, FLinearColor::Black, false);
	SequenceDuration = 0.0f;
	if (Camera->Sequence)
	{
		FMovieSceneSequencePlaybackSettings Settings;
		Settings.FinishCompletionStateOverride = EMovieSceneCompletionModeOverride::ForceRestoreState;
		ALevelSequenceActor* CreatedActor = nullptr;
		SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(), Camera->Sequence, Settings, CreatedActor);
		SequenceActor = CreatedActor;
		if (SequencePlayer)
		{
			SequenceStartSeconds = static_cast<float>(SequencePlayer->GetStartTime().AsSeconds());
			SequenceDuration = static_cast<float>(SequencePlayer->GetDuration().AsSeconds());
			SequencePlayer->Play();
			SequencePlayer->Pause();
		}
	}
	if (PresentationWidget)
	{
		const float EffectiveDuration = SequenceDuration > 0.0f
			? SequenceDuration
			: PlaybackShots[ShotIndex].Duration;
		PresentationWidget->BeginUnlockShot(EffectiveDuration);
	}
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::HoldCamera()
{
	// 이동이 끝난 구도와 설명을 유지하고, 다음 입력을 받으실 때까지 기다립니다.
	Stage = EStage::CameraHold;
	if (Controller && Controller->PlayerCameraManager) Controller->PlayerCameraManager->StopCameraFade();
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::UpdateCamera(float RealDelta)
{
	if (!PlaybackCamera || !Controller->PlayerCameraManager) { Finish(false); return; }
	Elapsed += RealDelta;
	if (Stage == EStage::Returning)
	{
		// 먼 위치에서 복귀할 때는 암전 중 시점을 바꾸어 벽을 관통하는 이동을 감춥니다.
		const float Alpha = bFadeReturn ? (Elapsed >= 0.2f ? 1.0f : 0.0f) : FMath::SmoothStep(0.0f, 0.6f, Elapsed);
		const float Fade = bFadeReturn ? (Elapsed < 0.2f ? Elapsed / 0.2f : (0.6f - Elapsed) / 0.4f) : 0.0f;
		Controller->PlayerCameraManager->SetManualCameraFade(FMath::Clamp(Fade, 0.0f, 1.0f), FLinearColor::Black, false);
		PlaybackCamera->SetActorLocationAndRotation(FMath::Lerp(ReturnStart.GetLocation(), ReturnEnd.GetLocation(), Alpha),
			FQuat::Slerp(ReturnStart.GetRotation(), ReturnEnd.GetRotation(), Alpha));
		PlaybackCamera->GetCameraComponent()->SetFieldOfView(FMath::Lerp(ShotFOV, ReturnFOV, Alpha));
		if (Elapsed >= 0.6f)
		{
			Controller->PlayerCameraManager->StopCameraFade();
			AActor* Target = PreviousViewTarget.Get();
			if (!Target) Target = Controller->GetPawn();
			if (Target) Controller->SetViewTarget(Target);
			Stage = bLevelEvent ? EStage::Goal : EStage::Summary;
			RefreshWidget();
		}
		return;
	}
	if (!PlaybackShots.IsValidIndex(ShotIndex)) { BeginReturn(); return; }
	ACSUnlockGuideCamera* Camera = PlaybackShots[ShotIndex].Camera.Get();
	if (!Camera) { NextShot(); return; }
	const float Duration = SequenceDuration > 0.0f ? SequenceDuration : PlaybackShots[ShotIndex].Duration;
	const float FadeTime = FMath::Min(0.2f, Duration * 0.25f);
	const float FadeAmount = bLevelEvent ? FMath::Max(0.0f, 1.0f - Elapsed / FadeTime)
		: FMath::Max(1.0f - Elapsed / FadeTime, (Elapsed - Duration + FadeTime) / FadeTime);
	Controller->PlayerCameraManager->SetManualCameraFade(FMath::Clamp(FadeAmount, 0.0f, 1.0f), FLinearColor::Black, false);
	if (SequencePlayer && SequenceDuration > 0.0f)
	{
		// 일시정지된 월드 타이머 대신 실제 시간으로 시퀀스의 평가 위치를 갱신합니다.
		SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(
			SequenceStartSeconds + FMath::Min(Elapsed, FMath::Max(0.0f, SequenceDuration - 0.0001f)), EUpdatePositionMethod::Play));
		if (Elapsed >= SequenceDuration) NextShot();
	}
	else
	{
		const float Alpha = FMath::SmoothStep(0.0f, Duration, Elapsed);
		const FVector Location = FMath::Lerp(ShotStart.GetLocation(), ShotEnd.GetLocation(), Alpha);
		const FQuat Rotation = Camera->LookAtActor ? (Camera->LookAtActor->GetActorLocation() - Location).Rotation().Quaternion()
			: FQuat::Slerp(ShotStart.GetRotation(), ShotEnd.GetRotation(), Alpha);
		PlaybackCamera->SetActorLocationAndRotation(Location, Rotation);
		const float EndFOV = Camera->EndFieldOfView > 0.0f ? FMath::Clamp(Camera->EndFieldOfView, 5.0f, 170.0f) : ShotFOV;
		PlaybackCamera->GetCameraComponent()->SetFieldOfView(FMath::Lerp(ShotFOV, EndFOV, Alpha));
		if (Elapsed >= Duration) NextShot();
	}
}

void UCSUnlockGuideSubsystem::BeginReturn()
{
	if (!Controller || !Controller->PlayerCameraManager) { Finish(false); return; }
	ReturnStart = FTransform(Controller->PlayerCameraManager->GetCameraRotation(), Controller->PlayerCameraManager->GetCameraLocation());
	ShotFOV = Controller->PlayerCameraManager->GetFOVAngle();
	ClearSequence();
	AActor* Target = PreviousViewTarget.Get();
	if (!Target) Target = Controller->GetPawn();
	FMinimalViewInfo View;
	if (Target) Target->CalcCamera(0.0f, View);
	else { Finish(false); return; }
	ReturnEnd = FTransform(View.Rotation, View.Location);
	bFadeReturn = FVector::DistSquared(ReturnStart.GetLocation(), ReturnEnd.GetLocation()) > FMath::Square(500.0f);
	ReturnFOV = View.FOV;
	Controller->PlayerCameraManager->StopCameraFade();
	PlaybackCamera->SetActorTransform(ReturnStart);
	Controller->SetViewTarget(PlaybackCamera);
	Elapsed = 0.0f;
	Stage = EStage::Returning;
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::SkipCamera()
{
	if (Stage == EStage::Camera || Stage == EStage::CameraHold) BeginReturn();
}

void UCSUnlockGuideSubsystem::CloseSummary()
{
	if (Stage == EStage::Summary) Finish(true);
}

void UCSUnlockGuideSubsystem::Finish(bool bMarkSeen)
{
	if (EyeWidget)
	{
		EyeWidget->OnEyeOpeningFinished.RemoveDynamic(this, &ThisClass::FinishEyeOpening);
		EyeWidget->RemoveFromParent();
		EyeWidget = nullptr;
	}
	ClearSequence();
	// 개별 HUD 숨김 요청을 해제하기 전에 스냅샷을 복원하여 HUD가 다시 숨겨지지 않게 합니다.
	RestoreOtherWidgets();
	if (Controller && bOwnsPause)
	{
		if (Controller->PlayerCameraManager) Controller->PlayerCameraManager->StopCameraFade();
		AActor* Target = PreviousViewTarget.Get();
		if (!Target) Target = Controller->GetPawn();
		if (Target) Controller->SetViewTarget(Target);
		Controller->SetGameplayUIInputLock(Widget, false);
		if (PresentationWidget) Controller->SetGameplayUIInputLock(PresentationWidget, false);
		Controller->SetStoreHUDHidden(this, false);
		Controller->SetStoreTopBarHidden(this, false);
		Controller->SetFairyIntroductionTickWhenPaused(bPreviousFullTick);
		Controller->SetPause(false);
	}
	if (PlaybackCamera) PlaybackCamera->Destroy();
	PlaybackCamera = nullptr;
	if (bMarkSeen) for (FName Id : Batch) { Seen.AddUnique(Id); Pending.Remove(Id); }
	if (bMarkSeen && bLevelEvent)
	{
		CompletedLevels.AddUnique(ActiveLevel);
		PendingLevels.Remove(ActiveLevel);
	}
	bOpening = false;
	bLevelEvent = false;
	ActiveLevel = INDEX_NONE;
	ActiveScenes.Reset();
	if (PresentationWidget) PresentationWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (PresentationWidget) PresentationWidget->SetStoryFadeOpacity(0.0f);
	bOwnsPause = false;
	bHistory = false;
	Stage = EStage::Idle;
	Batch.Reset();
	PlaybackShots.Reset();
	RequestIds.Reset();
	NotificationIds = Pending;
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::ClearSequence()
{
	if (SequencePlayer) SequencePlayer->Stop();
	SequencePlayer = nullptr;
	if (SequenceActor) SequenceActor->Destroy();
	SequenceActor = nullptr;
}

void UCSUnlockGuideSubsystem::HandleWidgetAdded(UWidget* AddedWidget, ULocalPlayer* Player)
{
	if (!bOwnsPause || !AddedWidget || AddedWidget == Widget || AddedWidget == PresentationWidget || AddedWidget == EyeWidget || AddedWidget->GetWorld() != GetWorld()) return;
	HiddenWidgets.FindOrAdd(AddedWidget, AddedWidget->GetVisibility());
	AddedWidget->SetVisibility(ESlateVisibility::Hidden);
}

void UCSUnlockGuideSubsystem::HideOtherWidgets()
{
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), Widgets, UUserWidget::StaticClass(), true);
	for (UUserWidget* Other : Widgets)
	{
		if (Other == Widget || Other == PresentationWidget || Other == EyeWidget || Other->GetWorld() != GetWorld() || !Other->IsInViewport()) continue;
		HiddenWidgets.FindOrAdd(Other, Other->GetVisibility());
		Other->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UCSUnlockGuideSubsystem::RestoreOtherWidgets()
{
	for (const auto& Pair : HiddenWidgets)
		if (UWidget* Other = Pair.Key.Get()) Other->SetVisibility(Pair.Value);
	HiddenWidgets.Reset();
}

void UCSUnlockGuideSubsystem::RefreshWidget()
{
	if (bShuttingDown || !Controller) return;
	if (bOpening || bLevelEvent)
	{
		if (Widget) Widget->SetVisibility(ESlateVisibility::Collapsed);
		RefreshPresentation();
		return;
	}
	if (!Widget)
	{
		UClass* Class = Controller->UnlockGuideWidgetClass ? Controller->UnlockGuideWidgetClass.Get() : UCSUnlockGuideWidget::StaticClass();
		if (Class->HasAnyClassFlags(CLASS_Abstract)) Class = UCSUnlockGuideWidget::StaticClass();
		Widget = CreateWidget<UCSUnlockGuideWidget>(Controller, Class);
		if (!Widget) return;
		Widget->AddToViewport(3000);
	}
	// 레벨업은 자동 팝업으로 시작하며, 이전 안내 보기·나중에 보기 알림은 표시하지 않습니다.
	if (Stage == EStage::Idle) { Widget->SetVisibility(ESlateVisibility::Collapsed); return; }
	if (NotificationIds.IsEmpty() && Stage == EStage::Idle) NotificationIds = Pending;
	FString Body;
	FText Heading;
	if (Stage == EStage::Camera)
	{
		Heading = PlaybackShots.IsValidIndex(ShotIndex) ? PlaybackShots[ShotIndex].Title : FText::GetEmpty();
	}
	else if (Stage == EStage::Returning) Heading = FText::FromString(TEXT("플레이어 시점으로 돌아갑니다."));
	else if (Stage == EStage::Summary)
	{
		Heading = FText::FromString(bHistory ? TEXT("해금 안내 다시 읽기") : TEXT("새로운 기능이 해금되었습니다!"));
		for (FName Id : Batch)
			if (const FCSUnlockGuideEntry* Entry = FindEntry(Id))
				Body += Entry->Title.ToString() + TEXT("\n") + Entry->Description.ToString() + TEXT("\n") + Entry->UsageLocation.ToString() + TEXT("\n\n");
	}
	else if (Stage == EStage::Waiting && bHistory)
	{
		Heading = FText::FromString(TEXT("해금 안내 다시 읽기를 준비하고 있습니다."));
		Body = TEXT("현재 작업을 마치고 잠깐 멈추시면 설명을 표시합니다. 요청을 취소하실 수 있습니다.");
	}
	else
	{
		Heading = FText::Format(FText::FromString(TEXT("해금 안내 · {0}개")), FText::AsNumber(Pending.Num()));
		if (Stage == EStage::Waiting) Body = TEXT("현재 작업을 마치고 잠깐 멈추시면 안내를 시작합니다. 요청을 취소하실 수 있습니다.");
		else for (FName Id : NotificationIds) if (const FCSUnlockGuideEntry* Entry = FindEntry(Id)) Body += Entry->Title.ToString() + TEXT("\n");
	}
	Widget->Present(Heading, FText::FromString(Body), bDeferred && Stage == EStage::Idle,
		Stage == EStage::Waiting, Stage == EStage::Camera || Stage == EStage::Returning, Stage == EStage::Summary);
	if (bOwnsPause) Widget->SetKeyboardFocus();
}

void UCSUnlockGuideSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	OutSave.UnlockGuideObservedLevel = ObservedLevel;
	OutSave.UnlockGuideSeen = Seen;
	OutSave.UnlockGuidePending = Pending;
	OutSave.bUnlockGuideDeferred = bDeferred;
	OutSave.PendingLevelStories = PendingLevels;
	OutSave.CompletedLevelStories = CompletedLevels;
}

void UCSUnlockGuideSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	Finish(false);
	ObservedLevel = InSave.UnlockGuideObservedLevel == INDEX_NONE ? InSave.StoreState.CurrentStoreLevel : InSave.UnlockGuideObservedLevel;
	Seen = InSave.UnlockGuideSeen;
	Pending = InSave.UnlockGuidePending;
	bDeferred = InSave.bUnlockGuideDeferred;
	PendingLevels = InSave.PendingLevelStories;
	CompletedLevels = InSave.CompletedLevelStories;
	NotificationIds = Pending;
	Configure();
	PendingLevels.RemoveAll([this](int32 Level) { return Level < 2 || !FindStoryEvent(Level) || CompletedLevels.Contains(Level); });
	PendingLevels.Sort();
	if (bConfigured) Pending.RemoveAll([this](FName Id) { return !FindEntry(Id) || Seen.Contains(Id); });
	NotificationIds = Pending;
	RefreshWidget();
}

void UCSUnlockGuideSubsystem::Deinitialize()
{
	bShuttingDown = true;
	if (StoreState) StoreState->OnStoreLevelChanged.RemoveDynamic(this, &ThisClass::HandleLevelChanged);
	if (UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get()) Viewport->OnWidgetAdded.Remove(WidgetAddedHandle);
	Finish(false);
	if (Widget) Widget->RemoveFromParent();
	if (PresentationWidget) PresentationWidget->RemoveFromParent();
	PresentationWidget = nullptr;
	Widget = nullptr;
	Super::Deinitialize();
}
