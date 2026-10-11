#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Audio/CSFantasyShopStyle.h"
#include "CSGachaWellWidget.generated.h"

class ACSGachaMachine;
class ACSStoreGameState;
class UCSProductVisualRegistry;
class UVerticalBox;
class UButton;
class UTextBlock;
class USizeBox;

UCLASS(Blueprintable)
class CONVENIENCESTORE_API UCSGachaWellWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UCSGachaWellWidget(const FObjectInitializer& Initializer);
	void SetMachine(ACSGachaMachine* InMachine) { Machine=InMachine; }
	UFUNCTION(BlueprintCallable) void Draw();
	UFUNCTION(BlueprintCallable) void Browse();
	UFUNCTION(BlueprintCallable) void Back();
	UFUNCTION(BlueprintCallable) void Close();
	UFUNCTION(BlueprintPure) int32 GetDisplayedRewardCount() const { return DisplayedRewardCount; }
	UFUNCTION(BlueprintPure) bool IsBrowsing() const { return bBrowsing; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
	UPROPERTY() FCSFantasyShopStyle ShopStyle;
	UPROPERTY() TObjectPtr<UCSProductVisualRegistry> ProductVisuals;
	UPROPERTY(Transient) TObjectPtr<ACSGachaMachine> Machine;
	UPROPERTY(Transient) TObjectPtr<ACSStoreGameState> State;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Menu;
	UPROPERTY(Transient) TObjectPtr<USizeBox> ListPanel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> RewardList;
	UPROPERTY(Transient) TObjectPtr<UButton> DrawButton;
	UPROPERTY(Transient) TObjectPtr<UButton> BrowseButton;
	UPROPERTY(Transient) TObjectPtr<UButton> BackButton;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CostLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BrowseHint;
	int32 DisplayedRewardCount=0;
	bool bBrowsing=false;
};
