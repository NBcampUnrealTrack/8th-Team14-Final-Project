#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSWeatherIndoorArea.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

// 플레이어의 실내 진입 여부를 판정할 날씨 전용 영역
// 청결도용 실내 영역과 별도로 배치하며, 판정 결과는 날씨 서브시스템에서 관리
UCLASS()
class CONVENIENCESTORE_API ACSWeatherIndoorArea : public AActor
{
	GENERATED_BODY()

public:
	ACSWeatherIndoorArea();

	// 지정한 액터가 IndoorBox와 겹쳐 있는지 반환
	bool IsActorInside(const AActor* Actor) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 실내 박스에 액터가 들어오면 서브시스템에 재판정을 요청
	UFUNCTION()
	void HandleIndoorBoxBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	// 실내 박스에서 액터가 나가면 서브시스템에 재판정을 요청
	// 다른 판정 영역에도 걸쳐 있으면 보호 상태를 유지
	UFUNCTION()
	void HandleIndoorBoxEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	// 실내 범위를 지정하는 루트 박스 컴포넌트
	// Pawn 오버랩을 감지하며, 실제 플레이어인지는 서브시스템에서 확인
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> IndoorBox;
};
