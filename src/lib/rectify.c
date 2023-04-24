/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <rectify/rectify.h>

void rectifyInit(Rectify* self, TransmuteVm authoritativeVm, TransmuteVm predictVm, struct ImprintAllocator* allocator,
                 size_t maxInputOctetSize, size_t maxPlayerCount, Clog log)
{
    tc_snprintf(self->prefixAuthoritative, 32, "%s/Authoritative", log.constantPrefix);
    Clog authSubLog;
    authSubLog.config = log.config;
    authSubLog.constantPrefix = self->prefixAuthoritative;

    assentInit(&self->authoritative, authoritativeVm, allocator, maxInputOctetSize, maxPlayerCount, authSubLog);

    tc_snprintf(self->prefixPredicted, 32, "%s/Predict", log.constantPrefix);
    Clog seerSubLog;
    seerSubLog.config = log.config;
    seerSubLog.constantPrefix = self->prefixPredicted;

    seerInit(&self->predicted, predictVm, allocator, maxInputOctetSize, maxPlayerCount, seerSubLog);

    nbsStepsInit(&self->authoritativeSteps, allocator, maxInputOctetSize);
    self->maxPredictionTicksFromAuthoritative = 20;
    self->log = log;
}

void rectifyUpdate(Rectify* self)
{
    if (!transmuteVmHasState(&self->authoritative.transmuteVm)) {
        CLOG_C_NOTICE(&self->log, "authoritative state has not been set yet")
        return;
    }

    /*
    if (targetTickId < self->authoritative.stepId) {
        CLOG_C_ERROR(&self->log,
                     "can not get to a targetTickId that happened before the current authoritative step %04X %04X",
                     targetTickId, self->authoritative.stepId)
        return;
    }
     */

    // Try to advance the authoritative steps as far as possible
    assentUpdate(&self->authoritative, &self->authoritativeSteps);

    if (self->authoritativeSteps.stepsCount != 0) {
        StepId firstStepId;
        bool didHaveAtLeastOneStep = nbsStepsPeek(&self->authoritativeSteps, &firstStepId);
        if (!didHaveAtLeastOneStep) {
            CLOG_C_ERROR(&self->log, "nbsStepsPeek should have returned true")
        }

        CLOG_C_NOTICE(&self->log,
                      "still trying to catch up to a complete authoritative state, couldn't advance through all steps "
                      "this update, hopefully catching up "
                      "next update() %04X (%zu count)",
                      firstStepId, self->authoritativeSteps.stepsCount)
    }

    // Everytime we have consumed *all* the knowledge about the truth, we should update our predictions
    // or if we don't have any predictions at all, then it is time to set a prediction
    if (self->authoritativeSteps.stepsCount == 0) {
        StepId authoritativeTickId;
        TransmuteState authoritativeTransmuteState = assentGetState(&self->authoritative, &authoritativeTickId);
        // seerSetState discards all predicted inputs before the `authoritativeTickId`
        seerSetState(&self->predicted, authoritativeTransmuteState, authoritativeTickId);
    }

    if (!self->predicted.transmuteVm.initialStateIsSet) {
        // We are not working on a prediction, so just return
        return;
    }

    if (self->predicted.predictedSteps.count == 0) {
        // We have no more predictions at this time
        return;
    }

    /*
        StepId targetTickId;
        bool hadPredictedStep = seerPredictedStepsLastStepId(&self->predicted.predictedSteps, &targetTickId);

        // We are here because we have advanced at least as far as we could with authoritative steps
        if (!hadPredictedStep || targetTickId <= self->authoritative.stepId) {
            CLOG_C_VERBOSE(&self->log,
                           "we should not predict, the requested is either already at authoritative or before auth: %04X
       " "requested: %04X", self->authoritative.stepId, targetTickId) return;
        }
        */

    // We need to continue our ongoing prediction, up to the number of predicted inputs or the maximum prediction ticks
    // that are allowed
    seerUpdate(&self->predicted);
}

void rectifySetAuthoritativeState(Rectify* self, TransmuteState authoritativeState, StepId tickId)
{
    assentSetState(&self->authoritative, &authoritativeState, tickId);
}
