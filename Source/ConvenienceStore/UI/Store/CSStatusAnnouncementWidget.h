#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSStatusAnnouncementWidget.generated.h"

class UTextBlock;
class UWidgetAnimation;

UENUM(BlueprintType)
enum class ECSStatusAnnouncementType : uint8
{
	Opened,
	Closed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnStatusAnnouncementFinished);

// 영업 시작·종료처럼 매장 상태가 바뀌는 중요한 순간을 공통으로 안내하는 위젯입니다.
// 문구는 C++에서 관리하고, 배경지·빛나는 띠·글로우 연출은 파생 WBP에서 구현합니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSStatusAnnouncementWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 안내 타입에 맞는 문구를 적용하고 WBP의 등장 연출을 요청합니다.
	UFUNCTION(BlueprintCallable, Category = "Store|Announcement")
	void PlayAnnouncement(ECSStatusAnnouncementType InAnnouncementType);

	// WBP 애니메이션의 마지막 프레임에서 호출합니다.
	UFUNCTION(BlueprintCallable, Category = "Store|Announcement")
	void NotifyAnnouncementFinished();

	UPROPERTY(BlueprintAssignable, Category = "Store|Announcement")
	FCSOnStatusAnnouncementFinished OnAnnouncementFinished;

	UFUNCTION(BlueprintPure, Category = "Store|Announcement")
	ECSStatusAnnouncementType GetAnnouncementType() const
	{
		return AnnouncementType;
	}

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// WBP에서 같은 이름의 애니메이션을 만들면 C++에서 자동으로 연결합니다.
	UPROPERTY(Transient, meta = (BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> AnnouncementAnimation;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> AnnouncementText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Announcement|Text")
	FText OpenedAnnouncementText = NSLOCTEXT(
		"StoreAnnouncement",
		"StoreOpened",
		"영업을 시작하였습니다");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Announcement|Text")
	FText ClosedAnnouncementText = NSLOCTEXT(
		"StoreAnnouncement",
		"StoreClosed",
		"영업시간이 종료되었습니다");

private:
	UFUNCTION()
	void HandleAnnouncementAnimationFinished();

	UPROPERTY(Transient)
	ECSStatusAnnouncementType AnnouncementType = ECSStatusAnnouncementType::Opened;

	bool bIsAnnouncementPlaying = false;
};
