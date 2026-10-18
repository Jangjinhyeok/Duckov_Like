#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "DuckovCharacter.h"
#include "DuckovPlayerController.h"
#include "GameSessionSubsystem.h"
#include "InteractionPointActor.h"
#include "GameHudWidget.h"
#include "InteractionPromptWidget.h"
#include "InteractionRangeComponent.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "LoadingScreenWidget.h"
#include "WorldLootActor.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/DataTable.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Input/CommonUIActionRouterBase.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"

namespace RaidFlowTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

template <typename T>
T* Field(UObject* Owner, FName Name)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

class FWaitFor final : public IAutomationLatentCommand
{
public:
    FWaitFor(FAutomationTestBase& InTest, FString InDescription, TFunction<bool()> InCondition)
        : Test(InTest), Description(MoveTemp(InDescription)), Condition(MoveTemp(InCondition)) {}
    virtual bool Update() override
    {
        if (Started == 0.0) { Started = FPlatformTime::Seconds(); }
        if (Condition()) { return true; }
        if (FPlatformTime::Seconds() - Started < 20.0) { return false; }
        Test.AddError(FString::Printf(TEXT("PIE timeout: %s"), *Description));
        return true;
    }
private:
    FAutomationTestBase& Test;
    FString Description;
    TFunction<bool()> Condition;
    double Started = 0.0;
};

struct FFlowScene
{
    TWeakObjectPtr<ADuckovPlayerController> Controller;
    TWeakObjectPtr<ADuckovCharacter> Character;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UGameInstance> GameInstance;
    TWeakObjectPtr<UInventoryModel> Model;
    TWeakObjectPtr<AWorldLootActor> Loot;
    TArray<int32> CarriedIds;
    int32 BagQuantityBefore = 0;
    int32 LootQuantity = 0;
    FInventorySaveRecord Snapshot;
    bool bOriginalInactiveInput = false;
};

void PressKey(FKey Key)
{
    FSlateApplication& Slate = FSlateApplication::Get();
    const FKeyEvent Event(Key, FModifierKeysState(), Slate.GetUserIndexForKeyboard(), false, 0, 0);
    Slate.ProcessKeyDownEvent(Event);
    Slate.ProcessKeyUpEvent(Event);
}

bool IsViewportFocused()
{
    return GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid()
        && GEngine->GameViewport->GetGameViewportWidget()->HasAnyUserFocus().IsSet();
}

bool IsGameInputActive(ADuckovPlayerController* Controller)
{
    UCommonUIActionRouterBase* Router = Controller && Controller->GetLocalPlayer()
        ? Controller->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
    return Router && Router->GetActiveInputMode() == ECommonInputMode::Game && IsViewportFocused();
}

// 지정한 맵이 PIE에서 열리고 지상 캐릭터가 착지하면 scene을 그 맵으로 바꾼다. 이전 맵의 world면 기다린다.
bool CaptureLandedScene(FFlowScene& Scene, const TCHAR* MapName)
{
    UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
    if (!World || !World->GetMapName().EndsWith(MapName)) { return false; }
    auto* Controller = Cast<ADuckovPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
    auto* Character = Controller ? Cast<ADuckovCharacter>(Controller->GetPawn()) : nullptr;
    if (!Character || !Character->GetCharacterMovement()->IsMovingOnGround()) { return false; }
    Scene.World = World;
    Scene.Controller = Controller;
    Scene.Character = Character;
    return true;
}

bool IsSceneValid(const FFlowScene& Scene)
{
    return Scene.Controller.IsValid() && Scene.Character.IsValid() && Scene.Model.IsValid();
}

AInteractionPointActor* FindPoint(UWorld* World, EInteractionKind Kind)
{
    for (TActorIterator<AInteractionPointActor> It(World); It; ++It)
    {
        if (It->GetRange()->Kind == Kind) { return *It; }
    }
    return nullptr;
}

AWorldLootActor* FindLoot(UWorld* World, FName Row)
{
    for (TActorIterator<AWorldLootActor> It(World); It; ++It)
    {
        if (It->DefinitionRowName == Row) { return *It; }
    }
    return nullptr;
}

// 같은 높이에서 대상 옆으로 순간 이동한다. 이동만으로 overlap이 갱신되는지가 검사 대상이다.
void TeleportNear(ADuckovCharacter* Character, const AActor* Target, const FVector2D& Offset)
{
    const FVector Location = Target->GetActorLocation();
    Character->SetActorLocation(FVector(Location.X + Offset.X, Location.Y + Offset.Y, Character->GetActorLocation().Z),
        false, nullptr, ETeleportType::TeleportPhysics);
}

bool IsTargetKind(const FFlowScene& Scene, EInteractionKind Kind)
{
    const UInteractionRangeComponent* Target = Scene.Controller.IsValid() ? Scene.Controller->GetInteractionTarget() : nullptr;
    return Target && Target->Kind == Kind;
}

FString GetText(UUserWidget* Widget, FName WidgetName)
{
    const UTextBlock* Text = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(WidgetName)) : nullptr;
    return Text ? Text->GetText().ToString() : FString();
}

UWidgetComponent* GetPromptComponent(ADuckovPlayerController* Controller)
{
    return Field<UWidgetComponent>(Controller, TEXT("PromptComponent"));
}

FString GetPromptText(ADuckovPlayerController* Controller)
{
    UWidgetComponent* Component = GetPromptComponent(Controller);
    return GetText(Component ? Component->GetUserWidgetObject() : nullptr, TEXT("PromptText"));
}

FString GetHudText(ADuckovPlayerController* Controller, FName WidgetName)
{
    return GetText(Field<UGameHudWidget>(Controller, TEXT("Hud")), WidgetName);
}

// 프롬프트는 현재 대상의 범위 component에 붙어 있고 보일 때만 위치를 갱신한다.
bool IsPromptShown(ADuckovPlayerController* Controller)
{
    UWidgetComponent* Component = GetPromptComponent(Controller);
    // 숨은 actor가 소유하면 screen space widget이 그려지지 않으므로 소유 actor의 표시 여부도 본다.
    return Component && Component->IsVisible() && Component->IsComponentTickEnabled() && !Component->GetOwner()->IsHidden()
        && Component->GetAttachParent() && Component->GetAttachParent() == Controller->GetInteractionTarget()
        && Cast<UInteractionPromptWidget>(Component->GetUserWidgetObject()) && Component->GetUserWidgetObject()->IsVisible();
}

// 화면에 이미 올라간 widget 자체가 감춰졌는지 본다. component만 숨기면 마지막 위치에 남아 그려진다.
bool IsPromptWidgetVisible(ADuckovPlayerController* Controller)
{
    UWidgetComponent* Component = GetPromptComponent(Controller);
    const UUserWidget* Prompt = Component ? Component->GetUserWidgetObject() : nullptr;
    return Prompt && Prompt->IsVisible();
}

bool IsHudShown(ADuckovPlayerController* Controller)
{
    const UGameHudWidget* Hud = Field<UGameHudWidget>(Controller, TEXT("Hud"));
    return Hud && Hud->IsVisible();
}

bool IsStashPanelShown(UInventoryScreenWidget* Screen)
{
    const UWidget* Panel = Screen ? Screen->GetWidgetFromName(TEXT("StashPanel")) : nullptr;
    return Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

bool IsStashGridEnabled(UInventoryScreenWidget* Screen)
{
    const UWidget* Grid = Screen ? Screen->GetWidgetFromName(TEXT("LeftGrid")) : nullptr;
    return Grid && Grid->GetIsEnabled();
}

int32 GetTotalQuantity(const UInventoryModel* Model, FName ContainerId)
{
    int32 Total = 0;
    if (const FInventoryContainer* Container = Model->FindContainer(ContainerId))
    {
        for (const FItemInstance& Item : Container->Items) { Total += Item.Quantity; }
    }
    return Total;
}

TArray<int32> GetIds(const UInventoryModel* Model, FName ContainerId)
{
    TArray<int32> Ids;
    if (const FInventoryContainer* Container = Model->FindContainer(ContainerId))
    {
        for (const FItemInstance& Item : Container->Items) { Ids.Add(Item.InstanceId); }
    }
    return Ids;
}

void BeginScene(FFlowScene& Scene)
{
    Scene.bOriginalInactiveInput = FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
    FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);
}
}

// 베이스먼트 → 창고 → 출격 → 레이드 loot → 탈출 → 베이스먼트를 실제 F 키 입력과 맵 이동으로 완주한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaidLoopFromBasement, "Duckov.Raid.LoopFromBasement", Flags)
bool FRaidLoopFromBasement::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())) { return false; }
    if (!TestTrue(TEXT("베이스먼트 map load"), AutomationOpenMap(TEXT("/Game/Maps/L_Basement")))) { return false; }
    TSharedRef<FFlowScene> Scene = MakeShared<FFlowScene>();
    BeginScene(*Scene);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("베이스먼트 착지"), [Scene] { return CaptureLandedScene(*Scene, TEXT("L_Basement")); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Controller.IsValid()) { AddError(TEXT("베이스먼트 controller unavailable")); return; }
        Scene->GameInstance = Scene->World->GetGameInstance();
        UInventoryModel* Model = Scene->GameInstance->GetSubsystem<UGameSessionSubsystem>()->GetInventoryModel();
        Scene->Model = Model;
        if (!TestNotNull(TEXT("세션 인벤토리"), Model)) { return; }
        TestTrue(TEXT("controller가 세션 인벤토리를 사용"), Field<UInventoryModel>(Scene->Controller.Get(), TEXT("Model")) == Model);
        TestTrue(TEXT("시작 가방 장착"), Model->IsBagSlotBound() && Model->GetEquippedBag()
            && Model->GetEquippedBag()->DefinitionRowName == TEXT("SmallBag"));
        TestTrue(TEXT("시작 가방에 구급상자"), GetIds(Model, TEXT("Bag")) == TArray<int32>{3});
        TestFalse(TEXT("시작 위치에서는 프롬프트 없음"), IsPromptShown(Scene->Controller.Get()));
        UWidgetComponent* PromptComponent = GetPromptComponent(Scene->Controller.Get());
        TestTrue(TEXT("대상이 없으면 프롬프트 tick 꺼짐"), PromptComponent && !PromptComponent->IsComponentTickEnabled());
        TestTrue(TEXT("HUD 표시"), IsHudShown(Scene->Controller.Get()));
        TestEqual(TEXT("베이스먼트 장소"), GetHudText(Scene->Controller.Get(), TEXT("LocationText")), FString(TEXT("기지")));
        AInteractionPointActor* Stash = FindPoint(Scene->World.Get(), EInteractionKind::Stash);
        if (!TestNotNull(TEXT("창고"), Stash)) { return; }
        TeleportNear(Scene->Character.Get(), Stash, FVector2D(0.f, 110.f));
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("창고 대상"), [Scene]
    {
        return !IsSceneValid(*Scene) || (IsTargetKind(*Scene, EInteractionKind::Stash) && IsViewportFocused());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        PressKey(EKeys::O);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("O로 조작 설명 펼침"), [Scene]
    {
        const UGameHudWidget* Hud = Scene->Controller.IsValid() ? Field<UGameHudWidget>(Scene->Controller.Get(), TEXT("Hud")) : nullptr;
        return !Scene->Controller.IsValid() || (Hud && Hud->AreControlsExpanded());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestTrue(TEXT("창고 프롬프트 표시"), IsPromptShown(Scene->Controller.Get())
            && GetPromptText(Scene->Controller.Get()).Contains(TEXT("창고 열기")));
        PressKey(EKeys::F);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("F로 창고 열림"), [Scene]
    {
        UInventoryScreenWidget* Screen = Scene->Controller.IsValid() ? Field<UInventoryScreenWidget>(Scene->Controller.Get(), TEXT("Screen")) : nullptr;
        return !Scene->Controller.IsValid() || (Screen && Screen->IsActivated());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        UInventoryScreenWidget* Screen = Field<UInventoryScreenWidget>(Scene->Controller.Get(), TEXT("Screen"));
        TestTrue(TEXT("창고에서는 보관함 사용"), IsStashGridEnabled(Screen));
        TestFalse(TEXT("메뉴 위에는 프롬프트를 숨김"), IsPromptShown(Scene->Controller.Get()) || IsPromptWidgetVisible(Scene->Controller.Get()));
        TestFalse(TEXT("메뉴가 열리면 HUD를 숨김"), IsHudShown(Scene->Controller.Get()));
        Screen->DeactivateWidget();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("창고 닫은 뒤 Game 입력"), [Scene]
    {
        return !Scene->Controller.IsValid() || IsGameInputActive(Scene->Controller.Get());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestTrue(TEXT("닫은 뒤 창고 프롬프트 복귀"), IsPromptShown(Scene->Controller.Get()));
        TestTrue(TEXT("닫은 뒤 HUD 복귀"), IsHudShown(Scene->Controller.Get()));
        PressKey(EKeys::I);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("I로 가방 열림"), [Scene]
    {
        UInventoryScreenWidget* Screen = Scene->Controller.IsValid() ? Field<UInventoryScreenWidget>(Scene->Controller.Get(), TEXT("Screen")) : nullptr;
        return !Scene->Controller.IsValid() || (Screen && Screen->IsActivated());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        UInventoryScreenWidget* Screen = Field<UInventoryScreenWidget>(Scene->Controller.Get(), TEXT("Screen"));
        TestTrue(TEXT("원작처럼 베이스먼트의 I는 창고까지 엶"), IsStashGridEnabled(Screen) && IsStashPanelShown(Screen));
        Screen->DeactivateWidget();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("가방 닫은 뒤 Game 입력"), [Scene]
    {
        return !Scene->Controller.IsValid() || IsGameInputActive(Scene->Controller.Get());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        AInteractionPointActor* Sortie = FindPoint(Scene->World.Get(), EInteractionKind::Sortie);
        if (!TestNotNull(TEXT("출격 지점"), Sortie)) { return; }
        TeleportNear(Scene->Character.Get(), Sortie, FVector2D(0.f, -60.f));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("출격 대상"), [Scene]
    {
        return !IsSceneValid(*Scene) || IsTargetKind(*Scene, EInteractionKind::Sortie);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("베이스먼트 scene unavailable")); return; }
        TestTrue(TEXT("출격 프롬프트"), GetPromptText(Scene->Controller.Get()).Contains(TEXT("출격")));
        PressKey(EKeys::F);
    }, 0.f));
    // 맵 로드는 이동을 예약한 다음 engine frame에 일어나므로 그 사이에 로딩 상태를 관찰한다.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("출격 로딩 화면"), [Scene]
    {
        return !Scene->Controller.IsValid() || Field<ULoadingScreenWidget>(Scene->Controller.Get(), TEXT("LoadingScreen"));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Controller.IsValid()) { AddError(TEXT("로딩 화면을 관찰하기 전에 맵이 바뀜")); return; }
        TestFalse(TEXT("로딩 중 프롬프트 숨김"), IsPromptShown(Scene->Controller.Get()) || IsPromptWidgetVisible(Scene->Controller.Get()));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("레이드 맵 이동과 착지"), [Scene] { return CaptureLandedScene(*Scene, TEXT("L_Raid")); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        TestTrue(TEXT("같은 게임 인스턴스"), Scene->World->GetGameInstance() == Scene->GameInstance.Get());
        TestEqual(TEXT("레이드 장소"), GetHudText(Scene->Controller.Get(), TEXT("LocationText")), FString(TEXT("레이드 구역")));
        // 맵 이동 뒤 새 controller도 십자선 커서를 보인다. CommonUI는 같은 입력 설정을 다시 적용하지 않는다.
        TestTrue(TEXT("레이드 도착 뒤 십자선 커서 표시"), Scene->Controller->ShouldShowMouseCursor()
            && Scene->Controller->GetMouseCursor() == EMouseCursor::Crosshairs);
        TestTrue(TEXT("레이드에서도 같은 세션 인벤토리"),
            Field<UInventoryModel>(Scene->Controller.Get(), TEXT("Model")) == Scene->Model.Get());
        AWorldLootActor* Loot = FindLoot(Scene->World.Get(), TEXT("Medkit"));
        Scene->Loot = Loot;
        if (!TestNotNull(TEXT("레이드 구급상자 loot"), Loot)) { return; }
        Scene->BagQuantityBefore = GetTotalQuantity(Scene->Model.Get(), TEXT("Bag"));
        Scene->LootQuantity = Loot->Quantity;
        TeleportNear(Scene->Character.Get(), Loot, FVector2D(-80.f, 0.f));
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("loot 대상"), [Scene]
    {
        return !IsSceneValid(*Scene) || (IsTargetKind(*Scene, EInteractionKind::Loot) && IsViewportFocused());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        TestTrue(TEXT("줍기 프롬프트"), GetPromptText(Scene->Controller.Get()).Contains(TEXT("줍기")));
        PressKey(EKeys::F);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("F로 loot 획득"), [Scene]
    {
        return !IsSceneValid(*Scene) || (!Scene->Loot.IsValid()
            && GetTotalQuantity(Scene->Model.Get(), TEXT("Bag")) == Scene->BagQuantityBefore + Scene->LootQuantity);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        Scene->CarriedIds = GetIds(Scene->Model.Get(), TEXT("Bag"));
        // 세션 가방의 구급상자(2/5)에 loot가 합쳐진다.
        TestEqual(TEXT("획득 구급상자는 기존 스택에 합산"), Scene->CarriedIds.Num(), 1);
        TestFalse(TEXT("획득한 loot는 대상에서 빠짐"), IsTargetKind(*Scene, EInteractionKind::Loot));
        TestNull(TEXT("레이드에는 창고 없음"), FindPoint(Scene->World.Get(), EInteractionKind::Stash));
        PressKey(EKeys::I);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("레이드에서 I로 가방 열림"), [Scene]
    {
        UInventoryScreenWidget* Screen = Scene->Controller.IsValid() ? Field<UInventoryScreenWidget>(Scene->Controller.Get(), TEXT("Screen")) : nullptr;
        return !Scene->Controller.IsValid() || (Screen && Screen->IsActivated());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        UInventoryScreenWidget* Screen = Field<UInventoryScreenWidget>(Scene->Controller.Get(), TEXT("Screen"));
        TestTrue(TEXT("레이드의 I는 가방만 열고 창고 패널을 감춤"), !IsStashGridEnabled(Screen) && !IsStashPanelShown(Screen));
        Screen->DeactivateWidget();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("레이드 가방 닫은 뒤 Game 입력"), [Scene]
    {
        return !Scene->Controller.IsValid() || IsGameInputActive(Scene->Controller.Get());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        AInteractionPointActor* Extraction = FindPoint(Scene->World.Get(), EInteractionKind::Extraction);
        if (!TestNotNull(TEXT("탈출 지점"), Extraction)) { return; }
        TeleportNear(Scene->Character.Get(), Extraction, FVector2D(-60.f, -60.f));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("탈출 대상"), [Scene]
    {
        return !IsSceneValid(*Scene) || IsTargetKind(*Scene, EInteractionKind::Extraction);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        TestTrue(TEXT("탈출 프롬프트"), GetPromptText(Scene->Controller.Get()).Contains(TEXT("탈출")));
        PressKey(EKeys::F);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("베이스먼트 복귀와 착지"), [Scene] { return CaptureLandedScene(*Scene, TEXT("L_Basement")); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("복귀 scene unavailable")); return; }
        UInventoryModel* Model = Scene->Model.Get();
        TestTrue(TEXT("복귀 뒤에도 같은 세션 인벤토리"), Field<UInventoryModel>(Scene->Controller.Get(), TEXT("Model")) == Model);
        TestTrue(TEXT("복귀 뒤 십자선 커서 표시"), Scene->Controller->ShouldShowMouseCursor()
            && Scene->Controller->GetMouseCursor() == EMouseCursor::Crosshairs);
        TestEqual(TEXT("탈출하면 가방이 비워짐"), GetIds(Model, TEXT("Bag")).Num(), 0);
        for (const int32 Id : Scene->CarriedIds)
        {
            TestNotNull(*FString::Printf(TEXT("가방 항목 %d이 같은 ID로 창고에"), Id), Model->FindItem(TEXT("Stash"), Id));
        }
        TestTrue(TEXT("장착 가방 유지"), Model->IsBagSlotBound());
        TestEqual(TEXT("탈출 결과 알림"), GetHudText(Scene->Controller.Get(), TEXT("MessageText")),
            FString(TEXT("탈출 성공: 가방의 아이템을 창고로 옮겼습니다")));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(Scene->bOriginalInactiveInput);
    }, 0.f));
    return true;
}

// 가방과 창고가 가득 찬 상태: loot 획득은 이유를 보이고 상태를 바꾸지 않으며,
// 탈출은 아무것도 옮기지 않은 채 베이스먼트로 돌아가 이유를 알린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaidFullInventory, "Duckov.Raid.FullInventory", Flags)
bool FRaidFullInventory::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())) { return false; }
    if (!TestTrue(TEXT("레이드 map load"), AutomationOpenMap(TEXT("/Game/Maps/L_Raid")))) { return false; }
    TSharedRef<FFlowScene> Scene = MakeShared<FFlowScene>();
    BeginScene(*Scene);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("레이드 착지"), [Scene] { return CaptureLandedScene(*Scene, TEXT("L_Raid")); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Controller.IsValid()) { AddError(TEXT("레이드 controller unavailable")); return; }
        UInventoryModel* Model = Scene->World->GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->GetInventoryModel();
        Scene->Model = Model;
        if (!TestNotNull(TEXT("세션 인벤토리"), Model)) { return; }
        // 창고 6x4는 탄약 24묶음, 가방 4x4는 가득 쌓인 구급상자 4개로 채운다.
        const FSoftObjectPath Definitions(TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
        FInventorySaveRecord Full;
        int32 NextId = 1;
        auto Fill = [&](FName ContainerId, FIntPoint Size, FName Row, FIntPoint Footprint, int32 Quantity)
        {
            auto& Container = Full.Containers.AddDefaulted_GetRef();
            Container.ContainerId = ContainerId;
            Container.GridSize = Size;
            for (int32 Y = 0; Y < Size.Y; Y += Footprint.Y)
            {
                for (int32 X = 0; X < Size.X; X += Footprint.X)
                {
                    auto& Item = Container.Items.AddDefaulted_GetRef();
                    Item.InstanceId = NextId++;
                    Item.DefinitionTable = Definitions;
                    Item.DefinitionRowName = Row;
                    Item.AnchorCell = FIntPoint(X, Y);
                    Item.Quantity = Quantity;
                }
            }
        };
        Fill(TEXT("Stash"), FIntPoint(6, 4), TEXT("Ammo"), FIntPoint(1, 1), 30);
        Fill(TEXT("Bag"), FIntPoint(4, 4), TEXT("Medkit"), FIntPoint(2, 2), 5);
        // 실제 플레이처럼 소형 가방을 장착한 상태로 둔다.
        Fill(TEXT("BagSlot"), FIntPoint(1, 1), TEXT("SmallBag"), FIntPoint(1, 1), 1);
        Full.NextInstanceId = NextId;
        if (!TestEqual(TEXT("가득 찬 인벤토리 load"), Model->Load(Full), EInventorySaveFailure::None)
            || !TestEqual(TEXT("가방 장착"), Model->BindBagSlot(TEXT("BagSlot"), TEXT("Bag"), TEXT("Stash")), EInventoryOperationFailure::None))
        {
            return;
        }
        TestEqual(TEXT("snapshot"), Model->Save(Scene->Snapshot), EInventorySaveFailure::None);
        AWorldLootActor* Loot = FindLoot(Scene->World.Get(), TEXT("Medkit"));
        Scene->Loot = Loot;
        if (!TestNotNull(TEXT("레이드 구급상자 loot"), Loot)) { return; }
        TeleportNear(Scene->Character.Get(), Loot, FVector2D(-80.f, 0.f));
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("loot 대상"), [Scene]
    {
        return !IsSceneValid(*Scene) || (IsTargetKind(*Scene, EInteractionKind::Loot) && IsViewportFocused());
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        PressKey(EKeys::F);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("획득 실패 문구"), [Scene]
    {
        return !IsSceneValid(*Scene) || !GetHudText(Scene->Controller.Get(), TEXT("MessageText")).IsEmpty();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        TestTrue(TEXT("가방이 차면 loot가 남음"), Scene->Loot.IsValid() && IsTargetKind(*Scene, EInteractionKind::Loot));
        TestEqual(TEXT("공간 부족 이유 표시"), GetHudText(Scene->Controller.Get(), TEXT("MessageText")),
            FString(TEXT("가방에 넣을 공간이 없습니다")));
        TestTrue(TEXT("획득 실패는 가방을 바꾸지 않음"), GetIds(Scene->Model.Get(), TEXT("Bag")) == TArray<int32>{25, 26, 27, 28});
        AInteractionPointActor* Extraction = FindPoint(Scene->World.Get(), EInteractionKind::Extraction);
        if (!TestNotNull(TEXT("탈출 지점"), Extraction)) { return; }
        TeleportNear(Scene->Character.Get(), Extraction, FVector2D(-60.f, -60.f));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("탈출 대상"), [Scene]
    {
        return !IsSceneValid(*Scene) || IsTargetKind(*Scene, EInteractionKind::Extraction);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("레이드 scene unavailable")); return; }
        TestTrue(TEXT("대상이 바뀌면 이전 실패 문구 지움"), GetHudText(Scene->Controller.Get(), TEXT("MessageText")).IsEmpty());
        PressKey(EKeys::F);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("창고가 차도 베이스먼트 복귀"), [Scene] { return CaptureLandedScene(*Scene, TEXT("L_Basement")); }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!IsSceneValid(*Scene)) { AddError(TEXT("복귀 scene unavailable")); return; }
        UInventoryModel* Model = Scene->Model.Get();
        TestTrue(TEXT("창고 반영 실패 시 가방 유지"), GetIds(Model, TEXT("Bag")) == TArray<int32>{25, 26, 27, 28});
        TestEqual(TEXT("창고 반영 실패 시 창고 유지"), GetIds(Model, TEXT("Stash")).Num(), 24);
        FInventorySaveRecord After;
        TestEqual(TEXT("복귀 뒤 save"), Model->Save(After), EInventorySaveFailure::None);
        TestEqual(TEXT("다음 ID 유지"), After.NextInstanceId, Scene->Snapshot.NextInstanceId);
        TestTrue(TEXT("창고 반영 실패 뒤에도 장착 가방 유지"), Model->IsBagSlotBound());
        TestEqual(TEXT("창고 공간 부족 알림"), GetHudText(Scene->Controller.Get(), TEXT("MessageText")),
            FString(TEXT("탈출 성공: 창고 공간이 부족해 아이템을 가방에 그대로 두었습니다")));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(Scene->bOriginalInactiveInput);
    }, 0.f));
    return true;
}
}
#endif
