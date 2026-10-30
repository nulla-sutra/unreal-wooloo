#if WITH_DEV_AUTOMATION_TESTS && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)

#include "WoolooBlueprint/Tools/BlueprintToolset.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FAnimationTemplateFixture
{
    FString Root = TEXT("/Game/WoolooBlueprintAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FString Path = Root + TEXT("/ABP_Template");
    TStrongObjectPtr<UAnimBlueprint> Asset;

    ~FAnimationTemplateFixture()
    {
        if (!Asset.IsValid()) return;
        FAssetRegistryModule::AssetDeleted(Asset.Get());
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->SetFlags(RF_Transient);
        Asset->GetOutermost()->SetDirtyFlag(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooAnimationTemplateTest,
    "Wooloo.Blueprint.AnimationBlueprintTemplate.CreationAndGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooAnimationTemplateTest::RunTest(const FString& Parameters)
{
    FAnimationTemplateFixture Fixture;
    TestFalse(TEXT("A project-wide root is refused"),
        UWoolooBlueprintToolset::CreateAnimationBlueprintTemplate(Fixture.Path, TEXT("/Game")).bSuccess);
    const auto Sibling = Fixture.Root + TEXT("Sibling/ABP_Template");
    TestFalse(TEXT("A sibling sharing the root prefix is outside scope"),
        UWoolooBlueprintToolset::CreateAnimationBlueprintTemplate(Sibling, Fixture.Root).bSuccess);
    TestNull(TEXT("Scope refusal does not create a package"), FindPackage(nullptr, *Sibling));
    TestFalse(TEXT("Invalid package paths are refused"),
        UWoolooBlueprintToolset::CreateAnimationBlueprintTemplate(TEXT("Not/A/Package"), Fixture.Root).bSuccess);
    TestFalse(TEXT("Engine content is protected even when supplied as the allowed root"),
        UWoolooBlueprintToolset::CreateAnimationBlueprintTemplate(TEXT("/Engine/WoolooBlueprintAutomation/ABP_Template"),
            TEXT("/Engine/WoolooBlueprintAutomation")).bSuccess);

    const auto Result = UWoolooBlueprintToolset::CreateAnimationBlueprintTemplate(Fixture.Path, Fixture.Root);
    const auto ObjectPath = Fixture.Path + TEXT(".ABP_Template");
    Fixture.Asset.Reset(FindObject<UAnimBlueprint>(nullptr, *ObjectPath));
    if (!TestTrue(TEXT("Native template creation succeeds"), Result.bSuccess)
        || !TestNotNull(TEXT("The created animation Blueprint is loaded"), Fixture.Asset.Get())) return false;

    auto* Template = Fixture.Asset.Get();
    TestTrue(TEXT("The asset is an animation Blueprint template"), Template->bIsTemplate);
    TestNull(TEXT("No Skeleton is assigned"), Template->TargetSkeleton.Get());
    TestEqual(TEXT("The parent is the generic native AnimInstance"), Template->ParentClass.Get(), UAnimInstance::StaticClass());
    TestTrue(TEXT("The new package is dirty"), Template->GetOutermost()->IsDirty());
    TestFalse(TEXT("Creation does not save a package"), FPackageName::DoesPackageExist(Fixture.Path));

    TSharedPtr<FJsonObject> Json;
    if (TestTrue(TEXT("Creation returns structured metadata"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Json), Json) && Json.IsValid()))
    {
        TestEqual(TEXT("Metadata identifies the requested package"), Json->GetStringField(TEXT("asset_path")), Fixture.Path);
        TestTrue(TEXT("Metadata reports creation"), Json->GetBoolField(TEXT("created")));
        TestTrue(TEXT("Metadata reports the actual template flag"), Json->GetBoolField(TEXT("is_template")));
        TestTrue(TEXT("Metadata reports unsaved changes"), Json->GetBoolField(TEXT("dirty")));
    }

    Template->GetOutermost()->SetDirtyFlag(false);
    const auto Duplicate = UWoolooBlueprintToolset::CreateAnimationBlueprintTemplate(Fixture.Path, Fixture.Root);
    TestFalse(TEXT("An existing in-memory asset is never replaced"), Duplicate.bSuccess);
    TestTrue(TEXT("Duplicate refusal preserves object identity"),
        FindObject<UAnimBlueprint>(nullptr, *ObjectPath) == Template);
    TestFalse(TEXT("Duplicate refusal does not dirty the existing package"), Template->GetOutermost()->IsDirty());
    return true;
}

#endif
