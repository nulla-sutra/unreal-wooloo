#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectGlobals.h"
#include "WoolooMaterial/Tools/MaterialToolset.h"

class FWoolooMaterialModule final : public IModuleInterface
{
    virtual void StartupModule() override
    {
        if (!UToolsetRegistry::IsToolsetClassRegistered(UWoolooMaterialToolset::StaticClass()))
            UToolsetRegistry::RegisterToolsetClass(UWoolooMaterialToolset::StaticClass());
    }

    virtual void ShutdownModule() override
    {
        if (UObjectInitialized())
            UToolsetRegistry::UnregisterToolsetClass(UWoolooMaterialToolset::StaticClass());
    }
};

IMPLEMENT_MODULE(FWoolooMaterialModule, WoolooMaterial);
