/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef RECTIFY_H
#define RECTIFY_H

#include <assent/assent.h>
#include <seer/seer.h>

typedef struct Rectify {
    Seer predicted;
    Assent authoritative;
    NbsSteps authoritativeSteps;
    Clog log;
    size_t maxPredictionTicksFromAuthoritative;
    char prefixAuthoritative[32];
    char prefixPredicted[32];
} Rectify;

void rectifyInit(Rectify* self, TransmuteVm authoritativeVm, TransmuteVm predictVm, struct ImprintAllocator* allocator,
                 size_t maxInputOctetSize, size_t maxPlayerCount, Clog log);

void rectifyUpdate(Rectify* self);
void rectifySetAuthoritativeState(Rectify* self, TransmuteState authoritativeState, StepId tickId);

#endif
