#include "Modules/ModuleManager.h"
#include "WoolooStateTree/Tools/StateTreeToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectGlobals.h"

class FWoolooStateTreeModule final : public IModuleInterface
{
    virtual void StartupModule() override
    {
        if (!UToolsetRegistry::IsToolsetClassRegistered(UWoolooStateTreeToolset::StaticClass()))
        {
            UToolsetRegistry::RegisterToolsetClass(UWoolooStateTreeToolset::StaticClass());
        }
    }
    virtual void ShutdownModule() override
    {
        if (UObjectInitialized())
        {
            UToolsetRegistry::UnregisterToolsetClass(UWoolooStateTreeToolset::StaticClass());
        }
    }
};
IMPLEMENT_MODULE(FWoolooStateTreeModule, WoolooStateTree);
