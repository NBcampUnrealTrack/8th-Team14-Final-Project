#pragma once

#include "CoreMinimal.h"
#include "Features/Tools/CSUsableToolActor.h"
#include "CSMagicWandActor.generated.h"

class ACSMagicProjectileActor;
class USceneComponent;
class UCSMagicWandCrosshairWidget;

// 주 사용 입력으로 마법 투사체를 발사하는 지팡이 도구입니다.
UCLASS()
class CONVENIENCESTORE_API ACSMagicWandActor : public ACSUsableToolActor
{
	GENERATED_BODY()

public:
	ACSMagicWandActor();

	virtual bool UsePrimary_Implementation(AActor* User) override;
	virtual void OnEquipped(ACSPlayerCharacter* NewOwner) override;
	virtual void OnUnequipped() override;
	virtual void PrepareForPickupPhysics() override;
	// Drop 위치 계산은 해제 후 복원될 크기를 사용하되, 아직 장착된 Scale은 유지합니다.
	FBox GetUnheldBounds() const;

	// 최초 집기 전 현재 AttachParent를 원래 거치대로 기억하며, 유효한 기존 정보는 덮어쓰지 않습니다.
	bool RememberHomeDockFromCurrentAttachment();
	USceneComponent* GetHomeDockComponent() const;
	AActor* GetHomeDockOwner() const;

	// ----- 세이브 / 로드 -----

	// 거치대에 꽂혀 있는지 확인
	// 기억한 거치대가 없으면 플레이어가 아닌 액터에 부착된 상태를 최초 거치대로 판단
	bool IsDockedAtHome() const;

	// 레벨 거치대 소속 지팡이인지 확인
	// 거치대에 꽂혀 있거나 꺼낸 뒤에도 거치대를 기억하는 경우 true를 반환
	bool BelongsToHomeDock() const;

	// 거치대를 소유한 시설의 세이브 식별자를 조회하고 없으면 유효하지 않은 값을 반환
	FGuid GetHomeDockOwnerSaveId() const;

	// 장착 배율을 제외한 월드 배율을 반환
	FVector GetUnheldScale() const;

	// 거치대에 꽂힌 같은 클래스의 지팡이를 조회하고 없으면 nullptr을 반환
	// 소유 시설 식별자가 유효하면 해당 시설의 거치대만 검색
	static ACSMagicWandActor* FindDockedWand(UWorld& InWorld, UClass* WandClass, const FGuid& HomeDockOwnerSaveId);

protected:
	// 기본 지팡이는 0(직원 버프 없음), 고급 지팡이 BP는 30초로 설정합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic|Employee", meta=(ClampMin="0"))
	float EmployeeBoostDuration = 0.0f;

	// 블루프린트에서 지팡이 끝에 배치하는 실제 투사체 발사 위치입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Muzzle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic")
	TSubclassOf<ACSMagicProjectileActor> MagicProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PrimaryUseCooldown = 0.35f;

	// 장착 중에만 화면에 띄우는 조준점 WBP
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic|UI")
	TSubclassOf<UCSMagicWandCrosshairWidget> CrosshairWidgetClass;

private:
	void RestoreWorldScale();
	void ShowCrosshairWidget(ACSPlayerCharacter* NewOwner);
	void HideCrosshairWidget();

	UPROPERTY(Transient)
	TObjectPtr<UCSMagicWandCrosshairWidget> ActiveCrosshairWidget;

	// 장착 중에만 유지합니다. 복원 후 비워 다음 획득에서도 원래 월드 크기를 기록합니다.
	TOptional<FVector> CachedWorldScale;

	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> HomeDockComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> HomeDockOwner;

	float LastPrimaryUseTime = -FLT_MAX;
};
