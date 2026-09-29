#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSSaveParticipant.generated.h"

class UCSSaveGame;

UINTERFACE(MinimalAPI)
class UCSSaveParticipant : public UInterface
{
	GENERATED_BODY()
};

// 세이브·로드에 참여하는 시스템이 구현하는 인터페이스
// 각 시스템이 자기 상태를 직접 캡처·복원해 외부 Getter·Setter 추가를 방지
class CONVENIENCESTORE_API ICSSaveParticipant
{
	GENERATED_BODY()

public:
	// 복원 우선순위를 반환하고 값이 작을수록 먼저 복원
	// GameState(0) → 서브시스템(5) → 월드 액터(10) 순서로 자금·일차를 먼저 확정
	// 단계 사이에 새 단계를 추가할 때 기존 값을 변경하지 않도록 5 간격으로 설정
	virtual int32 GetSaveRestorePriority() const { return 5; }

	// 현재 상태를 세이브 데이터의 자기 블록에 기록
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const = 0;

	// 세이브 데이터의 자기 블록으로 상태를 복원
	virtual void RestoreSaveData(const UCSSaveGame& InSave) = 0;
};
