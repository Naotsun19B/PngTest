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
}

FPngTextChunkHelper::FPngTextChunkHelper()
	: RawFormat(ERGBFormat::Invalid)
	, RawBitDepth(0)
	, Format(ERGBFormat::Invalid)
	, BitDepth(0)
	, Width(0)
	, Height(0)
	, ReadOffset(0)
	, ColorType(0)
	, Channels(0)
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

bool FPngTextChunkHelper::SetRaw(const void * InRawData, int64 InRawSize, const int32 InWidth, const int32 InHeight, const ERGBFormat InFormat, const int32 InBitDepth)
{
	check(InRawData != NULL);
	check(InRawSize > 0);
	check(InWidth > 0);
	check(InHeight > 0);

	Reset();
	CompressedData.Empty();

	RawData.Empty(InRawSize);
	RawData.AddUninitialized(InRawSize);
	FMemory::Memcpy(RawData.GetData(), InRawData, InRawSize);

	RawFormat = InFormat;
	RawBitDepth = InBitDepth;

	Width = InWidth;
	Height = InHeight;

	return true;
}

const TArray64<uint8>& FPngTextChunkHelper::GetCompressed(int32 Quality)
{
	LastError.Empty();
	Compress(Quality);

	return CompressedData;
}

bool FPngTextChunkHelper::SetCompressed(const void* InCompressedData, int64 InCompressedSize)
{
	if (InCompressedSize > 0 && InCompressedData != nullptr)
	{
		Reset();
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

bool FPngTextChunkHelper::Read(TMap<FString, FString>& MapToRead)
{
	if (CompressedData.Num() == 0)
	{
		return false;
	}

	// Test whether the data this PNGLoader is pointing at is a PNG or not.
	if (!IsPNG())
	{
		return false;
	}

	// thread safety
	FScopeLock PNGLock(&GPngTextChunkHelperSection);

	png_structp png_ptr = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, this, FPngTextChunkHelper::user_error_fn, FPngTextChunkHelper::user_warning_fn, NULL, FPngTextChunkHelper::user_malloc, FPngTextChunkHelper::user_free);
	if (png_ptr == NULL)
	{
		return false;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		return false;
	}

	PngTextChunkHelperInternal::FPngReadGuard PNGGuard(&png_ptr, &info_ptr);
	{
		png_set_read_fn(png_ptr, this, FPngTextChunkHelper::user_read_compressed);
		png_read_info(png_ptr, info_ptr);

		Width = info_ptr->width;
		Height = info_ptr->height;
		ColorType = info_ptr->color_type;
		BitDepth = info_ptr->bit_depth;
		Channels = info_ptr->channels;
		Format = (ColorType & PNG_COLOR_MASK_COLOR) ? ERGBFormat::RGBA : ERGBFormat::Gray;

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
	}

	return true;
}

void FPngTextChunkHelper::Reset()
{
	LastError.Empty();

	RawFormat = ERGBFormat::Invalid;
	RawBitDepth = 0;
	Format = ERGBFormat::Invalid;
	BitDepth = 0;
	Width = 0;
	Height = 0;
	ReadOffset = 0;
	ColorType = 0;
	Channels = 0;
}

void FPngTextChunkHelper::Compress(int32 Quality)
{
	if (!CompressedData.Num())
	{
		check(RawData.Num());
		check(Width > 0);
		check(Height > 0);

		// Reset to the beginning of file so we can use png_read_png(), which expects to start at the beginning.
		ReadOffset = 0;

		png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, this, FPngTextChunkHelper::user_error_fn, FPngTextChunkHelper::user_warning_fn);
		check(png_ptr);

		png_infop info_ptr = png_create_info_struct(png_ptr);
		check(info_ptr);

		png_bytep* row_pointers = (png_bytep*)png_malloc(png_ptr, Height * sizeof(png_bytep));
		PngTextChunkHelperInternal::FPngWriteGuard PNGGuard(&png_ptr, &info_ptr);
		PNGGuard.SetRowPointers(row_pointers);

		//Use libPNG jump buffer solution to allow concurrent compression\decompression on concurrent threads.
		if (setjmp(png_jmpbuf(png_ptr)) != 0)
		{
			return;
		}

		// ---------------------------------------------------------------------------------------------------------
		// Anything allocated on the stack after this point will not be destructed correctly in the case of an error
		{
			png_set_compression_level(png_ptr, Z_BEST_SPEED);
			png_set_IHDR(png_ptr, info_ptr, Width, Height, RawBitDepth, (RawFormat == ERGBFormat::Gray) ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			png_set_write_fn(png_ptr, this, FPngTextChunkHelper::user_write_compressed, FPngTextChunkHelper::user_flush_data);

			TMap<FString, FString> MapToWrite;
			MapToWrite.Add(TEXT("Test"), TEXT("Hello World"));
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

			const uint64 PixelChannels = (RawFormat == ERGBFormat::Gray) ? 1 : 4;
			const uint64 BytesPerPixel = (RawBitDepth * PixelChannels) / 8;
			const uint64 BytesPerRow = BytesPerPixel * Width;

			for (int64 i = 0; i < Height; i++)
			{
				row_pointers[i] = &RawData[i * BytesPerRow];
			}
			png_set_rows(png_ptr, info_ptr, row_pointers);

			uint32 Transform = (RawFormat == ERGBFormat::BGRA) ? PNG_TRANSFORM_BGR : PNG_TRANSFORM_IDENTITY;
			png_write_png(png_ptr, info_ptr, Transform, NULL);
		}
	}
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

//bool FPngTextChunkHelper::Write(const TMap<FString, FString>& MapToWrite)
//{
//	
//	return true;
//}
//
//bool FPngTextChunkHelper::Read(TMap<FString, FString>& MapToRead)
//{
//	
//	return true;
//}

// Renable warning "interaction between '_setjmp' and C++ object destruction is non-portable".
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
