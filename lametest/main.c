//
//  main.c
//  lametest
//
//  Created by Lucius Kwok on 12/10/19.
//  Copyright © 2019 Lucius Kwok. All rights reserved.
//

/* Tips:
	- In order to run this, the LAME.framework folder will need to be copied into the same folder as the executable file.
	- This command line tool takes only 1 argument: the input file name. This is set to "test.wav" by the Xcode scheme under "Arguements Passed On Launch".
 */

#include <stdio.h>
#include <string.h>
#include "LAME/lame.h"


/* WAV file structs */

#pragma pack(push, 2)

typedef struct {
	uint32_t	ckID;				// should be 'fmt '
	uint32_t	ckSize;				// size doesn't include the 8 bytes which make up the header
	int16_t		formatTag;			// Wave Format ID: see constants
	uint16_t	channels;			// Number of Channels: 1=mono, 2=stereo
	uint32_t	sampleRate;			// Sample Rate: samples per second
	uint32_t	bytesPerSec;		// sampleRate * blockAlign
	uint16_t	blockAlign;			// sample frame size = channels * sampleSize / 8
	uint16_t	bitsPerSample;		// sampleSize (8 or 16), also two's-complement for 16-bit, offset for 8-bit
} FormatChunk;

#pragma pack(pop)


fpos_t offset_of_chunk(uint32_t chunk, fpos_t start, FILE *fp) {
	fseek(fp, start, SEEK_SET);
	uint32_t chunkHeader[2];
	unsigned long count;
	
	while (1) {
		count = fread(chunkHeader, 4, 2, fp);
		if (count < 2) {
			return 0; // not found
		}
		if (_OSSwapInt32(chunkHeader[0]) == chunk) {
			fpos_t mark = 0;
			fgetpos(fp, &mark);
			return mark - 8;
		}
		fseek(fp, chunkHeader[1], SEEK_CUR);
	}
}

int read_wav_header(FILE *fp, fpos_t *outDataOffset, uint32_t *outDataLength, uint16_t *outChannels, uint32_t *outSampleRate) {
	// This function checks that the file is a valid WAV file, and that it is a 16-bit stereo file.
	// Returns 0 if file is valid, 1 if the file is not valid.
	
	// Check the RIFF chunk.
	uint32_t header[3] = {0};
	fread(header, 4, 3, fp);
	if (_OSSwapInt32(header[0]) != 'RIFF' || _OSSwapInt32(header[2]) != 'WAVE') {
		printf("Input is not a WAV file.");
		goto error_exit;
	}
	
	// Read the format chunk.
	FormatChunk formatChunk;
	fpos_t formatChunkOffset = offset_of_chunk('fmt ', 12, fp);
	if (formatChunkOffset == 0) {
		printf("File is missing a format chunk.");
		goto error_exit;
	}
	fseek(fp, formatChunkOffset, SEEK_SET);
	fread(&formatChunk, sizeof(formatChunk), 1, fp);
	if (formatChunk.formatTag != 1 || formatChunk.bitsPerSample != 16) {
		printf("Unsupported WAV format, which must be 16-bit PCM.");
		goto error_exit;
	}
	if (formatChunk.channels != 1 && formatChunk.channels != 2) {
		printf("Unsupported %d number of channels, which must be 1 or 2.", formatChunk.channels);
		goto error_exit;
	}
	*outChannels = formatChunk.channels;
	*outSampleRate = formatChunk.sampleRate;

	// Read the data chunk.
	uint32_t dataChunk[2] = {0};
	fpos_t dataChunkOffset = offset_of_chunk('data', 12, fp);
	if (dataChunkOffset == 0) {
		goto error_exit;
	}
	fseek(fp, dataChunkOffset, SEEK_SET);
	fread(dataChunk, 4, 2, fp);
	*outDataOffset = dataChunkOffset + 8;
	*outDataLength = dataChunk[1];

	return 0;
	
error_exit:
	//printf("Invalid file header.\n");
	return 1;
}

int main(int argc, const char * argv[]) {
	// This tool takes one argument, the name of the input file.
	if (argc != 2) {
		printf("Usage: lametest input_file.wav\n");
		return 1;
	}
	
	// Open the WAV file.
	FILE *inputFile = fopen(argv[1], "rb");
	if (inputFile == NULL) {
		printf("Error opening file.\n");
		return 1;
	}
	
	// Parse the WAV file header.
	fpos_t dataOffset = 0;
	uint32_t dataLength = 0;
	uint16_t channels = 0;
	uint32_t sampleRate = 0;
	if (read_wav_header(inputFile, &dataOffset, &dataLength, &channels, &sampleRate)) {
		printf("Invalid file header.\n");
		return 1;
	}
	
	// Create the output file name.
	char outputFilename[1024];
	strncpy(outputFilename, argv[1], 1019);
	strcat(outputFilename, ".mp3");
	
	// Create the output file.
	FILE *outputFile = fopen(outputFilename, "wb");
	
	// Set up LAME
	int err;
	lame_global_flags *flags = lame_init();
	if (flags == NULL) {
		printf("LAME init error.");
		goto lame_error;
	}
	
	err = lame_set_in_samplerate(flags, sampleRate);
	if (err) {
		printf("lame_set_in_samplerate() error %d", err);
		goto lame_error;
	}
	
	err = lame_set_num_channels(flags, channels);
	if (err) {
		printf("lame_set_num_channels() error %d", err);
		goto lame_error;
	}

	// We will write the Xing VBR/INFO tag
	err = lame_set_bWriteVbrTag (flags, 1);
	if (err) {
	   printf("lame_set_bWriteVbrTag() error %d", err);
	   goto lame_error;
	}
	
	// Encoder algorithm quality
	lame_set_quality(flags, 7);
	
	// Constant Bit Rate (VBR off)
	err = lame_set_VBR(flags, vbr_off);
	if (err) {
		printf("lame_set_VBR() error %d", err);
		goto lame_error;
	}
	
	// Set the bit rate in KBPS
	err = lame_set_brate (flags, 128 * channels); // kbps
	if (err) {
		printf("lame_set_brate() error %d", err);
		goto lame_error;
	}
	
	// Finish setting up the parameters
	err = lame_init_params (flags);
	if (err) {
		printf("lame_init_params() error %d", err);
		goto lame_error;
	}

	// Loop over the data
	fseek(inputFile, dataOffset, SEEK_SET);
	uint32_t remain = dataLength;
	uint32_t readCount;
	int32_t writeCount;
	const uint32_t readBufferSize = 4096;
	uint8_t readBuffer[readBufferSize];
	const uint32_t writeBufferSize = 10240;
	uint8_t writeBuffer[writeBufferSize];
	uint32_t numSamples;
	
	while (remain > 0) {
		readCount = (remain < readBufferSize) ? remain : readBufferSize;
		numSamples = readCount / (2 * channels);
		
		fread(readBuffer, readCount, 1, inputFile);
		
		if (channels == 1) {
			writeCount = lame_encode_buffer(flags, (int16_t *)readBuffer, (int16_t *)readBuffer, numSamples, writeBuffer, writeBufferSize);
		} else if (channels == 2) {
			writeCount = lame_encode_buffer_interleaved(flags, (int16_t *)readBuffer, numSamples, writeBuffer, writeBufferSize);
		} else {
			writeCount = -1;
		}
		
		if (writeCount < 0) {
			printf("lame_encode_buffer() error %d", writeCount);
			break;
		} else if (writeCount > 0) {
			fwrite(writeBuffer, writeCount, 1, outputFile);
		}
		
		remain -= readCount;
	}
	
	// Flush remaining bytes
	writeCount = lame_encode_flush(flags, writeBuffer, writeBufferSize);
	if (writeCount < 0) {
		printf("lame_encode_flush() error %d", writeCount);
	} else if (writeCount > 0) {
		fwrite(writeBuffer, writeCount, 1, outputFile);
	}

	// Write the Xing VBR/INFO tag at the beginning of the file.
	writeCount = (int32_t)lame_get_lametag_frame(flags, writeBuffer, writeBufferSize);
	if (writeCount < 0) {
		printf("lame_get_lametag_frame() error %d", writeCount);
	} else if (writeCount > 0) {
		fseek(outputFile, 0, SEEK_SET);
		fwrite(writeBuffer, writeCount, 1, outputFile);
	}
	
	// Close the encoder
	err = lame_close(flags);
	if (err) {
		printf("lame_close() error %d", err);
	}
	
	// Close files
	fclose(inputFile);
	fclose(outputFile);
	
	return 0;
	
lame_error:
	fclose(inputFile);
	fclose(outputFile);
	return 1;
}
