#include "DuckovPlayerController.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "CustomizationScreenWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameRootLayoutWidget.h"
#include "GameSessionSubsystem.h"
#include "Input/CommonUIActionRouterBase.h"
#include "InputCoreTypes.h"
#include "InteractionPromptWidget.h"
#include "InteractionRangeComponent.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "Kismet/GameplayStatics.h"
#include "LoadingScreenWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "WorldLootActor.h"

namespace DuckovFlow
{
const FName BasementMap(TEXT("/Game/Maps/L_Basement"));
const FName RaidMap(TEXT("/Game/Maps/L_Raid"));

FText GetActionName(EInteractionKind Kind)
{
    switch (Kind)
    {
    case EInteractionKind::Stash: return NSLOCTEXT("DuckovFlow", "OpenStash", "창고 열기");
    case EInteractionKind::Sortie: return NSLOCTEXT("DuckovFlow", "Sortie", "출격");
    case EInteractionKind::Extraction: return NSLOCTEXT("DuckovFlow", "Extract", "탈출");
    case EInteractionKind::Loot: return NSLOCTEXT("DuckovFlow", "Pickup", "줍기");
    }
    return FText::GetEmpty();
}

FText GetExtractionResultText(EInventoryOperationFailure Result)
{
    switch (Result)
    {
    case EInventoryOperationFailure::None:
        return NSLOCTEXT("DuckovFlow", "ExtractionStored", "탈출 성공: 가방의 아이템을 창고로 옮겼습니다");
    case EInventoryOperationFailure::NoSpace:
        return NSLOCTEXT("DuckovFlow", "ExtractionStashFull", "탈출 성공: 창고 공간이 부족해 아이템을 가방에 그대로 두었습니다");
    default:
        return NSLOCTEXT("DuckovFlow", "ExtractionKept", "탈출 성공: 창고로 옮기지 못해 아이템을 가방에 그대로 두었습니다");
    }
}
}

void ADuckovPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) { return; }
    UGameSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>();
    Model = Session->GetInventoryModel();
    // 모든 메뉴는 이 layout의 Menu stack을 거쳐 열린다. stack이 비면 게임 입력으로 되돌린다.
    RootLayout = CreateWidget<UGameRootLayoutWidget>(this, UGameRootLayoutWidget::StaticClass());
    RootLayout->AddToViewport();
    RootLayout->GetMenuStack()->OnDisplayedWidgetChanged().AddUObject(this, &ThisClass::OnMenuChanged);
    Prompt = CreateWidget<UInteractionPromptWidget>(this, UInteractionPromptWidget::StaticClass());
    RootLayout->AddNoticeElement(Prompt);
    // 레이드에서 돌아온 직후라면 탈출 결과를 한 번 알린다.
    if (const TOptional<EInventoryOperationFailure> Result = Session->ConsumeExtractionResult())
    {
        Prompt->SetMessage(DuckovFlow::GetExtractionResultText(*Result));
    }
    SetInputMode(FInputModeGameOnly());
    bShowMouseCursor = false;
    RefreshPrompt();
}

void ADuckovPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ThisClass::ToggleInventory);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ThisClass::Interact);
}

void ADuckovPlayerController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    if (!InPawn) { return; }
    // 상호작용 대상은 pawn의 overlap 이벤트로만 바뀐다. 거리를 매 프레임 확인하지 않는다.
    InPawn->OnActorBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnPawnBeginOverlap);
    InPawn->OnActorEndOverlap.AddUniqueDynamic(this, &ThisClass::OnPawnEndOverlap);
    TArray<AActor*> Overlapping;
    InPawn->GetOverlappingActors(Overlapping);
    for (AActor* Actor : Overlapping) { OnPawnBeginOverlap(InPawn, Actor); }
}

void ADuckovPlayerController::OnUnPossess()
{
    if (APawn* PossessedPawn = GetPawn())
    {
        PossessedPawn->OnActorBeginOverlap.RemoveDynamic(this, &ThisClass::OnPawnBeginOverlap);
        PossessedPawn->OnActorEndOverlap.RemoveDynamic(this, &ThisClass::OnPawnEndOverlap);
    }
    Targets.Reset();
    RefreshPrompt();
    Super::OnUnPossess();
}

void ADuckovPlayerController::OnPawnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
    UInteractionRangeComponent* Range = OtherActor ? OtherActor->FindComponentByClass<UInteractionRangeComponent>() : nullptr;
    if (!Range || Targets.Contains(Range)) { return; }
    Targets.Add(Range);
    // 이전 결과 문구는 다른 대상으로 옮겨 가면 지운다.
    if (Prompt) { Prompt->SetMessage(FText::GetEmpty()); }
    RefreshPrompt();
}

void ADuckovPlayerController::OnPawnEndOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
    const int32 Removed = Targets.RemoveAll([OtherActor](const TWeakObjectPtr<UInteractionRangeComponent>& Range)
        { return !Range.IsValid() || Range->GetOwner() == OtherActor; });
    if (Removed == 0) { return; }
    if (Prompt) { Prompt->SetMessage(FText::GetEmpty()); }
    RefreshPrompt();
}

UInteractionRangeComponent* ADuckovPlayerController::GetInteractionTarget() const
{
    for (int32 Index = Targets.Num() - 1; Index >= 0; --Index)
    {
        // 획득된 loot는 파괴 전에 충돌부터 꺼지므로 그 순간에도 대상이 되지 않는다.
        UInteractionRangeComponent* Range = Targets[Index].Get();
        if (Range && Range->IsCollisionEnabled() && !Range->GetOwner()->IsActorBeingDestroyed()) { return Range; }
    }
    return nullptr;
}

void ADuckovPlayerController::Interact()
{
    if (!Model || LoadingScreen || IsMenuOpen()) { return; }
    UInteractionRangeComponent* Target = GetInteractionTarget();
    if (!Target) { return; }
    switch (Target->Kind)
    {
    case EInteractionKind::Stash: OpenInventory(true); return;
    case EInteractionKind::Sortie: StartTravel(DuckovFlow::RaidMap); return;
    case EInteractionKind::Extraction: Extract(); return;
    case EInteractionKind::Loot: PickupLoot(Target); return;
    }
}

void ADuckovPlayerController::PickupLoot(UInteractionRangeComponent* Target)
{
    AWorldLootActor* Loot = Cast<AWorldLootActor>(Target->GetOwner());
    if (!Loot) { return; }
    // 성공하면 loot가 사라지며 end overlap이 대상과 문구를 정리한다. 실패만 이유를 남긴다.
    const EInventoryOperationFailure Result = Loot->TryPickup(Model, TEXT("Bag"));
    UE_LOG(LogTemp, Display, TEXT("월드 loot 획득 결과: %s"), *UEnum::GetValueAsString(Result));
    if (Result == EInventoryOperationFailure::None || !Prompt) { return; }
    Prompt->SetMessage(Result == EInventoryOperationFailure::NoSpace
        ? NSLOCTEXT("DuckovFlow", "LootNoSpace", "가방에 넣을 공간이 없습니다")
        : NSLOCTEXT("DuckovFlow", "LootFailed", "지금은 주울 수 없습니다"));
}

void ADuckovPlayerController::Extract()
{
    // 탈출은 항상 베이스먼트로 돌아간다. 창고에 다 들어가지 않으면 원자적 이전이 아무것도 옮기지 않고
    // 가방이 그대로 남으므로, 레이드에 갇히지 않고 창고에서 정리할 수 있다.
    const EInventoryOperationFailure Result = Model->TryTransferAll(TEXT("Bag"), TEXT("Stash"));
    UE_LOG(LogTemp, Display, TEXT("탈출 창고 반영 결과: %s"), *UEnum::GetValueAsString(Result));
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->SetExtractionResult(Result);
    StartTravel(DuckovFlow::BasementMap);
}

void ADuckovPlayerController::StartTravel(FName Map)
{
    if (LoadingScreen || !RootLayout) { return; }
    LoadingScreen = CreateWidget<ULoadingScreenWidget>(this, ULoadingScreenWidget::StaticClass());
    RootLayout->AddNoticeElement(LoadingScreen);
    RefreshPrompt();
    // OpenLevel은 이동을 예약만 하고 실제 맵 로드는 다음 engine frame에 한다. 그 사이 로딩 화면이 한 번 그려진다.
    UGameplayStatics::OpenLevel(this, Map);
}

void ADuckovPlayerController::ToggleInventory()
{
    if (Screen && Screen->IsActivated()) { Screen->DeactivateWidget(); return; }
    OpenInventory(false);
}

void ADuckovPlayerController::OpenInventory(bool bStashAccessible)
{
    if (!Model || !RootLayout || LoadingScreen || IsMenuOpen()) { return; }
    UClass* ScreenClass = LoadClass<UInventoryScreenWidget>(nullptr, TEXT("/Game/UI/WBP_InventoryScreen.WBP_InventoryScreen_C"));
    if (!ensureMsgf(ScreenClass, TEXT("인벤토리 화면 Widget Blueprint가 없습니다"))) { return; }
    // 닫으면 Menu stack이 화면을 pool로 돌려보내고, 다시 열 때 같은 인스턴스를 재사용한다.
    Screen = RootLayout->GetMenuStack()->AddWidget<UInventoryScreenWidget>(ScreenClass,
        [this, bStashAccessible](UInventoryScreenWidget& NewScreen)
        {
            NewScreen.SetSession(Model);
            NewScreen.SetStashAccessible(bStashAccessible);
        });
}

void ADuckovPlayerController::OpenCustomization()
{
    if (!RootLayout || LoadingScreen || IsMenuOpen()) { return; }
    // 타이틀에서 확정한 외형을 이어서 편집하도록 세션이 소유한 Model을 쓴다.
    if (!CustomizationModel) { CustomizationModel = GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->GetCustomizationModel(); }
    CustomizationScreen = RootLayout->GetMenuStack()->AddWidget<UCustomizationScreenWidget>(UCustomizationScreenWidget::StaticClass(),
        [this](UCustomizationScreenWidget& NewScreen) { NewScreen.SetSession(CustomizationModel); });
}

bool ADuckovPlayerController::IsMenuOpen() const
{
    return RootLayout && RootLayout->GetMenuStack()->GetActiveWidget() != nullptr;
}

void ADuckovPlayerController::RefreshPrompt()
{
    if (!Prompt) { return; }
    // 메뉴나 로딩 화면 위에 프롬프트와 결과 문구가 겹치지 않게 한다.
    const bool bVisible = !IsMenuOpen() && !LoadingScreen;
    Prompt->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    const UInteractionRangeComponent* Target = bVisible ? GetInteractionTarget() : nullptr;
    Prompt->SetPrompt(Target
        ? FText::Format(NSLOCTEXT("DuckovFlow", "PromptFormat", "{0}  [F]"), DuckovFlow::GetActionName(Target->Kind))
        : FText::GetEmpty());
}

void ADuckovPlayerController::OnMenuChanged(UCommonActivatableWidget* DisplayedMenu)
{
    // 메뉴가 모두 닫혀도 Router는 Game 입력·viewport capture·커서를 되돌리지 않으므로 게임 코드가 되돌린다.
    if (!DisplayedMenu) { RestoreInputHandle = GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::RestoreGameInput); }
    RefreshPrompt();
}

void ADuckovPlayerController::RestoreGameInput()
{
    if (IsMenuOpen()) { return; }
    if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
    {
        if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
        {
            Router->SetActiveUIInputConfig(FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently));
        }
    }
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
}

void ADuckovPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (APawn* CurrentPawn = GetPawn())
    {
        CurrentPawn->OnActorBeginOverlap.RemoveDynamic(this, &ThisClass::OnPawnBeginOverlap);
        CurrentPawn->OnActorEndOverlap.RemoveDynamic(this, &ThisClass::OnPawnEndOverlap);
    }
    Targets.Reset();
    if (RootLayout) { RootLayout->GetMenuStack()->OnDisplayedWidgetChanged().RemoveAll(this); }
    GetWorldTimerManager().ClearTimer(RestoreInputHandle);
    // 세션 Model은 다음 맵에서도 남는다. 화면을 비활성화해 이 맵의 ViewModel 구독을 먼저 끊는다.
    if (CustomizationScreen && CustomizationScreen->IsActivated()) { CustomizationScreen->DeactivateWidget(); }
    if (Screen && Screen->IsActivated()) { Screen->DeactivateWidget(); }
    if (RootLayout) { RootLayout->RemoveFromParent(); }
    RootLayout = nullptr;
    Screen = nullptr;
    CustomizationScreen = nullptr;
    CustomizationModel = nullptr;
    Prompt = nullptr;
    LoadingScreen = nullptr;
    Model = nullptr;
    Super::EndPlay(EndPlayReason);
}
