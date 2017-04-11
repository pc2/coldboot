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

#include <algorithm>
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <math.h>
#include <sstream>
#include <sys/mman.h>
#include <vector>

//TODO: Dynamic size of SFlagMem, compatibility check complete from 8 undone, AES 256 undone, check whether t and PATH are still used
//Remark: //TODO: REGION: is used to define Regions in code.
#define USE_EVT 1
#define KNOWN_BIT 1
#define TSOW_ACCTO_LOECKE true // Should be used as it shows better performance
#define PAR_IMPL false // PAR_IMPL is deprecated. Do not use!
//Defines known bit for PAD model. KNOWN_BIT is the inverse of GROUND_STATE

#define USE_STATIC_PATH false
#define USE_BETTER_GUESS true
#define NUMBER_OF_SM 4
//FINAL_CHECK: In this generator, the maximum supported depth is 4 currently!
#define FINAL_CHECK_MAX_ADDER_DEPTH 4

#define NUM_ROUNDS 11
#define NUM_ROUND_BYTES 16 //== STAGE_COUNT
#define NUM_KS_BYTES 176

#define START_SPLIT_AT_STAGE 7
//START_SPLIT_AT_STAGE Minimum is 1 - There is no point in splitting a single compatibility check as done on stage 0
//Splitting is not supported for PAD error model
#define IMPLEMENT_SBOX_AS_SWITCH_TILL_STAGE 6
#define MAXIMUM_NUMBER_OF_SWITCHBOXES_PER_STATE 2
#define MAX_COMPLETE_DEPTH 4
//define the maximum number of sequential implications calculated in a single stage of the COMPLETE step. This might influence the maximum clock frequency.
//A value which is too low will result in an incomplete completion stage and the generator will abort.
//For the static path, the minimum value is 4.

//if Devel ==1, additional debug output is printed. with Devel == 2, even more debug is printed
#define Devel 1

//for PAD, spliiting of stages is pointless and therefor not supported.
#if  USE_EVT == 0

#define START_SPLIT_AT_STAGE 42
stagebits =4;
#endif

//stagebits defines the number of bits used to describe the stage. The number of stages is determined by the NUM_ROUND_BYTES (dependent on AES Type) and where the splitting of the stages (the Compat Checks) starts)
#if START_SPLIT_AT_STAGE > NUM_ROUND_BYTES
int stagebits = ceil (log (NUM_ROUND_BYTES) / log (2) );
#endif
#if START_SPLIT_AT_STAGE <= NUM_ROUND_BYTES
int stagebits = ceil(log(START_SPLIT_AT_STAGE + (2* (NUM_ROUND_BYTES-START_SPLIT_AT_STAGE))) / log(2));
#endif

using namespace std;

uint8_t path[NUM_ROUND_BYTES][NUM_ROUNDS] = {
  {   0, },                                                   //  0
  {  16,  13, },                                              //  1
  {  32,  29,  25, },                                         //  2
  {  48,  45,  41,  37, },                                    //  3
  {  64,  61,  57,  53,  49, },                               //  4
  {  80,  77,  73,  69,  65,  62, },                          //  5
  {  96,  93,  89,  85,  81,  78,  74, },                     //  6
  { 112, 109, 105, 101,  97,  94,  90,  86, },                //  7
  { 128, 125, 121, 117, 113, 110, 106, 102,  98, },           //  8
  { 144, 141, 137, 133, 129, 126, 122, 118, 114, 111, },      //  9
  { 160, 157, 153, 149, 145, 142, 138, 134, 130, 127, 123, }, // 10
  { 173, 169, 165, 161, 158, 154, 150, 146, 143, 139, 135, }, // 11
  { 131, 119, 107,  95,  82,  70,  58,  46,  33,  21,   9, }, // 12
  { 124, 115, 103,  91,  79,  66,  54,  42,  30,  17,   5, }, // 13
  { 120, 108,  99,  87,  75,  63,  50,  38,  26,  14,   1, }, // 14
  { 174, 170, 166, 162, 159, 155, 151, 147, 140, 136, 132, }, // 15
};

bool useEVT=USE_EVT;
bool availableThisStage[NUM_KS_BYTES];
bool availableNextStage[NUM_KS_BYTES];
int stage = 0, currentindex = 0;

//declares the stringstreams, which are used by the generateX functions
stringstream tBoxDeclare;
stringstream tBoxInit;
stringstream push;
stringstream pop;
stringstream steal;
stringstream isCompat;
stringstream code;

//FINAL_CHECK
stringstream isCompatFinal;

bool was_used[7][3];

// Define length of each path.
// This is the number of inferred bytes per round +1 (the first is the guessed byte)
uint8_t path_len[NUM_ROUND_BYTES] = 
  { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 11, 11, 11, 11 };

uint8_t guessOrder[NUM_ROUND_BYTES][256];
uint8_t guessOrderRev[NUM_ROUND_BYTES][256];

void printKeySchedule(uint8_t b[NUM_KS_BYTES]) {
  int i;
  int j;
  printf("     ");

  for(i = 0; i < 4; i++) {
    for(j = 0; j < 4; j++) {
      printf("%3d ", j);
    }
  }

  printf("\n");
  printf("   -------------------------------------------------------------\n");

  for(i = 0; i < NUM_ROUNDS; i ++) {
    printf("%2d | ", i);

    for(j = i*NUM_ROUND_BYTES; j < (i+1)*NUM_ROUND_BYTES; j ++) {
      printf("%3d ", b[j]);
    }

    printf("\n");
  }

  printf("\n");
}

void printGuessOrder() {
  int i;
  int j;
  printf("uint64_t betterGuess[256 * NUM_ROUND_BYTES] = {\n");

  for(i = 0; i < NUM_ROUND_BYTES; i++) {
    printf("\n // Byte pos. %2d\n", i);

    for(j = 0; j < 256; j++) {
      printf("\t%3d,\n", guessOrder[i][j]);
    }

    printf("\n");
  }

  printf("};\n");
}

/**
 * Print the recovery path.
 *
 * @param b the uint8_ts of key schedule.
 */
void printPath(uint8_t (*b)[NUM_ROUNDS]) {
  int i;
  int j;
  printf("uint8_t path[NUM_ROUND_BYTES][NUM_ROUNDS] = {\n");

  for(i = 0; i < NUM_ROUND_BYTES; i++) {
    printf("\t{ ");

    for(j = 0; j < path_len[i]; j++) {
      printf("%3d, ", b[i][j]);
    }

    printf("}, // %2d\n", i);
  }

  printf("};\n");
}

/**
 * Count the number of ones in a given byte.
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
 */
uint8_t countBit(uint8_t *b, int count) {
  uint8_t n = 0;
  uint8_t i;

  for(i = 0; i < count; i ++) {
    n += countBit(b[i]);
  }

  return n;
}

/**
 * Class ComparatorDecayProbability
 *
 * Reimplementation of Thomas Loecke's Java version
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

ComparatorDecayProbability betterGuessComparator;

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

void createBetterGuess(uint8_t *dm, uint8_t (*g_path)[NUM_ROUNDS]) {
  for(int i = 0; i < NUM_ROUND_BYTES; i++) {
    uint8_t allBytes[256];

    for (int j = 0; j < 256; j++)
      allBytes[j] = j;

    std::vector<uint8_t> byteOrder (allBytes, allBytes+256);
    betterGuessComparator.setDecayedByte(dm[g_path[i][0]]);
    std::sort(byteOrder.begin(), byteOrder.end(), betterGuessComparator);
    int j = 0;

    for (std::vector<uint8_t>::iterator it=byteOrder.begin(); it!=byteOrder.end(); ++it) {
      guessOrder[i][j] = *it;
      guessOrderRev[i][*it] = j;
      j++;
    }
  }
}

void pathGenerator(uint8_t *dm, uint8_t (*g_path)[NUM_ROUNDS]) {
  uint8_t i, j;
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
    generateRound1to15(paths[i], pos, dm);
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
# if PAR_IMPL == true
      num += countBit(dm[paths[i][j][(path_len[j]-1)/2]]);
# else
      num += countBit(dm[paths[i][j][0]]);
# endif
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
#if PAR_IMPL == true
    uint8_t j=0, k=0;

    while ((path_len[i]-1)/2 - j >= 0) {
      g_path[i][j+k] = paths[max_path][i][(path_len[i]-1)/2-j];
      j++;
      g_path[i][j+k] = paths[max_path][i][(path_len[i]-1)/2+k+1];
      k++;
    }

    while (j + k < NUM_ROUNDS) {
      g_path[i][j+k] = paths[max_path][i][(path_len[i]-1)/2+k+1];
      k++;
    }

#else
    uint8_t j;

    for(j = 0; j < NUM_ROUNDS; j++) {
      g_path[i][j] = paths[max_path][i][j];
    }
#endif
  }
}

bool isAvailableInKSC(int currentValue) {
  return availableThisStage[currentValue];
}
bool isAvailableAsCx(int currentValue) {
  return !availableThisStage[currentValue] && availableNextStage[currentValue];
}
bool isAvailable(int currentValue) {
  return isAvailableInKSC(currentValue) || isAvailableAsCx(currentValue);
}

bool isInferredWithoutSBox(int i, bool knownBytes[]) {
  //range checks are needed here to check for Array 
  //  out of Bounds-accesses causing wrong results.
  switch (i % 16) {
  case 0:
  case 1:
  case 2:
  case 3:
    //implication from next word c = c+4 XOR c-12
    if ((i - 12>=0) && knownBytes[i + 4] && knownBytes[i - 12])
      return true;

    return false;
  case 12:
  case 13:
    //implication from below c = c+12 XOR c+16
    if (((i + 16 < NUM_KS_BYTES) && knownBytes[i + 12] && knownBytes[i + 16]) ||
        //default implication c = c-4 XOR c-16
        ((i - 16>=0) && knownBytes[i - 4] && knownBytes[i - 16]))
      return true;

    return false;
  case 14:
  case 15:

    //implication from below c = c+12 XOR c+16
    if (((i + 16 < NUM_KS_BYTES) && knownBytes[i + 12] && knownBytes[i + 16]) ||
        //default implication c = c-4 XOR c-16
        ((i - 16>=0) && knownBytes[i - 4] && knownBytes[i - 16]))
      return true;

    return false;

  default:

    //implication from below c = c+12 XOR c+16
    if (((i + 16 < NUM_KS_BYTES) && knownBytes[i + 12] && knownBytes[i + 16]) ||
        //default implication c = c-4 XOR c-16
        ((i - 16>=0) && knownBytes[i - 4] && knownBytes[i - 16]) ||
        //next word implication, c =  c+4 XOR c-12
        ((i - 12>=0) && knownBytes[i + 4] && knownBytes[i - 12]))
      return true;

    return false;
  }
}

bool isInferredWithSBoxOnly(int i, bool knownBytes[]) {
  //range checks are needed here to check for Array out of Bounds-accesses causing wrong results.
  switch (i % 16) {
  case 0:
  case 1:
  case 2:
    if ((((i - 16>=0) && knownBytes[i - 16] && knownBytes[i - 3])
         || ((i + 16 < NUM_KS_BYTES) && knownBytes[i + 16] && knownBytes[i + 13]))
        &&	not (knownBytes[i + 4] && (i - 12>=0) && knownBytes[i - 12]))
      return true;

    return false;

  case 3:
    if ((((i - 16>=0) && knownBytes[i - 16] && knownBytes[i - 7])
         || ((i + 16 < NUM_KS_BYTES) && knownBytes[i + 16] && knownBytes[i + 9]))
        &&	not (knownBytes[i + 4] && (i - 12>=0) && knownBytes[i - 12]))
      return true;

    return false;

  case 12:
    if (((i + 7 < NUM_KS_BYTES) && knownBytes[i + 7] && knownBytes[i - 9])
        && not ((i - 16>=0) && knownBytes[i - 4] && knownBytes[i - 16])
        && not ((i + 16 < NUM_KS_BYTES) && knownBytes[i + 12] && knownBytes[i + 16]))
      return true;

    return false;

  case 13:
  case 14:
  case 15:
    if ((i + 3 < NUM_KS_BYTES)&&(knownBytes[i + 3] && knownBytes[i - 13])
        && not ((i - 16>=0) && knownBytes[i - 4] && knownBytes[i - 16])
        && not ((i + 16 < NUM_KS_BYTES) && knownBytes[i + 12] && knownBytes[i + 16]))
      return true;

    return false;

  default:
    return false;
  }
}

bool isUnSBox(int i) {
  switch (i % 16) {
  case 12:

    //if next word implication c = unSBox(c+7 XOR c-9) is available
    if ((isAvailable(i + 7) && isAvailable(i - 9))
        //and the others are not:
        && !(isAvailable(i + 12) && isAvailable(i + 16))
        && !(isAvailable(i - 4) && isAvailable(i - 16)))
      return true;

    break;

  case 13:
  case 14:
  case 15:

    //if next word implication c = unSBox(c+3 XOR c-13) is available
    if ((isAvailable(i + 3) && isAvailable(i - 13))
        //and the others are not:
        &&!(isAvailable(i + 12) && isAvailable(i + 16))
        &&!(isAvailable(i - 4) && isAvailable(i - 16)))
      return true;

    break;

  default:
    break;
  }

  return false;
}
//TODO: REGION: DerivationPrinters
#if (1)
void printValue(int currentValue) {
  if (isAvailableInKSC(currentValue))
    code << "ksC[SMID]["<< (int)  currentValue <<"]";
  else if (isAvailableAsCx(currentValue))
    code << "C" << (int)  currentValue;
  else
    cerr << "Tried to print unavailable value\n";
}

void printSimpleSwitchbox(int value, bool use_UN_sbox) {
  code << "		// Get ";

  if (use_UN_sbox)
    code << "un";

  code << "Sbox value. Complex computation.\n";
  code << "						_SWITCH(";
  printValue(value);
  code << ");\n";
  code << "						for (int i = 0; i <= 255; ++i) {\n";
  code << "							_CASE(i);\n";
  code << "							t";

  if (use_UN_sbox)
    code << "un";

  //code << "Sbox%d_%d <== ", stage, currentindex);
  code << "Sbox" << (int) stage << "_" << (int) currentindex <<" <== ";

  if (use_UN_sbox)
    code << "Un";

  code << "sbox[i];\n";
  code << "						}\n";
  code << "						_OTHERWISE(); _END_SWITCH();\n";
}

// Could be simplified, SBox is never used with 2 inputs, so no support needed (only needs UNsbox support)
void printDoubleSwitchbox(int value1, int value2, bool use_UN_sbox) {
  code << "		// Get ";

  if (use_UN_sbox)
    code << "un";

  code << "Sbox value. Complex computation.\n";
  code << "						_SWITCH(";
  printValue(value1);
  code << ".xor(";
  printValue(value2);
  code << "));\n";
  code << "						for (int i = 0; i <= 255; ++i) {\n";
  code << "							_CASE(i);\n";
  code << "							t";

  if (use_UN_sbox)
    code << "Un";

  code << "Sbox" << (int) stage << "_" << (int) currentindex <<" <== ";//, stage, currentindex);

  if (use_UN_sbox)
    code << "un";

  code << "sbox[i];\n";
  code << "						}\n";
  code << "						_OTHERWISE(); _END_SWITCH();\n";
}

void printDoubleSwitchboxRCon(int value1, int value2, int Rcon,
                              bool use_UN_sbox) {
  code << "		// Get ";

  if (use_UN_sbox)
    code << "un";

  code << "Sbox value. Complex computation.\n";
  code << "						_SWITCH(";
  printValue(value1);
  code << ".xor(";
  printValue(value2);
  code << ").xor(rcon["<< Rcon<<"])";//, Rcon;
  code << ");\n";
  code << "						for (int i = 0; i <= 255; ++i) {\n";
  code << "							_CASE(i);\n";
  code << "							t";

  if (use_UN_sbox)
    code << "Un";

  code << "Sbox" << (int) stage << "_" << (int) currentindex <<" <== ";

  if (use_UN_sbox)
    code << "un";

  code << "sbox[i];\n";
  code << "						}\n";
  code << "						_OTHERWISE(); _END_SWITCH();\n";
}

void printSimpleImplication(int i, int j, int k) {
  code << "DFEsmValue C" << i << " = ";//, i;
  printValue(j);
  code << ".xor(";
  printValue(k);
  code << ");";
  code << "\n";
  code << "		ksC[SMID]["<<i<<"].next <== C"<<i<<";\n";//, i, i);
}

void printFirstByteComplexImplicationRcon(int i,
    int XORinput, int SBoxInput, int Rcon, bool use_ROM_for_SBox, int phase) {
  if (use_ROM_for_SBox) {
    if (phase == 0) {
      code << "			rom";
      code << "Sbox[SMID].address <== ";
      printValue(SBoxInput);
      code << ";\n";
    }

    if (phase == 1) {
      code << "DFEsmValue C"<<i<<" = ";//, i);
      printValue(XORinput);
      code << ".xor(rom";
      code << "Sbox[SMID].dataOut).xor(rcon["<<Rcon<<"]);\n";//, Rcon);
    }

  } else {
    if (phase == 0) {
      printSimpleSwitchbox(SBoxInput, false);
    }

    if (phase == 1) {
      code << "DFEsmValue C"<<i<<" = ";//, i);
      printValue(XORinput);
      code << ".xor(t";
      code << "Sbox" << stage << "_" << currentindex <<").xor(rcon[" << Rcon <<   "]);\n";
    }
  }
}

void printFirstByteComplexImplicationNoRcon(int i, int XORinput,
    int SBoxInput, bool use_ROM_for_SBox, int phase) {
  if (use_ROM_for_SBox) {
    if (phase == 0) {
      code << "			rom"
           "Sbox[SMID].address <== ";
      printValue(SBoxInput);
      code << ";\n";
    }

    if (phase == 1) {
      code << "			DFEsmValue C"<<i<<" = ";//, i);
      printValue(XORinput);
      code << ".xor(rom";
      code << "Sbox[SMID].dataOut);\n";
    }

  } else {
    if (phase == 0) {
      printSimpleSwitchbox(SBoxInput, false);
    }

    if (phase == 1) {
      code << "			DFEsmValue C"<<i<<" = ";//, i);
      //code << "DFEsmValue C%d = ", i);
      printValue(XORinput);
      code << ".xor(t";
      code << "Sbox" << stage << "_" << currentindex <<");\n";//, stage, currentindex);
    }
  }
}

void printInverseFirstByteComplexImplicationNoRcon(int i,
    int SBoxInput1, int SBoxInput2, bool use_ROM_for_SBox,
    int phase) {
  if (use_ROM_for_SBox) {
    if (phase == 0) {
      code << "			romUnSbox[SMID].address <== ";
      printValue(SBoxInput1);
      code << ".xor(";
      printValue(SBoxInput2);
      code << ");\n";
    }

    if (phase == 1) {
      code << "			DFEsmValue C"<<i<<" = ";//, i);
      code << "rom";
      code << "UnSbox[SMID].dataOut;\n";
    }

  } else {
    if (phase == 0) {
      printDoubleSwitchbox(SBoxInput1, SBoxInput2, true);
    }

    if (phase == 1) {
      code << "			DFEsmValue C"<<i<<" = t";//, i);
      code << "UnSbox" << stage << "_" << currentindex << ";\n";//, stage, currentindex);
    }
  }
}

void printInverseFirstByteComplexImplicationRcon(int i, int SBoxInput1,
    int SBoxInput2, int Rcon, bool use_ROM_for_SBox, int phase) {
  if (use_ROM_for_SBox) {
    if (phase == 0) {
      code << "			romUnSbox[SMID].address <== ";
      printValue(SBoxInput1);
      code << ".xor(";
      printValue(SBoxInput2);
      code << ").xor(rcon["<<Rcon<<"]);\n";
    }

    if (phase == 1) {
      code << "			DFEsmValue C"<<i<<" = ";
      code << "romUnSbox[SMID].dataOut;\n";
    }

  } else {
    if (phase == 0) {
      printDoubleSwitchboxRCon(SBoxInput1, SBoxInput2, Rcon, true);
    }

    if (phase == 1) {
      code << "DFEsmValue C"<<i<<" = t"
           << "UnSbox" << stage << "_" << currentindex << ";\n";
    }
  }
}
#endif

// prints an implication. Phase is used to destinguish the 2 steps in a complex derivation
// (either set ROMS address and read from ROM or the SwitchBox and the access to the tBox variable)
void printCandidates(int i, bool use_ROM_for_SBox, int phase) {
  switch (i % 16) {
  case 0:

    //implication from next word c = c+4 XOR c-12
    if ((i-12>=0) && isAvailable(i + 4) && isAvailable(i - 12)) {
      printSimpleImplication(i, i + 4, i - 12);
      was_used[0][0]=true;
    }

    //default implication, c = c-16 XOR SBox(c-3) XOR Rcon (c/16)
    else if ((i-16>=0) && isAvailable(i - 16) && isAvailable(i - 3)) {
      printFirstByteComplexImplicationRcon(i, i - 16, i - 3, i / 16,
                                           use_ROM_for_SBox, phase);
      was_used[0][1]=true;
    }

    //implication from below, c = c+16 XOR SBox(c+13) XOR Rcon ((c+16)/16)
    else if ((i+16<NUM_KS_BYTES) && isAvailable(i + 16) && isAvailable(i + 13)) {
      printFirstByteComplexImplicationRcon(i, i + 16, i + 13, (i + 16)
                                           / 16, use_ROM_for_SBox, phase);
      was_used[0][2]=true;

    } else {
      cerr << "Implication not possible: ERROR!";
      exit(1);
    }

    break;

  case 1:
  case 2:

    //implication from next word c = c+4 XOR c-12
    if ((i-12>=0) && isAvailable(i + 4) && isAvailable(i - 12)) {
      printSimpleImplication(i, i + 4, i - 12);
      was_used[1][0]=true;
    }

    //default implication, c = c-16 XOR SBox(c-3)
    else if ((i-16>=0) && isAvailable(i - 16) && isAvailable(i - 3)) {
      printFirstByteComplexImplicationNoRcon(i, i - 16, i - 3,
                                             use_ROM_for_SBox, phase);
      was_used[1][1]=true;
    }

    //implication from below, c = c+16 XOR SBox(c+13)
    else if ((i+16<NUM_KS_BYTES) && isAvailable(i + 16) && isAvailable(i + 13)) {
      printFirstByteComplexImplicationNoRcon(i, i + 16, i + 13,
                                             use_ROM_for_SBox, phase);
      was_used[1][2]=true;

    } else {
      cerr << "Implication not possible: ERROR!";
      exit(1);
    }

    break;

  case 3:

    //implication from next word c = c+4 XOR c-12
    if ((i-12>=0) && isAvailable(i + 4) && isAvailable(i - 12)) {
      printSimpleImplication(i, i + 4, i - 12);
      was_used[2][0]=true;
    }

    //default implication, c = c-16 XOR SBox(c-7)
    else if ((i-16>=0) && isAvailable(i - 16) && isAvailable(i - 7)) {
      printFirstByteComplexImplicationNoRcon(i, i - 16, i - 7,
                                             use_ROM_for_SBox, phase);
      was_used[2][1]=true;
    }

    //implication from below, c = c+16 XOR SBox(c+9)
    else if ((i+16<NUM_KS_BYTES) && isAvailable(i + 16) && isAvailable(i + 9)) {
      printFirstByteComplexImplicationNoRcon(i, i + 16, i + 9,
                                             use_ROM_for_SBox, phase);
      was_used[2][2]=true;

    } else {
      cerr << "Implication not possible: ERROR!";
      exit(1);
    }

    break;

  case 12:

    //implication from below c = c+12 XOR c+16
    if ((i+16<NUM_KS_BYTES) && isAvailable(i + 12) && isAvailable(i + 16)) {
      printSimpleImplication(i, i + 12, i + 16);
      was_used[3][0]=true;
    }

    //default implication c = c-4 XOR c-16
    else if ((i-16>=0) && isAvailable(i - 4) && isAvailable(i - 16)) {
      printSimpleImplication(i, i - 4, i - 16);
      was_used[3][1]=true;
    }

    //next word implication, c = unSBox(c+7 XOR c-9)
    else if ((i+7<NUM_KS_BYTES) && isAvailable(i + 7) && isAvailable(i - 9)) {
      printInverseFirstByteComplexImplicationNoRcon(i, i + 7, i - 9,
          use_ROM_for_SBox, phase);
      was_used[3][2]=true;

    } else {
      cerr << "Implication not possible: ERROR!";
      exit(1);
    }

    break;

  case 13:

    //implication from below c = c+12 XOR c+16
    if ((i+16<NUM_KS_BYTES) && isAvailable(i + 12) && isAvailable(i + 16)) {
      printSimpleImplication(i, i + 12, i + 16);
      was_used[4][0]=true;
    }

    //default implication c = c-4 XOR c-16
    else if ((i-16>=0) && isAvailable(i - 4) && isAvailable(i - 16)) {
      printSimpleImplication(i, i - 4, i - 16);
      was_used[4][1]=true;
    }

    //next word implication, c = unSBox(c+3 XOR c-13 XOR Rcon [(c+3)/16])
    else if ((i+3<NUM_KS_BYTES) && (i-13>=0) && isAvailable(i + 3) && isAvailable(i - 13)) {
      printInverseFirstByteComplexImplicationRcon(i, i + 3, i - 13, (i
          + 3) / 16, use_ROM_for_SBox, phase);
      was_used[4][2]=true;

    } else {
      cerr << "Case: 13, Implication not possible: ERROR!\n";
      exit(1);
    }

    break;

  case 14:
  case 15:

    //implication from below c = c+12 XOR c+16
    if ((i+16<NUM_KS_BYTES) && isAvailable(i + 12) && isAvailable(i + 16)) {
      printSimpleImplication(i, i + 12, i + 16);
      was_used[5][0]=true;
    }

    //default implication c = c-4 XOR c-16
    else if ((i-16>=0) && isAvailable(i - 4) && isAvailable(i - 16)) {
      printSimpleImplication(i, i - 4, i - 16);
      was_used[5][1]=true;
    }

    //next word implication, c = unSBox(c+3 XOR c-13)
    else if ((i+13<NUM_KS_BYTES) && isAvailable(i + 3) && isAvailable(i - 13)) {
      printInverseFirstByteComplexImplicationNoRcon(i, i + 3, i - 13,
          use_ROM_for_SBox, phase);
      was_used[5][2]=true;

    } else {
      cerr << "Implication not possible: ERROR!";
      exit(1);
    }

    break;

  default:

    //implication from below c = c+12 XOR c+16
    if ((i+16<NUM_KS_BYTES) && isAvailable(i + 12) && isAvailable(i + 16)) {
      printSimpleImplication(i, i + 12, i + 16);
      was_used[6][0]=true;
    }

    //default implication c = c-4 XOR c-16
    else if ((i-16>=0) && isAvailable(i - 4) && isAvailable(i - 16)) {
      printSimpleImplication(i, i - 4, i - 16);
      was_used[6][1]=true;
    }

    //next word implication, c =  c+4 XOR c-12
    else if ((i-12>=0) && isAvailable(i + 4) && isAvailable(i - 12)) {
      printSimpleImplication(i, i + 4, i - 12);
      was_used[6][2]=true;

    } else {
      cerr << "Case: default,"<<i
           <<", Implication not possible: ERROR! , index"<<currentindex<<"\n";
      exit(1);
    }

    break;
  }

  if (phase == 1)
    code << "		ksC[SMID]["<<i<<"].next <== C"<<i<<";\n";//, i, i);
}


// dest:
// 0 = Modes.STATE_PUSH
// 1 = sStage[SMID] + 1
// 2 = Modes.COMPLETE_FROM8
//prints the compatibility check of each stage in the path implication stages
void printCompatCheck(int dest, bool decrement, int stage) {
  //code << "CompatCheck(%d,%d)\n", dest, decrement);
  if (useEVT) {
    code << "	IF(sFlagRecover[SMID].eq(false).and(((sCurrentN0[SMID]+tmp_n0) <= expected_n0).and(((sCurrentN1[SMID]+tmp_n1) <= expected_n1)))) {\n";
    code << "	n0[SMID].next <== tmp_n0;\n		n1[SMID].next <== tmp_n1;\n";

  } else
    code << "	IF(sFlagRecover[SMID].eq(false).and(isCompatible_pad_stage"<< stage<<"())) {\n";//, stage);

  if (dest == 2) {
    code << "		debug.simPrintf(\"  --> %d  \\n\", sNextGuessIndex[SMID]);\n";
    code << "	//	printKeySchedule(ksC[SMID]);\n";
    code << "		debug.simPrintf(\" --> Round 8 complete! \\n\");\n";
    code << "		// Done ... PUSH State, derive remaining bits of key Schedule and verify\n";
  }

  if (dest == 0) {
    code << "		// Yes, PUSH STATE\n		sMode[SMID].next <== Modes.STATE_PUSH;\n";

    if (USE_BETTER_GUESS)
      code << "//Set ROM adress to 0 position of next stage (realstage " << stage+1 <<")\n"
           << "	romBetterGuess[SMID].address <== 256*"<<stage+1<<";\n"
           << "	sValueBackupOnPush[SMID].next <== romBetterGuess[SMID].dataOut.cast(typeByte);\n";
  }

  if (dest == 1)
    code << "		// Go to next stage for Xb \n		sStage[SMID].next <== sStage[SMID] + 1;\n";

  if (dest == 2)
    code << "		sMode[SMID].next <== Modes.STATE_PUSH;\n";

  code << "	} ELSE {\n";
  code << "		// Not compatible, all bytes tested?\n";
  code << "		IF(sNextGuessIndex[SMID].eq(";

  if (USE_BETTER_GUESS)
    code << "256";

  else
    code << "255";

  code <<")) {\n";
  code << "			//  - YES: POP STATE\n";
  code << "			sMode[SMID].next <== Modes.STATE_POP;\n";
  code << "		} ELSE {\n";
  code << "			//  - NO: test next byte\n";
  code << "			sNextGuessIndex[SMID].next <== sNextGuessIndex[SMID] + 1;\n";
  code << "			ksC[SMID]["<<(int)path[stage][0]<<"].next <== ";

  if (USE_BETTER_GUESS)
    code << "romBetterGuess[SMID].dataOut;\n"
         << "romBetterGuess[SMID].address <== (256*" << stage << "+ sNextGuessIndex[SMID] + 1).cast(typeBetterGuessROMAddress);\n";

  else
    code << "sNextGuessIndex[SMID] + 1;\n";

  code << "			// Reset memory flags.\n";
  code << "			sFlagMem[SMID][0].next <== false;\n";
  code << "			sFlagMem[SMID][1].next <== false;\n";
  code << "			sFlagMem[SMID][2].next <== false;\n";
  code << "			sFlagMem[SMID][3].next <== false;\n";
  code << "		}\n";
  code << "		// Set to normal mode.\n";
  code << "		sFlagRecover[SMID].next <== false;\n";

  // for split stages (7b=8, 8b, 9b ... etc) Go back to last stage Xa
  // if Decrement then...
  if (decrement)
    code << "			sStage[SMID].next <== sStage[SMID] - 1;\n";

  code << "	}\n";
}

//acommulative functions introduced for readability
void printSameCaseCompatCheck(int stage, int curSubstage) {
  code << "							sFlagMem[SMID]["<<curSubstage - 1<<"].next <== true;\n"
       << "			}\n			ELSE {\n";

  if (useEVT==true) {
    code << "					// Compute errors in each direction in parallel.\n";

    if (stage >= START_SPLIT_AT_STAGE) {
      code << "					tmp_n0 = isCompatible_evt_stage"<<stage<<"a(1, SMID).cast(typeError);\n"
           << "					tmp_n1 = isCompatible_evt_stage"<<stage<<"a(0, SMID).cast(typeError);\n";

    } else {
      code << "					tmp_n0 = isCompatible_evt_stage"<<stage<<"(1, SMID).cast(typeError);\n"
           << "					tmp_n1 = isCompatible_evt_stage"<<stage<<"(0, SMID).cast(typeError);\n";
    }

  } else {

  }

  code << "					// debug.simPrintf(\"-- %d/%d/%d %d/%d/%d -- \\n\", tmp_n0, sCurrentN0[SMID], expected_n0, tmp_n1, sCurrentN1[SMID], expected_n1);\n"
       << "					// Check for compatibility.\n";

  if (stage >= START_SPLIT_AT_STAGE) {
    printCompatCheck(1, false, stage);

  } else if (stage < 15)
    printCompatCheck(0, false, stage);

  else
    printCompatCheck(2, false, stage);
}
void printSplittedCaseCompatCheck(int stage) {
  code << "					// Compute errors in each direction in parallel.\n"
       <<"					tmp_n0 = isCompatible_evt_stage"<< stage <<"b(1, SMID).cast(typeError);\n"
       <<"					tmp_n1 = isCompatible_evt_stage"<< stage <<"b(0, SMID).cast(typeError);\n"
       <<"					// debug.simPrintf(\"-- %d/%d/%d %d/%d/%d -- \\n\", tmp_n0, sCurrentN0[SMID], expected_n0, tmp_n1, sCurrentN1[SMID], expected_n1);\n"
       <<"					// Check for compatibility.\n";

  if (stage < 15)
    printCompatCheck(0, true, stage);

  if (stage == 15)
    printCompatCheck(2, true, stage);
}

//TODO: REGION: Static printers. These parts do not depend on the chosen path
#if (1)
void printImports() {
  code << "import java.util.ArrayList;\n\n";
  code << "import com.maxeler.maxcompiler.v2.managers.DFEManager;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.DFEsmAssignableValue;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.DFEsmInput;\n";

  if (USE_BETTER_GUESS)
    code << "import com.maxeler.maxcompiler.v2.statemachine.DFEsmSinglePortMappedROM;\n";

  code << "import com.maxeler.maxcompiler.v2.statemachine.DFEsmSinglePortROM;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.DFEsmStateEnum;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.DFEsmStateValue;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.DFEsmValue;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.Latency;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.manager.DFEsmPullInput;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.manager.DFEsmPushOutput;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.manager.ManagerStateMachine;\n";
  code << "import com.maxeler.maxcompiler.v2.statemachine.types.DFEsmValueType;\n";
}

void printDeclarations() {
  code << "// Public IO, reused in Manager.\n";
  code << "static public final String IO_IN_DATA  = \"input\";\n";
  code << "static public final String IO_OUT_DATA = \"output\";\n";
  code << "\n";
  code << "// State machine main modes.\n";
  code << "enum Modes {\n";
  code << "	// Initiation stuff.\n";
  code << "	INIT,\n";
  code << "	// Guess byte for position (0..255).\n";
  code << "	GUESS_STAGE_BYTE,\n";
  code << "	// Save state on stack.\n";
  code << "	STATE_PUSH,\n";
  code << "	// Recover state from stack.\n";
  code << "	STATE_POP,\n";
  code << "	// Compute complete key schedule from round 8.\n";
  code << "	COMPLETE_FROM8,\n";
  //FINAL_CHECK
  code << "	// Verify final number of errors.\n";
  code << "	FINAL_CHECK,\n";
  code << "	// No result.\n";
  code << "	RESULT_NO,\n";
  code << "	// Computation done.\n";
  code << "	RESULT_DONE,\n";
  code << "	// Exit.\n";
  code << "	SYSTEM_EXIT\n";
  code << "}\n";
  code << "\n";
  code << "private final int NUMBER_OF_SM=" << NUMBER_OF_SM << ";\n";
  code << "\n";
  code << "// Types.\n";
  code << "private final DFEsmValueType typeByte = dfeUInt(8);\n";

  if (useEVT)
    code << "private final DFEsmValueType typeError = dfeInt(10);\n";

  code << "private final DFEsmValueType typeStage = dfeUInt("<< stagebits <<");\n";
  code << "private final DFEsmValueType typeBoolean = dfeBool();\n";
  code << "private final DFEsmValueType typeUInt64 = dfeUInt(64);\n";

  if (USE_BETTER_GUESS)
    code << "private final DFEsmValueType typeBetterGuessROMAddress = dfeUInt(12);\n";

  code << "\n";
  code << "\n";
  code << "// Helper.\n";
  code << "//  - Number of bytes in whole key schedule.\n";
  code << "private static final int KEY_SIZE = 176;\n";
  code << "//  - Number of bytes in each round.\n";
  code << "private static final int NUM_BYTES = 16;\n";
  code << "//  - Number of rounds.\n";
  code << "private static final int NUM_ROUNDS = 11;\n";
  code << "\n";
  code << "// I/O.\n";
  code << "private final DFEsmPullInput input;\n";
  code << "private final DFEsmPushOutput output;\n";
  code << "\n";
  code << "private final DFEsmStateValue clkCnt;\n";
  code << "\n";

  if (useEVT)
    code << "private final DFEsmInput expected_n0;\n"
         <<"private final DFEsmInput expected_n1;\n\n";

  code << "// State.\n";
  code << "//multi-state SM. These values are needed for every submachine\n";
  code << "// - Current main mode.\n";
  code << "private final ArrayList<DFEsmStateEnum<Modes>>sMode;\n";
  code << "//\n";
  code << "// - Current stage.\n";
  code << "private final DFEsmStateValue[] sStage;\n";
  code << "private final DFEsmStateValue[] maxStage;\n";
  code << "// - Current guessing byte.\n";
  code << "private final DFEsmStateValue[] sNextGuessIndex;\n";
  code << "// Candidate Key Schedule.\n";
  code << "private final DFEsmStateValue[][] ksC;\n";
  code << "\n";
  code << "// - Flag to control recovery on incompatible\n";
  code << "//     assignments.\n";
  code << "private final DFEsmStateValue[] sFlagRecover;\n";
  code << "\n";
  code << "// Recovery Stack.\n";
  code << "// Key Schedule Candidate and guessing byte.\n";
  code << "private final DFEsmStateValue[][][] recS_C;\n";
  code << "private final DFEsmStateValue[][] recS_sNextGuessIndex;\n";

  if (useEVT) {
    code << "private final DFEsmStateValue[][] recS_n0;\n";
    code << "private final DFEsmStateValue[][] recS_n1;\n";
    code << "\n";
    code << "// ValidityExaminer_ExpectedValueAsThreshold\n";
    code << "private final DFEsmStateValue[] n0;\n";
    code << "private final DFEsmStateValue[] n1;\n";
    code << "private final DFEsmStateValue[] sCurrentN0;\n";
    code << "private final DFEsmStateValue[] sCurrentN1;\n";
    code << "\n";
  }

  code << "// ROMs.\n";
  code << "private final DFEsmSinglePortROM[] romSbox;\n";
  code << "private final DFEsmSinglePortROM[] romUnSbox;\n";

  if (USE_BETTER_GUESS)
    code << "private final DFEsmSinglePortMappedROM[] romBetterGuess;\n";

  code << "// - Flags to control memory request/response.\n";
  code << "//     to wait for memory.\n";
  code << "private final DFEsmStateValue[][] sFlagMem;\n";
  code << "\n";
  code <<"//if (NUMBER_OF_SM > 1){\n";
  code <<"	// New for Coordinated Stack Access\n";
  code <<"	private final DFEsmStateValue[] topOfStack;\n";
  code <<"	//BOS: indicates the stage of the oldest working packet which has not been stolen yet\n";
  code <<"	private final DFEsmStateValue[] bottomOfStack;\n";
  code <<"	private final DFEsmStateValue[] idleNoResult;\n\n";
  code <<"//}\n\n";

  if (USE_BETTER_GUESS)
    code <<"	//if Better Guess ROM is used\n"
         <<"	private final DFEsmStateValue[] sValueBackupOnPush;\n";

  code << "// - Control output/input data/flags\n";
  code << "private final DFEsmStateValue sReadDataReady;\n";
  code << "// - Current count (to read input).\n";
  code << "private final DFEsmInput pollID;\n";
  code << "private final DFEsmStateValue lastPollID;\n";
  code << "private final DFEsmStateValue outValid;\n";
  code << "private final DFEsmStateValue byteOut;\n";
  code << "private final DFEsmStateValue sendingReply;\n";
  code << "private final DFEsmStateValue sCounterIn;\n";
  code << "private final DFEsmStateValue sCounterOut;\n\n";
  code << "// Decay Key Schedule.\n";
  code << "private final DFEsmStateValue[] ksD;\n";
  code << "\n";
  code << "// Interval for several SMs.\n";
  code << "private final int sGuessIndexStart;\n";
  code << "private final int sGuessIndexEnd;\n";
  code << "private final int smID;\n";
  code << "\n";
}

void printConstructor() {
  code << "/**\n"
       << "* Init state variables.\n"
       << "*\n"
       << "* @param owner\n"
       << "*/\n";
  code << "protected AESKeyFixEVT_SM(DFEManager owner, int sGuessIndexStart, int sGuessIndexEnd, int smID) {\n";
  code << "super(owner);\n";
  code << "\n";
  code << "// Set guessing interval for first tree level.\n";
  code << "this.sGuessIndexStart = sGuessIndexStart;\n";
  code << "this.sGuessIndexEnd = sGuessIndexEnd;\n";
  code << "this.smID = smID;\n";
  code << "\n";
  code << "// I/O.\n";
  code << "// Set up inputs/outputs\n";
  code << "input = io.pullInput(\"input\", typeByte);\n";
  code << "output = io.pushOutput(\"output\", typeByte, 64);\n";
  code << "\n";
  code << "clkCnt = state.value(typeUInt64, 0);\n\n";
  code << "// State.\n";
  code << "//for In-/Output (needed only once)\n";
  code << "sCounterIn = state.value(typeByte, 0);\n";
  code << "sCounterOut = state.value(typeByte, 0);\n";
  code << "sReadDataReady = state.value(typeBoolean, false);\n";
  code << "pollID = io.scalarInput(\"pollID\", typeUInt64);\n";
  code << "lastPollID = state.value(typeUInt64, Long.MAX_VALUE);\n";
  code << "byteOut = state.value(typeByte, 0);\n";
  code << "outValid = state.value(typeBoolean, 0);\n";
  code << "sendingReply = state.value(typeBoolean, 0);\n\n";
  code <<		"//initialize variables which are needed once per submachine\n"
       <<"		sMode = new ArrayList<DFEsmStateEnum<Modes>>(NUMBER_OF_SM);\n"
       <<"		//ArrayList needed, due to Generic Type of DFEsmStateEnum<Modes>:\n"
       <<"		//Not allowed:\n"
       <<"		//sMode = new DFEsmStateEnum<Modes> [NUMBER_OF_SM];\n"
       <<"		//sMode = new Memory[NUMBER_OF_SM];\n"
       <<"		sStage = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"		maxStage = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"		sNextGuessIndex = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"		\n"
       <<"		//sMode[0]=state.enumerated(Modes.class, Modes.INIT);\n"
       <<"		sMode.add(0, state.enumerated(Modes.class, Modes.INIT));\n"
       <<"		for (int i=1;i< NUMBER_OF_SM;i++){\n"
       <<"			//sMode[i]=state.enumerated(Modes.class, Modes.STATE_POP);\n"
       <<"			sMode.add(i, state.enumerated(Modes.class, Modes.STATE_POP));\n"
       <<"		}\n"
       <<"		for (int SMID=0;SMID< NUMBER_OF_SM;SMID++){\n"
       <<"			sStage[SMID] = state.value(typeStage, 0);\n"
       <<"			maxStage[SMID] = state.value(typeStage, 0);\n"
       <<"			sNextGuessIndex[SMID] = state.value(";

  if (USE_BETTER_GUESS)
    code <<	"typeBetterGuessROMAddress";

  else
    code <<	"typeByte";

  code << ", this.sGuessIndexStart);\n"
       <<"		}\n"
       <<"		\n"
       <<"		// Boolean Flag for Recovery.\n"
       <<"		sFlagRecover = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"		// Boolean Flag for Recovery.\n"
       <<"		for (int SMID=0; SMID< NUMBER_OF_SM; SMID++){\n"
       <<"			sFlagRecover[SMID] = state.value(typeBoolean, false);\n"
       <<"		}\n"
       <<"		\n"
       <<"		// Recovery Stack States.\n"
       <<"		recS_C = new DFEsmStateValue[NUMBER_OF_SM][NUM_BYTES][KEY_SIZE];\n"
       <<"		recS_sNextGuessIndex = new DFEsmStateValue[NUMBER_OF_SM][NUM_BYTES];\n"
       <<"		recS_n0 = new DFEsmStateValue[NUMBER_OF_SM][NUM_BYTES];\n"
       <<"		recS_n1 = new DFEsmStateValue[NUMBER_OF_SM][NUM_BYTES];\n"
       <<"\n"
       <<"		// ValidityExaminer_ExpectedValueAsThreshold\n"
       <<"		n0 = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"		n1 = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"		sCurrentN0 = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"		sCurrentN1 = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"\n"
       <<"		//(Un)SBox Roms\n"
       <<"		sFlagMem = new DFEsmStateValue[NUMBER_OF_SM][4];\n"
       <<"		romUnSbox = new DFEsmSinglePortROM [NUMBER_OF_SM];\n"
       <<"		romSbox = new DFEsmSinglePortROM [NUMBER_OF_SM];\n";

  if (USE_BETTER_GUESS)
    code <<"		romBetterGuess = new DFEsmSinglePortMappedROM [NUMBER_OF_SM];\n";

  code <<"\n"
       <<"		\n"
       <<"		// Initiate Key Decayed Schedule with zero.\n"
       <<"		// Initiate Candidate Key Schedule with zero.\n"
       <<"		//TODO: Check whether replication of ksD improves performance, e.g. higher clock frequency possible\n"
       <<"		ksD = new DFEsmStateValue[KEY_SIZE];\n"
       <<"		ksC = new DFEsmStateValue[NUMBER_OF_SM][KEY_SIZE];\n"
       <<"		for (int SMID=0;SMID< NUMBER_OF_SM;SMID++){\n"
       <<"			for(int i=0; i<KEY_SIZE; i++) {\n"
       <<"				ksD[i] = state.value(typeByte, 0);\n"
       <<"				ksC[SMID][i] = state.value(typeByte, 0);\n"
       <<"			}\n"
       <<"			// Recovery Stack States.\n"
       <<"			// Initiate Recovery Stack with zero.\n"
       <<"\n"
       <<"			for(int i=0; i<NUM_BYTES; i++) {\n"
       <<"				for(int j=0; j<KEY_SIZE; j++) {\n"
       <<"					// Candidate.\n"
       <<"					recS_C[SMID][i][j] = state.value(typeByte, 0);\n"
       <<"				}\n"
       <<"				// Guessing byte.\n"
       <<"				recS_sNextGuessIndex[SMID][i] = state.value(";

  if (USE_BETTER_GUESS)
    code <<	"typeBetterGuessROMAddress";

  else
    code <<	"typeByte";

  code << ", 0);\n"
       <<"				// n0.\n"
       <<"				recS_n0[SMID][i] = state.value(typeError, 0);\n"
       <<"				// n1.\n"
       <<"				recS_n1[SMID][i] = state.value(typeError, 0);\n"
       <<"			}\n"
       <<"			// ValidityExaminer_ExpectedValueAsThreshold\n"
       <<"			n0[SMID] = state.value(typeError, 0);\n"
       <<"			n1[SMID] = state.value(typeError, 0);\n"
       <<"			sCurrentN0[SMID] = state.value(typeError, 0);\n"
       <<"			sCurrentN1[SMID] = state.value(typeError, 0);\n"
       <<"			// ROMs\n"
       <<"			romUnSbox[SMID] = mem.rom(typeByte, Latency.ONE_CYCLE, unsbox);\n"
       <<"			romSbox[SMID] = mem.rom(typeByte, Latency.ONE_CYCLE, sbox);\n";

  if (USE_BETTER_GUESS)
    //for (int i=0; i< NUMBER_OF_SM
    code << "romBetterGuess[SMID] = mem.romMapped(\"betterGuess\" + SMID, typeByte, 256*16, Latency.ONE_CYCLE);\n";

  code <<"			// Value get Helper.\n"
       <<"			for(int i=0; i<sFlagMem[SMID].length; i++) {\n"
       <<"				sFlagMem[SMID][i] = state.value(typeBoolean, false);\n"
       <<"			}\n"
       <<"		}\n"
       <<"\n"
       <<"		// New for Coordinated Stack Access\n"
       <<"		//ONLY NEDED IN MULTI_SM\n"
       <<"		//if (NUMBER_OF_SM > 1){\n"
       <<"			topOfStack = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"			bottomOfStack = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"			idleNoResult = new DFEsmStateValue[NUMBER_OF_SM];\n"
       <<"			for (int SMID=0; SMID< NUMBER_OF_SM; SMID++){\n"
       <<"				// New for Coordinated Stack Access\n"
       <<"				topOfStack[SMID] = state.value(typeByte, 0);\n"
       <<"				bottomOfStack[SMID]= state.value(typeByte, 0);\n"
       <<"				idleNoResult[SMID]= state.value(typeBoolean, false);\n"
       // only needed for better Guess ROM
       <<"		//	}\n"
       <<"		}\n";

  if (USE_BETTER_GUESS)
    code << "		// Only needed with enabled BETTER_GUESS\n"
         << "		sValueBackupOnPush = new DFEsmStateValue[NUMBER_OF_SM];\n"
         << "		for (int SMID=0; SMID< NUMBER_OF_SM; SMID++){\n"
         << "			sValueBackupOnPush[SMID] = state.value(typeByte, 0);\n"
         << "		}\n";

  if (useEVT) {
    code << "// ValidityExaminer_ExpectedValueAsThreshold Expected Values\n";
    code << "expected_n0 = io.scalarInput(\"expected_n0\", typeError);\n";
    code << "expected_n1 = io.scalarInput(\"expected_n1\", typeError);\n";
  }

  code << "}\n";
}

#endif
//TODO: REGION: Generators
#if (1)
void generateTBox() {
  memset (availableThisStage, 0, NUM_KS_BYTES*sizeof(bool));
  memset (availableNextStage, 0, NUM_KS_BYTES*sizeof(bool));
  tBoxDeclare << "// Sbox temporary value for large switch-blocks.\n";
  int curSubstage;
  bool usedSwitchBox=false;
  availableNextStage[path[0][0]]=true;

  for (stage = 0; stage < 16; stage++) {
    bool use_ROM_forSBox = stage > IMPLEMENT_SBOX_AS_SWITCH_TILL_STAGE;
    curSubstage=0;
    availableNextStage[path[stage][0]]=true;

    for (currentindex = 1; currentindex < path_len[stage]; currentindex++) {
      if (currentindex == 1)
        curSubstage++;

      if (isInferredWithSBoxOnly(path[stage][currentindex], availableNextStage)) {
        if (use_ROM_forSBox) {
          curSubstage++;

          for (int j=0; j< NUM_KS_BYTES; j++)
            availableThisStage[j]=availableNextStage[j];

        } else {
          //derive corrsponding TSBox/TUnSBox for tBox Declaration & initialisation
          usedSwitchBox=true;

          if (isUnSBox(path[stage][currentindex])) {
            tBoxDeclare << "DFEsmAssignableValue tUnSbox"
                        <<stage <<"_"<<currentindex
                        <<" = assignable.value(typeByte);\n";
            tBoxInit << "tUnSbox"<<stage <<"_"
                     <<currentindex <<".connect(0);\n";

          } else {
            tBoxDeclare << "DFEsmAssignableValue tSbox" << stage
                        << "_" << currentindex
                        << " = assignable.value(typeByte);\n";
            tBoxInit << "tSbox" <<stage << "_"
                     << currentindex << ".connect(0);\n";
          }
        }
      }

      availableNextStage[path[stage][currentindex]]=true;
    }
  }

  if (!usedSwitchBox)
    //delete tBox Comment, if no SwitchBox is used
    tBoxDeclare.str("");

  memset (availableThisStage, 0, NUM_KS_BYTES*sizeof(bool));
  memset (availableNextStage, 0, NUM_KS_BYTES*sizeof(bool));
}
void generatePushPopSteal() {
  push << "CASE (Modes.STATE_PUSH) {\n";
  push << "debug.simPrintf(\"  --> Stage %d COMPATIBLE Index %d \\n\", sStage[SMID], sNextGuessIndex[SMID]-1);\n" << "\n" << "// printKeySchedule(ksC[SMID]);\n" << "\n";
  push << "// Set next stage.\n"
       << "sStage[SMID].next <== sStage[SMID]+1;\n"
       << "IF (maxStage[SMID].eq(sStage[SMID]))\n"
       << "maxStage[SMID].next <== sStage[SMID]+1;\n"
       << "// Change main state.\n"
       << "sMode[SMID].next <== Modes.GUESS_STAGE_BYTE;\n";
  push << "SWITCH (sStage[SMID]) {\n";
  push << "// reduced representation: In first Case of a splitted stage, no action is performed. So these cases are not mentioned\n// If the old Otherwise (with rom.address setter) is restored, they might need to be used again\n";
  pop << "private void doPOP(int SMID){\n" << "// Stage exhausted.\n" << "// Recover last valid state.\n";
  pop << "debug.simPrintf(\"  <-- EXHAUSTED/BACKTRACKING - RECOVER LAST VALID STATE  \\n\");\n" << "\n";
  pop << "// Go back to last stage.\n" << "sStage[SMID].next <== sStage[SMID]-1;\n" << "\n";
  pop << "SWITCH (sStage[SMID]) {\n" << "// Stage 0.\n" << "CASE (0) {\n" << "// No result.\n";
  pop << "sMode[SMID].next <== Modes.RESULT_NO;\n" << "}\n";
  steal << "private void doSteal(int fromSMID, int toSMID){\n"
        << "debug.simPrintf(\"  Will steal now from %d to %d   \\n\",fromSMID,toSMID);\n\n"
        << "	SWITCH (bottomOfStack[fromSMID]) {\n";
  int curCase =0;
  int backtrackTo;

  for (int stage = 0; stage <= 16; stage++) {
    if (stage < START_SPLIT_AT_STAGE) {
      if (stage < 15)
        push << " CASE(" << curCase << ") { state_push(" << stage <<", SMID); }\n";

      if (curCase>0)
        pop << " CASE(" << curCase << ") { state_pop(" << stage -1 <<", SMID); }\n";

      steal << "CASE (" << stage << ") { state_steal(" << stage << ", fromSMID, toSMID); sStage[toSMID].next <== " << stage << ";}\n";

    } else {
      //First half of splitted state
      backtrackTo = curCase - 1;

      //if previous stage was already splitted
      if (stage-1 >= START_SPLIT_AT_STAGE)
        backtrackTo--;

      if (curCase>0) {
        pop << " CASE(" << curCase << ") { state_pop(" << stage -1 <<", SMID); sStage[SMID].next <== "<< backtrackTo <<"; }\n";
      }

      if (stage < 16) {
        steal << "CASE (" << stage << ") { state_steal(" << stage << ", fromSMID, toSMID); sStage[toSMID].next <== "<< curCase << ";}\n";
        curCase++;
        //Second half of splitted state
        push << " CASE(" << curCase << ") { state_push(" << stage <<", SMID); ";

        if (stage == 15) {
          push << "sStage[SMID].next <== 0; sMode[SMID].next <== Modes.COMPLETE_FROM8;";
        }

        push << "}\n";
        pop << " CASE(" << curCase << ") { state_pop(" << stage -1 <<", SMID); sStage[SMID].next <== "<<backtrackTo <<"; }\n";
      }
    }

    curCase++;
  }

  push << "	OTHERWISE {";//\n";
  push << "}\n" << "}\n";
  //close Otherwise & sStage[SMID]-Switch
  push << "// Reset guessing index.\n";

  if (!USE_BETTER_GUESS)
    push << "sNextGuessIndex[SMID].next <== 0;\n";

  else
    push << "sNextGuessIndex[SMID].next <== 1;\n";

  push << "\n"<< "// Reset helper.\n" << "sFlagMem[SMID][0].next <== false;\n"
       << "sFlagMem[SMID][1].next <== false;\n"
       << "sFlagMem[SMID][2].next <== false;\n"
       << "sFlagMem[SMID][3].next <== false;\n" << "\n";

  if (useEVT) {
    push << "// Update current values.\n"
         << "sCurrentN0[SMID].next <== sCurrentN0[SMID]+n0[SMID];\n"
         << "sCurrentN1[SMID].next <== sCurrentN1[SMID]+n1[SMID];\n" << "\n";
  }

  push << "if (NUMBER_OF_SM >1 ) \n topOfStack[SMID].next <== topOfStack[SMID]+1;\n"
       << "}\n";
  //
  pop << "OTHERWISE {";//\n" ;;
  pop << "}\n" << "}\n";
  pop << "// Set flag from recover.\n"
      << "sFlagRecover[SMID].next <== true;\n" << "\n";
  pop << "//TODO: Check the if != 0 below\n"
      << "IF (sStage[SMID] !== 0)\n"
      << "sMode[SMID].next <== Modes.GUESS_STAGE_BYTE;\n" << "}\n";
  steal << "OTHERWISE {\n"
        << "				debug.simPrintf(\"steal on stage 16+ from %d to %d\\n\",fromSMID, toSMID);\n"
        << "			}\n"
        << "		}\n"
        << "		// Set flag from recover.\n"
        << "		sFlagRecover[toSMID].next <== true;\n"
        << "\n"
        << "		sMode.get(toSMID).next <== Modes.GUESS_STAGE_BYTE;\n"
        << "		//sMode[toSMID].next <== Modes.GUESS_STAGE_BYTE;\n"
        << "	}\n"
        << "\n"
        << "\n";
}
//generates an adder tree for the isCompatible_evt_stageX methods
void generateAdderTreeEVT(int numberOfValues, stringstream& isCompat) {
  switch (numberOfValues) {
  case 1:
    isCompat << "return s0;\n";
    break;

  case 2:
    isCompat << "return s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n";
    break;

  case 3:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n\n"
             //This value is at most 16+8=24. Cast to 5 should be sufficient!
             << "return suma0.cast(dfeUInt(6)).add(s2.cast(dfeUInt(6)));\n";
    break;

  case 4:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma1 = s2.cast(dfeUInt(5)).add(s3.cast(dfeUInt(5)));\n\n"
             << "return suma0.cast(dfeUInt(6)).add(suma1.cast(dfeUInt(6)));\n";
    break;

  case 5:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma1 = s2.cast(dfeUInt(5)).add(s3.cast(dfeUInt(5)));\n\n"
             << "DFEsmValue sumb0 = suma0.cast(dfeUInt(6)).add(suma1.cast(dfeUInt(6)));\n\n"
             //This value is at most 32+8=40. Cast to 6 should be sufficient!
             << "return sumb0.cast(dfeUInt(7)).add(s4.cast(dfeUInt(7)));\n";
    break;

  case 6:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma1 = s2.cast(dfeUInt(5)).add(s3.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma2 = s4.cast(dfeUInt(5)).add(s5.cast(dfeUInt(5)));\n\n"
             << "DFEsmValue sumb0 = suma0.cast(dfeUInt(6)).add(suma1.cast(dfeUInt(6)));\n\n"
             //This value is at most 32+16=48. Cast to 6 should be sufficient!
             << "return sumb0.cast(dfeUInt(7)).add(suma2.cast(dfeUInt(7)));\n";
    break;

  case 7:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma1 = s2.cast(dfeUInt(5)).add(s3.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma2 = s4.cast(dfeUInt(5)).add(s5.cast(dfeUInt(5)));\n\n"
             << "DFEsmValue sumb0 = suma0.cast(dfeUInt(6)).add(suma1.cast(dfeUInt(6)));\n"
             //This value is at most 16+8=24. Cast to 5 should be sufficient!
             << "DFEsmValue sumb1 = suma2.cast(dfeUInt(6)).add(s6.cast(dfeUInt(6)));\n\n"
             //This value is at most 32+24=56. Cast to 6 should be sufficient!
             << "return sumb0.cast(dfeUInt(7)).add(sumb1.cast(dfeUInt(7)));\n";
    break;

  case 8:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma1 = s2.cast(dfeUInt(5)).add(s3.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma2 = s4.cast(dfeUInt(5)).add(s5.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma3 = s6.cast(dfeUInt(5)).add(s7.cast(dfeUInt(5)));\n\n"
             << "DFEsmValue sumb0 = suma0.cast(dfeUInt(6)).add(suma1.cast(dfeUInt(6)));\n"
             << "DFEsmValue sumb1 = suma2.cast(dfeUInt(6)).add(suma3.cast(dfeUInt(6)));\n\n"
             << "return sumb0.cast(dfeUInt(7)).add(sumb1.cast(dfeUInt(7)));\n";
    break;

  case 9:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma1 = s2.cast(dfeUInt(5)).add(s3.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma2 = s4.cast(dfeUInt(5)).add(s5.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma3 = s6.cast(dfeUInt(5)).add(s7.cast(dfeUInt(5)));\n\n"
             << "DFEsmValue sumb0 = suma0.cast(dfeUInt(6)).add(suma1.cast(dfeUInt(6)));\n"
             << "DFEsmValue sumb1 = suma2.cast(dfeUInt(6)).add(suma3.cast(dfeUInt(6)));\n\n"
             //This value is at most 32+8=40. Cast to 6 should be sufficient!
             << "DFEsmValue sumc0 = sumb0.cast(dfeUInt(7)).add(s8.cast(dfeUInt(7)));\n\n"
             //This value is at most 32+40=72. Cast to 7 should be sufficient!
             << "return sumb1.cast(dfeUInt(8)).add(sumc0.cast(dfeUInt(8)))\n";
    break;

  case 10:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma1 = s2.cast(dfeUInt(5)).add(s3.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma2 = s4.cast(dfeUInt(5)).add(s5.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma3 = s6.cast(dfeUInt(5)).add(s7.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma4 = s8.cast(dfeUInt(5)).add(s9.cast(dfeUInt(5)));\n\n"
             << "DFEsmValue sumb0 = suma0.cast(dfeUInt(5)).add(suma1.cast(dfeUInt(5)));\n"
             << "DFEsmValue sumb1 = suma2.cast(dfeUInt(5)).add(suma3.cast(dfeUInt(5)));\n\n"
             //This value is at most 16+32=48. Cast to 5 should be sufficient!
             << "DFEsmValue sumc0 = suma4.cast(dfeUInt(6)).add(sumb0.cast(dfeUInt(6)));\n\n"
             //This value is at most 48+32=80. Cast to 6 should be sufficient!
             << "return sumc0.cast(dfeUInt(7)).add(sumb1.cast(dfeUInt(7)));\n";
    break;

  case 11:
    isCompat << "DFEsmValue suma0 = s0.cast(dfeUInt(5)).add(s1.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma1 = s2.cast(dfeUInt(5)).add(s3.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma2 = s4.cast(dfeUInt(5)).add(s5.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma3 = s6.cast(dfeUInt(5)).add(s7.cast(dfeUInt(5)));\n"
             << "DFEsmValue suma4 = s8.cast(dfeUInt(5)).add(s9.cast(dfeUInt(5)));\n\n"
             << "DFEsmValue sumb0 = suma0.cast(dfeUInt(5)).add(suma1.cast(dfeUInt(5)));\n"
             << "DFEsmValue sumb1 = suma2.cast(dfeUInt(5)).add(suma3.cast(dfeUInt(5)));\n"
             //This value is at most 16+8=24
             << "DFEsmValue sumb2 = suma4.cast(dfeUInt(5)).add(s10.cast(dfeUInt(5)));\n\n"
             //This value is at most 24+32=56.
             << "DFEsmValue sumc0 = sumb2.cast(dfeUInt(6)).add(sumb0.cast(dfeUInt(6)));\n\n"
             //This value is at most 56+32=88. Cast to 6 should be sufficient!
             << "return sumc0.cast(dfeUInt(7)).add(sumb1.cast(dfeUInt(7)));\n";
    break;

  default:
    cerr << "ERROR. TRIED TO GENERATE ADDER TREE FOR "<<numberOfValues<<" ELEMENTS. ABORTING!";
    exit(2);
  }
}

//generates an ander tree for the isCompatible_pad_stageX methods
void generateAndTreePAD(int numberOfValues, stringstream& isCompat) {
  switch (numberOfValues) {
  case 1:
    isCompat << "return s0;\n";
    break;

  case 2:
    isCompat << "return s0.and(s1);\n";
    break;

  case 3:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n\n"
             << "return anda0.and(s2);\n";
    break;

  case 4:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n"
             << "DFEsmValue anda1 = s2.and(s3);\n\n"
             << "return anda0.and(anda1);\n";
    break;

  case 5:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n"
             << "DFEsmValue anda1 = s2.and(s3);\n\n"
             << "DFEsmValue andb0 = anda0.and(anda1);\n\n"
             << "return andb0.and(s4);\n";
    break;

  case 6:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n"
             << "DFEsmValue anda1 = s2.and(s3);\n"
             << "DFEsmValue anda2 = s4.and(s5);\n\n"
             << "DFEsmValue andb0 = anda0.and(anda1);\n\n"
             << "return andb0.and(anda2);\n";
    break;

  case 7:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n"
             << "DFEsmValue anda1 = s2.and(s3);\n"
             << "DFEsmValue anda2 = s4.and(s5);\n\n"
             << "DFEsmValue andb0 = anda0.and(anda1);\n"
             << "DFEsmValue andb1 = anda2.and(s6);\n\n"
             << "return andb0.and(andb1);\n";
    break;

  case 8:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n"
             << "DFEsmValue anda1 = s2.and(s3);\n"
             << "DFEsmValue anda2 = s4.and(s5);\n"
             << "DFEsmValue anda3 = s6.and(s7);\n\n"
             << "DFEsmValue andb0 = anda0.and(anda1);\n"
             << "DFEsmValue andb1 = anda2.and(anda3);\n\n"
             << "return andb0.and(andb1);\n";
    break;

  case 9:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n"
             << "DFEsmValue anda1 = s2.and(s3);\n"
             << "DFEsmValue anda2 = s4.and(s5);\n"
             << "DFEsmValue anda3 = s6.and(s7);\n\n"
             << "DFEsmValue andb0 = anda0.and(anda1);\n"
             << "DFEsmValue andb1 = anda2.and(anda3);\n\n"
             << "DFEsmValue andc0 = andb0.and(s8);\n\n"
             << "return andb1.cast(dfeUInt(8)).and(andc0.cast(dfeUInt(8)))\n";
    break;

  case 10:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n"
             << "DFEsmValue anda1 = s2.and(s3);\n"
             << "DFEsmValue anda2 = s4.and(s5);\n"
             << "DFEsmValue anda3 = s6.and(s7);\n"
             << "DFEsmValue anda4 = s8.and(s9);\n\n"
             << "DFEsmValue andb0 = anda0.and(anda1);\n"
             << "DFEsmValue andb1 = anda2.and(anda3);\n\n"
             << "DFEsmValue andc0 = anda4.and(andb0);\n\n"
             << "return andc0.and(andb1);\n";
    break;

  case 11:
    isCompat << "DFEsmValue anda0 = s0.and(s1);\n"
             << "DFEsmValue anda1 = s2.and(s3);\n"
             << "DFEsmValue anda2 = s4.and(s5);\n"
             << "DFEsmValue anda3 = s6.and(s7);\n"
             << "DFEsmValue anda4 = s8.and(s9);\n\n"
             << "DFEsmValue andb0 = anda0.and(anda1);\n"
             << "DFEsmValue andb1 = anda2.and(anda3);\n"
             << "DFEsmValue andb2 = anda4.and(s10);\n\n"
             << "DFEsmValue andc0 = andb2.and(andb0);\n\n"
             << "return andc0.and(andb1);\n";
    break;

  default:
    cerr <<
         "ERROR. TRIED TO GENERATE andER TREE FOR "<<numberOfValues<<" ELEMENTS. ABORTING!";
    exit(2);
  }
}

void generateIsCompat() {
  if (useEVT)
    isCompat << "public DFEsmValue isCompatible_evt_X(int p, int v, int SMID) {\n"
             << "// Compare every position, check if D_i and C_i are different\n"
             << "// (according to v) at this position.\n"
             << "DFEsmValue[] check = new DFEsmValue[8];\n" << "\n"
             << "for(int i = 0;i < 8; i ++){\n"
             << "// Test if D_i and C_i are different at this bit.\n"
             << "check[i] = (((ksC[SMID][p] >> i) & 1) ^ ((ksD[p] >> i) & 1)).eq(1).and(((ksD[p] >> i) & 1).neq(v)).cast(typeBoolean);\n"
             << "}\n\n" << "// Sum diffs.\n"
             << "DFEsmValue[] sum0 = new DFEsmValue[4];\n"
             << "sum0[0] = check[0].cast(dfeUInt(2)).add(check[1].cast(dfeUInt(2)));\n"
             << "sum0[1] = check[2].cast(dfeUInt(2)).add(check[3].cast(dfeUInt(2)));\n"
             << "sum0[2] = check[4].cast(dfeUInt(2)).add(check[5].cast(dfeUInt(2)));\n"
             << "sum0[3] = check[6].cast(dfeUInt(2)).add(check[7].cast(dfeUInt(2)));\n"
             << "\n" << "DFEsmValue[] sum1 = new DFEsmValue[2];\n"
             << "sum1[0] = sum0[0].cast(dfeUInt(3)).add(sum0[1].cast(dfeUInt(3)));\n"
             << "sum1[1] = sum0[2].cast(dfeUInt(3)).add(sum0[3].cast(dfeUInt(3)));\n"
             << "\n"
             << "return sum1[0].cast(dfeUInt(4)).add(sum1[1].cast(dfeUInt(4)));\n"
             << "}\n\n";

  else {
    isCompat << "	 * Perfect Asymmetric Decay.\n" << " *\n"
             << " * 1/TRUE 	=> it is compatible\n"
             << " * 0/FALSE 	=> is is not compatible\n" << " *\n"
             << " *  TODO: cascading tree.\n" << " *\n" << " * @return\n"
             << " */\n" << "private DFEsmValue pad(int i) {\n";

    if (KNOWN_BIT==1)
      isCompat << "	return ((ksC[SMID][i].xor(ksD[i]).and(ksD[i])).eq(constant.value(typeByte, 0))).cast(typeBoolean);\n}\n";

    else
      isCompat << "	return ((ksC[SMID][i].xor(ksD[i]).and(ksC[SMID][i])).eq(constant.value(typeByte, 0))).cast(typeBoolean);\n}\n";
  }

  for (int stage = 0; stage < 16; stage++) {
    if (stage < START_SPLIT_AT_STAGE) {
      //TODO: Remark: Maybe print path here?!
      if (useEVT) {
        isCompat << "private DFEsmValue isCompatible_evt_stage" << stage << "(int v, int SMID) {\n";

        for (int i = 0; i < path_len[stage]; i++) {
          isCompat << "DFEsmValue s" << (int) i << " = isCompatible_evt_X(" << (int) path [stage][i] << ", v, SMID);\n";
        }

        generateAdderTreeEVT(path_len[stage] - 0, isCompat);

      } else {
        isCompat << "private DFEsmValue isCompatible_pad_stage" << stage << " {\n";

        for (int i = 0; i < path_len[stage]; i++) {
          isCompat << "DFEsmValue s" << (int) i << " = pad(" << (int) path [stage][i] << ");\n";
        }

        generateAndTreePAD(path_len[stage] - 0, isCompat);
      }

      isCompat << "}\n\n";

    } else {
      isCompat << "private DFEsmValue isCompatible_evt_stage" << stage << "a(int v, int SMID) {\n";

      for (int i = 0; i < path_len[stage]/2; i++) {
        isCompat << "DFEsmValue s" << (int) i << " = isCompatible_evt_X(" << (int) path [stage][i] << ", v, SMID);\n";
      }

      generateAdderTreeEVT(path_len[stage]/2 - 0, isCompat);
      isCompat << "}\n\n";
      isCompat << "private DFEsmValue isCompatible_evt_stage" << stage << "b(int v, int SMID) {\n";

      for (int i = path_len[stage]/2; i < path_len[stage]; i++) {
        isCompat << "DFEsmValue s" << (int) i-path_len[stage]/2 << " = isCompatible_evt_X(" << (int) path [stage][i] << ", v, SMID);\n";
      }

      generateAdderTreeEVT(path_len[stage]-path_len[stage]/2, isCompat);
      isCompat << "}\n\n";
    }
  }
}
#endif

//prints the derivation stages
void printPathImplicationStages() {
  //Reset valid positions
  memset (availableThisStage, 0, NUM_KS_BYTES*sizeof(bool));
  memset (availableNextStage, 0, NUM_KS_BYTES*sizeof(bool));
  code << "	CASE (Modes.GUESS_STAGE_BYTE) {\n		SWITCH (sStage[SMID]) {\n"
       << "CASE (0) {\n" <<"t = 0;\n";

  if (USE_BETTER_GUESS)
    code << "romBetterGuess[SMID].address <== (256*t+ sNextGuessIndex[SMID]).cast(typeBetterGuessROMAddress);\n\n";

  code << "// Compute errors in each direction in parallel.\n";

  if (Devel > 0 ) {
    if (USE_BETTER_GUESS)
      code << "debug.simPrintf(\"Current Stage: 0 Current Index: %d, current value %d, next value (dataOut): %d\\n\", sNextGuessIndex[SMID]-1, ksC[SMID][path[0][0]], romBetterGuess[SMID].dataOut);\n";

    else
      code << "debug.simPrintf(\"Current Stage: 0 Current Index: %d, current value %d\\n\", sNextGuessIndex[SMID]-1, ksC[SMID][path[0][0]]);\n";
  }

  if (useEVT)
    code << "tmp_n0 = isCompatible_evt_stage0(1, SMID).cast(typeError);\n"
         << "tmp_n1 = isCompatible_evt_stage0(0, SMID).cast(typeError);\n\n"
         << "IF(sFlagRecover[SMID].eq(false).and(((sCurrentN0[SMID]+tmp_n0) <= expected_n0).and(((sCurrentN1[SMID]+tmp_n1) <= expected_n1)))) {\n"
         << "	n0[SMID].next <== tmp_n0;\n"
         << "	n1[SMID].next <== tmp_n1;\n\n";

  else
    code << "	IF(sFlagRecover[SMID].eq(false).and(isCompatible_pad_stage"<<stage<<"())) {\n";

  code << "	// Yes Compatible, PUSH STATE\n"
       << "	sMode[SMID].next <== Modes.STATE_PUSH;\n";

  if (USE_BETTER_GUESS)
    code << "//Set ROM adress to 0 position of next stage (realstage 1)\n"
         << "	romBetterGuess[SMID].address <== 256;\n"
         << "	sValueBackupOnPush[SMID].next <== romBetterGuess[SMID].dataOut.cast(typeByte);\n";

  code << "} ELSE {\n"
       << "	// Not compatible, all bytes tested?\n"
       << "	IF(sNextGuessIndex[SMID].eq(sGuessIndexEnd";

  if (USE_BETTER_GUESS)
    code << "+1";

  code << ")) {\n"
       << "		//  - YES: POP last valid state.\n"
       << "		sMode[SMID].next <== Modes.STATE_POP;\n"
       << "	} ELSE {\n"
       << "		//  - NO: test next byte.\n"
       << "		sNextGuessIndex[SMID].next <== sNextGuessIndex[SMID] + 1;\n"
       << "		ksC[SMID][path[t][0]].next <== ";

  if (USE_BETTER_GUESS)
    code << "romBetterGuess[SMID].dataOut;\n"
         << "romBetterGuess[SMID].address <== sNextGuessIndex[SMID] + 1;\n";

  else
    code << "sNextGuessIndex[SMID] + 1;\n";

  code << "}\n\n"
       << "		// TODO: check here. reset values.\n"
       << "		sCurrentN0[SMID].next <== 0;\n"
       << "		sCurrentN1[SMID].next <== 0;\n\n"
       << "		// Set to normal mode.\n"
       << "		sFlagRecover[SMID].next <== false;\n }\n }\n";
  int curCase = 1;
  int curSubstage = 0;
  availableNextStage[path[0][0]]=true;

  for (stage = 1; stage < 16; stage++) {
    int curSwitchBoxCount=0;
    code << "		//stage " << stage;//%d", stage);

    if (stage >= START_SPLIT_AT_STAGE)
      code << "a";

    code << "\n		CASE ("<<curCase++<<") {\n			t = "<<stage<<";\n";//, curCase++, stage);

    if (USE_BETTER_GUESS)
      code << "romBetterGuess[SMID].address <== (256*t+ sNextGuessIndex[SMID]).cast(typeBetterGuessROMAddress);\n\n";

    availableNextStage[path[stage][0]]=true;

    for (int j=0; j< NUM_KS_BYTES; j++)
      availableThisStage[j]=availableNextStage[j];

    for (currentindex = 1; currentindex < path_len[stage]; currentindex++) {
      if (currentindex == 1) {
        code << "							// Sub-Stage "<<curSubstage<<". \n";//, curSubstage);
        code << "								IF(sFlagRecover[SMID].eq(false).and(sFlagMem[SMID]["
             <<curSubstage++<<"].eq(false))) {\n";
      }

      if (isInferredWithSBoxOnly(path[stage][currentindex], availableNextStage)) {
        //printf("//SBOX!!! %d\n",currentindex);
        bool use_ROM_forSBox = stage > IMPLEMENT_SBOX_AS_SWITCH_TILL_STAGE || curSwitchBoxCount > MAXIMUM_NUMBER_OF_SWITCHBOXES_PER_STATE;// || currentindex > MAXIMUM_NUMBER_OF_IMPLICATIONS_PER_STATE
        printCandidates(path[stage][currentindex], use_ROM_forSBox, 0);

        if (use_ROM_forSBox) {
          code << "							sFlagMem[SMID]["<<curSubstage - 1
               <<"].next <== true;\n";//, curSubstage- 1);
          code << "			}\n			ELSE {\n";
          code << "							// Sub-Stage "<<curSubstage<<". \n";
          code <<
               "								IF(sFlagRecover[SMID].eq(false).and(sFlagMem[SMID]["<<curSubstage++<<"].eq(false))) {\n";

          for (int j=0; j< NUM_KS_BYTES; j++)
            availableThisStage[j]=availableNextStage[j];

          curSwitchBoxCount=0;

        } else
          curSwitchBoxCount++;

        printCandidates(path[stage][currentindex], use_ROM_forSBox, 1);

      } else {
        //printf("//AN IMPLICATION upper;\n");
        printCandidates(path[stage][currentindex], 0, 0);
        //As no Lookup is needed, parameter 2 and 3 do not matter!
      }

      availableNextStage[path[stage][currentindex]]=true;
    }

    printSameCaseCompatCheck(stage, curSubstage);

    for (; curSubstage > 0; curSubstage--)
      code << "}\n";

    //generate the second part of compatibility check, if level is at least StarSplitAtStage
    if (stage >= START_SPLIT_AT_STAGE) {
      code << "		}\n";
      code << "		//stage "<<stage<<"b\n";//, stage);
      code << "		CASE ("<<curCase++<<") {\n			t = "<<stage<<";\n";//, curCase++);//			t = %d;\n", curCase++, stage);

      if (USE_BETTER_GUESS)
        code << "romBetterGuess[SMID].address <== (256*t+ sNextGuessIndex[SMID]).cast(typeBetterGuessROMAddress);\n\n";

      printSplittedCaseCompatCheck(stage);
    }

    code << "		}\n";
  }

  code << "	}\n";
  code << "}\n";
}

//TODO: REGION: CompletionState Functions
#if (1)
void printNextCompletionStageSimple() {
  //cout << "simple\n";
  //int stage = 15;
  bool availableThisSubstage[NUM_KS_BYTES];

  //bool availableNextStage[NUM_KS_BYTES];
  for (int i=0; i< NUM_KS_BYTES; i++) {
    //update available Bytes
    availableThisSubstage[i]=availableNextStage[i];
  }

  int curSubstage = 0;
  bool continueSearch =true;

  while (continueSearch) {
    curSubstage++;
    continueSearch=false;

    for (int i=0; i< NUM_KS_BYTES; i++) {
      if ((!availableNextStage[i]) && isInferredWithoutSBox(i, availableThisSubstage)) {
        //print i
        availableNextStage[i]=true;
        printCandidates(i, stage > IMPLEMENT_SBOX_AS_SWITCH_TILL_STAGE, 0);
        //code << " implied: " << i << "\n";
        continueSearch=true;
      }
    }

    //update to current state
    for (int i=0; i< NUM_KS_BYTES; i++)
      //update available Bytes
      availableThisSubstage[i]=availableNextStage[i];

    //1 additional sequential step might be used in the complex implication step
    if (curSubstage == MAX_COMPLETE_DEPTH-1)
      return;

    //code << "next stage (may include results of previous stage)\n";
  }
}

bool printNextCompletionStageComplex(int phase) {
  //cout << "complex "<< phase<<"\n";
  //code << "ROM access:";
  for (int i=0; i< NUM_KS_BYTES; i++) {
    if ((availableNextStage[i]==false) && isInferredWithSBoxOnly(i, availableNextStage)) {
      //print the value  //code << " implied complex: " << i << "\n";
      printCandidates(i, stage > IMPLEMENT_SBOX_AS_SWITCH_TILL_STAGE, phase);

      if (phase==1)
        availableNextStage[i]=true;

      return true;
    }
  }

  //code << "no complex found\n";
  return false;
}

void printCompletionMode() {
  stage = NUM_ROUND_BYTES-1;
  code << "CASE (Modes.COMPLETE_FROM8) {\n"
       <<" sStage[SMID].next <== sStage[SMID]+1;\n SWITCH (sStage[SMID]) {\n";

  for (int j=0; j< NUM_KS_BYTES; j++)
    availableThisStage[j]=availableNextStage[j];

  int curCase=0;
  bool complete=false;

  while (!complete) {
    code << "CASE(" << curCase++ << "){\n";

    if (curCase > 1)
      printNextCompletionStageComplex(1);

    printNextCompletionStageSimple();
    complete = !printNextCompletionStageComplex(0);

    for (int i=0; i< NUM_KS_BYTES; i++)
      //update available Bytes
      availableThisStage[i]=availableNextStage[i];

    if (complete) {
      code << "sMode[SMID].next <== Modes.FINAL_CHECK;\n"
           << "sStage[SMID].next <== 0;\n";
    }

    code << "}\n";
  }

  for (int i=0; i< NUM_KS_BYTES; i++)
    if (availableThisStage[i]==false) {
      //if (knownBytes[i]==false)
      cerr << "INCOMPLETE. Cannot find full derivation function for completion step\n";
      exit(2);
    }

  code << "OTHERWISE {}\n"
       << "}\n"
       //close SWITCH
       << "}\n"
       //close COMPLETE_FROM8-case
       << "\n";
}
#endif

void printFinalCheck();

//prints the NextState Function, which describes the transitions of the state machine.
void printNextState() {
  code << "@Override\n"
       << "protected void nextState() {\n"
       << "// Helper.\n"
       << "// Java level stage.\n"
       << "int t;\n";
  code << tBoxDeclare.str();
  code << tBoxInit.str();
  //Init operations
  code << "\n"
       << "// Default address for ROMS.\n"
       << "// To fix:\n"
       << "// Tue 17:24: WARNING: Found one or more variables, e.g. input/output ports of RAM, that lack a default assignment.\n"
       << "// This might trigger the generation of a latch in hardware which should be avoided. Statemachine 'TsowFixManagerStateMachine'\n"
       << "// (in next state function), Types affected: smUInt(8) (signal class: 'MemoryAddress'), smUInt(8) (signal class: 'MemoryAddress')\n"
       << "for (int SMID=0; SMID< NUMBER_OF_SM; SMID++){\n"
       << "romUnSbox[SMID].address <== 0;\n"
       << "romSbox[SMID].address <== 0;\n"
       << "}\n\n"
       << "// Count clock cycles since startup, stop when one SM finished\n"
       << "clkCnt.next <== clkCnt + 1;\n"
       << "for (int SMID = 0; SMID < NUMBER_OF_SM; SMID++) {\n"
       << "IF (sMode[SMID].eq(Modes.SYSTEM_EXIT)) {\n"
       << "clkCnt.next <== clkCnt;\n"
       << "}\n"
       << "}\n"
       << "\n"
       << "// Part of Poll logic\n"
       << "outValid.next <== 0;\n"
       << "byteOut.next <== 0;\n"
       << "IF (pollID.neq(lastPollID).and(~output.stall)) {\n"
       << "        sendingReply.next <== 1; // start sending\n"
       << "        sCounterOut.next <== 0;\n"
       << "        lastPollID.next <== pollID;\n"
       << "}\n"
       << "IF (sendingReply.and(~output.stall)) {\n"
       << "        outValid.next <== 1;\n"
       << "\n"
       << "        sCounterOut.next <== sCounterOut + 1;\n"
       << "        IF (sCounterOut.eq(191)) { // transfer finished\n"
       << "                sendingReply.next <== 0; // stop sending\n"
       << "                sCounterOut.next <== 0;\n"
       << "        }\n"
       << "\n"
       << "        _SWITCH(sCounterOut);\n"
       << "        for (int i = 0; i < 176; i++) { // output KS\n"
       << "                _CASE(i);\n"
       << "                for (int SMID = 0; SMID < NUMBER_OF_SM; SMID++) {\n"
       << "                        IF (sMode[SMID].eq(Modes.SYSTEM_EXIT)) {\n"
       << "                                byteOut.next <== ksC[SMID][i];\n"
       << "                        }\n"
       << "                }\n"
       << "        }\n"
       << "        _CASE(176);\n"
       << "        byteOut.next <== clkCnt.shiftRight(0).cast(typeByte);\n"
       << "        _CASE(177);\n"
       << "        byteOut.next <== clkCnt.shiftRight(8).cast(typeByte);\n"
       << "        _CASE(178);\n"
       << "        byteOut.next <== clkCnt.shiftRight(16).cast(typeByte);\n"
       << "        _CASE(179);\n"
       << "        byteOut.next <== clkCnt.shiftRight(24).cast(typeByte);\n"
       << "        _CASE(180);\n"
       << "        byteOut.next <== clkCnt.shiftRight(32).cast(typeByte);\n"
       << "        _CASE(181);\n"
       << "        byteOut.next <== clkCnt.shiftRight(40).cast(typeByte);\n"
       << "        _CASE(182);\n"
       << "        byteOut.next <== clkCnt.shiftRight(48).cast(typeByte);\n"
       << "        _CASE(183);\n"
       << "        byteOut.next <== clkCnt.shiftRight(56).cast(typeByte);\n"
       << "        _CASE(184); // finished signal\n"
       << "        for (int SMID = 0; SMID < NUMBER_OF_SM; SMID++) {\n"
       << "                IF (sMode[SMID].eq(Modes.SYSTEM_EXIT)) {\n"
       << "                        byteOut.next <== 1;\n"
       << "                }\n"
       << "        }\n"
       << "        // Byte 185ff. show the max reached stage of each SM\n"
       << "        for (int SMID = 0; SMID < NUMBER_OF_SM; SMID++) {\n"
       << "                _CASE(185+SMID);\n"
       << "                byteOut.next <== maxStage[SMID].cast(typeByte);\n"
       << "        }\n"
       << "        _OTHERWISE();\n"
       << "        _END_SWITCH();\n"
       << "}\n"
       << "\n"
       << "// Read ctrl.\n"
       << "sReadDataReady.next <== ~input.empty;\n"
       << "\n";

  if (useEVT)
    code << "DFEsmValue tmp_n0 = constant.value(typeByte, 0);\n"
         << "DFEsmValue tmp_n1 = constant.value(typeByte, 0);\n";

  //read Data State
  code << "	/*\n"
       << "* If we get data from the input, then we consume input\n"
       << "* else we do the computation */\n"
       << "IF (sReadDataReady.eq(true)) {\n" << "// Wire input with vars.\n"
       << "_SWITCH(sCounterIn);\n"
       << "for (int i = 0; i <= 175; ++i) {\n"
       << "    _CASE(i);\n"
       << "    ksD[i].next <== input;\n"
       << "}\n"
       << "_OTHERWISE();\n"
       << "_END_SWITCH();\n"
       << "\n"
       << "// Nope. Go on.\n"
       << "sCounterIn.next <== sCounterIn + 1;\n";
  code << "} ELSE {\n"
       << "for (int SMID=0; SMID< NUMBER_OF_SM; SMID++){\n";

  if (Devel>1)
    code << "		debug.simPrintf(\"I'm %d. TOS %d BOS: %d Mode: %d\\n\", SMID, topOfStack[SMID], bottomOfStack[SMID], sMode[SMID]);\n";

  //Begin of sMode[SMID]-Switch
  code << "SWITCH (sMode[SMID]) {\n"
       << "// Init, get input data.\n";
  //Mode INIT
  code << "CASE (Modes.INIT) {\n"
       << "// Check if reading input is done.\n"
       << "IF(sCounterIn.eq(176)) {\n"
       << "// Done. Start computation.\n"
       << "debug.simPrintf(\":: %d :: -- Reading Input ... Done. \\n\", smID);\n"
       << "\n"
       << "printKeySchedule(ksD);\n"
       << "\n"
       << "debug.simPrintf(\"-- Start Computation. g0 = %d, interval: [%d-%d] \\n\", sNextGuessIndex[SMID], sGuessIndexStart, sGuessIndexEnd);\n"
       << "\n"
       << "sMode[SMID].next <== Modes.GUESS_STAGE_BYTE;\n";

  if (USE_BETTER_GUESS)
    code << "\n"
         << "ksC[SMID][path[0][0]].next <== romBetterGuess[SMID].dataOut;\n"
         << "sNextGuessIndex[SMID].next <== 1;\n"
         << "romBetterGuess[SMID].address <== 1;\n"
         << "} ELSE {\n"
         << "sNextGuessIndex[SMID].next <== this.sGuessIndexStart;\n"
         << "romBetterGuess[SMID].address <== this.sGuessIndexStart;\n";

  code << "	}\n"
       << "if (SMID != 0)\n"
       << "	debug.simPrintf(\"I'm %d, INIT should never happen\\n\",SMID);\n"
       << "	}\n";
  printPathImplicationStages();
  code << push.str();
  //code << pop.str();
  //Mode STATE_POP
  code << "CASE (Modes.STATE_POP) {\n";
  code << "	if (NUMBER_OF_SM==1){\n";
  code << "		doPOP(SMID);\n";
  code << "	}else {\n";
  code << "		//if own stack is empty, try to steal\n";
  code << "		IF (topOfStack [SMID] === bottomOfStack[SMID]){\n";
  code << "			IF (sMode[0] !== Modes.INIT){\n";
  code << "				//Signal: IdleNoResult\n";
  code << "				IF (idleNoResult[SMID] === false)\n";
  code << "				debug.simPrintf(\"I'm %d, have an empty stack and idleNoResult\",SMID);\n";
  code << "				idleNoResult[SMID].next <== true;\n";
  code << "\n";
  code << "				//check: If SMID=0 and all SMs are IdleNoResult, go to Modes.RESULT_NO\n";
  code << "				if (SMID == 0){\n";
  code << "					DFEsmValue allIdle = idleNoResult[1];\n";
  code << "					//the for loop is needed for 3+ SMs. For 2 SMS, no additional hardware is generated.\n";
  code << "					for (int i=2; i <  NUMBER_OF_SM; i++)\n";
  code << "					allIdle &= idleNoResult[i];\n";
  code << "					IF (allIdle === true){\n";
  code << "						sMode[SMID].next <== Modes.RESULT_NO;\n";
  code << "					}\n";
  code << "				}\n";
  code << "\n";
  code << "				int target = (((SMID-1 % NUMBER_OF_SM) + NUMBER_OF_SM) % NUMBER_OF_SM);//1-SMID\n";
  code << "				DFEsmValue targetTOS=topOfStack[target];\n";
  code << "				DFEsmValue targetBOS=bottomOfStack[target];\n";
  code << "				debug.simPrintf(\"I'm %d and  want to steal from %d. targetTOS: %d targetBOS: %d\\n\",SMID, target,targetTOS,targetBOS);\n";
  code << "				IF (targetTOS-targetBOS > 1| (targetTOS-targetBOS === 1 & (sMode[target] !== Modes.STATE_POP)))\n";
  code << "				doSteal(target,SMID);\n";
  code << "			}\n";
  code << "			//else high-priority access for own stack!\n";
  code << "		}ELSE{\n";
  code << "			if( SMID == 1)\n";
  code << "			debug.simPrintf(\"I'm %d, and I pop myself\\n\",SMID);\n";
  code << "			//Do access like with a single SM\n";
  code << "			doPOP(SMID);\n";
  code << "			topOfStack[SMID].next <== topOfStack[SMID]-1;\n";
  code << "		}\n";
  code << "	}\n";
  code << "}";
  printCompletionMode();
  printFinalCheck();
  //Mode RESULT_NO
  code << "CASE (Modes.RESULT_NO) {\n"
       << "debug.simPrintf(\"------- RESULT_NO \\n\");\n\n"
       << "// Set key schedule to zero.\n"
       << "for(int i=0; i<KEY_SIZE; i++) {\n"
       << "	ksC[SMID][i].next <== constant.value(typeByte, 0);\n }\n\n"
       << "printKeySchedule(ksC[SMID]);\n\n"
       << "sCounterIn.next <== 0;\n"
       << "sMode[SMID].next <== Modes.SYSTEM_EXIT;\n	}\n";
  //Mode RESULT_DONE
  code << "	CASE (Modes.RESULT_DONE) {          \n"
       << "		debug.simPrintf(\"------- RESULT_DONE by %d \\n\",SMID);\n"
       << "\n"
       << "		printKeySchedule(ksC[SMID]);\n"
       << "\n"
       << "		DFEsmValue allowedToWrite = sCounterIn.eq(176);//constant.value(typeBoolean, 0);\n"
       << "		for (int i=0; i< SMID;i++)\n"
       << "		allowedToWrite &= (sMode[i] !== Modes.RESULT_DONE);\n"
       << "		IF(allowedToWrite){\n"
       << "			sCounterIn.next <== 0;\n"
       << "			sMode[SMID].next <== Modes.SYSTEM_EXIT;\n"
       << "		}\n"
       << "	}\n";
  //Mode SYSTEM_EXIT
  code << "CASE (Modes.SYSTEM_EXIT) {\n"
       << "// debug.simPrintf(\"ComputationStop here \\n\");\n\n"
       << "}\n";
  //End of sMode[SMID]-Switch, SMID-for Loop, IF and NextState() method
  code << "OTHERWISE {}\n}\n	} \n}\n	}\n";
}
//TODO: REGION: Printer functions for methods and constants.
//These parts do not depend on the chosen path, besides the printer of the path itself methods
#if (1)
void printOutputFunction() {
  code << "@Override\n"
       << "protected void outputFunction() {\n"
       << "input.read <== ~input.empty;\n"
       << "output.valid <== outValid;\n"
       << "output <== byteOut;\n"
       << "}\n"
       << "\n";
}
void printStatePushMethod() {
  code << "private void state_push(int stage, int SMID) {\n"
       << "// Save C.\n"
       << "for(int i=0; i<KEY_SIZE; i++) {\n"
       << "	recS_C[SMID][stage][i].next <== ksC[SMID][i];\n"
       << "}\n";

  if (USE_BETTER_GUESS)
    code << "recS_C[SMID][stage][path[stage][0]].next <== sValueBackupOnPush[SMID];\n";

  code << "// Save current compatible byte.\n"
       << "recS_sNextGuessIndex[SMID][stage].next <== sNextGuessIndex[SMID];\n";

  if (useEVT)
    code << "// Save current n0[SMID].\n"
         << "recS_n0[SMID][stage].next <== sCurrentN0[SMID];\n"
         << "// Save current n1[SMID].\n"
         << "recS_n1[SMID][stage].next <== sCurrentN1[SMID];\n"
         << "\n"
         << "//		debug.simPrintf(\" OOO %d + %d = %d ------- %d + %d = %d \\n\", sCurrentN0[SMID], n0[SMID], sCurrentN0[SMID]+n0[SMID], sCurrentN1[SMID], n1[SMID], sCurrentN1[SMID]+n1[SMID]);\n";

  if (USE_BETTER_GUESS)
    code << "debug.simPrintf(\"Stage %d push - Next guess: %d\\n\", stage, romBetterGuess[SMID].dataOut);\n"
         << "if (stage < 15){\n"
         << "  ksC[SMID][path[stage+1][0]].next <== romBetterGuess[SMID].dataOut;\n"
         << "  romBetterGuess[SMID].address <== 256*(stage+1) + 1;\n"
         << "  }\n";

  code << "}\n"
       << "\n";
}
void printStatePopMethod() {
  code << "private void state_pop(int realstage, int SMID) {\n"
       << "// Recover C.\n"
       << "ksC[SMID][path[realstage][0]].next <== recS_C[SMID][realstage][path[realstage][0]];\n"
       << "// Recover last compatible byte.\n"
       << "sNextGuessIndex[SMID].next <== recS_sNextGuessIndex[SMID][realstage];\n";

  if (useEVT)
    code << "// Recover last n0[SMID].\n"
         << "sCurrentN0[SMID].next <== recS_n0[SMID][realstage];\n"
         << "// Recover last n1[SMID].\n"
         << "sCurrentN1[SMID].next <== recS_n1[SMID][realstage];\n";

  if (USE_BETTER_GUESS)
    code << "romBetterGuess[SMID].address <== realstage * 256 + recS_sNextGuessIndex[SMID][realstage];\n";

  code << "}\n" << "\n";
}

void printStateStealMethod() {
  code << "	private void state_steal(int realstage, int fromSMID, int toSMID) {\n"
       << "		debug.simPrintf(\"stealing....from %d to %d\\n\", fromSMID, toSMID);\n"
       << "		//Todo:: maybe necessary to copy recovery stack?!\n"
       << "		// Recover C.\n"
       << "		for(int i=0; i<KEY_SIZE; i++) {\n"
       << "			ksC[toSMID][i].next <== recS_C[fromSMID][realstage][i];\n"
       << "			//setTo2DArray(toSMID, i, recS_C[fromSMID][realstage][i], ksC);\n"
       << "\n"
       << "		}\n"
       << "		// Recover last compatible byte.\n"
       << "\n"
       << "		sNextGuessIndex[toSMID].next <== recS_sNextGuessIndex[fromSMID][realstage];\n"
       << "		// Recover last n0.\n"
       << "		sCurrentN0[toSMID].next <== recS_n0[fromSMID][realstage];\n"
       << "		// Recover last n1.\n"
       << "		sCurrentN1[toSMID].next <== recS_n1[fromSMID][realstage];\n\n";

  if (USE_BETTER_GUESS)
    code << "romBetterGuess[toSMID].address <== realstage * 256 + recS_sNextGuessIndex[fromSMID][realstage];\n";

  code << "\n"
       << "\n"
       << "		bottomOfStack[toSMID].next <== bottomOfStack[fromSMID];\n"
       << "		bottomOfStack[fromSMID].next <== bottomOfStack[fromSMID]+1;\n"
       << "		topOfStack[toSMID].next <== bottomOfStack[fromSMID];\n"
       << "		idleNoResult[toSMID].next <== false;\n"
       << "	}\n";
}

void printPrintKeySchedule() {
  code << "/**\n"
       << " * Print the key schedule.\n"
       << " * @param b the bytes of key schedule.\n"
       << " */\n"
       << "private void printKeySchedule(DFEsmStateValue[] b){\n"
       << "	for(int i = 0; i < NUM_ROUNDS; i ++){\n"
       << "	    for(int j = i*NUM_BYTES; j < i*NUM_BYTES + NUM_BYTES; j ++){\n"
       << "			debug.simPrintf(\"%02x \\t\", b[j]);\n"
       << "	  }\n"
       << "	    debug.simPrintf(\"\\n\");\n"
       << "	}\n"
       << "	debug.simPrintf(\"\\n\");\n"
       << "}\n"
       << "\n";
}

void printDoPop() {
  code << pop.str();
}

void printDoSteal() {
  code << steal.str();
}

//constants
void printPath() {
  code << "// Fixed path for order of tree computation.\n"
       << "private final int[][] path = {";

  for (int i=0; i< NUM_ROUND_BYTES; i++) {
    for (int j=0; j< path_len[i]; j++) {
      if (j==0)
        code << "{";

      //print values:
      code << " " << (int) path[i][j];

      if (i==NUM_ROUND_BYTES-1 && j==path_len[i]-1)
        code << "}	};";

      else {
        if (j==path_len[i]-1)
          code << "}, ";

        else
          code << ",";
      }
    }

    code << "		//" << i <<"\n";
  }
}
void printSBox() {
  code << "/**\n"
       << " * Substitution box of type int to fit into ROM.\n"
       << " */\n"
       << "private final int[] sbox = {\n"
       << "    // 0    1       2       3       4       5       6       7       8       9       A       B       C       D       E       F\n"
       << "	99, 	124, 	119, 	123, 	242, 	107, 	111, 	197, 	48, 	1, 		103, 	43, 	254, 	215, 	171, 	118, // 0\n"
       << "	202, 	130, 	201, 	125, 	250, 	89, 	71, 	240, 	173, 	212, 	162, 	175, 	156, 	164, 	114, 	192, // 1\n"
       << "	183, 	253, 	147, 	38, 	54, 	63, 	247, 	204, 	52, 	165, 	229, 	241, 	113, 	216, 	49, 	21,  // 2\n"
       << "	4, 		199, 	35, 	195, 	24, 	150, 	5, 		154, 	7, 		18, 	128, 	226, 	235, 	39, 	178, 	117, // 3\n"
       << "	9, 		131, 	44, 	26, 	27, 	110, 	90, 	160, 	82, 	59, 	214, 	179, 	41, 	227, 	47, 	132, // 4\n"
       << "	83, 	209, 	0, 		237, 	32, 	252, 	177, 	91, 	106, 	203, 	190, 	57, 	74, 	76, 	88, 	207, // 5\n"
       << "	208, 	239, 	170, 	251, 	67, 	77, 	51, 	133, 	69, 	249, 	2, 		127, 	80, 	60, 	159, 	168, // 6\n"
       << "	81, 	163, 	64, 	143, 	146, 	157, 	56, 	245, 	188, 	182, 	218, 	33, 	16, 	255, 	243, 	210, // 7\n"
       << "	205, 	12, 	19, 	236, 	95, 	151, 	68, 	23, 	196, 	167, 	126, 	61, 	100, 	93, 	25, 	115, // 8\n"
       << "	96, 	129, 	79, 	220, 	34, 	42, 	144, 	136, 	70, 	238, 	184, 	20, 	222, 	94, 	11, 	219, // 9\n"
       << "	224, 	50, 	58, 	10, 	73, 	6, 		36, 	92, 	194, 	211, 	172, 	98, 	145, 	149, 	228, 	121, // A\n"
       << "	231, 	200, 	55, 	109, 	141, 	213, 	78, 	169, 	108, 	86, 	244, 	234, 	101, 	122, 	174, 	8,   // B\n"
       << "	186, 	120, 	37, 	46, 	28, 	166, 	180, 	198, 	232, 	221, 	116, 	31, 	75, 	189, 	139, 	138, // C\n"
       << "	112, 	62, 	181, 	102, 	72, 	3, 		246, 	14, 	97, 	53, 	87, 	185, 	134, 	193, 	29, 	158, // D\n"
       << "	225, 	248, 	152, 	17, 	105, 	217, 	142, 	148, 	155, 	30, 	135, 	233, 	206, 	85, 	40, 	223, // E\n"
       << "	140, 	161, 	137, 	13, 	191, 	230, 	66, 	104, 	65, 	153, 	45, 	15, 	176, 	84, 	187, 	22   // F\n"
       << "};\n"
       << "\n";
}
void printUnSBox() {
  code << "/**\n"
       << " * Inverse of substitution box of type int to fit into ROM.\n"
       << " */\n"
       << "private final int[] unsbox = {\n"
       << "    // 0    1       2       3       4       5       6       7       8       9       A       B       C       D       E       F\n"
       << "	82,		9,		106,	213,	48,		54,		165,	56,		191,	64,		163,	158,	129,	243,	215,	251,\n"
       << "	124,	227,	57,		130,	155,	47,		255,	135,	52,		142,	67,		68,		196,	222,	233,	203,\n"
       << "	84,		123,	148,	50,		166,	194,	35,		61,		238,	76,		149,	11,		66,		250,	195,	78,\n"
       << "	8,		46,		161,	102,	40,		217,	36,		178,	118,	91,		162,	73,		109,	139,	209,	37,\n"
       << "	114,	248,	246,	100,	134,	104,	152,	22,		212,	164,	92,		204,	93,		101,	182,	146,\n"
       << "	108,	112,	72,		80,		253,	237,	185,	218,	94,		21,		70,		87,		167,	141,	157,	132,\n"
       << "	144,	216,	171,	0,		140,	188,	211,	10,		247,	228,	88,		5,		184,	179,	69,		6,\n"
       << "	208,	44,		30,		143,	202,	63,		15,		2,		193,	175,	189,	3,		1,		19,		138,	107,\n"
       << "	58,		145,	17,		65,		79,		103,	220,	234,	151,	242,	207,	206,	240,	180,	230,	115,\n"
       << "	150,	172,	116,	34,		231,	173,	53,		133,	226,	249,	55,		232,	28,		117,	223,	110,\n"
       << "	71,		241,	26,		113,	29,		41,		197,	137,	111,	183,	98,		14,		170,	24,		190,	27,\n"
       << "	252,	86,		62,		75,		198,	210,	121,	32,		154,	219,	192,	254,	120,	205,	90,		244,\n"
       << "	31,		221,	168,	51,		136,	7,		199,	49,		177,	18,		16,		89,		39,		128,	236,	95,\n"
       << "	96,		81,		127,	169,	25,		181,	74,		13,		45,		229,	122,	159,	147,	201,	156,	239,\n"
       << "	160,	224,	59,		77,		174,	42,		245,	176,	200,	235,	187,	60,		131,	83,		153,	97,\n"
       << "	23,		43,		4,		126,	186,	119,	214,	38,		225,	105,	20,		99,		85,		33,		12,		125\n"
       << "};\n"
       << "\n";
}
void printRcon()	{
  code << "//	Round	constants.\n"
       << "	private	final	int[]	rcon	=	{\n"
       << "	141,	1,		2,		4,		8,		16,		32,		64,		128,	27,		54,		108,	216,	171,	77,		154,\n"
       << "	47,		94,		188,	99,		198,	151,	53,		106,	212,	179,	125,	250,	239,	197,	145,	57,\n"
       << "	114,	228,	211,	189,	97,		194,	159,	37,		74,		148,	51,		102,	204,	131,	29,		58,\n"
       << "	116,	232,	203,	141,	1,		2,		4,		8,		16,		32,		64,		128,	27,		54,		108,	216,\n"
       << "	171,	77,		154,	47,		94,		188,	99,		198,	151,	53,		106,	212,	179,	125,	250,	239,\n"
       << "	197,	145,	57,		114,	228,	211,	189,	97,		194,	159,	37,		74,		148,	51,		102,	204,\n"
       << "	131,	29,		58,		116,	232,	203,	141,	1,		2,		4,		8,		16,		32,		64,		128,	27,\n"
       << "	54,		108,	216,	171,	77,		154,	47,		94,		188,	99,		198,	151,	53,		106,	212,	179,\n"
       << "	125,	250,	239,	197,	145,	57,		114,	228,	211,	189,	97,		194,	159,	37,		74,		148,\n"
       << "	51,		102,	204,	131,	29,		58,		116,	232,	203,	141,	1,		2,		4,		8,		16,		32,\n"
       << "	64,		128,	27,		54,		108,	216,	171,	77,		154,	47,		94,		188,	99,		198,	151,	53,\n"
       << "	106,	212,	179,	125,	250,	239,	197,	145,	57,		114,	228,	211,	189,	97,		194,	159,\n"
       << "	37,		74,		148,	51,		102,	204,	131,	29,		58,		116,	232,	203,	141,	1,		2,		4,\n"
       << "	8,		16,		32,		64,		128,	27,		54,		108,	216,	171,	77,		154,	47,		94,		188,	99,\n"
       << "	198,	151,	53,		106,	212,	179,	125,	250,	239,	197,	145,	57,		114,	228,	211,	189,\n"
       << "	97,		194,	159,	37,		74,		148,	51,		102,	204,	131,	29,		58,		116,	232,	203,	141\n"
       << "	};\n\n";
}
#endif

void generateCode() {
  //Reset stringstreams
  tBoxDeclare.str(std::string());
  tBoxInit.str(std::string());
  push.str(std::string());
  pop.str(std::string());
  isCompat.str(std::string());
  code.str(std::string());
  generateTBox();
  generatePushPopSteal();
  generateIsCompat();
  code << "package locexthwfixevtopt;\n\n";
  printImports();

  if (useEVT)
    code << "\npublic class AESKeyFixEVT_SM extends ManagerStateMachine {\n";

  else
    code << "\npublic class AESKeyFixPAD_SM extends ManagerStateMachine {\n";

  printDeclarations();
  printConstructor();
  printNextState();
  printOutputFunction();
  code << isCompat.str();
  code << isCompatFinal.str();
  printPrintKeySchedule();
  printDoPop();
  printDoSteal();
  printStatePopMethod();
  printStateStealMethod();
  printStatePushMethod();
  printPath();
  printSBox();
  printUnSBox();
  printRcon();
  code << "}\n";
}

//FINAL_CHECK
//requires availableThisStage to be set true for all elements of the static_path as done in printFinalCheck()
void generateFinalCheckHelperMethods(int maxNumberOfAllsummands) {
  int maxNumberOfSummands = pow (2, (FINAL_CHECK_MAX_ADDER_DEPTH-1));
  int currentNumberOfSummands=0;
  int currentNumberOfMethods=0;
  int currentNumberOfAllSummands=0;

  for (int i=0; i< NUM_KS_BYTES; i++) {
    if (!availableThisStage[i]) {
      //printf("%d %d\n", currentNumberOfMethods, currentNumberOfSummands);
      if (currentNumberOfSummands==0) {
        currentNumberOfMethods++;

        if (useEVT) {
          isCompatFinal << "private DFEsmValue isCompatible_evt_final_" << currentNumberOfMethods << "(int v, int SMID) {\n";

        } else {
          isCompatFinal << "private DFEsmValue isCompatible_pad_final" << currentNumberOfMethods << " {\n";
        }
      }

      if (currentNumberOfSummands < maxNumberOfSummands) {
        if (useEVT) {
          isCompatFinal << "DFEsmValue s" << currentNumberOfSummands << " = isCompatible_evt_X(" << i << ", v, SMID);\n";

        } else {
          isCompatFinal << "DFEsmValue s" << currentNumberOfSummands << " = pad(" << i << ");\n";
        }

        currentNumberOfSummands++;
        currentNumberOfAllSummands++;

        if (currentNumberOfAllSummands == maxNumberOfAllsummands) {
          int remainder = maxNumberOfAllsummands-((currentNumberOfMethods-1)*maxNumberOfSummands);

          if (useEVT) {
            generateAdderTreeEVT(remainder, isCompatFinal);

          } else {
            generateAndTreePAD(remainder, isCompatFinal);
          }

          isCompatFinal << "}\n\n";
        }

      } else {
        //if a new method is needed
        if (useEVT) {
          generateAdderTreeEVT(currentNumberOfSummands, isCompatFinal);

        } else {
          generateAndTreePAD(currentNumberOfSummands, isCompatFinal);
        }

        isCompatFinal << "}\n\n";
        currentNumberOfSummands=0;
        i--;
      }
    }
  }
}

//FINAL_CHECK
void printFinalCheck() {
  memset (availableThisStage, 0, NUM_KS_BYTES*sizeof(bool));
  int NumberOfStagesInFinalCheck = 0;
  int numberOfUncheckedPositions = 0;
  code << "CASE (Modes.FINAL_CHECK) {\n";

  for (int stage = 0; stage < 16; stage++) {
    for (int i = 0; i < path_len[stage]; i++) {
      availableThisStage[path[stage][i]]=true;
    }
  }

  for (int i=0; i< NUM_KS_BYTES; i++) {
    if (!availableThisStage[i]) numberOfUncheckedPositions++;
  }

  NumberOfStagesInFinalCheck = (int) ceil (((double) numberOfUncheckedPositions)/ ( pow (2, (FINAL_CHECK_MAX_ADDER_DEPTH-1)) ));
  //determine last sStage to backtrack to if compatibility check fails
  int backtrackTo = 0;
  int curCase = 0;

  //of course, GUESS_STAGE_BYTE stage 18 does not exists. The final compatibility check check is a "virtual" additional GUESS_STAGE_BYTE-Stage.
  for (int stage = 0; stage < 18; stage++) {
    if (stage >= START_SPLIT_AT_STAGE) {
      backtrackTo = curCase - 1;

      if (stage-1 >= START_SPLIT_AT_STAGE)
        backtrackTo--;

      curCase++;
    }

    curCase++;
  }

  code << "SWITCH (sStage[SMID]) {\n";

  for (int i = 0; i < NumberOfStagesInFinalCheck; i++) {
    //TODO: Support for PAD
    code << "CASE (" << i << "){\n";
    code << "tmp_n0 = isCompatible_evt_final_" << i+1 <<"(1, SMID).cast(typeError);\n"
         << "tmp_n1 = isCompatible_evt_final_" << i+1 <<"(0, SMID).cast(typeError);\n"
         << "IF (((sCurrentN0[SMID]+tmp_n0) <= expected_n0).and(((sCurrentN1[SMID]+tmp_n1) <= expected_n1))) {\n"
         << "  sStage[SMID].next <== sStage[SMID] + 1;\n"
         << "  sCurrentN0[SMID].next <== sCurrentN0[SMID]+tmp_n0;\n"
         << "  sCurrentN1[SMID].next <== sCurrentN1[SMID]+tmp_n1;\n";

    if (i == NumberOfStagesInFinalCheck-1)
      code << "sMode[SMID].next <== Modes.RESULT_DONE;\n";

    code << "}\n"
         << "ELSE{\n"
         << "  sMode[SMID].next <== Modes.STATE_POP;\n"
         << "  sStage[SMID].next <== " << backtrackTo << ";\n"
         << "}\n";
    code <<"}\n";
  }

  //close sStage[SMID]-Switch ans FINAL_CHECK-case
  code << "}\n}\n";
  generateFinalCheckHelperMethods(numberOfUncheckedPositions);
  memset (availableThisStage, 0, NUM_KS_BYTES*sizeof(bool));
}

int main(int argc, char * argv[]) {
  
  char kspath[100], bgpath[100];
  if (argc != 4) {
    cout << "Usage: ./dfe_generator number_of_key_schedule p1 p0" << endl;
    return 1;
  }
  sprintf(kspath, "demo-input/key_schedules_decayed/%d.keys_decayed", atoi(argv[1]));

  uint8_t *dm = (uint8_t*) calloc(NUM_KS_BYTES, 1);
  FILE *fh = fopen(kspath, "rb");
  fread(dm, 1, NUM_KS_BYTES, fh);
  fclose(fh);

  betterGuessComparator.setn0ton1(atof(argv[2])*2); // p1
  betterGuessComparator.setn1ton0(atof(argv[3])*2); // p0

  // printKeySchedule(dm);

  if (!USE_STATIC_PATH)
    pathGenerator(dm, path);

  //printPath(path);

  if (USE_BETTER_GUESS) {
    if (USE_STATIC_PATH)
      sprintf(bgpath, "better_guess_%d.conf", atoi(argv[1]));
    else
      sprintf(bgpath, "better_guess_isc_%d.conf", atoi(argv[1]));
    
    createBetterGuess(dm, path);
    fh = fopen(bgpath, "wb");
    for(int i = 0; i < NUM_ROUND_BYTES; i++) {
      for(int j = 0; j < 256; j++) {
        fwrite(&(guessOrder[i][j]), 1, 1, fh);
      }
    }
    fclose(fh);
  }

  // printGuessOrder();

  generateCode();

  ofstream dfeFile("AESKeyFixEVT_SM.maxj");
  dfeFile << "/*" << endl
          << " * Copyright (c) 2013-2017 Paderborn Center for Parallel Computing" << endl
          << " * Copyright (c) 2014-2015 Robert Mittendorf" << endl
          << " * Copyright (c) 2013      Heinrich Riebler" << endl
          << " *" << endl
          << " * Permission is hereby granted, free of charge, to any person obtaining a copy" << endl
          << " * of this software and associated documentation files (the \"Software\"), to deal" << endl
          << " * in the Software without restriction, including without limitation the rights" << endl
          << " * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell" << endl
          << " * copies of the Software, and to permit persons to whom the Software is" << endl
          << " * furnished to do so, subject to the following conditions:" << endl
          << " *" << endl
          << " * The above copyright notice and this permission notice shall be included in" << endl
          << " * all copies or substantial portions of the Software." << endl
          << " *" << endl
          << " * THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR" << endl
          << " * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY," << endl
          << " * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE" << endl
          << " * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER" << endl
          << " * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM," << endl
          << " * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE" << endl
          << " * SOFTWARE." << endl
          << " */" << endl << endl;
  dfeFile << code.rdbuf() << endl;

  // cout << code.rdbuf();

  free(dm);

  return 0;
}
