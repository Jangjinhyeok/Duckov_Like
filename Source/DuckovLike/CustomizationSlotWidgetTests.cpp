#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationAppearanceComponent.h"
#include "CustomizationCatalog.h"
#include "CustomizationModel.h"
#include "CustomizationPreviewActor.h"
#include "CustomizationScreenWidget.h"
#include "CustomizationViewModel.h"
#include "InventoryDemoPlayerController.h"
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

struct FScene
{
    TWeakObjectPtr<AInventoryDemoPlayerController> Controller;
    TWeakObjectPtr<UCustomizationScreenWidget> Screen;
    TWeakObjectPtr<UCustomizationModel> Model;
    TArray<FColor> Pixels[5];
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
        Controller->OpenCustomizationPrototype();
        Scene->Screen = Field<UCustomizationScreenWidget>(Controller, TEXT("CustomizationScreen"));
        Scene->Model = Field<UCustomizationModel>(Controller, TEXT("CustomizationModel"));
        if (!TestTrue(TEXT("화면·편집·preview"), Scene->Screen.IsValid() && Scene->Screen->IsActivated()
            && Scene->Model.IsValid() && Scene->Model->IsEditing() && Scene->Screen->GetPreview())) { return; }
        Scene->Ready = true;
        // 카탈로그의 모든 파츠에 선택 버튼이 있다. 코드에 파츠 이름이 없어도 row가 늘면 버튼이 늘어난다.
        const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
        const TCHAR* Prefixes[] = {TEXT("Part"), TEXT("Eyewear"), TEXT("BeakWear")};
        for (const ECustomizationSlot Slot : GetCustomizationSlots())
        {
            for (const FName PartId : Catalog.GetPartIds(Slot))
            {
                TestNotNull(*FString::Printf(TEXT("%s 선택 버튼"), *PartId.ToString()),
                    Find(Scene->Screen.Get(), *(FString(Prefixes[static_cast<int32>(Slot)]) + PartId.ToString())));
            }
        }
        TestNotNull(TEXT("눈 슬롯 '없음' 버튼"), Find(Scene->Screen.Get(), TEXT("EyewearNone")));
        TestNotNull(TEXT("부리 슬롯 '없음' 버튼"), Find(Scene->Screen.Get(), TEXT("BeakWearNone")));
        TestTrue(TEXT("필수 슬롯에는 '없음' 버튼이 없다"), !Find(Scene->Screen.Get(), TEXT("PartNone")));
        TestTrue(TEXT("선택된 머리 장식 버튼과 아닌 버튼의 색이 다르다"),
            Find(Scene->Screen.Get(), TEXT("PartA"))->GetBackgroundColor() != Find(Scene->Screen.Get(), TEXT("PartB"))->GetBackgroundColor());
        TestTrue(TEXT("처음에는 선택 슬롯이 비어 있다"), Scene->Screen->GetViewModel()->GetEyewearId().IsNone() && Scene->Screen->GetViewModel()->GetBeakWearId().IsNone());
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 0, TEXT("Slots0-None"));
        Click(Scene->Screen.Get(), TEXT("EyewearRoundGlasses"));
        auto* VM = Scene->Screen->GetViewModel();
        TestEqual(TEXT("눈 장식 선택 반영"), VM->GetEyewearId(), FName(TEXT("RoundGlasses")));
        TestTrue(TEXT("눈 장식 표시"), Scene->Screen->GetPreview()->GetAppearance()->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear) != nullptr);
        TestTrue(TEXT("확정 값은 그대로"), Scene->Model->GetProfile() == FCustomizationProfile());
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 1, TEXT("Slots1-Glasses"));
        TestTrue(TEXT("안경을 쓰면 미리보기가 바뀐다"), Scene->Pixels[1] != Scene->Pixels[0]);
        Click(Scene->Screen.Get(), TEXT("BeakWearBeakRing"));
        TestEqual(TEXT("부리 장식 선택 반영"), Scene->Screen->GetViewModel()->GetBeakWearId(), FName(TEXT("BeakRing")));
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 2, TEXT("Slots2-GlassesAndRing"));
        TestTrue(TEXT("부리 고리까지 쓰면 또 바뀐다"), Scene->Pixels[2] != Scene->Pixels[1]);
        Click(Scene->Screen.Get(), TEXT("EyewearNone"));
        TestTrue(TEXT("안경 해제"), Scene->Screen->GetViewModel()->GetEyewearId().IsNone()
            && !Scene->Screen->GetPreview()->GetAppearance()->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear));
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 3, TEXT("Slots3-RingOnly"));
        TestTrue(TEXT("안경만 빠지면 고리만 남은 모습이다"), Scene->Pixels[3] != Scene->Pixels[2] && Scene->Pixels[3] != Scene->Pixels[0]);
        Click(Scene->Screen.Get(), TEXT("BeakWearNone"));
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        Read(*this, *Scene, 4, TEXT("Slots4-NoneAgain"));
        TestTrue(TEXT("모두 해제하면 처음 모습과 픽셀이 같다(잔류 없음)"), Scene->Pixels[4] == Scene->Pixels[0]);
        // 슬롯 하나가 실패하면 전체가 바뀌지 않고 실패 슬롯이 화면에 나온다.
        auto* VM = Scene->Screen->GetViewModel();
        Click(Scene->Screen.Get(), TEXT("EyewearRoundGlasses"));
        auto* Appearance = Scene->Screen->GetPreview()->GetAppearance();
        auto Broken = UCustomizationAppearanceComponent::GetDefaultResources(TEXT("BeakRing"));
        Broken.Mesh.Reset();
        Appearance->SetResourcesForTests(TEXT("BeakRing"), Broken);
        const FCustomizationProfile Draft = Scene->Model->GetDraft();
        const UMeshComponent* Eyewear = Appearance->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear);
        Click(Scene->Screen.Get(), TEXT("BeakWearBeakRing"));
        TestTrue(TEXT("실패한 선택은 Draft·표시를 바꾸지 않는다"), Scene->Model->GetDraft() == Draft
            && VM->GetBeakWearId().IsNone() && Appearance->GetDisplayedSlotMesh(ECustomizationSlot::Eyewear) == Eyewear
            && !Appearance->GetDisplayedSlotMesh(ECustomizationSlot::Beak));
        auto* Failure = Field<UTextBlock>(Scene->Screen.Get(), TEXT("FailureText"));
        TestTrue(TEXT("화면에 실패한 슬롯 이름이 나온다"), Failure
            && Failure->GetText().ToString().Contains(GetCustomizationSlotText(ECustomizationSlot::Beak).ToString()));
        Appearance->SetResourcesForTests(TEXT("BeakRing"), UCustomizationAppearanceComponent::GetDefaultResources(TEXT("BeakRing")));
        Click(Scene->Screen.Get(), TEXT("BeakWearBeakRing"));
        TestEqual(TEXT("복구 뒤 선택 성공"), VM->GetBeakWearId(), FName(TEXT("BeakRing")));
        TestTrue(TEXT("성공하면 실패 안내가 지워진다"), Failure->GetText().IsEmpty());
        Click(Scene->Screen.Get(), TEXT("ApplyButton"));
        TestTrue(TEXT("확정 값에 세 슬롯 선택이 들어간다"), Scene->Model->GetProfile().EyewearId == TEXT("RoundGlasses")
            && Scene->Model->GetProfile().BeakWearId == TEXT("BeakRing") && !Scene->Screen->IsActivated());
        Scene->Controller->OpenCustomizationPrototype();
        TestTrue(TEXT("재개방 화면이 확정 선택을 보인다"), Scene->Screen->GetViewModel()->GetEyewearId() == TEXT("RoundGlasses")
            && Scene->Screen->GetViewModel()->GetBeakWearId() == TEXT("BeakRing")
            && Find(Scene->Screen.Get(), TEXT("EyewearRoundGlasses"))->GetBackgroundColor() != Find(Scene->Screen.Get(), TEXT("EyewearNone"))->GetBackgroundColor());
        Click(Scene->Screen.Get(), TEXT("ResetButton"));
        TestTrue(TEXT("초기화는 Draft만 기본값(선택 슬롯 비움)"), Scene->Model->GetDraft() == FCustomizationProfile()
            && Scene->Model->GetProfile().EyewearId == TEXT("RoundGlasses"));
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
