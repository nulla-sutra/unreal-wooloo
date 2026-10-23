#pragma once

#include "WoolooStateTree/Tools/StateTreeToolTypes.h"

namespace Wooloo::StateTree
{
    WOOLOOSTATETREE_API FWoolooStateTreeResult Describe(const FString& Path);
    WOOLOOSTATETREE_API FWoolooStateTreeResult Create(const FString& Path, const FString& Root, const FString& SchemaPath);
    WOOLOOSTATETREE_API FWoolooStateTreeResult Edit(const FString& Path, const FString& Root, const FWoolooStateTreeEdit& Edit);
    WOOLOOSTATETREE_API FWoolooStateTreeResult Compile(const FString& Path, const FString& Root);
    WOOLOOSTATETREE_API FWoolooStateTreeResult Save(const FString& Path, const FString& Root);
}
