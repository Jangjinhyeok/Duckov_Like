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
#include "Components/Slider.h"
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
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/CustomizationGate/Visual");
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
    FCustomizationProfile Confirmed;
    FInventorySaveRecord InventoryBefore;
    FDelegateHandle ChangedHandle;
    int32 Events = 0;
    bool Ready = false;
    TArray<FColor> BeforeFailurePixels;
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
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
        auto* Model = Scene->Model.Get();
        auto* VM = Screen->GetViewModel();
        auto* B = Cast<UButton>(Screen->GetWidgetFromName(TEXT("PartB")));
        auto* Hue = Cast<USlider>(Screen->GetWidgetFromName(TEXT("HueSlider")));
        auto* Shape = Cast<USlider>(Screen->GetWidgetFromName(TEXT("ShapeSlider")));
        if (!TestTrue(TEXT("이벤트 입력 위젯"), B && Hue && Shape && VM)) { Scene->Ready = false; return; }
        Hue->OnValueChanged.Broadcast(0.65f);
        Shape->OnValueChanged.Broadcast(0.7f);
        B->OnClicked.Broadcast();
        TestTrue(TEXT("UI 정상 편집 확정 값 보존"), Model->GetProfile() == FCustomizationProfile());
        TestEqual(TEXT("슬라이더 Hue 표시"), Hue->GetValue(), Model->GetDraft().Hue);
        TestEqual(TEXT("슬라이더 Shape 표시"), Shape->GetValue(), Model->GetDraft().Shape);
        TestEqual(TEXT("선택 표시 getter"), VM->GetPartId(), FName(TEXT("B")));
        const int32 BeforeEvents = Scene->Events;
        B->OnClicked.Broadcast();
        TestEqual(TEXT("같은 선택 이벤트 중복 없음"), Scene->Events, BeforeEvents);
        auto* Appearance = Screen->GetPreview()->GetAppearance();
        auto* PreviousMesh = Appearance->GetDisplayedMesh();
        auto* PreviousMID = Appearance->GetDisplayedMID();
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
        Screen->GetPreview()->CapturePreview();
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
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
            && Cast<USlider>(Screen->GetWidgetFromName(TEXT("HueSlider")))->GetValue() == Scene->Confirmed.Hue);
        Cast<UButton>(Screen->GetWidgetFromName(TEXT("ResetButton")))->OnClicked.Broadcast();
        TestTrue(TEXT("초기화 Draft만 기본값"), Scene->Model->GetDraft() == FCustomizationProfile()
            && Scene->Model->GetProfile() == Scene->Confirmed);
    }, 0.25f));
    ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Scene]
    {
        if (!Scene->Ready) { return; }
        auto* Screen = Scene->Screen.Get();
        CaptureWidget(*this, Screen, TEXT("ResetDraft"));
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
            && Screen->GetViewModel()->GetHue() == FCustomizationProfile().Hue);
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
    }, 0.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWait(*this, [] { return !GEditor || !GEditor->PlayWorld; }));
    return true;
}
}
#endif
