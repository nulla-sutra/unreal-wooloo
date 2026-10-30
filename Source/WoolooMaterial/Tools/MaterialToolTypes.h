#pragma once
#include "CoreMinimal.h"
#include "MaterialToolTypes.generated.h"

USTRUCT(BlueprintType)
struct WOOLOOMATERIAL_API FWoolooMaterialResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Material") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Material") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Material") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category="Material") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category="Material") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category="Material") TArray<FString> Warnings;
};
