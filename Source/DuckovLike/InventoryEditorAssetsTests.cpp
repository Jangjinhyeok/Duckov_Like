#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "InventoryDemoGameMode.h"
#include "InventoryGridWidget.h"
#include "InventoryItemWidget.h"
#include "InventoryScreenWidget.h"
#include "ItemDefinitionRow.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
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

FItemDefinitionRow ArmorDefinition()
{
    FItemDefinitionRow Row;
    Row.Size = FIntPoint(2, 2);
    Row.EquipmentType = EItemEquipmentType::Armor;
    return Row;
}

bool MatchesDefinition(const FItemDefinitionRow* Row, const FItemDefinitionRow& Expected)
{
    return Row && FItemDefinitionRow::StaticStruct()->CompareScriptStruct(Row, &Expected, 0);
}

// 원작 인벤토리의 밝은 하늘색 둥근 버튼. 글자는 어둡게 쓴다.
UButton* PanelButton(UWidgetTree* Tree, FName Name, const FText& Text)
{
    UButton* Result = Add<UButton>(Tree, Name);
    const FLinearColor Color(0.62f, 0.85f, 0.97f);
    FButtonStyle Style = Result->GetStyle();
    Style.SetNormal(FSlateRoundedBoxBrush(Color, 6.f));
    Style.SetHovered(FSlateRoundedBoxBrush(Color * 1.12f, 6.f, FLinearColor::White, 2.f));
    Style.SetPressed(FSlateRoundedBoxBrush(Color * 0.8f, 6.f));
    Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.3f, 0.36f, 0.42f, 0.8f), 6.f));
    Style.SetNormalPadding(FMargin(12.f, 4.f));
    Style.SetPressedPadding(FMargin(12.f, 5.f, 12.f, 3.f));
    Result->SetStyle(Style);
    UTextBlock* Caption = Label(Tree, *FString::Printf(TEXT("%sLabel"), *Name.ToString()), Text);
    Caption->SetColorAndOpacity(FSlateColor(FLinearColor(0.06f, 0.1f, 0.16f)));
    FSlateFontInfo Font = Caption->GetFont();
    Font.Size = 14;
    Caption->SetFont(Font);
    Result->AddChild(Caption);
    return Result;
}

// keyboard·gamepad 이동을 배치와 무관하게 정해 둔다. 대상은 이름으로 저장되어 화면 인스턴스에서 찾아진다.
void Navigate(UWidget* From, std::initializer_list<TPair<EUINavigation, FName>> Rules)
{
    if (!From->Navigation) { From->Navigation = NewObject<UWidgetNavigation>(From); }
    for (const TPair<EUINavigation, FName>& Rule : Rules)
    {
        FWidgetNavigationData& Data = Rule.Key == EUINavigation::Up ? From->Navigation->Up
            : Rule.Key == EUINavigation::Down ? From->Navigation->Down
            : Rule.Key == EUINavigation::Left ? From->Navigation->Left : From->Navigation->Right;
        Data.Rule = EUINavigationRule::Explicit;
        Data.WidgetToFocus = Rule.Value;
    }
}

// 원작의 반투명 남색 둥근 패널.
UBorder* Panel(UWidgetTree* Tree, FName Name)
{
    UBorder* Result = Add<UBorder>(Tree, Name);
    Result->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.05f, 0.09f, 0.15f, 0.9f), 10.f, FLinearColor(0.25f, 0.35f, 0.45f, 0.6f), 1.f));
    Result->SetPadding(FMargin(16.f));
    return Result;
}

// 그리드 헤더("가방 (8/16)") 줄의 오른쪽 끝에 버튼을 겹쳐 둔다. 원작처럼 제목과 버튼이 한 줄에 놓인다.
USizeBox* GridSection(UWidgetTree* Tree, FName Name, UWidget* Grid, std::initializer_list<UButton*> Buttons, float MinWidth)
{
    USizeBox* Size = Add<USizeBox>(Tree, *FString::Printf(TEXT("%sSize"), *Name.ToString()));
    Size->SetMinDesiredWidth(MinWidth);
    UOverlay* Section = Add<UOverlay>(Tree, Name);
    Size->AddChild(Section);
    UOverlaySlot* GridSlot = Section->AddChildToOverlay(Grid);
    GridSlot->SetHorizontalAlignment(HAlign_Left);
    GridSlot->SetVerticalAlignment(VAlign_Top);
    UHorizontalBox* Actions = Add<UHorizontalBox>(Tree, *FString::Printf(TEXT("%sActions"), *Name.ToString()));
    for (UButton* Button : Buttons) { Actions->AddChildToHorizontalBox(Button)->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }
    UOverlaySlot* ActionSlot = Section->AddChildToOverlay(Actions);
    ActionSlot->SetHorizontalAlignment(HAlign_Right);
    ActionSlot->SetVerticalAlignment(VAlign_Top);
    return Size;
}

// Item·Grid·Screen WBP를 원작 배치로 만든다. 새로 만들 때와 레이아웃을 다시 만들 때 같은 함수를 쓴다.
bool CreateInventoryWidgets()
{
    // 아이템 칸: 어두운 둥근 칸, 얇은 밝은 테두리, 수량·이름은 오른쪽 아래.
    UWidgetBlueprint* ItemBP = CreateWidgetAsset(Paths[1], UInventoryItemWidget::StaticClass());
    if (!ItemBP) { return false; }
    UBorder* ItemBorder = Add<UBorder>(ItemBP->WidgetTree, TEXT("ItemBorder"));
    ItemBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.18f, 0.26f, 0.96f), 6.f, FLinearColor(0.6f, 0.72f, 0.84f, 0.7f), 1.f));
    ItemBorder->SetPadding(FMargin(5.f));
    ItemBorder->SetHorizontalAlignment(HAlign_Right);
    ItemBorder->SetVerticalAlignment(VAlign_Bottom);
    ItemBP->WidgetTree->RootWidget = ItemBorder;
    UTextBlock* ItemLabel = Label(ItemBP->WidgetTree, TEXT("ItemText"), FText::GetEmpty());
    FSlateFontInfo ItemFont = ItemLabel->GetFont();
    ItemFont.Size = 11;
    ItemLabel->SetFont(ItemFont);
    ItemLabel->SetJustification(ETextJustify::Right);
    ItemLabel->SetShadowOffset(FVector2D(1.f, 1.f));
    ItemLabel->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
    ItemBorder->AddChild(ItemLabel);
    if (!CompileAndSave(ItemBP)) { return false; }

    UWidgetBlueprint* GridBP = CreateWidgetAsset(Paths[2], UInventoryGridWidget::StaticClass());
    if (!GridBP) { return false; }
    UVerticalBox* GridRoot = Add<UVerticalBox>(GridBP->WidgetTree, TEXT("GridRoot"));
    GridBP->WidgetTree->RootWidget = GridRoot;
    UTextBlock* Header = Label(GridBP->WidgetTree, TEXT("HeaderText"), FText::GetEmpty());
    FSlateFontInfo HeaderFont = Header->GetFont();
    HeaderFont.Size = 18;
    Header->SetFont(HeaderFont);
    // 헤더 줄에 겹쳐 놓을 버튼 높이만큼 아래 여백을 둔다.
    GridRoot->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 2.f, 0.f, 10.f));
    USizeBox* GridSize = Add<USizeBox>(GridBP->WidgetTree, TEXT("GridSize"));
    GridSize->SetWidthOverride(312.f); GridSize->SetHeightOverride(208.f);
    GridSize->SetClipping(EWidgetClipping::ClipToBounds);
    GridRoot->AddChildToVerticalBox(GridSize);
    UBorder* GridBorder = Add<UBorder>(GridBP->WidgetTree, TEXT("GridBorder"));
    GridBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
    GridBorder->SetPadding(FMargin(0.f));
    GridSize->AddChild(GridBorder);
    UCanvasPanel* Cells = Add<UCanvasPanel>(GridBP->WidgetTree, TEXT("GridCanvas"));
    GridBorder->AddChild(Cells);
    for (int32 Y = 0; Y < 4; ++Y)
    {
        for (int32 X = 0; X < 6; ++X)
        {
            // 빈 칸도 원작처럼 둥근 테두리 칸으로 보인다.
            UBorder* Cell = Add<UBorder>(GridBP->WidgetTree, *FString::Printf(TEXT("Cell_%d_%d"), X, Y));
            Cell->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.07f, 0.11f, 0.17f, 0.85f), 6.f, FLinearColor(0.3f, 0.4f, 0.5f, 0.45f), 1.f));
            UCanvasPanelSlot* CellSlot = Cells->AddChildToCanvas(Cell);
            CellSlot->SetPosition(FVector2D(X * 52.f, Y * 52.f));
            CellSlot->SetSize(FVector2D(50.f, 50.f));
        }
    }
    if (!CompileAndSave(GridBP)) { return false; }

    UWidgetBlueprint* ScreenBP = CreateWidgetAsset(Paths[3], UInventoryScreenWidget::StaticClass());
    if (!ScreenBP) { return false; }
    UWidgetTree* Tree = ScreenBP->WidgetTree;
    UCanvasPanel* Root = Add<UCanvasPanel>(Tree, TEXT("Root"));
    Tree->RootWidget = Root;
    // 원작은 인벤토리를 열어도 장면이 보인다. 패널 대비만 남길 만큼 옅게 어둡게 한다.
    UBorder* Background = Add<UBorder>(Tree, TEXT("Background"));
    Background->SetBrushColor(FLinearColor(0.01f, 0.02f, 0.04f, 0.45f));
    UCanvasPanelSlot* BackgroundSlot = Root->AddChildToCanvas(Background);
    BackgroundSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackgroundSlot->SetOffsets(FMargin(0.f));
    auto Place = [Root](UWidget* Widget, FAnchors Anchors, FVector2D Alignment, FVector2D Position)
    {
        UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(Widget);
        CanvasSlot->SetAnchors(Anchors);
        CanvasSlot->SetAlignment(Alignment);
        CanvasSlot->SetPosition(Position);
        CanvasSlot->SetAutoSize(true);
    };
    // 위 가운데: 원작의 화면 탭 자리. 이 게임은 화면이 하나라 제목만 둔다.
    UTextBlock* Title = Label(Tree, TEXT("Title"), NSLOCTEXT("Inventory", "Title", "인벤토리"));
    FSlateFontInfo TitleFont = Title->GetFont();
    TitleFont.Size = 22;
    Title->SetFont(TitleFont);
    Place(Title, FAnchors(0.5f, 0.f), FVector2D(0.5f, 0.f), FVector2D(0.f, 24.f));
    // 아래 가운데: 원작의 퀵슬롯 자리. 이 게임은 퀵슬롯이 없어 닫기를 둔다. 열 때 처음 focus를 받는다.
    UButton* CloseButton = PanelButton(Tree, TEXT("CloseButton"), NSLOCTEXT("Inventory", "Close", "닫기  I / Esc"));
    Place(CloseButton, FAnchors(0.5f, 1.f), FVector2D(0.5f, 1.f), FVector2D(0.f, -32.f));
    auto MakeGrid = [Tree, GridBP](FName Name)
    {
        UInventoryGridWidget* Widget = Tree->ConstructWidget<UInventoryGridWidget>(TSubclassOf<UInventoryGridWidget>(GridBP->GeneratedClass.Get()), Name);
        Widget->bIsVariable = true;
        return Widget;
    };
    // 왼쪽: 캐릭터 패널 — 장비(가방 슬롯) 아래에 가방 격자. 헤더 줄 오른쪽에 전부 창고에 보관·정리.
    UBorder* CharacterPanel = Panel(Tree, TEXT("CharacterPanel"));
    UVerticalBox* CharacterColumn = Add<UVerticalBox>(Tree, TEXT("CharacterColumn"));
    CharacterPanel->SetContent(CharacterColumn);
    CharacterColumn->AddChildToVerticalBox(Label(Tree, TEXT("EquipmentTitle"), NSLOCTEXT("Inventory", "Equipment", "장비")));
    UVerticalBox* EquipmentSlot = Add<UVerticalBox>(Tree, TEXT("EquipmentSlot"));
    CharacterColumn->AddChildToVerticalBox(EquipmentSlot)->SetPadding(FMargin(0.f, 6.f, 0.f, 14.f));
    UButton* StoreAllButton = PanelButton(Tree, TEXT("StoreAllButton"), NSLOCTEXT("Inventory", "StoreAll", "전부 창고에 보관"));
    UButton* SortBagButton = PanelButton(Tree, TEXT("SortRightButton"), NSLOCTEXT("Inventory", "SortBag", "정리"));
    CharacterColumn->AddChildToVerticalBox(GridSection(Tree, TEXT("BagSection"), MakeGrid(TEXT("RightGrid")),
        {StoreAllButton, SortBagButton}, 460.f));
    Place(CharacterPanel, FAnchors(0.f, 0.5f), FVector2D(0.f, 0.5f), FVector2D(48.f, 0.f));
    // 오른쪽: 창고 패널. 레이드에서는 화면이 통째로 감춘다.
    UBorder* StashPanel = Panel(Tree, TEXT("StashPanel"));
    UButton* SortStashButton = PanelButton(Tree, TEXT("SortLeftButton"), NSLOCTEXT("Inventory", "SortStash", "정리"));
    StashPanel->SetContent(GridSection(Tree, TEXT("StashSection"), MakeGrid(TEXT("LeftGrid")), {SortStashButton}, 400.f));
    Place(StashPanel, FAnchors(1.f, 0.5f), FVector2D(1.f, 0.5f), FVector2D(-48.f, 0.f));
    // 버튼들은 패널 머리줄에 한 줄로 있고 닫기는 아래 가운데다. 그 배치대로 방향 키 이동을 고정한다.
    Navigate(CloseButton, {{EUINavigation::Up, TEXT("SortRightButton")}});
    Navigate(StoreAllButton, {{EUINavigation::Right, TEXT("SortRightButton")}, {EUINavigation::Down, TEXT("CloseButton")}});
    Navigate(SortBagButton, {{EUINavigation::Left, TEXT("StoreAllButton")}, {EUINavigation::Right, TEXT("SortLeftButton")},
        {EUINavigation::Down, TEXT("CloseButton")}});
    Navigate(SortStashButton, {{EUINavigation::Left, TEXT("SortRightButton")}, {EUINavigation::Down, TEXT("CloseButton")}});
    return CompileAndSave(ScreenBP);
}

// 장비 slot 이전의 DT는 Rifle이 장비 종류 None이다. 그 외 값은 처음 세 정의 그대로여야 한다.
bool CheckOriginalDefinitions(UDataTable* Table, EItemEquipmentType RifleType)
{
    FItemDefinitionRow Rifle;
    Rifle.Size = FIntPoint(2, 1);
    Rifle.EquipmentType = RifleType;
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
    Rifle.EquipmentType = EItemEquipmentType::Weapon;
    Table->AddRow(TEXT("Rifle"), Rifle);
    FItemDefinitionRow Ammo;
    Ammo.bStackable = true; Ammo.MaxStack = 30;
    Table->AddRow(TEXT("Ammo"), Ammo);
    FItemDefinitionRow Medkit;
    Medkit.Size = FIntPoint(2, 2); Medkit.bStackable = true; Medkit.MaxStack = 5;
    Table->AddRow(TEXT("Medkit"), Medkit);
    Table->AddRow(TEXT("SmallBag"), BagDefinition(FIntPoint(4, 4)));
    Table->AddRow(TEXT("LargeBag"), BagDefinition(FIntPoint(6, 4)));
    Table->AddRow(TEXT("Armor"), ArmorDefinition());
    if (!SaveAsset(Table)) { AddError(TEXT("DataTable 저장 실패")); return false; }

    if (!CreateInventoryWidgets()) { AddError(TEXT("인벤토리 Widget Blueprint 생성/컴파일/저장 실패")); return false; }

    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World) { AddError(TEXT("데모 map 생성 실패")); return false; }
    World->GetWorldSettings()->DefaultGameMode = AInventoryDemoGameMode::StaticClass();
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, Paths[4])) { AddError(TEXT("데모 map 저장 실패")); return false; }
    return true;
}

// 명시적으로 실행하는 레이아웃 재생성 경로다. Item·Grid·Screen WBP 세 개를 지운 뒤에만 실행되며 덮어쓰지 않는다.
// DataTable과 데모 map은 건드리지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRebuildInventoryWidgets, "InventoryAssets.RebuildWidgets", Flags)
bool FRebuildInventoryWidgets::RunTest(const FString& Parameters)
{
    for (int32 Index = 1; Index <= 3; ++Index)
    {
        if (FPackageName::DoesPackageExist(Paths[Index]))
        {
            AddError(FString::Printf(TEXT("기존 에셋을 덮어쓰지 않습니다. 먼저 지우세요: %s"), Paths[Index]));
            return false;
        }
    }
    if (!CreateInventoryWidgets()) { AddError(TEXT("인벤토리 Widget Blueprint 생성/컴파일/저장 실패")); return false; }
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
    TestEqual(TEXT("정의 행 수"), Table->GetRowNames().Num(), 6);
    TestTrue(TEXT("기존 세 정의의 값 보존, Rifle만 무기"), CheckOriginalDefinitions(Table, EItemEquipmentType::Weapon));
    TestTrue(TEXT("방어구 2x2, non-stackable, Armor"), MatchesDefinition(
        Table->FindRow<FItemDefinitionRow>(TEXT("Armor"), TEXT("에셋 검증"), false), ArmorDefinition()));
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
        {TEXT("SortRightButton"), UButton::StaticClass()}, {TEXT("StoreAllButton"), UButton::StaticClass()},
        {TEXT("StashPanel"), UBorder::StaticClass()}, {TEXT("EquipmentSlot"), UVerticalBox::StaticClass()}});
    UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/L_InventoryDemo.L_InventoryDemo"));
    if (TestNotNull(TEXT("데모 map"), World))
    {
        TestEqual(TEXT("map GameMode override"), World->GetWorldSettings()->DefaultGameMode.Get(), AInventoryDemoGameMode::StaticClass());
    }
    return !HasAnyErrors();
}

// main이 원본 DT를 backup한 뒤 명시적으로 실행한다. Duckov.* 회귀 선택에서는 저장하지 않는다.
// 장비 slot 이전 DT 전용이다. 이후 DT는 InventoryEquipmentAssets.AddDefinitions가 이어받았고, 이 명령은 저장 없이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAddBagDefinitions, "InventoryBagAssets.AddDefinitions", Flags)
bool FAddBagDefinitions::RunTest(const FString& Parameters)
{
    UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
    if (!TestNotNull(TEXT("기존 DataTable"), Table)
        || !TestTrue(TEXT("정의 struct"), Table->GetRowStruct() == FItemDefinitionRow::StaticStruct())) { return false; }
    if (!TestTrue(TEXT("기존 세 정의와 capacity zero 보존"), CheckOriginalDefinitions(Table, EItemEquipmentType::None))) { return false; }
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
    TestTrue(TEXT("추가 후 원래 세 정의 보존"), CheckOriginalDefinitions(Table, EItemEquipmentType::None));
    TestEqual(TEXT("추가 후 정의 행 수"), Table->GetRowNames().Num(), 5);
    return !HasAnyErrors();
}

// main이 원본 DT를 backup한 뒤 명시적으로 실행한다. Rifle을 무기로 표시하고 방어구 정의를 더한다. 다시 실행해도 같은 결과다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAddEquipmentDefinitions, "InventoryEquipmentAssets.AddDefinitions", Flags)
bool FAddEquipmentDefinitions::RunTest(const FString& Parameters)
{
    UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Inventory/DT_ItemDefinitions.DT_ItemDefinitions"));
    if (!TestNotNull(TEXT("기존 DataTable"), Table)
        || !TestTrue(TEXT("정의 struct"), Table->GetRowStruct() == FItemDefinitionRow::StaticStruct())) { return false; }
    const bool bMigrated = CheckOriginalDefinitions(Table, EItemEquipmentType::Weapon);
    if (!TestTrue(TEXT("기존 세 정의 보존"), bMigrated || CheckOriginalDefinitions(Table, EItemEquipmentType::None))
        || !TestTrue(TEXT("두 가방 정의 보존"),
            MatchesDefinition(Table->FindRow<FItemDefinitionRow>(TEXT("SmallBag"), TEXT("EquipmentAssets"), false), BagDefinition(FIntPoint(4, 4)))
            && MatchesDefinition(Table->FindRow<FItemDefinitionRow>(TEXT("LargeBag"), TEXT("EquipmentAssets"), false), BagDefinition(FIntPoint(6, 4)))))
    {
        return false;
    }
    const FItemDefinitionRow* Armor = Table->FindRow<FItemDefinitionRow>(TEXT("Armor"), TEXT("EquipmentAssets"), false);
    if ((Armor && !TestTrue(TEXT("동명 방어구 정의 충돌 없음"), MatchesDefinition(Armor, ArmorDefinition())))
        || !TestEqual(TEXT("예상 외 정의 행 없음"), Table->GetRowNames().Num(), Armor ? 6 : 5)) { return false; }
    if (!bMigrated || !Armor)
    {
        Table->FindRow<FItemDefinitionRow>(TEXT("Rifle"), TEXT("EquipmentAssets"))->EquipmentType = EItemEquipmentType::Weapon;
        if (!Armor) { Table->AddRow(TEXT("Armor"), ArmorDefinition()); }
        TestTrue(TEXT("장비 정의 DT 저장"), SaveAsset(Table));
    }
    TestTrue(TEXT("추가 후 Rifle만 무기로 바뀜"), CheckOriginalDefinitions(Table, EItemEquipmentType::Weapon));
    TestEqual(TEXT("추가 후 정의 행 수"), Table->GetRowNames().Num(), 6);
    return !HasAnyErrors();
}
}
#endif
