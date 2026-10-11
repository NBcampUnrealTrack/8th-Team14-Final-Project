#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "UI/Audio/CSFantasyShopStyle.h"
#include "CSJukeboxWidget.generated.h"

class UTextBlock;
class UVerticalBox;
class UCSAudioSubsystem;
class ACSStoreGameState;
class UCSJukeboxWidget;

UCLASS()
class CONVENIENCESTORE_API UCSJukeboxTrackButton : public UButton
{
	GENERATED_BODY()
public:
	FName TrackId;
	UPROPERTY(Transient) TObjectPtr<UCSJukeboxWidget> OwnerWidget;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusLabel;
	UFUNCTION() void Choose();
};

UCLASS(Blueprintable)
class CONVENIENCESTORE_API UCSJukeboxWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UCSJukeboxWidget(const FObjectInitializer& Initializer);
	UFUNCTION(BlueprintCallable, Category = "Jukebox") void SelectTrack(FName Id);
	UFUNCTION(BlueprintCallable, Category = "Jukebox") void ApplySelection();
	UFUNCTION(BlueprintCallable, Category = "Jukebox") void Close();
	UFUNCTION(BlueprintPure, Category = "Jukebox") bool CanApply() const;
	UFUNCTION(BlueprintPure, Category = "Jukebox") FName GetCandidateId() const { return CandidateId; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
	UPROPERTY() FCSFantasyShopStyle ShopStyle;
	UFUNCTION() void RefreshView();
	FName CandidateId;
	UPROPERTY(Transient) TObjectPtr<UCSAudioSubsystem> Audio;
	UPROPERTY(Transient) TObjectPtr<ACSStoreGameState> State;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> TrackList;
	UPROPERTY(Transient) TObjectPtr<UButton> ApplyButton;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UCSJukeboxTrackButton>> Rows;
};
