#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationScreenWidget.h"
#include "CustomizationViewModel.h"
#include "CustomizationModel.h"
#include "CustomizationPreviewActor.h"
#include "CustomizationAppearanceComponent.h"
#include "InventoryDemoPlayerController.h"
#include "InventoryModel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Slider.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBindingHandle.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"

namespace CustomizationWidgetTests
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
        Test.AddError(TEXT("커스터마이징 PIE 준비/focus/종료 시간 초과"));
        return true;
    }
private:
    FAutomationTestBase& Test;
    TFunction<bool()> Condition;
    double Start = 0.0;
};

void SavePNG(FAutomationTestBase& Test, const TCHAR* Name, const TArray<FColor>& Pixels, FIntPoint Size)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/CustomizationRegions/Visual");
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
    TWeakObjectPtr<ACustomizationPreviewActor> ReleasedPreview;
    TWeakObjectPtr<ACustomizationPreviewActor> OtherPreview;
    FCustomizationProfile Confirmed;
    FInventorySaveRecord InventoryBefore;
    FDelegateHandle ChangedHandle;
    int32 Events = 0;
    bool Ready = false;
    TArray<FColor> BeforeFailurePixels;
    TArray<FColor> ShapePixels[3];
    TArray<FColor> RegionPixels[3][3];
    TArray<FColor> NeutralPixels;
    TArray<FColor> OtherPixels;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationPIELifecycle, "Duckov.Customization.UI.PIELifecycle", Flags)
bool FCustomizationPIELifecycle::RunTest(const FString& Parameters)
{
    auto Scene = MakeShared<FScene>();
    if (!TestTrue(TEXT("기존 데모 map PIE"), AutomationOpenMap(TEXT("/Game/Maps/L_InventoryDemo")))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FWait(*this, []
    {
        return GEditor && GEditor->PlayWorld && Cast<AInventoryDemoPlayerController>(
            UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        auto* Controller = Cast<AInventoryDemoPlayerController>(UGameplayStatics::GetPlayerController(GEditor->PlayWorld, 0));
        if (!Controller) { return; }
        Scene->Controller = Controller;
        auto* Inventory = Field<UInventoryModel>(Controller, TEXT("Model"));
        if (!TestNotNull(TEXT("기존 inventory Model"), Inventory)) { return; }
        TestEqual(TEXT("변경 전 inventory snapshot"), Inventory->Save(Scene->InventoryBefore), EInventorySaveFailure::None);
        Controller->OpenCustomizationPrototype();
        Scene->Screen = Field<UCustomizationScreenWidget>(Controller, TEXT("CustomizationScreen"));
        Scene->Model = Field<UCustomizationModel>(Controller, TEXT("CustomizationModel"));
        if (!TestTrue(TEXT("opt-in 화면·편집·preview"), Scene->Screen.IsValid() && Scene->Screen->IsActivated()
            && Scene->Model.IsValid() && Scene->Model->IsEditing() && Scene->Screen->GetPreview())) { return; }
        Scene->ChangedHandle = Scene->Model->OnChanged().AddLambda([Scene] { ++Scene->Events; });
        Scene->Ready = true;
        TestNotNull(TEXT("실제 skeletal body preview"), Scene->Screen->GetPreview()->GetAppearance()->GetDisplayedBody());
        TestNotNull(TEXT("실제 skeletal part preview"), Cast<USkeletalMeshComponent>(Scene->Screen->GetPreview()->GetAppearance()->GetDisplayedMesh()));
        auto* Other = GEditor->PlayWorld->SpawnActor<ACustomizationPreviewActor>(
            Scene->Screen->GetPreview()->GetActorLocation() + FVector(0.f, 2000.f, 0.f), FRotator::ZeroRotator);
        Scene->OtherPreview = Other;
        Other->InitializeCapture();
        FCustomizationProfile OtherProfile; OtherProfile.Hue = 0.2f;
        TestEqual(TEXT("두 번째 실제 색상 preview 준비"), Other->GetAppearance()->TryApply(OtherProfile), ECustomizationFailure::None);
        Other->CapturePreview();
        Scene->Screen->GetViewModel()->SetShape(-1.f);
    }, 0.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWait(*this, [Scene]
    {
        auto* Controller = Scene->Controller.Get();
        auto* Screen = Scene->Screen.Get();
        auto* PartA = Screen ? Cast<UButton>(Screen->GetWidgetFromName(TEXT("PartA"))) : nullptr;
        auto* Router = Controller && Controller->GetLocalPlayer()
            ? Controller->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
        return !Scene->Ready || (PartA && PartA->HasUserFocus(Controller) && Router
            && Router->GetActiveInputMode() == ECommonInputMode::Menu && !Router->IsPendingTreeChange());
    }));
    for (int32 Sample = 0; Sample < 3; ++Sample)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene, Sample]
        {
            if (!Scene->Ready) { return; }
            auto* Screen = Scene->Screen.Get();
            auto* Target = Screen->GetPreview()->GetRenderTarget();
            TestTrue(TEXT("실제 Shape render pixels"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Scene->ShapePixels[Sample]));
            SavePNG(*this, *FString::Printf(TEXT("MorphA-%d"), Sample), Scene->ShapePixels[Sample], FIntPoint(Target->SizeX, Target->SizeY));
            if (Sample == 0)
            {
                TestTrue(TEXT("두 번째 대상 최초 pixels"), Scene->OtherPreview->GetRenderTarget()->GameThread_GetRenderTargetResource()->ReadPixels(Scene->OtherPixels));
            }
            if (Sample < 2) { Screen->GetViewModel()->SetShape(static_cast<float>(Sample)); }
            else
            {
                TestTrue(TEXT("실제 Morph 최소/중간/최대 표시가 다름"), Scene->ShapePixels[0] != Scene->ShapePixels[1] && Scene->ShapePixels[1] != Scene->ShapePixels[2]);
                Scene->NeutralPixels = Scene->ShapePixels[2];
                auto* Appearance = Screen->GetPreview()->GetAppearance();
                auto Masked = Appearance->GetDefaultResources(TEXT("A"));
                Masked.MaskParameter = TEXT("ColorMask"); Masked.bMaskRequired = true;
                Masked.Mask = FSoftObjectPath(TEXT("/Game/Customization/Tests/T_TestMask.T_TestMask"));
                Appearance->SetResourcesForTests(TEXT("A"), Masked);
                TestEqual(TEXT("필수 실제 mask 준비 성공"), Appearance->TryApply(Scene->Model->GetDraft()), ECustomizationFailure::None);
                Screen->GetPreview()->CapturePreview();
            }
        }, 0.25f));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get(); auto* Target = Screen->GetPreview()->GetRenderTarget();
        TArray<FColor> Masked;
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Masked);
        TestTrue(TEXT("실제 mask 효과 pixels"), Masked != Scene->NeutralPixels);
        SavePNG(*this, TEXT("MaskedA"), Masked, FIntPoint(Target->SizeX, Target->SizeY));
        auto* Appearance = Screen->GetPreview()->GetAppearance();
        auto Neutral = Appearance->GetDefaultResources(TEXT("A")); Neutral.MaskParameter = TEXT("ColorMask");
        Appearance->SetResourcesForTests(TEXT("A"), Neutral);
        TestEqual(TEXT("선택 mask 미지정 흰색으로 복귀"), Appearance->TryApply(Scene->Model->GetDraft()), ECustomizationFailure::None);
        Screen->GetPreview()->CapturePreview();
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get(); auto* Target = Screen->GetPreview()->GetRenderTarget();
        TArray<FColor> Restored; Target->GameThread_GetRenderTargetResource()->ReadPixels(Restored);
        TestTrue(TEXT("mask 해제는 원래 pixels 복원"), Restored == Scene->NeutralPixels);
        SavePNG(*this, TEXT("NeutralA"), Restored, FIntPoint(Target->SizeX, Target->SizeY));
        Screen->GetPreview()->GetAppearance()->SetResourcesForTests(TEXT("A"), UCustomizationAppearanceComponent::GetDefaultResources(TEXT("A")));
        TestEqual(TEXT("실제 B 표시로 교체"), Screen->GetViewModel()->SelectPart(TEXT("B")), ECustomizationFailure::None);
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get(); auto* Target = Screen->GetPreview()->GetRenderTarget();
        TArray<FColor> B; Target->GameThread_GetRenderTargetResource()->ReadPixels(B);
        TestTrue(TEXT("동일 값의 실제 A/B pixels 차이"), B != Scene->NeutralPixels);
        SavePNG(*this, TEXT("MorphB-Max"), B, FIntPoint(Target->SizeX, Target->SizeY));
        TestEqual(TEXT("실제 A 표시로 복귀"), Screen->GetViewModel()->SelectPart(TEXT("A")), ECustomizationFailure::None);
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get(); auto* Target = Screen->GetPreview()->GetRenderTarget();
        TArray<FColor> A; Target->GameThread_GetRenderTargetResource()->ReadPixels(A);
        TestTrue(TEXT("A/B/A 실제 pixels 잔류 없음"), A == Scene->NeutralPixels);
        SavePNG(*this, TEXT("RestoredA"), A, FIntPoint(Target->SizeX, Target->SizeY));
    }, 0.25f));
    const FName RegionSliders[] = {TEXT("EyeSizeSlider"), TEXT("BeakLengthSlider"), TEXT("BodyLengthSlider")};
    for (int32 Region = 0; Region < UE_ARRAY_COUNT(RegionSliders); ++Region)
    {
        const FName SliderName = RegionSliders[Region];
        for (int32 Sample = 0; Sample < 3; ++Sample)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene, SliderName, Sample]
            {
                if (!Scene->Ready) { return; }
                auto* Slider = Cast<USlider>(Scene->Screen->GetWidgetFromName(SliderName));
                if (!TestNotNull(TEXT("부위별 delegate 입력 slider"), Slider)) { Scene->Ready = false; return; }
                Slider->OnValueChanged.Broadcast(static_cast<float>(Sample - 1));
                TestEqual(TEXT("부위별 slider 값 즉시 반영"), Slider->GetValue(), static_cast<float>(Sample - 1));
                TestTrue(TEXT("부위별 미리보기는 확정 값 보존"), Scene->Model->GetProfile() == FCustomizationProfile());
            }, 0.f));
            ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene, SliderName, Region, Sample]
            {
                if (!Scene->Ready) { return; }
                auto* Screen = Scene->Screen.Get(); auto* Target = Screen->GetPreview()->GetRenderTarget();
                TestTrue(TEXT("부위별 실제 Morph pixels 읽기"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Scene->RegionPixels[Region][Sample]));
                SavePNG(*this, *FString::Printf(TEXT("%s-%d"), *SliderName.ToString(), Sample), Scene->RegionPixels[Region][Sample], FIntPoint(Target->SizeX, Target->SizeY));
                if (Sample == 2)
                {
                    TestTrue(TEXT("부위별 최소/중립/최대 실제 표시 차이"), !Scene->RegionPixels[Region][0].IsEmpty()
                        && Scene->RegionPixels[Region][0] != Scene->RegionPixels[Region][1]
                        && Scene->RegionPixels[Region][1] != Scene->RegionPixels[Region][2]
                        && Scene->RegionPixels[Region][0] != Scene->RegionPixels[Region][2]);
                    Cast<USlider>(Screen->GetWidgetFromName(SliderName))->OnValueChanged.Broadcast(0.f);
                }
            }, 0.25f));
        }
    }
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
        auto* Model = Scene->Model.Get();
        auto* VM = Screen->GetViewModel();
        auto* B = Cast<UButton>(Screen->GetWidgetFromName(TEXT("PartB")));
        auto* Hue = Cast<USlider>(Screen->GetWidgetFromName(TEXT("HueSlider")));
        auto* Shape = Cast<USlider>(Screen->GetWidgetFromName(TEXT("ShapeSlider")));
        auto* Eye = Cast<USlider>(Screen->GetWidgetFromName(TEXT("EyeSizeSlider")));
        auto* Beak = Cast<USlider>(Screen->GetWidgetFromName(TEXT("BeakLengthSlider")));
        auto* Body = Cast<USlider>(Screen->GetWidgetFromName(TEXT("BodyLengthSlider")));
        if (!TestTrue(TEXT("이벤트 입력 위젯"), B && Hue && Shape && Eye && Beak && Body && VM)) { Scene->Ready = false; return; }
        Hue->OnValueChanged.Broadcast(0.65f);
        Shape->OnValueChanged.Broadcast(0.7f);
        Eye->OnValueChanged.Broadcast(0.5f);
        Beak->OnValueChanged.Broadcast(-0.6f);
        Body->OnValueChanged.Broadcast(0.7f);
        B->OnClicked.Broadcast();
        TestTrue(TEXT("UI 정상 편집 확정 값 보존"), Model->GetProfile() == FCustomizationProfile());
        TestEqual(TEXT("슬라이더 Hue 표시"), Hue->GetValue(), Model->GetDraft().Hue);
        TestEqual(TEXT("슬라이더 Shape 표시"), Shape->GetValue(), Model->GetDraft().Shape);
        TestEqual(TEXT("슬라이더 눈 크기 표시"), Eye->GetValue(), Model->GetDraft().EyeSize);
        TestEqual(TEXT("슬라이더 입 길이 표시"), Beak->GetValue(), Model->GetDraft().BeakLength);
        TestEqual(TEXT("슬라이더 몸통 길이 표시"), Body->GetValue(), Model->GetDraft().BodyLength);
        TestEqual(TEXT("선택 표시 getter"), VM->GetPartId(), FName(TEXT("B")));
        const int32 BeforeEvents = Scene->Events;
        B->OnClicked.Broadcast();
        Eye->OnValueChanged.Broadcast(0.5f);
        Beak->OnValueChanged.Broadcast(-0.6f);
        Body->OnValueChanged.Broadcast(0.7f);
        TestEqual(TEXT("같은 선택/부위 값 이벤트 중복 없음"), Scene->Events, BeforeEvents);
        auto* Appearance = Screen->GetPreview()->GetAppearance();
        auto* PreviousMesh = Appearance->GetDisplayedMesh();
        auto* PreviousMID = Appearance->GetDisplayedMID();
        auto* PreviousBody = Appearance->GetDisplayedBody();
        auto* PreviousBodyMID = Appearance->GetDisplayedBodyMID();
        const auto Draft = Model->GetDraft();
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("FailureButton")))->OnClicked.Broadcast();
        TestTrue(TEXT("불명 ID 실패 세 상태 보존"), Model->GetProfile() == FCustomizationProfile()
            && Model->GetDraft() == Draft && Appearance->GetDisplayedMesh() == PreviousMesh
            && Appearance->GetDisplayedMID() == PreviousMID);
        TestEqual(TEXT("실패 상태 통지 0"), Scene->Events, BeforeEvents);
        auto* Failure = Field<UTextBlock>(Screen, TEXT("FailureText"));
        TestTrue(TEXT("화면 실패 사유 표시"), Failure && !Failure->GetText().IsEmpty());
        auto Broken = UCustomizationAppearanceComponent::GetDefaultResources(TEXT("B"));
        Broken.Material = FSoftObjectPath();
        Appearance->SetResourcesForTests(TEXT("B"), Broken);
        TestEqual(TEXT("최종 적용 준비 실패"), VM->Apply(), ECustomizationFailure::MissingMaterial);
        TestTrue(TEXT("최종 적용 실패 화면 유지"), Screen->IsActivated() && Model->IsEditing());
        TestTrue(TEXT("최종 적용 실패 확정/Draft/표시 보존"), Model->GetProfile() == FCustomizationProfile()
            && Model->GetDraft() == Draft && Appearance->GetDisplayedMesh() == PreviousMesh
            && Appearance->GetDisplayedMID() == PreviousMID);
        TestEqual(TEXT("최종 적용 실패 통지 0"), Scene->Events, BeforeEvents);
        Appearance->SetResourcesForTests(TEXT("B"), UCustomizationAppearanceComponent::GetDefaultResources(TEXT("B")));
        Appearance->SetBodyMeshForTests(FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationPartA.SK_CustomizationPartA")));
        TestEqual(TEXT("UI 부위 Morph 누락 실패"), VM->SetEyeSize(-0.8f), ECustomizationFailure::MissingMorph);
        for (USlider* Slider : {Hue, Shape, Eye, Beak, Body})
        {
            const float Previous = Slider->GetValue();
            for (float Rejected : {0.1f, 0.2f})
            {
                // 실제 USlider 입력처럼 자체 값 변경 후 delegate를 호출한다.
                Slider->SetValue(Rejected);
                Slider->OnValueChanged.Broadcast(Rejected);
                TestEqual(TEXT("반복 준비 실패 후 slider 손잡이도 직전 정상 값 복원"), Slider->GetValue(), Previous);
            }
        }
        TestTrue(TEXT("UI 부위 Morph 실패 확정/Draft/body 보존"), Model->GetProfile() == FCustomizationProfile()
            && Model->GetDraft() == Draft && Appearance->GetDisplayedMesh() == PreviousMesh
            && Appearance->GetDisplayedMID() == PreviousMID && Appearance->GetDisplayedBody() == PreviousBody
            && Appearance->GetDisplayedBodyMID() == PreviousBodyMID);
        TestEqual(TEXT("UI 부위 Morph 실패 통지 0"), Scene->Events, BeforeEvents);
        Appearance->SetBodyMeshForTests(FSoftObjectPath(TEXT("/Game/Customization/Prototype/SK_CustomizationBody.SK_CustomizationBody")));
        Screen->GetPreview()->CapturePreview();
        Scene->OtherPreview->CapturePreview();
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
        TArray<FColor> OtherAfter;
        TestTrue(TEXT("두 번째 대상 재캡처 pixels"), Scene->OtherPreview->GetRenderTarget()->GameThread_GetRenderTargetResource()->ReadPixels(OtherAfter));
        TestTrue(TEXT("다른 대상의 실제 색상 pixels 독립"), !Scene->OtherPixels.IsEmpty() && OtherAfter == Scene->OtherPixels);
        SavePNG(*this, TEXT("IndependentColor"), OtherAfter, FIntPoint(512, 512));
        CaptureWidget(*this, Screen, TEXT("EditedB-FailureNotice"));
        auto* Target = Screen->GetPreview()->GetRenderTarget();
        TArray<FColor> Pixels;
        if (TestTrue(TEXT("실제 엔진 preview render pixels"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)))
        {
            SavePNG(*this, TEXT("EnginePreviewB"), Pixels, FIntPoint(Target->SizeX, Target->SizeY));
            const FColor First = Pixels[0];
            TestTrue(TEXT("미리보기 비어 있지 않음"), Pixels.ContainsByPredicate([First](FColor Color)
                { return FMath::Abs(int32(Color.R) - First.R) > 15 || FMath::Abs(int32(Color.G) - First.G) > 15; }));
            Scene->BeforeFailurePixels = Pixels;
        }
        auto* Appearance = Screen->GetPreview()->GetAppearance();
        auto Broken = UCustomizationAppearanceComponent::GetDefaultResources(TEXT("B"));
        Broken.Mesh.Reset();
        Appearance->SetResourcesForTests(TEXT("B"), Broken);
        const auto PreviousDraft = Scene->Model->GetDraft();
        const int32 PreviousEvents = Scene->Events;
        TestEqual(TEXT("render 후 리소스 실패 입력"), Screen->GetViewModel()->SetHue(0.2f), ECustomizationFailure::MissingMesh);
        TestTrue(TEXT("render 후 실패 Draft 보존"), Scene->Model->GetDraft() == PreviousDraft);
        TestEqual(TEXT("render 후 실패 통지 0"), Scene->Events, PreviousEvents);
        Appearance->SetResourcesForTests(TEXT("B"), UCustomizationAppearanceComponent::GetDefaultResources(TEXT("B")));
        Screen->GetPreview()->CapturePreview();
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
        auto* Target = Screen->GetPreview()->GetRenderTarget();
        TArray<FColor> AfterFailure;
        TestTrue(TEXT("실패 후 실제 render 읽기"), Target->GameThread_GetRenderTargetResource()->ReadPixels(AfterFailure));
        TestTrue(TEXT("리소스 실패 전후 표시 pixels 보존"), !Scene->BeforeFailurePixels.IsEmpty()
            && Scene->BeforeFailurePixels == AfterFailure);
        SavePNG(*this, TEXT("EnginePreviewB-AfterFailure"), AfterFailure, FIntPoint(Target->SizeX, Target->SizeY));
        Scene->Confirmed = Scene->Model->GetDraft();
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("ApplyButton")))->OnClicked.Broadcast();
        TestTrue(TEXT("실패 후 정상 적용·종료"), Scene->Model->GetProfile() == Scene->Confirmed
            && !Scene->Model->IsEditing() && !Screen->IsActivated() && !Screen->GetPreview());
        Scene->Controller->OpenCustomizationPrototype();
        TestTrue(TEXT("재개방 확정/Draft/표시값 일치"), Scene->Model->GetDraft() == Scene->Confirmed
            && Screen->GetViewModel()->GetPartId() == Scene->Confirmed.PartId
            && Cast<USlider>(Screen->GetWidgetFromName(TEXT("HueSlider")))->GetValue() == Scene->Confirmed.Hue
            && Cast<USlider>(Screen->GetWidgetFromName(TEXT("EyeSizeSlider")))->GetValue() == Scene->Confirmed.EyeSize
            && Cast<USlider>(Screen->GetWidgetFromName(TEXT("BeakLengthSlider")))->GetValue() == Scene->Confirmed.BeakLength
            && Cast<USlider>(Screen->GetWidgetFromName(TEXT("BodyLengthSlider")))->GetValue() == Scene->Confirmed.BodyLength);
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("ResetButton")))->OnClicked.Broadcast();
        TestTrue(TEXT("초기화 Draft만 기본값"), Scene->Model->GetDraft() == FCustomizationProfile()
            && Scene->Model->GetProfile() == Scene->Confirmed);
        TestTrue(TEXT("초기화 부위 slider 중립 표시"), Cast<USlider>(Screen->GetWidgetFromName(TEXT("EyeSizeSlider")))->GetValue() == 0.f
            && Cast<USlider>(Screen->GetWidgetFromName(TEXT("BeakLengthSlider")))->GetValue() == 0.f
            && Cast<USlider>(Screen->GetWidgetFromName(TEXT("BodyLengthSlider")))->GetValue() == 0.f);
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
        CaptureWidget(*this, Screen, TEXT("ResetDraft"));
        Screen->GetWidgetFromName(TEXT("FailureButton"))->SetUserFocus(Scene->Controller.Get());
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FWait(*this, [Scene]
    {
        if (!Scene->Ready) { return true; }
        auto* Screen = Scene->Screen.Get();
        return Screen->GetWidgetFromName(TEXT("FailureButton"))->HasUserFocus(Scene->Controller.Get())
            && Cast<UScrollBox>(Screen->GetWidgetFromName(TEXT("ControlsScroll")))->GetScrollOffset() > 0.f;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
        const FGeometry& Scroll = Screen->GetWidgetFromName(TEXT("ControlsScroll"))->GetCachedGeometry();
        for (const TCHAR* Name : {TEXT("ResetButton"), TEXT("ApplyButton"), TEXT("CancelButton"), TEXT("FailureButton")})
        {
            const FGeometry& Button = Screen->GetWidgetFromName(Name)->GetCachedGeometry();
            const auto Top = Scroll.AbsoluteToLocal(Button.LocalToAbsolute(FVector2D::ZeroVector));
            const auto Bottom = Scroll.AbsoluteToLocal(Button.LocalToAbsolute(Button.GetLocalSize()));
            TestTrue(TEXT("하단 focus 시 모든 동작 버튼이 scroll 안에 표시"), Top.Y >= -1.f && Bottom.Y <= Scroll.GetLocalSize().Y + 1.f);
        }
        CaptureWidget(*this, Screen, TEXT("FocusedActions"));
        Scene->ReleasedPreview = Screen->GetPreview();
        auto* Router = Scene->Controller->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>();
        TestEqual(TEXT("CommonUI 실제 Back routing"), Router->ProcessInput(EKeys::Escape, IE_Pressed), ERouteUIInputResult::Handled);
        TestTrue(TEXT("Back 확정/Draft 복원·preview 폐기"), Scene->Model->GetProfile() == Scene->Confirmed
            && Scene->Model->GetDraft() == Scene->Confirmed && !Scene->Model->IsEditing()
            && !Screen->IsActivated() && !Screen->GetPreview());
        TestTrue(TEXT("이전 preview destroy"), !Scene->ReleasedPreview.IsValid() || Scene->ReleasedPreview->IsActorBeingDestroyed());
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FWait(*this, [Scene]
    {
        if (!Scene->Ready) { return true; }
        auto* Router = Scene->Controller->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>();
        return Router->GetActiveInputMode() == ECommonInputMode::Game && GEngine->GameViewport
            && GEngine->GameViewport->GetGameViewportWidget()->HasAnyUserFocus().IsSet();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
        for (int32 Index = 0; Index < 3; ++Index)
        {
            Scene->Controller->OpenCustomizationPrototype();
            const int32 Before = Scene->Events;
            Screen->GetViewModel()->SetShape(Index % 2 ? 0.3f : -0.2f);
            TestEqual(TEXT("재개방 후 Model 통지 1회"), Scene->Events, Before + 1);
            Cast<UButton>(Screen->GetWidgetFromName(TEXT("CancelButton")))->OnClicked.Broadcast();
            TestTrue(TEXT("취소 확정과 Draft 일치"), Scene->Model->GetProfile() == Scene->Confirmed
                && Scene->Model->GetDraft() == Scene->Confirmed && !Screen->GetPreview());
        }
        Scene->Controller->OpenCustomizationPrototype();
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("ResetButton")))->OnClicked.Broadcast();
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("ApplyButton")))->OnClicked.Broadcast();
        TestTrue(TEXT("초기화 뒤 적용 기본값 확정"), Scene->Model->GetProfile() == FCustomizationProfile());
        Scene->Controller->OpenCustomizationPrototype();
        TestTrue(TEXT("기본값 재개방 UI"), Screen->GetViewModel()->GetPartId() == TEXT("A")
            && Screen->GetViewModel()->GetHue() == FCustomizationProfile().Hue
            && Screen->GetViewModel()->GetEyeSize() == 0.f && Screen->GetViewModel()->GetBeakLength() == 0.f
            && Screen->GetViewModel()->GetBodyLength() == 0.f);
        Scene->Controller->ToggleInventory();
        TestFalse(TEXT("customization 중 inventory 중복 진입 차단"), Field<UObject>(Scene->Controller.Get(), TEXT("Screen")) != nullptr);
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("CancelButton")))->OnClicked.Broadcast();
        auto* Inventory = Field<UInventoryModel>(Scene->Controller.Get(), TEXT("Model"));
        FInventorySaveRecord After;
        Inventory->Save(After);
        bool Same = After.NextInstanceId == Scene->InventoryBefore.NextInstanceId
            && After.Containers.Num() == Scene->InventoryBefore.Containers.Num();
        for (int32 I = 0; Same && I < After.Containers.Num(); ++I)
        {
            const auto& A = After.Containers[I];
            const auto& B = Scene->InventoryBefore.Containers[I];
            Same = A.ContainerId == B.ContainerId && A.GridSize == B.GridSize && A.Items.Num() == B.Items.Num();
            for (int32 J = 0; Same && J < A.Items.Num(); ++J)
            {
                Same = A.Items[J].InstanceId == B.Items[J].InstanceId && A.Items[J].AnchorCell == B.Items[J].AnchorCell
                    && A.Items[J].Quantity == B.Items[J].Quantity && A.Items[J].DefinitionRowName == B.Items[J].DefinitionRowName;
            }
        }
        TestTrue(TEXT("기존 inventory snapshot 불변"), Same);
        TestEqual(TEXT("기존 Raid phase 불변"), Scene->Controller->GetRaidPhase(), ERaidDemoPhase::Disabled);
        Scene->Model->OnChanged().Remove(Scene->ChangedHandle);
        if (Scene->OtherPreview.IsValid()) { Scene->OtherPreview->Destroy(); }
    }, 0.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWait(*this, [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}
}
#endif
