#pragma once

#include "CoreMinimal.h"
#include "CSAudioTypes.generated.h"

// 게임플레이 코드가 실제 사운드 에셋 대신 요청할 공용 SFX 식별값입니다.
UENUM(BlueprintType)
enum class ECSAudioEvent : uint8
{
	BarcodeScan UMETA(DisplayName = "Barcode Scan"),
	CheckoutComplete UMETA(DisplayName = "Checkout Complete"),
	MagicAttack UMETA(DisplayName = "Magic Attack"),
	WandPlace UMETA(DisplayName = "Wand Place"),
	ProductStock UMETA(DisplayName = "Product Stock"),
	FacilityPlace UMETA(DisplayName = "Facility Place"),
	TrashDispose UMETA(DisplayName = "Trash Dispose"),
	DeliveryCrateDisappear UMETA(DisplayName = "Delivery Crate Disappear"),
	CrystalBallOpen UMETA(DisplayName = "Crystal Ball Open"),
	LedgerOpen UMETA(DisplayName = "Ledger Open"),
	LedgerClose UMETA(DisplayName = "Ledger Close"),
	EndingItemSubmit UMETA(DisplayName = "Ending Item Submit"),
	MagicHitCustomer,
	StoreOpen,
	GachaWellBlessing,
	PlayerLevelUp,
	MagicCircleLevelUp,
	OrderSuccess,
	EmployeeHireSuccess,
	OrderFail,
	CheckoutWaitingBell,
	// 21은 제거된 Pick SFX 값입니다. 기존 BP/DataAsset의 이후 이벤트 ID를 유지합니다.
	JournalEntryAdded = 22,
	StoreClose,
	GachaCapsuleBreak,
	GachaCapsuleSpawn,
	EndOfDaySleep,
	DeliveryOrder,
	MenuButton,
	GameStart,
	DeliveryDispatch
};
