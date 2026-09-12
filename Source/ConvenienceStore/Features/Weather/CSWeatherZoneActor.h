	#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSWeatherZoneActor.generated.h"

class UBoxComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPrimitiveComponent;
class USceneComponent;

// 문 밖에 비와 눈을 표시할 연출 영역
// 연출 범위와 플레이어 판정 범위를 별도 박스로 관리
// 날씨 에셋과 표시 상태는 날씨 서브시스템에서 전달받아 적용
UCLASS()
class CONVENIENCESTORE_API ACSWeatherZoneActor : public AActor
{
	GENERATED_BODY()

public:
	ACSWeatherZoneActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	// 지정한 액터가 JudgeBox와 겹쳐 있는지 반환
	bool IsActorInJudgeBox(const AActor* Actor) const;

	// 전달받은 날씨 에셋을 영역 나이아가라에 적용
	// 에셋이 없으면 즉시 비활성화하고, 다른 에셋이면 교체 후 재생 시작
	// 영역의 표시 여부와 파티클 활성화 상태는 별도로 관리
	void ApplyZoneSystem(UNiagaraSystem* System);

	// 문 앞 날씨 파티클의 표시 여부만 변경
	// 숨길 때 파티클을 비활성화하지 않음
	void SetZoneVisible(bool bVisible);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 나이아가라 생성 위치를 VisualBox 윗면 중앙으로 이동하고 박스 회전을 반영
	// 박스 크기와 원본 생성 평면 크기로 X·Y 배율을 계산하며 Z 배율은 1로 유지
	void UpdateZoneNiagaraTransform();

	// 판정 박스에 액터가 들어오면 서브시스템에 재판정을 요청
	// 플레이어 여부는 서브시스템에서 확인
	UFUNCTION()
	void HandleJudgeBoxBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	// 판정 박스에서 액터가 나가면 서브시스템에 재판정을 요청
	// 다른 실내 영역이나 문 앞 박스에 걸쳐 있는지는 전체 영역을 조회해 확인
	UFUNCTION()
	void HandleJudgeBoxEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 비와 눈이 생성될 위치와 범위를 정하는 연출용 박스
	// 박스 윗면을 생성 평면으로 사용하며, 충돌과 실내·외 판정에는 사용하지 않음
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> VisualBox;

	// 캐릭터 주위 연출에서 문 앞 연출로 전환할 위치를 판정하는 박스
	// Pawn 오버랩을 감지하며, 실제 플레이어인지는 서브시스템에서 확인
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> JudgeBox;

	// 문 앞 영역의 비와 눈을 재생할 나이아가라 컴포넌트
	// 날씨 에셋이 적용되기 전에는 자동으로 활성화하지 않음
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> ZoneNiagara;

	// 나이아가라 에셋에 설정된 원본 생성 평면 크기
	// VisualBox 크기에 맞는 배율을 계산할 때 사용
	// 에셋의 ShapeLocation Plane Size와 맞추고 UseOwnerScale을 활성화
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Zone", meta = (ClampMin = "1.0"))
	FVector2D ZoneAssetPlaneSize = FVector2D(100.0, 100.0);
};
