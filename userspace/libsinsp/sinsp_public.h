// SPDX-License-Identifier: Apache-2.0
/*
Copyright (C) 2023 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/
#pragma once

#define SINSP_PUBLIC

#ifndef ASSERT

#include <assert.h>
#ifdef _DEBUG

#ifdef _WIN32
#include <cassert>
#endif

#define ASSERT(X) assert(X);

#else  // _DEBUG
#define ASSERT(X)
#endif  // _DEBUG
#endif  // ASSERT
