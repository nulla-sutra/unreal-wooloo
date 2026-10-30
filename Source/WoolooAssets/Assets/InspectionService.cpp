#include "WoolooAssets/Assets/InspectionService.h"
#include "WoolooAssets/Assets/ImportService.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/PreviewMeshCollection.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Misc/PackageName.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace Wooloo::Assets
{
namespace
{
using FJson = TSharedPtr<FJsonObject>;

TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const auto Value : Values) Result.Add(MakeShared<FJsonValueNumber>(Value));
    return Result;
}

FJson Transform(const FTransform& Value)
{
    auto Result = MakeShared<FJsonObject>();
    const auto P = Value.GetTranslation();
    const auto Q = Value.GetRotation();
    const auto S = Value.GetScale3D();
    Result->SetArrayField(TEXT("translation"), Numbers({P.X, P.Y, P.Z}));
    Result->SetArrayField(TEXT("quaternion"), Numbers({Q.X, Q.Y, Q.Z, Q.W}));
    Result->SetArrayField(TEXT("scale"), Numbers({S.X, S.Y, S.Z}));
    return Result;
}

FString Package(const UObject* Asset)
{
    return Asset ? Asset->GetOutermost()->GetName() : FString();
}

void Reference(const FReferenceSkeleton& Ref, const FJson& Data)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (auto Index = 0; Index < Ref.GetNum(); ++Index)
    {
        auto Row = Transform(Ref.GetRefBonePose()[Index]);
        Row->SetStringField(TEXT("name"), Ref.GetBoneName(Index).ToString());
        const auto Parent = Ref.GetParentIndex(Index);
        if (Parent == INDEX_NONE) Row->SetField(TEXT("parent"), MakeShared<FJsonValueNull>());
        else Row->SetStringField(TEXT("parent"), Ref.GetBoneName(Parent).ToString());
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Data->SetNumberField(TEXT("bone_count"), Ref.GetNum());
    Data->SetArrayField(TEXT("bones"), Rows);
    Data->SetStringField(TEXT("reference_space"), TEXT("local; centimeters; quaternion XYZW"));
}

FJson MaterialInput(const FExpressionInput& Input)
{
    auto Result = MakeShared<FJsonObject>();
    auto* Expression = Input.Expression;
    Result->SetStringField(TEXT("expression"), Expression ? Expression->GetClass()->GetName() : FString());
    Result->SetNumberField(TEXT("output_index"), Input.OutputIndex);
    if (const auto* Sample = Cast<UMaterialExpressionTextureSample>(Expression))
        Result->SetStringField(TEXT("texture"), Package(Sample->Texture));
    if (const auto* Constant = Cast<UMaterialExpressionConstant>(Expression))
        Result->SetNumberField(TEXT("value"), Constant->R);
    if (const auto* Color = Cast<UMaterialExpressionConstant3Vector>(Expression))
        Result->SetArrayField(TEXT("color"), Numbers({Color->Constant.R, Color->Constant.G, Color->Constant.B}));
    return Result;
}
}

FWoolooInspectionResult Inspect(const FString& AssetPath,
    const TArray<float>& SampleTimes, const TArray<FName>& BoneNames)
{
    FWoolooInspectionResult Out;
    if (!IsInGameThread() || !FPackageName::IsValidLongPackageName(AssetPath) || SampleTimes.Num() > 128)
    {
        Out.Message = TEXT("Inspect on the editor game thread using a package path and at most 128 sample times.");
        return Out;
    }
    auto* Asset = LoadObject<UObject>(nullptr, *Production::ObjectPath(AssetPath));
    if (!Asset)
    {
        Out.Message = TEXT("Asset does not exist.");
        return Out;
    }
    auto* Sequence = Cast<UAnimSequence>(Asset);
    if ((!SampleTimes.IsEmpty() || !BoneNames.IsEmpty()) && !Sequence)
    {
        Out.Message = TEXT("SampleTimes/BoneNames are only valid for an AnimSequence.");
        return Out;
    }
    auto Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("class_name"), Asset->GetClass()->GetName());
    Data->SetBoolField(TEXT("dirty"), Asset->GetOutermost()->IsDirty());
    TArray<TSharedPtr<FJsonValue>> SourceFiles;
    if (const auto* Property = FindFProperty<FObjectPropertyBase>(Asset->GetClass(), TEXT("AssetImportData")))
    {
        if (const auto* ImportData = Cast<UAssetImportData>(Property->GetObjectPropertyValue_InContainer(Asset)))
        {
            TArray<FString> Filenames;
            ImportData->ExtractFilenames(Filenames);
            for (const auto& Filename : Filenames) SourceFiles.Add(MakeShared<FJsonValueString>(Filename));
        }
    }
    Data->SetArrayField(TEXT("source_files"), SourceFiles);
    TArray<FName> Dependencies;
    FAssetRegistryModule::GetRegistry().GetDependencies(FName(AssetPath), Dependencies,
        UE::AssetRegistry::EDependencyCategory::Package);
    TArray<TSharedPtr<FJsonValue>> DependencyValues;
    for (const auto Name : Dependencies) DependencyValues.Add(MakeShared<FJsonValueString>(Name.ToString()));
    Data->SetArrayField(TEXT("dependencies"), DependencyValues);
    if (const auto* Skeleton = Cast<USkeleton>(Asset))
    {
        Reference(Skeleton->GetReferenceSkeleton(), Data);
        // These getters return loaded pointers and lose a valid unloaded soft path.
        // Read the serialized soft references without resolving or mutating their caches.
        const auto* PreviewProperty = FindFProperty<FSoftObjectProperty>(Skeleton->GetClass(), TEXT("PreviewSkeletalMesh"));
        const auto* CollectionProperty = FindFProperty<FSoftObjectProperty>(Skeleton->GetClass(), TEXT("AdditionalPreviewSkeletalMeshes"));
        if (!PreviewProperty || !CollectionProperty)
        {
            Out.Message = TEXT("Native Skeleton preview soft-reference properties are unavailable.");
            return Out;
        }
        const auto PreviewPath = PreviewProperty->GetPropertyValue_InContainer(Skeleton).ToSoftObjectPath();
        const auto CollectionPath = CollectionProperty->GetPropertyValue_InContainer(Skeleton).ToSoftObjectPath();
        Data->SetStringField(TEXT("preview_mesh"), PreviewPath.GetLongPackageName());
        Data->SetStringField(TEXT("preview_collection"), CollectionPath.GetLongPackageName());
        Data->SetStringField(TEXT("preview_mesh_object_path"), PreviewPath.ToString());
        Data->SetStringField(TEXT("preview_collection_object_path"), CollectionPath.ToString());
    }
    else if (const auto* Mesh = Cast<USkeletalMesh>(Asset))
    {
        if (Mesh->IsCompiling())
        {
            Out.Message = TEXT("Mesh compilation is pending; inspect again after native completion.");
            return Out;
        }
        Reference(Mesh->GetRefSkeleton(), Data);
        Data->SetStringField(TEXT("skeleton"), Package(Mesh->GetSkeleton()));
        TArray<TSharedPtr<FJsonValue>> Slots;
        for (const auto& Slot : Mesh->GetMaterials())
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("slot"), Slot.MaterialSlotName.ToString());
            Row->SetStringField(TEXT("imported_slot"), Slot.ImportedMaterialSlotName.ToString());
            Row->SetStringField(TEXT("material"), Package(Slot.MaterialInterface));
            Slots.Add(MakeShared<FJsonValueObject>(Row));
        }
        Data->SetArrayField(TEXT("materials"), Slots);
        const auto* Model = Mesh->GetImportedModel();
        Data->SetNumberField(TEXT("lod_count"), Model ? Model->LODModels.Num() : 0);
        if (Model && Model->LODModels.Num())
        {
            const auto& LOD = Model->LODModels[0];
            auto Triangles = 0;
            for (const auto& Section : LOD.Sections) Triangles += Section.NumTriangles;
            Data->SetNumberField(TEXT("vertices"), LOD.NumVertices);
            Data->SetNumberField(TEXT("triangles"), Triangles);
        }
    }
    else if (Sequence)
    {
        const auto* AnimationSkeleton = Sequence->GetSkeleton();
        if (!AnimationSkeleton)
        {
            Out.Message = TEXT("Animation has no Skeleton.");
            return Out;
        }
        const auto& Ref = AnimationSkeleton->GetReferenceSkeleton();
        Data->SetStringField(TEXT("skeleton"), Package(AnimationSkeleton));
        Data->SetNumberField(TEXT("duration"), Sequence->GetPlayLength());
        Data->SetNumberField(TEXT("fps"), Sequence->GetSamplingFrameRate().AsDecimal());
        Data->SetNumberField(TEXT("sampled_keys"), Sequence->GetNumberOfSampledKeys());
        // These public queries check current-platform data and reference validity.
        // IsCompiling is protected in this engine; inspection must not force a cache build.
        const auto* ForceRaw = IConsoleManager::Get().FindConsoleVariable(TEXT("a.ForceEvalRawData"));
        const auto bCompressedReady = Sequence->IsBoneCompressedDataValid()
            && !Sequence->IsCompressedDataOutOfDate() && (!ForceRaw || ForceRaw->GetInt() != 1);
        Data->SetBoolField(TEXT("compressed_data_ready"), bCompressedReady);
        Reference(Ref, Data);
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
        if (!SampleTimes.IsEmpty() && (!bCompressedReady || Sequence->IsValidAdditive()))
        {
            Out.Message = TEXT("Sampling needs ready compressed data and a non-additive animation; no data is rebuilt or repaired.");
            return Out;
        }
        TArray<TSharedPtr<FJsonValue>> Samples;
        for (const auto Time : SampleTimes)
        {
            TArray<FTransform> Component;
            Component.SetNum(Ref.GetNum());
            auto Poses = MakeShared<FJsonObject>();
            FAnimExtractContext Context(static_cast<double>(Time), false);
            Context.bEnforceCompressedDataSampling = true;
            for (auto Index = 0; Index < Ref.GetNum(); ++Index)
            {
                // Unanimated bones retain their reference transform; the native
                // decompressor only assigns bones that have compressed tracks.
                auto Local = Ref.GetRefBonePose()[Index];
                Sequence->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Index), Context, false);
                const auto Parent = Ref.GetParentIndex(Index);
                Component[Index] = Parent == INDEX_NONE ? Local : Local * Component[Parent];
                const auto Name = Ref.GetBoneName(Index);
                if (Selected.IsEmpty() || Selected.Contains(Name)) Poses->SetObjectField(Name.ToString(), Transform(Component[Index]));
            }
            auto Sample = MakeShared<FJsonObject>();
            Sample->SetNumberField(TEXT("time"), Time);
            Sample->SetObjectField(TEXT("poses"), Poses);
            Samples.Add(MakeShared<FJsonValueObject>(Sample));
        }
        Data->SetArrayField(TEXT("samples"), Samples);
        Data->SetStringField(TEXT("sample_space"), TEXT("component; centimeters; quaternion XYZW; compressed animation"));
    }
    else if (const auto* Collection = Cast<UPreviewMeshCollection>(Asset))
    {
        Data->SetStringField(TEXT("skeleton"), Package(Collection->Skeleton));
        TArray<TSharedPtr<FJsonValue>> Meshes;
        for (const auto& Entry : Collection->SkeletalMeshes)
            Meshes.Add(MakeShared<FJsonValueString>(Entry.SkeletalMesh.ToSoftObjectPath().GetLongPackageName()));
        Data->SetArrayField(TEXT("meshes"), Meshes);
    }
    else if (const auto* Material = Cast<UMaterial>(Asset))
    {
        Data->SetBoolField(TEXT("two_sided"), Material->TwoSided != 0);
        Data->SetStringField(TEXT("blend_mode"), Material->GetBlendMode() == BLEND_Masked ? TEXT("Masked")
            : Material->GetBlendMode() == BLEND_Opaque ? TEXT("Opaque") : TEXT("Other"));
        Data->SetBoolField(TEXT("used_with_skeletal_mesh"), Material->GetUsageByFlag(MATUSAGE_SkeletalMesh));
        if (const auto* EditorData = Material->GetEditorOnlyData())
        {
            Data->SetObjectField(TEXT("base_color"), MaterialInput(EditorData->BaseColor));
            Data->SetObjectField(TEXT("opacity_mask"), MaterialInput(EditorData->OpacityMask));
            Data->SetObjectField(TEXT("roughness"), MaterialInput(EditorData->Roughness));
        }
    }
    else if (const auto* Texture = Cast<UTexture2D>(Asset))
    {
        Data->SetNumberField(TEXT("width"), Texture->GetSizeX());
        Data->SetNumberField(TEXT("height"), Texture->GetSizeY());
        Data->SetBoolField(TEXT("srgb"), Texture->SRGB != 0);
    }
    else
    {
        Out.Message = TEXT("Unsupported asset type.");
        return Out;
    }
    const auto Writer = TJsonWriterFactory<>::Create(&Out.Json);
    Out.bSuccess = FJsonSerializer::Serialize(Data, Writer);
    Out.Message = Out.bSuccess ? TEXT("Read asset data without authoring or saving.") : TEXT("JSON serialization failed.");
    return Out;
}
}
