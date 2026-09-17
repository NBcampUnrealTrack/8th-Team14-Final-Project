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
	Artifact
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

// 레이아웃은 저장된 WBP 트리에서 가져오며 이름으로 연결된 표시 내용만 갱신합니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSJournalDesignedWidget : public UCSJournalWidget
{
	GENERATED_BODY()
	friend class FCSJournalAssetTest;

public:
	UPROPERTY(EditDefaultsOnly, Category = "Journal|Design") TSubclassOf<UUserWidget> ArtifactCellClass;

protected:
	virtual void NativeConstruct() override;
	virtual void ApplyJournalView() override;
	virtual void ApplyMapView(const FCSJournalMapView& Data) override;
	virtual void ApplyDeliveryClock() override;

private:
	void SetLabel(FName Name, const FText& Text);
	void SetLabel(FName Name, const FString& Text);
	void RefreshDeliveries();
	void RefreshArtifactCells();
	UPROPERTY(Transient) TArray<TObjectPtr<UUserWidget>> ArtifactCells;
	UPROPERTY(Transient) TObjectPtr<UCSJournalCatalog> DisplayedCatalog;
	int32 DisplayedArtifactCount = INDEX_NONE;
	uint32 MapLayoutHash = 0;
	bool bMapLayoutBuilt = false;
};
