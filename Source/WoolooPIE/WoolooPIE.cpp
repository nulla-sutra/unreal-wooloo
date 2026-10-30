#include "Modules/ModuleManager.h"

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
#include "WoolooPIE/Tools/PIEToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectGlobals.h"

class FWoolooPIEModule final : public IModuleInterface
{
    virtual void StartupModule() override
    {
        if (!UToolsetRegistry::IsToolsetClassRegistered(UWoolooPIEToolset::StaticClass()))
            UToolsetRegistry::RegisterToolsetClass(UWoolooPIEToolset::StaticClass());
    }

    virtual void ShutdownModule() override
    {
        if (UObjectInitialized()) UToolsetRegistry::UnregisterToolsetClass(UWoolooPIEToolset::StaticClass());
    }
};

IMPLEMENT_MODULE(FWoolooPIEModule, WoolooPIE);
#endif
