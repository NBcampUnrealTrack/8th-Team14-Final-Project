#pragma once

#include "CoreMinimal.h"
#include "Data/CSNoticeEventTypes.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSNoticeBoardActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UCSNoticeBoardWidget;

/**
 * 월드에 배치하는 게시판입니다.
 *
 * DisplayWidgetComponent는 상호작용 없이 보이는 간략 3D UI용이며,
 * 전체 탭 UI는 플레이어가 Interact 했을 때만 별도의 화면 위젯으로 엽니다.
 */
UCLASS()
class CONVENIENCESTORE_API ACSNoticeBoardActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSNoticeBoardActor();

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 현재 열려 있는 전체 화면 게시판을 닫습니다. */
	UFUNCTION(BlueprintCallable, Category = "Notice Board")
	void CloseNoticeBoardUI(UCSNoticeBoardWidget* RequestingWidget);

	UFUNCTION(BlueprintPure, Category = "Notice Board")
	bool IsNoticeBoardOpen() const;

	/** 게시판은 상태를 소유하지 않고 NoticeEventSubsystem의 오늘 뉴스만 전달합니다. */
	UFUNCTION(BlueprintPure, Category = "Notice Board|Events")
	bool HasTodayNews() const;

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Events")
	bool GetTodayNews(FCSNewsEventDataRow& OutNews) const;

	UFUNCTION(BlueprintPure, Category = "Notice Board|Events")
	bool HasTodayHoliday() const;

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Events")
	bool GetTodayHoliday(FCSHolidayEventDataRow& OutHoliday) const;

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Events")
	void GetTodayHolidays(TArray<FCSHolidayEventDataRow>& OutHolidays) const;

	UFUNCTION(BlueprintPure, Category = "Notice Board")
	UWidgetComponent* GetDisplayWidgetComponent() const { return DisplayWidgetComponent; }

protected:
	/** 게시판 모델. 에디터에서 원하는 메시를 지정합니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	/** 월드 공간에서 항상 표시할 날씨 요약 및 알림 룬 위젯입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> DisplayWidgetComponent;

	/** 게시판 근처의 상호작용 범위를 시각화하고 조정하기 위한 콜리전입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> InteractionCollision;

	/** 상호작용 시 전체 화면에 표시할 UMG 위젯 블루프린트입니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|UI")
	TSubclassOf<UCSNoticeBoardWidget> NoticeBoardWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UCSNoticeBoardWidget> ActiveNoticeBoardWidget;
};
