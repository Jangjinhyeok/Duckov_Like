#include "CustomizationPreviewActor.h"

#include "CustomizationAppearanceComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"

ACustomizationPreviewActor::ACustomizationPreviewActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot")));
    Appearance = CreateDefaultSubobject<UCustomizationAppearanceComponent>(TEXT("Appearance"));
    Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
    Capture->SetupAttachment(RootComponent);
    Capture->SetRelativeLocation(FVector(230.f, -230.f, 150.f));
    Capture->SetRelativeRotation((FVector(0.f, 0.f, 57.f) - Capture->GetRelativeLocation()).Rotation());
    Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Capture->OrthoWidth = 210.f;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetTemporalAA(false);
    Capture->ShowFlags.SetSkyLighting(false);
    Capture->ShowFlags.SetEyeAdaptation(false);
    UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("PreviewLight"));
    Light->SetupAttachment(RootComponent);
    Light->SetRelativeLocation(FVector(140.f, -140.f, 170.f));
    Light->SetIntensity(16000.f);
    Light->SetAttenuationRadius(650.f);
    Light->SetCastShadows(false);
}

void ACustomizationPreviewActor::InitializeCapture()
{
    RenderTarget = NewObject<UTextureRenderTarget2D>(this);
    RenderTarget->ClearColor = FLinearColor(0.018f, 0.025f, 0.04f, 1.f);
    RenderTarget->RenderTargetFormat = RTF_RGBA8;
    RenderTarget->InitAutoFormat(512, 512);
    RenderTarget->UpdateResourceImmediate();
    Capture->TextureTarget = RenderTarget;
}

void ACustomizationPreviewActor::CapturePreview()
{
    if (!RenderTarget || !Appearance->GetDisplayedMesh()) { return; }
    Capture->ClearShowOnlyComponents();
    Capture->ShowOnlyActorComponents(this);
    Capture->CaptureScene();
}

void ACustomizationPreviewActor::EndPlay(const EEndPlayReason::Type Reason)
{
    Capture->TextureTarget = nullptr;
    Capture->ClearShowOnlyComponents();
    Appearance->ClearPreview();
    RenderTarget = nullptr;
    Super::EndPlay(Reason);
}
