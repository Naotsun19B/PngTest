// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PngTestFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class PNGTEST_API UPngTestFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable)
	static bool Write(const TMap<FString, FString>& MapToWrite);

	UFUNCTION(BlueprintCallable)
	static bool Read(TMap<FString, FString>& MapToRead);
};
