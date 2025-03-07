#ifndef _GLCPACKEDPIXELSTESTS_HPP
#define _GLCPACKEDPIXELSTESTS_HPP
/*-------------------------------------------------------------------------
 * OpenGL Conformance Test Suite
 * -----------------------------
 *
 * Copyright (c) 2017 The Khronos Group Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */ /*!
 * \file glcPackedPixelsTests.hpp
 * \brief
 */ /*-------------------------------------------------------------------*/

#include "glcTestCase.hpp"

namespace glcts
{

class PackedPixelsTests : public deqp::TestCaseGroup
{
public:
    /* Public methods */
    PackedPixelsTests(deqp::Context &context);
    virtual ~PackedPixelsTests(void);

    void init(void);

private:
    /* Private methods */
    PackedPixelsTests(const PackedPixelsTests &other);
    PackedPixelsTests &operator=(const PackedPixelsTests &other);
};

} // namespace glcts

#endif // _GLCPACKEDPIXELSTESTS_HPP
