#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CustomizationAppearanceComponent.h"
#include "CustomizationModel.h"
#include "Animation/Skeleton.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace CustomizationTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

// C0~C2의 실제 engine placeholder 회귀를 production skeletal catalog와 분리한다.
// 기본 Profile의 여섯 슬롯을 모두 엔진 도형으로 바꾸므로 body가 없는 static 표시다. HairB만 구(Sphere)로 만들어 교체를 구별한다.
FCustomizationPartResources GetEngineResources(FName PartId)
{
    FCustomizationPartResources Resources;
    Resources.Mesh = FSoftObjectPath(PartId == TEXT("HairB")
        ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cube.Cube"));
    Resources.Material = FSoftObjectPath(TEXT("/Engine/EngineDebugMaterials/M_SimpleOpaque.M_SimpleOpaque"));
    // 발은 색이 없는 슬롯이라 고정색 계약을 따른다.
    if (PartId == FCustomizationProfile().FootId) { Resources.ColorParameter = NAME_None; }
    return Resources;
}

// 표시 callback 대역이다. 실제 리소스/렌더링 증거와 구분한다.
struct FModelFixture
{
    TStrongObjectPtr<UCustomizationModel> Model{NewObject<UCustomizationModel>()};
    FCustomizationProfile Display;
    ECustomizationFailure Failure = ECustomizationFailure::None;
    int32 Presentations = 0;
    int32 Events = 0;
    FModelFixture() { Model->OnChanged().AddLambda([this] { ++Events; }); }
    ECustomizationFailure Present(const FCustomizationProfile& Candidate)
    {
        ++Presentations;
        if (Failure == ECustomizationFailure::None) { Display = Candidate; }
        return Failure;
    }
};

// 실제 엔진 Cube/Sphere/material을 등록하는 별도 transient world이다. 픽셀 관찰은 하지 않는다.
struct FAppearanceFixture
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    UCustomizationAppearanceComponent* A = nullptr;
    UCustomizationAppearanceComponent* B = nullptr;
    FAppearanceFixture()
    {
        A = CreateTarget();
        B = CreateTarget();
    }
    UCustomizationAppearanceComponent* CreateTarget()
    {
        AActor* Actor = World->SpawnActor<AActor>();
        USceneComponent* Root = NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root);
        Root->RegisterComponent();
        UCustomizationAppearanceComponent* Appearance = NewObject<UCustomizationAppearanceComponent>(Actor);
        const FCustomizationProfile Defaults;
        for (const ECustomizationSlot Slot : GetCustomizationSlots())
        {
            Appearance->SetResourcesForTests(Defaults.GetSlotPartId(Slot), GetEngineResources(Defaults.GetSlotPartId(Slot)));
        }
        Appearance->SetResourcesForTests(TEXT("HairB"), GetEngineResources(TEXT("HairB")));
        Appearance->RegisterComponent();
        return Appearance;
    }
    ~FAppearanceFixture()
    {
        World->DestroyWorld(false);
        if (World->IsRooted()) { World->RemoveFromRoot(); }
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationModelAtomicity, "Duckov.Customization.Model.AtomicFailureAndRetry", Flags)
bool FCustomizationModelAtomicity::RunTest(const FString& Parameters)
{
    FModelFixture F;
    const auto Present = [&F](const FCustomizationProfile& Value) { return F.Present(Value); };
    const FCustomizationProfile Original = F.Model->GetProfile();
    TestEqual(TEXT("편집 시작"), F.Model->BeginEdit(Present), ECustomizationFailure::None);
    FCustomizationProfile Good = Original;
    Good.HairId = TEXT("HairB"); Good.EyeId = TEXT("EyeLarge"); Good.SkinColor = 7; Good.WingColor = 12; Good.HeadShape = 0.4f;
    TestEqual(TEXT("정상 편집"), F.Model->TryEdit(Good, Present), ECustomizationFailure::None);
    TestTrue(TEXT("편집은 확정 값을 보존"), F.Model->GetProfile() == Original);
    TestTrue(TEXT("표시와 Draft 일치"), F.Display == Good && F.Model->GetDraft() == Good);

    struct FInvalidCase { FCustomizationProfile Value; ECustomizationFailure Expected; };
    TArray<FInvalidCase> Cases;
    auto Add = [&Cases, &Good](ECustomizationFailure Expected) -> FCustomizationProfile&
    {
        Cases.Add({Good, Expected}); return Cases.Last().Value;
    };
    Add(ECustomizationFailure::UnknownPart).HairId = TEXT("Unknown");
    Add(ECustomizationFailure::RequiredPartMissing).EyeId = NAME_None;
    Add(ECustomizationFailure::RequiredPartMissing).FootId = NAME_None;
    // 다른 슬롯의 파츠를 가리키면 슬롯 불일치다.
    Add(ECustomizationFailure::SlotMismatch).EyeId = TEXT("HairA");
    Add(ECustomizationFailure::SlotMismatch).HairId = TEXT("EyeRound");
    Add(ECustomizationFailure::UnknownPart).BeakId = TEXT("Unknown");
    Add(ECustomizationFailure::InvalidNumber).HeadShape = std::numeric_limits<float>::quiet_NaN();
    Add(ECustomizationFailure::InvalidNumber).HeadShape = std::numeric_limits<float>::infinity();
    Add(ECustomizationFailure::ColorOutOfRange).SkinColor = -1;
    Add(ECustomizationFailure::ColorOutOfRange).SkinColor = GetCustomizationPalette().Num();
    Add(ECustomizationFailure::ColorOutOfRange).HairColor = GetCustomizationPalette().Num();
    Add(ECustomizationFailure::ColorOutOfRange).EyeColor = -1;
    Add(ECustomizationFailure::HeadShapeOutOfRange).HeadShape = -0.01f;
    Add(ECustomizationFailure::HeadShapeOutOfRange).HeadShape = 1.01f;
    const int32 BeforeEvents = F.Events;
    const int32 BeforePresentations = F.Presentations;
    for (const FInvalidCase& Case : Cases)
    {
        TestEqual(TEXT("후보 실패 사유"), F.Model->TryEdit(Case.Value, Present), Case.Expected);
        TestTrue(TEXT("후보 실패 후 상태/표시 보존"), F.Model->GetProfile() == Original
            && F.Model->GetDraft() == Good && F.Display == Good && F.Model->IsEditing());
    }
    TestEqual(TEXT("데이터 실패에 표시 callback 없음"), F.Presentations, BeforePresentations);
    TestEqual(TEXT("데이터 실패에 이벤트 없음"), F.Events, BeforeEvents);
    F.Failure = ECustomizationFailure::MissingMesh;
    FCustomizationProfile Next = Good; Next.SkinColor = 3;
    TestEqual(TEXT("리소스 준비 실패"), F.Model->TryEdit(Next, Present), ECustomizationFailure::MissingMesh);
    TestEqual(TEXT("최종 적용도 리소스 재검증"), F.Model->Apply(Present), ECustomizationFailure::MissingMesh);
    TestEqual(TEXT("취소 복귀 실패"), F.Model->Cancel(Present), ECustomizationFailure::MissingMesh);
    TestEqual(TEXT("초기화 준비 실패"), F.Model->Reset(Present), ECustomizationFailure::MissingMesh);
    TestTrue(TEXT("리소스 실패 뒤 세 값과 편집 보존"), F.Model->GetProfile() == Original
        && F.Model->GetDraft() == Good && F.Display == Good && F.Model->IsEditing());
    TestEqual(TEXT("리소스 실패에 이벤트 없음"), F.Events, BeforeEvents);
    F.Failure = ECustomizationFailure::None;
    TestEqual(TEXT("실패 후 정상 편집 재시도"), F.Model->TryEdit(Next, Present), ECustomizationFailure::None);
    TestEqual(TEXT("실패 후 적용 재시도"), F.Model->Apply(Present), ECustomizationFailure::None);
    TestTrue(TEXT("적용 값/표시 일치와 편집 종료"), F.Model->GetProfile() == Next
        && F.Model->GetDraft() == Next && F.Display == Next && !F.Model->IsEditing());
    F.Model->BeginEdit(Present);
    FCustomizationProfile NoHair = Next; NoHair.HairId = NAME_None;
    TestEqual(TEXT("헤어는 비울 수 있다"), F.Model->TryEdit(NoHair, Present), ECustomizationFailure::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationModelLifecycle, "Duckov.Customization.Model.EditLifecycleAndNotifications", Flags)
bool FCustomizationModelLifecycle::RunTest(const FString& Parameters)
{
    FModelFixture F;
    const auto Present = [&F](const FCustomizationProfile& Value) { return F.Present(Value); };
    FCustomizationProfile Good;
    Good.HairId = TEXT("HairB"); Good.SkinColor = 15; Good.HairColor = 0; Good.TailColor = 9; Good.HeadShape = 0.0f;
    F.Model->BeginEdit(Present);
    F.Model->TryEdit(Good, Present);
    const int32 EditedEvents = F.Events;
    TestEqual(TEXT("동일 값 편집 성공"), F.Model->TryEdit(Good, Present), ECustomizationFailure::None);
    TestEqual(TEXT("중복 열기 성공"), F.Model->BeginEdit(Present), ECustomizationFailure::None);
    TestEqual(TEXT("동일 값/중복 열기 알림 없음"), F.Events, EditedEvents);
    FCustomizationProfile PartA = Good; PartA.HairId = TEXT("HairA");
    F.Model->TryEdit(PartA, Present);
    F.Model->TryEdit(Good, Present);
    TestTrue(TEXT("헤어 A/B 교체가 색상·머리 모양 값 유지"), F.Model->GetDraft() == Good && F.Display == Good);
    F.Model->Apply(Present);
    const int32 AppliedEvents = F.Events;
    TestEqual(TEXT("종료 뒤 반복 적용 거부"), F.Model->Apply(Present), ECustomizationFailure::NotEditing);
    TestEqual(TEXT("반복 적용 알림 없음"), F.Events, AppliedEvents);
    F.Model->BeginEdit(Present);
    TestTrue(TEXT("재개방 확정 값 복원"), F.Model->GetDraft() == Good && F.Display == Good);
    F.Model->Reset(Present);
    TestTrue(TEXT("초기화는 Draft/표시만 기본값"), F.Model->GetProfile() == Good
        && F.Model->GetDraft() == FCustomizationProfile() && F.Display == FCustomizationProfile());
    const int32 ResetEvents = F.Events;
    F.Model->Reset(Present);
    TestEqual(TEXT("반복 초기화 이벤트 없음"), F.Events, ResetEvents);
    F.Model->Cancel(Present);
    TestTrue(TEXT("취소는 확정 외형으로 복귀"), F.Model->GetProfile() == Good
        && F.Model->GetDraft() == Good && F.Display == Good && !F.Model->IsEditing());
    F.Model->BeginEdit(Present);
    F.Model->Reset(Present);
    F.Model->Apply(Present);
    F.Model->BeginEdit(Present);
    TestTrue(TEXT("초기화 후 적용/재개방 기본값 일치"), F.Model->GetProfile() == FCustomizationProfile()
        && F.Model->GetDraft() == FCustomizationProfile() && F.Display == FCustomizationProfile());
    FCustomizationProfile Upper; Upper.SkinColor = 0; Upper.HairColor = 15; Upper.HeadShape = 1.0f;
    TestEqual(TEXT("반대쪽 수치 경계 허용"), F.Model->TryEdit(Upper, Present), ECustomizationFailure::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationModelPublication, "Duckov.Customization.Model.PublicationAndReentry", Flags)
bool FCustomizationModelPublication::RunTest(const FString& Parameters)
{
    FModelFixture F;
    FCustomizationProfile Next; Next.HairId = TEXT("HairB"); Next.SkinColor = 6;
    const auto Present = [&F](const FCustomizationProfile& Value) { return F.Present(Value); };
    F.Model->BeginEdit(Present);
    ECustomizationFailure CallbackReentry = ECustomizationFailure::None;
    ECustomizationFailure EventReentry = ECustomizationFailure::None;
    F.Model->OnChanged().AddLambda([&]
    {
        TestTrue(TEXT("이벤트 시 표시가 먼저 공개됨"), F.Display == F.Model->GetDraft());
        EventReentry = F.Model->Cancel(Present);
    });
    const auto CheckedPresent = [&](const FCustomizationProfile& Value)
    {
        TestTrue(TEXT("표시 callback 중 Model은 이전 정상 값"), F.Model->GetDraft() == FCustomizationProfile());
        CallbackReentry = F.Model->TryEdit(Next, Present);
        return F.Present(Value);
    };
    TestEqual(TEXT("정상 후보 공개"), F.Model->TryEdit(Next, CheckedPresent), ECustomizationFailure::None);
    TestEqual(TEXT("callback 재진입 거부"), CallbackReentry, ECustomizationFailure::OperationInProgress);
    TestEqual(TEXT("이벤트 재진입 거부"), EventReentry, ECustomizationFailure::OperationInProgress);
    TestTrue(TEXT("재진입으로 값 손상 없음"), F.Model->GetDraft() == Next && F.Display == Next);
    F.Model->OnChanged().Clear();
    F.Model->Apply(Present);
    F.Failure = ECustomizationFailure::PreparationFailed;
    TestEqual(TEXT("재개방 표시 준비 실패"), F.Model->BeginEdit(Present), ECustomizationFailure::PreparationFailed);
    TestTrue(TEXT("재개방 실패는 편집 시작 안 함"), !F.Model->IsEditing() && F.Model->GetProfile() == Next
        && F.Model->GetDraft() == Next && F.Display == Next);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationRealResources, "Duckov.Customization.Appearance.EngineAssetsAtomicity", Flags)
bool FCustomizationRealResources::RunTest(const FString& Parameters)
{
    FAppearanceFixture F;
    FCustomizationProfile Value;
    if (!TestEqual(TEXT("실제 엔진 도형/material 준비"), F.A->TryApply(Value), ECustomizationFailure::None)) { return false; }
    TestTrue(TEXT("static 대역만 있으면 body가 없다"), !F.A->GetDisplayedBody());
    UMeshComponent* OriginalMesh = F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair);
    UMaterialInstanceDynamic* OriginalMID = F.A->GetDisplayedSlotMID(ECustomizationSlot::Hair);
    TStrongObjectPtr<UCustomizationModel> Model{NewObject<UCustomizationModel>()};
    const auto Present = [&](const FCustomizationProfile& Candidate) { return F.A->TryApply(Candidate); };
    Model->BeginEdit(Present);
    int32 Events = 0;
    Model->OnChanged().AddLambda([&] { ++Events; });
    const auto Shown = [&] { return F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair) == OriginalMesh
        && F.A->GetDisplayedSlotMID(ECustomizationSlot::Hair) == OriginalMID; };
    TestTrue(TEXT("표시 component 등록/visible"), OriginalMesh->IsRegistered() && OriginalMesh->IsVisible());
    TestEqual(TEXT("동일 Profile 반복 적용"), F.A->TryApply(Value), ECustomizationFailure::None);
    TestTrue(TEXT("동일 값은 mesh/MID 재생성 없음"), Shown());
    FCustomizationProfile Invalid = Value; Invalid.HeadShape = std::numeric_limits<float>::quiet_NaN();
    TestEqual(TEXT("Component 자체 수치 검증"), F.A->TryApply(Invalid), ECustomizationFailure::InvalidNumber);
    FCustomizationProfile Unsupported = Value; Unsupported.HeadShape = 0.5f;
    TestEqual(TEXT("body 없는 static 표시는 머리 모양 성공을 가장하지 않음"), Model->TryEdit(Unsupported, Present),
        ECustomizationFailure::UnsupportedBodyAdjustment);
    TestTrue(TEXT("미지원 조절의 Model/표시 보존"), Model->GetProfile() == Value && Model->GetDraft() == Value && Shown());
    TestEqual(TEXT("미지원 조절 통지 없음"), Events, 0);
    FCustomizationPartResources Broken = GetEngineResources(TEXT("HairB")); Broken.Mesh.Reset();
    F.A->SetResourcesForTests(TEXT("HairB"), Broken);
    FCustomizationProfile B = Value; B.HairId = TEXT("HairB");
    TestEqual(TEXT("실제 표시 후 리소스 실패"), F.A->TryApply(B), ECustomizationFailure::MissingMesh);
    TestEqual(TEXT("실제 리소스 callback 편집 실패"), Model->TryEdit(B, Present), ECustomizationFailure::MissingMesh);
    TestTrue(TEXT("실제 리소스 실패는 확정/Draft도 보존"), Model->GetProfile() == Value && Model->GetDraft() == Value);
    TestEqual(TEXT("실제 리소스 실패 이벤트 없음"), Events, 0);
    TestTrue(TEXT("실패는 기존 등록 mesh/MID/visibility 보존"), Shown() && OriginalMesh->IsRegistered() && OriginalMesh->IsVisible());
    F.A->SetResourcesForTests(TEXT("HairB"), GetEngineResources(TEXT("HairB")));
    Value.HairColor = 9;
    TestEqual(TEXT("실패 후 색상 편집"), Model->TryEdit(Value, Present), ECustomizationFailure::None);
    TestTrue(TEXT("같은 static mesh 색 편집은 component 유지"), F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair) == OriginalMesh);
    FLinearColor Color;
    TestTrue(TEXT("실제 MID 팔레트 색상"), F.A->GetDisplayedSlotMID(ECustomizationSlot::Hair)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color)
        && Color.Equals(GetCustomizationColor(Value.HairColor)));
    TestTrue(TEXT("색이 없는 발은 고정색(override 없음)"), F.A->GetDisplayedSlotMID(ECustomizationSlot::Foot)->VectorParameterValues.Num() == 0);
    TestTrue(TEXT("static 표시는 원점·scale 1"), F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair)->GetRelativeTransform().Equals(FTransform::Identity));
    B = Value; B.HairId = TEXT("HairB");
    TestEqual(TEXT("실제 B 교체"), F.A->TryApply(B), ECustomizationFailure::None);
    TestTrue(TEXT("이전 A는 unregister"), !OriginalMesh->IsRegistered());
    TestEqual(TEXT("실제 A 복귀"), F.A->TryApply(Value), ECustomizationFailure::None);
    TestTrue(TEXT("A/B/A 색상 잔류 없음"), F.A->GetDisplayedSlotMID(ECustomizationSlot::Hair)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color)
        && Color.Equals(GetCustomizationColor(Value.HairColor)));
    TestTrue(TEXT("A/B/A transform 누적 없음"), F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair)->GetRelativeTransform().Equals(FTransform::Identity));
    TestEqual(TEXT("실제 리소스 경로로 확정 적용"), Model->Apply(Present), ECustomizationFailure::None);
    TestTrue(TEXT("실제 리소스 적용은 Model 확정과 일치"), Model->GetProfile() == Value && !Model->IsEditing());
    F.A->ClearPreview();
    TestTrue(TEXT("preview 종료 수명 정리"), !F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair) && !F.A->GetDisplayedSlotMID(ECustomizationSlot::Hair));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationColorIsolation, "Duckov.Customization.Appearance.TargetColorIsolation", Flags)
bool FCustomizationColorIsolation::RunTest(const FString& Parameters)
{
    FAppearanceFixture F;
    FCustomizationProfile A; A.HairColor = 1;
    FCustomizationProfile B; B.HairColor = 9;
    UMaterialInterface* Shared = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/M_SimpleOpaque.M_SimpleOpaque"));
    if (!TestNotNull(TEXT("공유 실제 engine material"), Shared)) { return false; }
    FLinearColor BeforeShared;
    TestTrue(TEXT("공유 material 실제 Color parameter"), Shared->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), BeforeShared));
    if (!TestEqual(TEXT("첫 대상"), F.A->TryApply(A), ECustomizationFailure::None)
        || !TestEqual(TEXT("두 번째 대상"), F.B->TryApply(B), ECustomizationFailure::None)) { return false; }
    const auto HairMID = [](UCustomizationAppearanceComponent* Target) { return Target->GetDisplayedSlotMID(ECustomizationSlot::Hair); };
    TestTrue(TEXT("대상별 별도 MID"), HairMID(F.A) != HairMID(F.B) && HairMID(F.A)->Parent == Shared && HairMID(F.B)->Parent == Shared);
    UMaterialInstanceDynamic* BeforeBMID = HairMID(F.B);
    A.HairColor = 12;
    F.A->TryApply(A);
    FLinearColor BColor;
    TestTrue(TEXT("한 대상 편집은 다른 대상 MID/색 보존"), HairMID(F.B) == BeforeBMID
        && BeforeBMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), BColor)
        && BColor.Equals(GetCustomizationColor(B.HairColor)));
    FLinearColor AfterShared;
    TestTrue(TEXT("공유 material asset 변경 없음"), Shared->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), AfterShared)
        && AfterShared.Equals(BeforeShared));
    CollectGarbage(RF_NoFlags);
    UMeshComponent* Hair = F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair);
    TestTrue(TEXT("GC 후 표시 mesh/MID 보존"), IsValid(Hair) && IsValid(HairMID(F.A)) && Hair->GetMaterial(0) == HairMID(F.A));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomizationResourceContracts, "Duckov.Customization.Appearance.ResourceDiagnostics", Flags)
bool FCustomizationResourceContracts::RunTest(const FString& Parameters)
{
    FAppearanceFixture F;
    FCustomizationProfile Value;
    if (!TestEqual(TEXT("진단 전 정상 표시"), F.A->TryApply(Value), ECustomizationFailure::None)) { return false; }
    UMeshComponent* OriginalMesh = F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair);
    UMaterialInstanceDynamic* OriginalMID = F.A->GetDisplayedSlotMID(ECustomizationSlot::Hair);
    const auto Check = [&](const FCustomizationPartResources& Resources, ECustomizationFailure Expected)
    {
        F.A->SetResourcesForTests(TEXT("HairA"), Resources);
        TestEqual(TEXT("리소스 규약 실패 사유"), F.A->TryApply(Value), Expected);
        TestTrue(TEXT("각 리소스 실패 후 표시 보존"), F.A->GetDisplayedSlotMesh(ECustomizationSlot::Hair) == OriginalMesh
            && F.A->GetDisplayedSlotMID(ECustomizationSlot::Hair) == OriginalMID && OriginalMesh->IsRegistered() && OriginalMesh->IsVisible());
    };
    FCustomizationPartResources Bad = GetEngineResources(TEXT("HairA"));
    Bad.Material.Reset(); Check(Bad, ECustomizationFailure::MissingMaterial);
    Bad = GetEngineResources(TEXT("HairA")); Bad.MaterialSlot = 99; Check(Bad, ECustomizationFailure::MissingMaterial);
    Bad = GetEngineResources(TEXT("HairA")); Bad.ColorParameter = TEXT("AbsentColor"); Check(Bad, ECustomizationFailure::MissingColorParameter);
    Bad = GetEngineResources(TEXT("HairA")); Bad.bMaskRequired = true; Check(Bad, ECustomizationFailure::MissingMask);
    Bad = GetEngineResources(TEXT("HairA")); Bad.MaskParameter = TEXT("AbsentMask"); Check(Bad, ECustomizationFailure::MissingMaskParameter);
    Bad = GetEngineResources(TEXT("HairA"));
    FCustomizationMorphFollow Follow;
    Follow.BodyMorph = TEXT("NoSuchMorph");
    Bad.FollowBodyMorphs.Add(Follow);
    Check(Bad, ECustomizationFailure::MissingMorph);
    Bad = GetEngineResources(TEXT("HairA")); Bad.MeshKind = ECustomizationMeshKind::Skeletal;
    Check(Bad, ECustomizationFailure::MissingMesh);

    // 실제 rig가 없으므로 다음 세 검사는 transient Skeleton/mesh 대역의 사전 진단이다.
    TStrongObjectPtr<USkeleton> Skeleton{NewObject<USkeleton>()};
    TStrongObjectPtr<USkeleton> OtherSkeleton{NewObject<USkeleton>()};
    TStrongObjectPtr<USkeletalMesh> Skeletal{NewObject<USkeletalMesh>()};
    Skeletal->SetSkeleton(Skeleton.Get());
    Bad.Mesh = FSoftObjectPath(Skeletal.Get());
    Check(Bad, ECustomizationFailure::MissingSkeleton);
    Bad.ExpectedSkeleton = FSoftObjectPath(OtherSkeleton.Get());
    Check(Bad, ECustomizationFailure::SkeletonMismatch);
    // Skeleton이 맞아도 material 슬롯이 없는 mesh다.
    Bad.ExpectedSkeleton = FSoftObjectPath(Skeleton.Get());
    Check(Bad, ECustomizationFailure::MissingMaterial);
    F.A->SetResourcesForTests(TEXT("HairA"), GetEngineResources(TEXT("HairA")));
    TestEqual(TEXT("선택 mask 없음은 정상 기본 경로"), F.A->TryApply(Value), ECustomizationFailure::None);
    return true;
}
}
#endif
