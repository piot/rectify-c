/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <rectify/rectify.h>

void rectifyInit(Rectify* self, TransmuteVm authoritativeVm, TransmuteVm predictVm, RectifySetup setup, TransmuteState state, StepId stepId)
{
    tc_snprintf(self->prefixAuthoritative, 32, "%s/Authoritative", setup.log.constantPrefix);
    Clog authSubLog;
    authSubLog.config = setup.log.config;
    authSubLog.constantPrefix = self->prefixAuthoritative;


    AssentSetup assentSetup;

    assentSetup.allocator = setup.allocator;
    assentSetup.maxStepOctetSizeForSingleParticipant = setup.maxStepOctetSizeForSingleParticipant;
    assentSetup.maxPlayers = setup.maxPlayerCount;
    assentSetup.log = authSubLog;
    assentSetup.maxTicksPerRead = 20;

    assentInit(&self->authoritative, authoritativeVm, assentSetup, state, stepId);

    tc_snprintf(self->prefixPredicted, 32, "%s/Predict", setup.log.constantPrefix);
    Clog seerSubLog;
    seerSubLog.config = setup.log.config;
    seerSubLog.constantPrefix = self->prefixPredicted;


    SeerSetup seerSetup;
    seerSetup.maxPlayers = setup.maxPlayerCount;
    seerSetup.maxStepOctetSizeForSingleParticipant = setup.maxStepOctetSizeForSingleParticipant;
    seerSetup.allocator = setup.allocator;
    seerSetup.maxTicksFromAuthoritative = setup.maxTicksFromAuthoritative;
    seerSetup.log = seerSubLog;

    seerInit(&self->predicted, predictVm, seerSetup, state, stepId);

    self->log = setup.log;
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

    bool hadAuthoritativeStepsBeforeUpdate = self->authoritative.authoritativeSteps.stepsCount > 0;
    // Try to advance the authoritative steps as far as possible
    assentUpdate(&self->authoritative);


    if (self->authoritative.authoritativeSteps.stepsCount != 0) {
        StepId firstStepId;
        bool didHaveAtLeastOneStep = nbsStepsPeek(&self->authoritative.authoritativeSteps, &firstStepId);
        if (!didHaveAtLeastOneStep) {
            CLOG_C_ERROR(&self->log, "nbsStepsPeek should have returned true")
        }

        CLOG_C_NOTICE(&self->log,
                      "still trying to catch up to a complete authoritative state, couldn't advance through all steps "
                      "this update, hopefully catching up "
                      "next update() %04X (%zu count)",
                      firstStepId, self->authoritative.authoritativeSteps.stepsCount)
    }

    // Everytime we have consumed *all* the knowledge about the truth, we should update our predictions
    // or if we don't have any predictions at all, then it is time to set a prediction
    if (hadAuthoritativeStepsBeforeUpdate && self->authoritative.authoritativeSteps.stepsCount == 0) {
        StepId authoritativeTickId;
        TransmuteState authoritativeTransmuteState = assentGetState(&self->authoritative, &authoritativeTickId);
        CLOG_C_VERBOSE(&self->log, "we have a new truth at %04X, set it to seer (which was at %04X) and starts predicting our future", authoritativeTickId, self->predicted.stepId)
        // seerSetState discards all predicted inputs before the `authoritativeTickId`
        seerSetState(&self->predicted, authoritativeTransmuteState, authoritativeTickId);
    }

    if (!self->predicted.transmuteVm.initialStateIsSet) {
        CLOG_C_VERBOSE(&self->log, "we have not established a truth, waiting for that")
        // We are not working on a prediction, so just return
        return;
    }

    if (self->predicted.predictedSteps.stepsCount == 0) {
        // We have no more predictions at this time
        CLOG_C_VERBOSE(&self->log, "we have no predicted steps remaining at %04X (%04X), so can not advance the prediction", self->predicted.stepId, self->authoritative.stepId)
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
    CLOG_C_VERBOSE(&self->log, "we can ask seer to predict the future from %04X", self->predicted.stepId)
    seerUpdate(&self->predicted);
    CLOG_C_VERBOSE(&self->log, "new prediction from seer at %04X", self->predicted.stepId)
}

int rectifyAddAuthoritativeStep(Rectify* self, const TransmuteInput* input, StepId tickId)
{
    return assentAddAuthoritativeStep(&self->authoritative, input, tickId);
}

int rectifyAddAuthoritativeStepRaw(Rectify* self, const uint8_t* combinedStep, size_t octetCount, StepId tickId)
{
    return assentAddAuthoritativeStepRaw(&self->authoritative, combinedStep, octetCount, tickId);
}

int rectifyAddPredictedStep(Rectify* self, const TransmuteInput* input, StepId tickId)
{
    return seerAddPredictedStep(&self->predicted, input, tickId);
}

int rectifyAddPredictedStepRaw(Rectify* self, const uint8_t* combinedStep, size_t octetCount, StepId tickId)
{
    return seerAddPredictedStepRaw(&self->predicted, combinedStep, octetCount, tickId);
}
