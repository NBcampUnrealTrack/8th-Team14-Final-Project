#include "Features/Checkout/CSCheckoutCounterActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Items/CSProductPickupActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Data/CSProductTypes.h"
#include "UI/Checkout/CSCheckoutWidget.h"

ACSCheckoutCounterActor::ACSCheckoutCounterActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 계산대의 모든 구성 요소가 공유할 루트 컴포넌트를 생성합니다.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 계산대 외형은 블루프린트에서 자유롭게 지정할 수 있도록 빈 메시 컴포넌트로 둡니다.
	CounterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CounterMesh"));
	CounterMesh->SetupAttachment(SceneRoot);

	// 화살표의 시작점은 계산 중인 첫 번째 손님의 위치이며, 화살표 정면은 대기열 방향입니다.
	QueueStartPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("QueueStartPoint"));
	QueueStartPoint->SetupAttachment(SceneRoot);
	QueueStartPoint->SetHiddenInGame(true);

	// 화살표 위치를 계산대 상판에 맞추면 상품 표시 위치와 배치 방향을 에디터에서 조정할 수 있습니다.
	CheckoutDisplayOrigin = CreateDefaultSubobject<UArrowComponent>(TEXT("CheckoutDisplayOrigin"));
	CheckoutDisplayOrigin->SetupAttachment(SceneRoot);
	CheckoutDisplayOrigin->SetHiddenInGame(true);
}

void ACSCheckoutCounterActor::BeginPlay()
{
	Super::BeginPlay();

	// 월드에서 손님 흐름 Subsystem을 찾아 현재 계산대를 대기열 관리 대상으로 등록합니다.
	if (UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>())
	{
		CustomerSubsystem->RegisterCounter(this);
	}

	// 영업 단계가 바뀌었을 때 진행 중인 계산을 정리할 수 있도록 GameState 이벤트를 구독합니다.
	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnStorePhaseChanged.AddDynamic(
			this, &ACSCheckoutCounterActor::HandleStorePhaseChanged);
	}
}

void ACSCheckoutCounterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		// 계산대가 제거될 때 손님 Subsystem이 사라진 계산대를 계속 참조하지 않게 합니다.
		if (UCSCustomerRuntimeSubsystem* CustomerSubsystem =
			World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			CustomerSubsystem->UnregisterCounter(this);
		}
	}

	if (IsValid(StoreGameState))
	{
		StoreGameState->OnStorePhaseChanged.RemoveDynamic(
			this, &ACSCheckoutCounterActor::HandleStorePhaseChanged);
	}

	// 레벨 종료나 계산대 제거 시 UI와 입력 상태가 남지 않도록 정리합니다.
	CloseCheckoutWidget();

	Super::EndPlay(EndPlayReason);
}

FVector ACSCheckoutCounterActor::GetQueueSlotLocation(const int32 Index) const
{
	// 음수 순서 또는 누락된 대기열 시작점은 유효하지 않으므로 영벡터를 반환합니다.
	if (Index < 0 || !IsValid(QueueStartPoint))
	{
		return FVector::ZeroVector;
	}

	// 0번 손님은 QueueStartPoint 위치에 서고, 이후 손님은 화살표 정면 방향으로 일정 간격만큼 배치합니다.
	return QueueStartPoint->GetComponentLocation()
		+ QueueStartPoint->GetForwardVector() * QueueSlotSpacing * static_cast<float>(Index);
}

bool ACSCheckoutCounterActor::CanProcessCheckout() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	// 매장 단계 규칙은 GameState가 관리하므로, 계산대는 결제 허용 여부만 조회합니다.
	const ACSStoreGameState* CurrentStoreGameState = World->GetGameState<ACSStoreGameState>();
	return IsValid(CurrentStoreGameState) && CurrentStoreGameState->bCanCheckout;
}

void ACSCheckoutCounterActor::Interact_Implementation(AActor* Interactor)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (!IsValid(CustomerSubsystem))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 손님 흐름 Subsystem을 찾을 수 없어 계산을 완료할 수 없습니다."));
		return;
	}

	// 준비·정산 등 계산이 허용되지 않은 단계에서는 손님 대기열을 변경하지 않습니다.
	if (!CanProcessCheckout())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CheckoutCounter] 현재 매장 단계에서는 계산을 진행할 수 없습니다."));
		return;
	}

	// 계산할 손님이 없을 때는 대기열과 손님 상태를 변경하지 않습니다.
	if (!IsValid(CustomerSubsystem->GetFrontCustomer()))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CheckoutCounter] 계산 대기 중인 손님이 없습니다."));
		return;
	}

	OpenCheckoutWidget(Interactor);
}

FText ACSCheckoutCounterActor::GetInteractionText_Implementation() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return FText::GetEmpty();
	}

	// 현재 단계에서 결제가 허용되지 않으면, 손님 유무와 관계없이 제한 안내를 표시합니다.
	if (!CanProcessCheckout())
	{
		return NSLOCTEXT("CheckoutCounter", "CheckoutUnavailable", "현재는 계산할 수 없습니다.");
	}

	const UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (!IsValid(CustomerSubsystem) || !IsValid(CustomerSubsystem->GetFrontCustomer()))
	{
		return NSLOCTEXT("CheckoutCounter", "NoCustomer", "계산 대기 중인 손님이 없습니다.");
	}

	return NSLOCTEXT("CheckoutCounter", "StartCheckout", "계산 시작");
}

bool ACSCheckoutCounterActor::BuildFrontCustomerOrder(
	TArray<FCSCheckoutLineItem>& OutLineItems,
	int32& OutTotalSalePrice) const
{
	OutLineItems.Reset();
	OutTotalSalePrice = 0;

	if (!IsValid(ProductMasterDataTable))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 상품 마스터 데이터 테이블이 설정되지 않아 주문을 생성할 수 없습니다."));
		return false;
	}

	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	ACSCustomerCharacter* FrontCustomer =
		IsValid(CustomerSubsystem) ? CustomerSubsystem->GetFrontCustomer() : nullptr;
	if (!IsValid(FrontCustomer))
	{
		return false;
	}

	const UCSCustomerStateComponent* CustomerState = FrontCustomer->GetCustomerState();
	if (!IsValid(CustomerState) || !CustomerState->HasSelectedItems()) //[무송]이 부분 CustomerState->SelectItemIds.IsEmpty()에서 변경해서 State에서 함수를 만들어서 사용했습니다.
	{
		return false;
	}


	TArray<FName> OrderedProductIds;
	TMap<FName, int32> ProductQuantities;
	for (const FName ProductId : CustomerState->GetSelectProductIds()) //[무송] (const FName ProductId : CustomerState->SelectItemIds)에서 변경
	{
		// 비어 있는 상품 ID는 데이터 오류이므로 결제를 진행하지 않습니다.
		if (ProductId.IsNone())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CheckoutCounter] 손님 구매 목록에 비어 있는 상품 ID가 포함되어 있습니다."));
			OutLineItems.Reset();
			return false;
		}

		int32& Quantity = ProductQuantities.FindOrAdd(ProductId);
		if (Quantity == 0)
		{
			// 손님이 상품을 집은 순서를 유지하여 계산대 UI의 표시 순서를 안정적으로 관리합니다.
			OrderedProductIds.Add(ProductId);
		}

		++Quantity;
	}

	for (const FName ProductId : OrderedProductIds)
	{
		// 상품 ID는 상품 마스터 데이터 테이블의 RowName과 일치해야 합니다.
		const FCSProductDataRow* ProductData = ProductMasterDataTable->FindRow<FCSProductDataRow>(
			ProductId,
			TEXT("BuildFrontCustomerOrder"));
		if (ProductData == nullptr)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CheckoutCounter] 상품 마스터에서 RowName을 찾을 수 없습니다: %s"),
				*ProductId.ToString());
			OutLineItems.Reset();
			OutTotalSalePrice = 0;
			return false;
		}

		const int32 Quantity = ProductQuantities.FindChecked(ProductId);
		FCSCheckoutLineItem& LineItem = OutLineItems.AddDefaulted_GetRef();
		LineItem.ProductId = ProductId;
		LineItem.ItemName = ProductData->DisplayName.IsEmpty()
			? FText::FromName(ProductId)
			: ProductData->DisplayName;
		LineItem.Quantity = Quantity;
		LineItem.UnitSalePrice = ProductData->RecommendedSalePrice;
		LineItem.UnitPurchasePrice = ProductData->PurchasePrice;
		LineItem.LineTotalSalePrice = LineItem.UnitSalePrice * Quantity;

		OutTotalSalePrice += LineItem.LineTotalSalePrice;
	}

	return !OutLineItems.IsEmpty();
}

bool ACSCheckoutCounterActor::OpenCheckoutWidget(AActor* Interactor)
{
	// 이미 열린 계산 UI가 있으면 같은 주문을 중복으로 열지 않습니다.
	if (IsValid(ActiveCheckoutWidget))
	{
		return true;
	}

	if (CheckoutWidgetClass == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 계산 UI 클래스가 설정되지 않아 계산을 시작할 수 없습니다."));
		return false;
	}

	TArray<FCSCheckoutLineItem> OrderItems;
	int32 TotalSalePrice = 0;
	if (!BuildFrontCustomerOrder(OrderItems, TotalSalePrice))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 손님 주문을 생성할 수 없어 계산을 시작하지 않습니다."));
		return false;
	}

	APawn* InteractingPawn = Cast<APawn>(Interactor);
	APlayerController* PlayerController =
		IsValid(InteractingPawn) ? Cast<APlayerController>(InteractingPawn->GetController()) : nullptr;
	if (!IsValid(PlayerController))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 계산 UI를 표시할 플레이어 컨트롤러를 찾을 수 없습니다."));
		return false;
	}

	// 계산대가 만든 주문 데이터를 위젯에 전달한 뒤 화면에 표시합니다.
	ActiveCheckoutWidget = CreateWidget<UCSCheckoutWidget>(PlayerController, CheckoutWidgetClass);
	if (!IsValid(ActiveCheckoutWidget))
	{
		return false;
	}

	ActiveCheckoutWidget->SetCheckoutOrder(OrderItems, TotalSalePrice);
	ActiveCheckoutWidget->OnPaymentRequested.AddDynamic(
		this, &ACSCheckoutCounterActor::HandleCheckoutPaymentRequested);
	ActiveCheckoutWidget->OnCheckoutCancelled.AddDynamic(
		this, &ACSCheckoutCounterActor::HandleCheckoutCancelled);
	ActiveCheckoutWidget->OnItemScanned.AddDynamic(
		this, &ACSCheckoutCounterActor::HandleCheckoutItemScanned);
	ActiveCheckoutWidget->AddToViewport();

	// UI가 열린 주문과 같은 손님의 상품을 계산대 위에 표시합니다.
	SpawnCheckoutDisplayProducts();

	// 스캔 버튼을 조작할 수 있도록 플레이어 입력을 UI 중심으로 전환합니다.
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ActiveCheckoutWidget->TakeWidget());
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);

	return true;
}

void ACSCheckoutCounterActor::HandleCheckoutPaymentRequested()
{
	// 다른 위젯 또는 스캔이 끝나지 않은 위젯의 요청은 처리하지 않습니다.
	if (!IsValid(ActiveCheckoutWidget) || !ActiveCheckoutWidget->IsScanComplete())
	{
		return;
	}

	if (!CanProcessCheckout())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 결제 도중 매장 단계가 변경되어 계산을 완료할 수 없습니다."));
		return;
	}

	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	if (!IsValid(CustomerSubsystem) || !IsValid(CustomerSubsystem->GetFrontCustomer()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 결제를 완료할 대기 손님을 찾을 수 없습니다."));
		CloseCheckoutWidget();
		return;
	}

	ACSStoreGameState* CurrentStoreGameState = World->GetGameState<ACSStoreGameState>();
	if (!IsValid(CurrentStoreGameState) ||
		!CurrentStoreGameState->RecordCheckoutTransaction(ActiveCheckoutWidget->OrderItems))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CheckoutCounter] 거래 기록에 실패하여 손님 결제를 완료하지 않습니다."));
		return;
	}

	// 거래를 먼저 기록한 뒤 손님 Subsystem에 결제 완료를 알려 대기열과 손님 퇴장 흐름을 진행합니다.
	CustomerSubsystem->CompleteFrontCheckout();
	CloseCheckoutWidget();
}

void ACSCheckoutCounterActor::HandleCheckoutCancelled()
{
	// 취소 시에는 판매 기록과 손님 대기열을 변경하지 않고 계산 UI만 닫습니다.
	CloseCheckoutWidget();
}

void ACSCheckoutCounterActor::HandleCheckoutItemScanned(FName ProductId)
{
	if (ProductId.IsNone())
	{
		return;
	}

	// 주문은 같은 상품을 묶어 스캔하므로, 아직 남아 있는 동일 상품 중 첫 번째 표시 액터를 제거합니다.
	for (int32 ProductIndex = 0; ProductIndex < CheckoutDisplayProductIds.Num(); ++ProductIndex)
	{
		if (CheckoutDisplayProductIds[ProductIndex] != ProductId)
		{
			continue;
		}

		if (CheckoutDisplayProducts.IsValidIndex(ProductIndex) &&
			IsValid(CheckoutDisplayProducts[ProductIndex]))
		{
			CheckoutDisplayProducts[ProductIndex]->Destroy();
		}

		CheckoutDisplayProducts.RemoveAt(ProductIndex);
		CheckoutDisplayProductIds.RemoveAt(ProductIndex);
		return;
	}
}

void ACSCheckoutCounterActor::HandleStorePhaseChanged(ECStorePhase NewPhase)
{
	(void)NewPhase;

	// 정산 등 계산 불가 단계로 전환되면 미완료 거래를 기록하지 않고 UI만 닫습니다.
	if (!CanProcessCheckout())
	{
		CloseCheckoutWidget();
	}
}

void ACSCheckoutCounterActor::CloseCheckoutWidget()
{
	// UI가 어떤 이유로 닫히더라도 표시용 상품이 월드에 남지 않게 정리합니다.
	ClearCheckoutDisplayProducts();

	if (!IsValid(ActiveCheckoutWidget))
	{
		return;
	}

	APlayerController* PlayerController = ActiveCheckoutWidget->GetOwningPlayer();
	ActiveCheckoutWidget->RemoveFromParent();
	ActiveCheckoutWidget = nullptr;

	if (IsValid(PlayerController))
	{
		// 계산 UI가 닫힌 뒤에는 플레이어가 다시 캐릭터를 조작할 수 있게 복구합니다.
		FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(false);
	}
}

void ACSCheckoutCounterActor::SpawnCheckoutDisplayProducts()
{
	ClearCheckoutDisplayProducts();

	if (!IsValid(CheckoutDisplayOrigin) || CheckoutDisplayProductsPerRow < 1)
	{
		return;
	}

	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	ACSCustomerCharacter* FrontCustomer =
		IsValid(CustomerSubsystem) ? CustomerSubsystem->GetFrontCustomer() : nullptr;
	const UCSCustomerStateComponent* CustomerState =
		IsValid(FrontCustomer) ? FrontCustomer->GetCustomerState() : nullptr;
	if (!IsValid(World) || !IsValid(CustomerState))
	{
		return;
	}

	const FVector OriginLocation = CheckoutDisplayOrigin->GetComponentLocation();
	const FVector RightDirection = CheckoutDisplayOrigin->GetRightVector();
	const FVector ForwardDirection = CheckoutDisplayOrigin->GetForwardVector();
	const FVector UpDirection = CheckoutDisplayOrigin->GetUpVector();
	const FQuat OriginRotation = CheckoutDisplayOrigin->GetComponentQuat();

	for (int32 ProductIndex = 0; ProductIndex < CustomerState->SelectProducts.Num(); ++ProductIndex)
	{
		const FCSSelectProduct& SelectedProduct = CustomerState->SelectProducts[ProductIndex];
		if (SelectedProduct.ProductClass == nullptr ||
			SelectedProduct.ProductRow.DataTable == nullptr ||
			SelectedProduct.ProductRow.RowName.IsNone())
		{
			continue;
		}

		const int32 RowIndex = ProductIndex / CheckoutDisplayProductsPerRow;
		const int32 ColumnIndex = ProductIndex % CheckoutDisplayProductsPerRow;
		const FVector BaseLocation = OriginLocation +
			RightDirection * (CheckoutDisplayRightSpacing * static_cast<float>(ColumnIndex)) +
			ForwardDirection * (CheckoutDisplayForwardSpacing * static_cast<float>(RowIndex));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* SpawnedActor = World->SpawnActor<AActor>(
			SelectedProduct.ProductClass,
			BaseLocation,
			OriginRotation.Rotator(),
			SpawnParameters);
		ACSProductPickupActor* DisplayProduct = Cast<ACSProductPickupActor>(SpawnedActor);
		if (!IsValid(DisplayProduct) ||
			!DisplayProduct->InitializeProduct(SelectedProduct.ProductRow))
		{
			if (IsValid(SpawnedActor))
			{
				SpawnedActor->Destroy();
			}
			continue;
		}

		// 표시 전용 액터는 플레이어가 다시 집거나 충돌에 밀리지 않도록 비상호작용 상태로 둡니다.
		DisplayProduct->SetActorEnableCollision(false);
		const float ProductHalfHeight =
			DisplayProduct->GetComponentsBoundingBox(false).GetExtent().Z;
		DisplayProduct->SetActorLocation(BaseLocation + UpDirection * ProductHalfHeight);
		DisplayProduct->AttachToComponent(
			CheckoutDisplayOrigin,
			FAttachmentTransformRules::KeepWorldTransform);

		CheckoutDisplayProducts.Add(DisplayProduct);
		CheckoutDisplayProductIds.Add(SelectedProduct.ProductRow.RowName);
	}
}

void ACSCheckoutCounterActor::ClearCheckoutDisplayProducts()
{
	for (ACSProductPickupActor* DisplayProduct : CheckoutDisplayProducts)
	{
		if (IsValid(DisplayProduct))
		{
			DisplayProduct->Destroy();
		}
	}

	CheckoutDisplayProducts.Reset();
	CheckoutDisplayProductIds.Reset();
}
