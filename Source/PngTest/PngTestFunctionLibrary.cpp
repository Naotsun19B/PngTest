// Fill out your copyright notice in the Description page of Project Settings.

#include "PngTestFunctionLibrary.h"
#include "PngTextChunkHelper.h"

static const FString& Filename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Test.png")));

bool UPngTestFunctionLibrary::Write(const TMap<FString, FString>& MapToWrite)
{
	TSharedPtr<FPngTextChunkHelper> Helper = FPngTextChunkHelper::CreatePngTextChunkHelper(Filename);
	if (!Helper.IsValid())
	{
		return false;
	}

	return Helper->Write(MapToWrite);
}

bool UPngTestFunctionLibrary::Read(TMap<FString, FString>& MapToRead)
{
	TSharedPtr<FPngTextChunkHelper> Helper = FPngTextChunkHelper::CreatePngTextChunkHelper(Filename);
	if (!Helper.IsValid())
	{
		return false;
	}

	return Helper->Read(MapToRead);
}
