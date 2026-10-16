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

// 허용 오차(cm)다. 접촉 이동은 장식 단면 두께에 걸친 정점의 평균이라 그만큼 흩어진다.
constexpr float Tolerance = 0.3f;
// 장식 정점과 이 거리(cm) 안에 있는 body 정점을 장식이 기대는 정점으로 본다.
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

// 장식이 기대는 body 정점의 평균 이동이다. 장식 정점마다 이 Morph가 움직이는 가장 가까운 body 정점을 찾고,
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

// 장식 정점(Offset만큼 옮긴)과 body 정점(이 Morph를 Weight만큼 적용한) 사이의 최소 간격이다.
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

// 이 Morph가 움직이는 정점 중 가장 앞(+X)에 있는 정점의 이동이다. 눈처럼 중심에서 커지는 부위의 앞면 이동을 본다.
FVector FrontShift(const TArray<FVector3f>& Body, TConstArrayView<const FMorphTargetDelta*> Moving)
{
    const FMorphTargetDelta* Front = nullptr;
    for (const FMorphTargetDelta* Delta : Moving)
    {
        if (!Front || Body[Delta->SourceIdx].X > Body[Front->SourceIdx].X) { Front = Delta; }
    }
    return Front ? FVector(Front->PositionDelta) : FVector::ZeroVector;
}

// 적용 component와 같은 순서(GetCustomizationBodyMorphs)의 body Morph weight다.
float BodyWeight(const FCustomizationProfile& Profile, int32 Index)
{
    const float Values[] = {Profile.EyeSize, Profile.BeakLength, Profile.BodyLength};
    const float Value = Values[Index / 2];
    return Index % 2 == 0 ? FMath::Max(-Value, 0.f) : FMath::Max(Value, 0.f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAccessoriesFollowBodyMorphs, "Duckov.Customization.Slots.AccessoriesFollowBodyMorphs", Flags)
bool FAccessoriesFollowBodyMorphs::RunTest(const FString& Parameters)
{
    LoadCatalogAssets();
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    const USkeletalMesh* BodyMesh = Cast<USkeletalMesh>(GetDefaultCustomizationBodyMesh().TryLoad());
    if (!TestTrue(TEXT("카탈로그와 body"), Catalog.IsAvailable() && BodyMesh)) { return false; }
    const TArray<FVector3f> Body = ReferencePositions(BodyMesh);
    const TConstArrayView<FName> BodyMorphs = GetCustomizationBodyMorphs();

    // 1) 모든 등록 파츠 × body Morph 극값(weight 1)의 형상 검사. 어느 Morph를 따를지는 데이터(사람의 의도)가 정한다.
    //    따르는 Morph는 이동량이 장식이 기대는 body 정점의 평균 이동(접촉 이동)과 같아야 한다(기준 정점에 붙어 있음).
    //    따르지 않는 Morph의 측정값과 간격은 판단 근거로 기록만 한다. 예: 안경 아래를 앞뒤로 미끄러지는 부리 윗면.
    //    정점 간 최소 간격은 관통을 판정하지 못해(정점 사이로 지나갈 수 있다) 검사 조건으로 쓰지 않는다.
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
                const FVector Front = FrontShift(Body, Moving);
                const FVector Offset = DataOffset(Row->Resources, MorphName);
                const bool bFollows = Row->Resources.FollowBodyMorphs.ContainsByPredicate(
                    [MorphName](const FCustomizationMorphFollow& Item) { return Item.BodyMorph == MorphName; });
                const float NeutralAll = MinClearance(Body, Moving, PartPositions, 0.f, FVector::ZeroVector, false);
                const float ExtremeAll = MinClearance(Body, Moving, PartPositions, 1.f, Offset, false);
                const float NeutralRegion = MinClearance(Body, Moving, PartPositions, 0.f, FVector::ZeroVector, true);
                const float Corrected = MinClearance(Body, Moving, PartPositions, 1.f, Offset, true);
                const float Uncorrected = MinClearance(Body, Moving, PartPositions, 1.f, FVector::ZeroVector, true);
                const FString Label = FString::Printf(
                    TEXT("%s × %s: 접촉 이동 (%.2f, %.2f, %.2f), 앞면 이동 (%.2f, %.2f, %.2f), 데이터 (%.2f, %.2f, %.2f), ")
                    TEXT("부위 간격 %.2f → 보정 %.2f / 미보정 %.2f, 전체 간격 %.2f → %.2f cm"),
                    *PartId.ToString(), *MorphName.ToString(), Contact.X, Contact.Y, Contact.Z, Front.X, Front.Y, Front.Z,
                    Offset.X, Offset.Y, Offset.Z, NeutralRegion, Corrected, Uncorrected, NeutralAll, ExtremeAll);
                AddInfo(Label);
                if (bFollows)
                {
                    // 새 파츠를 등록할 때 접촉 이동 측정값을 JSON의 FollowBodyMorphs에 적는다.
                    TestTrue(Label + TEXT(" / 따르는 Morph의 이동량은 접촉 이동과 같다"),
                        !Contact.IsNearlyZero(Tolerance) && FVector::Dist(Contact, Offset) <= Tolerance);
                }
                // 몸통 길이는 머리 장식·눈·부리 장식이 기대는 정점을 움직이지 않는다.
                if (MorphName.ToString().StartsWith(TEXT("Body")))
                {
                    TestTrue(Label + TEXT(" / 몸통 Morph는 장식에 닿지 않는다"), Contact.IsNearlyZero(Tolerance) && !bFollows);
                }
                // 고친 증상이 실제로 크다는 것을 고정한다. 보정이 없으면 부리를 늘렸을 때 고리가 수 cm 남는다.
                if (PartId == TEXT("BeakRing") && MorphName == TEXT("BeakLong"))
                {
                    TestTrue(Label + TEXT(" / 부리 고리는 부리 길이 최대에서 3 cm 넘게 옮겨진다"), bFollows && Contact.Size() > 3.0);
                }
                // 눈은 중심에서 커지므로 안경 근처 눈 정점의 평균 이동은 작다(따르지 않은 근거). 평균이라 상쇄가 있고
                // 가장 앞 정점은 최대 1 cm 나오므로, 눈 크기 최대에서 안경과 겹쳐 보이는지는 이 검사가 아니라 PIE로 확인한다.
                if (PartId == TEXT("RoundGlasses") && MorphName.ToString().StartsWith(TEXT("Eye")))
                {
                    TestTrue(Label + TEXT(" / 안경 근처 눈 정점의 평균 이동이 허용 오차 안"), Contact.IsNearlyZero(Tolerance) && !bFollows);
                }
            }
        }
    }

    // 2) 실제 적용: 최소·중간·최대와 조합에서 장식 위치가 현재 weight × 데이터 이동량의 합이다.
    FRealWorld Scene;
    UCustomizationAppearanceComponent* Target = Scene.CreateTarget();
    const TPair<ECustomizationSlot, FName> Accessories[] = {{ECustomizationSlot::Eyewear, TEXT("RoundGlasses")},
        {ECustomizationSlot::Beak, TEXT("BeakRing")}};
    FCustomizationProfile Profile;
    Profile.EyewearId = TEXT("RoundGlasses");
    Profile.BeakWearId = TEXT("BeakRing");
    for (const float Value : {-1.f, -0.5f, 0.f, 0.5f, 1.f})
    {
        for (const float BodyLength : {-1.f, 0.f, 1.f})
        {
            Profile.EyeSize = Value;
            Profile.BeakLength = -Value;
            Profile.BodyLength = BodyLength;
            if (!TestEqual(*FString::Printf(TEXT("적용 (눈 %.1f, 부리 %.1f, 몸통 %.1f)"), Value, -Value, BodyLength),
                Target->TryApply(Profile), ECustomizationFailure::None)) { continue; }
            for (const TPair<ECustomizationSlot, FName>& Accessory : Accessories)
            {
                const FCustomizationPartResources Resources = Target->GetDefaultResources(Accessory.Value);
                FVector Expected = FVector::ZeroVector;
                for (int32 Index = 0; Index < BodyMorphs.Num(); ++Index)
                {
                    Expected += DataOffset(Resources, BodyMorphs[Index]) * BodyWeight(Profile, Index);
                }
                const UMeshComponent* Mesh = Target->GetDisplayedSlotMesh(Accessory.Key);
                const FVector Actual = Mesh ? Mesh->GetRelativeLocation() : FVector(UE_BIG_NUMBER);
                TestTrue(*FString::Printf(TEXT("%s (눈 %.1f, 부리 %.1f, 몸통 %.1f): 위치 (%.2f, %.2f, %.2f) / 기대 (%.2f, %.2f, %.2f)"),
                    *Accessory.Value.ToString(), Value, -Value, BodyLength, Actual.X, Actual.Y, Actual.Z, Expected.X, Expected.Y, Expected.Z),
                    Actual.Equals(Expected, KINDA_SMALL_NUMBER));
            }
        }
    }

    // 3) 같은 값의 재적용은 component를 다시 만들지 않는다(옮긴 위치를 재사용 판정에 반영).
    Profile.EyeSize = 1.f; Profile.BeakLength = 1.f; Profile.BodyLength = 0.f;
    TestEqual(TEXT("극값 적용"), Target->TryApply(Profile), ECustomizationFailure::None);
    const FDisplaySnapshot Applied(Target);
    TestEqual(TEXT("같은 극값 재적용"), Target->TryApply(Profile), ECustomizationFailure::None);
    TestTrue(TEXT("재적용은 표시를 그대로 둔다"), FDisplaySnapshot(Target) == Applied);

    // 4) 다른 슬롯이 리소스 계약 단계에서 실패하면 부리 길이를 바꾼 후보의 위치 보정도 적용되지 않는다.
    //    준비 후반(render 준비) 실패의 보존은 기존 실패 보존 테스트가 다룬다.
    UMeshComponent* Ring = Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak);
    if (!TestNotNull(TEXT("부리 고리 표시"), Ring)) { return false; }
    const FVector RingLocation = Ring->GetRelativeLocation();
    FCustomizationPartResources Broken = Target->GetDefaultResources(TEXT("RoundGlasses"));
    Broken.Mesh.Reset();
    Target->SetResourcesForTests(TEXT("RoundGlasses"), Broken);
    FCustomizationProfile Shorter = Profile;
    Shorter.BeakLength = -1.f;
    TestEqual(TEXT("눈 슬롯 실패"), Target->TryApply(Shorter), ECustomizationFailure::MissingMesh);
    TestTrue(TEXT("실패하면 부리 고리 component와 위치가 그대로다"), FDisplaySnapshot(Target) == Applied
        && Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak) == Ring && Ring->GetRelativeLocation() == RingLocation);
    Target->SetResourcesForTests(TEXT("RoundGlasses"), Target->GetDefaultResources(TEXT("RoundGlasses")));
    TestEqual(TEXT("복구 뒤 성공"), Target->TryApply(Shorter), ECustomizationFailure::None);
    TestTrue(TEXT("복구 뒤 부리 고리 위치가 바뀐다"), Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak)
        && !Target->GetDisplayedSlotMesh(ECustomizationSlot::Beak)->GetRelativeLocation().Equals(RingLocation));
    Target->ClearPreview();
    return !HasAnyErrors();
}
}
#endif
