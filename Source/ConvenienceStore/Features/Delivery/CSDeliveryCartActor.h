#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryCartActor.generated.h"

class UArrowComponent;
class UBoxComponent;
class UCameraComponent;
class USceneComponent;
class UStaticMeshComponent;
class ACSProductPickupActor;
class ACSDeliveryCrateActor;

// 배달 상품을 고정하지 않고 물리 상태로 실어 나르는 전용 수레입니다.
UCLASS()
class CONVENIENCESTORE_API ACSDeliveryCartActor : public AActor
{
	GENERATED_BODY()

public:
	ACSDeliveryCartActor();
	virtual void Tick(float DeltaSeconds) override;

	// 지정한 플레이어가 수레 손잡이를 잡고 운전을 시작합니다.
	bool BeginDriving(AActor* NewDriver);
	// 현재 운전자를 해제하고 수레의 기본 충돌을 복원합니다.
	void StopDriving(AActor* RequestingDriver);
	// 운전 입력으로 수레를 앞뒤 이동하고 좌우로 조향합니다.
	void Drive(float ForwardInput, float TurnInput);

	UFUNCTION(BlueprintPure, Category = "Delivery Cart|Driving")
	bool IsBeingDriven() const { return bIsBeingDriven; }

	UFUNCTION(BlueprintPure, Category = "Delivery Cart|Driving")
	USceneComponent* GetDriverAnchor() const { return DriverAnchor; }

	// 손에 든 상품을 적재함 위로 옮긴 뒤 물리를 켜서 자연스럽게 떨어뜨립니다.
	UFUNCTION(BlueprintCallable, Category = "Delivery Cart|Cargo")
	bool LoadProduct(ACSProductPickupActor* Product);

	// 배송 상자에서 상품 하나를 생성해 적재함에 물리 상태로 떨어뜨립니다.
	UFUNCTION(BlueprintCallable, Category = "Delivery Cart|Cargo")
	bool LoadOneProductFromCrate(ACSDeliveryCrateActor* DeliveryCrate);

	// 현재 적재함 영역 안에 남아 있는 상품 목록을 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Cart|Cargo")
	TArray<ACSProductPickupActor*> GetCargoProducts() const;

	// 상품 프리뷰가 표시될 적재함 내부 위치를 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Cart|Cargo")
	FTransform GetCargoPreviewTransform() const;

	// 배달 완료로 사라지는 상품을 수레의 추적 목록에서도 제거합니다.
	void RemoveCargoProduct(ACSProductPickupActor* Product);

protected:
	// 블루프린트에서 수레 전체의 위치·회전 기준으로 사용할 공통 루트입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 블루프린트에서 배달 수레 메시와 실제 물리 충돌을 설정할 컴포넌트입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CartMesh;

	// 물리 상품이 들어왔는지 감지하는 적재함 범위입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CargoArea;

	// 적재 시 상품이 나타나는 공중 위치입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> CargoDropPoint;

	// 적재 상품과 겹치지 않는 수레 하단의 바닥 탐색 기준점입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> GroundTracePoint;

	// 네 바퀴 위치의 바닥을 각각 감지해 앞뒤 기울기와 좌우 기울기를 계산합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> FrontLeftGroundTracePoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> FrontRightGroundTracePoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> RearLeftGroundTracePoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> RearRightGroundTracePoint;

	// 운전 중 캐릭터 Capsule 기준점이 붙을 위치입니다. 손잡이 위치와는 구분합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> DriverAnchor;

	// 추후 양손 IK를 연결할 왼손 손잡이 기준점입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> LeftHandHandleAnchor;

	// 추후 양손 IK를 연결할 오른손 손잡이 기준점입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> RightHandHandleAnchor;

	// 운전 중 수레 뒤쪽 위에서 플레이어와 적재함을 바라보는 고정 카메라입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> DriverCamera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DriveSpeed = 450.0f;

	// 입력 직후 최고 속도로 튀지 않도록 사용하는 가속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DriveAcceleration = 700.0f;

	// 입력을 놓거나 방향을 바꿀 때 감속하는 속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DriveDeceleration = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TurnSpeedDegreesPerSecond = 90.0f;

	// 정지 상태에서 제자리 회전을 막고, 이 속도 이상일 때만 조향을 적용합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MinimumSteeringSpeed = 15.0f;

	// 낮은 문턱·바닥 단차를 넘기 위해 수레가 시도할 최대 높이입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Collision",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxStepHeight = 30.0f;

	// 턱 상단과 운전 충돌 Box가 닿아 재차 막히지 않도록 더하는 작은 여유 높이입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Collision",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StepClearance = 5.0f;

	// SceneRoot를 유지하면서 벽·문 충돌을 검사할 운전용 가상 Box의 중심 오프셋입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Collision")
	FVector DrivingCollisionCenterOffset = FVector(0.0f, 0.0f, 55.0f);

	// 수레 모델의 바깥 크기에 맞춰 조정할 운전용 가상 Box의 반쪽 크기입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Collision",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	FVector DrivingCollisionHalfExtent = FVector(125.0f, 75.0f, 55.0f);

	// 수레 아래에서 바닥을 찾기 시작하는 높이입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GroundTraceStartHeight = 200.0f;

	// 시작점부터 아래로 바닥을 탐색할 최대 거리입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float GroundTraceDistance = 1200.0f;

	// SceneRoot가 바닥에서 얼마나 떨어져 있어야 하는지 정하는 보정값입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground")
	float GroundClearance = 0.0f;

	// 이 값보다 작은 바닥 높이 차이는 무시해 수레가 미세하게 덜컹거리지 않게 합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GroundHeightTolerance = 3.0f;

	// 오르막에서 바닥을 따라 올라가는 최대 속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GroundRiseSpeed = 800.0f;

	// 절벽 아래처럼 바닥이 낮아질 때 내려가는 최대 속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GroundFallSpeed = 1000.0f;

	// 앞·뒤 바닥 높이 차이로 만들어질 수레의 최대 앞뒤 기울기입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "0.0", ClampMax = "45.0", UIMin = "0.0", UIMax = "45.0"))
	float MaxGroundPitchDegrees = 15.0f;

	// 턱과 경사에서 수레가 즉시 꺾이지 않고 부드럽게 기울어지는 속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GroundPitchInterpSpeed = 8.0f;

	// 좌우 바퀴 높이 차이로 만들어질 수레의 최대 좌우 기울기입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "0.0", ClampMax = "45.0", UIMin = "0.0", UIMax = "45.0"))
	float MaxGroundRollDegrees = 8.0f;

	// 좌우 단차에서 적재물이 과하게 쏠리지 않도록 Roll을 Pitch보다 느리게 보간합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving|Ground",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GroundRollInterpSpeed = 6.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery Cart|Driving")
	TObjectPtr<AActor> DrivingActor;

	UPROPERTY(Transient)
	bool bIsBeingDriven = false;

	UPROPERTY(Transient)
	float CurrentDriveSpeed = 0.0f;

	UPROPERTY(Transient)
	float DriveForwardInput = 0.0f;

	UPROPERTY(Transient)
	float DriveTurnInput = 0.0f;

	UPROPERTY(Transient)
	TEnumAsByte<ECollisionResponse> PreviousPawnCollisionResponse = ECR_Block;

	// 운전 시작 전 카메라 상대 회전을 저장해, 운전을 끝낼 때 블루프린트 설정값으로 복원합니다.
	UPROPERTY(Transient)
	FRotator DefaultDriverCameraRelativeRotation = FRotator::ZeroRotator;

	// 적재함에 실제로 들어와 있는 상품만 추적합니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery Cart|Cargo")
	TArray<TObjectPtr<ACSProductPickupActor>> CargoProducts;

	UFUNCTION()
	void HandleCargoBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleCargoEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	// SceneRoot는 충돌이 없으므로, 수레 크기 기준의 별도 Box Sweep으로 수평 이동을 검사합니다.
	void MoveWithDrivingCollision(const FVector& DesiredDelta);
	// 운전 중 수레 아래 바닥을 추적해 경사와 절벽 높이를 따라가게 합니다.
	void UpdateDrivingGroundHeight(float DeltaSeconds, bool bForceSnapToGround = false);
	// 가속·감속을 적용한 실제 수레 이동을 매 프레임 수행합니다.
	void UpdateDrivingMovement(float DeltaSeconds);
	// 수레 본체는 Roll을 유지하되, 운전 카메라 화면은 수평으로 유지합니다.
	void UpdateDrivingCameraRotation();
};
