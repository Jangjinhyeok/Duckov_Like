#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryDemoGameMode.h"
#include "InventoryGridWidget.h"
#include "InventoryItemWidget.h"
#include "InventoryScreenWidget.h"
#include "ItemDefinitionRow.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

namespace InventoryEditorAssetsTests
{
namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
const TCHAR* const Paths[] = {
    TEXT("/Game/Inventory/DT_ItemDefinitions"), TEXT("/Game/UI/WBP_InventoryItem"),
    TEXT("/Game/UI/WBP_InventoryGrid"), TEXT("/Game/UI/WBP_InventoryScreen"),
    TEXT("/Game/Maps/L_InventoryDemo")
};

bool SaveAsset(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

UWidgetBlueprint* CreateWidgetAsset(const TCHAR* Path, UClass* Parent)
{
    UPackage* Package = CreatePackage(Path);
    UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        Parent, Package, FName(*FPackageName::GetLongPackageAssetName(Path)), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
    return Blueprint;
}

template<typename T>
T* Add(UWidgetTree* Tree, FName Name)
{
    T* Widget = Tree->ConstructWidget<T>(T::StaticClass(), Name);
    Widget->bIsVariable = true;
    return Widget;
}

UTextBlock* Label(UWidgetTree* Tree, FName Name, const FText& Text)
{
    UTextBlock* Result = Add<UTextBlock>(Tree, Name);
    Result->SetText(Text);
    Result->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    return Result;
}

UButton* Button(UWidgetTree* Tree, FName Name, const FText& Text)
{
    UButton* Result = Add<UButton>(Tree, Name);
    Result->AddChild(Label(Tree, *FString::Printf(TEXT("%sLabel"), *Name.ToString()), Text));
    return Result;
}

bool CompileAndSave(UWidgetBlueprint* Blueprint)
{
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint->GeneratedClass && Blueprint->Status != BS_Error && SaveAsset(Blueprint);
}

FItemDefinitionRow BagDefinition(FIntPoint Size)
{
    FItemDefinitionRow Row;
    Row.BagGridSize = Size;
    return Row;
}

bool MatchesDefinition(const FItemDefinitionRow* Row, const FItemDefinitionRow& Expected)
{
    return Row && FItemDefinitionRow::StaticStruct()->CompareScriptStruct(Row, &Expected, 0);
}

bool CheckOriginalDefinitions(UDataTable* Table)
{
    FItemDefinitionRow Rifle;
    Rifle.Size = FIntPoint(2, 1);
    FItemDefinitionRow Ammo;
    Ammo.bStackable = true; Ammo.MaxStack = 30;
    FItemDefinitionRow Medkit;
    Medkit.Size = FIntPoint(2, 2); Medkit.bStackable = true; Medkit.MaxStack = 5;
    return MatchesDefinition(Table->FindRow<FItemDefinitionRow>(TEXT("Rifle"), TEXT("BagAssets"), false), Rifle)
        && MatchesDefinition(Table->FindRow<FItemDefinitionRow>(TEXT("Ammo"), TEXT("BagAssets"), false), Ammo)
        && MatchesDefinition(Table->FindRow<FItemDefinitionRow>(TEXT("Medkit"), TEXT("BagAssets"), false), Medkit);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenerateInventoryAssets, "InventoryAssets.Create", Flags)
bool FGenerateInventoryAssets::RunTest(const FString& Parameters)
{
    for (const TCHAR* Path : Paths)
    {
        if (FPackageName::DoesPackageExist(Path))
        {
            AddError(FString::Printf(TEXT("기존 에셋을 덮어쓰지 않습니다: %s"), Path));
            return false;
        }
    }
    UPackage* TablePackage = CreatePackage(Paths[0]);
    UDataTable* Table = NewObject<UDataTable>(TablePackage, TEXT("DT_ItemDefinitions"), RF_Public | RF_Standalone);
    Table->RowStruct = FItemDefinitionRow::StaticStruct();
    FItemDefinitionRow Rifle;
    Rifle.Size = FIntPoint(2, 1);
    Table->AddRow(TEXT("Rifle"), Rifle);
    FItemDefinitionRow Ammo;
    Ammo.bStackable = true; Ammo.MaxStack = 30;
    Table->AddRow(TEXT("Ammo"), Ammo);
    FItemDefinitionRow Medkit;
    Medkit.Size = FIntPoint(2, 2); Medkit.bStackable = true; Medkit.MaxStack = 5;
    Table->AddRow(TEXT("Medkit"), Medkit);
    Table->AddRow(TEXT("SmallBag"), BagDefinition(FIntPoint(4, 4)));
    Table->AddRow(TEXT("LargeBag"), BagDefinition(FIntPoint(6, 4)));
    if (!SaveAsset(Table)) { AddError(TEXT("DataTable 저장 실패")); return false; }

    UWidgetBlueprint* ItemBP = CreateWidgetAsset(Paths[1], UInventoryItemWidget::StaticClass());
    if (!ItemBP) { AddError(TEXT("Item Widget Blueprint 생성 실패")); return false; }
    UBorder* ItemBorder = Add<UBorder>(ItemBP->WidgetTree, TEXT("ItemBorder"));
    ItemBorder->SetBrushColor(FLinearColor(0.18f, 0.35f, 0.50f, 0.95f));
    ItemBorder->SetPadding(FMargin(4.f));
    ItemBP->WidgetTree->RootWidget = ItemBorder;
    UTextBlock* ItemLabel = Label(ItemBP->WidgetTree, TEXT("ItemText"), FText::GetEmpty());
    FSlateFontInfo ItemFont = ItemLabel->GetFont();
    ItemFont.Size = 12;
    ItemLabel->SetFont(ItemFont);
    ItemBorder->AddChild(ItemLabel);
    if (!CompileAndSave(ItemBP)) { AddError(TEXT("Item Widget Blueprint 컴파일/저장 실패")); return false; }

    UWidgetBlueprint* GridBP = CreateWidgetAsset(Paths[2], UInventoryGridWidget::StaticClass());
    if (!GridBP) { AddError(TEXT("Grid Widget Blueprint 생성 실패")); return false; }
    UVerticalBox* GridRoot = Add<UVerticalBox>(GridBP->WidgetTree, TEXT("GridRoot"));
    GridBP->WidgetTree->RootWidget = GridRoot;
    GridRoot->AddChildToVerticalBox(Label(GridBP->WidgetTree, TEXT("HeaderText"), FText::GetEmpty()));
    USizeBox* GridSize = Add<USizeBox>(GridBP->WidgetTree, TEXT("GridSize"));
    GridSize->SetWidthOverride(312.f); GridSize->SetHeightOverride(208.f);
    GridSize->SetClipping(EWidgetClipping::ClipToBounds);
    GridRoot->AddChildToVerticalBox(GridSize);
    UBorder* GridBorder = Add<UBorder>(GridBP->WidgetTree, TEXT("GridBorder"));
    GridBorder->SetBrushColor(FLinearColor(0.08f, 0.13f, 0.19f, 0.98f));
    GridBorder->SetPadding(FMargin(0.f));
    GridSize->AddChild(GridBorder);
    UCanvasPanel* Cells = Add<UCanvasPanel>(GridBP->WidgetTree, TEXT("GridCanvas"));
    GridBorder->AddChild(Cells);
    for (int32 Y = 0; Y < 4; ++Y)
    {
        for (int32 X = 0; X < 6; ++X)
        {
            UBorder* Cell = Add<UBorder>(GridBP->WidgetTree, *FString::Printf(TEXT("Cell_%d_%d"), X, Y));
            Cell->SetBrushColor((X + Y) % 2 ? FLinearColor(0.11f, 0.17f, 0.23f) : FLinearColor(0.13f, 0.19f, 0.26f));
            UCanvasPanelSlot* CellSlot = Cells->AddChildToCanvas(Cell);
            CellSlot->SetPosition(FVector2D(X * 52.f, Y * 52.f));
            CellSlot->SetSize(FVector2D(50.f, 50.f));
        }
    }
    if (!CompileAndSave(GridBP)) { AddError(TEXT("Grid Widget Blueprint 컴파일/저장 실패")); return false; }

    UWidgetBlueprint* ScreenBP = CreateWidgetAsset(Paths[3], UInventoryScreenWidget::StaticClass());
    if (!ScreenBP) { AddError(TEXT("Screen Widget Blueprint 생성 실패")); return false; }
    UCanvasPanel* Root = Add<UCanvasPanel>(ScreenBP->WidgetTree, TEXT("Root"));
    ScreenBP->WidgetTree->RootWidget = Root;
    UBorder* Background = Add<UBorder>(ScreenBP->WidgetTree, TEXT("Background"));
    Background->SetBrushColor(FLinearColor(0.02f, 0.04f, 0.06f, 0.88f));
    UCanvasPanelSlot* BackgroundSlot = Root->AddChildToCanvas(Background);
    BackgroundSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackgroundSlot->SetOffsets(FMargin(0.f));
    UVerticalBox* Panel = Add<UVerticalBox>(ScreenBP->WidgetTree, TEXT("Panel"));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.5f));
    PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    PanelSlot->SetSize(FVector2D(700.f, 360.f));
    Panel->AddChildToVerticalBox(Label(ScreenBP->WidgetTree, TEXT("Title"), NSLOCTEXT("Inventory", "Title", "인벤토리  |  I / Esc 닫기")));
    UHorizontalBox* Columns = Add<UHorizontalBox>(ScreenBP->WidgetTree, TEXT("Columns"));
    Panel->AddChildToVerticalBox(Columns);
    UVerticalBox* Left = Add<UVerticalBox>(ScreenBP->WidgetTree, TEXT("LeftColumn"));
    UVerticalBox* Right = Add<UVerticalBox>(ScreenBP->WidgetTree, TEXT("RightColumn"));
    Columns->AddChildToHorizontalBox(Left)->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
    Columns->AddChildToHorizontalBox(Right);
    auto AddGrid = [&](UVerticalBox* Column, FName Name)
    {
        UInventoryGridWidget* Widget = ScreenBP->WidgetTree->ConstructWidget<UInventoryGridWidget>(
            TSubclassOf<UInventoryGridWidget>(GridBP->GeneratedClass.Get()), Name);
        Widget->bIsVariable = true;
        Column->AddChildToVerticalBox(Widget);
    };
    AddGrid(Left, TEXT("LeftGrid"));
    AddGrid(Right, TEXT("RightGrid"));
    Left->AddChildToVerticalBox(Button(ScreenBP->WidgetTree, TEXT("SortLeftButton"), NSLOCTEXT("Inventory", "SortStash", "보관함 정렬")));
    Right->AddChildToVerticalBox(Button(ScreenBP->WidgetTree, TEXT("SortRightButton"), NSLOCTEXT("Inventory", "SortBag", "가방 정렬")));
    Panel->AddChildToVerticalBox(Button(ScreenBP->WidgetTree, TEXT("CloseButton"), NSLOCTEXT("Inventory", "Close", "닫기")));
    if (!CompileAndSave(ScreenBP)) { AddError(TEXT("Screen Widget Blueprint 컴파일/저장 실패")); return false; }

    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World) { AddError(TEXT("데모 map 생성 실패")); return false; }
    World->GetWorldSettings()->DefaultGameMode = AInventoryDemoGameMode::StaticClass();
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, Paths[4])) { AddError(TEXT("데모 map 저장 실패")); return false; }
    return true;
}

// 명시적으로 실행하는 에셋 유지보수 경로이며 Duckov 회귀 검사는 파일을 저장하지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecompileInventoryTickPolicy, "InventoryAssets.RecompileTickPolicy", Flags)
bool FRecompileInventoryTickPolicy::RunTest(const FString& Parameters)
{
    for (int32 Index = 1; Index <= 3; ++Index)
    {
        UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, Paths[Index]);
        if (!TestNotNull(TEXT("Tick policy 대상 WBP"), Blueprint)) { return false; }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        const auto* Generated = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
        if (!TestTrue(TEXT("native Tick 불필요 컴파일 결과"), Generated && !Generated->ClassRequiresNativeTick())
            || !TestTrue(TEXT("WBP 컴파일 성공"), Blueprint->Status == BS_UpToDate)) { return false; }
        TestTrue(TEXT("Tick policy WBP 저장"), SaveAsset(Blueprint));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerifyInventoryAssets, "InventoryAssets.Verify", Flags)
bool FVerifyInventoryAssets::RunTest(const FString& Parameters)
{
    UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
    if (!TestNotNull(TEXT("DataTable"), Table)) { return false; }
    TestEqual(TEXT("DataTable row struct"), Table->RowStruct.Get(), FItemDefinitionRow::StaticStruct());
    TestEqual(TEXT("정의 행 수"), Table->GetRowNames().Num(), 5);
    TestTrue(TEXT("기존 세 정의의 모든 값 보존"), CheckOriginalDefinitions(Table));
    TestTrue(TEXT("소형 가방 1x1, non-stackable, capacity 4x4"), MatchesDefinition(
        Table->FindRow<FItemDefinitionRow>(TEXT("SmallBag"), TEXT("에셋 검증"), false), BagDefinition(FIntPoint(4, 4))));
    TestTrue(TEXT("대형 가방 1x1, non-stackable, capacity 6x4"), MatchesDefinition(
        Table->FindRow<FItemDefinitionRow>(TEXT("LargeBag"), TEXT("에셋 검증"), false), BagDefinition(FIntPoint(6, 4))));
    if (const FItemDefinitionRow* Rifle = Table->FindRow<FItemDefinitionRow>(TEXT("Rifle"), TEXT("에셋 검증")))
    {
        TestEqual(TEXT("Rifle footprint"), Rifle->Size, FIntPoint(2, 1));
    }
    else { AddError(TEXT("Rifle 정의 없음")); }
    if (const FItemDefinitionRow* Medkit = Table->FindRow<FItemDefinitionRow>(TEXT("Medkit"), TEXT("에셋 검증")))
    {
        TestEqual(TEXT("Medkit footprint"), Medkit->Size, FIntPoint(2, 2));
        TestEqual(TEXT("Medkit stack"), Medkit->MaxStack, 5);
    }
    else { AddError(TEXT("Medkit 정의 없음")); }
    UClass* Item = LoadClass<UInventoryItemWidget>(nullptr, TEXT("/Game/UI/WBP_InventoryItem.WBP_InventoryItem_C"));
    UClass* Grid = LoadClass<UInventoryGridWidget>(nullptr, TEXT("/Game/UI/WBP_InventoryGrid.WBP_InventoryGrid_C"));
    UClass* Screen = LoadClass<UInventoryScreenWidget>(nullptr, TEXT("/Game/UI/WBP_InventoryScreen.WBP_InventoryScreen_C"));
    TestNotNull(TEXT("Item Widget class"), Item);
    TestNotNull(TEXT("Grid Widget class"), Grid);
    TestNotNull(TEXT("Screen Widget class"), Screen);
    auto VerifyWidget = [this](const TCHAR* Path, std::initializer_list<TPair<FName, UClass*>> Bindings)
    {
        UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, Path);
        if (!TestNotNull(TEXT("저장된 Widget Blueprint"), Blueprint)) { return; }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        TestTrue(TEXT("Widget Blueprint compile status"), Blueprint->Status == BS_UpToDate);
        for (const auto& Binding : Bindings)
        {
            UWidget* Widget = Blueprint->WidgetTree->FindWidget(Binding.Key);
            TestTrue(*FString::Printf(TEXT("BindWidget %s"), *Binding.Key.ToString()),
                Widget && Widget->IsA(Binding.Value) && Widget->bIsVariable);
        }
    };
    VerifyWidget(TEXT("/Game/UI/WBP_InventoryItem.WBP_InventoryItem"), {{TEXT("ItemText"), UTextBlock::StaticClass()}});
    VerifyWidget(TEXT("/Game/UI/WBP_InventoryGrid.WBP_InventoryGrid"),
        {{TEXT("HeaderText"), UTextBlock::StaticClass()}, {TEXT("GridSize"), USizeBox::StaticClass()}, {TEXT("GridCanvas"), UCanvasPanel::StaticClass()}});
    VerifyWidget(TEXT("/Game/UI/WBP_InventoryScreen.WBP_InventoryScreen"),
        {{TEXT("LeftGrid"), UInventoryGridWidget::StaticClass()}, {TEXT("RightGrid"), UInventoryGridWidget::StaticClass()},
        {TEXT("CloseButton"), UButton::StaticClass()}, {TEXT("SortLeftButton"), UButton::StaticClass()},
        {TEXT("SortRightButton"), UButton::StaticClass()}});
    UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/L_InventoryDemo.L_InventoryDemo"));
    if (TestNotNull(TEXT("데모 map"), World))
    {
        TestEqual(TEXT("map GameMode override"), World->GetWorldSettings()->DefaultGameMode.Get(), AInventoryDemoGameMode::StaticClass());
    }
    return !HasAnyErrors();
}

// main이 원본 DT를 backup한 뒤 명시적으로 실행한다. Duckov.* 회귀 선택에서는 저장하지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAddBagDefinitions, "InventoryBagAssets.AddDefinitions", Flags)
bool FAddBagDefinitions::RunTest(const FString& Parameters)
{
    UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
    if (!TestNotNull(TEXT("기존 DataTable"), Table)
        || !TestTrue(TEXT("정의 struct"), Table->GetRowStruct() == FItemDefinitionRow::StaticStruct())) { return false; }
    if (!TestTrue(TEXT("기존 세 정의와 capacity zero 보존"), CheckOriginalDefinitions(Table))) { return false; }
    const TPair<FName, FIntPoint> Bags[] = {{TEXT("SmallBag"), FIntPoint(4, 4)}, {TEXT("LargeBag"), FIntPoint(6, 4)}};
    int32 Existing = 3;
    for (const auto& Bag : Bags)
    {
        if (const auto* Row = Table->FindRow<FItemDefinitionRow>(Bag.Key, TEXT("BagAssets"), false))
        {
            ++Existing;
            if (!TestTrue(TEXT("동명 가방 정의 충돌 없음"), MatchesDefinition(Row, BagDefinition(Bag.Value)))) { return false; }
        }
    }
    if (!TestEqual(TEXT("예상 외 정의 행 없음"), Table->GetRowNames().Num(), Existing)) { return false; }
    bool bAdded = false;
    for (const auto& Bag : Bags)
    {
        if (!Table->FindRow<FItemDefinitionRow>(Bag.Key, TEXT("BagAssets"), false))
        {
            Table->AddRow(Bag.Key, BagDefinition(Bag.Value));
            bAdded = true;
        }
    }
    if (bAdded) { TestTrue(TEXT("두 가방 정의 DT 저장"), SaveAsset(Table)); }
    TestTrue(TEXT("추가 후 원래 세 정의 보존"), CheckOriginalDefinitions(Table));
    TestEqual(TEXT("추가 후 정의 행 수"), Table->GetRowNames().Num(), 5);
    return !HasAnyErrors();
}
}
#endif
