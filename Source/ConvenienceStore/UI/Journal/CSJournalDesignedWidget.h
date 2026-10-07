#pragma once
#include "CoreMinimal.h"
#include "Components/Button.h"
#include "UI/Journal/CSJournalWidget.h"
#include "CSJournalDesignedWidget.generated.h"

UENUM(BlueprintType)
enum class ECSJournalButtonAction : uint8
{
	Page,
	Day,
	PreviousMonth,
	NextMonth,
	Artifact,
	UseArtifact,
	CancelArtifactUse
};

// 디자이너에서 동작 종류와 값을 지정할 수 있는 수첩 버튼입니다.
UCLASS(meta = (DisplayName = "Journal Action Button"))
class CONVENIENCESTORE_API UCSJournalActionButton : public UButton
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal")
	ECSJournalButtonAction Action = ECSJournalButtonAction::Page;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal") int32 Value = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal") FName ArtifactId;
	void Connect(UCSJournalWidget* InJournal);

private:
	UPROPERTY(Transient) TObjectPtr<UCSJournalWidget> OwnerJournal;
	UFUNCTION() void ExecuteAction();
};

// 아티팩트 셀에 일반 UButton을 사용해도 클릭한 셀의 ID를 수첩에 전달합니다.
UCLASS()
class CONVENIENCESTORE_API UCSJournalArtifactClickProxy : public UObject
{
	GENERATED_BODY()
public:
	void Connect(UCSJournalWidget* InJournal, FName InArtifactId);
	UFUNCTION() void Execute();

private:
	UPROPERTY(Transient) TObjectPtr<UCSJournalWidget> OwnerJournal;
	FName ArtifactId;
};

// 레이아웃은 저장된 WBP 트리에서 가져오며 이름으로 연결된 표시 내용만 갱신합니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSJournalDesignedWidget : public UCSJournalWidget
{
	GENERATED_BODY()
	friend class FCSJournalAssetTest;

public:
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design") TSubclassOf<UUserWidget> ArtifactCellClass;
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design|Artifacts")
	FMargin ArtifactCellPadding = FMargin(6.0f);
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design|Artifacts")
	FLinearColor ArtifactDefaultTextColor = FLinearColor::FromSRGBColor(FColor(54, 39, 22));
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design|Artifacts")
	FLinearColor ArtifactSelectedTextColor = FLinearColor::FromSRGBColor(FColor(241, 226, 181));
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design|Tabs")
	FLinearColor DefaultTabTextColor = FLinearColor::FromSRGBColor(FColor(54, 39, 22));
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design|Tabs")
	FLinearColor SelectedTabTextColor = FLinearColor::FromSRGBColor(FColor(241, 226, 181));
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design|Calendar")
	FLinearColor DefaultDayTextColor = FLinearColor::FromSRGBColor(FColor(54, 39, 22));
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design|Calendar")
	FLinearColor SelectedDayTextColor = FLinearColor::FromSRGBColor(FColor(250, 244, 221));
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design|Calendar")
	FLinearColor UnavailableDayTextColor = FLinearColor::FromSRGBColor(FColor(166, 157, 132));

protected:
	virtual void NativeConstruct() override;
	virtual void ApplyJournalView() override;
	virtual void ApplyMapView(const FCSJournalMapView& Data) override;
	virtual void ApplyDeliveryClock() override;

private:
	void SetLabel(FName Name, const FText& Text);
	void SetLabel(FName Name, const FString& Text);
	void RefreshCalendarEvents();
	void RefreshDeliveries();
	void RefreshArtifactCells();
	UPROPERTY(Transient) TArray<TObjectPtr<UUserWidget>> ArtifactCells;
	UPROPERTY(Transient) TArray<TObjectPtr<UCSJournalArtifactClickProxy>> ArtifactClickProxies;
	UPROPERTY(Transient) TObjectPtr<UCSJournalActionButton> ArtifactUseButton;
	UPROPERTY(Transient) TObjectPtr<UCSJournalActionButton> ArtifactCancelUseButton;
	UPROPERTY(Transient) TObjectPtr<UCSJournalCatalog> DisplayedCatalog;
	int32 DisplayedArtifactCount = INDEX_NONE;
	uint32 MapLayoutHash = 0;
	bool bMapLayoutBuilt = false;
};
