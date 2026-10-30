#include "WoolooPIE/Tools/PIEToolset.h"

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
#include "WoolooPIE/Session/PIESessionService.h"

FWoolooPIEResult UWoolooPIEToolset::DescribeSession()
{
    return Wooloo::PIE::DescribeSession();
}

FWoolooPIEResult UWoolooPIEToolset::InjectInputAction(const FWoolooPIEPlayerTarget& Target,
    const FString& InputActionPath, const FVector& Value)
{
    return Wooloo::PIE::InjectInputAction(Target, InputActionPath, Value);
}
#endif
