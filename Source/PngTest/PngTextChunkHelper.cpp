// Fill out your copyright notice in the Description page of Project Settings.

#include "PngTextChunkHelper.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"

#if WITH_UNREALPNG
#include "Formats/PngImageWrapper.h"

// Disable warning "interaction between '_setjmp' and C++ object destruction is non-portable".
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4611)
#endif

THIRD_PARTY_INCLUDES_START
#include "ThirdParty/zlib/zlib-1.2.5/Inc/zlib.h"

// make sure no other versions of libpng headers are picked up.
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

namespace PngTextChunkHelperInternal
{
	class FPngReadGuard
	{
	public:
		FPngReadGuard(png_structp* InReadPtr, png_infop* InInfoPtr)
			: ReadPtr(InReadPtr)
			, InfoPtr(InInfoPtr)
			, RowPointers(NULL)
		{
		}

		~FPngReadGuard()
		{
			if (RowPointers != NULL)
			{
				png_free(*ReadPtr, RowPointers);
			}
			png_destroy_read_struct(ReadPtr, InfoPtr, NULL);
		}

		void SetRowPointers(png_bytep* InRowPointers)
		{
			RowPointers = InRowPointers;
		}

	private:
		png_structp* ReadPtr;
		png_infop* InfoPtr;
		png_bytep* RowPointers;
	};

	class FPngWriteGuard
	{
	public:
		FPngWriteGuard(png_structp* InWritePtr, png_infop* InInfoPtr)
			: WritePtr(InWritePtr)
			, InfoPtr(InInfoPtr)
			, RowPointers(NULL)
		{
		}

		~FPngWriteGuard()
		{
			if (RowPointers != NULL)
			{
				png_free(*WritePtr, RowPointers);
			}
			png_destroy_write_struct(WritePtr, InfoPtr);
		}

		void SetRowPointers(png_bytep* InRowPointers)
		{
			RowPointers = InRowPointers;
		}

	private:
		png_structp* WritePtr;
		png_infop* InfoPtr;
		png_bytep* RowPointers;
	};

	class FLibPngEventHandler
	{
	public:
		static void user_read_compressed(png_structp png_ptr, png_bytep data, png_size_t length)
		{
			//FPngImageWrapper* Ctx = (FPngImageWrapper*)png_get_io_ptr(png_ptr);
			//TArray64<uint8>& CompressedData = const_cast<TArray64<uint8>&>(Ctx->GetCompressed());
			//if (Ctx->ReadOffset + (int64)length <= CompressedData.Num())
			//{
			//	FMemory::Memcpy(data, &CompressedData[Ctx->ReadOffset], length);
			//	Ctx->ReadOffset += length;
			//}
			//else
			//{
			//	Ctx->SetError(TEXT("Invalid read position for CompressedData."));
			//}
		}

		static void user_write_compressed(png_structp png_ptr, png_bytep data, png_size_t length)
		{
			//FPngImageWrapper* Ctx = (FPngImageWrapper*)png_get_io_ptr(png_ptr);
			//TArray64<uint8>& CompressedData = const_cast<TArray64<uint8>&>(Ctx->GetCompressed());
			//int64 Offset = CompressedData.AddUninitialized(length);
			//FMemory::Memcpy(&CompressedData[Offset], data, length);
		}

		static void user_flush_data(png_structp png_ptr)
		{
		}

		static void user_error_fn(png_structp png_ptr, png_const_charp error_msg)
		{
			UE_LOG(LogTemp, Error, TEXT("PNG Error: %s"), ANSI_TO_TCHAR(error_msg));
		}

		static void user_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
		{
			UE_LOG(LogTemp, Warning, TEXT("PNG Warning: %s"), ANSI_TO_TCHAR(warning_msg));
		}

		static void* user_malloc(png_structp png_ptr, png_size_t size)
		{
			check(size > 0);
			return FMemory::Malloc(size);
		}

		static void user_free(png_structp png_ptr, png_voidp struct_ptr)
		{
			check(struct_ptr);
			FMemory::Free(struct_ptr);
		}
	};
}

FPngTextChunkHelper::FPngTextChunkHelper(const FString& FilePath)
{
	TArray<uint8> CompressdData;
	if (!FFileHelper::LoadFileToArray(CompressdData, *FilePath))
	{
		bIsValid = false;
		return;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	if (ImageWrapperModule.DetectImageFormat(CompressdData.GetData(), CompressdData.Num()) != EImageFormat::PNG)
	{
		bIsValid = false;
		return;
	}
	
	ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper->SetCompressed(CompressdData.GetData(), CompressdData.Num()))
	{
		bIsValid = false;
		return;
	}

	bIsValid = true;
}

bool FPngTextChunkHelper::Write(const TMap<FString, FString>& MapToWrite)
{
	if (!bIsValid || !ImageWrapper.IsValid())
	{
		return false;
	}

	const ERGBFormat Format = ImageWrapper->GetFormat();
	const int32 BitDepth = ImageWrapper->GetBitDepth();
	TArray<uint8> RawData;
	ImageWrapper->GetRaw(Format, ImageWrapper->GetBitDepth(), RawData);
	const int32 Height = ImageWrapper->GetHeight();
	const int32 Width = ImageWrapper->GetWidth();
	if (RawData.Num() == 0 || Height == 0 || Width == 0)
	{
		return false;
	}

	png_structp png_ptr = png_create_write_struct(
		PNG_LIBPNG_VER_STRING,
		ImageWrapper.Get(),
		PngTextChunkHelperInternal::FLibPngEventHandler::user_error_fn,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_warning_fn
	);
	png_infop info_ptr = png_create_info_struct(png_ptr);
	png_bytep* row_pointers = (png_bytep*)png_malloc(png_ptr, Height * sizeof(png_bytep));
	PngTextChunkHelperInternal::FPngWriteGuard PNGGuard(&png_ptr, &info_ptr);
	PNGGuard.SetRowPointers(row_pointers);

	if(png_ptr == NULL || info_ptr == NULL)
	{
		return false;
	}

	if (setjmp(png_jmpbuf(png_ptr)) != 0)
	{
		return false;
	}

	png_set_compression_level(png_ptr, Z_BEST_SPEED);
	png_set_IHDR(
		png_ptr, 
		info_ptr, 
		Width, 
		Height, 
		BitDepth, 
		(Format == ERGBFormat::Gray) ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGBA, 
		PNG_INTERLACE_NONE, 
		PNG_COMPRESSION_TYPE_DEFAULT, 
		PNG_FILTER_TYPE_DEFAULT
	);
	png_set_write_fn(
		png_ptr, 
		ImageWrapper.Get(),
		PngTextChunkHelperInternal::FLibPngEventHandler::user_write_compressed,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_flush_data
	);

	const int32 NumText = MapToWrite.Num();
	TArray<png_text> TextPtr;
	TextPtr.Reserve(NumText);
	int32 Index = 0;
	for (const auto& Data : MapToWrite)
	{
		const auto CastedKey = StringCast<ANSICHAR>(*Data.Key);
		const auto CastedValue = StringCast<ANSICHAR>(*Data.Value);

		png_text Text;
		Text.key = const_cast<char*>(CastedKey.Get());
		Text.text = const_cast<char*>(CastedValue.Get());
		Text.text_length = TCString<char>::Strlen(Text.text);
		Text.compression = PNG_TEXT_COMPRESSION_NONE;

		TextPtr.Add(Text);

		Index++;
	}
	png_set_text(png_ptr, info_ptr, TextPtr.GetData(), NumText);

	const uint64 PixelChannels = (Format == ERGBFormat::Gray) ? 1 : 4;
	const uint64 BytesPerPixel = (BitDepth * PixelChannels) / 8;
	const uint64 BytesPerRow = BytesPerPixel * Width;

	for (int64 i = 0; i < Height; i++)
	{
		row_pointers[i] = &RawData[i * BytesPerRow];
	}
	png_set_rows(png_ptr, info_ptr, row_pointers);

	uint32 Transform = (Format == ERGBFormat::BGRA) ? PNG_TRANSFORM_BGR : PNG_TRANSFORM_IDENTITY;
	png_write_png(png_ptr, info_ptr, Transform, NULL);

	return true;
}

bool FPngTextChunkHelper::Read(TMap<FString, FString>& MapToRead)
{
	if (!bIsValid || !ImageWrapper.IsValid())
	{
		return false;
	}

	const ERGBFormat Format = ImageWrapper->GetFormat();
	const int32 BitDepth = ImageWrapper->GetBitDepth();
	TArray<uint8> RawData;
	ImageWrapper->GetRaw(Format, ImageWrapper->GetBitDepth(), RawData);
	const int32 Height = ImageWrapper->GetHeight();
	const int32 Width = ImageWrapper->GetWidth();
	if (RawData.Num() == 0 || Height == 0 || Width == 0 ||
		!(Format == ERGBFormat::BGRA || Format == ERGBFormat::RGBA || Format == ERGBFormat::Gray) ||
		!(BitDepth == 8 || BitDepth == 16))
	{
		return false;
	}

	png_structp png_ptr = png_create_read_struct_2(
		PNG_LIBPNG_VER_STRING, 
		ImageWrapper.Get(), 
		PngTextChunkHelperInternal::FLibPngEventHandler::user_error_fn,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_warning_fn,
		NULL, 
		PngTextChunkHelperInternal::FLibPngEventHandler::user_malloc,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_free
	);
	png_infop info_ptr = png_create_info_struct(png_ptr);
	png_bytep* row_pointers = (png_bytep*)png_malloc(png_ptr, Height * sizeof(png_bytep));
	PngTextChunkHelperInternal::FPngReadGuard PNGGuard(&png_ptr, &info_ptr);
	PNGGuard.SetRowPointers(row_pointers);

	if (png_ptr == NULL || info_ptr == NULL)
	{
		return false;
	}

	if (setjmp(png_jmpbuf(png_ptr)) != 0)
	{
		return false;
	}

	const int32 ColorType = info_ptr->color_type;

	if (ColorType == PNG_COLOR_TYPE_PALETTE)
	{
		png_set_palette_to_rgb(png_ptr);
	}

	if ((ColorType & PNG_COLOR_MASK_COLOR) == 0 && BitDepth < 8)
	{
		png_set_expand_gray_1_2_4_to_8(png_ptr);
	}

	// Insert alpha channel with full opacity for RGB images without alpha
	if ((ColorType & PNG_COLOR_MASK_ALPHA) == 0 && (Format == ERGBFormat::BGRA || Format == ERGBFormat::RGBA))
	{
		// png images don't set PNG_COLOR_MASK_ALPHA if they have alpha from a tRNS chunk, but png_set_add_alpha seems to be safe regardless
		if ((ColorType & PNG_COLOR_MASK_COLOR) == 0)
		{
			png_set_tRNS_to_alpha(png_ptr);
		}
		else if (ColorType == PNG_COLOR_TYPE_PALETTE)
		{
			png_set_tRNS_to_alpha(png_ptr);
		}
		if (BitDepth == 8)
		{
			png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
		}
		else if (BitDepth == 16)
		{
			png_set_add_alpha(png_ptr, 0xffff, PNG_FILLER_AFTER);
		}
	}

	// Calculate Pixel Depth
	const uint64 PixelChannels = (Format == ERGBFormat::Gray) ? 1 : 4;
	const uint64 BytesPerPixel = (BitDepth * PixelChannels) / 8;
	const uint64 BytesPerRow = BytesPerPixel * Width;
	RawData.Empty(Height * BytesPerRow);
	RawData.AddUninitialized(Height * BytesPerRow);

	png_set_read_fn(
		png_ptr, 
		ImageWrapper.Get(), 
		PngTextChunkHelperInternal::FLibPngEventHandler::user_read_compressed
	);

	for (int64 i = 0; i < Height; i++)
	{
		row_pointers[i] = &RawData[i * BytesPerRow];
	}
	png_set_rows(png_ptr, info_ptr, row_pointers);

	uint32 Transform = (Format == ERGBFormat::BGRA) ? PNG_TRANSFORM_BGR : PNG_TRANSFORM_IDENTITY;

	// Convert grayscale png to RGB if requested
	if ((ColorType & PNG_COLOR_MASK_COLOR) == 0 &&
		(Format == ERGBFormat::RGBA || Format == ERGBFormat::BGRA))
	{
		Transform |= PNG_TRANSFORM_GRAY_TO_RGB;
	}

	// Convert RGB png to grayscale if requested
	if ((ColorType & PNG_COLOR_MASK_COLOR) != 0 && Format == ERGBFormat::Gray)
	{
		png_set_rgb_to_gray_fixed(png_ptr, 2 /* warn if image is in color */, -1, -1);
	}

	// Strip alpha channel if requested output is grayscale
	if (Format == ERGBFormat::Gray)
	{
		// this is not necessarily the best option, instead perhaps:
		// png_color background = {0,0,0};
		// png_set_background(png_ptr, &background, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
		Transform |= PNG_TRANSFORM_STRIP_ALPHA;
	}

	png_read_png(png_ptr, info_ptr, Transform, NULL);

	png_textp TextPtr;
	int32 NumText;
	if (!png_get_text(png_ptr, info_ptr, &TextPtr, &NumText))
	{
		return false;
	}

	MapToRead.Reserve(NumText);
	for (int32 Index = 0; Index < NumText; Index++)
	{
		const auto Key = FString(ANSI_TO_TCHAR(TextPtr[Index].key));
		const auto Value = FString(ANSI_TO_TCHAR(TextPtr[Index].text));

		MapToRead.Add(Key, Value);
	}

	return true;
}

// Renable warning "interaction between '_setjmp' and C++ object destruction is non-portable".
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
