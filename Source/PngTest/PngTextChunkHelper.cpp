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

	png_structp WritePtr = png_create_write_struct(
		PNG_LIBPNG_VER_STRING, 
		ImageWrapper.Get(), 
		PngTextChunkHelperInternal::FLibPngEventHandler::user_error_fn,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_warning_fn
	);
	png_infop InfoPtr = png_create_info_struct(WritePtr);
	png_bytep* RowPointers = (png_bytep*)png_malloc(WritePtr, ImageWrapper->GetHeight() * sizeof(png_bytep));

	PngTextChunkHelperInternal::FPngWriteGuard WriteGuard(&WritePtr, &InfoPtr);
	WriteGuard.SetRowPointers(RowPointers);

	if (WritePtr == nullptr || InfoPtr == nullptr)
	{
		return false;
	}

	if (setjmp(png_jmpbuf(WritePtr)) != 0)
	{
		return false;
	}

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

	png_set_write_fn(
		WritePtr, 
		ImageWrapper.Get(), 
		PngTextChunkHelperInternal::FLibPngEventHandler::user_write_compressed,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_flush_data
	);	
	png_set_text(WritePtr, InfoPtr, TextPtr.GetData(), NumText);
	uint32 Transforms = (ImageWrapper->GetFormat() == ERGBFormat::BGRA) ? PNG_TRANSFORM_BGR : PNG_TRANSFORM_IDENTITY;
	png_write_png(WritePtr, InfoPtr, Transforms, NULL);

	return true;
}

bool FPngTextChunkHelper::Read(TMap<FString, FString>& MapToRead)
{
	if (!bIsValid || !ImageWrapper.IsValid())
	{
		return false;
	}

	png_structp ReadPtr = png_create_read_struct_2(
		PNG_LIBPNG_VER_STRING,
		ImageWrapper.Get(),
		PngTextChunkHelperInternal::FLibPngEventHandler::user_error_fn,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_warning_fn,
		NULL,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_malloc,
		PngTextChunkHelperInternal::FLibPngEventHandler::user_free
	);
	png_infop InfoPtr = png_create_info_struct(ReadPtr);

	PngTextChunkHelperInternal::FPngReadGuard ReadGuard(&ReadPtr, &InfoPtr);

	if (ReadPtr == nullptr || InfoPtr == nullptr)
	{
		return false;
	}

	png_read_png(ReadPtr, InfoPtr, PNG_TRANSFORM_IDENTITY, NULL);

	png_textp TextPtr;
	int32 NumText;
	if (!png_get_text(ReadPtr, InfoPtr, &TextPtr, &NumText))
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
