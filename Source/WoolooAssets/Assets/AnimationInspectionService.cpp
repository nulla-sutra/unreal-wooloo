#include "WoolooAssets/Assets/AnimationInspectionService.h"
#include "WoolooAssets/Assets/ImportService.h"

#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCompressionTypes.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace Wooloo::Assets
{
namespace
{
TSharedPtr<FJsonObject> AnimationTransformJson(const FTransform& Value)
{
    auto Row = MakeShared<FJsonObject>();
    const auto Position = Value.GetTranslation();
    const auto Rotation = Value.GetRotation();
    const auto Scale = Value.GetScale3D();
    const auto Numbers = [](std::initializer_list<double> Values)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const auto Number : Values) Result.Add(MakeShared<FJsonValueNumber>(Number));
        return Result;
    };
    Row->SetArrayField(TEXT("translation"), Numbers({Position.X, Position.Y, Position.Z}));
    Row->SetArrayField(TEXT("quaternion"), Numbers({Rotation.X, Rotation.Y, Rotation.Z, Rotation.W}));
    Row->SetArrayField(TEXT("scale"), Numbers({Scale.X, Scale.Y, Scale.Z}));
    return Row;
}

TSharedPtr<FJsonObject> AnimationCompressionObjectJson(const UObject* Object)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("path"), Object ? Object->GetPathName() : FString());
    Row->SetStringField(TEXT("class"), Object ? Object->GetClass()->GetPathName() : FString());
    auto Scalars = MakeShared<FJsonObject>();
    if (Object)
    {
        // Read editable scalar codec settings without invoking an editor property setter.
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            const auto* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->ArrayDim != 1) continue;
            const auto* Value = Property->ContainerPtrToValuePtr<void>(Object);
            if (const auto* Bool = CastField<FBoolProperty>(Property))
                Scalars->SetBoolField(Property->GetName(), Bool->GetPropertyValue(Value));
            else if (const auto* Number = CastField<FNumericProperty>(Property))
            {
                const auto Scalar = Number->IsFloatingPoint() ? Number->GetFloatingPointPropertyValue(Value)
                    : static_cast<double>(Number->GetSignedIntPropertyValue(Value));
                Scalars->SetNumberField(Property->GetName(), Scalar);
            }
            else if (const auto* Enum = CastField<FEnumProperty>(Property))
                Scalars->SetStringField(Property->GetName(), Enum->GetEnum()->GetNameStringByValue(
                    Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value)));
        }
    }
    Row->SetObjectField(TEXT("editable_scalars"), Scalars);
    return Row;
}
}

FWoolooInspectionResult InspectAnimation(const FString& AssetPath,
    const TArray<float>& SampleTimes, const TArray<FName>& BoneNames)
{
    FWoolooInspectionResult Out;
    if (!IsInGameThread() || !FPackageName::IsValidLongPackageName(AssetPath) || SampleTimes.Num() > 128)
    {
        Out.Message = TEXT("Inspect animation on the editor game thread with a package path and at most 128 times.");
        return Out;
    }
    const auto* Sequence = LoadObject<UAnimSequence>(nullptr, *Production::ObjectPath(AssetPath));
    if (!Sequence || !Sequence->GetSkeleton() || Sequence->IsValidAdditive())
    {
        Out.Message = TEXT("An existing non-additive AnimSequence with a Skeleton is required.");
        return Out;
    }
    const auto* Model = Sequence->IsDataModelValid() ? Sequence->GetDataModel() : nullptr;
    // CanEvaluateRawAnimationData is protected in this engine and delegates to
    // the public IsDataModelValid query in editor builds.
    if (!Model || Model->GetNumberOfKeys() < 1
        || Model->GetNumBoneTracks() < 1 || Model->GetFrameRate().AsDecimal() <= 0.)
    {
        Out.Message = TEXT("Native raw bone animation data is unavailable; no compressed fallback is performed.");
        return Out;
    }
    const auto* ForceRaw = IConsoleManager::Get().FindConsoleVariable(TEXT("a.ForceEvalRawData"));
    if (!Sequence->IsBoneCompressedDataValid() || Sequence->IsCompressedDataOutOfDate()
        || (ForceRaw && ForceRaw->GetInt() == 1))
    {
        Out.Message = TEXT("Current compressed data is not ready or raw evaluation is forced; no rebuild/fallback is performed.");
        return Out;
    }
    const auto& Ref = Sequence->GetSkeleton()->GetReferenceSkeleton();
    TSet<FName> Selected;
    for (const auto Name : BoneNames)
    {
        if (Ref.FindBoneIndex(Name) == INDEX_NONE || Selected.Contains(Name))
        {
            Out.Message = TEXT("Unknown or duplicate sample bone: ") + Name.ToString();
            return Out;
        }
        Selected.Add(Name);
    }
    for (const auto Time : SampleTimes)
    {
        if (!FMath::IsFinite(Time) || Time < 0.f || Time > Sequence->GetPlayLength())
        {
            Out.Message = TEXT("Sample time is non-finite or outside the animation interval.");
            return Out;
        }
    }
    auto Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetBoolField(TEXT("dirty"), Sequence->GetOutermost()->IsDirty());
    Data->SetStringField(TEXT("skeleton"), Sequence->GetSkeleton()->GetOutermost()->GetName());
    Data->SetNumberField(TEXT("duration"), Sequence->GetPlayLength());
    Data->SetNumberField(TEXT("fps"), Model->GetFrameRate().AsDecimal());
    Data->SetNumberField(TEXT("sampled_keys"), Model->GetNumberOfKeys());
    Data->SetNumberField(TEXT("raw_transform_curves"), Model->GetNumberOfTransformCurves());
    Data->SetStringField(TEXT("sample_units"), TEXT("centimeters; quaternion XYZW; local and component space"));
    Data->SetObjectField(TEXT("bone_compression_settings"), AnimationCompressionObjectJson(Sequence->BoneCompressionSettings));
    Data->SetObjectField(TEXT("curve_compression_settings"), AnimationCompressionObjectJson(Sequence->CurveCompressionSettings));
    Data->SetNumberField(TEXT("compression_error_threshold_scale"), Sequence->CompressionErrorThresholdScale);
    Data->SetBoolField(TEXT("allow_frame_stripping"), Sequence->bAllowFrameStripping);
    TArray<TSharedPtr<FJsonValue>> Codecs;
    if (Sequence->BoneCompressionSettings)
        for (const auto& Codec : Sequence->BoneCompressionSettings->Codecs)
            Codecs.Add(MakeShared<FJsonValueObject>(AnimationCompressionObjectJson(Codec)));
    Data->SetArrayField(TEXT("configured_bone_codecs"), Codecs);
    {
        // The native scoped accessor locks only while reading codec metadata.
        const auto Compressed = Sequence->GetCompressedData();
        if (!Compressed.Get().CompressedDataStructure || !Compressed.Get().BoneCompressionCodec)
        {
            Out.Message = TEXT("No current-platform bone codec/data structure is ready.");
            return Out;
        }
        Data->SetObjectField(TEXT("active_bone_codec"), AnimationCompressionObjectJson(Compressed.Get().BoneCompressionCodec));
        Data->SetNumberField(TEXT("compressed_byte_stream_bytes"), Compressed.Get().CompressedByteStream.Num());
    }
    TArray<FName> TrackNames;
    Model->GetBoneTrackNames(TrackNames);
    TSet<FName> Tracks;
    TArray<TSharedPtr<FJsonValue>> TrackValues;
    for (const auto Name : TrackNames)
    {
        Tracks.Add(Name);
        TrackValues.Add(MakeShared<FJsonValueString>(Name.ToString()));
    }
    Data->SetArrayField(TEXT("raw_tracks"), TrackValues);
    TArray<TSharedPtr<FJsonValue>> References;
    for (auto Index = 0; Index < Ref.GetNum(); ++Index)
    {
        auto Row = AnimationTransformJson(Ref.GetRefBonePose()[Index]);
        const auto Name = Ref.GetBoneName(Index);
        const auto Parent = Ref.GetParentIndex(Index);
        Row->SetStringField(TEXT("name"), Name.ToString());
        if (Parent == INDEX_NONE) Row->SetField(TEXT("parent"), MakeShared<FJsonValueNull>());
        else Row->SetStringField(TEXT("parent"), Ref.GetBoneName(Parent).ToString());
        Row->SetBoolField(TEXT("has_raw_track"), Tracks.Contains(Name));
        References.Add(MakeShared<FJsonValueObject>(Row));
    }
    Data->SetArrayField(TEXT("bones"), References);
    TArray<TSharedPtr<FJsonValue>> Samples;
    for (const auto Time : SampleTimes)
    {
        TArray<FTransform> RawComponent, CompressedComponent;
        RawComponent.SetNum(Ref.GetNum());
        CompressedComponent.SetNum(Ref.GetNum());
        auto Poses = MakeShared<FJsonObject>();
        FAnimExtractContext RawContext(static_cast<double>(Time), false);
        FAnimExtractContext CompressedContext(static_cast<double>(Time), false);
        CompressedContext.bEnforceCompressedDataSampling = true;
        for (auto Index = 0; Index < Ref.GetNum(); ++Index)
        {
            const auto Name = Ref.GetBoneName(Index);
            const auto Parent = Ref.GetParentIndex(Index);
            auto RawLocal = Ref.GetRefBonePose()[Index];
            auto CompressedLocal = RawLocal;
            // Native raw bone extraction assigns identity for a missing track.
            // Keep the reference fallback explicitly, as the runtime pose builder does.
            if (Tracks.Contains(Name)) Sequence->GetBoneTransform(RawLocal, FSkeletonPoseBoneIndex(Index), RawContext, true);
            Sequence->GetBoneTransform(CompressedLocal, FSkeletonPoseBoneIndex(Index), CompressedContext, false);
            RawComponent[Index] = Parent == INDEX_NONE ? RawLocal : RawLocal * RawComponent[Parent];
            CompressedComponent[Index] = Parent == INDEX_NONE ? CompressedLocal : CompressedLocal * CompressedComponent[Parent];
            if (RawComponent[Index].ContainsNaN() || CompressedComponent[Index].ContainsNaN())
            {
                Out.Message = TEXT("Native animation evaluation returned a non-finite transform.");
                return Out;
            }
            if (!Selected.IsEmpty() && !Selected.Contains(Name)) continue;
            auto Pose = MakeShared<FJsonObject>();
            Pose->SetBoolField(TEXT("has_raw_track"), Tracks.Contains(Name));
            Pose->SetObjectField(TEXT("raw_local"), AnimationTransformJson(RawLocal));
            Pose->SetObjectField(TEXT("compressed_local"), AnimationTransformJson(CompressedLocal));
            Pose->SetObjectField(TEXT("raw_component"), AnimationTransformJson(RawComponent[Index]));
            Pose->SetObjectField(TEXT("compressed_component"), AnimationTransformJson(CompressedComponent[Index]));
            Poses->SetObjectField(Name.ToString(), Pose);
        }
        auto Sample = MakeShared<FJsonObject>();
        Sample->SetNumberField(TEXT("time"), Time);
        Sample->SetObjectField(TEXT("poses"), Poses);
        Samples.Add(MakeShared<FJsonValueObject>(Sample));
    }
    Data->SetArrayField(TEXT("samples"), Samples);
    const auto Writer = TJsonWriterFactory<>::Create(&Out.Json);
    if (!FJsonSerializer::Serialize(Data, Writer))
    {
        Out.Message = TEXT("Could not serialize animation inspection.");
        return Out;
    }
    Out.bSuccess = true;
    Out.Message = TEXT("Read native raw/compressed samples; no data was modified, saved or rebuilt.");
    return Out;
}
}
