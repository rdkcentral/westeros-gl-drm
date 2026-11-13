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
/* Intentionally no include guard: pair with the production-source include. */
#undef calloc
#undef drmSetMaster
#undef drmGetVersion
#undef drmFreeVersion
#undef drmModeGetEncoder
#undef drmModeGetCrtc
#undef drmModeGetPlaneResources
#undef drmModeFreePlaneResources
#undef drmModeGetPlane
#undef gbm_create_device
#undef pthread_create
#undef pthread_attr_init
#undef pthread_attr_setdetachstate
#undef flock
#undef socket
#undef bind
#undef listen
#undef shutdown
#undef unlink