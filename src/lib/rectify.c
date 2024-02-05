/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include "imprint/allocator.h"
#include <rectify/rectify.h>

void rectifyInit(Rectify* self, RectifyCallbackObject callbackObject, RectifySetup setup, TransmuteState state,
                 StepId stepId)
{
    self->log = setup.log;
    tc_snprintf(self->prefixAuthoritative, 32, "%s/Authoritative", setup.log.constantPrefix);
    Clog authSubLog;
    authSubLog.config = setup.log.config;
    authSubLog.constantPrefix = self->prefixAuthoritative;

    AssentCallbackVtbl assentVtbl = {
        .preTicksFn = callbackObject.vtbl->preAuthoritativeTicksFn,
        .tickFn = callbackObject.vtbl->authoritativeTickFn,
        .deserializeFn = callbackObject.vtbl->authoritativeDeserializeFn,
        .hashFn = callbackObject.vtbl->authoritativeHashFn,
    };
    self->assentCallbackVtbl = assentVtbl;

    const AssentCallbackObject assentCallbackObject = {.vtbl = &self->assentCallbackVtbl, .self = callbackObject.self};

    AssentSetup assentSetup;
    assentSetup.allocator = setup.allocator;
    assentSetup.maxStepOctetSizeForSingleParticipant = setup.maxStepOctetSizeForSingleParticipant;
    assentSetup.maxPlayers = setup.maxPlayerCount;
    assentSetup.log = authSubLog;
    assentSetup.maxTicksPerRead = 20u;

    assentInit(&self->authoritative, assentCallbackObject, assentSetup, state, stepId);

    const SeerCallbackObjectVtbl seerVtbl = {
        .predictionTickFn = callbackObject.vtbl->predictionTickFn,
        .copyFromAuthoritativeFn = callbackObject.vtbl->copyFromAuthoritativeToPredictionFn,
        .postPredictionTicksFn = callbackObject.vtbl->postPredictionTicksFn,
    };

    self->seerCallbackVtbl = seerVtbl;
    const SeerCallbackObject seerCallbackObject = {.vtbl = &self->seerCallbackVtbl, .self = callbackObject.self};

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

    seerInit(&self->predicted, seerCallbackObject, seerSetup, stepId);

    CLOG_C_NOTICE(&self->log, "prepare memory for build composed max player count: %zu", setup.maxPlayerCount)
    self->buildComposedPredictedInput.participantInputs = IMPRINT_ALLOC_TYPE_COUNT(
        setup.allocator, TransmuteParticipantInput, setup.maxPlayerCount);
    self->buildComposedPredictedInput.participantCount = setup.maxPlayerCount;
    self->authoritativeHasBeenCopiedToPrediction = false;
}

void rectifyUpdate(Rectify* self)
{

    /*
    if (targetTickId < self->authoritative.stepId) {
        CLOG_C_ERROR(&self->log,
                     "can not get to a targetTickId that happened before the current authoritative step %04X %04X",
                     targetTickId, self->authoritative.stepId)
        return;
    }
     */

    size_t authoritativeStepCountBeforeUpdate = self->authoritative.authoritativeSteps.stepsCount;
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
                      "next update() %04X (%zu count now and %zd before. max %zu ticks/update)",
                      firstStepId, self->authoritative.authoritativeSteps.stepsCount,
                      authoritativeStepCountBeforeUpdate, self->authoritative.maxTicksPerRead)
    }

    // Everytime we have consumed *all* the knowledge about the truth, we should update our predictions
    // or if we don't have any predictions at all, then it is time to set a prediction
    if (authoritativeStepCountBeforeUpdate > 0 && self->authoritative.authoritativeSteps.stepsCount == 0) {
        CLOG_C_VERBOSE(&self->log,
                       "we have a new authoritative state (truth) at %04X, copy to prediction (which was at %04X) and "
                       "starts predicting our future",
                       self->authoritative.stepId, self->predicted.stepId)
        // seerSetState discards all predicted inputs before the `authoritativeTickId`
        seerAuthoritativeGotNewState(&self->predicted, self->authoritative.stepId);
        self->authoritativeHasBeenCopiedToPrediction = true;
    }

    if (!self->authoritativeHasBeenCopiedToPrediction) {
        CLOG_C_VERBOSE(
            &self->log,
            "we have not given a truth (authoritative state) to be able to predict the future, waiting for that")
        // We are not working on a prediction, so just return
        return;
    }

    if (self->predicted.predictedSteps.stepsCount == 0) {
        // We have no more predictions at this time
        CLOG_C_VERBOSE(&self->log,
                       "we have no predicted steps remaining at %08X (%08X), so can not advance the prediction",
                       self->predicted.stepId, self->authoritative.stepId)
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

ssize_t rectifyAddAuthoritativeStep(Rectify* self, const TransmuteInput* input, StepId tickId)
{
    return assentAddAuthoritativeStep(&self->authoritative, input, tickId);
}

int rectifyAddAuthoritativeStepRaw(Rectify* self, const uint8_t* combinedStep, size_t octetCount, StepId tickId)
{
    return assentAddAuthoritativeStepRaw(&self->authoritative, combinedStep, octetCount, tickId);
}

bool rectifyMustAddPredictedStepThisTick(const Rectify* self)
{
    return seerShouldAddPredictedStepThisTick(&self->predicted);
}

int rectifyAddPredictedStep(Rectify* self, const TransmuteInput* predictedInput, StepId tickId)
{
    if (predictedInput->participantCount > self->buildComposedPredictedInput.participantCount) {
        CLOG_C_SOFT_ERROR(&self->log, "more input than was prepared for predictedInput:%zu, buildComposed:%zu",
                          predictedInput->participantCount, self->buildComposedPredictedInput.participantCount)
        return -1;
    }

    self->buildComposedPredictedInput = self->authoritative.lastTransmuteInput;
    const TransmuteInput* lastAuthoritativeInput = &self->authoritative.lastTransmuteInput;
    for (size_t i = 0; i < lastAuthoritativeInput->participantCount; ++i) {
        const TransmuteParticipantInput* authoritativeParticipant = &lastAuthoritativeInput->participantInputs[i];
        TransmuteParticipantInput* buildTarget = &self->buildComposedPredictedInput.participantInputs[i];
        int foundInPredicted = transmuteInputFindParticipantId(predictedInput, authoritativeParticipant->participantId);
        if (foundInPredicted < 0) {
            // Set zero input for remote participants
            CLOG_C_VERBOSE(&self->log, "set null for remote participants")
            buildTarget->octetSize = 0;
            buildTarget->input = 0;
            buildTarget->inputType = TransmuteParticipantInputTypeNoInputInTime;
        } else {
            TransmuteParticipantInput* participantInput = &predictedInput->participantInputs[foundInPredicted];
            if (participantInput->input == 0 || participantInput->octetSize == 0) {
                CLOG_C_ERROR(&self->log, "can not set empty participant input for prediction")
            }
            CLOG_ASSERT(participantInput->inputType == TransmuteParticipantInputTypeNormal,
                        "local participants must be of normal type")
            buildTarget->octetSize = participantInput->octetSize;
            CLOG_ASSERT(participantInput->input != 0,
                        "local participants must have a valid input pointer")
            buildTarget->input = participantInput->input;
            buildTarget->inputType = TransmuteParticipantInputTypeNormal;
        }
    }

    /* TODO: Check if this code is correct, but ignore it for now
        for (size_t i = 0; i < predictedInput->participantCount; ++i) {
            const TransmuteParticipantInput* predictedParticipant = &predictedInput->participantInputs[i];
            const int foundInAuthoritative = transmuteInputFindParticipantId(&self->authoritative.lastTransmuteInput,
                                                                             predictedParticipant->participantId);
            if (foundInAuthoritative < 0) {
                // It was not found in authoritative, we predict that we have joined then
                size_t index = self->buildComposedPredictedInput.participantCount++;
                TransmuteParticipantInput* newParticipantInput = &self->buildComposedPredictedInput
                                                                      .participantInputs[index];
                newParticipantInput->input = predictedParticipant->input;
                newParticipantInput->octetSize = predictedParticipant->octetSize;
                newParticipantInput->participantId = predictedParticipant->participantId;
                newParticipantInput->inputType = predictedParticipant->inputType;
            }
        }

    */
    if (self->buildComposedPredictedInput.participantCount == 0) {
        CLOG_C_NOTICE(&self->log, "can not predict yet, need to get some authoritative first")
        return 0;
    }

    return seerAddPredictedStep(&self->predicted, &self->buildComposedPredictedInput, tickId);
}
