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
    Clog log;
    char prefixAuthoritative[32];
    char prefixPredicted[32];
} Rectify;

typedef struct RectifySetup {
    struct ImprintAllocator* allocator;
    size_t maxInputOctetSize;
    size_t maxPlayerCount;
    Clog log;
} RectifySetup;

void rectifyInit(Rectify* self, TransmuteVm authoritativeVm, TransmuteVm predictVm, RectifySetup setup, TransmuteState state, StepId stepId);
void rectifyUpdate(Rectify* self);
int rectifyAddAuthoritativeStep(Rectify* self, const TransmuteInput* input, StepId tickId);
int rectifyAddPredictedStep(Rectify* self, const TransmuteInput* input, StepId tickId);

#endif
