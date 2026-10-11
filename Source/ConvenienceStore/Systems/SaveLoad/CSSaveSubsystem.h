#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/CSSaveTypes.h"
#include "CSSaveSubsystem.generated.h"

class UCSSaveGame;
class UCSSaveMetaGame;
class UWorld;

// 슬롯 저장이 성공했을 때 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnSaveCompleted,FName,SlotName);

// 슬롯 읽기나 버전 검사에 실패했을 때 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnLoadFailed,FName,SlotName);

// 세이브·로드 전체 흐름을 관리하는 서브시스템
// 로드는 레벨 재오픈을 동반하므로 레벨 전환 후에도 유지되는 GameInstanceSubsystem으로 설정
UCLASS()
class CONVENIENCESTORE_API UCSSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 자동 세이브 슬롯 이름을 반환
	static FName GetAutoSaveSlotName() { return TEXT("CS_Auto"); }

	// 0부터 시작하는 번호로 수동 세이브 슬롯 이름을 반환
	static FName MakeManualSlotName(int32 Index)
	{
		return FName(*FString::Printf(TEXT("CS_Slot%d"), FMath::Max(0, Index)));
	}

	// 수동 세이브 슬롯 개수를 반환
	static int32 GetManualSlotCount() { return 3; }

	// 타이틀 슬롯 카드 순서에 대응하는 세이브 파일 슬롯 이름(CS_Slot0~2)을 반환
	UFUNCTION(BlueprintPure, Category = "Save")
	static FName GetFileSlotName(int32 Index) { return MakeManualSlotName(Index); }

	// 현재 플레이 중인 세이브 파일 슬롯을 반환하고 없으면 None을 반환
	UFUNCTION(BlueprintPure, Category = "Save")
	FName GetActiveSlotName() const { return ActiveSlotName; }

	// 타이틀에서 세이브 파일 슬롯을 선택해 진입했는지 확인
	UFUNCTION(BlueprintPure, Category = "Save")
	bool HasActiveSlot() const { return !ActiveSlotName.IsNone(); }

	// 지정 슬롯을 현재 슬롯으로 설정하고 본 게임 레벨을 새 게임으로 열어 성공 여부를 반환
	// 슬롯 파일은 첫 영업 종료 자동 저장 시점에 생성
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool StartNewGame(FName SlotName, TSoftObjectPtr<UWorld> GameLevel);

	// 참가자 상태를 모아 지정 슬롯에 저장하고 성공 여부를 반환
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool SaveToSlot(FName SlotName);

	// 현재 슬롯이 있으면 해당 슬롯에, 없으면 자동 세이브 슬롯에 저장하고 성공 여부를 반환
	// 영업 종료 후 손님 상품 떨어뜨림이 끝난 시점에 ACSStoreGameMode가 호출
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool SaveToAutoSlot();

	// 세이브를 읽어 보관한 뒤 헤더에 기록된 레벨을 열고 성공 여부를 반환
	// 실제 복원은 새 월드에서 ApplyPendingLoad()가 수행
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool LoadFromSlot(FName SlotName);

	// 보관 중인 세이브를 새 월드에 적용하고 적용 여부를 반환
	bool ApplyPendingLoad(UWorld& InWorld);

	// 레벨 재오픈 후 적용을 기다리는 세이브가 있는지 확인
	UFUNCTION(BlueprintPure, Category = "Save")
	bool HasPendingLoad() const { return PendingLoadData != nullptr; }

	// 메타 슬롯의 헤더 목록을 최근 저장 순으로 반환
	UFUNCTION(BlueprintPure, Category = "Save")
	TArray<FCSSaveHeader> GetAllSlotHeaders() const;

	// 슬롯 이름으로 헤더를 조회하고 없으면 false를 반환
	UFUNCTION(BlueprintPure, Category = "Save")
	bool GetSlotHeader(FName SlotName, FCSSaveHeader& OutHeader) const;

	// 슬롯 파일이 존재하는지 확인
	UFUNCTION(BlueprintPure, Category = "Save")
	bool DoesSlotExist(FName SlotName) const;

	// 슬롯과 백업 슬롯, 메타 헤더를 함께 제거하고 본 슬롯 삭제 여부를 반환
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool DeleteSlot(FName SlotName);

	// 이어받은 플레이 시간과 현재 월드 경과 시간의 합을 반환
	UFUNCTION(BlueprintPure, Category = "Save")
	float GetTotalPlayTimeSeconds() const;

	// 슬롯 저장 성공 알림
	UPROPERTY(BlueprintAssignable, Category = "Save")
	FCSOnSaveCompleted OnSaveCompleted;

	// 슬롯 읽기 실패 알림
	UPROPERTY(BlueprintAssignable, Category = "Save")
	FCSOnLoadFailed OnLoadFailed;

private:
	// 월드와 게임 인스턴스에서 참가자를 수집하고 복원 우선순위로 정렬
	// 참가 시스템이 추가되어도 이 함수는 변경하지 않음
	void CollectParticipants(UWorld& InWorld, TArray<UObject*>& OutParticipants) const;

	// 참가자 캡처가 끝난 세이브에 헤더 정보를 설정
	void FillHeader(UWorld& InWorld, UCSSaveGame& InOutSave, FName SlotName) const;

	// 쓰기 중 크래시에 대비해 기존 슬롯을 백업 슬롯으로 복사
	void BackupSlot(FName SlotName) const;

	// 본 슬롯을 읽고 실패하면 백업 슬롯을 조회
	UCSSaveGame* LoadSaveGameWithFallback(FName SlotName) const;

	// 메타 슬롯의 헤더를 교체하거나 추가
	void UpdateMetaHeader(const FCSSaveHeader& InHeader) const;

	// 메타 슬롯에서 헤더를 제거
	void RemoveMetaHeader(FName SlotName) const;

	// 메타 슬롯을 읽고 없으면 새 메타 세이브를 반환
	UCSSaveMetaGame* LoadOrCreateMeta() const;

	static FString ToSlotString(FName SlotName) { return SlotName.ToString(); }

	// 확장자 대신 접미사로 백업 슬롯 이름을 구분
	static FString MakeBackupSlotString(FName SlotName)
	{
		return SlotName.ToString() + TEXT("_Backup");
	}

	static FName GetMetaSlotName() { return TEXT("CS_Meta"); }

	// 레벨 재오픈 후 적용할 세이브 데이터
	// 레벨 전환 중 GC로 제거되지 않도록 UPROPERTY로 유지
	UPROPERTY(Transient)
	TObjectPtr<UCSSaveGame> PendingLoadData;

	// 불러온 세이브에서 이어받은 플레이 시간(초)
	float BasePlayTimeSeconds = 0.0f;

	// 현재 플레이 중인 세이브 파일 슬롯
	// 타이틀을 거치지 않아 None이면 자동 저장을 자동 세이브 슬롯(CS_Auto)에 기록
	UPROPERTY(Transient)
	FName ActiveSlotName;
};
