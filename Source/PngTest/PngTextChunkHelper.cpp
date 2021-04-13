// Fill out your copyright notice in the Description page of Project Settings.

#include "PngTextChunkHelper.h"
#include "Misc/FileHelper.h"

#if WITH_UNREALPNG

THIRD_PARTY_INCLUDES_START
#include <setjmp.h>
THIRD_PARTY_INCLUDES_END

// Disable warning "interaction between '_setjmp' and C++ object destruction is non-portable".
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4611)
#endif

namespace PngTextChunkHelperInternal
{
	/**
	 * Classes for safely handling pointers used by libpng.
	 */
	class FPngReadGuard : public FNoncopyable
	{
	public:
		FPngReadGuard(
			png_voidp Context,
			png_error_ptr ErrorCallback,
			png_error_ptr WarningCallback,
			png_malloc_ptr MallocCallback,
			png_free_ptr FreeCallback,
			png_rw_ptr ReadDataCallback
		)
		{
			ReadPtr = png_create_read_struct_2(
				PNG_LIBPNG_VER_STRING, 
				Context, 
				ErrorCallback, 
				WarningCallback,
				nullptr, 
				MallocCallback,
				FreeCallback
			);
			InfoPtr = png_create_info_struct(ReadPtr);
			png_set_read_fn(ReadPtr, Context, ReadDataCallback);
		}

		virtual ~FPngReadGuard()
		{
			png_destroy_read_struct(&ReadPtr, &InfoPtr, nullptr);
		}

		png_structp GetReadPtr() const { return ReadPtr; }
		png_infop GetInfoPtr() const { return InfoPtr; }
		virtual bool IsValid() const { return (ReadPtr != nullptr && InfoPtr != nullptr); }

	private:
		png_structp ReadPtr;
		png_infop InfoPtr;
	};

	class FPngWriteGuard : public FNoncopyable
	{
	public:
		FPngWriteGuard(
			png_voidp Context,
			png_error_ptr ErrorCallback,
			png_error_ptr WarningCallback,
			png_rw_ptr WriteDataCallback,
			png_flush_ptr OutputFlushCallback
		)
		{
			WritePtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, Context, ErrorCallback, WarningCallback);
			InfoPtr = png_create_info_struct(WritePtr);
			png_set_write_fn(WritePtr, Context, WriteDataCallback, OutputFlushCallback);
		}

		~FPngWriteGuard()
		{
			png_destroy_write_struct(&WritePtr, &InfoPtr);
		}

		png_structp GetWritePtr() const { return WritePtr; }
		png_infop GetInfoPtr() const { return InfoPtr; }
		bool IsValid() const { return (WritePtr != nullptr && InfoPtr != nullptr); }

	private:
		png_structp WritePtr;
		png_infop InfoPtr;
	};

	void CopyPngInfoStruct(png_structp DestinationPngPtr, png_infop DestinationInfoPtr, png_structp SourcePngPtr, png_infop SourceInfoPtr)
	{
		check(DestinationPngPtr != nullptr && DestinationInfoPtr != nullptr);
		check(SourcePngPtr != nullptr && SourceInfoPtr != nullptr);

		// Set compression level.
		{
			png_set_compression_level(DestinationPngPtr, Z_BEST_SPEED);
		}
		// Copy IHDR.
		{
			png_uint_32 Width;
			png_uint_32 Height;
			int32 BitDepth;
			int32 ColorType;
			int32 InterlaceMethod;
			int32 CompressionMethod;
			int32 FilterMethod;
			png_get_IHDR(SourcePngPtr, SourceInfoPtr, &Width, &Height, &BitDepth, &ColorType, &InterlaceMethod, &CompressionMethod, &FilterMethod);
			png_set_IHDR(DestinationPngPtr, DestinationInfoPtr, Width, Height, BitDepth, ColorType, InterlaceMethod, CompressionMethod, FilterMethod);
		}
		// Copy row pointers.
		{
			png_bytepp RowPointers = png_get_rows(SourcePngPtr, SourceInfoPtr);
			png_set_rows(DestinationPngPtr, DestinationInfoPtr, RowPointers);
		}
		// Copy channels.
		{
			DestinationInfoPtr->channels = SourceInfoPtr->channels;
		}
	}
}

// Only allow one thread to use libpng at a time.
FCriticalSection GPngTextChunkHelperSection;

FPngTextChunkHelper::FPngTextChunkHelper()
	: Filename(TEXT(""))
	, ReadOffset(0)
{
}

TSharedPtr<FPngTextChunkHelper> FPngTextChunkHelper::CreatePngTextChunkHelper(const FString& InFilename)
{
	TArray<uint8> CompressdData;
	if (!FFileHelper::LoadFileToArray(CompressdData, *InFilename))
	{
		return nullptr;
	}

	TSharedPtr<FPngTextChunkHelper> PngTextChunkHelper = MakeShared<FPngTextChunkHelper>();
	if (!PngTextChunkHelper->Initialize(InFilename, CompressdData.GetData(), CompressdData.Num()))
	{
		return nullptr;
	}

	return PngTextChunkHelper;
}

bool FPngTextChunkHelper::Write(const TMap<FString, FString>& MapToWrite)
{
	check(IsPng());

	// Only allow one thread to use libpng at a time.
	FScopeLock PNGLock(&GPngTextChunkHelperSection);
	
	// Reset to the beginning of file so we can use png_read_png(), which expects to start at the beginning.
	ReadOffset = 0;

	// Read the png_info of the original image file.
	PngTextChunkHelperInternal::FPngReadGuard ReadGuard(
		this,
		FPngTextChunkHelper::UserError,
		FPngTextChunkHelper::UserWarning,
		FPngTextChunkHelper::UserMalloc,
		FPngTextChunkHelper::UserFree,
		FPngTextChunkHelper::UserReadCompressed
	);
	if (!ReadGuard.IsValid())
	{
		return false;
	}

	if (setjmp(png_jmpbuf(ReadGuard.GetReadPtr())) != 0)
	{
		return false;
	}

	png_read_png(ReadGuard.GetReadPtr(), ReadGuard.GetInfoPtr(), PNG_TRANSFORM_IDENTITY, nullptr);

	// Prepare png_struct etc. for writing.
	PngTextChunkHelperInternal::FPngWriteGuard WriteGuard(
		this,
		FPngTextChunkHelper::UserError,
		FPngTextChunkHelper::UserWarning,
		FPngTextChunkHelper::UserWriteCompressed,
		FPngTextChunkHelper::UserFlushData
	);
	if (!WriteGuard.IsValid())
	{
		return false;
	}

	if (setjmp(png_jmpbuf(WriteGuard.GetWritePtr())) != 0)
	{
		return false;
	}

	// Copy the read original png_info to png_info used for write.
	PngTextChunkHelperInternal::CopyPngInfoStruct(
		WriteGuard.GetWritePtr(), WriteGuard.GetInfoPtr(),
		ReadGuard.GetReadPtr(), ReadGuard.GetInfoPtr()
	);
	
	// Create text chunk data and set it to png.
	const int32 NumText = MapToWrite.Num();
	TArray<png_text> TextPtr;
	TextPtr.Reserve(NumText);
	for (const auto& Data : MapToWrite)
	{
		png_text Text;
		FMemory::Memzero(Text);

		auto Key = StringCast<ANSICHAR>(*Data.Key);
		const int32 KeyLength = TCString<ANSICHAR>::Strlen(Key.Get());
		auto KeyBuffer = static_cast<ANSICHAR*>(FMemory::Malloc(KeyLength));
		TCString<ANSICHAR>::Strcpy(KeyBuffer, KeyLength, Key.Get());
		Text.key = KeyBuffer;

		auto Value = StringCast<ANSICHAR>(*Data.Value);
		const int32 ValueLength = TCString<ANSICHAR>::Strlen(Value.Get());
		auto ValueBuffer = static_cast<ANSICHAR*>(FMemory::Malloc(ValueLength));
		TCString<ANSICHAR>::Strcpy(ValueBuffer, ValueLength, Value.Get());
		Text.text = ValueBuffer;

		Text.text_length = ValueLength;
		Text.itxt_length = 0;
		Text.compression = PNG_TEXT_COMPRESSION_NONE;
		TextPtr.Add(Text);
	}
	png_set_text(WriteGuard.GetWritePtr(), WriteGuard.GetInfoPtr(), TextPtr.GetData(), NumText);

	// Write the data prepared so far to the png file.
	CompressedData.Empty();
	png_write_png(WriteGuard.GetWritePtr(), WriteGuard.GetInfoPtr(), PNG_TRANSFORM_IDENTITY, nullptr);

	return FFileHelper::SaveArrayToFile(CompressedData, *Filename);
}

bool FPngTextChunkHelper::Read(TMap<FString, FString>& MapToRead)
{
	check(IsPng());

	// Only allow one thread to use libpng at a time.
	FScopeLock PNGLock(&GPngTextChunkHelperSection);

	// Reset to the beginning of file so we can use png_read_png(), which expects to start at the beginning.
	ReadOffset = 0;

	// Read png_info.
	PngTextChunkHelperInternal::FPngReadGuard ReadGuard(
		this, 
		FPngTextChunkHelper::UserError, 
		FPngTextChunkHelper::UserWarning, 
		FPngTextChunkHelper::UserMalloc, 
		FPngTextChunkHelper::UserFree,
		FPngTextChunkHelper::UserReadCompressed
	);
	if (!ReadGuard.IsValid())
	{
		return false;
	}

	if (setjmp(png_jmpbuf(ReadGuard.GetReadPtr())) != 0)
	{
		return false;
	}

	png_read_info(ReadGuard.GetReadPtr(), ReadGuard.GetInfoPtr());

	// Transfer the text chunk data to the argument map.
	png_textp TextPtr;
	int32 NumText;
	if (!png_get_text(ReadGuard.GetReadPtr(), ReadGuard.GetInfoPtr(), &TextPtr, &NumText))
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

bool FPngTextChunkHelper::Initialize(const FString& InFilename, const void* InCompressedData, int64 InCompressedSize)
{
	FText FailedReason;
	if (!FPaths::ValidatePath(InFilename, &FailedReason))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *FailedReason.ToString());
		return false;
	}

	check(InCompressedData != nullptr && InCompressedSize > 0);

	Filename = InFilename;
	CompressedData.Empty(InCompressedSize);
	CompressedData.AddUninitialized(InCompressedSize);
	FMemory::Memcpy(CompressedData.GetData(), InCompressedData, InCompressedSize);

	return IsPng();
}

bool FPngTextChunkHelper::IsPng() const
{
	// Determine if this file is in Png format from the Png signature size.
	const int32 PngSignatureSize = sizeof(png_size_t);
	if (CompressedData.Num() > PngSignatureSize)
	{
		png_size_t PngSignature = *reinterpret_cast<const png_size_t*>(CompressedData.GetData());
		return (0 == png_sig_cmp(reinterpret_cast<png_bytep>(&PngSignature), 0, PngSignatureSize));
	}

	return false;
}

void FPngTextChunkHelper::UserReadCompressed(png_structp PngPtr, png_bytep Data, png_size_t Length)
{
	FPngTextChunkHelper* Context = reinterpret_cast<FPngTextChunkHelper*>(png_get_io_ptr(PngPtr));
	if (Context == nullptr)
	{
		UE_LOG(LogTemp, Fatal, TEXT("[%s] Context is invalid."), GET_FUNCTION_NAME_STRING_CHECKED(FPngTextChunkHelper, UserReadCompressed));
	}

	if (Context->ReadOffset + static_cast<int64>(Length) <= Context->CompressedData.Num())
	{
		FMemory::Memcpy(Data, &Context->CompressedData[Context->ReadOffset], Length);
		Context->ReadOffset += Length;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid read position for CompressedData."));
	}
}

void FPngTextChunkHelper::UserWriteCompressed(png_structp PngPtr, png_bytep Data, png_size_t Length)
{
	FPngTextChunkHelper* Context = reinterpret_cast<FPngTextChunkHelper*>(png_get_io_ptr(PngPtr));
	if (Context == nullptr)
	{
		UE_LOG(LogTemp, Fatal, TEXT("[%s] Context is invalid."), GET_FUNCTION_NAME_STRING_CHECKED(FPngTextChunkHelper, UserWriteCompressed));
	}

	int64 Offset = Context->CompressedData.AddUninitialized(Length);
	FMemory::Memcpy(&Context->CompressedData[Offset], Data, Length);
}

void FPngTextChunkHelper::UserFlushData(png_structp PngPtr)
{
}

void FPngTextChunkHelper::UserError(png_structp PngPtr, png_const_charp ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("libPng Error : %s"), ANSI_TO_TCHAR(ErrorMessage));
}

void FPngTextChunkHelper::UserWarning(png_structp PngPtr, png_const_charp WarningMessage)
{
	UE_LOG(LogTemp, Warning, TEXT("libPng Warning : %s"), ANSI_TO_TCHAR(WarningMessage));
}

void* FPngTextChunkHelper::UserMalloc(png_structp PngPtr, png_size_t Size)
{
	check(Size > 0);
	return FMemory::Malloc(Size);
}

void FPngTextChunkHelper::UserFree(png_structp PngPtr, png_voidp StructPtr)
{
	check(StructPtr);
	FMemory::Free(StructPtr);
}

// Renable warning "interaction between '_setjmp' and C++ object destruction is non-portable".
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
