// Fill out your copyright notice in the Description page of Project Settings.

#include "PngTextChunkHelper.h"
#include "Misc/FileHelper.h"

#if WITH_UNREALPNG

THIRD_PARTY_INCLUDES_START
#include <string>
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
			png_free_ptr FreeCallback
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
		}

		~FPngReadGuard()
		{
			png_destroy_read_struct(&ReadPtr, &InfoPtr, nullptr);
		}

		png_structp GetReadPtr() const { return ReadPtr; }
		png_infop GetInfoPtr() const { return InfoPtr; }
		bool IsValid() const { return (ReadPtr != nullptr && InfoPtr != nullptr); }

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
			png_error_ptr WarningCallback
		)
		{
			WritePtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, Context, ErrorCallback, WarningCallback);
			InfoPtr = png_create_info_struct(WritePtr);
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
	if (!IsPng())
	{
		return false;
	}

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
		FPngTextChunkHelper::UserFree
	);
	if (!ReadGuard.IsValid())
	{
		return false;
	}

	if (setjmp(png_jmpbuf(ReadGuard.GetReadPtr())) != 0)
	{
		return false;
	}

	png_set_read_fn(ReadGuard.GetReadPtr(), this, FPngTextChunkHelper::UserReadCompressed);
	png_read_png(ReadGuard.GetReadPtr(), ReadGuard.GetInfoPtr(), PNG_TRANSFORM_IDENTITY, nullptr);

	// Prepare png_struct etc. for writing.
	PngTextChunkHelperInternal::FPngWriteGuard WriteGuard(
		this, 
		FPngTextChunkHelper::UserError, 
		FPngTextChunkHelper::UserWarning
	);
	if (!WriteGuard.IsValid())
	{
		return false;
	}

	if (setjmp(png_jmpbuf(WriteGuard.GetWritePtr())) != 0)
	{
		return false;
	}

	// Copy the read original png_info.
	FMemory::Memcpy(WriteGuard.GetInfoPtr(), ReadGuard.GetInfoPtr(), sizeof(png_info));
	
	// Create text chunk data and set it to png..
	const int32 NumText = MapToWrite.Num();
	TArray<png_text> TextPtr;
	TextPtr.Reserve(NumText);
	TArray<std::string> Strings;
	for (const auto& Data : MapToWrite)
	{
		auto& Key = Strings.Add_GetRef(StringCast<ANSICHAR>(*Data.Key).Get());
		auto& Value = Strings.Add_GetRef(StringCast<ANSICHAR>(*Data.Value).Get());

		png_text Text;
		Text.key = const_cast<char*>(Key.data());
		Text.text = const_cast<char*>(Value.data());
		Text.text_length = TCString<char>::Strlen(Text.text);
		Text.compression = PNG_TEXT_COMPRESSION_NONE;

		TextPtr.Add(Text);
	}
	png_set_text(WriteGuard.GetWritePtr(), WriteGuard.GetInfoPtr(), TextPtr.GetData(), NumText);
	
	// Get the pixel data from the read png_struct and png_info and set it to png.
	png_bytepp RowPointers = png_get_rows(ReadGuard.GetReadPtr(), ReadGuard.GetInfoPtr());
	png_set_rows(WriteGuard.GetWritePtr(), WriteGuard.GetInfoPtr(), RowPointers);

	// Write the data prepared so far to the png file.
	png_set_write_fn(WriteGuard.GetWritePtr(), this, FPngTextChunkHelper::UserWriteCompressed, FPngTextChunkHelper::UserFlushData);
	png_write_png(WriteGuard.GetWritePtr(), WriteGuard.GetInfoPtr(), PNG_TRANSFORM_IDENTITY, nullptr);

	// Release the acquired pixel data.
	if (RowPointers != nullptr)
	{
		png_free(ReadGuard.GetReadPtr(), RowPointers);
	}

	return FFileHelper::SaveArrayToFile(CompressedData, *Filename);
}

bool FPngTextChunkHelper::Read(TMap<FString, FString>& MapToRead)
{
	if (!IsPng())
	{
		return false;
	}

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
		FPngTextChunkHelper::UserFree
	);
	if (!ReadGuard.IsValid())
	{
		return false;
	}

	if (setjmp(png_jmpbuf(ReadGuard.GetReadPtr())) != 0)
	{
		return false;
	}

	png_set_read_fn(ReadGuard.GetReadPtr(), this, FPngTextChunkHelper::UserReadCompressed);
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

	if (InCompressedSize == 0 || InCompressedData == nullptr)
	{
		return false;
	}

	Filename = InFilename;
	CompressedData.Empty(InCompressedSize);
	CompressedData.AddUninitialized(InCompressedSize);
	FMemory::Memcpy(CompressedData.GetData(), InCompressedData, InCompressedSize);

	return IsPng();
}

bool FPngTextChunkHelper::IsPng() const
{
	if (CompressedData.Num() == 0)
	{
		return false;
	}

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
