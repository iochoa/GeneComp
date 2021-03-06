//
//  read_decompression.c
//  XC_s2fastqIO
//
//  Created by Mikel Hernaez on 11/13/14.
//  Copyright (c) 2014 Mikel Hernaez. All rights reserved.
//

#include "read_compression.h"
#include <ctype.h>
#include <cinttypes>
#include <iostream>

#define DEBUG false
#define VERIFY false

using namespace std;
//**************************************************************//
//                                                              //
//                  STORE REFERENCE IN MEMORY                   //
//                                                              //
//**************************************************************//
int store_reference_in_memory(FILE* refFile){
    uint32_t letterCount, endoffile = 1;
    char header[1024];
    char buf[1024];
    
    reference = (char *) malloc(MAX_BP_CHR*sizeof(char));
    
    // ******* Read and Store Reference****** //
    letterCount = 0;
    
    // Remove the first header
    if (ftell(refFile) == 0) {
      fgets(header, sizeof(header), refFile);
    }
    
    while (fgets(buf, 1024, refFile)) {
      if (buf[0] == '>' || reference[0] == '@') {
        endoffile = 0;
        break;
      }
      for (int i = 0; i < 1024; i++) {
        if (buf[i] == '\n') break;
        reference[letterCount] = toupper(buf[i]);
        letterCount++;
      }
    }

    reference[letterCount] = '\0';

    reference = (char *) realloc(reference, letterCount + 1);
    
    if (endoffile)
        return END_GENOME_FLAG;
    
    return letterCount;
    
}


/************************
 * Decompress the read
 **********************/
uint32_t decompress_read(Arithmetic_stream as, sam_block sb, uint8_t chr_change, struct sam_line_t *sline){
    
    int invFlag, tempP, k;
    uint32_t readLen;
    uint16_t maskedReadVal;
    
    read_models models = sb->reads->models;
    
    
    // Decompress read length (4 uint8)
    readLen = 0;
    for (k=0;k<4;k++) {
        maskedReadVal = decompress_uint8t(as, models->rlength[k]);
        readLen = readLen | maskedReadVal<<(k*8);
    }
    
    // Decompress the read
    tempP = decompress_pos(as, models->pos, models->pos_alpha, chr_change, &sline->pos);
    
    invFlag = decompress_flag(as, models->flag, &sline->flag);
    
    reconstruct_read(as, models, tempP, invFlag, sline->read, readLen, chr_change, sline->cigar);
    
    return invFlag;
}

uint32_t decompress_cigar(Arithmetic_stream as, sam_block sb, struct sam_line_t *sline)
{
    uint8_t cigarFlags;
    int cigarCtr,cigarLen;

    read_models models = sb->reads->models;


    // Decompress cigarFlags
    cigarFlags = decompress_uint8t(as,models->cigarFlags[0]);

    if(cigarFlags==0) {
        //The cigar is not the recCigar.
        cigarLen = decompress_uint8t(as,models->cigar[0]);
        for(cigarCtr = 0; cigarCtr<cigarLen; cigarCtr++)
            sline->cigar[cigarCtr] = decompress_uint8t(as,models->cigar[0]);

        sline->cigar[cigarCtr] = '\0';
    }

    return 1;
}

/***********************
 * Decompress the Flag
 **********************/
uint32_t decompress_flag(Arithmetic_stream a, stream_model *F, uint32_t *flag){
    
    
    // In this case we are just compressing the binary information of whether the
    // read is in reverse or not. we use F[0] as there is no context for the flag.
    uint16_t x;
    // Read the value from the Arithmetic Stream
    x = read_value_from_as(a, F[0]);
    
    // Update model
    update_model(F[0], x);
    
    *flag = x;
    
    x = x & 16;
    x >>= 4;
    
    return x;
    
}

/***********************************
 * Decompress the Alphabet of Position
 ***********************************/
uint32_t decompress_pos_alpha(Arithmetic_stream as, stream_model *PA){
    
    uint32_t Byte = 0, x = 0;
    
    // we encode byte per byte i.e. x = [B0 B1 B2 B3]
    
    // Read B0 from the Arithmetic Stream using the alphabet model
    Byte = read_value_from_as(as, PA[0]);
    // Update model
    update_model(PA[0], Byte);
    // Reconstruct B0
    x |= (Byte << 24);
    
    // Read B1 from the Arithmetic Stream using the alphabet model
    Byte = read_value_from_as(as, PA[1]);
    // Update model
    update_model(PA[1], Byte);
    // Reconstruct B1
    x |= (Byte << 16);
    
    // Send B2 to the Arithmetic Stream using the alphabet model
    Byte = read_value_from_as(as, PA[2]);
    // Update model
    update_model(PA[2], Byte);
    // Reconstruct B2
    x |= (Byte << 8);
    
    // Send B3 to the Arithmetic Stream using the alphabet model
    Byte = read_value_from_as(as, PA[3]);
    // Update model
    update_model(PA[3], Byte);
    // Reconstruct B3
    x |= (Byte);
    
    return x;
    
    
}

/**************************
 * Decompress the Position
 *************************/
uint32_t decompress_pos(Arithmetic_stream as, stream_model *P, stream_model *PA, uint8_t chr_change, uint32_t *p){
    
    static uint32_t prevPos = 0;
    
    int32_t pos, alphaMapX = 0, x = 0;
    
    enum {SMALL_STEP = 0, BIG_STEP = 1};
    
    // Check if we are changing chromosomes.
    if (chr_change)
        prevPos = 0;
    
    // Read from the AS and get the position
    alphaMapX = read_value_from_as(as, P[0]);
    
    x = P[0]->alphabet[alphaMapX];
    
    // Update the statistics
    update_model(P[0], alphaMapX);
    
    // A new value of pos
    if (x == -1) {
        
        // Read from the AS to get the unknown alphabet letter alpha
        x = decompress_pos_alpha(as, PA);
        
        // Update the statistics of the alphabet for x
        P[0]->alphaExist[x] = 1;
        P[0]->alphaMap[x] = P[0]->alphabetCard; // We reserve the bin 0 for the new symbol flag
        P[0]->alphabet[P[0]->alphabetCard] = x;
        
        update_model(P[0], P[0]->alphabetCard++);
    }
    
    // Decompress the position diference (+ 1 to reserve 0 for new symbols)
    pos = prevPos + x - 1;
    
    *p = pos;
    
    prevPos = pos;
    
    return pos;
}

/****************************
 * Decompress the match
 *****************************/
uint32_t decompress_match(Arithmetic_stream a, stream_model *M, uint32_t P){
    
    uint32_t ctx = 0;
    static uint8_t  prevM = 0;
    
    uint8_t match = 0;
    
    
    // Compute Context
    P = (P != 1)? 0:1;
    //prevP = (prevP > READ_LENGTH)? READ_LENGTH:prevP;
    //prevP = (prevP > READ_LENGTH/4)? READ_LENGTH:prevP;
    
    ctx = (P << 1) | prevM;
    
    //ctx = 0;
    
    // Read the value from the Arithmetic Stream
    match = read_value_from_as(a, M[ctx]);
    
    // Update model
    update_model(M[ctx], match);
    
    prevM = match;
    
    return match;
}

/*************************
 * Decompress the snps
 *************************/
uint32_t decompress_snps(Arithmetic_stream a, stream_model *S){
    
    uint8_t numSnps = 0;
    // No context is used for the numSnps for the moment.
    
    // Send the value to the Arithmetic Stream
    numSnps = read_value_from_as(a, S[0]);
    
    // Update model
    update_model(S[0], numSnps);
    
    return numSnps;
    
}


/********************************
 * Decompress the indels
 *******************************/
uint32_t decompress_indels(Arithmetic_stream a, stream_model *I){
    
    uint8_t numIndels = 0;
    // No context is used for the numIndels for the moment.
    
    // Read the value from the Arithmetic Stream
    numIndels = read_value_from_as(a, I[0]);
    
    // Update model
    update_model(I[0], numIndels);
    
    return numIndels;
    
}

/*******************************
 * Decompress the variations
 ********************************/
uint32_t decompress_var(Arithmetic_stream a, stream_model *v,  uint32_t prevPos, uint32_t flag){
    
    uint32_t ctx = 0;
    uint32_t pos = 0;
    
    //flag = 0;
    ctx = prevPos << 1 | flag;
    
    // Read the value from the Arithmetic Stream
    pos = read_value_from_as(a, v[ctx]);
    
    // Update model
    update_model(v[ctx], pos);
    
    return pos;
    
}

/*****************************************
 * Decompress the chars
 ******************************************/
uint8_t decompress_chars(Arithmetic_stream a, stream_model *c, enum BASEPAIR ref){
    
    uint32_t target = 0;
    
    // Read the value from the Arithmetic Stream
    target = read_value_from_as(a, c[ref]);
    
    // Update model
    update_model(c[ref], target);
    
    return basepair2char((enum BASEPAIR)target);
    
}

uint32_t argmin(uint32_t *arr, uint32_t len) {
  uint32_t min = UINT32_MAX;
  uint32_t index = 0;
  for (uint32_t i = 0; i < len; i++) {
    if (min > arr[i]) {
      min = arr[i]; 
      index = i;
    }
  }
  return index;
}

static void fill_target(char *ref, char *target, uint32_t prev_pos, uint32_t cur_pos, uint32_t *ref_pos, uint32_t *Dels, uint32_t *dels_pos, uint32_t numDels) {

  uint32_t ref_start = *ref_pos;
  while (*dels_pos < numDels && *ref_pos >= Dels[*dels_pos]) {
    if (DEBUG) printf("DELETE %d\n", Dels[*dels_pos]);
    (*ref_pos)++;
    (*dels_pos)++;
  }
  for (int i = prev_pos; i < cur_pos; i++) {
    target[i] = ref[*ref_pos];
    if (VERIFY) assert(isalpha(target[i]));
    (*ref_pos)++;
    while (*dels_pos < numDels && *ref_pos >= Dels[*dels_pos]) {
      if (DEBUG) printf("DELETE %d\n", Dels[*dels_pos]);
      (*ref_pos)++;
      (*dels_pos)++;
    }
  }
  //if (DEBUG) printf("MATCH [%d, %d), ref [%d, %d)\n", prev_pos, cur_pos, ref_start, *ref_pos);
}

static void handle_insertions(char *ref, char *target, uint32_t *start_copy, int cur_pos, uint32_t *ref_pos, struct ins *Insers, uint32_t *ins_pos, uint32_t numIns, uint32_t *Dels, uint32_t *dels_pos, uint32_t numDels) {
  while (*ins_pos < numIns && Insers[*ins_pos].pos < cur_pos) {
    fill_target(ref, target, *start_copy, Insers[*ins_pos].pos, ref_pos, Dels, dels_pos, numDels);
    if (DEBUG) printf("Insert %c at %d\n", basepair2char(Insers[*ins_pos].targetChar), Insers[*ins_pos].pos);
    target[Insers[*ins_pos].pos] = basepair2char(Insers[*ins_pos].targetChar);
    if (VERIFY) assert(isalpha(target[Insers[*ins_pos].pos]));
    *start_copy = Insers[*ins_pos].pos + 1;
    (*ins_pos)++;
  }

}

/*****************************************
 * Reconstruct the read
 ******************************************/
uint32_t reconstruct_read(Arithmetic_stream as, read_models models, uint32_t pos, uint8_t invFlag, char *read, uint32_t readLen, uint8_t chr_change, char *recCigar){
    
    unsigned int numIns = 0, numDels = 0, numSnps = 0, delPos = 0, ctrPos = 0, snpPos = 0, insPos = 0;
    uint32_t prev_pos = 0, delta = 0, deltaPos = 0;

    
    uint32_t Dels[MAX_READ_LENGTH];
    ins Insers[MAX_READ_LENGTH];
    snp SNPs[MAX_READ_LENGTH];
    
    static uint32_t prevPos = 0;
    
    unsigned int ctrDels = 0, readCtr = 0;
    int i = 0;
    uint8_t tmpChar;
    unsigned int returnVal;
    
    uint8_t match;
    
    enum BASEPAIR refbp;
    
    read[models->read_length] = '\0';
    // reset prevPos if the chromosome changed
    if (chr_change == 1) {
      prevPos = 0;
    }

    if (pos < prevPos){
        deltaPos = pos;
    }else{
        deltaPos = pos - prevPos + 1;// deltaPos is 1-based.
    }
    prevPos = pos;
    
    match = decompress_match(as, models->match, deltaPos);

    // cumsumP is equal to pos
    cumsumP = pos;
    
    // If there is a match, reconstruct the read
    if (match) {
      for (ctrPos=0; ctrPos<models->read_length; ctrPos++)
        read[readCtr++] = reference[pos + ctrPos - 1];
      reconstructCigar(Dels, Insers, 0, 0, readLen, recCigar);
      return 1;
    }
    // There is no match, retreive the edits
    else{
        numSnps = decompress_snps(as, models->snps);
        numDels = 0;
        numIns = 0;
        
        if (numSnps == 0){
            numSnps = decompress_indels(as, models->indels);
            numDels = decompress_indels(as, models->indels);
            numIns = decompress_indels(as, models->indels);
        } 
    }
    
    if (VERIFY) assert(numDels == numIns);
    if (DEBUG) printf("snps %d, dels %d, ins %d\n", numSnps, numDels, numIns);

    // Reconstruct the read
    
    // Deletions
    prev_pos = 0;
    for (ctrDels = 0; ctrDels < numDels; ctrDels++){
        delPos = decompress_var(as, models->var, prev_pos, invFlag);
        //if (DEBUG) printf("Delete ref at %d\n", delPos + prev_pos);
        Dels[ctrDels] = delPos + prev_pos;
        prev_pos += delPos;
    }

    // Insertions
    prev_pos = 0;
    for (i = 0; i < numIns; i++){
        insPos = decompress_var(as, models->var, prev_pos, invFlag);
        Insers[i].pos = prev_pos + insPos;
        Insers[i].targetChar = char2basepair(decompress_chars(as, models->chars, O));
        //if (DEBUG) printf("Insert %c at offset: %d, prev_pos %d\n", basepair2char(Insers[i].targetChar), insPos, prev_pos);
        prev_pos += insPos;
    }


    uint32_t ins_pos = 0, dels_pos = 0;
    uint32_t start_copy = 0, ref_pos = 0;

    while (numDels > 0 && dels_pos < numDels && Dels[dels_pos] == ref_pos) {
      if (DEBUG) printf("DELETE %d\n", Dels[dels_pos]);
      (ref_pos)++; 
      (dels_pos)++;
    }

    // SNPS

    prev_pos = 0;
    for (i = 0; i < numSnps; i++) {
        
        if (VERIFY) assert(prev_pos <= models->read_length);

        delta = compute_delta_to_first_snp(prev_pos + 1, models->read_length);
        delta = (delta << BITS_DELTA);

        snpPos = decompress_var(as, models->var, delta + prev_pos, invFlag);
        snpInRef[cumsumP - 1 + prev_pos + snpPos] = 1;
        SNPs[i].pos = prev_pos + snpPos;

        handle_insertions(&(reference[pos-1]), read, &start_copy, SNPs[i].pos, &ref_pos, Insers, &ins_pos, numIns, Dels, &dels_pos, numDels);
        fill_target(&(reference[pos - 1]), read, start_copy, SNPs[i].pos, &ref_pos, Dels, &dels_pos, numDels);
        start_copy = SNPs[i].pos + 1;

        refbp = char2basepair( reference[pos + ref_pos - 1] );
        SNPs[i].refChar = refbp;
        SNPs[i].targetChar = char2basepair(decompress_chars(as, models->chars, refbp));
        read[SNPs[i].pos] = basepair2char(SNPs[i].targetChar);
        if (VERIFY) assert(isalpha(read[SNPs[i].pos]));
        if (DEBUG) printf("Replace %c with %c at %d\n", basepair2char(SNPs[i].refChar), basepair2char(SNPs[i].targetChar), SNPs[i].pos);
        ref_pos++;
        prev_pos += snpPos;


    }
    handle_insertions(&(reference[pos-1]), read, &start_copy, models->read_length, &ref_pos, Insers, &ins_pos, numIns, Dels, &dels_pos, numDels);

    fill_target(&(reference[pos - 1]), read, start_copy, models->read_length, &ref_pos, Dels, &dels_pos, numDels);

    absolute_to_relative(Dels, numDels, Insers, numIns);
    reconstructCigar(Dels, Insers, numDels, numIns, readLen, recCigar);

    if (invFlag == 0) returnVal = 0;
    else if (invFlag == 1) returnVal = 1;
    else returnVal = 2;
    return returnVal;
}
