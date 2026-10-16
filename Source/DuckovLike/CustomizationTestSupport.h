#pragma once

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "CustomizationAppearanceComponent.h"
#include "CustomizationCatalog.h"
#include "CustomizationPartContract.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SkinnedAssetCompiler.h"

// 슬롯·카탈로그 자동 검사가 함께 쓰는 실제 에셋 장면이다.
namespace CustomizationTestSupport
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

// 카탈로그와 body의 mesh를 미리 불러오고 비동기 컴파일을 끝낸다.
inline void LoadCatalogAssets()
{
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        for (const FName PartId : Catalog.GetPartIds(Slot)) { Catalog.Find(PartId)->Resources.Mesh.TryLoad(); }
    }
    GetDefaultCustomizationBodyMesh().TryLoad();
    FSkinnedAssetCompilingManager::Get().FinishAllCompilation();
}

// 실제 에셋을 표시하는 별도 transient world다.
struct FRealWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FRealWorld() { LoadCatalogAssets(); }
    UCustomizationAppearanceComponent* CreateTarget()
    {
        AActor* Owner = World->SpawnActor<AActor>();
        auto* Root = NewObject<USceneComponent>(Owner);
        Owner->SetRootComponent(Root);
        Root->RegisterComponent();
        auto* Result = NewObject<UCustomizationAppearanceComponent>(Owner);
        Result->RegisterComponent();
        return Result;
    }
    ~FRealWorld()
    {
        World->DestroyWorld(false);
        if (World->IsRooted()) { World->RemoveFromRoot(); }
    }
};

// 표시 중인 모든 component와 MID의 pointer다. 실패가 표시를 건드리지 않았는지 비교한다.
struct FDisplaySnapshot
{
    UMeshComponent* Meshes[3] = {};
    UMaterialInstanceDynamic* MIDs[3] = {};
    UMeshComponent* Body = nullptr;
    UMaterialInstanceDynamic* BodyMID = nullptr;
    bool bRegisteredAndVisible = true;
    FCustomizationProfile Applied;

    explicit FDisplaySnapshot(const UCustomizationAppearanceComponent* Target)
    {
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const ECustomizationSlot Slot = static_cast<ECustomizationSlot>(Index);
            Meshes[Index] = Target->GetDisplayedSlotMesh(Slot);
            MIDs[Index] = Target->GetDisplayedSlotMID(Slot);
            bRegisteredAndVisible &= !Meshes[Index] || (Meshes[Index]->IsRegistered() && Meshes[Index]->IsVisible());
        }
        Body = Target->GetDisplayedBody();
        BodyMID = Target->GetDisplayedBodyMID();
        bRegisteredAndVisible &= !Body || (Body->IsRegistered() && Body->IsVisible());
        Applied = Target->GetAppliedProfile();
    }
    bool operator==(const FDisplaySnapshot& Other) const
    {
        for (int32 Index = 0; Index < 3; ++Index)
        {
            if (Meshes[Index] != Other.Meshes[Index] || MIDs[Index] != Other.MIDs[Index]) { return false; }
        }
        return Body == Other.Body && BodyMID == Other.BodyMID && bRegisteredAndVisible == Other.bRegisteredAndVisible
            && Applied == Other.Applied;
    }
};
}
#endif
