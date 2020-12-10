// Fill out your copyright notice in the Description page of Project Settings.

#include "MyBlueprintFunctionLibrary.h"

THIRD_PARTY_INCLUDES_START
#include "ThirdParty/zlib/zlib-1.2.5/Inc/zlib.h"
#include "ThirdParty/libPNG/libPNG-1.5.2/png.h"
#include "ThirdParty/libPNG/libPNG-1.5.2/pnginfo.h"
#include "ThirdParty/libPNG/libPNG-1.5.2/pngstruct.h"
#include <setjmp.h>
THIRD_PARTY_INCLUDES_END

class FPngTextChunkHelpers
{
public:
	// The signature length for PNG.
	static const int32 SignatureLength = 8;
	// Size of a byte.
	static const int32 ByteSize = 8;
	// The number of bits used for storing the length of a file.
	static const int32 SizeWidth = 32;

public:
	FPngTextChunkHelpers(const FString& InFilename)
	{
		FILE* inputFile;

		unsigned char header[ByteSize];

		//Check if the file opened
		check(fopen_s(&inputFile, TCHAR_TO_ANSI(*InFilename), "rb") == 0);

		// START READING HERE

		fread(header, 1, SignatureLength, inputFile);

		//Check if it is a PNG
		check(!png_sig_cmp(header, 0, SignatureLength));


		//Set up libPNG data structures and error handling
		read_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

		check(read_ptr);

		info_ptr = png_create_info_struct(read_ptr);

		if (!info_ptr)
		{
			png_destroy_read_struct(&read_ptr, (png_infopp)NULL, (png_infopp)NULL);
			checkNoEntry();
		}

		png_infop end_info = png_create_info_struct(read_ptr);

		if (!end_info)
		{
			png_destroy_read_struct(&read_ptr, &info_ptr, (png_infopp)NULL);
			checkNoEntry();
		}
		//End data structure/error handling setup

		//Initialize IO for PNG
		png_init_io(read_ptr, inputFile);

		//Alert libPNG that we read PNG_SIG_LENGTH bytes at the beginning
		png_set_sig_bytes(read_ptr, SignatureLength);

		//Read the entire PNG image into memory
		png_read_png(read_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

		row_pointers = png_get_rows(read_ptr, info_ptr);

		//Make sure the bit depth is correct
		check(read_ptr->bit_depth == ByteSize);

		fclose(inputFile);
	}

	virtual ~FPngTextChunkHelpers()
	{
		png_destroy_read_struct(&read_ptr, &info_ptr, NULL);
	}

protected:
	png_bytep* row_pointers;
	png_infop info_ptr;
	png_structp read_ptr;
};

class FPngTextChunkReader : public FPngTextChunkHelpers
{
public:
	FPngTextChunkReader(const FString& InFilename) : FPngTextChunkHelpers(InFilename) {}

	void readTextChunk(TArray<FString>& TextChunk)
	{
		check(info_ptr);

		png_textp text_ptr;
		int32 num_text;
		if (png_get_text(read_ptr, info_ptr, &text_ptr, &num_text))
		{
			for (int32 index = 0; index < num_text; index++)
			{
				TextChunk.Add(ANSI_TO_TCHAR(text_ptr[index].key));
			}
		}
	}
};

class FPngTextChunkWriter : public FPngTextChunkHelpers
{
public:
	FPngTextChunkWriter(const FString& InFilename) : FPngTextChunkHelpers(InFilename), filename(InFilename) {}

	~FPngTextChunkWriter()
	{
		png_destroy_write_struct(&write_ptr, &info_ptr);
	}

	void writeTextChunk(const FString& text)
	{
		FILE* outputFile;
		check(fopen_s(&outputFile, TCHAR_TO_ANSI(*filename), "wb") == 0);

		write_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		check(write_ptr);

		png_init_io(write_ptr, outputFile);

		png_text text_ptr[1];

		text_ptr[0].key = TCHAR_TO_ANSI(*text);
		text_ptr[0].compression = PNG_TEXT_COMPRESSION_NONE;

		png_set_text(write_ptr, info_ptr, text_ptr, 1);

		png_set_rows(write_ptr, info_ptr, row_pointers);

		//Write the rows to the file
		png_write_png(write_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

		fclose(outputFile);
	}

private:
	png_structp write_ptr;
	FString filename;
};

static const FString& Filename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Test.png")));

bool UMyBlueprintFunctionLibrary::Write(const FString& InText)
{
	//PNG_file link = PNG_file(TCHAR_TO_ANSI(*Filename));
	//link.writeTextChunk(TCHAR_TO_ANSI(*Filename), TCHAR_TO_ANSI(*InText));

	FPngTextChunkWriter Writer(Filename);
	Writer.writeTextChunk(InText);
	return true;
}

bool UMyBlueprintFunctionLibrary::Read(TArray<FString>& TextChunk)
{
	//PNG_file link = PNG_file(TCHAR_TO_ANSI(*Filename));
	//link.readTextChunk(TextChunk);

	FPngTextChunkReader Reader(Filename);
	Reader.readTextChunk(TextChunk);
	return true;
}
