#include "DuckovPlayerController.h"

#include "AimCrosshair.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/WidgetComponent.h"
#include "CustomizationScreenWidget.h"
#include "DuckovCharacter.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameHudWidget.h"
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
#include "Misc/PackageName.h"
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
    Hud = CreateWidget<UGameHudWidget>(this, UGameHudWidget::StaticClass());
    RootLayout->AddHudElement(Hud);
    // 두 맵이 같은 GameMode를 쓰므로 장소 이름은 맵으로 정한다.
    bInRaid = UWorld::RemovePIEPrefix(GetWorld()->GetMapName()) == FPackageName::GetShortName(DuckovFlow::RaidMap);
    Hud->SetLocation(bInRaid ? NSLOCTEXT("DuckovFlow", "RaidZone", "레이드 구역") : NSLOCTEXT("DuckovFlow", "Base", "기지"));
    // 레이드에서 돌아온 직후라면 탈출 결과를 한 번 알린다.
    if (const TOptional<EInventoryOperationFailure> Result = Session->ConsumeExtractionResult())
    {
        Hud->SetMessage(DuckovFlow::GetExtractionResultText(*Result));
    }
    // 원작처럼 게임 중 커서는 십자선이다. Slate가 커서 위치에 그리므로 위치를 갱신하는 tick이 없다.
    DefaultMouseCursor = EMouseCursor::Crosshairs;
    CurrentMouseCursor = EMouseCursor::Crosshairs;
    if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
    {
        Viewport->SetSoftwareCursorWidget(EMouseCursor::Crosshairs, SNew(SAimCrosshair));
    }
    RestoreGameInput();
    // 맵 이동으로 도착했다면 원작처럼 검은 화면에서 클릭을 기다린 뒤 원이 커지며 드러낸다. 그동안 캐릭터는 움직이지 않는다.
    if (Session->ConsumeArrivalPending())
    {
        LoadingScreen = CreateWidget<ULoadingScreenWidget>(this, ULoadingScreenWidget::StaticClass());
        RootLayout->AddNoticeElement(LoadingScreen);
        LoadingScreen->OnContinued().AddUObject(this, &ThisClass::OnArrivalContinued);
        LoadingScreen->BeginArrival();
        SetIgnoreMoveInput(true);
    }
    RefreshHud();
}

void ADuckovPlayerController::OnArrivalContinued()
{
    // 원이 커지는 동안 화면은 스스로 빠진다. 조작과 HUD는 클릭한 순간 돌려준다.
    LoadingScreen = nullptr;
    SetIgnoreMoveInput(false);
    RefreshHud();
}

void ADuckovPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ThisClass::ToggleInventory);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ThisClass::Interact);
    InputComponent->BindKey(EKeys::O, IE_Pressed, this, &ThisClass::ToggleControls);
    InputComponent->BindKey(EKeys::L, IE_Pressed, this, &ThisClass::ToggleLaserSight);
}

void ADuckovPlayerController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    if (!InPawn) { return; }
    // 상호작용 대상은 pawn의 overlap 이벤트로만 바뀐다. 거리를 매 프레임 확인하지 않는다.
    InPawn->OnActorBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnPawnBeginOverlap);
    InPawn->OnActorEndOverlap.AddUniqueDynamic(this, &ThisClass::OnPawnEndOverlap);
    // 원작처럼 프롬프트는 물체 위치에 붙는다. 하나만 만들어 대상에 옮겨 붙이고, 대상이 없으면 숨긴다.
    // 화면 위치 투영은 엔진의 screen layer가 맡고, Automatic tick mode는 widget이 보일 때만 component를 tick한다.
    if (IsLocalController() && !PromptComponent)
    {
        PromptComponent = NewObject<UWidgetComponent>(InPawn);
        PromptComponent->SetWidgetSpace(EWidgetSpace::Screen);
        PromptComponent->SetWidgetClass(UInteractionPromptWidget::StaticClass());
        PromptComponent->SetDrawAtDesiredSize(true);
        PromptComponent->SetPivot(FVector2D(0.f, 0.5f));
        PromptComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PromptComponent->SetOwnerPlayer(GetLocalPlayer());
        PromptComponent->SetVisibility(false);
        PromptComponent->SetTickMode(ETickMode::Automatic);
        PromptComponent->RegisterComponent();
        PromptComponent->InitWidget();
        if (UUserWidget* Prompt = PromptComponent->GetUserWidgetObject()) { Prompt->SetVisibility(ESlateVisibility::Collapsed); }
    }
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
    RefreshHud();
    DestroyPromptComponent();
    Super::OnUnPossess();
}

void ADuckovPlayerController::OnPawnBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
    UInteractionRangeComponent* Range = OtherActor ? OtherActor->FindComponentByClass<UInteractionRangeComponent>() : nullptr;
    if (!Range || Targets.Contains(Range)) { return; }
    Targets.Add(Range);
    // 이전 결과 문구는 다른 대상으로 옮겨 가면 지운다.
    if (Hud) { Hud->SetMessage(FText::GetEmpty()); }
    RefreshHud();
}

void ADuckovPlayerController::OnPawnEndOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
    const int32 Removed = Targets.RemoveAll([OtherActor](const TWeakObjectPtr<UInteractionRangeComponent>& Range)
        { return !Range.IsValid() || Range->GetOwner() == OtherActor; });
    if (Removed == 0) { return; }
    if (Hud) { Hud->SetMessage(FText::GetEmpty()); }
    RefreshHud();
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
    if (Result == EInventoryOperationFailure::None || !Hud) { return; }
    Hud->SetMessage(Result == EInventoryOperationFailure::NoSpace
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
    TravelMap = Map;
    // 원이 오므라드는 동안 캐릭터를 멈춘다. 맵을 떠나므로 되돌리지 않는다.
    SetIgnoreMoveInput(true);
    LoadingScreen->OnCovered().AddUObject(this, &ThisClass::OnDepartureCovered);
    LoadingScreen->BeginDeparture();
    RefreshHud();
}

void ADuckovPlayerController::OnDepartureCovered()
{
    RefreshHud();
    // 다 덮인 순간은 Slate paint 도중이라 "로딩 중..."은 다음 frame에야 그려진다. 바로 OpenLevel을 부르면
    // 다음 frame의 engine tick이 paint보다 먼저 맵을 내리므로, 한 tick 미뤄 문구가 한 번 그려진 뒤 이동한다.
    TravelHandle = GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::TravelAfterDeparture);
}

void ADuckovPlayerController::TravelAfterDeparture()
{
    GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->SetArrivalPending();
    UGameplayStatics::OpenLevel(this, TravelMap);
}

void ADuckovPlayerController::ToggleInventory()
{
    if (Screen && Screen->IsActivated()) { Screen->DeactivateWidget(); return; }
    OpenInventory(!bInRaid);
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

void ADuckovPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    // 메뉴나 로딩 화면이 있는 동안 커서는 UI를 가리키므로 조준을 바꾸지 않는다. 커서가 viewport 밖이면 마지막 조준을 유지한다.
    ADuckovCharacter* Aimer = Cast<ADuckovCharacter>(GetPawn());
    FVector2D Mouse;
    FVector Point;
    if (Aimer && !IsMenuOpen() && !LoadingScreen && GetMousePosition(Mouse.X, Mouse.Y) && ScreenToAimPoint(Mouse, Point))
    {
        Aimer->SetAimPoint(Point);
    }
}

bool ADuckovPlayerController::ScreenToAimPoint(const FVector2D& ScreenPosition, FVector& OutPoint) const
{
    const APawn* CurrentPawn = GetPawn();
    FVector Origin, Direction;
    if (!CurrentPawn || !DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, Origin, Direction)) { return false; }
    // 탑다운 카메라의 시선은 아래를 향한다. 수평에 가깝거나 위를 향하면 면과 만나지 않는다.
    if (Direction.Z > -UE_KINDA_SMALL_NUMBER) { return false; }
    const float Distance = (CurrentPawn->GetActorLocation().Z - Origin.Z) / Direction.Z;
    if (Distance <= 0.f) { return false; }
    OutPoint = Origin + Direction * Distance;
    return true;
}

bool ADuckovPlayerController::IsMenuOpen() const
{
    return RootLayout && RootLayout->GetMenuStack()->GetActiveWidget() != nullptr;
}

void ADuckovPlayerController::ToggleControls()
{
    if (Hud && !IsMenuOpen() && !LoadingScreen) { Hud->ToggleControls(); }
}

void ADuckovPlayerController::ToggleLaserSight()
{
    // 캐릭터마다 상태를 가지므로 맵을 이동하면 새 캐릭터는 다시 꺼진 상태로 시작한다.
    if (ADuckovCharacter* Aimer = Cast<ADuckovCharacter>(GetPawn()))
    {
        Aimer->SetLaserSightEnabled(!Aimer->IsLaserSightEnabled());
    }
}

void ADuckovPlayerController::RefreshHud()
{
    if (!Hud) { return; }
    // 원작처럼 메뉴가 열리거나 로딩 화면이 게임을 다 덮으면 HUD를 감춘다. 원이 오므라들거나 커지는 동안에는 HUD도 원 안에 보인다.
    const bool bVisible = !IsMenuOpen() && !(LoadingScreen && LoadingScreen->IsCoveringGame());
    Hud->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    // 프롬프트는 빙의한 pawn이 있을 때만 있다. 로딩 화면이 있으면 상호작용할 수 없으므로 감춘다.
    if (!PromptComponent) { return; }
    UInteractionRangeComponent* Target = bVisible && !LoadingScreen ? GetInteractionTarget() : nullptr;
    UUserWidget* Prompt = PromptComponent->GetUserWidgetObject();
    if (Target)
    {
        if (PromptComponent->GetAttachParent() != Target)
        {
            PromptComponent->AttachToComponent(Target, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
            // 물체 중심보다 조금 위에 ◎를 둔다.
            PromptComponent->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
        }
        if (auto* PromptWidget = Cast<UInteractionPromptWidget>(Prompt)) { PromptWidget->SetAction(DuckovFlow::GetActionName(Target->Kind)); }
    }
    else if (PromptComponent->GetAttachParent())
    {
        PromptComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }
    // Automatic tick mode는 widget이 보이는 동안 tick을 끄지 않으므로 widget 자체를 감춰야 tick 비용이 사라진다.
    PromptComponent->SetVisibility(Target != nullptr);
    if (Prompt) { Prompt->SetVisibility(Target ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
}

void ADuckovPlayerController::DestroyPromptComponent()
{
    // pawn이 먼저 파괴됐을 수 있다. 살아 있으면 붙어 있던 대상에서 떼어 함께 정리한다.
    if (IsValid(PromptComponent)) { PromptComponent->DestroyComponent(); }
    PromptComponent = nullptr;
}

void ADuckovPlayerController::OnMenuChanged(UCommonActivatableWidget* DisplayedMenu)
{
    // 메뉴가 모두 닫혀도 Router는 Game 입력·viewport capture·커서를 되돌리지 않으므로 게임 코드가 되돌린다.
    if (!DisplayedMenu) { RestoreInputHandle = GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::RestoreGameInput); }
    RefreshHud();
}

void ADuckovPlayerController::RestoreGameInput()
{
    if (IsMenuOpen()) { return; }
    if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
    {
        if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
        {
            // 커서 위치로 조준하므로 커서를 보이게 두고 고정하지 않는다. CapturePermanently는 high precision 마우스라
            // 커서가 한 자리에 묶인다. 누르는 동안만 capture해 viewport 밖으로 끌어도 버튼 입력을 놓치지 않는다.
            Router->SetActiveUIInputConfig(FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CaptureDuringMouseDown, false));
        }
    }
    // Router는 local player에 남아 맵을 넘어도 같은 설정을 다시 적용하지 않는다. 새 controller의 커서 표시는 직접 켠다.
    SetShowMouseCursor(true);
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
    GetWorldTimerManager().ClearTimer(TravelHandle);
    // 세션 Model은 다음 맵에서도 남는다. 화면을 비활성화해 이 맵의 ViewModel 구독을 먼저 끊는다.
    if (CustomizationScreen && CustomizationScreen->IsActivated()) { CustomizationScreen->DeactivateWidget(); }
    if (Screen && Screen->IsActivated()) { Screen->DeactivateWidget(); }
    if (RootLayout) { RootLayout->RemoveFromParent(); }
    RootLayout = nullptr;
    Screen = nullptr;
    CustomizationScreen = nullptr;
    CustomizationModel = nullptr;
    Hud = nullptr;
    DestroyPromptComponent();
    LoadingScreen = nullptr;
    Model = nullptr;
    Super::EndPlay(EndPlayReason);
}
