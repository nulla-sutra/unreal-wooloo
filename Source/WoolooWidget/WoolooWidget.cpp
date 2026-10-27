#include "Modules/ModuleManager.h"
#include "WoolooWidget/Tools/WidgetToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectGlobals.h"

class FWoolooWidgetModule final : public IModuleInterface
{
    virtual void StartupModule() override
    {
        if (!UToolsetRegistry::IsToolsetClassRegistered(UWoolooWidgetToolset::StaticClass()))
            UToolsetRegistry::RegisterToolsetClass(UWoolooWidgetToolset::StaticClass());
    }

    virtual void ShutdownModule() override
    {
        if (UObjectInitialized()) UToolsetRegistry::UnregisterToolsetClass(UWoolooWidgetToolset::StaticClass());
    }
};

IMPLEMENT_MODULE(FWoolooWidgetModule, WoolooWidget);
