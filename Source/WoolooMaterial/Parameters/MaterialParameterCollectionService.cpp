#include "WoolooMaterial/Parameters/MaterialParameterCollectionService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Containers/UnrealString.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Factories/MaterialParameterCollectionFactoryNew.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace Wooloo::Material
{
namespace
{
FString Result(const FString& AssetPath, bool bSuccess, bool bCreated, bool bDirty, const FString& Message)
{
    auto Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("success"), bSuccess);
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetBoolField(TEXT("created"), bCreated);
    Data->SetBoolField(TEXT("dirty"), bDirty);
    Data->SetStringField(TEXT("message"), Message);
    FString Json;
    auto Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Data, Writer);
    return Json;
}

bool ValidateName(FName Name, TSet<FName>& AllNames, TSet<FString>& NativeNames, FString& Error)
{
    const auto Text = Name.ToString();
    if (Name.IsNone() || Text.TrimStartAndEnd().IsEmpty() || AllNames.Contains(Name))
    {
        Error = TEXT("Parameter names must be non-empty and unique across both types: ") + Text;
        return false;
    }

    // Native MPC editing treats trailing numeric suffixes as one name family. Reject aliases
    // before creating a package so PostEditChange cannot silently rename a caller's parameter.
    const auto NumberStart = Text.FindLastCharByPredicate([](TCHAR Letter) { return !FChar::IsDigit(Letter); }) + 1;
    auto Base = Text.Left(NumberStart);
    int32 Number = 0;
    if (NumberStart < Text.Len())
    {
        const auto Suffix = Text.Mid(NumberStart);
        TCHAR* End = nullptr;
        const auto Parsed = FCString::Strtoi64(*Suffix, &End, 10);
        if (!End || *End != 0 || Parsed < 0 || Parsed > MAX_int32)
        {
            Error = TEXT("Parameter numeric suffix must fit a non-negative native int32: ") + Text;
            return false;
        }
        Number = static_cast<int32>(Parsed);
    }
    if (Base.IsEmpty()) Base = TEXT("Param");
    const auto NativeName = Base + TEXT("/") + LexToString(Number);
    if (NativeNames.Contains(NativeName))
    {
        Error = TEXT("Parameter names alias under native MPC name sanitization: ") + Text;
        return false;
    }
    AllNames.Add(Name);
    NativeNames.Add(NativeName);
    return true;
}
}

FString CreateParameterCollection(const FString& AssetPath, const FString& AllowedPackageRoot,
    const TMap<FName, float>& ScalarParameters, const TMap<FName, FLinearColor>& VectorParameters)
{
    const auto Refuse = [&AssetPath](const FString& Message)
    {
        return Result(AssetPath, false, false, false, Message);
    };
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
        return Refuse(TEXT("Creation requires the editor game thread with PIE stopped."));

    auto Root = AllowedPackageRoot;
    Root.RemoveFromEnd(TEXT("/"));
    FString RootFilename;
    if (Root.Len() >= NAME_SIZE || !FPackageName::IsValidLongPackageName(Root)
        || Root.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1) == INDEX_NONE
        || !FPackageName::TryConvertLongPackageNameToFilename(Root, RootFilename))
        return Refuse(TEXT("AllowedPackageRoot must be a mounted content directory below a mount point."));

    FString Filename;
    if (AssetPath.Len() >= NAME_SIZE || !FPackageName::IsValidLongPackageName(AssetPath)
        || !AssetPath.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase)
        || !FPackageName::TryConvertLongPackageNameToFilename(AssetPath, Filename)
        || FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename),
            FPaths::ConvertRelativePathToFull(FPaths::EngineDir())))
        return Refuse(TEXT("AssetPath must be inside AllowedPackageRoot and outside engine content."));

    const auto AssetName = FPackageName::GetShortName(AssetPath);
    if (AssetName.Len() >= NAME_SIZE || !FPackageName::IsValidObjectPath(AssetPath + TEXT(".") + AssetName))
        return Refuse(TEXT("AssetPath must be a package name with a valid asset name, without an object suffix."));

    const auto* StorageLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MPC.MaxVectorStorage"));
    const int64 VectorStorage = (static_cast<int64>(ScalarParameters.Num()) + 3) / 4 + VectorParameters.Num();
    // A new, empty native collection has no initialized buffer layout until its storage changes.
    if (VectorStorage == 0) return Refuse(TEXT("Supply at least one scalar or vector parameter."));
    if (!StorageLimit || VectorStorage > StorageLimit->GetInt())
        return Refuse(TEXT("Parameter storage exceeds the engine's r.MPC.MaxVectorStorage budget or the budget is unavailable."));

    TSet<FName> Names;
    TSet<FString> ScalarNativeNames;
    TSet<FString> VectorNativeNames;
    FString Error;
    for (const auto& Entry : ScalarParameters)
    {
        if (!ValidateName(Entry.Key, Names, ScalarNativeNames, Error)) return Refuse(Error);
        if (!FMath::IsFinite(Entry.Value)) return Refuse(TEXT("Scalar values must be finite: ") + Entry.Key.ToString());
    }
    for (const auto& Entry : VectorParameters)
    {
        if (!ValidateName(Entry.Key, Names, VectorNativeNames, Error)) return Refuse(Error);
        const auto& Value = Entry.Value;
        if (!FMath::IsFinite(Value.R) || !FMath::IsFinite(Value.G)
            || !FMath::IsFinite(Value.B) || !FMath::IsFinite(Value.A))
            return Refuse(TEXT("Vector components must be finite: ") + Entry.Key.ToString());
    }

    auto& Registry = FAssetRegistryModule::GetRegistry();
    if (Registry.IsLoadingAssets()) return Refuse(TEXT("Wait for Asset Registry discovery to finish."));
    TArray<FAssetData> ExistingAssets;
    Registry.GetAssetsByPackageName(FName(*AssetPath), ExistingAssets);
    if (!ExistingAssets.IsEmpty() || FPackageName::DoesPackageExist(AssetPath) || FindPackage(nullptr, *AssetPath))
        return Refuse(TEXT("The destination package already exists on disk, in the registry or in memory."));

    auto* Factory = NewObject<UMaterialParameterCollectionFactoryNew>();
    Factory->bEditAfterNew = false;
    TGuardValue<bool> Unattended(GIsRunningUnattendedScript, true);
    auto* Asset = FAssetToolsModule::GetModule().Get().CreateAsset(AssetName,
        FPackageName::GetLongPackagePath(AssetPath), UMaterialParameterCollection::StaticClass(), Factory,
        NAME_None, false);
    auto* Collection = Cast<UMaterialParameterCollection>(Asset);
    if (!Collection)
        return Result(AssetPath, false, Asset != nullptr, Asset && Asset->GetOutermost()->IsDirty(),
            TEXT("The native factory did not return an MPC; inspect any reported partial creation before retrying."));

    Collection->PreEditChange(nullptr);
    Collection->ScalarParameters.Reset(ScalarParameters.Num());
    Collection->VectorParameters.Reset(VectorParameters.Num());
    TArray<FName> ScalarNames;
    TArray<FName> VectorNames;
    ScalarParameters.GetKeys(ScalarNames);
    VectorParameters.GetKeys(VectorNames);
    const auto LexicalOrder = [](FName A, FName B) { return A.LexicalLess(B); };
    ScalarNames.Sort(LexicalOrder);
    VectorNames.Sort(LexicalOrder);
    for (const auto Name : ScalarNames)
    {
        auto& Parameter = Collection->ScalarParameters.AddDefaulted_GetRef();
        Parameter.ParameterName = Name;
        Parameter.DefaultValue = ScalarParameters.FindChecked(Name);
    }
    for (const auto Name : VectorNames)
    {
        auto& Parameter = Collection->VectorParameters.AddDefaulted_GetRef();
        Parameter.ParameterName = Name;
        Parameter.DefaultValue = VectorParameters.FindChecked(Name);
    }
    // Native editor notification builds the uniform-buffer layout and updates world instances.
    // Saving remains separate; this tool never adopts an existing collection.
    Collection->PostEditChange();
    Collection->MarkPackageDirty();
    return Result(AssetPath, true, true, Collection->GetOutermost()->IsDirty(),
        TEXT("Created an unsaved MPC. Save the exact package separately after inspection."));
}
}
