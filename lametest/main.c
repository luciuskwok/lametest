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
#include <CoreServices/CoreServices.h>
#include "LAME/lame.h"


uint32_t read_uint32(FILE *fp, uint8_t swapBytes) {
	uint32_t value;
	
	if (fread(&value, 4, 1, fp) == 0) {
		return 0; // reached end of file: return zero
	}
	if (swapBytes) {
		value = _OSSwapInt32(value);
	}
	return value;
}

uint16_t read_uint16(FILE *fp, uint8_t swapBytes) {
	uint16_t value;
	
	if (fread(&value, 2, 1, fp) == 0) {
		return 0; // reached end of file: return zero
	}
	if (swapBytes) {
		value = _OSSwapInt16(value);
	}
	return value;
}

fpos_t offset_of_chunk(uint32_t chunkToFind, fpos_t start, FILE *fp, uint8_t fileIsBigEndian) {
	uint32_t chunkIdentifier, chunkDataSize;
	fpos_t mark = start;

	fseek(fp, start, SEEK_SET);

	while (1) {
		chunkIdentifier = read_uint32(fp, 1);
		if (chunkIdentifier == chunkToFind) {
			return mark; // found the chunk
		}
		// Else:
		chunkDataSize = read_uint32(fp, fileIsBigEndian);
		if (chunkDataSize == 0) {
			return 0; // reached end of file: chunk not found
		}
		chunkDataSize += (chunkDataSize % 2); // skip over 1 pad byte if chunk data size is odd
		fseek(fp, chunkDataSize, SEEK_CUR);
		mark += chunkDataSize + 8;
	}
}

int read_wav_header(FILE *fp, fpos_t *outDataOffset, uint32_t *outDataLength, uint16_t *outChannels, uint32_t *outSampleRate) {
	// This function checks that the file is a valid WAV file, and that it is a 16-bit stereo file.
	// Returns 0 if file is valid, 1 if the file is not valid.
	
	// Check the RIFF chunk.
	uint32_t header[3] = {0};
	fseek(fp, 0, SEEK_SET);
	fread(header, 4, 3, fp);
	if (_OSSwapInt32(header[0]) != 'RIFF' || _OSSwapInt32(header[2]) != 'WAVE') {
		//printf("Input is not a WAV file.\n");
		goto error_exit;
	}
	
	/*  == WAV format chunk ==
		Offset	Length	Name
		0		4		ckID = 'fmt '
		4		4		ckSize // size doesn't include the 8 bytes which make up the header
		8		2		formatTag = 1 for PCM audio
		10		2		channels // number of channels
		12		4		sampleRate
		16		4		bytesPerSec = sampleRate * blockAlign
		20		2		blockAlign = channels * bitsPerSample / 8 // sample frame size
		22		2		bitsPerSample // sample size
		24				// end of chunk
	*/

	// Read the format chunk.
	fpos_t formatChunkOffset = offset_of_chunk('fmt ', 12, fp, 0);
	if (formatChunkOffset == 0) {
		printf("File is missing a format chunk.\n");
		goto error_exit;
	}
	
	uint16_t formatTag = 0, bitsPerSample = 0;
	
	// Seek to the formatTag and read several values
	fseek(fp, formatChunkOffset + 8, SEEK_SET);
	fread(&formatTag, 2, 1, fp);
	fread(outChannels, 2, 1, fp);
	fread(outSampleRate, 4, 1, fp);
	fseek(fp, 6, SEEK_CUR); // skip bytesPerSec(4) and blockAlign(2)
	fread(&bitsPerSample, 2, 1, fp);
	
	// Check that the audio format is what we expect.
	if (formatTag != 1 || bitsPerSample != 16) {
		printf("Unsupported WAV format, which must be 16-bit PCM.\n");
		goto error_exit;
	}
	if (*outChannels != 1 && *outChannels != 2) {
		printf("Unsupported %d number of channels, which must be 1 or 2.\n", *outChannels);
		goto error_exit;
	}

	// Read the data chunk offset and size.
	fpos_t dataChunkOffset = offset_of_chunk('data', 12, fp, 0);
	if (dataChunkOffset == 0) {
		goto error_exit;
	}
	*outDataOffset = dataChunkOffset + 8;
	fseek(fp, dataChunkOffset + 4, SEEK_SET);
	fread(outDataLength, 4, 1, fp);

	return 0;
	
error_exit:
	//printf("Invalid file header.\n");
	return 1;
}

int read_aiff_header(FILE *fp, fpos_t *outDataOffset, uint32_t *outDataLength, uint16_t *outChannels, uint32_t *outSampleRate, uint8_t *outDataIsBigEndian) {
	// This function checks that the file is a valid AIFF file, and that it is a 16-bit stereo file.
	// Returns 0 if file is valid, 1 if the file is not valid.
	
	// Check the FORM chunk.
	uint32_t header[3] = {0};
	fseek(fp, 0, SEEK_SET);
	fread(header, 4, 3, fp);
	if (_OSSwapInt32(header[0]) != 'FORM') {
		//printf("Input is not a AIFF file.\n");
		goto error_exit;
	}
	if (_OSSwapInt32(header[2]) != 'AIFF' && _OSSwapInt32(header[2]) != 'AIFC') {
		//printf("Input is not a AIFF file.\n");
		goto error_exit;
	}

	/*  == AIFF common chunk ==
		Offset	Length	Name
		0		4		ckID = 'COMM'
		4		4		ckSize // size doesn't include the 8 bytes which make up the header
		8		2		numChannels
		10		4		numSampleFrames
		14		2		sampleSize
		16		10		sampleRate // 80-bit floating point
		26		4		compressionType
	    xx		var		compressionName
		var				// end of chunk
	*/

	// Read the common chunk.
	fpos_t commonChunkOffset = offset_of_chunk('COMM', 12, fp, 1);
	if (commonChunkOffset == 0) {
		printf("File is missing a COMM chunk.\n");
		goto error_exit;
	}
	
	uint32_t numSampleFrames = 0;
	uint16_t sampleSize = 0, channels = 0;
	extended80 sampleRate; // 80-bit floating point
	
	fseek(fp, commonChunkOffset + 8, SEEK_SET);
	channels = read_uint16(fp, 1);
	numSampleFrames = read_uint32(fp, 1);
	sampleSize = read_uint16(fp, 1);
	fread(&sampleRate, sizeof(sampleRate), 1, fp);
	
	// Check that the audio format is what we expect.
	if (sampleSize != 16) {
		printf("Unsupported sample size, which must be 16-bit PCM.\n");
		goto error_exit;
	}
	if (channels != 1 && channels != 2) {
		printf("Unsupported %d number of channels, which must be 1 or 2.\n", channels);
		goto error_exit;
	}
	
	*outChannels = channels;
	
	// Calculate the number of bytes of data from the number of sample frames.
	*outDataLength = numSampleFrames * channels * 2;
	
	// Convert sample rate from extended 80-bit float to 32-bit unsigned int.
	*outSampleRate = x80tod(&sampleRate);

	// Read the sound data chunk offset.
	fpos_t dataChunkOffset = offset_of_chunk('SSND', 12, fp, 1);
	if (dataChunkOffset == 0) {
		printf("Sound data chunk not found.\n");
		goto error_exit;
	}
	fseek(fp, dataChunkOffset + 8, SEEK_SET);
	uint32_t addedOffset = read_uint32(fp, 1);
	*outDataOffset = dataChunkOffset + 8 + addedOffset;
	
	// TODO: handle little-endian AIFF format
	// For now, assume sample data is big endian
	*outDataIsBigEndian = 1;

	return 0;
	
error_exit:
	//printf("Invalid file header.\n");
	return 1;
}


void swap_buffer_16(uint8_t *buffer, uint32_t count) {
	uint32_t index;
	uint8_t temp;
	for (index=0; index<count; index+=2) {
		temp = buffer[index+1];
		buffer[index+1] = buffer[index];
		buffer[index] = temp;
	}
}


int main(int argc, const char * argv[]) {
	// This tool takes one argument, the name of the input file.
	if (argc != 2) {
		printf("Usage: lametest input_file\n");
		return 1;
	}
	
	// Open the file.
	FILE *inputFile = fopen(argv[1], "rb");
	if (inputFile == NULL) {
		printf("Could not open file.\n");
		return 1;
	}
	
	// Parse the audio file header.
	fpos_t dataOffset = 0;
	uint32_t dataLength = 0;
	uint16_t channels = 0;
	uint32_t sampleRate = 0;
	uint8_t dataIsBigEndian = 0;

	// Try opening as WAV first, then try as AIFF.
	int error;
	error = read_wav_header(inputFile, &dataOffset, &dataLength, &channels, &sampleRate);
	if (error) {
		error = read_aiff_header(inputFile, &dataOffset, &dataLength, &channels, &sampleRate, &dataIsBigEndian);
	}
	if (error) {
		printf("Unrecognized file format.\n");
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
		printf("lame_set_in_samplerate() error %d.\n", err);
		goto lame_error;
	}
	
	err = lame_set_num_channels(flags, channels);
	if (err) {
		printf("lame_set_num_channels() error %d.\n", err);
		goto lame_error;
	}

	// We will write the Xing VBR/INFO tag
	err = lame_set_bWriteVbrTag (flags, 1);
	if (err) {
	   printf("lame_set_bWriteVbrTag() error %d.\n", err);
	   goto lame_error;
	}
	
	// Encoder algorithm quality
	lame_set_quality(flags, 7);
	
	// Constant Bit Rate (VBR off)
	err = lame_set_VBR(flags, vbr_off);
	if (err) {
		printf("lame_set_VBR() error %d.\n", err);
		goto lame_error;
	}
	
	// Set the bit rate in KBPS
	err = lame_set_brate (flags, 128 * channels); // kbps
	if (err) {
		printf("lame_set_brate() error %d.\n", err);
		goto lame_error;
	}
	
	// Finish setting up the parameters
	err = lame_init_params (flags);
	if (err) {
		printf("lame_init_params() error %d.\n", err);
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
		
		if (dataIsBigEndian) {
			// Swap bytes to convert from big endian to little endian.
			swap_buffer_16(readBuffer, readCount);
		}
		
		if (channels == 1) {
			writeCount = lame_encode_buffer(flags, (int16_t *)readBuffer, (int16_t *)readBuffer, numSamples, writeBuffer, writeBufferSize);
		} else if (channels == 2) {
			writeCount = lame_encode_buffer_interleaved(flags, (int16_t *)readBuffer, numSamples, writeBuffer, writeBufferSize);
		} else {
			writeCount = -1;
		}
		
		if (writeCount < 0) {
			printf("lame_encode_buffer() error %d.\n", writeCount);
			break;
		} else if (writeCount > 0) {
			fwrite(writeBuffer, writeCount, 1, outputFile);
		}
		
		remain -= readCount;
	}
	
	// Flush remaining bytes
	writeCount = lame_encode_flush(flags, writeBuffer, writeBufferSize);
	if (writeCount < 0) {
		printf("lame_encode_flush() error %d.\n", writeCount);
	} else if (writeCount > 0) {
		fwrite(writeBuffer, writeCount, 1, outputFile);
	}

	// Write the Xing VBR/INFO tag at the beginning of the file.
	writeCount = (int32_t)lame_get_lametag_frame(flags, writeBuffer, writeBufferSize);
	if (writeCount < 0) {
		printf("lame_get_lametag_frame() error %d.\n", writeCount);
	} else if (writeCount > 0) {
		fseek(outputFile, 0, SEEK_SET);
		fwrite(writeBuffer, writeCount, 1, outputFile);
	}
	
	// Close the encoder
	err = lame_close(flags);
	if (err) {
		printf("lame_close() error %d.\n", err);
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
