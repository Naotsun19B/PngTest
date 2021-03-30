// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#if WITH_UNREALPNG
class IImageWrapper;

class FPngTextChunkHelper
{
public:
	// Constructor.
	FPngTextChunkHelper(const FString& FilePath);
	
	bool Write(const TMap<FString, FString>& MapToWrite);
	bool Read(TMap<FString, FString>& MapToRead);

private:
	bool bIsValid;
	TSharedPtr<IImageWrapper> ImageWrapper;
};
#endif
