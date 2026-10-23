#include "Modules/ModuleManager.h"

#include "Tools/MutableToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectGlobals.h"

class FWoolooMutableModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // PostEngineInit runs after the editor registry exists. AICallable alone does not register a class.
        if (!UToolsetRegistry::IsToolsetClassRegistered(UMutableToolset::StaticClass()))
        {
            UToolsetRegistry::RegisterToolsetClass(UMutableToolset::StaticClass());
        }
    }

    virtual void ShutdownModule() override
    {
        if (UObjectInitialized())
        {
            UToolsetRegistry::UnregisterToolsetClass(UMutableToolset::StaticClass());
        }
    }
};

IMPLEMENT_MODULE(FWoolooMutableModule, WoolooMutable)
