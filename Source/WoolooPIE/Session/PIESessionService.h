#pragma once

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
#include "WoolooPIE/Tools/PIEToolTypes.h"

class APlayerController;
class APawn;
class UInputAction;
class UInputComponent;
class UWorld;
enum class EInputActionValueType : uint8;

// Internal implementation shared by the tool facade and its explicit Automation cases.
namespace Wooloo::PIE
{
FWoolooPIEResult DescribeSession();
FWoolooPIEResult InjectInputAction(const FWoolooPIEPlayerTarget& Target,
    const FString& InputActionPath, const FVector& Value);

namespace Private
{
UWorld* ResolveWorld(FName ContextHandle);
bool ValidateIdentity(const FWoolooPIEPlayerTarget& Target, const UWorld* World,
    const APlayerController* Controller, const APawn* Pawn, FString& Error);
bool ValidateValue(EInputActionValueType Type, const FVector& Value, FString& Error);
const UInputAction* FindBoundAction(TConstArrayView<TWeakObjectPtr<const UInputComponent>> Stack,
    const FString& ObjectPath, FString& Error);
}
}
#endif
