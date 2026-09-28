#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryTickProbe.h"
#include "InventoryPerformanceProbe.h"
#include "InventoryDemoPlayerController.h"
#include "InventoryGridWidget.h"
#include "InventoryItemWidget.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "ItemDefinitionRow.h"
#include "Components/CanvasPanel.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/LatentActionManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

FInventoryTickProbe* GInventoryTickProbe = nullptr;

namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
constexpr int32 ItemCount = 100;

template <typename T>
T* ReflectedObject(UObject* Owner, FName Name)
{
    const FObjectProperty* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}

struct FInventoryTickScene
{
    TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
    FInventoryTickProbe TickProbe;
    FInventoryPerformanceProbe RefreshProbe;
    TWeakObjectPtr<UInventoryScreenWidget> Screen;
    TWeakObjectPtr<UWorld> World;
    int32 OriginalCounter = FItemInstanceIdAllocator::GetNextInstanceId();
    uint64 StartFrame = 0;
    float StartWorldSeconds = 0.f;
    double StartSeconds = 0.0;

    ~FInventoryTickScene()
    {
        GInventoryTickProbe = nullptr;
        GInventoryPerformanceProbe = nullptr;
        FItemInstanceIdAllocator::ResetInstanceIdCounter_ForTests(OriginalCounter);
    }
};

class FWaitForCondition final : public IAutomationLatentCommand
{
public:
    FWaitForCondition(FAutomationTestBase& InTest, FString InName, TFunction<bool()> InCondition)
        : Test(InTest), Name(MoveTemp(InName)), Condition(MoveTemp(InCondition)) {}

    virtual bool Update() override
    {
        if (Started == 0.0) { Started = FPlatformTime::Seconds(); }
        if (Condition()) { return true; }
        if (FPlatformTime::Seconds() - Started < 60.0) { return false; }
        Test.AddError(FString::Printf(TEXT("PIE timeout: %s"), *Name));
        return true;
    }
private:
    FAutomationTestBase& Test;
    FString Name;
    TFunction<bool()> Condition;
    double Started = 0.0;
};

void LogTickPolicy(const TCHAR* Name, UUserWidget* Widget)
{
    UClass* NativeParent = Widget ? Widget->GetClass() : nullptr;
    while (NativeParent && !NativeParent->HasAnyClassFlags(CLASS_Native)) { NativeParent = NativeParent->GetSuperClass(); }
    const FBoolProperty* ScriptTick = FindFProperty<FBoolProperty>(UUserWidget::StaticClass(), TEXT("bHasScriptImplementedTick"));
    const UWidgetBlueprintGeneratedClass* BlueprintClass = Widget
        ? Cast<UWidgetBlueprintGeneratedClass>(Widget->GetClass()) : nullptr;
    const UFunction* TickFunction = Widget ? Widget->GetClass()->FindFunctionByName(TEXT("Tick")) : nullptr;
    const bool bBlueprintTick = Widget && TickFunction
        && TickFunction->GetOuterUClass() == Widget->GetClass() && TickFunction->Script.Num() > 0;
    UE_LOG(LogTemp, Display,
        TEXT("InventoryNoPersistentTick policy %s: class=%s frequency=%s slateCanTick=%d nativeRequired=%d disableNativeTick=%d blueprintTick=%d animations=%d bindings=%d"),
        Name, Widget ? *Widget->GetClass()->GetName() : TEXT("null"),
        Widget && Widget->GetDesiredTickFrequency() == EWidgetTickFrequency::Never ? TEXT("Never") : TEXT("Auto"),
        Widget && Widget->TakeWidget()->GetCanTick(), BlueprintClass && BlueprintClass->ClassRequiresNativeTick(),
        Widget && Widget->GetClass()->HasMetaData(TEXT("DisableNativeTick")), bBlueprintTick,
        BlueprintClass ? BlueprintClass->Animations.Num() : 0,
        BlueprintClass ? BlueprintClass->Bindings.Num() : 0);
    UE_LOG(LogTemp, Display, TEXT("InventoryNoPersistentTick dependency %s: nativeParent=%s nativeDisableTick=%d scriptTick=%d latent=%d animationPlaying=%d"),
        Name, *GetNameSafe(NativeParent), NativeParent && NativeParent->HasMetaData(TEXT("DisableNativeTick")),
        Widget && ScriptTick && ScriptTick->GetPropertyValue_InContainer(Widget),
        Widget && Widget->GetWorld() ? Widget->GetWorld()->GetLatentActionManager().GetNumActionsForObject(Widget) : 0,
        Widget && Widget->IsAnyAnimationPlaying());
}

void LogBlueprintClass(const TCHAR* Name, UClass* WidgetClass)
{
    const UWidgetBlueprintGeneratedClass* BlueprintClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetClass);
    const UFunction* TickFunction = WidgetClass ? WidgetClass->FindFunctionByName(TEXT("Tick")) : nullptr;
    UE_LOG(LogTemp, Display,
        TEXT("InventoryNoPersistentTick asset %s: class=%s nativeRequired=%d disableNativeTick=%d blueprintTick=%d animations=%d bindings=%d"),
        Name, *GetNameSafe(WidgetClass), BlueprintClass && BlueprintClass->ClassRequiresNativeTick(),
        WidgetClass && WidgetClass->HasMetaData(TEXT("DisableNativeTick")),
        WidgetClass && TickFunction && TickFunction->GetOuterUClass() == WidgetClass && TickFunction->Script.Num() > 0,
        BlueprintClass ? BlueprintClass->Animations.Num() : 0,
        BlueprintClass ? BlueprintClass->Bindings.Num() : 0);
}

template <typename WidgetType>
void LogTickRange(const TCHAR* Name, const TSet<const WidgetType*>& Tracked,
    const TMap<const WidgetType*, int32>& CallsByWidget)
{
    int32 Minimum = MAX_int32;
    int32 Maximum = 0;
    for (const WidgetType* Widget : Tracked)
    {
        const int32 Calls = CallsByWidget.FindRef(Widget);
        Minimum = FMath::Min(Minimum, Calls);
        Maximum = FMath::Max(Maximum, Calls);
    }
    UE_LOG(LogTemp, Display, TEXT("InventoryNoPersistentTick %s: tracked=%d ticked=%d perWidgetMin=%d perWidgetMax=%d"),
        Name, Tracked.Num(), CallsByWidget.Num(), Tracked.IsEmpty() ? 0 : Minimum, Maximum);
}

int32 CountItemWidgets(UCanvasPanel* Canvas, FInventoryTickProbe& TickProbe, FInventoryPerformanceProbe& RefreshProbe)
{
    int32 Count = 0;
    for (int32 Index = 0; Canvas && Index < Canvas->GetChildrenCount(); ++Index)
    {
        if (UInventoryItemWidget* Item = Cast<UInventoryItemWidget>(Canvas->GetChildAt(Index)))
        {
            TickProbe.Items.Add(Item);
            RefreshProbe.TrackedWidgets.Add(Item);
            ++Count;
        }
    }
    return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryNoPersistentTick, "Duckov.Performance.InventoryNoPersistentTick", Flags)
bool FInventoryNoPersistentTick::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Slate initialized"), FSlateApplication::IsInitialized())
        || !TestTrue(TEXT("데모 map load"), AutomationOpenMap(TEXT("/Game/Maps/L_InventoryDemo")))) { return false; }
    TSharedRef<FInventoryTickScene> Scene = MakeShared<FInventoryTickScene>();
    Scene->Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Definition;
    Definition.Size = FIntPoint(1, 1);
    Scene->Table->AddRow(TEXT("OneCell"), Definition);

    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("데모 PIE controller"), []
    {
        return GEditor && GEditor->PlayWorld
            && Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        AInventoryDemoPlayerController* Controller = World
            ? Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        UInventoryModel* Model = ReflectedObject<UInventoryModel>(Controller, TEXT("Model"));
        if (!TestNotNull(TEXT("PIE Model"), Model)) { return; }
        Scene->World = World;
        FInventorySaveRecord Record;
        Record.NextInstanceId = ItemCount + 2;
        auto& Stash = Record.Containers.AddDefaulted_GetRef();
        Stash.ContainerId = TEXT("Stash");
        Stash.GridSize = FIntPoint(20, 20);
        const FSoftObjectPath DefinitionTable(Scene->Table.Get());
        for (int32 Index = 0; Index < ItemCount; ++Index)
        {
            auto& Item = Stash.Items.AddDefaulted_GetRef();
            Item.InstanceId = Index + 1;
            Item.DefinitionTable = DefinitionTable;
            Item.DefinitionRowName = TEXT("OneCell");
            Item.AnchorCell = FIntPoint(Index % 20, Index / 20);
        }
        auto& Bag = Record.Containers.AddDefaulted_GetRef();
        Bag.ContainerId = TEXT("Bag");
        Bag.GridSize = FIntPoint(4, 4);
        if (!TestEqual(TEXT("100개 fixture Load"), Model->Load(Record), EInventorySaveFailure::None)) { return; }
        const FInventoryContainer* Loaded = Model->FindContainer(TEXT("Stash"));
        if (!TestNotNull(TEXT("20x20 Stash"), Loaded)) { return; }
        TestEqual(TEXT("Stash GridSize"), Loaded->GridSize, FIntPoint(20, 20));
        TestEqual(TEXT("Stash item count"), Loaded->Items.Num(), ItemCount);
        TSet<FIntPoint> Anchors;
        for (const FItemInstance& Item : Loaded->Items) { Anchors.Add(Item.AnchorCell); }
        TestEqual(TEXT("겹치지 않는 1x1 cell"), Anchors.Num(), ItemCount);

        UClass* ScreenClass = LoadClass<UInventoryScreenWidget>(nullptr,
            TEXT("/Game/UI/WBP_InventoryScreen.WBP_InventoryScreen_C"));
        if (!TestNotNull(TEXT("실제 WBP Screen class"), ScreenClass)) { return; }
        UClass* GridClass = LoadClass<UInventoryGridWidget>(nullptr,
            TEXT("/Game/UI/WBP_InventoryGrid.WBP_InventoryGrid_C"));
        UClass* ItemClass = LoadClass<UInventoryItemWidget>(nullptr,
            TEXT("/Game/UI/WBP_InventoryItem.WBP_InventoryItem_C"));
        TestNotNull(TEXT("WBP Grid class"), GridClass);
        TestNotNull(TEXT("WBP Item class"), ItemClass);
        LogBlueprintClass(TEXT("WBP_InventoryScreen"), ScreenClass);
        LogBlueprintClass(TEXT("WBP_InventoryGrid"), GridClass);
        LogBlueprintClass(TEXT("WBP_InventoryItem"), ItemClass);
        UInventoryScreenWidget* Screen = CreateWidget<UInventoryScreenWidget>(Controller, ScreenClass);
        if (!TestNotNull(TEXT("실제 WBP Screen"), Screen)) { return; }
        Scene->Screen = Screen;
        Screen->SetSession(Model);
        Screen->SetVisibility(ESlateVisibility::Collapsed);
        Screen->AddToViewport(10);
        Screen->ActivateWidget();
        TestTrue(TEXT("Inventory screen 활성"), Screen->IsActivated() && Screen->IsInViewport());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("100 Item Slate geometry"), [Scene]
    {
        UInventoryScreenWidget* Screen = Scene->Screen.Get();
        UInventoryGridWidget* Grid = Screen ? Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("LeftGrid"))) : nullptr;
        UCanvasPanel* Canvas = Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
        int32 Items = 0;
        bool bAllItemGeometryReady = true;
        for (int32 Index = 0; Canvas && Index < Canvas->GetChildrenCount(); ++Index)
        {
            if (UInventoryItemWidget* Item = Cast<UInventoryItemWidget>(Canvas->GetChildAt(Index)))
            {
                ++Items;
                bAllItemGeometryReady &= Item->GetCachedGeometry().GetLocalSize().X > 0.f;
            }
        }
        UInventoryGridWidget* Right = Screen ? Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("RightGrid"))) : nullptr;
        return Screen && Screen->IsActivated() && Screen->IsInViewport()
            && Screen->GetCachedGeometry().GetLocalSize().X > 0.f
            && Grid->GetCachedGeometry().GetLocalSize().X > 0.f
            && Right && Right->GetCachedGeometry().GetLocalSize().X > 0.f
            && Canvas && Canvas->GetCachedGeometry().GetLocalSize().X > 0.f
            && Items == ItemCount && bAllItemGeometryReady;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        UInventoryScreenWidget* Screen = Scene->Screen.Get();
        UWorld* World = Scene->World.Get();
        if (!TestNotNull(TEXT("idle Screen"), Screen) || !TestNotNull(TEXT("idle World"), World)) { return; }
        UInventoryGridWidget* Left = Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("LeftGrid")));
        UInventoryGridWidget* Right = Cast<UInventoryGridWidget>(Screen->GetWidgetFromName(TEXT("RightGrid")));
        UCanvasPanel* Canvas = Left ? Cast<UCanvasPanel>(Left->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
        if (!TestNotNull(TEXT("Stash grid"), Left) || !TestNotNull(TEXT("Bag grid"), Right)
            || !TestNotNull(TEXT("Stash canvas"), Canvas)) { return; }
        Scene->TickProbe.Screen = Screen;
        Scene->TickProbe.Grids.Add(Left);
        Scene->TickProbe.Grids.Add(Right);
        Scene->RefreshProbe.TrackedGrids.Add(Left);
        Scene->RefreshProbe.TrackedGrids.Add(Right);
        const int32 ActualItems = CountItemWidgets(Canvas, Scene->TickProbe, Scene->RefreshProbe);
        TestEqual(TEXT("실제 Item Widget 100개"), ActualItems, ItemCount);
        TestEqual(TEXT("계측 Grid 2개"), Scene->TickProbe.Grids.Num(), 2);
        TestEqual(TEXT("계측 Item 100개"), Scene->TickProbe.Items.Num(), ItemCount);
        LogTickPolicy(TEXT("WBP_InventoryScreen"), Screen);
        LogTickPolicy(TEXT("LeftGrid"), Left);
        LogTickPolicy(TEXT("RightGrid"), Right);
        if (ActualItems > 0)
        {
            const UInventoryItemWidget* First = *Scene->TickProbe.Items.CreateConstIterator();
            LogTickPolicy(TEXT("WBP_InventoryItem"), const_cast<UInventoryItemWidget*>(First));
        }
        Scene->TickProbe.Reset();
        Scene->RefreshProbe.Reset();
        GInventoryTickProbe = &Scene->TickProbe;
        GInventoryPerformanceProbe = &Scene->RefreshProbe;
        Scene->StartFrame = GFrameCounter;
        Scene->StartWorldSeconds = World->GetTimeSeconds();
        Scene->StartSeconds = FPlatformTime::Seconds();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("active Inventory idle 300 frames"), [Scene]
    {
        return Scene->StartFrame > 0 && GFrameCounter - Scene->StartFrame >= 300;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        GInventoryTickProbe = nullptr;
        GInventoryPerformanceProbe = nullptr;
        UInventoryScreenWidget* Screen = Scene->Screen.Get();
        UWorld* World = Scene->World.Get();
        TestTrue(TEXT("실제 idle 300 engine frames"), GFrameCounter - Scene->StartFrame >= 300);
        TestTrue(TEXT("PIE world 진행"), World && World->GetTimeSeconds() > Scene->StartWorldSeconds);
        TestTrue(TEXT("활성 Inventory 화면 유지"), Screen && Screen->IsActivated() && Screen->IsInViewport() && Screen->IsVisible()
            && Screen->GetCachedGeometry().GetLocalSize().X > 0.f);
        UE_LOG(LogTemp, Display,
            TEXT("InventoryNoPersistentTick idle: frames=%llu seconds=%.3f screen=%d grids=%d items=%d uniqueGrids=%d uniqueItems=%d gridRefresh=%d itemRefresh=%d"),
            GFrameCounter - Scene->StartFrame, FPlatformTime::Seconds() - Scene->StartSeconds,
            Scene->TickProbe.ScreenCalls, Scene->TickProbe.GridCalls, Scene->TickProbe.ItemCalls,
            Scene->TickProbe.GridCallsByWidget.Num(), Scene->TickProbe.ItemCallsByWidget.Num(),
            Scene->RefreshProbe.GridRefresh, Scene->RefreshProbe.ItemRefresh);
        LogTickRange(TEXT("Grid"), Scene->TickProbe.Grids, Scene->TickProbe.GridCallsByWidget);
        LogTickRange(TEXT("Item"), Scene->TickProbe.Items, Scene->TickProbe.ItemCallsByWidget);
        TestEqual(TEXT("idle Screen project NativeTick 0"), Scene->TickProbe.ScreenCalls, 0);
        TestEqual(TEXT("idle Grid project NativeTick 0"), Scene->TickProbe.GridCalls, 0);
        TestEqual(TEXT("idle Item project NativeTick 0"), Scene->TickProbe.ItemCalls, 0);
        TestEqual(TEXT("idle Grid Refresh 0"), Scene->RefreshProbe.GridRefresh, 0);
        TestEqual(TEXT("idle Item Refresh 0"), Scene->RefreshProbe.ItemRefresh, 0);
    }, 0.f));
    // 앞선 fixture 또는 runtime assertion이 실패해도 화면과 계측을 정리한다.
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Scene]
    {
        GInventoryTickProbe = nullptr;
        GInventoryPerformanceProbe = nullptr;
        if (UInventoryScreenWidget* Screen = Scene->Screen.Get())
        {
            Screen->DeactivateWidget();
            Screen->RemoveFromParent();
        }
        Scene->Screen.Reset();
        Scene->World.Reset();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCondition(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}
#endif
