#if WITH_DEV_AUTOMATION_TESTS && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)

#include "WoolooMaterial/Tools/MaterialToolset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include <limits>

namespace
{
struct FCollectionFixture
{
    FString Root = TEXT("/Game/WoolooMaterialTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TArray<TStrongObjectPtr<UMaterialParameterCollection>> Collections;

    TSharedPtr<FJsonObject> Create(const FString& Path, const FString& AllowedRoot,
        const TMap<FName, float>& Scalars = {}, const TMap<FName, FLinearColor>& Vectors = {})
    {
        const auto Json = UWoolooMaterialToolset::CreateMaterialParameterCollection(Path, AllowedRoot, Scalars, Vectors);
        auto* Collection = FindObject<UMaterialParameterCollection>(nullptr,
            *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
        if (Collection && !Collections.ContainsByPredicate([Collection](const auto& Entry) { return Entry.Get() == Collection; }))
            Collections.Emplace(Collection);
        TSharedPtr<FJsonObject> Data;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data);
        return Data;
    }

    ~FCollectionFixture()
    {
        // Remove only this test's unsaved registry entries; do not save, delete files, or clear Undo.
        for (auto& Collection : Collections)
        {
            FAssetRegistryModule::AssetDeleted(Collection.Get());
            Collection->ClearFlags(RF_Public | RF_Standalone);
            Collection->SetFlags(RF_Transient);
            Collection->GetOutermost()->SetFlags(RF_Transient);
            Collection->GetOutermost()->SetDirtyFlag(false);
        }
    }
};

bool Succeeded(const TSharedPtr<FJsonObject>& Data)
{
    bool bSuccess = false;
    return Data.IsValid() && Data->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooMaterialCollectionCreationTest,
    "Wooloo.Material.ParameterCollection.CreateUnsavedDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooMaterialCollectionCreationTest::RunTest(const FString& Parameters)
{
    FCollectionFixture Fixture;
    const auto Path = Fixture.Root + TEXT("/MPC_Defaults");
    const TMap<FName, float> Scalars = { {TEXT("Scale"), 2.5f}, {TEXT("Blend"), 0.25f} };
    const TMap<FName, FLinearColor> Vectors = { {TEXT("Tint"), FLinearColor(0.1f, 0.2f, 0.3f, 0.4f)} };
    const auto Data = Fixture.Create(Path, Fixture.Root, Scalars, Vectors);
    if (!TestTrue(TEXT("Creation returns success"), Succeeded(Data))) return false;
    TestTrue(TEXT("Reports creation"), Data->GetBoolField(TEXT("created")));
    TestTrue(TEXT("Reports unsaved changes"), Data->GetBoolField(TEXT("dirty")));
    TestEqual(TEXT("Reports the exact package"), Data->GetStringField(TEXT("asset_path")), Path);
    if (!TestEqual(TEXT("One collection is created"), Fixture.Collections.Num(), 1)) return false;
    const auto* Collection = Fixture.Collections[0].Get();
    TestEqual(TEXT("All scalar parameters are present"), Collection->ScalarParameters.Num(), 2);
    TestEqual(TEXT("All vector parameters are present"), Collection->VectorParameters.Num(), 1);
    TSet<FGuid> Ids;
    for (const auto& Parameter : Collection->ScalarParameters)
    {
        TestTrue(TEXT("Scalar defaults retain supplied names and values"),
            Scalars.Contains(Parameter.ParameterName) && Scalars.FindChecked(Parameter.ParameterName) == Parameter.DefaultValue);
        TestTrue(TEXT("Scalar identifier is valid and unique"), Parameter.Id.IsValid() && !Ids.Contains(Parameter.Id));
        Ids.Add(Parameter.Id);
    }
    for (const auto& Parameter : Collection->VectorParameters)
    {
        TestTrue(TEXT("Vector defaults retain RGBA"),
            Vectors.Contains(Parameter.ParameterName) && Vectors.FindChecked(Parameter.ParameterName) == Parameter.DefaultValue);
        TestTrue(TEXT("Vector identifier is valid and unique"), Parameter.Id.IsValid() && !Ids.Contains(Parameter.Id));
        Ids.Add(Parameter.Id);
    }
    if (FApp::CanEverRenderOrProduceRenderData())
        TestTrue(TEXT("Native parameter-buffer layout is initialized"), Collection->HasValidUniformBufferStruct());
    TestTrue(TEXT("Native collection state identifier is valid"), Collection->StateId.IsValid());
    TestTrue(TEXT("Package remains dirty"), Collection->GetOutermost()->IsDirty());
    TestFalse(TEXT("No asset file is saved"), FPackageName::DoesPackageExist(Path));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooMaterialCollectionOverwriteTest,
    "Wooloo.Material.ParameterCollection.RefuseExistingPackage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooMaterialCollectionOverwriteTest::RunTest(const FString& Parameters)
{
    FCollectionFixture Fixture;
    const auto Path = Fixture.Root + TEXT("/MPC_Existing");
    if (!TestTrue(TEXT("Initial creation succeeds"), Succeeded(Fixture.Create(Path, Fixture.Root, {{TEXT("Keep"), 3.f}}))))
        return false;
    auto* Collection = Fixture.Collections[0].Get();
    const auto ParameterId = Collection->ScalarParameters[0].Id;
    Collection->GetOutermost()->SetDirtyFlag(false);
    const auto Data = Fixture.Create(Path, Fixture.Root, {{TEXT("Replace"), 99.f}});
    if (!TestTrue(TEXT("Duplicate request returns JSON"), Data.IsValid())) return false;
    TestFalse(TEXT("Existing collection is refused"), Succeeded(Data));
    TestFalse(TEXT("Reports no creation by the second request"), Data->GetBoolField(TEXT("created")));
    TestEqual(TEXT("Original name survives"), Collection->ScalarParameters[0].ParameterName, FName(TEXT("Keep")));
    TestEqual(TEXT("Original value survives"), Collection->ScalarParameters[0].DefaultValue, 3.f);
    TestTrue(TEXT("Original identifier survives"), Collection->ScalarParameters[0].Id == ParameterId);
    TestFalse(TEXT("Refusal does not dirty the existing package"), Collection->GetOutermost()->IsDirty());
    TestFalse(TEXT("Refusal does not save"), FPackageName::DoesPackageExist(Path));

    auto* Occupied = CreatePackage(*(Fixture.Root + TEXT("/Occupied")));
    Occupied->SetFlags(RF_Transient);
    TestFalse(TEXT("An existing empty package is also refused"),
        Succeeded(Fixture.Create(Occupied->GetName(), Fixture.Root, {{TEXT("Value"), 1.f}})));
    TestFalse(TEXT("Empty package is unchanged"), Occupied->IsDirty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooMaterialCollectionValidationTest,
    "Wooloo.Material.ParameterCollection.InvalidInputHasNoPackage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooMaterialCollectionValidationTest::RunTest(const FString& Parameters)
{
    FCollectionFixture Fixture;
    const auto Refused = [&](const TCHAR* Label, const FString& Path, const FString& Root,
        const TMap<FName, float>& Scalars = {}, const TMap<FName, FLinearColor>& Vectors = {})
    {
        const auto Data = Fixture.Create(Path, Root, Scalars, Vectors);
        if (!TestTrue(Label, Data.IsValid())) return;
        TestFalse(Label, Succeeded(Data));
        TestFalse(TEXT("Refused input reports no creation"), Data->GetBoolField(TEXT("created")));
        TestNull(TEXT("Preflight did not create a package"), FindPackage(nullptr, *Path));
        TestFalse(TEXT("Preflight did not save a file"), FPackageName::DoesPackageExist(Path));
    };
    Refused(TEXT("Whole-mount scope is refused"), Fixture.Root + TEXT("/MPC_Broad"), TEXT("/Game"));
    Refused(TEXT("Sibling-prefix escape is refused"), Fixture.Root + TEXT("Sibling/MPC_Outside"), Fixture.Root);
    Refused(TEXT("Engine content is refused"), TEXT("/Engine/WoolooMaterialTests/MPC_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits), TEXT("/Engine/WoolooMaterialTests"));
    Refused(TEXT("Empty parameters are refused before native initialization"), Fixture.Root + TEXT("/MPC_Empty"), Fixture.Root);
    Refused(TEXT("None parameter name is refused"), Fixture.Root + TEXT("/MPC_None"), Fixture.Root, {{NAME_None, 1.f}});
    Refused(TEXT("Cross-type names are case-insensitively unique"), Fixture.Root + TEXT("/MPC_Duplicate"), Fixture.Root,
        {{TEXT("Shared"), 1.f}}, {{TEXT("shared"), FLinearColor::White}});
    Refused(TEXT("Native numeric-suffix aliases are refused"), Fixture.Root + TEXT("/MPC_Alias"), Fixture.Root,
        {{TEXT("Value"), 1.f}, {TEXT("Value0"), 2.f}});
    Refused(TEXT("Leading-zero numeric suffixes use decimal identity"), Fixture.Root + TEXT("/MPC_DecimalAlias"), Fixture.Root,
        {{TEXT("Value08"), 1.f}, {TEXT("Value8"), 2.f}});
    Refused(TEXT("Native numeric-suffix overflow is refused"), Fixture.Root + TEXT("/MPC_Overflow"), Fixture.Root,
        {{TEXT("Value2147483648"), 1.f}});
    Refused(TEXT("Non-finite scalars are refused"), Fixture.Root + TEXT("/MPC_NaN"), Fixture.Root,
        {{TEXT("Invalid"), std::numeric_limits<float>::quiet_NaN()}});
    Refused(TEXT("Non-finite vector channels are refused"), Fixture.Root + TEXT("/MPC_Infinity"), Fixture.Root, {},
        {{TEXT("Invalid"), FLinearColor(0.f, 0.f, 0.f, std::numeric_limits<float>::infinity())}});
    TestTrue(TEXT("Validation created no collections"), Fixture.Collections.IsEmpty());
    return true;
}

#endif
