#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InteractionViewModel.h"
#include "InventoryDemoPlayerController.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace InventoryLayoutTests
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

// 위젯이 지정한 이름의 조상 아래에 있는지 본다.
bool IsInside(const UWidget* Widget, const UWidget* Ancestor)
{
    for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
    {
        if (Current == Ancestor) { return true; }
    }
    return false;
}

FString GetHeader(UInventoryScreenWidget* Screen, FName GridName)
{
    UUserWidget* Grid = Cast<UUserWidget>(Screen->GetWidgetFromName(GridName));
    const UTextBlock* Header = Grid ? Cast<UTextBlock>(Grid->GetWidgetFromName(TEXT("HeaderText"))) : nullptr;
    return Header ? Header->GetText().ToString() : FString();
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
}

// 원작 배치: 왼쪽 캐릭터 패널(장비·가방), 오른쪽 창고 패널. 헤더의 용량, 전부 창고에 보관의 성공·실패, 창고 잠금.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryLayout, "Duckov.UI.InventoryLayout", Flags)
bool FInventoryLayout::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("데모 map load"), AutomationOpenMap(TEXT("/Game/Maps/L_InventoryDemo")))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("데모 controller"), []
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        auto* Controller = World ? Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        return Controller && Field<UInventoryModel>(Controller, TEXT("Model"));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this]
    {
        auto* Controller = Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
        if (!TestNotNull(TEXT("controller"), Controller)) { return; }
        UInventoryModel* Model = Field<UInventoryModel>(Controller, TEXT("Model"));
        TestEqual(TEXT("가방 장착 데모 준비"), Controller->TryStartBagEquipmentDemo(), EInventoryOperationFailure::None);
        Controller->ToggleInventory();
        UInventoryScreenWidget* Screen = Field<UInventoryScreenWidget>(Controller, TEXT("Screen"));
        if (!TestTrue(TEXT("인벤토리 열림"), Screen && Screen->IsActivated())) { return; }

        // 배치: 가방 격자는 왼쪽 캐릭터 패널, 창고 격자는 오른쪽 창고 패널.
        UWidget* CharacterPanel = Screen->GetWidgetFromName(TEXT("CharacterPanel"));
        UWidget* StashPanel = Screen->GetWidgetFromName(TEXT("StashPanel"));
        const UCanvasPanelSlot* CharacterSlot = CharacterPanel ? Cast<UCanvasPanelSlot>(CharacterPanel->Slot) : nullptr;
        const UCanvasPanelSlot* StashSlot = StashPanel ? Cast<UCanvasPanelSlot>(StashPanel->Slot) : nullptr;
        TestTrue(TEXT("캐릭터 패널은 왼쪽"), CharacterSlot && CharacterSlot->GetAnchors().Minimum.X == 0.f);
        TestTrue(TEXT("창고 패널은 오른쪽"), StashSlot && StashSlot->GetAnchors().Minimum.X == 1.f);
        TestTrue(TEXT("가방 격자는 캐릭터 패널 안"), IsInside(Screen->GetWidgetFromName(TEXT("RightGrid")), CharacterPanel));
        TestTrue(TEXT("창고 격자는 창고 패널 안"), IsInside(Screen->GetWidgetFromName(TEXT("LeftGrid")), StashPanel));
        TestTrue(TEXT("가방 슬롯은 장비 자리 안"), IsInside(Screen->GetWidgetFromName(TEXT("BagEquipmentPanel")),
            Screen->GetWidgetFromName(TEXT("EquipmentSlot"))));

        // 용량: 차지한 칸 / 전체 칸. 창고 = Rifle 2 + Ammo 1 + LargeBag 1, 가방 = Medkit 2x2.
        TestEqual(TEXT("가방 헤더 용량"), GetHeader(Screen, TEXT("RightGrid")), FString(TEXT("가방 (4/16)")));
        TestEqual(TEXT("창고 헤더 용량"), GetHeader(Screen, TEXT("LeftGrid")), FString(TEXT("창고 (4/24)")));

        // 1칸 아이템의 긴 이름은 말줄임표로 줄인다(대형 가방 1x1).
        bool bTruncatedBag = false;
        UUserWidget* StashGrid = Cast<UUserWidget>(Screen->GetWidgetFromName(TEXT("LeftGrid")));
        if (const UCanvasPanel* Canvas = StashGrid ? Cast<UCanvasPanel>(StashGrid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr)
        {
            for (UWidget* Child : Canvas->GetAllChildren())
            {
                const UUserWidget* ItemWidget = Cast<UUserWidget>(Child);
                const UTextBlock* Text = ItemWidget ? Cast<UTextBlock>(ItemWidget->GetWidgetFromName(TEXT("ItemText"))) : nullptr;
                bTruncatedBag |= Text && Text->GetText().ToString() == TEXT("x1\nLarg…");
            }
        }
        TestTrue(TEXT("1칸 긴 이름 말줄임"), bTruncatedBag);

        // 전부 창고에 보관: 같은 ID로 옮기고 헤더가 바로 바뀐다.
        UButton* StoreAll = Cast<UButton>(Screen->GetWidgetFromName(TEXT("StoreAllButton")));
        if (!TestTrue(TEXT("전부 창고에 보관 버튼 활성"), StoreAll && StoreAll->GetIsEnabled())) { return; }
        const TArray<int32> BagIds = GetIds(Model, TEXT("Bag"));
        // 아이템을 끄는 동안에는 가방 장착처럼 전부 보관도 막는다.
        UInteractionViewModel* Interaction = Field<UInteractionViewModel>(Screen, TEXT("Interaction"));
        if (TestNotNull(TEXT("Interaction VM"), Interaction) && TestTrue(TEXT("가방 항목 있음"), BagIds.Num() > 0))
        {
            Interaction->BeginDrag(TEXT("Bag"), BagIds[0]);
            TestFalse(TEXT("끄는 중 전부 보관 비활성"), StoreAll->GetIsEnabled());
            Interaction->Cancel();
            TestTrue(TEXT("끌기 취소 뒤 전부 보관 활성"), StoreAll->GetIsEnabled());
        }
        StoreAll->OnClicked.Broadcast();
        TestEqual(TEXT("가방이 비워짐"), GetIds(Model, TEXT("Bag")).Num(), 0);
        for (const int32 Id : BagIds) { TestNotNull(TEXT("같은 ID로 창고에"), Model->FindItem(TEXT("Stash"), Id)); }
        TestEqual(TEXT("보관 뒤 가방 헤더"), GetHeader(Screen, TEXT("RightGrid")), FString(TEXT("가방 (0/16)")));
        TestEqual(TEXT("보관 뒤 창고 헤더"), GetHeader(Screen, TEXT("LeftGrid")), FString(TEXT("창고 (8/24)")));
        UTextBlock* Failure = Field<UTextBlock>(Screen, TEXT("FailureText"));
        TestTrue(TEXT("성공하면 실패 문구 없음"), Failure && Failure->GetText().IsEmpty());

        // 실패: 창고가 가득 차면 아무것도 옮기지 않고 이유를 보인다.
        UDataTable* Definitions = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
        FInventorySaveRecord Full;
        int32 NextId = 1;
        auto& Stash = Full.Containers.AddDefaulted_GetRef();
        Stash.ContainerId = TEXT("Stash");
        Stash.GridSize = FIntPoint(6, 4);
        for (int32 Cell = 0; Cell < 24; ++Cell)
        {
            auto& Item = Stash.Items.AddDefaulted_GetRef();
            Item.InstanceId = NextId++;
            Item.DefinitionTable = FSoftObjectPath(Definitions);
            Item.DefinitionRowName = TEXT("Ammo");
            Item.AnchorCell = FIntPoint(Cell % 6, Cell / 6);
            Item.Quantity = 30;
        }
        auto& Bag = Full.Containers.AddDefaulted_GetRef();
        Bag.ContainerId = TEXT("Bag");
        Bag.GridSize = FIntPoint(4, 4);
        auto& Medkit = Bag.Items.AddDefaulted_GetRef();
        Medkit.InstanceId = NextId++;
        Medkit.DefinitionTable = FSoftObjectPath(Definitions);
        Medkit.DefinitionRowName = TEXT("Medkit");
        Medkit.Quantity = 1;
        Full.NextInstanceId = NextId;
        if (!TestEqual(TEXT("가득 찬 창고 load"), Model->Load(Full), EInventorySaveFailure::None)) { return; }
        StoreAll->OnClicked.Broadcast();
        TestTrue(TEXT("실패하면 가방 유지"), GetIds(Model, TEXT("Bag")) == TArray<int32>{25});
        TestEqual(TEXT("실패하면 창고 유지"), GetIds(Model, TEXT("Stash")).Num(), 24);
        TestEqual(TEXT("공간 부족 이유 표시"), Failure ? Failure->GetText().ToString() : FString(), FString(TEXT("배치할 공간이 없습니다.")));

        // 창고 잠금(레이드): 창고 패널을 감추고 전부 보관을 막는다. 비활성 버튼 delegate를 직접 불러도 바뀌지 않는다.
        Screen->SetStashAccessible(false);
        TestEqual(TEXT("잠기면 창고 패널 감춤"), StashPanel ? StashPanel->GetVisibility() : ESlateVisibility::Visible, ESlateVisibility::Collapsed);
        TestFalse(TEXT("잠기면 전부 보관 비활성"), StoreAll->GetIsEnabled());
        StoreAll->OnClicked.Broadcast();
        TestTrue(TEXT("잠긴 상태의 직접 호출은 무시"), GetIds(Model, TEXT("Bag")) == TArray<int32>{25});
        Screen->SetStashAccessible(true);
        TestEqual(TEXT("다시 열리면 창고 패널 표시"), StashPanel ? StashPanel->GetVisibility() : ESlateVisibility::Collapsed,
            ESlateVisibility::SelfHitTestInvisible);
        TestTrue(TEXT("다시 열리면 전부 보관 활성"), StoreAll->GetIsEnabled());
        Screen->DeactivateWidget();
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}
}
#endif
