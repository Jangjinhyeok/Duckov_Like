#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "CustomizationCatalog.h"
#include "CustomizationPartContract.h"
#include "DuckovCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "Rendering/SkeletalMeshModel.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// 플레이어 걷기 애니메이션 에셋이다. AnimSequence는 pilot에서 FBX로 import하고(import_animation.py),
// BlendSpace는 Python에서 sample을 넣을 수 없어 여기서 만든다.
namespace CharacterAnimationAssetsTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
const TCHAR* IdlePath = TEXT("/Game/Character/A_DuckIdle.A_DuckIdle");
const TCHAR* WalkPath = TEXT("/Game/Character/A_DuckWalk.A_DuckWalk");
const TCHAR* IdleRiflePath = TEXT("/Game/Character/A_DuckIdleRifle.A_DuckIdleRifle");
const TCHAR* WalkRiflePath = TEXT("/Game/Character/A_DuckWalkRifle.A_DuckWalkRifle");
// 기본 동작과 총을 쥔 동작은 축·sample 배치가 같고 쓰는 AnimSequence만 다르다.
struct FLocomotionSpec
{
    const TCHAR* BlendSpacePath;
    const TCHAR* Idle;
    const TCHAR* Walk;
};
const FLocomotionSpec Locomotions[] = {
    {ADuckovCharacter::LocomotionAnimationPath, IdlePath, WalkPath},
    {ADuckovCharacter::RifleLocomotionAnimationPath, IdleRiflePath, WalkRiflePath},
};
const TCHAR* SpeedAxis = TEXT("Speed");
// 속도 변화를 부드럽게 따라가는 시간이다. 걷기 시작·정지가 한 frame에 튀지 않는다.
constexpr float InputSmoothingSeconds = 0.15f;
// 달리기는 걷기 동작을 속도 비율만큼 빨리 재생한다.
constexpr float SprintRateScale = ADuckovCharacter::SprintSpeed / ADuckovCharacter::WalkSpeed;

bool SaveAsset(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

// 축 설정과 sample 배열은 protected라 reflection으로 접근한다(에디터 detail panel과 같은 경로).
FBlendParameter& SpeedParameter(UBlendSpace* Space)
{
    const FStructProperty* Property = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));
    return *Property->ContainerPtrToValuePtr<FBlendParameter>(Space, 0);
}

TArray<FBlendSample>& Samples(UBlendSpace* Space)
{
    const FArrayProperty* Property = FindFProperty<FArrayProperty>(UBlendSpace::StaticClass(), TEXT("SampleData"));
    return *Property->ContainerPtrToValuePtr<TArray<FBlendSample>>(Space);
}

// mesh LOD0의 section들이 실제로 skin된 bone 이름이다.
TSet<FName> SkinnedBones(const USkeletalMesh* Mesh)
{
    TSet<FName> Result;
    for (const FSkelMeshSection& Section : Mesh->GetImportedModel()->LODModels[0].Sections)
    {
        for (const FBoneIndexType Bone : Section.BoneMap) { Result.Add(Mesh->GetRefSkeleton().GetBoneName(Bone)); }
    }
    return Result;
}

// 지정한 속도에서 각 animation의 blend weight다.
TMap<FName, float> WeightsAt(const UBlendSpace* Space, float Speed)
{
    TArray<FBlendSampleData> Data;
    int32 Cache = INDEX_NONE;
    Space->GetSamplesFromBlendInput(FVector(Speed, 0.0f, 0.0f), Data, Cache, true);
    TMap<FName, float> Result;
    for (const FBlendSampleData& Sample : Data)
    {
        Result.FindOrAdd(Space->GetBlendSample(Sample.SampleDataIndex).Animation->GetFName()) += Sample.GetClampedWeight();
    }
    return Result;
}

// 기본 동작 BlendSpace와 같은 축·sample 배치로 하나를 만들어 저장한다.
bool CreateLocomotion(FAutomationTestBase& Test, const FString& PackageName, const TCHAR* IdleAsset, const TCHAR* WalkAsset)
{
    UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, IdleAsset);
    UAnimSequence* Walk = LoadObject<UAnimSequence>(nullptr, WalkAsset);
    if (!Test.TestNotNull(TEXT("Idle"), Idle) || !Test.TestNotNull(TEXT("Walk"), Walk)) { return false; }
    UPackage* Package = CreatePackage(*PackageName);
    UBlendSpace1D* Space = NewObject<UBlendSpace1D>(Package, FName(FPackageName::GetShortName(PackageName)), RF_Public | RF_Standalone);
    Space->SetSkeleton(Idle->GetSkeleton());
    FBlendParameter& Speed = SpeedParameter(Space);
    Speed.DisplayName = SpeedAxis;
    Speed.Min = 0.0f;
    Speed.Max = ADuckovCharacter::SprintSpeed;
    Speed.GridNum = 12;
    Speed.bSnapToGrid = false;
    Space->InterpolationParam[0].InterpolationTime = InputSmoothingSeconds;
    Space->AddSample(Idle, FVector::ZeroVector);
    Space->AddSample(Walk, FVector(ADuckovCharacter::WalkSpeed, 0.0f, 0.0f));
    const int32 Sprint = Space->AddSample(Walk, FVector(ADuckovCharacter::SprintSpeed, 0.0f, 0.0f));
    if (!Test.TestEqual(TEXT("sample 3개 추가"), Space->GetBlendSamples().Num(), 3)) { return false; }
    Samples(Space)[Sprint].RateScale = SprintRateScale;
    Space->ValidateSampleData();
    Space->ResampleData();
    return Test.TestTrue(*FString::Printf(TEXT("%s 저장"), *PackageName), SaveAsset(Space));
}
}

// 명시적으로 한 번 실행하는 생성 경로다. 없는 BlendSpace만 만들고 이미 있는 것은 덮어쓰지 않는다.
// unity build에서 다른 테스트의 Flags와 겹치지 않도록 namespace를 함수 안에서만 연다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCreateCharacterAnimationAssets, "CharacterAnimationAssets.Create", CharacterAnimationAssetsTests::Flags)
bool FCreateCharacterAnimationAssets::RunTest(const FString& Parameters)
{
    using namespace CharacterAnimationAssetsTests;
    int32 Created = 0;
    for (const FLocomotionSpec& Spec : Locomotions)
    {
        const FString PackageName = FPackageName::ObjectPathToPackageName(FString(Spec.BlendSpacePath));
        if (FPackageName::DoesPackageExist(PackageName))
        {
            AddInfo(FString::Printf(TEXT("%s는 이미 있어 덮어쓰지 않습니다."), *PackageName));
            continue;
        }
        if (!CreateLocomotion(*this, PackageName, Spec.Idle, Spec.Walk)) { return false; }
        ++Created;
    }
    return TestTrue(TEXT("새로 만든 BlendSpace가 있음"), Created > 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerifyCharacterAnimationAssets, "CharacterAnimationAssets.Verify", CharacterAnimationAssetsTests::Flags)
bool FVerifyCharacterAnimationAssets::RunTest(const FString& Parameters)
{
    using namespace CharacterAnimationAssetsTests;
    // 공통 Skeleton: 기존 5 bones에 발목 bone 두 개를 root 아래에 더했다.
    const USkeletalMesh* Body = Cast<USkeletalMesh>(GetDefaultCustomizationBodyMesh().TryLoad());
    if (!TestNotNull(TEXT("고정 몸통"), Body)) { return false; }
    const USkeleton* Skeleton = Body->GetSkeleton();
    const FReferenceSkeleton& Bones = Skeleton->GetReferenceSkeleton();
    TestEqual(TEXT("Skeleton bone 수"), Bones.GetNum(), 7);
    for (const TCHAR* Foot : {TEXT("foot_l"), TEXT("foot_r")})
    {
        const int32 Index = Bones.FindBoneIndex(Foot);
        TestTrue(*FString::Printf(TEXT("%s는 root 아래"), Foot), Index != INDEX_NONE && Bones.GetParentIndex(Index) == 0);
    }
    // body와 발은 7 bones로 다시 import했고, 발 mesh만 발 bone에 skin된다. 다른 부위는 5 bones 그대로 같은 Skeleton이다.
    const TSet<FName> FootBones = {TEXT("foot_l"), TEXT("foot_r")};
    TestEqual(TEXT("body mesh bone 수"), Body->GetRefSkeleton().GetNum(), 7);
    TestTrue(TEXT("body는 발 bone에 skin되지 않음"), SkinnedBones(Body).Intersect(FootBones).IsEmpty());
    const FCustomizationCatalog Catalog = FCustomizationCatalog::LoadDefault();
    for (const ECustomizationSlot Slot : GetCustomizationSlots())
    {
        for (const FName PartId : Catalog.GetPartIds(Slot))
        {
            const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Catalog.Find(PartId)->Resources.Mesh.TryLoad());
            if (!TestNotNull(*PartId.ToString(), Mesh)) { continue; }
            TestTrue(*FString::Printf(TEXT("%s: 공통 Skeleton"), *PartId.ToString()), Mesh->GetSkeleton() == Skeleton);
            if (Slot == ECustomizationSlot::Foot)
            {
                TestTrue(*FString::Printf(TEXT("%s: 발 bone에만 skin"), *PartId.ToString()),
                    Mesh->GetRefSkeleton().GetNum() == 7 && SkinnedBones(Mesh).Difference(FootBones).IsEmpty()
                    && SkinnedBones(Mesh).Num() == 2);
            }
            else
            {
                TestTrue(*FString::Printf(TEXT("%s: 발 bone 없음"), *PartId.ToString()), SkinnedBones(Mesh).Intersect(FootBones).IsEmpty());
            }
        }
    }
    // 기본 동작과 총을 쥔 동작은 같은 축·sample 배치의 BlendSpace다.
    for (const FLocomotionSpec& Spec : Locomotions)
    {
        // 30 fps 제자리 루프. 마지막 frame이 첫 frame과 같아 이음매 없이 반복된다.
        const UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, Spec.Idle);
        const UAnimSequence* Walk = LoadObject<UAnimSequence>(nullptr, Spec.Walk);
        if (!TestNotNull(Spec.Idle, Idle) || !TestNotNull(Spec.Walk, Walk)) { return false; }
        for (const UAnimSequence* Anim : {Idle, Walk})
        {
            TestTrue(*FString::Printf(TEXT("%s: 공통 Skeleton·30 fps·root motion 없음"), *Anim->GetName()), Anim->GetSkeleton() == Skeleton
                && Anim->GetSamplingFrameRate() == FFrameRate(30, 1) && !Anim->bEnableRootMotion);
        }
        // Idle 첫 frame은 reference pose와 같은 방향이다. FBX import가 root를 90° 돌려 외형 전체가 옆을 본 회귀를 막는다.
        for (const TCHAR* Name : {TEXT("root"), TEXT("body")})
        {
            const int32 Index = Bones.FindBoneIndex(Name);
            FTransform First;
            Idle->GetBoneTransform(First, FSkeletonPoseBoneIndex(Index), FAnimExtractContext(0.0), false);
            TestTrue(*FString::Printf(TEXT("%s 첫 frame %s 방향이 reference pose와 같음"), *Idle->GetName(), Name), Index != INDEX_NONE
                && First.GetRotation().AngularDistance(Bones.GetRefBonePose()[Index].GetRotation()) < FMath::DegreesToRadians(1.0));
        }
        TestTrue(*FString::Printf(TEXT("%s 2초"), *Idle->GetName()), FMath::IsNearlyEqual(Idle->GetPlayLength(), 2.0, 1e-3));
        TestTrue(*FString::Printf(TEXT("%s 16 frame"), *Walk->GetName()), FMath::IsNearlyEqual(Walk->GetPlayLength(), 16.0 / 30.0, 1e-3));
        // BlendSpace: 0=Idle, 걷기 속도=Walk, 달리기 속도=Walk를 빠르게.
        UBlendSpace1D* Space = LoadObject<UBlendSpace1D>(nullptr, Spec.BlendSpacePath);
        if (!TestNotNull(Spec.BlendSpacePath, Space)) { return false; }
        const FString Prefix = Space->GetName();
        const FBlendParameter& Speed = Space->GetBlendParameter(0);
        TestTrue(*(Prefix + TEXT(": Speed 축 0..달리기 속도")), Speed.DisplayName == SpeedAxis && Speed.Min == 0.0f && Speed.Max == ADuckovCharacter::SprintSpeed);
        TestTrue(*(Prefix + TEXT(": 공통 Skeleton·반복")), Space->GetSkeleton() == Skeleton && Space->bLoop);
        TestEqual(*(Prefix + TEXT(": 입력 smoothing")), Space->InterpolationParam[0].InterpolationTime, InputSmoothingSeconds);
        const TArray<FBlendSample>& Data = Space->GetBlendSamples();
        if (!TestEqual(*(Prefix + TEXT(": sample 수")), Data.Num(), 3)) { return false; }
        TestTrue(*(Prefix + TEXT(": Idle@0")), Data[0].Animation == Idle && Data[0].SampleValue.X == 0.0f && Data[0].RateScale == 1.0f);
        TestTrue(*(Prefix + TEXT(": Walk@걷기")), Data[1].Animation == Walk && Data[1].SampleValue.X == ADuckovCharacter::WalkSpeed && Data[1].RateScale == 1.0f);
        TestTrue(*(Prefix + TEXT(": Walk@달리기")), Data[2].Animation == Walk && Data[2].SampleValue.X == ADuckovCharacter::SprintSpeed
            && FMath::IsNearlyEqual(Data[2].RateScale, SprintRateScale));
        const TMap<FName, float> Still = WeightsAt(Space, 0.0f);
        const TMap<FName, float> Half = WeightsAt(Space, ADuckovCharacter::WalkSpeed * 0.5f);
        const TMap<FName, float> Running = WeightsAt(Space, ADuckovCharacter::SprintSpeed);
        TestTrue(*(Prefix + TEXT(": 정지는 Idle만")), Still.Num() == 1 && FMath::IsNearlyEqual(Still.FindRef(Idle->GetFName()), 1.0f));
        TestTrue(*(Prefix + TEXT(": 걷기 절반 속도는 Idle·Walk 반반")), FMath::IsNearlyEqual(Half.FindRef(Idle->GetFName()), 0.5f, 1e-3f)
            && FMath::IsNearlyEqual(Half.FindRef(Walk->GetFName()), 0.5f, 1e-3f));
        TestTrue(*(Prefix + TEXT(": 달리기는 Walk만")), Running.Num() == 1 && FMath::IsNearlyEqual(Running.FindRef(Walk->GetFName()), 1.0f));
    }
    // 휘두르기는 한 번 재생하는 10 frame 동작이다. rest에서 시작해 rest로 끝나 이동 동작과 이어 붙여도 튀지 않고,
    // 중간에는 오른날개를 크게 돌려 방망이를 휘두른다. 발은 움직이지 않는다.
    // rest는 Idle 첫 frame으로 잡고, 압축 데이터가 아니라 원본 key로 비교한다. 방금 추가한 에셋은 압축 데이터가
    // 아직 준비되지 않아 압축 경로로 읽으면 처음과 끝이 모두 엉뚱한 같은 값(약 145°)으로 나왔다(2026-11-17).
    const UAnimSequence* Swing = LoadObject<UAnimSequence>(nullptr, ADuckovCharacter::MeleeSwingAnimationPath);
    const UAnimSequence* Rest = LoadObject<UAnimSequence>(nullptr, IdlePath);
    if (TestNotNull(TEXT("휘두르기"), Swing) && TestNotNull(TEXT("rest 기준 Idle"), Rest))
    {
        TestTrue(TEXT("휘두르기: 공통 Skeleton·30 fps·root motion 없음"), Swing->GetSkeleton() == Skeleton
            && Swing->GetSamplingFrameRate() == FFrameRate(30, 1) && !Swing->bEnableRootMotion);
        TestTrue(TEXT("휘두르기 10 frame"), FMath::IsNearlyEqual(Swing->GetPlayLength(), 10.0 / 30.0, 1e-3));
        auto DegreesFromRest = [&Bones, Swing, Rest](const TCHAR* Name, double Time)
        {
            const int32 Index = Bones.FindBoneIndex(Name);
            FTransform Pose, RestPose;
            Swing->GetBoneTransform(Pose, FSkeletonPoseBoneIndex(Index), FAnimExtractContext(Time), true);
            Rest->GetBoneTransform(RestPose, FSkeletonPoseBoneIndex(Index), FAnimExtractContext(0.0), true);
            return FMath::RadiansToDegrees(Pose.GetRotation().AngularDistance(RestPose.GetRotation()));
        };
        for (const TCHAR* Name : {TEXT("root"), TEXT("body"), TEXT("wing_r"), TEXT("foot_l"), TEXT("foot_r")})
        {
            const double Start = DegreesFromRest(Name, 0.0);
            const double End = DegreesFromRest(Name, Swing->GetPlayLength());
            TestTrue(*FString::Printf(TEXT("휘두르기 처음·끝의 %s는 rest(처음 %.2f°, 끝 %.2f°)"), Name, Start, End), Start < 1.0 && End < 1.0);
        }
        TestTrue(TEXT("휘두르는 중 오른날개를 크게 돌림"), DegreesFromRest(TEXT("wing_r"), Swing->GetPlayLength() * 0.55) > 45.0);
        TestTrue(TEXT("휘두르는 중 발은 그대로"), DegreesFromRest(TEXT("foot_l"), Swing->GetPlayLength() * 0.55) < 1.0);
    }
    // 총을 쥔 동작은 날개만 다르다. 몸·머리·발은 기본 동작과 같아 총을 들고 바꿔도 걸음이 어긋나지 않는다.
    const TPair<const TCHAR*, const TCHAR*> Pairs[] = {{IdlePath, IdleRiflePath}, {WalkPath, WalkRiflePath}};
    for (const TPair<const TCHAR*, const TCHAR*>& Pair : Pairs)
    {
        const UAnimSequence* Base = LoadObject<UAnimSequence>(nullptr, Pair.Key);
        const UAnimSequence* Rifle = LoadObject<UAnimSequence>(nullptr, Pair.Value);
        if (!Base || !Rifle) { continue; }
        for (int32 Index = 0; Index < Bones.GetNum(); ++Index)
        {
            const FName Name = Bones.GetBoneName(Index);
            const bool bWing = Name == TEXT("wing_l") || Name == TEXT("wing_r");
            double MaxAngle = 0.0;
            double MaxOffset = 0.0;
            for (const double Time : {0.0, Base->GetPlayLength() * 0.25, Base->GetPlayLength() * 0.5})
            {
                FTransform A, B;
                Base->GetBoneTransform(A, FSkeletonPoseBoneIndex(Index), FAnimExtractContext(Time), false);
                Rifle->GetBoneTransform(B, FSkeletonPoseBoneIndex(Index), FAnimExtractContext(Time), false);
                MaxAngle = FMath::Max(MaxAngle, static_cast<double>(A.GetRotation().AngularDistance(B.GetRotation())));
                MaxOffset = FMath::Max(MaxOffset, FVector::Dist(A.GetLocation(), B.GetLocation()));
            }
            if (bWing)
            {
                // 늘어진 날개를 앞으로 들어 올렸으므로 기본 동작과 크게 다르다.
                TestTrue(*FString::Printf(TEXT("%s: %s를 앞으로 듦"), *Rifle->GetName(), *Name.ToString()), MaxAngle > FMath::DegreesToRadians(45.0));
            }
            else
            {
                TestTrue(*FString::Printf(TEXT("%s: %s는 기본 동작과 같음"), *Rifle->GetName(), *Name.ToString()),
                    MaxAngle < FMath::DegreesToRadians(0.1) && MaxOffset < 0.01);
            }
        }
    }
    return true;
}
#endif
