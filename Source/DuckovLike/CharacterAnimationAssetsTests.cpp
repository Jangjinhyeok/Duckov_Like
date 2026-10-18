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
const TCHAR* BlendSpacePackage = TEXT("/Game/Character/BS_DuckLocomotion");
const TCHAR* IdlePath = TEXT("/Game/Character/A_DuckIdle.A_DuckIdle");
const TCHAR* WalkPath = TEXT("/Game/Character/A_DuckWalk.A_DuckWalk");
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
}

using namespace CharacterAnimationAssetsTests;

// 명시적으로 한 번 실행하는 생성 경로다. 이미 있으면 덮어쓰지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCreateCharacterAnimationAssets, "CharacterAnimationAssets.Create", Flags)
bool FCreateCharacterAnimationAssets::RunTest(const FString& Parameters)
{
    if (FPackageName::DoesPackageExist(BlendSpacePackage))
    {
        AddError(TEXT("기존 BlendSpace를 덮어쓰지 않습니다."));
        return false;
    }
    UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, IdlePath);
    UAnimSequence* Walk = LoadObject<UAnimSequence>(nullptr, WalkPath);
    if (!TestNotNull(TEXT("Idle"), Idle) || !TestNotNull(TEXT("Walk"), Walk)) { return false; }
    UPackage* Package = CreatePackage(BlendSpacePackage);
    UBlendSpace1D* Space = NewObject<UBlendSpace1D>(Package, FName(FPackageName::GetShortName(BlendSpacePackage)), RF_Public | RF_Standalone);
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
    if (!TestEqual(TEXT("sample 3개 추가"), Space->GetBlendSamples().Num(), 3)) { return false; }
    Samples(Space)[Sprint].RateScale = SprintRateScale;
    Space->ValidateSampleData();
    Space->ResampleData();
    return TestTrue(TEXT("BlendSpace 저장"), SaveAsset(Space));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerifyCharacterAnimationAssets, "CharacterAnimationAssets.Verify", Flags)
bool FVerifyCharacterAnimationAssets::RunTest(const FString& Parameters)
{
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
    // 30 fps 제자리 루프. 마지막 frame이 첫 frame과 같아 이음매 없이 반복된다.
    const UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, IdlePath);
    const UAnimSequence* Walk = LoadObject<UAnimSequence>(nullptr, WalkPath);
    if (!TestNotNull(TEXT("Idle"), Idle) || !TestNotNull(TEXT("Walk"), Walk)) { return false; }
    for (const UAnimSequence* Anim : {Idle, Walk})
    {
        TestTrue(*FString::Printf(TEXT("%s: 공통 Skeleton·30 fps·root motion 없음"), *Anim->GetName()), Anim->GetSkeleton() == Skeleton
            && Anim->GetSamplingFrameRate() == FFrameRate(30, 1) && !Anim->bEnableRootMotion);
    }
    TestTrue(TEXT("Idle 2초"), FMath::IsNearlyEqual(Idle->GetPlayLength(), 2.0, 1e-3));
    TestTrue(TEXT("Walk 16 frame"), FMath::IsNearlyEqual(Walk->GetPlayLength(), 16.0 / 30.0, 1e-3));
    // BlendSpace: 0=Idle, 걷기 속도=Walk, 달리기 속도=Walk를 빠르게.
    UBlendSpace1D* Space = LoadObject<UBlendSpace1D>(nullptr, ADuckovCharacter::LocomotionAnimationPath);
    if (!TestNotNull(TEXT("BlendSpace"), Space)) { return false; }
    const FBlendParameter& Speed = Space->GetBlendParameter(0);
    TestTrue(TEXT("Speed 축 0..달리기 속도"), Speed.DisplayName == SpeedAxis && Speed.Min == 0.0f && Speed.Max == ADuckovCharacter::SprintSpeed);
    TestTrue(TEXT("공통 Skeleton·반복"), Space->GetSkeleton() == Skeleton && Space->bLoop);
    TestEqual(TEXT("입력 smoothing"), Space->InterpolationParam[0].InterpolationTime, InputSmoothingSeconds);
    const TArray<FBlendSample>& Data = Space->GetBlendSamples();
    if (!TestEqual(TEXT("sample 수"), Data.Num(), 3)) { return false; }
    TestTrue(TEXT("Idle@0"), Data[0].Animation == Idle && Data[0].SampleValue.X == 0.0f && Data[0].RateScale == 1.0f);
    TestTrue(TEXT("Walk@걷기"), Data[1].Animation == Walk && Data[1].SampleValue.X == ADuckovCharacter::WalkSpeed && Data[1].RateScale == 1.0f);
    TestTrue(TEXT("Walk@달리기"), Data[2].Animation == Walk && Data[2].SampleValue.X == ADuckovCharacter::SprintSpeed
        && FMath::IsNearlyEqual(Data[2].RateScale, SprintRateScale));
    const TMap<FName, float> Still = WeightsAt(Space, 0.0f);
    const TMap<FName, float> Half = WeightsAt(Space, ADuckovCharacter::WalkSpeed * 0.5f);
    const TMap<FName, float> Running = WeightsAt(Space, ADuckovCharacter::SprintSpeed);
    TestTrue(TEXT("정지는 Idle만"), Still.Num() == 1 && FMath::IsNearlyEqual(Still.FindRef(Idle->GetFName()), 1.0f));
    TestTrue(TEXT("걷기 절반 속도는 Idle·Walk 반반"), FMath::IsNearlyEqual(Half.FindRef(Idle->GetFName()), 0.5f, 1e-3f)
        && FMath::IsNearlyEqual(Half.FindRef(Walk->GetFName()), 0.5f, 1e-3f));
    TestTrue(TEXT("달리기는 Walk만"), Running.Num() == 1 && FMath::IsNearlyEqual(Running.FindRef(Walk->GetFName()), 1.0f));
    return true;
}
#endif
