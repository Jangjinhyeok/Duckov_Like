#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationAppearanceComponent.h"
#include "CustomizationCatalog.h"
#include "CustomizationModel.h"
#include "CustomizationPreviewActor.h"
#include "CustomizationScreenWidget.h"
#include "CustomizationViewModel.h"
#include "InventoryDemoPlayerController.h"
#include "MenuTransitionTestSupport.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace CustomizationSlotWidgetTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
template<class T> T* Field(UObject* Owner, FName Name)
{
    auto* Property = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Name) : nullptr;
    return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
}
class FWait final : public IAutomationLatentCommand
{
public:
    FWait(FAutomationTestBase& InTest, TFunction<bool()> InCondition) : Test(InTest), Condition(MoveTemp(InCondition)) {}
    bool Update() override
    {
        if (Condition()) { return true; }
        if (!Start) { Start = FPlatformTime::Seconds(); }
        if (FPlatformTime::Seconds() - Start < 15.0) { return false; }
        Test.AddError(TEXT("슬롯 선택 PIE 준비/종료 시간 초과"));
        return true;
    }
private:
    FAutomationTestBase& Test;
    TFunction<bool()> Condition;
    double Start = 0.0;
};
void SavePNG(FAutomationTestBase& Test, const TCHAR* Name, const TArray<FColor>& Pixels, FIntPoint Size)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/CustomizationCatalog/Visual");
    IFileManager::Get().MakeDirectory(*Directory, true);
    TArray64<uint8> PNG;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PNG);
    Test.TestTrue(TEXT("최신 PNG 저장"), !PNG.IsEmpty() && FFileHelper::SaveArrayToFile(PNG, *(Directory / (FString(Name) + TEXT(".png")))));
}

void CaptureWidget(FAutomationTestBase& Test, UCustomizationScreenWidget* Screen, const TCHAR* Name)
{
    TArray<FColor> Pixels;
    FIntVector Size;
    if (Test.TestTrue(TEXT("현재 native 화면 캡처"), FSlateApplication::Get().TakeScreenshot(Screen->TakeWidget(), Pixels, Size)))
    {
        SavePNG(Test, Name, Pixels, FIntPoint(Size.X, Size.Y));
    }
}

struct FScene
{
    TWeakObjectPtr<AInventoryDemoPlayerController> Controller;
    TWeakObjectPtr<UCustomizationScreenWidget> Screen;
    TWeakObjectPtr<UCustomizationModel> Model;
    TArray<FColor> Pixels[6];
    bool Ready = false;
};

UButton* Find(UCustomizationScreenWidget* Screen, const TCHAR* Name) { return Screen ? Cast<UButton>(Screen->GetWidgetFromName(Name)) : nullptr; }
void Click(UCustomizationScreenWidget* Screen, const TCHAR* Name) { if (UButton* Button = Find(Screen, Name)) { Button->OnClicked.Broadcast(); } }
void Read(FAutomationTestBase& Test, FScene& Scene, int32 Index, const TCHAR* Name)
{
    auto* Target = Scene.Screen->GetPreview()->GetRenderTarget();
    Test.TestTrue(TEXT("preview pixels 읽기"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Scene.Pixels[Index]));
    SavePNG(Test, Name, Scene.Pixels[Index], FIntPoint(Target->SizeX, Target->SizeY));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlotSelectionPIE, "Duckov.Customization.UI.SlotSelection", Flags)
bool FSlotSelectionPIE::RunTest(const FString& Parameters)
{
    auto Scene = MakeShared<FScene>();
    if (!TestTrue(TEXT("기존 데모 map PIE"), AutomationOpenMap(TEXT("/Game/Maps/L_InventoryDemo")))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FWait(*this, []
    {
        return GEditor && GEditor->PlayWorld && Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        auto* Controller = Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
        if (!Controller) { return; }
        Scene->Controller = Controller;
        TestTrue(TEXT("Menu stack 즉시 전환"), DisableMenuTransitionForTest(Controller));
        Controller->OpenCustomizationPrototype();
        Scene->Screen = Field<UCustomizationScreenWidget>(Controller, TEXT("CustomizationScreen"));
        Scene->Model = Field<UCustomizationModel>(Controller, TEXT("CustomizationModel"));
        if (!TestTrue(TEXT("화면·편집·preview"), Scene->Screen.IsValid() && Scene->Screen->IsActivated()
            && Scene->Model.IsValid() && Scene->Model->IsEditing() && Scene->Screen->GetPreview())) { return; }
        Scene->Ready = true;
        auto* Screen = Scene->Screen.Get();
        // 카탈로그의 모든 파츠에 선택 버튼이 있다. 코드에 파츠 이름이 없어도 row가 늘면 버튼이 늘어난다.
        const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
        for (const ECustomizationSlot Slot : GetCustomizationSlots())
        {
            for (const FName PartId : Catalog.GetPartIds(Slot))
            {
                TestNotNull(*FString::Printf(TEXT("%s 선택 버튼"), *PartId.ToString()), Find(Screen, *(TEXT("Part") + PartId.ToString())));
            }
            const FString Name = StaticEnum<ECustomizationSlot>()->GetNameStringByValue(static_cast<int64>(Slot));
            TestEqual(*FString::Printf(TEXT("%s: 헤어만 '없음'이 있다"), *Name),
                Find(Screen, *(TEXT("Part") + Name + TEXT("None"))) != nullptr, !IsCustomizationSlotRequired(Slot));
            TestEqual(*FString::Printf(TEXT("%s: 발 외에는 색상 칸이 있다"), *Name),
                Find(Screen, *(Name + TEXT("Color"))) != nullptr, IsCustomizationSlotColorable(Slot));
        }
        TestNotNull(TEXT("머리 탭 피부색 칸"), Find(Screen, TEXT("SkinColor")));
        TestTrue(TEXT("팔레트 swatch 16개"), Find(Screen, TEXT("SkinSwatch15")) && !Find(Screen, TEXT("SkinSwatch16")));
        TestTrue(TEXT("팔레트는 처음에 접혀 있다"), !Screen->GetWidgetFromName(TEXT("SkinPalette"))->IsVisible());
        TestTrue(TEXT("선택된 헤어 버튼과 아닌 버튼의 색이 다르다"),
            Find(Screen, TEXT("PartHairA"))->GetBackgroundColor() != Find(Screen, TEXT("PartHairB"))->GetBackgroundColor());
        TestEqual(TEXT("처음 헤어는 기본값"), Screen->GetViewModel()->GetSlotPartId(ECustomizationSlot::Hair), FName(TEXT("HairA")));
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 0, TEXT("Slots0-Default"));
        Click(Scene->Screen.Get(), TEXT("PartEyeLarge"));
        auto* VM = Scene->Screen->GetViewModel();
        TestEqual(TEXT("눈 유형 선택 반영"), VM->GetSlotPartId(ECustomizationSlot::Eye), FName(TEXT("EyeLarge")));
        TestTrue(TEXT("확정 값은 그대로"), Scene->Model->GetProfile() == FCustomizationProfile());
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 1, TEXT("Slots1-EyeLarge"));
        TestTrue(TEXT("눈 유형을 바꾸면 미리보기가 바뀐다"), Scene->Pixels[1] != Scene->Pixels[0]);
        Click(Scene->Screen.Get(), TEXT("PartHairNone"));
        TestTrue(TEXT("헤어 해제"), Scene->Screen->GetViewModel()->GetSlotPartId(ECustomizationSlot::Hair).IsNone()
            && !Scene->Screen->GetPreview()->GetAppearance()->GetDisplayedSlotMesh(ECustomizationSlot::Hair));
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 2, TEXT("Slots2-NoHair"));
        TestTrue(TEXT("헤어를 비우면 또 바뀐다"), Scene->Pixels[2] != Scene->Pixels[1]);
        Click(Scene->Screen.Get(), TEXT("PartEyeRound"));
        Click(Scene->Screen.Get(), TEXT("PartHairA"));
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 3, TEXT("Slots3-DefaultAgain"));
        TestTrue(TEXT("기본 유형으로 돌아오면 처음 모습과 픽셀이 같다(잔류 없음)"), Scene->Pixels[3] == Scene->Pixels[0]);
        // 원작처럼 색상 칸을 누르면 팔레트가 펼쳐지고, 색을 고르면 접힌다.
        auto* Screen = Scene->Screen.Get();
        Click(Screen, TEXT("SkinColor"));
        TestTrue(TEXT("색상 칸은 팔레트를 펼친다"), Screen->GetWidgetFromName(TEXT("SkinPalette"))->IsVisible());
        CaptureWidget(*this, Screen, TEXT("PaletteOpen"));
        Click(Screen, TEXT("SkinSwatch10"));
        TestEqual(TEXT("피부색 선택 반영"), Screen->GetViewModel()->GetSkinColor(), 10);
        TestTrue(TEXT("색을 고르면 팔레트가 접힌다"), !Screen->GetWidgetFromName(TEXT("SkinPalette"))->IsVisible());
        auto* Mark = Cast<UTextBlock>(Find(Screen, TEXT("SkinSwatch10"))->GetChildAt(0));
        auto* Other = Cast<UTextBlock>(Find(Screen, TEXT("SkinSwatch0"))->GetChildAt(0));
        TestTrue(TEXT("고른 swatch에만 선택 표시"), Mark && Other && !Mark->GetText().IsEmpty() && Other->GetText().IsEmpty());
        TestTrue(TEXT("색상 칸은 고른 색으로 칠해진다"), Find(Screen, TEXT("SkinColor"))->GetBackgroundColor() == GetCustomizationColor(10));
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 4, TEXT("Slots4-Skin10"));
        TestTrue(TEXT("피부색을 바꾸면 미리보기가 바뀐다"), Scene->Pixels[4] != Scene->Pixels[0]);
        // 펼친 팔레트는 탭을 바꾸면 접힌다.
        Click(Scene->Screen.Get(), TEXT("SkinColor"));
        Click(Scene->Screen.Get(), TEXT("EyeTab"));
        TestTrue(TEXT("탭을 바꾸면 펼친 팔레트가 접힌다"), !Scene->Screen->GetWidgetFromName(TEXT("SkinPalette"))->IsVisible());
        Click(Scene->Screen.Get(), TEXT("HeadTab"));
        Click(Scene->Screen.Get(), TEXT("SkinSwatch0"));
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 5, TEXT("Slots5-SkinBack"));
        TestTrue(TEXT("기본 피부색으로 돌아오면 처음 모습과 픽셀이 같다"), Scene->Pixels[5] == Scene->Pixels[0]);
        // 슬롯 하나가 실패하면 전체가 바뀌지 않고 실패 슬롯이 화면에 나온다.
        auto* VM = Scene->Screen->GetViewModel();
        auto* Appearance = Scene->Screen->GetPreview()->GetAppearance();
        auto Broken = UCustomizationAppearanceComponent::GetDefaultResources(TEXT("BeakLong"));
        Broken.Mesh.Reset();
        Appearance->SetResourcesForTests(TEXT("BeakLong"), Broken);
        const FCustomizationProfile Draft = Scene->Model->GetDraft();
        const UMeshComponent* Beak = Appearance->GetDisplayedSlotMesh(ECustomizationSlot::Beak);
        Click(Scene->Screen.Get(), TEXT("PartBeakLong"));
        TestTrue(TEXT("실패한 선택은 Draft·표시를 바꾸지 않는다"), Scene->Model->GetDraft() == Draft
            && VM->GetSlotPartId(ECustomizationSlot::Beak) == TEXT("BeakDuck") && Appearance->GetDisplayedSlotMesh(ECustomizationSlot::Beak) == Beak);
        auto* Failure = Field<UTextBlock>(Scene->Screen.Get(), TEXT("FailureText"));
        TestTrue(TEXT("화면에 실패한 슬롯 이름이 나온다"), Failure
            && Failure->GetText().ToString().Contains(GetCustomizationSlotText(ECustomizationSlot::Beak).ToString()));
        Appearance->SetResourcesForTests(TEXT("BeakLong"), UCustomizationAppearanceComponent::GetDefaultResources(TEXT("BeakLong")));
        Click(Scene->Screen.Get(), TEXT("PartBeakLong"));
        TestEqual(TEXT("복구 뒤 선택 성공"), VM->GetSlotPartId(ECustomizationSlot::Beak), FName(TEXT("BeakLong")));
        TestTrue(TEXT("성공하면 실패 안내가 지워진다"), Failure->GetText().IsEmpty());
        Click(Scene->Screen.Get(), TEXT("ApplyButton"));
        TestTrue(TEXT("확정 값에 부리 선택이 들어간다"), Scene->Model->GetProfile().BeakId == TEXT("BeakLong") && !Scene->Screen->IsActivated());
        Scene->Controller->OpenCustomizationPrototype();
        TestTrue(TEXT("재개방 화면이 확정 선택을 보인다"), Scene->Screen->GetViewModel()->GetSlotPartId(ECustomizationSlot::Beak) == TEXT("BeakLong")
            && Find(Scene->Screen.Get(), TEXT("PartBeakLong"))->GetBackgroundColor() != Find(Scene->Screen.Get(), TEXT("PartBeakDuck"))->GetBackgroundColor());
        Click(Scene->Screen.Get(), TEXT("ResetButton"));
        TestTrue(TEXT("초기화는 Draft만 기본값"), Scene->Model->GetDraft() == FCustomizationProfile()
            && Scene->Model->GetProfile().BeakId == TEXT("BeakLong"));
        Click(Scene->Screen.Get(), TEXT("CancelButton"));
        TestTrue(TEXT("취소는 확정 선택으로 복귀"), Scene->Model->GetDraft() == Scene->Model->GetProfile() && !Scene->Model->IsEditing());
        // 다른 시험이 이어서 쓰는 세션 상태를 기본값으로 되돌린다.
        Scene->Controller->OpenCustomizationPrototype();
        Click(Scene->Screen.Get(), TEXT("ResetButton"));
        Click(Scene->Screen.Get(), TEXT("ApplyButton"));
        TestTrue(TEXT("기본값 복구"), Scene->Model->GetProfile() == FCustomizationProfile());
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWait(*this, [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}
}
#endif
