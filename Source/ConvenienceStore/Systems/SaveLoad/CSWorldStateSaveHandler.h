#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "CSWorldStateSaveHandler.generated.h"

class ACSDeliveryCrateActor;
class ACSFreshnessSafeZoneActor;
class ACSPlayerCharacter;
class ACSTrashActor;
class UCSSaveGame;
class UDataTable;
struct FCSCrateRecord;
struct FCSTrashRecord;
struct FCSWorldSaveRecord;

// 월드 액터(Tier B) 상태의 캡처·복원을 담당하는 서브시스템
// 재고 서브시스템은 자체 상태 없이 월드 액터를 집계하므로 재고 저장은 상품·상자·진열대 액터 저장으로 처리
UCLASS()
class CONVENIENCESTORE_API UCSWorldStateSaveHandler : public UWorldSubsystem,
	public ICSSaveParticipant
{
	GENERATED_BODY()

public:
	// ICSSaveParticipant
	// 자금·일차와 서브시스템 상태가 확정된 뒤에 액터를 생성하도록 가장 늦게 복원
	virtual int32 GetSaveRestorePriority() const override { return 10; }
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;

	// 상자 레코드로 배송 상자를 생성하고 저장 시점 상태를 적용해 반환
	// 월드 복원과 퀵바 복원이 같은 생성 순서를 사용하도록 공개
	static ACSDeliveryCrateActor* SpawnCrateFromRecord(UWorld& InWorld,const FCSCrateRecord& InRecord,const FTransform& InSpawnTransform,const UDataTable* FallbackProductTable);

	// 쓰레기 레코드로 쓰레기를 생성하고 저장 시점의 이름·메시·물리 상태를 적용해 반환
	// 월드 복원과 퀵바 복원이 같은 생성 순서를 사용하도록 공개
	static ACSTrashActor* SpawnTrashFromRecord(UWorld& InWorld,const FCSTrashRecord& InRecord,const FTransform& InSpawnTransform);

private:
	void CaptureShelves(FCSWorldSaveRecord& OutRecord) const;
	void CaptureLooseProducts(FCSWorldSaveRecord& OutRecord) const;
	void CaptureCrates(FCSWorldSaveRecord& OutRecord) const;
	void CaptureFacilities(FCSWorldSaveRecord& OutRecord) const;
	void CaptureTrash(FCSWorldSaveRecord& OutRecord) const;
	void CaptureArtifactPickups(FCSWorldSaveRecord& OutRecord) const;
	void CaptureQuickbar(FCSWorldSaveRecord& OutRecord) const;

	// 자금 복원 후 엔딩 조건이 다시 충족되므로 엔딩 진행 상태를 함께 기록
	void CaptureEnding(UCSSaveGame& OutSave) const;
	void RestoreEnding(const UCSSaveGame& InSave);

	// 슬롯 점유 포인터가 어긋나지 않도록 부분 갱신 대신 월드의 모든 상품·상자를 제거
	// 레벨 배치 진열대가 BeginPlay에서 채운 초기 상품도 함께 제거
	void DestroyAllProductsAndCrates();

	// 레벨이 다시 열리며 생성된 배치 쓰레기를 포함해 월드의 모든 쓰레기를 제거
	void DestroyAllTrash();

	// 획득한 레벨 배치 픽업이 다시 나타나지 않도록 배치분을 포함해 월드의 모든 아티팩트 픽업을 제거
	void DestroyAllArtifactPickups();

	void RestoreShelves(const FCSWorldSaveRecord& InRecord, const UDataTable* FallbackProductTable);
	void RestoreLooseProducts(const FCSWorldSaveRecord& InRecord, const UDataTable* FallbackProductTable);
	void RestoreCrates(const FCSWorldSaveRecord& InRecord, const UDataTable* FallbackProductTable);
	void RestoreFacilities(const FCSWorldSaveRecord& InRecord);
	void RestoreTrash(const FCSWorldSaveRecord& InRecord);
	void RestoreArtifactPickups(const FCSWorldSaveRecord& InRecord);
	void RestoreQuickbar(const FCSWorldSaveRecord& InRecord, const UDataTable* FallbackProductTable);

	// 월드의 플레이어 캐릭터를 조회
	ACSPlayerCharacter* FindPlayerCharacter() const;

	// 창고 액터를 이름 기준으로 수집
	void CollectStoragesByName(TMap<FName, ACSFreshnessSafeZoneActor*>& OutStorages) const;

	// 테이블 경로 해석 실패 시 사용할 상품 테이블을 조회
	// 월드 상품과 진열대 단 설정에서 찾으므로 상품 제거 전에 호출
	const UDataTable* FindFallbackProductTable() const;
};
