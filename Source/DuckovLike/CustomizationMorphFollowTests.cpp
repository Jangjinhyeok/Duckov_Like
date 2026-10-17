#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "CustomizationTestSupport.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"

namespace CustomizationMorphFollowTests
{
using namespace CustomizationTestSupport;

// 허용 오차(cm)다. 접촉 이동은 파츠 단면 두께에 걸친 정점의 평균이라 그만큼 흩어진다.
constexpr float Tolerance = 0.3f;
// 파츠 정점과 이 거리(cm) 안에 있는 body 정점을 파츠가 기대는 정점으로 본다.
constexpr float ContactDistance = 3.0f;

TArray<FVector3f> ReferencePositions(const USkeletalMesh* Mesh)
{
    TArray<FSoftSkinVertex> Vertices;
    Mesh->GetImportedModel()->LODModels[0].GetVertices(Vertices);
    TArray<FVector3f> Result;
    for (const FSoftSkinVertex& Vertex : Vertices) { Result.Add(Vertex.Position); }
    return Result;
}

TArray<const FMorphTargetDelta*> MovingDeltas(const TArray<FVector3f>& Body, const UMorphTarget* Morph)
{
    TArray<const FMorphTargetDelta*> Result;
    for (const FMorphTargetDelta& Delta : Morph->GetMorphTargetDeltas(0))
    {
        if (!Delta.PositionDelta.IsNearlyZero(0.01f) && Body.IsValidIndex(Delta.SourceIdx)) { Result.Add(&Delta); }
    }
    return Result;
}

// 파츠가 기대는 body 정점의 평균 이동이다. 파츠 정점마다 이 Morph가 움직이는 가장 가까운 body 정점을 찾고,
// ContactDistance 안이면 그 이동을 평균에 넣는다. 닿는 정점이 없으면 0이다. 데이터와 독립된 기준값이다.
FVector MeasureContactShift(const TArray<FVector3f>& Body, TConstArrayView<const FMorphTargetDelta*> Moving, const TArray<FVector3f>& Part)
{
    FVector Sum = FVector::ZeroVector;
    int32 Count = 0;
    for (const FVector3f& Position : Part)
    {
        const FMorphTargetDelta* Nearest = nullptr;
        float NearestSquared = FMath::Square(ContactDistance);
        for (const FMorphTargetDelta* Delta : Moving)
        {
            const float Squared = FVector3f::DistSquared(Body[Delta->SourceIdx], Position);
            if (Squared <= NearestSquared) { NearestSquared = Squared; Nearest = Delta; }
        }
        if (Nearest) { Sum += FVector(Nearest->PositionDelta); ++Count; }
    }
    return Count ? Sum / Count : FVector::ZeroVector;
}

// 파츠 정점(Offset만큼 옮긴)과 body 정점(이 Morph를 Weight만큼 적용한) 사이의 최소 간격이다.
// bMovingOnly면 이 Morph가 움직이는 정점만, 아니면 body 전체를 본다.
float MinClearance(const TArray<FVector3f>& Body, TConstArrayView<const FMorphTargetDelta*> Moving, const TArray<FVector3f>& Part,
    float Weight, const FVector& Offset, bool bMovingOnly)
{
    TArray<FVector3f> Points;
    if (bMovingOnly)
    {
        for (const FMorphTargetDelta* Delta : Moving) { Points.Add(Body[Delta->SourceIdx] + Delta->PositionDelta * Weight); }
    }
    else
    {
        Points = Body;
        for (const FMorphTargetDelta* Delta : Moving) { Points[Delta->SourceIdx] += Delta->PositionDelta * Weight; }
    }
    float MinSquared = UE_BIG_NUMBER;
    for (const FVector3f& Position : Part)
    {
        const FVector3f Moved = Position + FVector3f(Offset);
        for (const FVector3f& Point : Points) { MinSquared = FMath::Min(MinSquared, FVector3f::DistSquared(Point, Moved)); }
    }
    return FMath::Sqrt(MinSquared);
}

// 적용 component처럼 같은 Morph의 항목을 모두 더한다.
FVector DataOffset(const FCustomizationPartResources& Resources, FName Morph)
{
    FVector Result = FVector::ZeroVector;
    for (const FCustomizationMorphFollow& Follow : Resources.FollowBodyMorphs)
    {
        if (Follow.BodyMorph == Morph) { Result += Follow.Offset; }
    }
    return Result;
}

// 고정 몸통의 머리 모양 Morph(HeadFlat)를 머리에 붙은 헤어·눈이 따라간다. Morph는 정점만 옮기고 bone은 그대로라
// 따로 붙은 파츠는 데이터(FollowBodyMorphs)의 이동량만큼 옮긴다(설계결정기록 031의 장치를 머리 모양에 쓴다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadPartsFollowHeadShape, "Duckov.Customization.Slots.HeadPartsFollowHeadShape", Flags)
bool FHeadPartsFollowHeadShape::RunTest(const FString& Parameters)
{
    LoadCatalogAssets();
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    const USkeletalMesh* BodyMesh = Cast<USkeletalMesh>(GetDefaultCustomizationBodyMesh().TryLoad());
    if (!TestTrue(TEXT("카탈로그와 body"), Catalog.IsAvailable() && BodyMesh)) { return false; }
    const TArray<FVector3f> Body = ReferencePositions(BodyMesh);
    const TConstArrayView<FName> BodyMorphs = GetCustomizationBodyMorphs();

    // 1) 모든 등록 파츠 × 머리 모양 Morph 극값(weight 1)의 형상 검사. 어느 Morph를 따를지는 데이터(사람의 의도)가 정한다.
    //    따르는 Morph는 이동량이 파츠가 기대는 body 정점의 평균 이동(접촉 이동)과 같아야 한다(기준 정점에 붙어 있음).
    //    머리 아래 부위(부리·날개·꼬리·발)는 머리 모양이 닿지 않아 따르지 않는다.
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        for (const FName PartId : Catalog.GetPartIds(Slot))
        {
            const FCustomizationPartRow* Row = Catalog.Find(PartId);
            const USkeletalMesh* Part = Cast<USkeletalMesh>(Row->Resources.Mesh.TryLoad());
            if (!TestNotNull(*FString::Printf(TEXT("%s mesh"), *PartId.ToString()), Part)) { continue; }
            const TArray<FVector3f> PartPositions = ReferencePositions(Part);
            for (const FName MorphName : BodyMorphs)
            {
                const UMorphTarget* Morph = BodyMesh->FindMorphTarget(MorphName);
                if (!TestNotNull(*FString::Printf(TEXT("body Morph %s"), *MorphName.ToString()), Morph)) { continue; }
                const TArray<const FMorphTargetDelta*> Moving = MovingDeltas(Body, Morph);
                const FVector Contact = MeasureContactShift(Body, Moving, PartPositions);
                const FVector Offset = DataOffset(Row->Resources, MorphName);
                const bool bFollows = Row->Resources.FollowBodyMorphs.ContainsByPredicate(
                    [MorphName](const FCustomizationMorphFollow& Item) { return Item.BodyMorph == MorphName; });
                const float NeutralRegion = MinClearance(Body, Moving, PartPositions, 0.f, FVector::ZeroVector, true);
                const float Corrected = MinClearance(Body, Moving, PartPositions, 1.f, Offset, true);
                const float Uncorrected = MinClearance(Body, Moving, PartPositions, 1.f, FVector::ZeroVector, true);
                const FString Label = FString::Printf(
                    TEXT("%s × %s: 접촉 이동 (%.2f, %.2f, %.2f), 데이터 (%.2f, %.2f, %.2f), 머리 간격 %.2f → 보정 %.2f / 미보정 %.2f cm"),
                    *PartId.ToString(), *MorphName.ToString(), Contact.X, Contact.Y, Contact.Z,
                    Offset.X, Offset.Y, Offset.Z, NeutralRegion, Corrected, Uncorrected);
                AddInfo(Label);
                if (Slot == ECustomizationSlot::Hair || Slot == ECustomizationSlot::Eye)
                {
                    // 새 파츠를 등록할 때 접촉 이동 측정값을 JSON의 FollowBodyMorphs에 적는다.
                    TestTrue(Label + TEXT(" / 머리 파츠는 따르고, 이동량은 접촉 이동과 같다"),
                        bFollows && !Contact.IsNearlyZero(Tolerance) && FVector::Dist(Contact, Offset) <= Tolerance);
                }
                else
                {
                    TestTrue(Label + TEXT(" / 머리 아래 부위에는 머리 모양이 닿지 않는다"), Contact.IsNearlyZero(Tolerance) && !bFollows);
                }
                // 고친 증상이 실제로 크다는 것을 고정한다. 보정이 없으면 납작한 머리 위에 헤어가 수 cm 뜬다.
                if (Slot == ECustomizationSlot::Hair)
                {
                    TestTrue(Label + TEXT(" / 헤어는 가장 납작할 때 5 cm 넘게 내려간다"), Contact.Size() > 5.0);
                }
            }
        }
    }

    // 2) 실제 적용: 머리 모양 최대·중간·최소에서 머리 파츠 위치가 현재 weight × 데이터 이동량이다.
    FRealWorld Scene;
    UCustomizationAppearanceComponent* Target = Scene.CreateTarget();
    FCustomizationProfile Profile;
    for (const float Value : {1.f, 0.75f, 0.5f, 0.25f, 0.f})
    {
        Profile.HeadShape = Value;
        if (!TestEqual(*FString::Printf(TEXT("적용 (머리 모양 %.2f)"), Value), Target->TryApply(Profile), ECustomizationFailure::None)) { continue; }
        for (const ECustomizationSlot Slot : GetCustomizationSlots())
        {
            const FCustomizationPartResources Resources = Target->GetDefaultResources(Profile.GetSlotPartId(Slot));
            const FVector Expected = DataOffset(Resources, BodyMorphs[0]) * (1.f - Value);
            const UMeshComponent* Mesh = Target->GetDisplayedSlotMesh(Slot);
            const FVector Actual = Mesh ? Mesh->GetRelativeLocation() : FVector(UE_BIG_NUMBER);
            TestTrue(*FString::Printf(TEXT("%s (머리 모양 %.2f): 위치 (%.2f, %.2f, %.2f) / 기대 (%.2f, %.2f, %.2f)"),
                *GetCustomizationSlotText(Slot).ToString(), Value, Actual.X, Actual.Y, Actual.Z, Expected.X, Expected.Y, Expected.Z),
                Actual.Equals(Expected, KINDA_SMALL_NUMBER));
        }
    }

    // 3) 같은 값의 재적용은 component를 다시 만들지 않는다(옮긴 위치를 재사용 판정에 반영).
    Profile.HeadShape = 0.f;
    TestEqual(TEXT("가장 납작한 머리 적용"), Target->TryApply(Profile), ECustomizationFailure::None);
    const FDisplaySnapshot Applied(Target);
    TestEqual(TEXT("같은 값 재적용"), Target->TryApply(Profile), ECustomizationFailure::None);
    TestTrue(TEXT("재적용은 표시를 그대로 둔다"), FDisplaySnapshot(Target) == Applied);

    // 4) 다른 슬롯이 리소스 계약 단계에서 실패하면 머리 모양을 바꾼 후보의 헤어 위치 보정도 적용되지 않는다.
    UMeshComponent* Hair = Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair);
    if (!TestNotNull(TEXT("헤어 표시"), Hair)) { return false; }
    const FVector HairLocation = Hair->GetRelativeLocation();
    FCustomizationPartResources Broken = Target->GetDefaultResources(TEXT("TailFan"));
    Broken.Mesh.Reset();
    Target->SetResourcesForTests(TEXT("TailFan"), Broken);
    FCustomizationProfile Rounder = Profile;
    Rounder.HeadShape = 1.f;
    TestEqual(TEXT("꼬리 슬롯 실패"), Target->TryApply(Rounder), ECustomizationFailure::MissingMesh);
    TestTrue(TEXT("실패하면 헤어 component와 위치가 그대로다"), FDisplaySnapshot(Target) == Applied
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair) == Hair && Hair->GetRelativeLocation() == HairLocation);
    Target->SetResourcesForTests(TEXT("TailFan"), Target->GetDefaultResources(TEXT("TailFan")));
    TestEqual(TEXT("복구 뒤 성공"), Target->TryApply(Rounder), ECustomizationFailure::None);
    TestTrue(TEXT("복구 뒤 헤어가 원래 높이로 돌아간다"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair)
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Hair)->GetRelativeLocation().IsNearlyZero());
    Target->ClearPreview();
    return !HasAnyErrors();
}
}
#endif
