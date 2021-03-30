// Fill out your copyright notice in the Description page of Project Settings.

#include "PngTestFunctionLibrary.h"
#include "PngTextChunkHelper.h"

static const FString& Filename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Test.png")));

bool UPngTestFunctionLibrary::Write(const TMap<FString, FString>& MapToWrite)
{
	TSharedPtr<FPngTextChunkHelper> Helper = MakeShareable(new FPngTextChunkHelper(Filename));
	return Helper->Write(MapToWrite);
}

bool UPngTestFunctionLibrary::Read(TMap<FString, FString>& MapToRead)
{
	TSharedPtr<FPngTextChunkHelper> Helper = MakeShareable(new FPngTextChunkHelper(Filename));
	return Helper->Read(MapToRead);
}
