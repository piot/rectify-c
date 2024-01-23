/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include "rectify/rectify.h"
#include "utest.h"
#include <clog/clog.h>
#include <imprint/default_setup.h>
#include <nimble-steps-serialize/in_serialize.h>
#include <nimble-steps-serialize/out_serialize.h>
#include <nimble-steps/steps.h>
#include <seer/seer.h>

typedef struct AppSpecificState {
    int x;
    int time;
} AppSpecificState;

typedef struct AppSpecificParticipantInput {
    int horizontalAxis;
} AppSpecificParticipantInput;

typedef struct AppSpecificVm {
    AppSpecificState appSpecificState;
} AppSpecificVm;

void appSpecificTick(void* _self, const TransmuteInput* input)
{
    AppSpecificVm* self = (AppSpecificVm*) _self;

    if (input->participantCount > 0) {
        const AppSpecificParticipantInput* appSpecificInput = (AppSpecificParticipantInput*) input->participantInputs[0]
                                                                  .input;
        self->appSpecificState.x += appSpecificInput->horizontalAxis;
        CLOG_DEBUG("app: tick with input %d, x:%d", appSpecificInput->horizontalAxis,
                   self->appSpecificState.x)
    } else {
        CLOG_DEBUG("app: tick with no input")
    }

    self->appSpecificState.time++;
}

TransmuteState appSpecificGetState(const void* _self)
{
    TransmuteState state;

    AppSpecificVm* self = (AppSpecificVm*) _self;

    state.octetSize = sizeof(AppSpecificState);
    state.state = (const void*) &self->appSpecificState;

    return state;
}

void appSpecificSetState(void* _self, const TransmuteState* state)
{
    AppSpecificVm* self = (AppSpecificVm*) _self;

    self->appSpecificState = *(AppSpecificState*) state->state;
}

int appSpecificStateToString(void* _self, const TransmuteState* state, char* target, size_t maxTargetOctetSize)
{
    (void) _self;

    const AppSpecificState* appState = (AppSpecificState*) state->state;
    return tc_snprintf(target, maxTargetOctetSize, "state: time: %d pos.x: %d", appState->time, appState->x);
}

int appSpecificInputToString(void* _self, const TransmuteParticipantInput* input, char* target,
                             size_t maxTargetOctetSize)
{
    (void) _self;
    const AppSpecificParticipantInput* participantInput = (AppSpecificParticipantInput*) input->input;
    return tc_snprintf(target, maxTargetOctetSize, "input: horizontalAxis: %d", participantInput->horizontalAxis);
}

typedef struct AppSpecificCallback {
    TransmuteVm* authoritative;
    TransmuteVm* predicted;
    TransmuteState cachedState;
} AppSpecificCallback;

void rectifyAuthoritativeDeserialize(void* _self, const TransmuteState* state)
{
    AppSpecificCallback* self = (AppSpecificCallback*) _self;
    CLOG_INFO("deserialize into authoritative")
    transmuteVmSetState(self->authoritative, state);
}

void rectifyAuthoritativeTick(void* _self, const TransmuteInput* input)
{
    AppSpecificCallback* self = (AppSpecificCallback*) _self;
    CLOG_INFO("authoritative: tick")

    transmuteVmTick(self->authoritative, input);
}

void rectifyCopyAuthoritative(void* _self, StepId stepId)
{
    AppSpecificCallback* self = (AppSpecificCallback*) _self;
    self->cachedState = transmuteVmGetState(self->authoritative);
    CLOG_INFO("copy authoritative to prediction")

    transmuteVmSetState(self->predicted, &self->cachedState );
}

void rectifyPredictionTick(void* _self, const TransmuteInput* input)
{
    AppSpecificCallback* self = (AppSpecificCallback*) _self;
    CLOG_INFO("predicted: tick")
    transmuteVmTick(self->predicted, input);
}

void rectifyPostPredictionTick(void* _self)
{
    AppSpecificCallback* self = (AppSpecificCallback*) _self;
    CLOG_INFO("predicted: post prediction")
}

void rectifyAuthoritativePreTicks(void* _self)
{
    AppSpecificCallback* self = (AppSpecificCallback*) _self;
    CLOG_INFO("authoritative: pre ticks")
}

static TransmuteVm createVm(AppSpecificVm* vmPointer, const char* description)
{
    TransmuteVm transmuteVm;
    TransmuteVmSetup setup;
    setup.tickFn = appSpecificTick;
    setup.tickDurationMs = 16;
    setup.setStateFn = appSpecificSetState;
    setup.getStateFn = appSpecificGetState;
    setup.inputToString = appSpecificInputToString;
    setup.stateToString = appSpecificStateToString;
    setup.version.major = 0;
    setup.version.minor = 0;
    setup.version.patch = 0;

    Clog subLog;

    subLog.config = &g_clog;
    subLog.constantPrefix = description;

    transmuteVmInit(&transmuteVm, vmPointer, setup, subLog);

    return transmuteVm;
}

UTEST(Rectify, verify)
{
    ImprintDefaultSetup imprint;
    imprintDefaultSetupInit(&imprint, 16 * 1024 * 1024);

    AppSpecificVm appSpecificAuthoritativeVm;
    TransmuteVm authoritativeTransmuteVm = createVm(&appSpecificAuthoritativeVm, "AuthoritativeVm");

    AppSpecificVm appSpecificPredictedVm;
    TransmuteVm predictedTransmuteVm = createVm(&appSpecificPredictedVm, "PredictedVm");

    Rectify rectify;

    ImprintAllocator* allocator = &imprint.slabAllocator.info.allocator;

    AppSpecificState initialAppState;
    initialAppState.time = 0;
    initialAppState.x = 0;

    TransmuteState initialTransmuteState;
    initialTransmuteState.state = &initialAppState;
    initialTransmuteState.octetSize = sizeof(initialAppState);

    StepId initialStepId = {101};

    Clog subLog;
    subLog.constantPrefix = "rectify";
    subLog.config = &g_clog;

    AppSpecificCallback appCallback = {.predicted = &predictedTransmuteVm, .authoritative = &authoritativeTransmuteVm};

    RectifyCallbackObjectVtbl vtbl = {
        .authoritativeDeserializeFn = rectifyAuthoritativeDeserialize,
        .authoritativeTickFn = rectifyAuthoritativeTick,
        .predictionTickFn = rectifyPredictionTick,
        .postPredictionTicksFn = rectifyPostPredictionTick,
        .preAuthoritativeTicksFn = rectifyAuthoritativePreTicks,
        .copyFromAuthoritativeToPredictionFn = rectifyCopyAuthoritative,
    };

    RectifyCallbackObject rectifyCallbackObject = {.vtbl = &vtbl, .self = &appCallback};

    RectifySetup rectifySetup;
    rectifySetup.allocator = allocator;
    rectifySetup.maxStepOctetSizeForSingleParticipant = 5;
    rectifySetup.maxPlayerCount = 32;
    rectifySetup.maxTicksFromAuthoritative = 16;
    rectifySetup.log = subLog;
    rectifyInit(&rectify, rectifyCallbackObject, rectifySetup, initialTransmuteState, initialStepId);

    rectifyUpdate(&rectify);

    AppSpecificParticipantInput authoritativeGameInput;
    authoritativeGameInput.horizontalAxis = 24;

    TransmuteInput authoritativeInput;
    TransmuteParticipantInput authoritativeInputs[1];
    authoritativeInputs[0].input = &authoritativeGameInput;
    authoritativeInputs[0].octetSize = sizeof(authoritativeGameInput);
    authoritativeInputs[0].participantId = 1;
    authoritativeInputs[0].inputType = TransmuteParticipantInputTypeNormal;

    authoritativeInput.participantInputs = authoritativeInputs;
    authoritativeInput.participantCount = 1;

    rectifyAddAuthoritativeStep(&rectify, &authoritativeInput, initialStepId);

    AppSpecificParticipantInput predictedGameInput;
    predictedGameInput.horizontalAxis = -1;

    TransmuteInput predictedInput;
    TransmuteParticipantInput predictedInputs[1];
    predictedInputs[0].input = &predictedGameInput;
    predictedInputs[0].octetSize = sizeof(predictedGameInput);
    predictedInputs[0].participantId = 1;
    predictedInputs[0].inputType = TransmuteParticipantInputTypeNormal;

    predictedInput.participantInputs = predictedInputs;
    predictedInput.participantCount = 1;

    rectifyAddPredictedStep(&rectify, &predictedInput, initialStepId);
    rectifyAddPredictedStep(&rectify, &predictedInput, initialStepId + 1);
    const AppSpecificState* predicted = &appSpecificPredictedVm.appSpecificState;

    ASSERT_EQ(0, predicted->x);
    ASSERT_EQ(0, predicted->time);

    rectifyUpdate(&rectify);

    ASSERT_EQ(24-1, predicted->x);
    ASSERT_EQ(2, predicted->time);

    /*



        seerSetState(&seer, initialTransmuteState, initialStepId);

        NbsSteps stepBuffer;

        nbsStepsInit(&stepBuffer, &imprint.slabAllocator.info.allocator, 7);
        nbsStepsReInit(&stepBuffer, initialStepId);
        NimbleStepsOutSerializeLocalParticipants data;
        AppSpecificParticipantInput gameInput;
        gameInput.horizontalAxis = 24;

        data.participants[0].participantIndex = 0;
        data.participants[0].payload = (const uint8_t*) &gameInput;
        data.participants[0].payloadCount = sizeof(gameInput);
        data.participantCount = 1;

        uint8_t stepBuf[64];

        int octetLength = nbsStepsOutSerializeStep(&data, stepBuf, 64);
        if (octetLength < 0) {
            CLOG_ERROR("not working")
        }

        nbsStepsWrite(&stepBuffer, initialStepId, stepBuf, octetLength);

        ASSERT_EQ(0, appSpecificVm.appSpecificState.x);
        ASSERT_EQ(0, appSpecificVm.appSpecificState.time);

        seerUpdate(&seer);

        StepId expectedStepId;
        TransmuteState currentState = seerGetState(&seer, &expectedStepId);
        const AppSpecificState* currentAppState = (const AppSpecificState*) currentState.state;


        ASSERT_EQ(1, currentAppState->x);
        ASSERT_EQ(1, currentAppState->time);
        */
}
