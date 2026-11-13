/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/
#ifndef WESTEROS_RENDERER_TEST_SUPPORT_H
#define WESTEROS_RENDERER_TEST_SUPPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "westeros-gl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned resourceQueries;
    unsigned connectorQueries;
    unsigned connectorFrees;
    unsigned surfaceCreates;
    unsigned surfaceDestroys;
    unsigned openCalls;
    unsigned closeCalls;
    unsigned symbolQueries;
    uint32_t surfaceWidth, surfaceHeight, surfaceFormat, surfaceFlags;
    char cardPath[256];
} RendererMockCalls;

typedef struct {
    int width, height, rate;
    bool interlaced, pending, sizeChanged, explicitlySelected;
} RendererModeState;

typedef struct {
    int x, y, width, height;
    uint32_t visibleWidth, visibleHeight, skipX, skipY;
} RendererRect;

void RendererReset(void);
void RendererCleanup(void);
WstGLCtx *RendererCreateContext(int width, int height);
bool RendererHasContext(void);
int RendererReferenceCount(void);
void RendererSetDeviceAvailable(bool available);
void RendererSetResourceFailure(bool fail);
void RendererSetConnectorFailure(bool fail);
void RendererSetConnectorConnected(bool connected);
void RendererAddMode(int width, int height, int rate, bool interlaced);
RendererModeState RendererGetModeState(void);
void RendererSetGraphicsLimit(int width, int height);
void RendererSetCurrentSize(int width, int height);
void RendererNotifySize(void);
void RendererSetExplicitMode(bool selected);
bool RendererModeIsSet(void);
unsigned RendererWindowCount(void);
void RendererSetMissingSymbol(const char *name);
WstGLCtx *RendererInitDevice(void);
const RendererMockCalls *RendererCalls(void);
int RendererPutU32(unsigned char *data, unsigned value);
unsigned RendererGetU32(unsigned char *data);
int RendererPutS64(unsigned char *data, long long value);
long long RendererGetS64(unsigned char *data);
RendererRect RendererSetVideoRect(int x, int y, int width, int height);

#ifdef __cplusplus
}
#endif
#endif