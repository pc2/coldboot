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

#include <stdio.h>
#include <stdlib.h>    /* atoi, atof */
#include <stdint.h>		 /* uint8_t type */
#include <string.h>		 /* strcopy, memset */
#include <fcntl.h>	   /* open [FILE] */
#include <stack>
#include <cstdlib>
#include <unistd.h>
#include <typeinfo> 
#include <sys/time.h>  /* get_timestamp helpers */
#include <ctime>
#include <string>
#include <cilk/cilk.h>
#include <algorithm>
#include <vector>

#include <sys/mman.h>
#include <pthread.h>

//Start of Configurations
//define the basedir for key schedules
#define LINUX_BASEDIR "./"

// Set known bit. used for path generation, needed for PAD
#define KNOWN_BIT 1
#define USE_STATIC_PATH false
#define TSOW_ACCTO_LOECKE true

//define the mode of tree traversal
#define MODE 5
//1- Single threaded implementation.
//5- Parallel Cilk work stealing implementation.

// Use standard or better guess
#define BETTERGUESS true

//define maximum fork level. maximum is NUM_ROUND_BYTES -2, as with NUM_ROUND_BYTES -1, the completion step would be spawned more than once.
#define DO_NOT_SPAWN_BELOW NUM_ROUND_BYTES -2 //

//If set to true, exhaustive search is used. If set to false, the program will stop after the first complete compatible key schedule
#define CONTINUE_AFTER_FIRST_RESULT false

//Type of AES
#define AES_TYPE 128

//Size of a working packet. This means, how many of the 256 values of a byte are spawned as a single task. Minimum is 1, maximum is 256
//Very small values cause a huge overhead for copy operations
#define WP_SIZE 256

//Error Modell
#define Error_Model 2 //1=PAD, 2=EVT
#define PAD_MODE 2
// 1 - using XOR and &  (and ~)
// 2 - using ~ and &
// 3 - using recursive XOR and & (and ~)
// 4 - using recursive ~ and &

//For EVT, different possibilities are implemented:
#define EVT_MODE 1
//Mode of computation for EVT
// 1- old
// 2- lookup table 1 Byte
// 3- lookup table 2 Byte
// 4- PAD & OneCount by special method while
// 5- PAD & OneCount by special method for
// 6- PAD & OneCount lookup
// 7- PAD & popcount ICC
// 8- PAD & popcount GCC

//enable debug-mode
#define DEBUG 0
//uses copy and guess-counters. slows down multi-threadded version dramatically!

//End of Configurations

//Stop Search after first result? (If multiple results are possible, this may depend on thread scheduling in the multi-threaded case!)
#if CONTINUE_AFTER_FIRST_RESULT != true
bool doNotContinue = false;
#endif

//TODO:
// - it is possible to derive the defined positions from CandidateMatrix?
//        Idea: since they are fixed by the path, the defined positions are all
//                the indexes from previous stages in the path.
//
// - is there a check, if computeFrom8 is compatible?
//        Idea: extend the path for stage 16 with all missing bytes.
//
// - is it possible to create a global path stage for 16?
//        Idea: stage 16 has 55 inferred bytes. Positions are derivable from
//                previous positions in path.
using namespace std;

// Length of word in bytes.
#define NUM_WORD 4

#if AES_TYPE == 128

// Length of data in bytes.
#define NUM_ROUND_BYTES 16 //==BYTE_NUM_ROUND == STAGE_COUNT
// Number of rounds.
#define NUM_ROUNDS 11
// Length of key schedule in bytes.
#define NUM_KS_BYTES 176
// Standard path, which is used, if is not used heuristic
uint8_t path_static[NUM_ROUND_BYTES][NUM_ROUNDS] = {
  {   0 },                                                               //  0
  {  16,  13 },                                                          //  1
  {  32,  29,  25 },                                                     //  2
  {  48,  45,  41,  37 },                                                //  3
  {  64,  61,  57,  53,  49 },                                           //  4
  {  80,  77,  73,  69,  65,  62 },                                      //  5
  {  96,  93,  89,  85,  81,  78,  74 },                                 //  6
  { 112, 109, 105, 101,  97,  94,  90,  86 },                            //  7
  { 128, 125, 121, 117, 113, 110, 106, 102,  98 },                       //  8
  { 144, 141, 137, 133, 129, 126, 122, 118, 114, 111 },                  //  9
  { 160, 157, 153, 149, 145, 142, 138, 134, 130, 127, 123 },             // 10
  { 173, 169, 165, 161, 158, 154, 150, 146, 143, 139, 135 },             // 11
  { 131, 119, 107,  95,  82,  70,  58,  46,  33,  21,   9 },             // 12
  { 124, 115, 103,  91,  79,  66,  54,  42,  30,  17,   5 },             // 13
  { 120, 108,  99,  87,  75,  63,  50,  38,  26,  14,   1 },             // 14
  { 174, 170, 166, 162, 159, 155, 151, 147, 140, 136, 132 }              // 15
};

// Define length of each path.
//  This is the number of inferred bytes per round +1 
//  (the first is the guessed byte)
uint8_t path_len[NUM_ROUND_BYTES] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 11, 11, 11, 11
};

#endif

#if AES_TYPE == 256

// Length of data in bytes.
#define NUM_ROUND_BYTES 32 //==BYTE_NUM_ROUND == STAGE_COUNT
// Number of rounds.
#define NUM_ROUNDS 15
// Length of key schedule in bytes.
#define NUM_KS_BYTES 240

uint8_t path_static[NUM_ROUND_BYTES][NUM_ROUNDS] = {
  {0},
  {32,  29},
  {64,  61,  57},
  {96,  93,  89,  85},
  {128, 125, 121, 117, 113},
  {160, 157, 153, 149, 145, 141},
  {192, 189, 185, 181, 177, 173, 169},
  {224, 221, 217, 213, 209, 205, 201, 197 },
  {193, 165, 137, 109, 81,  53,  25 },
  {190, 161, 133, 105, 77,  49,  21 },
  {186, 158, 129, 101, 73,  45,  17 },
  {237, 233, 229, 225, 222, 218, 214 },
  {210, 182, 154, 126, 97,  69,  41, 13 },
  {206, 178, 150, 122, 94,  65,  37, 9},
  {202, 174, 146, 118, 90,  62,  33, 5},
  {198, 170, 142, 114, 86,  58,  30, 1},
  {194, 166, 138, 110, 82,  54,  26 },
  {191, 162, 134, 106, 78,  50,  22 },
  {187, 159, 130, 102, 74,  46,  18 },
  {238, 234, 230, 226, 223, 219, 215 },
  {211, 183, 155, 127, 98,  70,  42, 14 },
  {207, 179, 151, 123, 95,  66,  38, 10 },
  {203, 175, 147, 119, 91,  63,  34, 6 },
  {199, 171, 143, 115, 87,  59,  31, 2 },
  {195, 167, 139, 111, 83,  55,  27 },
  {188, 163, 135, 107, 79,  51,  23 },
  {184, 156, 131, 103, 75,  47,  19 },
  {239, 235, 231, 227, 220, 216, 212 },
  {208, 180, 152, 124, 99,  71,  43, 15},
  {204, 176, 148, 120, 92,  67,  39, 11},
  {200, 172, 144, 116, 88,  60,  35, 7},
  {196, 168, 140, 112, 84,  56,  28, 3}
};

// Define length of each path.
//  This is the number of inferred bytes per round +1 
//  (the first is the guessed byte)
uint8_t path_len[NUM_ROUND_BYTES] = {	
  1, 2, 3, 4, 5, 6, 7, 8, 7, 7, 7, 7, 8, 8, 8, 8, 7, 7, 7, 7, 8, 8, 8, 8, 7, 7, 7, 7, 8, 8, 8, 8
};
#endif

uint8_t guessOrder[NUM_ROUND_BYTES][256];
uint8_t guessOrderRev[NUM_ROUND_BYTES][256];

#if Error_Model == 2
#  if EVT_MODE == 1 // Rieblers implementation. Isolate every single bit, shift & compare
void evt(uint8_t c, uint8_t d, uint16_t *tmp_N0, uint16_t *tmp_N1) {
  uint8_t j;

  for (j = 0; j < 8; j++) {
    uint8_t sdm = (d >> j) & 1;
    uint8_t scm = (c >> j) & 1;

    if ((sdm ^ scm) == 1) {
      if (scm == 1) {
        (*tmp_N0)++;

      } else {
        (*tmp_N1)++;
      }
    }
  }
}
#   endif
#   if EVT_MODE == 2 
//Lookup table implementation in compressed format (1byte per input value pair)
uint8_t values[65536];

void oldevt(uint8_t c, uint8_t d, uint8_t *tmp_N0, uint8_t *tmp_N1) {
  uint8_t j;

  for (j = 0; j < 8; j++) {
    uint8_t sdm = (d >> j) & 1;
    uint8_t scm = (c >> j) & 1;

    if ((sdm ^ scm) == 1) {
      if (scm == 1) {
        (*tmp_N0)++;

      } else {
        (*tmp_N1)++;
      }
    }
  }
}

void evt_init() {
  uint8_t tmp_N0, tmp_N1;

  for (int i; i <= 65536; i++) {
    tmp_N0 = 0;
    tmp_N1 = 0;
    oldevt(i / 256, i & 0x00FF, &tmp_N0, &tmp_N1);
    uint8_t store = tmp_N0 * 16 + tmp_N1;
    values[i] = store;
  }
}

void evt(uint8_t c, uint8_t d, uint16_t *tmp_N0, uint16_t *tmp_N1) {
  uint8_t val = values[c * 256 + d];
  *tmp_N0 += val / 16;
  *tmp_N1 += val & 0x0F;
}

#   endif
#   if EVT_MODE == 3 
// Lookup table implementation in standard format (2byte, 1 per error direction, per input value pair)
uint16_t values16[2 * 65536];

void oldevt(uint8_t c, uint8_t d, uint8_t *tmp_N0, uint8_t *tmp_N1) {
  uint8_t j;

  for (j = 0; j < 8; j++) {
    uint8_t sdm = (d >> j) & 1;
    uint8_t scm = (c >> j) & 1;

    if ((sdm ^ scm) == 1) {
      if (scm == 1) {
        (*tmp_N0)++;

      } else {
        (*tmp_N1)++;
      }
    }
  }
}

void evt_init() {
  uint8_t tmp_N0, tmp_N1;

  for (int i; i <= 65536; i++) {
    tmp_N0 = 0;
    tmp_N1 = 0;
    oldevt(i / 256, i & 0x00FF, &tmp_N0, &tmp_N1);
    values16[2 * i] = tmp_N0;
    values16[2 * i + 1] = tmp_N1;
  }
}

void evt(uint8_t c, uint8_t d, uint16_t *tmp_N0, uint16_t *tmp_N1) {
  *tmp_N0 += values16[2 * (c * 256 + d)];
  *tmp_N1 += values16[2 * (c * 256 + d) + 1];
}

#   endif
#   if EVT_MODE == 4 
// Do PAD in both directions, then compute number of errors = number of ones with special method while
void evt(uint8_t c, uint8_t d, uint16_t *tmp_N0, uint16_t *tmp_N1) {
  uint8_t N0 = (~c & d);
  uint8_t N1 = (c & ~d);

  while (N0) {
    N0 &= (N0 - 1);
    (*tmp_N0)++;
  }

  while (N1) {
    N1 &= (N1 - 1);
    (*tmp_N1)++;
  }
}

#   endif
#   if EVT_MODE == 5 
// Do PAD in both directions, then compute number of errors = number of ones with special method for
void evt(uint8_t c, uint8_t d, uint16_t *tmp_N0, uint16_t *tmp_N1) {
  uint8_t N0 = (~c & d);
  uint8_t N1 = (c & ~d);

  for (tmp_N0 = 0; N0; (*tmp_N0)++) {
    N0 &= N0 - 1;
  }

  for (*tmp_N1 = 0; N1; (*tmp_N1)++) {
    N1 &= N1 - 1;
  }
}

#   endif
#   if EVT_MODE == 6 
// Do PAD for both directions and use a Lookup table for the number of errors = number of ones (OneCount/PopCount)
uint8_t oneCountLookup[256];

uint8_t oneCount(uint8_t n) {
  uint8_t tmp = 0;

  while (n) {
    n &= (n - 1);
    tmp++;
  }

  return tmp;
}

void evt_init() {
  for (int i; i <= 256; i++) {
    oneCountLookup[i] = oneCount(i);
  }
}
void evt(uint8_t c, uint8_t d, uint16_t *tmp_N0, uint16_t *tmp_N1) {
  uint8_t N0 = (~c & d);
  uint8_t N1 = (c & ~d);
  *tmp_N0 += oneCountLookup[N0];
  *tmp_N1 += oneCountLookup[N1];
}

#   endif
#   if EVT_MODE == 7 
// Do PAD for both directions and use popcount instruction for the number of errors =  number of ones (OneCount/PopCount) ICC syntax

void evt(uint8_t c, uint8_t d, uint16_t *tmp_N0, uint16_t *tmp_N1) {
  uint8_t N0 = (~c&d);
  uint8_t N1 = (c&~d);
  *tmp_N0 +=_popcnt32(N0);
  *tmp_N1 +=_popcnt32(N1);
}

#   endif
#   if EVT_MODE == 8 
// Do PAD for both directions and use popcount instruction for the number of errors =  number of ones (OneCount/PopCount) GCC syntax
void evt(uint8_t c, uint8_t d, uint16_t *tmp_N0, uint16_t *tmp_N1) {
  uint8_t N0 = (~c&d);
  uint8_t N1 = (c&~d);
  *tmp_N0 += __builtin_popcount(N0);
  *tmp_N1 += __builtin_popcount(N1);
}
#   endif

/**
* Model EVT.
*
* n0: 1 -> 0
* n1: 0 -> 1
*/
// Expected number of errors in each direction.
uint16_t expected_N0= 0;
uint16_t expected_N1= 0;

// Recovery array for each stage.
uint16_t recN0[NUM_ROUND_BYTES+2] = { 0 };
uint16_t recN1[NUM_ROUND_BYTES+2] = { 0 };

// Number of currently consumed errors in each direction.
uint16_t currentN0 = 0;
uint16_t currentN1 = 0;
#endif

// needed for Mutex variant only:
class ListOfResults;
ListOfResults* allResults=NULL;

int isCompatibleCalls;
int guessCalls;
int copiesCount;
int guessRollbackCalls;
bool outputEnabled;

// Define static path.
uint8_t (*path)[NUM_ROUND_BYTES][NUM_ROUNDS];

// AES Substitution box. 
uint8_t sbox[256] = {
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe,
  0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4,
  0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7,
  0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3,
  0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09,
  0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3,
  0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe,
  0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
  0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92,
  0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c,
  0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19,
  0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
  0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2,
  0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5,
  0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25,
  0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86,
  0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e,
  0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42,
  0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

// AES Substitution box, inverse. 
uint8_t unsbox[256] = {
  0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38, 0xBF, 0x40, 0xA3, 0x9E, 0x81,
  0xF3, 0xD7, 0xFB, 0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87, 0x34, 0x8E,
  0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB, 0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2, 0x23,
  0x3D, 0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E, 0x08, 0x2E, 0xA1, 0x66,
  0x28, 0xD9, 0x24, 0xB2, 0x76, 0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25, 0x72,
  0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65,
  0xB6, 0x92, 0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA, 0x5E, 0x15, 0x46,
  0x57, 0xA7, 0x8D, 0x9D, 0x84, 0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A,
  0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06, 0xD0, 0x2C, 0x1E, 0x8F, 0xCA,
  0x3F, 0x0F, 0x02, 0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B, 0x3A, 0x91,
  0x11, 0x41, 0x4F, 0x67, 0xDC, 0xEA, 0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6,
  0x73, 0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85, 0xE2, 0xF9, 0x37, 0xE8,
  0x1C, 0x75, 0xDF, 0x6E, 0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89, 0x6F,
  0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B, 0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2,
  0x79, 0x20, 0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4, 0x1F, 0xDD, 0xA8,
  0x33, 0x88, 0x07, 0xC7, 0x31, 0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F,
  0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D, 0x2D, 0xE5, 0x7A, 0x9F, 0x93,
  0xC9, 0x9C, 0xEF, 0xA0, 0xE0, 0x3B, 0x4D, 0xAE, 0x2A, 0xF5, 0xB0, 0xC8, 0xEB,
  0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61, 0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6,
  0x26, 0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D
};

// AES Round constants. 
uint8_t rcon[256] = {
  0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8,
  0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3,
  0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f,
  0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d,
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab,
  0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d,
  0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25,
  0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01,
  0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d,
  0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa,
  0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a,
  0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02,
  0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a,
  0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef,
  0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94,
  0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04,
  0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f,
  0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5,
  0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33,
  0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d
};

pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;

void lockMutex() {
  pthread_mutex_lock(&mymutex);
}
void unlockMutex() {
  pthread_mutex_unlock(&mymutex);
}

/**
* Print the key schedule.
*
* TODO: could have memory leak.
*
* @param b the uint8_ts of key schedule.
*/
template <class T>
void printKeySchedule(T b) {
  //return;
  int i;
  int j;
  printf("     ");

  for(i = 0; i < NUM_WORD; i++) {
    for(j = 0; j < NUM_WORD; j++) {
      printf("%3d ", j);
    }
  }

  printf("\n");
  printf("   -------------------------------------------------------------\n");

  for(i = 0; i < NUM_ROUNDS; i ++) {
    printf("%2d | ", i);

    for(j = i*16; j < (i+1)*16; j ++) {
      printf("%3d ", b[j]);
    }

    printf("\n");
  }

  printf("\n");
}

/**
* Print the key schedule.
*
* TODO: could have memory leak
*
* @param b the uint8_ts of key schedule.
*/
void printPath(uint8_t (*b)[11]) {
  int i;
  int j;

  for(i = 0; i < NUM_ROUND_BYTES; i++) {
    for(j = 0; j < path_len[i]; j++) {
      printf("%3d ", b[i][j]);
    }

    printf("\n");
  }
}

/**
* Class ComparatorDecayProbability
*/
class ComparatorDecayProbability {
private:
  double n0ton1;
  double n1ton0;
  uint8_t decayedByte;
  bool oneAtPos(uint8_t b, short i);
public:
  void setn0ton1(double p);
  void setn1ton0(double p);
  void setDecayedByte(uint8_t p_decayedByte);
  bool operator()(uint8_t o1, uint8_t o2);
};
bool ComparatorDecayProbability::oneAtPos(uint8_t b, short i) {
  return ((b & (1 << i)) != 0);
}
void ComparatorDecayProbability::setn0ton1(double p) {
  n0ton1 = p;
}
void ComparatorDecayProbability::setn1ton0(double p) {
  n1ton0 = p;
}
void ComparatorDecayProbability::setDecayedByte(uint8_t p_decayedByte) {
  decayedByte = p_decayedByte;
}
bool ComparatorDecayProbability::operator()(uint8_t o1, uint8_t o2) {
  double o1prob = 1;
  double o2prob = 1;

  for (int i = 0; i < 8; i++) {
    if (oneAtPos(o1, i) && oneAtPos(decayedByte, i))
      o1prob *= (1.0 - n1ton0);

    else if (oneAtPos(o1, i) && !oneAtPos(decayedByte, i))
      o1prob *= n1ton0;

    else if (!oneAtPos(o1, i) && oneAtPos(decayedByte, i))
      o1prob *= n0ton1;

    else if (!oneAtPos(o1, i) && !oneAtPos(decayedByte, i))
      o1prob *= (1.0 - n0ton1);

    if (oneAtPos(o2, i) && oneAtPos(decayedByte, i))
      o2prob *= (1.0 - n1ton0);

    else if (oneAtPos(o2, i) && !oneAtPos(decayedByte, i))
      o2prob *= n1ton0;

    else if (!oneAtPos(o2, i) && oneAtPos(decayedByte, i))
      o2prob *= n0ton1;

    else if (!oneAtPos(o2, i) && !oneAtPos(decayedByte, i))
      o2prob *= (1.0 - n0ton1);
  }

  return ((o1prob > o2prob) || ((o1prob == o2prob) && (o1 < o2)));
}

/**
* Class Candidate Matrix.
*
* Set guessed byte at correct position, depending on path and count.
*/
class CandidateMatrix {
private:
  bool isFirstWordDefined(uint8_t r);
  void complete();
public:
  // Number of current guesses.
  int8_t count;
  // Key Schedule to be extended.
  uint8_t m[NUM_KS_BYTES];
  // Defined/Guessed positions in the Key Schedule.
  bool d[NUM_KS_BYTES];

  // Default constructor.
  // First call with empty CandidateMatrix.
  CandidateMatrix() {
    // Init count.
    count = -1;
    // Init key schedule with zeros.
    memset(m, 0, NUM_KS_BYTES);
    // Init key schedule positions as undefined.
    memset(d, false, NUM_KS_BYTES);
  }
  //Copy-Constructor
  CandidateMatrix(int8_t scount, uint8_t *sm, bool *sd) {
    // Set count and key schedule
    count = scount;
#if DEBUG==1
    copiesCount++;
#endif
    memcpy(m, sm, NUM_KS_BYTES);
    memcpy(d, sd, NUM_KS_BYTES);
  }

  void guess(uint8_t b);
  void resetCM();
  void guessRollback();
};

/**
* CandidateMatrix::PRIVATE
*/

/**
* Checks if the all bytes in the first word of subkey
* for the rth round are already known.
*/
bool CandidateMatrix::isFirstWordDefined(uint8_t r) {
  uint8_t i;

  for(i = r * NUM_ROUND_BYTES; i < r * NUM_ROUND_BYTES + 4; i++) {
    if(d[i] == false) {
      return false;
    }
  }

  return true;
}

/**
* Recover last valid stage (backtracking), if current stage search
* is exhaused.
*/
void CandidateMatrix::guessRollback() {
  uint8_t i;
  uint8_t j;
#if DEBUG == 1
  guessRollbackCalls++;
#endif

  //TODO: check whether this works for AES 256,
  if(count >= NUM_ROUND_BYTES -1) {
    // Init key schedule positions as undefined.
    memset(d, false, NUM_KS_BYTES);

    // Set all path positions as defined.
    // TODO: Why not m[(*path)[i][j]]=0 ?
    for(i = 0; i < NUM_ROUND_BYTES; i++) {
      for(j = 0; j < path_len[i]; j++) {
        d[(*path)[i][j]] = true;
      }
    }
  } else {
    // Reset bytes for this stage (count).
    for(i = 0; i < path_len[count+1]; i++) {
      j = (*path)[count+1][i];
      // Reset: defined --> false.
      d[j] = false;
      m[j] = 0; 
    }
  }
}

/**
* CandidateMatrix::PUBLIC
*/

/**
* Update CandidateMatrix, set guessed byte and
* compute implications depending on count.
*
* TODO:
* - getRound: n / NUM_ROUND_BYTES; should be optimizable.
*/
#if AES_TYPE == 128
//Guess for AES 128
void CandidateMatrix::guess(uint8_t b) {
  // Increase number of guesses.
  count++;// = count + 1;
#if DEBUG == 1
  guessCalls++;
#endif
  // Done, if its the first guessed byte.
  if(count == 0) {
    // Set guessed byte at first position.
#if BETTERGUESS == true
    m[(*path)[count][0]] = guessOrder[count][b];
#else
    m[(*path)[count][0]] = b;
#endif
    d[(*path)[count][0]] = true;

  } else if(count == 16) {
    // Round 8 is complete. Compute missing positions.
    complete();//From8();

  } else {
    // Set guessed byte at first position.
#if BETTERGUESS == true
    m[(*path)[count][0]] = guessOrder[count][b];
#else
    m[(*path)[count][0]] = b;
#endif
    d[(*path)[count][0]] = true;
    // Compute and set implications according to path.
    uint8_t i;

    for(i = 1; i < path_len[count]; i++) {
      // Position vars. Two are always known, solve equation.
      uint8_t b0, b3, b4;
      // Tmp var. Intermediate result.
      uint8_t t;
      int16_t n = (*path)[count][i - 1] - NUM_ROUND_BYTES;

      if(n >= 0 && d[n]) {
        b4 = (*path)[count][i - 1];
        b3 = (*path)[count][i];
        b0 = b4 - NUM_ROUND_BYTES;
        t = m[b4] ^ m[b0];

        // Is first word of round.
        if(b4 % NUM_ROUND_BYTES < NUM_WORD) {
          // Is first byte of first word (needs rcon).
          if(b4 % NUM_ROUND_BYTES == 0) {
            m[b3] = unsbox[t ^ rcon[b4 / NUM_ROUND_BYTES]];

          } else {
            m[b3] = unsbox[t];
          }

        } else {
          m[b3] = t;
        }

        d[b3] = true;

      } else {
        if(count > 10) {
          b0 = (*path)[count][i];
          b3 = (*path)[count][i - 1];
          b4 = b0 + NUM_ROUND_BYTES;
          t = sbox[m[b3]] ^ m[b4];

          // Is first word of round.
          if(b4 % NUM_ROUND_BYTES < 4) {
            // Is first byte of first word (needs rcon).
            if(b4 % NUM_ROUND_BYTES == 0) {
              m[b0] = t ^ rcon[b4 / NUM_ROUND_BYTES];

            } else {
              m[b0] = t;
            }

          } else {
            m[b0] = m[b3] ^ m[b4];
          }

          d[b0] = true;

        } else {
          b0 = (*path)[count][i - 1] + NUM_ROUND_BYTES;
          b3 = (*path)[count][i - 1];
          b4 = (*path)[count][i];
          t = m[b0] ^ m[b3];

          // Is first word of round.
          if(b3 % NUM_ROUND_BYTES < 4) {
            // Is first byte of first word (needs rcon).
            if(b3 % NUM_ROUND_BYTES == 0) {
              m[b4] = unsbox[t ^ rcon[b0 / NUM_ROUND_BYTES]];

            } else {
              m[b4] = unsbox[t];
            }

          } else {
            m[b4] = t;
          }

          d[b4] = true;
        }
      }
    }
  }
}

/**
* Derives the full key schedule from completed subkey of round 8.
*
* TODO: optimization possible?
*/
void CandidateMatrix::complete() {
  // Loop runner.
  int i, j;
  // Tmp var.
  uint8_t t;

  // Derive 7, 6, 5, 4, 3, 2, 1, 0
  for (i = 7; i >= 0; i--) {
    // Easy computation for word 1, 2, 3
    for (j = i * 16 + 4; j < i * 16 + 16; j++) {
      if (d[j] == false) {
        // Compute element over XOR.
        m[j] = m[j+12] ^ m[j+16];
        // Set as defined.
        d[j] = true;
      }
    }

    // Harder computation for word 0
    if (!isFirstWordDefined(i)) {
      for (j = i * 16; j < i * 16 + 4; j++) {
        if (d[j] == false) {
          // Compute element with computation rule.
          t = sbox[m[(i * 16 + 12) + ((j % 16) + 1) % 4]] ^ m[j + 16];

          if (j % NUM_ROUND_BYTES == 0) {
            m[j] = t ^ rcon[i + 1];

          } else {
            m[j] = t;
          }

          // Set as defined.
          d[j] = true;
        }
      }
    }
  }

  // Round 8 is complete.
  // Derive 9, 10
  for (i = 9; i < 11; i++) {
    for (j = i * 16; j < i * 16 + 16; j++) {
      if (d[j] == false) {
        // Is first word of round.
        if (j % NUM_ROUND_BYTES < NUM_WORD) {
          // Harder for first round.
          t = sbox[m[(i * 16 - 4) + ((j % 16) + 1) % 4]] ^ m[j - 16];

          if (j % NUM_ROUND_BYTES == 0) {
            m[j] = t ^ rcon[i];

          } else {
            m[j] = t;
          }

          d[j] = true;
          // Easy computation for word 1, 2, 3

        } else {
          // Easy XOR.
          m[j] = m[j - 4] ^ m[j - 16];
          d[j] = true;
        }
      }
    }
  }
}
#endif

#if AES_TYPE == 256
//Guess for AES 256
void CandidateMatrix::guess(uint8_t b) {
  //CandidateMatrix256 c = copySelf(this);
  count++;

  if(count < 32) {
    m[path_static[count][0]] = b;
    d[path_static[count][0]] = true;
  }

  if(count == 0) return;

  if(count < 32) {
    for (int i = 1; i < path_len[count]; i++) {
      uint8_t t;

      if(count < 8) {
        int b0, b3, b4;
        int n = path_static[count][i-1] - 32;

        if (n >= 0 && d[n]) {
          b4 = path_static[count][i-1];
          b3 = path_static[count][i];
          b0 = b4 - 32;
          t = m[b4] ^ m[b0];

          //inFirstWordOfRoundKey(b4)
          if (b4 % NUM_ROUND_BYTES < 4 ) {
            // firstByteOfRoundKey(b4) 
            if(b4 % NUM_ROUND_BYTES == 0 )
              m[b3] = unsbox[t ^ rcon[b4 / NUM_ROUND_BYTES]];

            else
              m[b3] = unsbox[t];

          } else  if(b4%16 < 4)
            m[b3] = unsbox[t];

          else
            m[b3] = t;

          d[b3] = true;

        } else {
          b0 = path_static[count][i-1] + 32;
          b3 = path_static[count][i-1];
          b4 = path_static[count][i];
          t = m[b0] ^ m[b3];

          //inFirstWordOfRoundKey(b3)
          if(b3 % NUM_ROUND_BYTES < 4 ) { 
            // firstByteOfRoundKey(b3)
            if(b3 % NUM_ROUND_BYTES == 0 )
              m[b4] = unsbox[t ^ rcon[b0 / NUM_ROUND_BYTES]];

            else
              m[b4] =  unsbox[t];

          } else if(b3%16 < 4)
            m[b4] = unsbox[t];

          else
            m[b4] = t;

          d[b4] = true;
        }

      } else if(count == 11 || count == 19 || count == 27) {
        int b0, b3, b4;
        b4 = path_static[count][i-1];
        b3 = path_static[count][i];
        b0 = b4 - 32;
        t = m[b4] ^ m[b0];

        //inFirstWordOfRoundKey(b4)
        if ((b4 % NUM_ROUND_BYTES < 4 )) { 
          // firstByteOfRoundKey(b4)
          if(b4 % NUM_ROUND_BYTES == 0 )
            m[b3] = unsbox[t ^ rcon[b4 / NUM_ROUND_BYTES]];

          else
            m[b3] = unsbox[t];

        } else if (b4%16 < 4)
          m[b3] = unsbox[t];

        else
          m[b3] = t;

        d[b3] = true;

      } else {
        int b0, b1, b3, b4;
        b4 = path_static[count][i-1];
        b3 = path_static[count][i];
        b0 = b3 + 32;
        b1 = b4 + 4;

        //inLastWordOfRoundKey(b4)
        if((b4 % NUM_ROUND_BYTES >= 28 )) { 
          t = m[b4] ^ m[b0];

          if(b4%32 == 29)
            m[b3] = unsbox[t ^ rcon[b0 / NUM_ROUND_BYTES]];

          else
            m[b3] = unsbox[t];

        } else if(b3%16 < 4)
          m[b3] = unsbox[ m[b4] ^ m[b1] ];

        else
          m[b3] = m[b1] ^ m[b4];

        d[b3] = true;
      }
    }
  }

  if(count == 32)
    complete();//From6();
}
/**
* Derives the full key schedule from the subkey of round 6.
*/
void CandidateMatrix::complete() {
  uint8_t t;

  for(int i = 6; i >= 0; i --) {
    for(int j = i * 32 + 4 ; j < i * 32 + 32; j++)
      if(d[j] == false) {
        if(j%16 < 4)
          //m[j] = (byte) (util.subByte(m[j + 28]) ^ m[j + 32]);
          m[j] = sbox[m[j + 28]]^m[j + 32];

        else
          m[j] = m[j + 28] ^ m[j + 32];

        d[j] = true;
      }

    if(!isFirstWordDefined(i)) { 
      for(int j =  i * 32; j <  i * 32 + 4; j ++)
        if(d[j] == false) {
          t= sbox[m[(i*32+28) + ((j%28)+1)%4]] ^ m[j + 32];

          // firstByteOfRoundKey(j)
          if(j % NUM_ROUND_BYTES == 0 )
            m[j] = t  ^ rcon[i+1];

          else
            m[j] = t;

          d[j] = true;
        }
    }
  }

  for(int j = 7 * 32 ; j < 7 * 32 + 16; j++)
    if(d[j] == false) {
      //inFirstWordOfRoundKey(j)
      if ((j % NUM_ROUND_BYTES < 4 )) { 
        t = sbox[m[(7*32 - 4) + ((j%32)+1)%4]]  ^ m[j - 32];

        // firstByteOfRoundKey(j)
        if(j % NUM_ROUND_BYTES == 0 )
          m[j] = t ^ rcon[7];

        else
          m[j] = t;

      } else if(j%16 < 4)
        m[j] = sbox[(m[j-4])] ^ m[j - 32];

      else
        m[j] = m[j-4] ^ m[j - 32];

      d[j] = true;
    }
}
#endif

/**
* Key Schedule is solved. Reset CandidateMatrix for next computation.
*/
void CandidateMatrix::resetCM() {
  // Init count.
  count = -1;
  // Init key schedule with zeros.
  memset(m, 0, NUM_KS_BYTES); 
  // Init key schedule positions as undefined.
  memset(d, false, NUM_KS_BYTES);
}

// List for result(s). Possible optimization: 
//   Store uint_t[NUM_KS_BYTES] only instead of complete CandidateMatrix
class ListOfResultsEntry;
class ListOfResults;

class ListOfResultsEntry {
public:
  CandidateMatrix* cm;
  ListOfResultsEntry* next;

  ListOfResultsEntry(CandidateMatrix* newcm) {
    cm=newcm;
    next=NULL;
  }

};
class ListOfResults {
public:
  void append(ListOfResults* rearList);
  void add(CandidateMatrix* newcm);
  void print();
  bool isInList(uint8_t* reference);

  ListOfResults(CandidateMatrix* newcm) {
    head=new ListOfResultsEntry(newcm);
    tail=head;
    size=1;
  }

private:
  ListOfResultsEntry* head;
  ListOfResultsEntry* tail;
  uint16_t size;
};
bool ListOfResults::isInList(uint8_t* reference) {
  ListOfResultsEntry* current=head;

  while (current != NULL) {
    for (int i=0; i< NUM_KS_BYTES; i++) {
      if (current->cm->m[i]!=reference[i]) {
        i=NUM_KS_BYTES;

      } else {
        if (i==NUM_KS_BYTES-1)
          return true;
      }
    }

    current=current->next;
  }

  return false;
}

void ListOfResults::append(ListOfResults* rearList) {
  tail = rearList->tail;
  size += rearList->size;
}
void ListOfResults::add(CandidateMatrix* newcm) {
  tail->next = new ListOfResultsEntry(newcm);
  size ++;
  tail = tail->next;
}
void ListOfResults::print() {
  ListOfResultsEntry* current=head;
  uint8_t i =0;

  while (current != NULL) {
    printf("Result No %d:\n", i);
    printKeySchedule(current->cm->m);
    current=current->next;
    i++;
  }
}

#if Error_Model == 1
// PAD: if [(c XOR d) AND (d XOR GROUND_STATE)] == 0 then compatible. =>
// 		if [(c XOR d) AND (d XOR KNOWN_BIT)] != 0 then incompatible

#  if KNOWN_BIT == 0
#    if PAD_MODE == 4
bool padIsIncompatible(uint8_t c, uint8_t d) {
  return (c & ~d) != 0;
}
#    endif
#    if PAD_MODE == 3
bool padIsIncompatible(uint8_t c, uint8_t d) {
  return ((c ^ d) & (~d)) != 0;
}
#    endif
#  endif
#  if KNOWN_BIT == 1
#    if PAD_MODE == 4
bool padIsIncompatible(uint8_t c, uint8_t d) {
  return (~c & d) != 0;	
}
#    endif
#    if PAD_MODE == 3
bool padIsIncompatible(uint8_t c, uint8_t d) {
  return ((c ^ d) & d) != 0;
}
#    endif
#  endif

/**
* Compatibility Check between two Key Schedules.
*
* Model: Perfect Asymmetric Decay.
*
*  No longer true   with assumption ground state zero.
*
* note: c->count is equals to stage (stack.size()).
*
* @return  1/TRUE     => it is compatible
*          0/FALSE => is is not compatible
*/
bool isCompatiblePAD(CandidateMatrix* c, uint8_t *dm) {
  uint8_t i;

  if(c->count < NUM_ROUND_BYTES) {
    // Check derived bytes for this stage (c->count) for
    //   compatibility.
    for(i = 0; i < path_len[c->count]; i++) {
      uint8_t j = (*path)[c->count][i];
      // PAD: if [(c XOR d) AND d] != 0 then incompatible
#  if KNOWN_BIT == 0 && PAD_MODE == 1
      if ((c->m[j] & ~dm[j]) != 0)
#  endif
#  if KNOWN_BIT == 0 && PAD_MODE == 2
      if (((c->m[j] ^ dm[j]) & ~dm[j]) != 0)
#  endif
#  if KNOWN_BIT == 1 && PAD_MODE == 1
      if((~c->m[j] & dm[j]) != 0)
#  endif
#  if KNOWN_BIT == 1 && PAD_MODE == 2
      if (((c->m[j] ^ dm[j]) & dm[j]) != 0)
#  endif
      {
        return false;
      }
    }

    return true;
  } else {
    // Key Schedule is complete, check all elements.
    for(i = 0; i < NUM_KS_BYTES; i++) {
      // PAD: if [(c XOR d) AND d] != 0 then incompatible
#  if KNOWN_BIT == 0 && PAD_MODE == 1
      if ((c->m[i] & ~dm[i]) != 0)
#  endif
#  if KNOWN_BIT == 0 && PAD_MODE == 2
      if (((c->m[i] ^ dm[i]) & ~dm[i]) != 0)
#  endif
#  if KNOWN_BIT == 1 && PAD_MODE == 1
      if((~c->m[i] & dm[i]) != 0)
#  endif
#  if KNOWN_BIT == 1 && PAD_MODE == 2
      if (((c->m[i] ^ dm[i]) & dm[i]) != 0)
#  endif
      {
        return false;
      }
    }

    return true;
  }
}

#  if PAD_MODE == 3 || PAD_MODE == 4
bool isCompatiblePAD(CandidateMatrix* c, uint8_t *dm) {
  uint8_t i;

  if(c->count < NUM_ROUND_BYTES) {
    // Check derived bytes for this stage (c->count) for
    //   compatibility.
    for(i = 0; i < path_len[c->count]; i++) {
      uint8_t j = (*path)[c->count][i];

      // PAD: if [(c XOR d) AND d] != 0 then incompatible
      if(padIsIncompatible(c->m[j], dm[j])) {
        return false;
      }
    }

    return true;

  } else {
    // Key Schedule is complete, check all elements.
    for(i = 0; i < NUM_KS; i++) {
      // PAD: if [(c XOR d) AND d] != 0 then incompatible
      if(padIsIncompatible(c->m[i], dm[i])) {
        return false;
      }
    }

    return true;
  }
}

#  endif
#endif
#if Error_Model == 2

/**
* Compatibility Check between two Key Schedules.
*
* Model: Expected Value as Treshold.
*
* note: c->count is equals to stage (stack.size()).
*
* @return  1/TRUE     => it is compatible
*          0/FALSE => is is not compatible
*/
bool isCompatibleEVTRecurse(CandidateMatrix* c, uint8_t *dm, uint16_t* currentN0, uint16_t* currentN1, uint16_t* tmp_N0, uint16_t* tmp_N1) {
  // Compute number of bit errors, if we take this candidate.
  *tmp_N0 = 0;
  *tmp_N1 = 0;
#  if DEBUG == 1
  isCompatibleCalls++;
#  endif

  // Inline former isCompatible in aeskeyfixOpt for EVT
  if(c->count < NUM_ROUND_BYTES ) {
    // Check derived bytes for this stage (c->count) for compatibility.
    uint8_t i;

    for(i = 0; i < path_len[c->count]; i++) {
      // Position to check.
      uint8_t k = (*path)[c->count][i];
      evt(c->m[k], dm[k], tmp_N0, tmp_N1);
    }

  } else {
    // Get missing elements.
    uint8_t i;

    for(i = 0; i < NUM_KS_BYTES; i++) {
      bool flag = false;
      uint8_t j;
      uint8_t k;

      // TODO: Test idea to pre-compute the flag array this does 
      // probably not happen very often...
      for(j = 0; j < sizeof(path_len); j++) {
        for(k = 0; k < path_len[j]; k++) {
          if((*path)[j][k] == i) {
            flag = true;
          }
        }
      }

      if(!flag) {
        evt(c->m[i], dm[i], tmp_N0, tmp_N1);
      }
    }
  }

  // End former isCompatible in aeskeyfixOpt for EVT
  if(((*currentN0+*tmp_N0) <= expected_N0) && ((*currentN1+*tmp_N1) <= expected_N1)) {
    return true;
  }

  return false;
}

/**
* Compatibility Check between two Key Schedules.
*
* Model: Expected Value as Treshold.
*
* note: c->count is equals to stage (stack.size()).
*
* @return  1/TRUE     => it is compatible
*          0/FALSE => is is not compatible
*/
bool isCompatibleEVT(CandidateMatrix* c, uint8_t *dm) {
  // Compute number of bit errors, if we take this candidate.
  uint16_t tmp_N0 = 0;
  uint16_t tmp_N1 = 0;
#  if DEBUG == 1
  isCompatibleCalls++;
#  endif

  // Inline former isCompatible in aeskeyfixOpt for EVT
  if(c->count < NUM_ROUND_BYTES ) {
    // Check derived bytes for this stage (c->count) for compatibility.
    uint8_t i;

    for(i = 0; i < path_len[c->count]; i++) {
      // Position to check.
      uint8_t k = (*path)[c->count][i];
      evt(c->m[k], dm[k], &tmp_N0, &tmp_N1);
    }

  } else {
    // Get missing elements.
    uint8_t i;

    for(i = 0; i < NUM_KS_BYTES; i++) {
      bool flag = false;
      uint8_t j;
      uint8_t k;

      for(j = 0; j < sizeof(path_len); j++) {
        for(k = 0; k < path_len[j]; k++) {
          if((*path)[j][k] == i) {
            flag = true;
          }
        }
      }

      if(!flag) {
        evt(c->m[i], dm[i], &tmp_N0, &tmp_N1);
      }
    }
  }

  // End former isCompatible in aeskeyfixOpt for EVT
  if(((currentN0+tmp_N0) <= expected_N0) && ((currentN1+tmp_N1) <= expected_N1)) {
    // Backup current n0 and n1.
    recN0[c->count] = currentN0;
    recN1[c->count] = currentN1;

    // Increase with tmp.
    currentN0 += tmp_N0;
    currentN1 += tmp_N1;
    return true;
  }

  return false;
}
#endif

#if MODE == 1
// Equivalent iterative expression for key recovery.
void recoverIte(uint8_t *dm, CandidateMatrix *c) {
  // Flag to recover from last tree level, if exhausted.
  bool recoverFromLast = false;

  // increased to 17(33), which is a endless loop. 
  //   needed to continue search after first result
  while(c->count != NUM_ROUND_BYTES+1) {
    short int i = 0;

    if(recoverFromLast) {
      // Do a rollback to last stage.
      c->guessRollback();
      // Recover last value for i and increase it.
#  if BETTERGUESS == true
      i = guessOrderRev[c->count][c->m[(*path)[c->count][0]]] + 1;
#  else
      i = c->m[(*path)[c->count][0]] + 1;
#  endif
#  if Error_Model==2
      // Set last valid n0 and n1.
      currentN0 = recN0[c->count];
      currentN1 = recN1[c->count];
#  endif
      c->count--;
      recoverFromLast = false;
    }

    while(i <= 255) {
      // Set guessed and inferred bytes.
      // increments count [= stack size]
      c->guess(i);
      // Check compatibility of guessed and inferred bytes.
#  if Error_Model==1
      if(isCompatiblePAD(c, dm))
#  endif
#  if Error_Model==2
      if(isCompatibleEVT(c, dm))
#  endif
      {
        // this causes search to continue after first result
        if (c->count == NUM_ROUND_BYTES) {
          printKeySchedule(c->m);
#  if CONTINUE_AFTER_FIRST_RESULT == true
          recoverFromLast = true;
          c->count-=2;
#  endif
#  if CONTINUE_AFTER_FIRST_RESULT == false
          c->count++;
#  endif
        }

        // If compatible, go to next stage.
        // Or continue search, if this is a valid result
        break;

      } else {
        // If not compatible, try next byte.
        // If not compatible after stage 15, this will be called many times!
        c->count--;
        // i++ not necessary in stage 16. TODO: Compare performance!
        i++;

        if (c->count == NUM_ROUND_BYTES - 1) {
          recoverFromLast = true;
          break;
        }
      }
    }

    // Check if search for stage is exhausted.
    if(i == 256) {
      if(c->count == -1) {
        //EXHAUSTED - NO RESULT! 
        return;
      } else {
        //EXHAUSTED - recover from last valid state. 
        recoverFromLast = true;
      }
    }
  }
}
#endif

#if MODE == 5
void recoverRecurseMutexCilk(uint8_t* dm, CandidateMatrix* c, uint8_t starti, uint8_t endi,
                             uint16_t currentLocalN0, uint16_t currentLocalN1) {
  uint16_t tmp_N0=0;
  uint16_t tmp_N1=0;

  if(c->count <  NUM_ROUND_BYTES - 1) {
    for(short i=starti; i <= endi; i++) {
      // Set guessed and inferred bytes.
      // increments count [= stack size]
      c->guess(i);
      // Check compatibility of guessed and inferred bytes.
#  if Error_Model==1
      if(isCompatiblePAD(c, dm))
#  endif
#  if Error_Model==2
      if(isCompatibleEVTRecurse(c, dm, &currentLocalN0, &currentLocalN1, &tmp_N0, &tmp_N1))
#  endif
      {
        if (c->count < DO_NOT_SPAWN_BELOW) {
          uint16_t noPackets=256/WP_SIZE;
          uint16_t j;

          for(j=0; j< noPackets; j++) {
            //Copy
            CandidateMatrix* newcm= new CandidateMatrix(c->count, c->m, c->d);
            cilk_spawn recoverRecurseMutexCilk(dm, newcm, j*WP_SIZE, (j+1)*WP_SIZE-1, currentLocalN0+tmp_N0, currentLocalN1+tmp_N1);
          }

          //If WP_SIZE is not 2^k, the last call in the loop has a border 
          // lower than 255. The remaining Candidates need to be checked
          if (noPackets*WP_SIZE < 256) {
            CandidateMatrix* newcm= new CandidateMatrix(c->count, c->m, c->d);
            cilk_spawn recoverRecurseMutexCilk(dm, newcm, noPackets*WP_SIZE, 255, currentLocalN0+tmp_N0, currentLocalN1+tmp_N1);
          }
        } else {
          // avoid multiple subcalls on level 14, as on 
          // next level completefrom8 will be called!
          CandidateMatrix* newcm= new CandidateMatrix(c->count, c->m, c->d);
          cilk_spawn recoverRecurseMutexCilk(dm, newcm, 0, 255, currentLocalN0+tmp_N0, currentLocalN1+tmp_N1);
        }

#  if CONTINUE_AFTER_FIRST_RESULT != true
        if (doNotContinue)
          return;
#  endif
      }

      c->count--;
    }

    if (c->count>-1) {
      delete c;
    }
  } else {
    // Do the guess on level 16 only once!
    c->guess(0);
#  if Error_Model==1
    if (isCompatiblePAD(c, dm))
#  endif
#  if Error_Model==2
    if (isCompatibleEVTRecurse(c, dm, &currentLocalN0, &currentLocalN1, &tmp_N0, &tmp_N1))
#  endif
    {
      printf("last stage compatible mode5\n");
      printKeySchedule(c->m);
      //copying no longer needed, as copy is done on recursion step!
      ListOfResults* result = new ListOfResults (c);
      // Obtain Mutex!
      lockMutex();

      if (allResults == NULL) {
        allResults = result;

      } else
        allResults->append(result);

      // Release Mutex!
      unlockMutex();
#  if CONTINUE_AFTER_FIRST_RESULT != true
      doNotContinue=1;
#  endif
      return;
    }

    // only called, if no valid result 
    // (otherwise return end execution of subtask earlier)
    delete c;
  }
}

// Just a wrapper to encapsulate starti, endi and currentNx values
void recoverRecurseMutexCilk(uint8_t *dm, CandidateMatrix *c) {
  uint16_t noPackets=256/WP_SIZE;
  uint16_t j;

  for(j=0; j< noPackets; j++) {
    //Copy
    CandidateMatrix* newcm= new CandidateMatrix(c->count, c->m, c->d);
    cilk_spawn recoverRecurseMutexCilk(dm, newcm, j*WP_SIZE, (j+1)*WP_SIZE-1, 0, 0);
  }

  //If WP_SIZE is not 2^k, the last call in the loop has a border lower than 255. The remaining Candidates need to be checked
  if (noPackets*WP_SIZE < 256) {
    CandidateMatrix* newcm= new CandidateMatrix(c->count, c->m, c->d);
    cilk_spawn recoverRecurseMutexCilk(dm, newcm, noPackets*WP_SIZE, 255, 0, 0);
  }
}
#endif

/**
* Count the number of ones in a given byte.
*
* @param  b
* @param  bit  one or zero
* @return n    the number of ones or zeros.
*/
uint8_t countBit(uint8_t b) {
  uint8_t n = 0;
  uint8_t i;

  for(i = 0; i < 8; i++) {
    if(((b >> i) & 1) == KNOWN_BIT) {
      n++;
    }
  }

  return n;
}

/**
* Count the number of ones in a given byte array.
* @param  b
* @param  bit  one or zero
* @return n    the number of ones or zeros.
*/
uint8_t countBit(uint8_t *b, int count) {
  printf("%");
  uint8_t n = 0;
  uint8_t i;

  for(i = 0; i < count; i ++) {
    n += countBit(b[i]);
  }

  return n;
}

// subfunction of PathGenerator
#if AES_TYPE == 128
void generateRound1to15(uint8_t (*p)[NUM_ROUNDS], uint8_t pos, uint8_t *dm) {
  uint8_t bottomIndex = pos;
  uint8_t topIndex = pos;

  // Init key schedule positions as undefined.
  bool d[NUM_KS_BYTES] = { false };
  p[0][0] = pos;
  d[pos] = true;
  uint8_t round = 1;

  while(round < 11) {
    int8_t nNum = -1;
    int8_t mNum = -1;
    int16_t n = topIndex - 16;
    int16_t m = bottomIndex + 16;
    uint8_t l = path_len[round];
    uint8_t a[NUM_ROUNDS] = { 0 };
    uint8_t b[NUM_ROUNDS] = { 0 };

    if(n >= 0 && (d[n] == false)) {
      nNum = 0;
      uint8_t aBytes[NUM_ROUNDS] = { 0 };
      aBytes[0] = dm[n];
      a[0] = n;

      // TODO: can this be unsigned?
      int16_t last = n;
      uint8_t i;

      for(i = 1; i < l; i++) {
        if(last % NUM_ROUND_BYTES < 4) {
          last = (last/16)*16 + 12 + ((last%16)+1)%4;
          aBytes[i] = dm[last];
        } else {
          last = last + 12;
          aBytes[i] = dm[last];
        }

        a[i] = last;
      }

      nNum = countBit(aBytes, NUM_ROUNDS);
    }

    if(m < NUM_KS_BYTES && (d[m] == false)) {
      mNum = 0;
      uint8_t bBytes[NUM_ROUNDS] = { 0 };
      bBytes[0] = dm[m];
      b[0] = m;

      // TODO: can this be unsigned?
      int16_t last = m;
      uint8_t i;

      for(i = 1; i < l; i++) {
        if(last % NUM_ROUND_BYTES < 4) {
          last = (last/16)*16 - 4 + ((last%16)+1)%4;
          bBytes[i] = dm[last];
        } else {
          last = last - 4;
          bBytes[i] = dm[last];
        }

        b[i] = last;
      }

      mNum = countBit(bBytes, NUM_ROUNDS);
    }

    if(nNum > mNum) {
      topIndex = a[0];
      uint8_t i;

      for(i = 0; i < path_len[round]; i++) {
        p[round][i] = a[i];
        d[a[i]] = true;
      }

    } else {
      bottomIndex = b[0];

      for(int i = 0; i < path_len[round]; i++) {
        p[round][i] = b[i];
        d[b[i]] = true;
      }
    }

    round ++;
  }

  // Generates the path of round 11.
  int16_t i;

  for(i = 172; i < 176; i++) {
    if(d[i-16] == true) {
      p[11][0] = i;
      break;
    }
  }

  int16_t last = p[11][0];
  d[last] = true;

  for(i = 1; i < path_len[11]; i++) {
    if(last % NUM_ROUND_BYTES < 4) {
      last = (last/16)*16 - 4 + ((last%16)+1)%4;
    } else {
      last = last - 4;
    }

    p[11][i] = last;
    d[last] = true;
  }

  // Generates the path of round 12.
  for(i = 128; i < 132; i++) {
    if(d[i] == false) {
      p[12][0] = i;
      break;
    }
  }

  last = p[12][0];
  d[last] = true;

  for(i = 1; i < path_len[12]; i++) {
    // Last.
    if(last % NUM_ROUND_BYTES >= 12) {
      if(last%16 == 12) {
        last = (last/16)*16 + 3;
      } else {
        last = last - 13;
      }

    } else {
      last = last - 12;
    }

    p[12][i] = last;
    d[last] = true;
  }

  // Generates the path of round 13.
  for(i = 7 * 16 + 12; i < 7 * 16 + 12 + 4; i++) {
    if(d[i] == false) {
      p[13][0] = i;
      break;
    }
  }

  last = p[13][0];
  d[last] = true;

  for(i = 1; i < path_len[13]; i++) {
    if(last % NUM_ROUND_BYTES >= 12) {
      if(last%16 == 12) {
        last = (last/16)*16 + 3;
      } else {
        last = last - 13;
      }

    } else {
      last = last - 12;
    }

    p[13][i] = last;
    d[last] = true;
  }

  // Generates the path of round 14.
  for(i = 7 * 16 + 8; i <  7 * 16 + 8 + 4; i++) {
    if(d[i] == false) {
      p[14][0] = i;
      break;
    }
  }

  last = p[14][0];
  d[last] = true;

  for(i = 1; i < path_len[14]; i++) {
    if(last % NUM_ROUND_BYTES >= 12) {
      if(last%16 == 12) {
        last = (last/16)*16 + 3;
      } else {
        last = last - 13;
      }

    } else {
      last = last - 12;
    }

    p[14][i] = last;
    d[last] = true;
  }

  // Generates the path of round 15.
  for(i = 172; i < 176; i ++) {
    if(d[i-16] == true && d[i] == false) {
      p[15][0] = i;
      break;
    }
  }

  last = p[15][0];
  d[last] = true;

  for(i = 1; i < path_len[15]; i++) {
    if(last % NUM_ROUND_BYTES < 4) {
      last = (last/16)*16 - 4 + ((last%16)+1)%4;
    } else {
      last = last - 4;
    }

    p[15][i] = last;
    d[last] = true;
  }
}
#endif

#if AES_TYPE == 256
void generateRound1to31(uint8_t (*p)[NUM_ROUNDS], uint8_t pos, uint8_t *dm) {
  p[0][0] = pos;
  uint8_t bottomIndex = pos;
  uint8_t topIndex = pos;

  // Init key schedule positions as undefined.
  bool d[NUM_KS_BYTES] = { false };
  d[pos] = true;
  uint8_t round = 1;

  while(round < 8) {
    int8_t nNum = -1;
    int8_t mNum = -1;
    int16_t n = topIndex - 32;
    int16_t m = bottomIndex + 32;
    uint8_t l = path_len[round];
    uint8_t a[NUM_ROUNDS] = { 0 };
    uint8_t b[NUM_ROUNDS] = { 0 };

    if(n >= 0 && (d[n] == false)) {
      nNum = 0;
      uint8_t aBytes[NUM_ROUNDS] = { 0 };
      aBytes[0] = dm[n];
      a[0] = n;
      int last = n;

      for(int i = 1; i < l; i ++) {
        if(last % 32 < 4 ) {
          last = (last/32)*32 + 28 + ((last%32)+1)%4;
          aBytes[i] = dm[last];

        } else {
          last = last + 28;
          aBytes[i] = dm[last];
        }

        a[i] = last;
      }

      nNum = countBit(aBytes, NUM_ROUNDS);
    }

    if(m < 32 * 7 + 4 && (d[m] == false)) {
      mNum = 0;
      uint8_t bBytes[NUM_ROUNDS] = { 0 };
      bBytes[0] = dm[m];
      b[0] = m;
      int last = m;

      for(int i = 1; i < l; i ++) {
        if(last % 32 < 4 ) {
          last = (last/32)*32 - 4 + ((last%32)+1)%4;
          bBytes[i] = dm[last];
        } else {
          last = last - 4;
          bBytes[i] = dm[last];
        }

        b[i] = last;
      }

      mNum = countBit(bBytes, NUM_ROUNDS);
    }

    if(nNum > mNum) {
      topIndex = a[0];

      for(int i = 0; i < l; i ++) {
        p[round][i] = a[i];
        d[a[i]] = true;
      }

    } else {
      bottomIndex = b[0];

      for(int i = 0; i < l; i ++) {
        p[round][i] = b[i];
        d[b[i]] = true;
      }
    }

    round ++;
  }

  //former generateRound8to11
  for(int r = 0; r < 3; r ++) {
    for(int i = 6*32 - 4*r; i < 6*32 - 4*r + 4; i ++)
      if(d[i] == true) {
        p[8 + r][0] = (i+1)%4 + 6*32 - 4*r;
        break;
      }

    int l = path_len[8 + r];
    int last = p[8 + r][0];
    d[last] = true;

    for(int i = 1; i < l; i++) {
      if(last% 32 >= 28 ) {
        if((last%32) == 28)
          last = (last/32)*32 + 3;
        else
          last = (last/32)*32 + (last%4)-1;
      } else
        last = last - 28;

      p[8 + r][i] = last;
      d[last] = true;
    }
  }

  for(int i = 7*32 + 4*3; i < 7*32 + 4*3 + 4; i ++) {
    if(d[i-32] == true) {
      p[11][0] = i;
      break;
    }
  }

  int l = path_len[11];
  int last = p[11][0];
  d[last] = true;

  for(int i = 1; i < l; i++) {
    if(last % 32 < 4)
      last = (last/32)*32 - 4 + (last + 1)%4;

    else
      last = last - 4;

    p[11][i] = last;
    d[last] = true;
  }

  //former generateRound12to31
  round = 12;

  //loop:
  for(int i = 0 ; i < 3; i ++) {
    for(int j = 0; j < 8 ; j ++)
      if(j < 7) {
        for(int k = 6*32 + 4*4 - 4*j; k <  6*32 + 4*4 - 4*j + 4; k ++)
          if(d[k] == true && d[6*32 + 4*4 - 4*j + (k+1)%4] == false) {
            p[round][0] = 6*32 + 4*4 - 4*j + (k+1)%4;
            break;
          }

        int last = p[round][0];
        int l = path_len[round];
        d[last] = true;

        for(int k = 1; k < l; k ++) {
          if(last% 32 >= 28 ) {
            if(last%4 == 0)
              last = (last/32)*32 + 3;
            else
              last = (last/32)*32 + last%4 - 1;

          } else
            last = last - 28;

          p[round][k] = last;
          d[last] = true;
        }

        if(round < 31)
          round ++;

        else
          return;// Original: break loop:, where loop was the outermost loop

      } else {
        for(int k = 7*32 + 3*4; k < 7*32 + 3*4 + 4; k ++) {
          if(d[k] == true && d[7*32 + 3*4 + (k+1)%4] == false) {
            p[round][0] = 7*32 + 3*4 + (k+1)%4;
            break;
          }
        }

        int last = p[round][0];
        int l = path_len[round];
        d[last] = true;

        for(int k = 1; k < l; k ++) {
          if(last % 32 < 4 )
            last = (last/32)*32 - 4 + (last+1)%4;

          else
            last = last - 4;

          p[round][k] = last;
          d[last] = true;
        }

        round ++;
      }
  }
}
#endif

ComparatorDecayProbability betterGuessComparator;
void pathGenerator(uint8_t *dm, uint8_t (*g_path)[NUM_ROUNDS], bool useStatic) {
  int i, j;

  if(!useStatic) {
    // Create four tmp. paths.
    uint8_t paths[4][NUM_ROUND_BYTES][NUM_ROUNDS] = { {{0}} };
    // Create count vars.
    uint8_t pos;
    uint16_t maxNum = 0;

    for(i = 0; i < 4; i ++) {
      // Position of max.
      pos = 0;
      // Value of max.
      maxNum = 0;

      for(j = 0; j < NUM_ROUNDS; j ++) {
        uint8_t n = countBit(dm[j * NUM_ROUND_BYTES + i]);

        if(n > maxNum) {
          pos = j * NUM_ROUND_BYTES + i;
          maxNum = n;
        }
      }

      // compute rest of path, start at pos.
#if AES_TYPE == 128
      generateRound1to15(paths[i], pos, dm);
#endif
#if AES_TYPE == 256
      generateRound1to31(paths[i], pos, dm);
#endif
    }

    // Loop over all generated paths and find the path with
    // the overall maximum known bits.
    maxNum = 0;
    // Max position.
    uint8_t max_path = 0;

    for(i = 0; i < 4; i++) {
      uint16_t num = 0;
      uint8_t j;

      for(j = 0; j < NUM_ROUND_BYTES; j++) {
#if TSOW_ACCTO_LOECKE == true
        num += countBit(dm[paths[i][j][0]]);
#else
        for(int k = 0; k < path_len[j]; k++) {
          num += countBit(dm[paths[i][j][k]]);
        }
#endif
      }

      if(num > maxNum) {
        maxNum = num;
        max_path = i;
      }
    }

    for(i = 0; i < NUM_ROUND_BYTES; i++) {
      uint8_t j;

      for(j = 0; j < NUM_ROUNDS; j++) {
        g_path[i][j] = paths[max_path][i][j];
      }
    }
  }

  // Create betterGuess order
  for(i = 0; i < NUM_ROUND_BYTES; i++) {
    uint8_t allBytes[256];

    for (j = 0; j < 256; j++)
      allBytes[j] = j;

    std::vector<uint8_t> byteOrder (allBytes, allBytes+256);
    betterGuessComparator.setDecayedByte(dm[g_path[i][0]]);
    std::sort(byteOrder.begin(), byteOrder.end(), betterGuessComparator);
    j = 0;

    for (std::vector<uint8_t>::iterator it=byteOrder.begin(); it!=byteOrder.end(); ++it) {
      guessOrder[i][j] = *it;
      guessOrderRev[i][*it] = j;
      j++;
    }
  }
}

// Evaluation.
typedef unsigned long long timestamp_t;
static timestamp_t get_timestamp () {
  struct timeval now;
  gettimeofday (&now, NULL);
  return  now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
}

// Helper.
uint8_t* mmapFile(string fname, int sizeInByte);
void replaceAll (string& str, const string& from, const string& to);

int main(int argc, char * argv[]) {

  if (argc != 6) {
    // Print usage.
    printf("usage: aeskeyfix_workstealing_cilk [decay_rate_sum] [loop_start_iteration] [loop_end_iteration] [repetitions] [decay_rate_p1] \n");
    return 1;
  }

  //decay_rate_sum determines the Folder for PAD, thereby its needed in both error models!
#if DEBUG == 1
  printf("WARNING: Debug is activated. This version scales bad for multiple cores!\n");
  printf("WARNING: For productive use, recompile this tool with debugging disabled.\n");
#endif
  // File name.
  char name[200];
//   char name_cor[200];
  // File data (AES Key Schedule).
  uint8_t *rks;
  uint8_t *rks_cor;
  printf ("Error Model: %d EVT_MODE: %d WP_SIZE %d MODE: %d\n", Error_Model, EVT_MODE, WP_SIZE, MODE);
#if Error_Model == 2
  double p0 = atof(argv[1])-atof(argv[5]);
  double p1 = atof(argv[5]);
  expected_N0= (int) (p0 * NUM_KS_BYTES * 8);
  expected_N1= (int) (p1 * NUM_KS_BYTES * 8);
  printf("p0: %f -> expected_N0: %d - - - p1: %f -> expected_N1: %d \n", p0, expected_N0, p1, expected_N1);
#  if (EVT_MODE == 2 || EVT_MODE == 3 || EVT_MODE == 6)
  evt_init();
#  endif
#endif
  // Create CandidateMatrix object.
  CandidateMatrix cm;
  CandidateMatrix* c = &cm;//&cm;
  ListOfResults* results=NULL;
  // Comparator object for betterGuess strategy
  betterGuessComparator.setn0ton1(p1*2);
  betterGuessComparator.setn1ton0(p0*2);
  // Evaluation.
  int runs = atoi(argv[3]) - atoi(argv[2]);
  timestamp_t *runtimes = (timestamp_t *)malloc(sizeof(timestamp_t) * runs);
  int i;

  for(i = atoi(argv[2]); i<atoi(argv[3]); i++) {
    int sum = atof(argv[1])*100;
#if Error_Model == 2
    // Number of currently consumed errors in each direction.
    currentN0 = 0;
    currentN1 = 0;
    int p1int = p1*1000;
    sprintf(name, "demo-input/key_schedules_decayed/%d.keys_decayed", i); // 
    printf("%d %d %d\n", sum, p1int, i);
#endif
// #if Error_Model == 1
//    sprintf(name, "/keysd/%d/0/%d.keysd", sum, i);
//     printf("%d %d\n", sum, i);
// #endif
//     sprintf(name_cor, "/keys/%d.keys", i);
    // mmap the input file
    rks = mmapFile(name, NUM_KS_BYTES);
//     rks_cor = mmapFile(name_cor, NUM_KS_BYTES);
    //printKeySchedule(rks);
    //printKeySchedule(rks_cor);
    pathGenerator(rks, path_static, USE_STATIC_PATH);
    path = &path_static;
    //        printPath(*path);
#if DEBUG == 1
    isCompatibleCalls=0;
    guessCalls=0;
    guessRollbackCalls=0;
#endif
    outputEnabled=false;
    timestamp_t times=0;

    //execute REPETITIONS times:
    for (int j=0; j<atoi(argv[4]); j++) {
      fflush(stdout);
      doNotContinue =false;
      timestamp_t t0 = get_timestamp();
#if MODE == 1
      recoverIte(rks, c);
#endif
#if MODE == 5
      recoverRecurseMutexCilk(rks, c);
#endif
      timestamp_t t1 = get_timestamp();
      times+=(t1-t0);
      // Reset state
      c->resetCM();

      for (int z = 0; z < NUM_ROUND_BYTES+2; z++) {
        recN0[z] = 0;
        recN1[z] = 0;
      }

      currentN0 = 0;
      currentN1 = 0;
      results=NULL;
    }

    // Reset CandidateMatrix.
    c->resetCM();
    // Evaluation.
#if DEBUG == 1
    printf("DEBUG:\n");
    printf("isCompatibleCalls: %d\n", isCompatibleCalls);
    printf("guessCalls: %d\n", guessCalls);
    printf("Copy operations: %d\n", copiesCount);
    printf("guessRollbackCalls: %d\n", guessRollbackCalls);
#else
    printf("Debug mode is turned off, no verbose output\n");
#endif
    printf("%d average: %llu\n", atoi(argv[4]), times/atoi(argv[4]));
    runtimes[i-atoi(argv[2])]=times/atoi(argv[4]);
  }

  // Evaluation.
  printf("\n ----------- DONE ---------- \n");

  for(int i = 0; i<runs; i++) {
    printf("%llu \n", runtimes[i]);
  }

  return 0;
}

// test, whether string can replace char[]!
uint8_t* mmapFile(string fname, int sizeInByte) {
  int fdin = -1;
  string fullfname = LINUX_BASEDIR + fname;

  replaceAll(fullfname, "\\", "/");
  char name [fullfname.length() + 1];
  strcpy(name, fullfname.c_str());

  if ((fdin = open(name, O_RDONLY)) < 0) {
    printf("ERR FDIN:%d\n", fdin);
    int j;

    for (j = 0; j < fullfname.length(); j++ ) {
      printf("%c", name[j] );
    }

    printf("\n");
  }

  return (uint8_t*) mmap(0, sizeInByte, PROT_READ, MAP_PRIVATE, fdin, 0);
}

// replaces instances of "from" in str with "to"
void replaceAll(string& str, const string& from, const string& to) {
  if(from.empty())
    return;

  uint16_t start_pos = 0;

  //strange behavior of string.find: -1 or string:npos did not work here
  while((start_pos = str.find(from, start_pos)) != 65535) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
  }
}
