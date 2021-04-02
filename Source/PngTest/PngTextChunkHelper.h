// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "IImageWrapper.h"

#if WITH_UNREALPNG

THIRD_PARTY_INCLUDES_START
#include "ThirdParty/zlib/zlib-1.2.5/Inc/zlib.h"

// make sure no other versions of libpng headers are picked up
#if WITH_LIBPNG_1_6
#include "ThirdParty/libPNG/libPNG-1.6.37/png.h"
#include "ThirdParty/libPNG/libPNG-1.6.37/pngstruct.h"
#include "ThirdParty/libPNG/libPNG-1.6.37/pnginfo.h"
#else
#include "ThirdParty/libPNG/libPNG-1.5.2/png.h"
#include "ThirdParty/libPNG/libPNG-1.5.2/pnginfo.h"
#endif

#include <setjmp.h>
THIRD_PARTY_INCLUDES_END

class FPngTextChunkHelper : public IImageWrapper
{
public:
	static TSharedPtr<FPngTextChunkHelper> CreatePngTextChunkHelper(const FString& Filename);

	FPngTextChunkHelper();

	// IImageWrapper interface.
	virtual const TArray64<uint8>& GetCompressed(int32 Quality = 0) override { return CompressedData; }
	virtual bool SetCompressed(const void* InCompressedData, int64 InCompressedSize) override;
	virtual bool GetRaw(const ERGBFormat InFormat, int32 InBitDepth, TArray64<uint8>& OutRawData) override { return false; }
	virtual bool SetRaw(const void* InRawData, int64 InRawSize, const int32 InWidth, const int32 InHeight, const ERGBFormat InFormat, const int32 InBitDepth) override { return false; }
	virtual int32 GetHeight() const override { return -1; }
	virtual int32 GetWidth() const override { return -1; }
	virtual int32 GetNumFrames() const override { return -1; }
	virtual int32 GetFramerate() const override { return -1; }
	virtual int32 GetBitDepth() const override { return -1; }
	virtual ERGBFormat GetFormat() const override { return ERGBFormat::Invalid; }
	virtual bool SetAnimationInfo(int32 InNumFrames, int32 InFramerate) override { return false; }
	// End of IImageWrapper interface.

	virtual bool IsPNG() const;
	virtual bool Write(const TMap<FString, FString>& MapToWrite);
	virtual bool Read(TMap<FString, FString>& MapToRead);

protected:
	// Callbacks for the pnglibs.
	static void user_read_compressed(png_structp png_ptr, png_bytep data, png_size_t length);
	static void user_write_compressed(png_structp png_ptr, png_bytep data, png_size_t length);
	static void user_flush_data(png_structp png_ptr);
	static void user_error_fn(png_structp png_ptr, png_const_charp error_msg);
	static void user_warning_fn(png_structp png_ptr, png_const_charp warning_msg);
	static void* user_malloc(png_structp png_ptr, png_size_t size);
	static void user_free(png_structp png_ptr, png_voidp struct_ptr);
	// End of callbacks for the pnglibs.

private:
	// Arrays of compressed/raw data.
	TArray64<uint8> RawData;
	TArray64<uint8> CompressedData;

	// The read offset into our array.
	int64 ReadOffset;
};
#endif
