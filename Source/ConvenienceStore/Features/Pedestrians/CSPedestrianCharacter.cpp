#include "Features/Pedestrians/CSPedestrianCharacter.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/CSCustomerVisualDataAsset.h"
#include "GameFramework/CharacterMovementComponent.h"

// 행인의 이동, 충돌, 애니메이션 기본 설정을 초기화
ACSPedestrianCharacter::ACSPedestrianCharacter()
{
	// 이동 입력은 스포너가 처리하므로 캐릭터 자체 Tick은 사용하지 않음
	PrimaryActorTick.bCanEverTick = false;

	// 행인별 AIController를 생성하지 않고 이동 방향에 따라 회전
	AIControllerClass = nullptr;
	AutoPossessAI = EAutoPossessAI::Disabled;
	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();

	// 컨트롤러가 없어도 이동 입력을 처리하도록 설정
	Movement->bRunPhysicsWithNoController = true;

	// 내비게이션 데이터를 사용하는 지면 이동 모드로 설정
	Movement->DefaultLandMovementMode = MOVE_NavWalking;

	// 이동 방향으로 회전하고 기본 걷기 속도를 설정
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 360.0f, 0.0f);
	Movement->MaxWalkSpeed = 300.0f;

	// 행인 간 RVO 회피는 사용하지 않음
	Movement->bUseRVOAvoidance = false;

	UCapsuleComponent* Capsule = GetCapsuleComponent();

	// 플레이어, 손님, 직원 등 Pawn과의 충돌 응답을 무시
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	// 실내 판정 박스 등에 오버랩 이벤트를 발생시키지 않음
	Capsule->SetGenerateOverlapEvents(false);

	// 행인 캡슐이 내비게이션 생성에 영향을 주지 않도록 설정
	Capsule->SetCanEverAffectNavigation(false);

	USkeletalMeshComponent* MeshComp = GetMesh();

	// 메시의 충돌과 오버랩은 사용하지 않음
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetGenerateOverlapEvents(false);

	// 렌더링되는 동안에만 애니메이션 포즈를 갱신
	MeshComp->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	// 애니메이션 갱신 빈도 최적화를 활성화
	MeshComp->bEnableUpdateRateOptimizations = true;
}

// 행인의 배치와 외형을 적용하고 이동 가능한 상태로 전환
void ACSPedestrianCharacter::ActivatePedestrian(
	const UCSCustomerVisualDataAsset* Visual,
	const FTransform& SpawnTransform,
	float WalkSpeed,
	float AnimStartRatio)
{
	// 종족 크기를 먼저 적용한 뒤 위치와 회전을 설정
	SetActorScale3D(SpawnTransform.GetScale3D());
	SetActorLocationAndRotation(
		SpawnTransform.GetLocation(),
		SpawnTransform.GetRotation(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	// 풀에서 멈췄던 애니메이션 갱신을 다시 활성화
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->bPauseAnims = false;
		MeshComp->SetComponentTickEnabled(true);
	}

	ApplyVisual(Visual, AnimStartRatio);

	// 이전 이동 속도를 비우고 새 걷기 속도와 이동 모드를 적용
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->Activate(true);
	Movement->StopMovementImmediately();
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->SetMovementMode(MOVE_NavWalking);

	// 재사용 전 남아 있던 이동 입력을 제거
	ConsumeMovementInputVector();

	// 충돌과 표시를 활성화하고 현재 상태를 기록
	SetActorEnableCollision(true);
	SetActorHiddenInGame(false);
	bPedestrianActive = true;
}

// 행인을 숨기고 이동과 애니메이션 갱신을 중단
void ACSPedestrianCharacter::DeactivatePedestrian()
{
	bPedestrianActive = false;

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	// 이동을 즉시 멈추고 남은 입력을 제거
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	ConsumeMovementInputVector();

	// 숨겨진 동안 메시 Tick과 애니메이션 재생을 중단
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->bPauseAnims = true;
		MeshComp->SetComponentTickEnabled(false);
	}
}

// 종족 외형 데이터에 따라 메시와 애니메이션을 적용
void ACSPedestrianCharacter::ApplyVisual(
	const UCSCustomerVisualDataAsset* Visual,
	float AnimStartRatio)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!IsValid(MeshComp) || !IsValid(Visual))
	{
		return;
	}

	// 지정된 메시가 현재 메시와 다를 때만 교체
	if (Visual->CharacterMesh != nullptr &&
		MeshComp->GetSkeletalMeshAsset() != Visual->CharacterMesh)
	{
		MeshComp->SetSkeletalMesh(Visual->CharacterMesh);
	}

	// 애니메이션 블루프린트가 지정되어 있으면 우선 적용
	// 블루프린트 내부 로직은 행인 캐릭터에서도 동작하도록 구성 필요
	if (Visual->AnimBlueprintClass != nullptr)
	{
		MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);

		if (MeshComp->GetAnimClass() != Visual->AnimBlueprintClass)
		{
			MeshComp->SetAnimInstanceClass(Visual->AnimBlueprintClass);
		}

		return;
	}

	// 걷기 애니메이션을 우선 사용하고, 없으면 대기 애니메이션을 사용
	UAnimSequence* LoopAnimation = Visual->FallbackIdleAnimation.Get();
	if (Visual->FallbackWalkAnimation != nullptr)
	{
		LoopAnimation = Visual->FallbackWalkAnimation.Get();
	}

	if (LoopAnimation == nullptr)
	{
		return;
	}

	// 단일 애니메이션을 반복 재생하고 시작 위치를 분산
	MeshComp->PlayAnimation(LoopAnimation, true);
	MeshComp->SetPosition(
		LoopAnimation->GetPlayLength() *
			FMath::Clamp(AnimStartRatio, 0.0f, 1.0f),
		false);
}
