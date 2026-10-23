#include "Modules/ModuleManager.h"
#include "WoolooBlueprint/Tools/BlueprintToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectGlobals.h"

class FWoolooBlueprintModule final : public IModuleInterface
{
    virtual void StartupModule() override
    {
        if (!UToolsetRegistry::IsToolsetClassRegistered(UWoolooBlueprintToolset::StaticClass()))
            UToolsetRegistry::RegisterToolsetClass(UWoolooBlueprintToolset::StaticClass());
    }
    virtual void ShutdownModule() override
    {
        if (UObjectInitialized()) UToolsetRegistry::UnregisterToolsetClass(UWoolooBlueprintToolset::StaticClass());
    }
};
IMPLEMENT_MODULE(FWoolooBlueprintModule, WoolooBlueprint);
