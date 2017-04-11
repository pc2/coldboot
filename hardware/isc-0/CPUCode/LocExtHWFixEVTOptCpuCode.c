/*
 * Copyright (c) 2013-2017 Paderborn Center for Parallel Computing
 * Copyright (c) 2014-2015 Robert Mittendorf
 * Copyright (c) 2013      Heinrich Riebler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#include "Maxfiles.h"
#include "MaxSLiCInterface.h"

#define enableWS
#define err_p0 0.299
#define err_p1 0.001

#ifdef enableWS
# define TICKS_PER_MS 60000
#else
# define TICKS_PER_MS 80000
#endif

#define NUM_BYTES 16
#define NUM_ROUND_BYTES 16
#define ROUNDS_NUM 11

/**
 * Print the key schedule.
 * @param b the bytes of key schedule.
 */
void printKeySchedule(uint8_t *b) {
  for(int i = 0; i < ROUNDS_NUM; i ++) {
    for(int j = i*NUM_BYTES; j < i*NUM_BYTES + NUM_BYTES; j ++) {
      printf("%02x \t", b[j]);
    }

    printf("\n");
  }

  printf("\n");
}

int main(int argc, char *argv[]) {
  // Get the key schedule id to reconstruct.
  //   After, argv[1] is assumed to be the key schedule ID.
  if( argc != 2 ) {
    printf("Key schedule ID as argument expected.\n");
    return -1;
  }

  // Key schedule size in bytes.
  const int size = 176;
  char ks_path[100];
  unsigned long fileLen;
  uint8_t*  DFEinput;
  uint8_t* DFEoutput;

  // Define error rate.
  double p0 = err_p0;
  double p1 = err_p1;

  // Input file for better guess order.
  //   Instead of guessing bytes in order (0, 1, 2, ... 255),
  //   we use a better order, tailored to the key schedule instance.
  uint8_t betterGuessBytes[256 * NUM_ROUND_BYTES];
  uint64_t betterGuess[256 * NUM_ROUND_BYTES];
  char bg_path[100];

  // Compute expected number of bit errors
  //  in each direction.
  uint16_t expected_n0 = (int) (p0 * 176 * 8);
  uint16_t expected_n1 = (int) (p1 * 176 * 8);

  // Evaluation.
  unsigned long runtime;

  // Load MAX file
  printf("Loading MAX file...");
  max_file_t *myMaxFile = LocExtHWFixEVTOpt_init();
  max_engine_t *myDFE = max_load(myMaxFile, "local:*");
  printf("done.\n");

  // Actions
  LocExtHWFixEVTOpt_actions_t defaultStatic;
  LocExtHWFixEVTOpt_pollResult_actions_t pollResultStatic;
  max_actions_t* pollResultDynamic;

  // Read better guess order from file.
  FILE *fp_better_guess;
  sprintf(bg_path, "demo-input/better_guess/better_guess_isc_%d.conf", atoi(argv[1]));
  fp_better_guess = fopen(bg_path, "r");

  if (!fp_better_guess) {
    fprintf(stderr, "Unable to open file %s\n", bg_path);
    return 1;
  }

  fread(betterGuessBytes, 1, 256 * NUM_ROUND_BYTES, fp_better_guess);
  fclose(fp_better_guess);

  // Convert better guess data types for SLIC interface
  for (int m = 0; m < 256*NUM_ROUND_BYTES; m++) {
    betterGuess[m] = betterGuessBytes[m];
  }

  // Input file for key schedule.
  sprintf(ks_path, "demo-input/key_schedules_decayed/%d.keys_decayed", atoi(argv[1]));
  FILE *file;
  // Open file.
  file = fopen(ks_path, "rb");

  if (!file) {
    fprintf(stderr, "Unable to open file %s\n", ks_path);
    return 1;
  }

  // Get file length.
  fseek(file, 0, SEEK_END);
  fileLen = ftell(file);
  fseek(file, 0, SEEK_SET);
  // Allocate memory.
  DFEinput  = (uint8_t *) calloc(fileLen+1, 1);
  DFEoutput = (uint8_t *) calloc(192, 1);

  if (!DFEinput || !DFEoutput) {
    fprintf(stderr, "Memory allocation failed!");
    return 1;
  }

  //Read file contents into buffer
  fread(DFEinput, fileLen, 1, file);
  fclose(file);

  // Initial DFE call
  defaultStatic.param_N = size;
  defaultStatic.inscalar_AESFixEVTSM0_expected_n0 = expected_n0;
  defaultStatic.inscalar_AESFixEVTSM0_expected_n1 = expected_n1;
  defaultStatic.instream_input0 = DFEinput;
  defaultStatic.outstream_output0 = DFEoutput;
  defaultStatic.inmem_AESFixEVTSM0_betterGuess0 = betterGuess;
#ifdef enableWS
  defaultStatic.inmem_AESFixEVTSM0_betterGuess1 = betterGuess;
  defaultStatic.inmem_AESFixEVTSM0_betterGuess2 = betterGuess;
  defaultStatic.inmem_AESFixEVTSM0_betterGuess3 = betterGuess;
#endif

  printf("Starting DFE...");
  LocExtHWFixEVTOpt_run(myDFE, &defaultStatic);
  printf("done.\n");

  // Poll resultMem regularly
  pollResultStatic.outstream_output0 = DFEoutput;

  for (uint64_t pollID = 1; DFEoutput[184] == 0; pollID++) { // DFE not finished yet
    sleep(1);
    pollResultStatic.param_pollID = pollID;
    pollResultDynamic = LocExtHWFixEVTOpt_pollResult_convert(myMaxFile, &pollResultStatic);
    max_disable_reset(pollResultDynamic);
    printf(".");
    fflush(stdout);
    max_run(myDFE, pollResultDynamic);
    max_actions_free(pollResultDynamic);
  }

  printf("\n");
  printKeySchedule(DFEoutput);

  // elem 22 in 64bit world means elem 176-183 in byte world
  runtime = ((uint64_t*)DFEoutput)[22] / TICKS_PER_MS; 
  printf("%d - %lu ticks - %lu ms \n", atoi(argv[1]), ((uint64_t*)DFEoutput)[22], runtime);

  free(DFEoutput);
  free(DFEinput);
  max_unload(myDFE);
  
  printf("\n ----------- DONE ---------- \n");

  return 0;
}
