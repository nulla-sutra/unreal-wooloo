#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectGlobals.h"
#include "WoolooAssets/Tools/AssetToolset.h"

class FWoolooAssetsModule final : public IModuleInterface
{
    virtual void StartupModule() override
    {
        if (!UToolsetRegistry::IsToolsetClassRegistered(UWoolooAssetToolset::StaticClass()))
        {
            UToolsetRegistry::RegisterToolsetClass(UWoolooAssetToolset::StaticClass());
        }
    }

    virtual void ShutdownModule() override
    {
        if (UObjectInitialized())
        {
            UToolsetRegistry::UnregisterToolsetClass(UWoolooAssetToolset::StaticClass());
        }
    }
};

IMPLEMENT_MODULE(FWoolooAssetsModule, WoolooAssets);
