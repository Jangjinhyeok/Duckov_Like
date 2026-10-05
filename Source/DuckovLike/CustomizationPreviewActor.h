#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CustomizationPreviewActor.generated.h"

class UCustomizationAppearanceComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS()
class DUCKOVLIKE_API ACustomizationPreviewActor : public AActor
{
    GENERATED_BODY()
public:
    ACustomizationPreviewActor();
    void InitializeCapture();
    void CapturePreview();
    UCustomizationAppearanceComponent* GetAppearance() const { return Appearance; }
    UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCustomizationAppearanceComponent> Appearance;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneCaptureComponent2D> Capture;
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> RenderTarget;
};
