#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "DuckovPlayerController.h"
#include "GameSessionSubsystem.h"
#include "InteractionViewModel.h"
#include "InventoryGridWidget.h"
#include "InventoryItemWidget.h"
#include "InventoryModel.h"
#include "InventoryScreenWidget.h"
#include "ItemViewModel.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace EquipmentSlotWidgetTests
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

ADuckovPlayerController* GetController()
{
    UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
    return World ? Cast<ADuckovPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
}

UInventoryScreenWidget* GetScreen()
{
    return Field<UInventoryScreenWidget>(GetController(), TEXT("Screen"));
}

UCanvasPanel* GetCanvas(UInventoryGridWidget* Grid)
{
    return Grid ? Cast<UCanvasPanel>(Grid->GetWidgetFromName(TEXT("GridCanvas"))) : nullptr;
}

TArray<UInventoryItemWidget*> GetItemWidgets(UInventoryGridWidget* Grid)
{
    TArray<UInventoryItemWidget*> Result;
    if (UCanvasPanel* Canvas = GetCanvas(Grid))
    {
        for (UWidget* Child : Canvas->GetAllChildren())
        {
            if (UInventoryItemWidget* Widget = Cast<UInventoryItemWidget>(Child)) { Result.Add(Widget); }
        }
    }
    return Result;
}

UInventoryItemWidget* FindItemWidget(UInventoryGridWidget* Grid, int32 InstanceId)
{
    for (UInventoryItemWidget* Widget : GetItemWidgets(Grid))
    {
        const UItemViewModel* Item = Field<UItemViewModel>(Widget, TEXT("Item"));
        if (Item && Item->GetInstanceId() == InstanceId) { return Widget; }
    }
    return nullptr;
}

// 실제 배치된 grid 칸 위의 화면 좌표. 칸 왼쪽 위에서 조금 안쪽을 가리킨다.
FVector2D CellPosition(UInventoryGridWidget* Grid, FIntPoint Cell)
{
    UCanvasPanel* Canvas = GetCanvas(Grid);
    return Canvas ? Canvas->GetCachedGeometry().LocalToAbsolute(FVector2D(Cell.X * 52.f + 12.f, Cell.Y * 52.f + 12.f))
        : FVector2D::ZeroVector;
}

// 드래그는 화면의 공개 경로를 그대로 탄다: 아이템 위젯 geometry로 시작하고 화면 좌표로 옮긴다.
bool BeginDrag(UInventoryScreenWidget* Screen, UInventoryGridWidget* Grid, FName Source, int32 InstanceId)
{
    UInventoryItemWidget* Widget = FindItemWidget(Grid, InstanceId);
    UItemViewModel* Item = Field<UItemViewModel>(Widget, TEXT("Item"));
    if (!Widget || !Item) { return false; }
    const FGeometry& Geometry = Widget->GetCachedGeometry();
    return Screen->BeginItemDrag(Source, Item, Geometry, Geometry.LocalToAbsolute(FVector2D(12.f)));
}

UBorder* GetPreview(UInventoryGridWidget* Grid)
{
    return Field<UBorder>(Grid, TEXT("PreviewBorder"));
}

bool IsPreviewShown(UInventoryGridWidget* Grid)
{
    const UBorder* Preview = GetPreview(Grid);
    return Preview && Preview->GetVisibility() == ESlateVisibility::HitTestInvisible;
}

FString GetPreviewText(UInventoryGridWidget* Grid)
{
    const UTextBlock* Text = Field<UTextBlock>(Grid, TEXT("PreviewText"));
    return Text ? Text->GetText().ToString() : FString();
}

bool IsInside(const UWidget* Widget, const UWidget* Ancestor)
{
    for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
    {
        if (Current == Ancestor) { return true; }
    }
    return false;
}
}

// 베이스먼트 세션: 캐릭터 패널의 무기·방어구 slot으로 끌어 장착하고, 다른 종류는 이유와 함께 거부하고, 가방으로 끌어 해제한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentSlotsPIE, "Duckov.UI.EquipmentSlots", Flags)
bool FEquipmentSlotsPIE::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("베이스먼트 map load"), AutomationOpenMap(TEXT("/Game/Maps/L_Basement")))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("베이스먼트 controller"), []
    {
        ADuckovPlayerController* Controller = GetController();
        return Controller && Controller->GetPawn() && Field<UInventoryModel>(Controller, TEXT("Model"));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this]
    {
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
        FSlateApplication& Slate = FSlateApplication::Get();
        const FKeyEvent Event(EKeys::I, FModifierKeysState(), Slate.GetUserIndexForKeyboard(), false, 0, 0);
        Slate.ProcessKeyDownEvent(Event);
        Slate.ProcessKeyUpEvent(Event);
    }, 0.f));
    // 코드에서 만든 slot grid에 실제 배치 geometry가 생길 때까지 기다린다.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("I로 인벤토리와 slot 배치"), []
    {
        UInventoryScreenWidget* Screen = GetScreen();
        UInventoryGridWidget* Weapon = Field<UInventoryGridWidget>(Screen, TEXT("WeaponSlotGrid"));
        UCanvasPanel* Canvas = GetCanvas(Weapon);
        return Screen && Screen->IsActivated() && Canvas && Canvas->GetCachedGeometry().GetLocalSize().X > 1.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this]
    {
        ADuckovPlayerController* Controller = GetController();
        UInventoryScreenWidget* Screen = GetScreen();
        UInventoryModel* Model = Field<UInventoryModel>(Controller, TEXT("Model"));
        UInteractionViewModel* Interaction = Field<UInteractionViewModel>(Screen, TEXT("Interaction"));
        UInventoryGridWidget* Stash = Cast<UInventoryGridWidget>(Screen ? Screen->GetWidgetFromName(TEXT("LeftGrid")) : nullptr);
        UInventoryGridWidget* Bag = Cast<UInventoryGridWidget>(Screen ? Screen->GetWidgetFromName(TEXT("RightGrid")) : nullptr);
        UInventoryGridWidget* Weapon = Field<UInventoryGridWidget>(Screen, TEXT("WeaponSlotGrid"));
        UInventoryGridWidget* Armor = Field<UInventoryGridWidget>(Screen, TEXT("ArmorSlotGrid"));
        UWidget* Panel = Field<UWidget>(Screen, TEXT("EquipmentSlotsPanel"));
        if (!TestNotNull(TEXT("세션 Model"), Model) || !TestNotNull(TEXT("Interaction VM"), Interaction)
            || !TestTrue(TEXT("네 grid"), Stash && Bag && Weapon && Armor) || !TestNotNull(TEXT("slot 줄"), Panel)) { return; }
        TestTrue(TEXT("세션 Model을 그대로 사용"), Model == Controller->GetGameInstance()->GetSubsystem<UGameSessionSubsystem>()->GetInventoryModel());

        // 배치: slot 줄은 캐릭터 패널의 장비 자리 안에 있고, 두 slot이 등록돼 있어 보인다.
        TestTrue(TEXT("slot 줄은 장비 자리 안"), IsInside(Panel, Screen->GetWidgetFromName(TEXT("EquipmentSlot"))));
        TestTrue(TEXT("slot 줄은 캐릭터 패널 안"), IsInside(Panel, Screen->GetWidgetFromName(TEXT("CharacterPanel"))));
        TestEqual(TEXT("slot 줄 표시"), Panel->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
        TestEqual(TEXT("시작: 무기 slot 비어 있음"), GetItemWidgets(Weapon).Num(), 0);
        TestNotNull(TEXT("시작: 방어구 장착 표시"), FindItemWidget(Armor, 6));

        // 장착: 창고의 소총을 무기 slot에 끌어 놓는다.
        if (!TestTrue(TEXT("소총 끌기 시작"), BeginDrag(Screen, Stash, TEXT("Stash"), 1))) { return; }
        const FVector2D WeaponCell = CellPosition(Weapon, FIntPoint(1, 0));
        Screen->UpdateItemDrag(WeaponCell);
        TestEqual(TEXT("미리보기 대상은 무기 slot"), Interaction->GetTargetContainerId(), FName(TEXT("WeaponSlot")));
        TestTrue(TEXT("무기 slot 미리보기 표시"), IsPreviewShown(Weapon));
        TestEqual(TEXT("배치 가능 표시"), GetPreviewText(Weapon), FString(TEXT("배치 가능")));
        const UCanvasPanelSlot* PreviewSlot = GetPreview(Weapon) ? Cast<UCanvasPanelSlot>(GetPreview(Weapon)->Slot) : nullptr;
        TestTrue(TEXT("가리킨 칸과 상관없이 slot 원점 미리보기"), PreviewSlot && PreviewSlot->GetPosition().IsNearlyZero());
        Screen->EndItemDrag(WeaponCell);
        const FItemInstance* Rifle = Model->FindItem(TEXT("WeaponSlot"), 1);
        TestTrue(TEXT("같은 ID로 slot 원점에 장착"), Rifle && Rifle->AnchorCell == FIntPoint::ZeroValue && !Rifle->bRotated);
        TestNull(TEXT("창고에서 빠짐"), Model->FindItem(TEXT("Stash"), 1));
        TestNotNull(TEXT("무기 slot에 소총 표시"), FindItemWidget(Weapon, 1));
        TestFalse(TEXT("놓은 뒤 끌기 끝"), Interaction->IsDragging());

        // 회전 중에도 slot 미리보기는 원래 방향(2x1)이다.
        if (TestTrue(TEXT("소총 다시 끌기"), BeginDrag(Screen, Weapon, TEXT("WeaponSlot"), 1)))
        {
            Interaction->Rotate();
            Screen->UpdateItemDrag(WeaponCell);
            PreviewSlot = GetPreview(Weapon) ? Cast<UCanvasPanelSlot>(GetPreview(Weapon)->Slot) : nullptr;
            TestTrue(TEXT("회전 중 slot 미리보기는 무회전 크기"), PreviewSlot && PreviewSlot->GetSize().Equals(FVector2D(100.f, 48.f)));
            Screen->CancelItemDrag();
        }

        // 실패: 방어구를 무기 slot에 놓으면 이유를 보이고 아무것도 바꾸지 않는다.
        if (!TestTrue(TEXT("방어구 끌기 시작"), BeginDrag(Screen, Armor, TEXT("ArmorSlot"), 6))) { return; }
        Screen->UpdateItemDrag(WeaponCell);
        TestEqual(TEXT("다른 종류 미리보기 사유"), Interaction->GetPreviewFailure(), EInventoryOperationFailure::InvalidCategory);
        TestEqual(TEXT("미리보기에 이유 표시"), GetPreviewText(Weapon), FString(TEXT("이 슬롯에 장착할 수 없는 아이템입니다.")));
        Screen->EndItemDrag(WeaponCell);
        TestNotNull(TEXT("거부되면 방어구 그대로"), Model->FindItem(TEXT("ArmorSlot"), 6));
        TestNotNull(TEXT("거부되면 소총 그대로"), Model->FindItem(TEXT("WeaponSlot"), 1));
        UTextBlock* Failure = Field<UTextBlock>(Screen, TEXT("FailureText"));
        TestEqual(TEXT("실패 문구 표시"), Failure ? Failure->GetText().ToString() : FString(),
            FString(TEXT("이 슬롯에 장착할 수 없는 아이템입니다.")));

        // 해제: 끌던 방어구를 가방 빈칸에 놓는다.
        const FVector2D BagCell = CellPosition(Bag, FIntPoint::ZeroValue);
        Screen->UpdateItemDrag(BagCell);
        Screen->EndItemDrag(BagCell);
        const FItemInstance* Unequipped = Model->FindItem(TEXT("Bag"), 6);
        TestTrue(TEXT("방어구를 놓은 칸으로 해제"), Unequipped && Unequipped->AnchorCell == FIntPoint::ZeroValue);
        TestEqual(TEXT("방어구 slot 표시 비움"), GetItemWidgets(Armor).Num(), 0);
        TestNotNull(TEXT("가방에 방어구 표시"), FindItemWidget(Bag, 6));

        // 분할창이 열린 동안에는 slot 줄도 막는다.
        UItemViewModel* Medkit = Field<UItemViewModel>(FindItemWidget(Bag, 3), TEXT("Item"));
        if (TestTrue(TEXT("구급상자 분할창 열기"), Medkit && Screen->OpenSplitDialog(TEXT("Bag"), Medkit)))
        {
            TestFalse(TEXT("분할 중 slot 줄 비활성"), Panel->GetIsEnabled());
            Screen->NativeOnKeyDown(Screen->GetCachedGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
            TestTrue(TEXT("분할창 닫으면 slot 줄 활성"), Panel->GetIsEnabled());
        }
        Screen->DeactivateWidget();
        TestEqual(TEXT("닫으면 slot 줄 감춤"), Panel->GetVisibility(), ESlateVisibility::Collapsed);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitFor(*this, TEXT("PIE 종료"), [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}
}
#endif
