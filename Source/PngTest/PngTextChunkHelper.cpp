// Fill out your copyright notice in the Description page of Project Settings.

#include "PngTextChunkHelper.h"
#include "Misc/FileHelper.h"
#include "IImageWrapperModule.h"

#if WITH_UNREALPNG

// Disable warning "interaction between '_setjmp' and C++ object destruction is non-portable".
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4611)
#endif

FCriticalSection GPngTextChunkHelperSection;

namespace PngTextChunkHelperInternal
{
	class FPngReadGuard
	{
	public:
		FPngReadGuard(png_structp* InReadPtr, png_infop* InInfoPtr)
			: ReadPtr(InReadPtr)
			, InfoPtr(InInfoPtr)
		{
		}

		~FPngReadGuard()
		{
			png_destroy_read_struct(ReadPtr, InfoPtr, NULL);
		}

	private:
		png_structp* ReadPtr;
		png_infop* InfoPtr;
	};

	class FPngWriteGuard
	{
	public:
		FPngWriteGuard(png_structp* InWritePtr, png_infop* InInfoPtr)
			: WritePtr(InWritePtr)
			, InfoPtr(InInfoPtr)
		{
		}

		~FPngWriteGuard()
		{
			png_destroy_write_struct(WritePtr, InfoPtr);
		}

	private:
		png_structp* WritePtr;
		png_infop* InfoPtr;
	};
}

FPngTextChunkHelper::FPngTextChunkHelper()
	: ReadOffset(0)
{
}

TSharedPtr<FPngTextChunkHelper> FPngTextChunkHelper::CreatePngTextChunkHelper(const FString& Filename)
{
	TArray<uint8> CompressdData;
	if (!FFileHelper::LoadFileToArray(CompressdData, *Filename))
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	if (ImageWrapperModule.DetectImageFormat(CompressdData.GetData(), CompressdData.Num()) != EImageFormat::PNG)
	{
		return nullptr;
	}

	TSharedPtr<FPngTextChunkHelper> PngTextChunkHelper = MakeShareable(new FPngTextChunkHelper());
	if (!PngTextChunkHelper->SetCompressed(CompressdData.GetData(), CompressdData.Num()))
	{
		return nullptr;
	}

	return PngTextChunkHelper;
}

const TArray64<uint8>& FPngTextChunkHelper::GetCompressed(int32 Quality)
{
	LastError.Empty();
	return CompressedData;
}

bool FPngTextChunkHelper::SetCompressed(const void* InCompressedData, int64 InCompressedSize)
{
	if (InCompressedSize > 0 && InCompressedData != nullptr)
	{
		RawData.Empty();

		CompressedData.Empty(InCompressedSize);
		CompressedData.AddUninitialized(InCompressedSize);
		FMemory::Memcpy(CompressedData.GetData(), InCompressedData, InCompressedSize);

		return true;
	}

	return false;
}

bool FPngTextChunkHelper::IsPNG() const
{
	if (CompressedData.Num() == 0)
	{
		return false;
	}

	const int32 PNGSigSize = sizeof(png_size_t);

	if (CompressedData.Num() > PNGSigSize)
	{
		png_size_t PNGSignature = *reinterpret_cast<const png_size_t*>(CompressedData.GetData());
		return (0 == png_sig_cmp(reinterpret_cast<png_bytep>(&PNGSignature), 0, PNGSigSize));
	}

	return false;
}

#pragma optimize("", off)
bool FPngTextChunkHelper::Write(const TMap<FString, FString>& MapToWrite)
{
	if (!IsPNG())
	{
		return false;
	}

	// Thread safety.
	FScopeLock PNGLock(&GPngTextChunkHelperSection);
	
	// Read the png_info of the original image file.
	png_structp ReadPtr = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, this, FPngTextChunkHelper::user_error_fn, FPngTextChunkHelper::user_warning_fn, NULL, FPngTextChunkHelper::user_malloc, FPngTextChunkHelper::user_free);
	png_infop OriginalInfoPtr = png_create_info_struct(ReadPtr);
	PngTextChunkHelperInternal::FPngReadGuard ReadGuard(&ReadPtr, &OriginalInfoPtr);
	if (ReadPtr == nullptr || OriginalInfoPtr == nullptr)
	{
		return false;
	}

	// Prepare png_struct etc. for writing.
	png_structp WritePtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, this, FPngTextChunkHelper::user_error_fn, FPngTextChunkHelper::user_warning_fn);
	png_infop InfoPtr = png_create_info_struct(WritePtr);
	PngTextChunkHelperInternal::FPngWriteGuard WriteGuard(&WritePtr, &InfoPtr);
	if (WritePtr == nullptr || InfoPtr == nullptr)
	{
		return false;
	}

	// Copy the read original png_info.
	png_set_read_fn(ReadPtr, this, FPngTextChunkHelper::user_read_compressed);
	png_read_info(ReadPtr, OriginalInfoPtr);
	FMemory::Memcpy(InfoPtr, OriginalInfoPtr, sizeof(png_info));
	
	// Overwrite text chunks and write to file.
	png_set_write_fn(WritePtr, this, FPngTextChunkHelper::user_write_compressed, FPngTextChunkHelper::user_flush_data);

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
	png_set_text(WritePtr, InfoPtr, TextPtr.GetData(), NumText);
	png_write_info(WritePtr, InfoPtr);
	png_write_png(WritePtr, InfoPtr, PNG_TRANSFORM_IDENTITY, nullptr);

	return true;
}
#pragma optimize("", on)

bool FPngTextChunkHelper::Read(TMap<FString, FString>& MapToRead)
{
	if (!IsPNG())
	{
		return false;
	}

	// Thread safety.
	FScopeLock PNGLock(&GPngTextChunkHelperSection);

	png_structp png_ptr = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, this, FPngTextChunkHelper::user_error_fn, FPngTextChunkHelper::user_warning_fn, NULL, FPngTextChunkHelper::user_malloc, FPngTextChunkHelper::user_free);
	png_infop info_ptr = png_create_info_struct(png_ptr);
	PngTextChunkHelperInternal::FPngReadGuard PNGGuard(&png_ptr, &info_ptr);
	if (png_ptr == nullptr || info_ptr == nullptr)
	{
		return false;
	}

	png_set_read_fn(png_ptr, this, FPngTextChunkHelper::user_read_compressed);
	png_read_info(png_ptr, info_ptr);

	png_textp TextPtr;
	int32 NumText;
	if (!png_get_text(png_ptr, info_ptr, &TextPtr, &NumText))
	{
		return false;
	}

	for (int32 Index = 0; Index < NumText; Index++)
	{
		const auto Key = FString(ANSI_TO_TCHAR(TextPtr[Index].key));
		const auto Value = FString(ANSI_TO_TCHAR(TextPtr[Index].text));

		MapToRead.Add(Key, Value);
	}

	return true;
}

void FPngTextChunkHelper::user_read_compressed(png_structp png_ptr, png_bytep data, png_size_t length)
{
	FPngTextChunkHelper* ctx = (FPngTextChunkHelper*)png_get_io_ptr(png_ptr);
	if (ctx->ReadOffset + (int64)length <= ctx->CompressedData.Num())
	{
		FMemory::Memcpy(data, &ctx->CompressedData[ctx->ReadOffset], length);
		ctx->ReadOffset += length;
	}
	else
	{
		ctx->SetError(TEXT("Invalid read position for CompressedData."));
	}
}

void FPngTextChunkHelper::user_write_compressed(png_structp png_ptr, png_bytep data, png_size_t length)
{
	FPngTextChunkHelper* ctx = (FPngTextChunkHelper*)png_get_io_ptr(png_ptr);

	int64 Offset = ctx->CompressedData.AddUninitialized(length);
	FMemory::Memcpy(&ctx->CompressedData[Offset], data, length);
}

void FPngTextChunkHelper::user_flush_data(png_structp png_ptr)
{
}

void FPngTextChunkHelper::user_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
	FPngTextChunkHelper* ctx = (FPngTextChunkHelper*)png_get_error_ptr(png_ptr);
	{
		FString ErrorMsg = ANSI_TO_TCHAR(error_msg);
		ctx->SetError(*ErrorMsg);

		UE_LOG(LogTemp, Error, TEXT("PNG Error: %s"), *ErrorMsg);
	}
}

void FPngTextChunkHelper::user_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
	UE_LOG(LogTemp, Warning, TEXT("PNG Warning: %s"), ANSI_TO_TCHAR(warning_msg));
}

void* FPngTextChunkHelper::user_malloc(png_structp /*png_ptr*/, png_size_t size)
{
	check(size > 0);
	return FMemory::Malloc(size);
}

void FPngTextChunkHelper::user_free(png_structp /*png_ptr*/, png_voidp struct_ptr)
{
	check(struct_ptr);
	FMemory::Free(struct_ptr);
}

// Renable warning "interaction between '_setjmp' and C++ object destruction is non-portable".
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
