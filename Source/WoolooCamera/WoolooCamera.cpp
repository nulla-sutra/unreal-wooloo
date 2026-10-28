#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectGlobals.h"
#include "WoolooCamera/Tools/CameraToolset.h"

class FWoolooCameraModule final : public IModuleInterface
{
    virtual void StartupModule() override
    {
        if (!UToolsetRegistry::IsToolsetClassRegistered(UWoolooCameraToolset::StaticClass()))
            UToolsetRegistry::RegisterToolsetClass(UWoolooCameraToolset::StaticClass());
    }

    virtual void ShutdownModule() override
    {
        if (UObjectInitialized())
            UToolsetRegistry::UnregisterToolsetClass(UWoolooCameraToolset::StaticClass());
    }
};

IMPLEMENT_MODULE(FWoolooCameraModule, WoolooCamera);
