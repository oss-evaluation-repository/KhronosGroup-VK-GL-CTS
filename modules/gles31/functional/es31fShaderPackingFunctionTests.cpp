/*-------------------------------------------------------------------------
 * drawElements Quality Program OpenGL ES 3.1 Module
 * -------------------------------------------------
 *
 * Copyright 2014 The Android Open Source Project
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
 *//*!
 * \file
 * \brief Floating-point packing and unpacking function tests.
 *//*--------------------------------------------------------------------*/

#include "es31fShaderPackingFunctionTests.hpp"
#include "glsShaderExecUtil.hpp"
#include "tcuTestLog.hpp"
#include "tcuFormatUtil.hpp"
#include "tcuFloat.hpp"
#include "deRandom.hpp"
#include "deMath.h"
#include "deString.h"

namespace deqp
{
namespace gles31
{
namespace Functional
{

using std::string;
using tcu::TestLog;
using namespace gls::ShaderExecUtil;

namespace
{

inline uint32_t getUlpDiff(float a, float b)
{
    const uint32_t aBits = tcu::Float32(a).bits();
    const uint32_t bBits = tcu::Float32(b).bits();
    return aBits > bBits ? aBits - bBits : bBits - aBits;
}

struct HexFloat
{
    const float value;
    HexFloat(const float value_) : value(value_)
    {
    }
};

std::ostream &operator<<(std::ostream &str, const HexFloat &v)
{
    return str << v.value << " / " << tcu::toHex(tcu::Float32(v.value).bits());
}

} // namespace

// ShaderPackingFunctionCase

class ShaderPackingFunctionCase : public TestCase
{
public:
    ShaderPackingFunctionCase(Context &context, const char *name, const char *description, glu::ShaderType shaderType);
    ~ShaderPackingFunctionCase(void);

    void init(void);
    void deinit(void);

protected:
    glu::ShaderType m_shaderType;
    ShaderSpec m_spec;
    ShaderExecutor *m_executor;

private:
    ShaderPackingFunctionCase(const ShaderPackingFunctionCase &other);
    ShaderPackingFunctionCase &operator=(const ShaderPackingFunctionCase &other);
};

ShaderPackingFunctionCase::ShaderPackingFunctionCase(Context &context, const char *name, const char *description,
                                                     glu::ShaderType shaderType)
    : TestCase(context, name, description)
    , m_shaderType(shaderType)
    , m_executor(DE_NULL)
{
    m_spec.version = glu::getContextTypeGLSLVersion(context.getRenderContext().getType());
}

ShaderPackingFunctionCase::~ShaderPackingFunctionCase(void)
{
    ShaderPackingFunctionCase::deinit();
}

void ShaderPackingFunctionCase::init(void)
{
    DE_ASSERT(!m_executor);

    m_executor = createExecutor(m_context.getRenderContext(), m_shaderType, m_spec);
    m_testCtx.getLog() << m_executor;

    if (!m_executor->isOk())
        throw tcu::TestError("Compile failed");
}

void ShaderPackingFunctionCase::deinit(void)
{
    delete m_executor;
    m_executor = DE_NULL;
}

// Test cases

class PackSnorm2x16Case : public ShaderPackingFunctionCase
{
public:
    PackSnorm2x16Case(Context &context, glu::ShaderType shaderType, glu::Precision precision)
        : ShaderPackingFunctionCase(
              context,
              (string("packsnorm2x16") + getPrecisionPostfix(precision) + getShaderTypePostfix(shaderType)).c_str(),
              "packSnorm2x16", shaderType)
        , m_precision(precision)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_FLOAT_VEC2, precision)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = packSnorm2x16(in0);";
    }

    IterateResult iterate(void)
    {
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<tcu::Vec2> inputs;
        std::vector<uint32_t> outputs;
        const int                    maxDiff = m_precision == glu::PRECISION_HIGHP    ? 1        : // Rounding only.
                                                  m_precision == glu::PRECISION_MEDIUMP    ? 33    : // (2^-10) * (2^15) + 1
                                                  m_precision == glu::PRECISION_LOWP    ? 129    : 0;    // (2^-8) * (2^15) + 1

        // Special values to check.
        inputs.push_back(tcu::Vec2(0.0f, 0.0f));
        inputs.push_back(tcu::Vec2(-1.0f, 1.0f));
        inputs.push_back(tcu::Vec2(0.5f, -0.5f));
        inputs.push_back(tcu::Vec2(-1.5f, 1.5f));
        inputs.push_back(tcu::Vec2(0.25f, -0.75f));

        // Random values, mostly in range.
        for (int ndx = 0; ndx < 15; ndx++)
        {
            const float x = rnd.getFloat() * 2.5f - 1.25f;
            const float y = rnd.getFloat() * 2.5f - 1.25f;
            inputs.push_back(tcu::Vec2(x, y));
        }

        // Large random values.
        for (int ndx = 0; ndx < 80; ndx++)
        {
            const float x = rnd.getFloat() * 1e6f - 0.5e6f;
            const float y = rnd.getFloat() * 1e6f - 0.5e6f;
            inputs.push_back(tcu::Vec2(x, y));
        }

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < numValues; valNdx++)
            {
                const uint16_t ref0 =
                    (uint16_t)de::clamp(deRoundFloatToInt32(de::clamp(inputs[valNdx].x(), -1.0f, 1.0f) * 32767.0f),
                                        -(1 << 15), (1 << 15) - 1);
                const uint16_t ref1 =
                    (uint16_t)de::clamp(deRoundFloatToInt32(de::clamp(inputs[valNdx].y(), -1.0f, 1.0f) * 32767.0f),
                                        -(1 << 15), (1 << 15) - 1);
                const uint32_t ref  = (ref1 << 16) | ref0;
                const uint32_t res  = outputs[valNdx];
                const uint16_t res0 = (uint16_t)(res & 0xffff);
                const uint16_t res1 = (uint16_t)(res >> 16);
                const int diff0     = de::abs((int)ref0 - (int)res0);
                const int diff1     = de::abs((int)ref1 - (int)res1);

                if (diff0 > maxDiff || diff1 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog() << TestLog::Message << "ERROR: Mismatch in value " << valNdx
                                           << ", expected packSnorm2x16(" << inputs[valNdx] << ") = " << tcu::toHex(ref)
                                           << ", got " << tcu::toHex(res) << "\n  diffs = (" << diff0 << ", " << diff1
                                           << "), max diff = " << maxDiff << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }

private:
    glu::Precision m_precision;
};

class UnpackSnorm2x16Case : public ShaderPackingFunctionCase
{
public:
    UnpackSnorm2x16Case(Context &context, glu::ShaderType shaderType)
        : ShaderPackingFunctionCase(context, (string("unpacksnorm2x16") + getShaderTypePostfix(shaderType)).c_str(),
                                    "unpackSnorm2x16", shaderType)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_FLOAT_VEC2, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = unpackSnorm2x16(in0);";
    }

    IterateResult iterate(void)
    {
        const uint32_t maxDiff = 1; // Rounding error.
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<uint32_t> inputs;
        std::vector<tcu::Vec2> outputs;

        inputs.push_back(0x00000000u);
        inputs.push_back(0x7fff8000u);
        inputs.push_back(0x80007fffu);
        inputs.push_back(0xffffffffu);
        inputs.push_back(0x0001fffeu);

        // Random values.
        for (int ndx = 0; ndx < 95; ndx++)
            inputs.push_back(rnd.getUint32());

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < (int)inputs.size(); valNdx++)
            {
                const int16_t in0 = (int16_t)(uint16_t)(inputs[valNdx] & 0xffff);
                const int16_t in1 = (int16_t)(uint16_t)(inputs[valNdx] >> 16);
                const float ref0  = de::clamp(float(in0) / 32767.f, -1.0f, 1.0f);
                const float ref1  = de::clamp(float(in1) / 32767.f, -1.0f, 1.0f);
                const float res0  = outputs[valNdx].x();
                const float res1  = outputs[valNdx].y();

                const uint32_t diff0 = getUlpDiff(ref0, res0);
                const uint32_t diff1 = getUlpDiff(ref1, res1);

                if (diff0 > maxDiff || diff1 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog() << TestLog::Message << "ERROR: Mismatch in value " << valNdx << ",\n"
                                           << "  expected unpackSnorm2x16(" << tcu::toHex(inputs[valNdx]) << ") = "
                                           << "vec2(" << HexFloat(ref0) << ", " << HexFloat(ref1) << ")"
                                           << ", got vec2(" << HexFloat(res0) << ", " << HexFloat(res1) << ")"
                                           << "\n  ULP diffs = (" << diff0 << ", " << diff1
                                           << "), max diff = " << maxDiff << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }
};

class PackUnorm2x16Case : public ShaderPackingFunctionCase
{
public:
    PackUnorm2x16Case(Context &context, glu::ShaderType shaderType, glu::Precision precision)
        : ShaderPackingFunctionCase(
              context,
              (string("packunorm2x16") + getPrecisionPostfix(precision) + getShaderTypePostfix(shaderType)).c_str(),
              "packUnorm2x16", shaderType)
        , m_precision(precision)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_FLOAT_VEC2, precision)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = packUnorm2x16(in0);";
    }

    IterateResult iterate(void)
    {
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<tcu::Vec2> inputs;
        std::vector<uint32_t> outputs;
        const int                    maxDiff = m_precision == glu::PRECISION_HIGHP    ? 1        : // Rounding only.
                                                  m_precision == glu::PRECISION_MEDIUMP    ? 65    : // (2^-10) * (2^16) + 1
                                                  m_precision == glu::PRECISION_LOWP    ? 257    : 0;    // (2^-8) * (2^16) + 1

        // Special values to check.
        inputs.push_back(tcu::Vec2(0.0f, 0.0f));
        inputs.push_back(tcu::Vec2(0.5f, 1.0f));
        inputs.push_back(tcu::Vec2(1.0f, 0.5f));
        inputs.push_back(tcu::Vec2(-0.5f, 1.5f));
        inputs.push_back(tcu::Vec2(0.25f, 0.75f));

        // Random values, mostly in range.
        for (int ndx = 0; ndx < 15; ndx++)
        {
            const float x = rnd.getFloat() * 1.25f;
            const float y = rnd.getFloat() * 1.25f;
            inputs.push_back(tcu::Vec2(x, y));
        }

        // Large random values.
        for (int ndx = 0; ndx < 80; ndx++)
        {
            const float x = rnd.getFloat() * 1e6f - 1e5f;
            const float y = rnd.getFloat() * 1e6f - 1e5f;
            inputs.push_back(tcu::Vec2(x, y));
        }

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < (int)inputs.size(); valNdx++)
            {
                const uint16_t ref0 = (uint16_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].x(), 0.0f, 1.0f) * 65535.0f), 0, (1 << 16) - 1);
                const uint16_t ref1 = (uint16_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].y(), 0.0f, 1.0f) * 65535.0f), 0, (1 << 16) - 1);
                const uint32_t ref  = (ref1 << 16) | ref0;
                const uint32_t res  = outputs[valNdx];
                const uint16_t res0 = (uint16_t)(res & 0xffff);
                const uint16_t res1 = (uint16_t)(res >> 16);
                const int diff0     = de::abs((int)ref0 - (int)res0);
                const int diff1     = de::abs((int)ref1 - (int)res1);

                if (diff0 > maxDiff || diff1 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog() << TestLog::Message << "ERROR: Mismatch in value " << valNdx
                                           << ", expected packUnorm2x16(" << inputs[valNdx] << ") = " << tcu::toHex(ref)
                                           << ", got " << tcu::toHex(res) << "\n  diffs = (" << diff0 << ", " << diff1
                                           << "), max diff = " << maxDiff << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }

private:
    glu::Precision m_precision;
};

class UnpackUnorm2x16Case : public ShaderPackingFunctionCase
{
public:
    UnpackUnorm2x16Case(Context &context, glu::ShaderType shaderType)
        : ShaderPackingFunctionCase(context, (string("unpackunorm2x16") + getShaderTypePostfix(shaderType)).c_str(),
                                    "unpackUnorm2x16", shaderType)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_FLOAT_VEC2, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = unpackUnorm2x16(in0);";
    }

    IterateResult iterate(void)
    {
        const uint32_t maxDiff = 1; // Rounding error.
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<uint32_t> inputs;
        std::vector<tcu::Vec2> outputs;

        inputs.push_back(0x00000000u);
        inputs.push_back(0x7fff8000u);
        inputs.push_back(0x80007fffu);
        inputs.push_back(0xffffffffu);
        inputs.push_back(0x0001fffeu);

        // Random values.
        for (int ndx = 0; ndx < 95; ndx++)
            inputs.push_back(rnd.getUint32());

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < (int)inputs.size(); valNdx++)
            {
                const uint16_t in0 = (uint16_t)(inputs[valNdx] & 0xffff);
                const uint16_t in1 = (uint16_t)(inputs[valNdx] >> 16);
                const float ref0   = float(in0) / 65535.0f;
                const float ref1   = float(in1) / 65535.0f;
                const float res0   = outputs[valNdx].x();
                const float res1   = outputs[valNdx].y();

                const uint32_t diff0 = getUlpDiff(ref0, res0);
                const uint32_t diff1 = getUlpDiff(ref1, res1);

                if (diff0 > maxDiff || diff1 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog() << TestLog::Message << "ERROR: Mismatch in value " << valNdx << ",\n"
                                           << "  expected unpackUnorm2x16(" << tcu::toHex(inputs[valNdx]) << ") = "
                                           << "vec2(" << HexFloat(ref0) << ", " << HexFloat(ref1) << ")"
                                           << ", got vec2(" << HexFloat(res0) << ", " << HexFloat(res1) << ")"
                                           << "\n  ULP diffs = (" << diff0 << ", " << diff1
                                           << "), max diff = " << maxDiff << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }
};

class PackHalf2x16Case : public ShaderPackingFunctionCase
{
public:
    PackHalf2x16Case(Context &context, glu::ShaderType shaderType)
        : ShaderPackingFunctionCase(context, (string("packhalf2x16") + getShaderTypePostfix(shaderType)).c_str(),
                                    "packHalf2x16", shaderType)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_FLOAT_VEC2, glu::PRECISION_HIGHP)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = packHalf2x16(in0);";
    }

    IterateResult iterate(void)
    {
        const int maxDiff = 0; // Values can be represented exactly in mediump.
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<tcu::Vec2> inputs;
        std::vector<uint32_t> outputs;

        // Special values to check.
        inputs.push_back(tcu::Vec2(0.0f, 0.0f));
        inputs.push_back(tcu::Vec2(0.5f, 1.0f));
        inputs.push_back(tcu::Vec2(1.0f, 0.5f));
        inputs.push_back(tcu::Vec2(-0.5f, 1.5f));
        inputs.push_back(tcu::Vec2(0.25f, 0.75f));

        // Random values.
        {
            const int minExp = -14;
            const int maxExp = 15;

            for (int ndx = 0; ndx < 95; ndx++)
            {
                tcu::Vec2 v;
                for (int c = 0; c < 2; c++)
                {
                    const int s             = rnd.getBool() ? 1 : -1;
                    const int exp           = rnd.getInt(minExp, maxExp);
                    const uint32_t mantissa = rnd.getUint32() & ((1 << 23) - 1);

                    v[c] = tcu::Float32::construct(s, exp ? exp : 1 /* avoid denormals */, (1u << 23) | mantissa)
                               .asFloat();
                }
                inputs.push_back(v);
            }
        }

        // Convert input values to fp16 and back to make sure they can be represented exactly in mediump.
        for (std::vector<tcu::Vec2>::iterator inVal = inputs.begin(); inVal != inputs.end(); ++inVal)
            *inVal = tcu::Vec2(tcu::Float16(inVal->x()).asFloat(), tcu::Float16(inVal->y()).asFloat());

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < (int)inputs.size(); valNdx++)
            {
                const uint16_t ref0 = (uint16_t)tcu::Float16(inputs[valNdx].x()).bits();
                const uint16_t ref1 = (uint16_t)tcu::Float16(inputs[valNdx].y()).bits();
                const uint32_t ref  = (ref1 << 16) | ref0;
                const uint32_t res  = outputs[valNdx];
                const uint16_t res0 = (uint16_t)(res & 0xffff);
                const uint16_t res1 = (uint16_t)(res >> 16);
                const int diff0     = de::abs((int)ref0 - (int)res0);
                const int diff1     = de::abs((int)ref1 - (int)res1);

                if (diff0 > maxDiff || diff1 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog() << TestLog::Message << "ERROR: Mismatch in value " << valNdx
                                           << ", expected packHalf2x16(" << inputs[valNdx] << ") = " << tcu::toHex(ref)
                                           << ", got " << tcu::toHex(res) << "\n  diffs = (" << diff0 << ", " << diff1
                                           << "), max diff = " << maxDiff << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }
};

class UnpackHalf2x16Case : public ShaderPackingFunctionCase
{
public:
    UnpackHalf2x16Case(Context &context, glu::ShaderType shaderType)
        : ShaderPackingFunctionCase(context, (string("unpackhalf2x16") + getShaderTypePostfix(shaderType)).c_str(),
                                    "unpackHalf2x16", shaderType)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_FLOAT_VEC2, glu::PRECISION_MEDIUMP)));

        m_spec.source = "out0 = unpackHalf2x16(in0);";
    }

    IterateResult iterate(void)
    {
        const int maxDiff = 0; // All bits must be accurate.
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<uint32_t> inputs;
        std::vector<tcu::Vec2> outputs;

        // Special values.
        inputs.push_back((tcu::Float16(0.0f).bits() << 16) | tcu::Float16(1.0f).bits());
        inputs.push_back((tcu::Float16(1.0f).bits() << 16) | tcu::Float16(0.0f).bits());
        inputs.push_back((tcu::Float16(-1.0f).bits() << 16) | tcu::Float16(0.5f).bits());
        inputs.push_back((tcu::Float16(0.5f).bits() << 16) | tcu::Float16(-0.5f).bits());

        // Construct random values.
        {
            const int minExp   = -14;
            const int maxExp   = 15;
            const int mantBits = 10;

            for (int ndx = 0; ndx < 96; ndx++)
            {
                uint32_t inVal = 0;
                for (int c = 0; c < 2; c++)
                {
                    const int s             = rnd.getBool() ? 1 : -1;
                    const int exp           = rnd.getInt(minExp, maxExp);
                    const uint32_t mantissa = rnd.getUint32() & ((1 << mantBits) - 1);
                    const uint16_t value =
                        tcu::Float16::construct(s, exp ? exp : 1 /* avoid denorm */, (uint16_t)((1u << 10) | mantissa))
                            .bits();

                    inVal |= value << (16 * c);
                }
                inputs.push_back(inVal);
            }
        }

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < (int)inputs.size(); valNdx++)
            {
                const uint16_t in0 = (uint16_t)(inputs[valNdx] & 0xffff);
                const uint16_t in1 = (uint16_t)(inputs[valNdx] >> 16);
                const float ref0   = tcu::Float16(in0).asFloat();
                const float ref1   = tcu::Float16(in1).asFloat();
                const float res0   = outputs[valNdx].x();
                const float res1   = outputs[valNdx].y();

                const uint32_t refBits0 = tcu::Float32(ref0).bits();
                const uint32_t refBits1 = tcu::Float32(ref1).bits();
                const uint32_t resBits0 = tcu::Float32(res0).bits();
                const uint32_t resBits1 = tcu::Float32(res1).bits();

                const int diff0 = de::abs((int)refBits0 - (int)resBits0);
                const int diff1 = de::abs((int)refBits1 - (int)resBits1);

                if (diff0 > maxDiff || diff1 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog() << TestLog::Message << "ERROR: Mismatch in value " << valNdx << ",\n"
                                           << "  expected unpackHalf2x16(" << tcu::toHex(inputs[valNdx]) << ") = "
                                           << "vec2(" << ref0 << " / " << tcu::toHex(refBits0) << ", " << ref1 << " / "
                                           << tcu::toHex(refBits1) << ")"
                                           << ", got vec2(" << res0 << " / " << tcu::toHex(resBits0) << ", " << res1
                                           << " / " << tcu::toHex(resBits1) << ")"
                                           << "\n  ULP diffs = (" << diff0 << ", " << diff1
                                           << "), max diff = " << maxDiff << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }
};

class PackSnorm4x8Case : public ShaderPackingFunctionCase
{
public:
    PackSnorm4x8Case(Context &context, glu::ShaderType shaderType, glu::Precision precision)
        : ShaderPackingFunctionCase(
              context,
              (string("packsnorm4x8") + getPrecisionPostfix(precision) + getShaderTypePostfix(shaderType)).c_str(),
              "packSnorm4x8", shaderType)
        , m_precision(precision)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_FLOAT_VEC4, precision)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = packSnorm4x8(in0);";
    }

    IterateResult iterate(void)
    {
        de::Random rnd(deStringHash(getName()) ^ 0x42f2c0);
        std::vector<tcu::Vec4> inputs;
        std::vector<uint32_t> outputs;
        const int                    maxDiff = m_precision == glu::PRECISION_HIGHP    ? 1    : // Rounding only.
                                                  m_precision == glu::PRECISION_MEDIUMP    ? 1    : // (2^-10) * (2^7) + 1
                                                  m_precision == glu::PRECISION_LOWP    ? 2    : 0;    // (2^-8) * (2^7) + 1

        // Special values to check.
        inputs.push_back(tcu::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
        inputs.push_back(tcu::Vec4(-1.0f, 1.0f, -1.0f, 1.0f));
        inputs.push_back(tcu::Vec4(0.5f, -0.5f, -0.5f, 0.5f));
        inputs.push_back(tcu::Vec4(-1.5f, 1.5f, -1.5f, 1.5f));
        inputs.push_back(tcu::Vec4(0.25f, -0.75f, -0.25f, 0.75f));

        // Random values, mostly in range.
        for (int ndx = 0; ndx < 15; ndx++)
        {
            const float x = rnd.getFloat() * 2.5f - 1.25f;
            const float y = rnd.getFloat() * 2.5f - 1.25f;
            const float z = rnd.getFloat() * 2.5f - 1.25f;
            const float w = rnd.getFloat() * 2.5f - 1.25f;
            inputs.push_back(tcu::Vec4(x, y, z, w));
        }

        // Large random values.
        for (int ndx = 0; ndx < 80; ndx++)
        {
            const float x = rnd.getFloat() * 1e6f - 0.5e6f;
            const float y = rnd.getFloat() * 1e6f - 0.5e6f;
            const float z = rnd.getFloat() * 1e6f - 0.5e6f;
            const float w = rnd.getFloat() * 1e6f - 0.5e6f;
            inputs.push_back(tcu::Vec4(x, y, z, w));
        }

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < numValues; valNdx++)
            {
                const uint16_t ref0 = (uint8_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].x(), -1.0f, 1.0f) * 127.0f), -(1 << 7), (1 << 7) - 1);
                const uint16_t ref1 = (uint8_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].y(), -1.0f, 1.0f) * 127.0f), -(1 << 7), (1 << 7) - 1);
                const uint16_t ref2 = (uint8_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].z(), -1.0f, 1.0f) * 127.0f), -(1 << 7), (1 << 7) - 1);
                const uint16_t ref3 = (uint8_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].w(), -1.0f, 1.0f) * 127.0f), -(1 << 7), (1 << 7) - 1);
                const uint32_t ref =
                    (uint32_t(ref3) << 24) | (uint32_t(ref2) << 16) | (uint32_t(ref1) << 8) | uint32_t(ref0);
                const uint32_t res  = outputs[valNdx];
                const uint16_t res0 = (uint8_t)(res & 0xff);
                const uint16_t res1 = (uint8_t)((res >> 8) & 0xff);
                const uint16_t res2 = (uint8_t)((res >> 16) & 0xff);
                const uint16_t res3 = (uint8_t)((res >> 24) & 0xff);
                const int diff0     = de::abs((int)ref0 - (int)res0);
                const int diff1     = de::abs((int)ref1 - (int)res1);
                const int diff2     = de::abs((int)ref2 - (int)res2);
                const int diff3     = de::abs((int)ref3 - (int)res3);

                if (diff0 > maxDiff || diff1 > maxDiff || diff2 > maxDiff || diff3 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog()
                            << TestLog::Message << "ERROR: Mismatch in value " << valNdx << ", expected packSnorm4x8("
                            << inputs[valNdx] << ") = " << tcu::toHex(ref) << ", got " << tcu::toHex(res)
                            << "\n  diffs = " << tcu::IVec4(diff0, diff1, diff2, diff3) << ", max diff = " << maxDiff
                            << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }

private:
    glu::Precision m_precision;
};

class UnpackSnorm4x8Case : public ShaderPackingFunctionCase
{
public:
    UnpackSnorm4x8Case(Context &context, glu::ShaderType shaderType)
        : ShaderPackingFunctionCase(context, (string("unpacksnorm4x8") + getShaderTypePostfix(shaderType)).c_str(),
                                    "unpackSnorm4x8", shaderType)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_FLOAT_VEC4, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = unpackSnorm4x8(in0);";
    }

    IterateResult iterate(void)
    {
        const uint32_t maxDiff = 1; // Rounding error.
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<uint32_t> inputs;
        std::vector<tcu::Vec4> outputs;

        inputs.push_back(0x00000000u);
        inputs.push_back(0x7fff8000u);
        inputs.push_back(0x80007fffu);
        inputs.push_back(0xffffffffu);
        inputs.push_back(0x0001fffeu);

        // Random values.
        for (int ndx = 0; ndx < 95; ndx++)
            inputs.push_back(rnd.getUint32());

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < (int)inputs.size(); valNdx++)
            {
                const int8_t in0 = (int8_t)(uint8_t)(inputs[valNdx] & 0xff);
                const int8_t in1 = (int8_t)(uint8_t)((inputs[valNdx] >> 8) & 0xff);
                const int8_t in2 = (int8_t)(uint8_t)((inputs[valNdx] >> 16) & 0xff);
                const int8_t in3 = (int8_t)(uint8_t)(inputs[valNdx] >> 24);
                const float ref0 = de::clamp(float(in0) / 127.f, -1.0f, 1.0f);
                const float ref1 = de::clamp(float(in1) / 127.f, -1.0f, 1.0f);
                const float ref2 = de::clamp(float(in2) / 127.f, -1.0f, 1.0f);
                const float ref3 = de::clamp(float(in3) / 127.f, -1.0f, 1.0f);
                const float res0 = outputs[valNdx].x();
                const float res1 = outputs[valNdx].y();
                const float res2 = outputs[valNdx].z();
                const float res3 = outputs[valNdx].w();

                const uint32_t diff0 = getUlpDiff(ref0, res0);
                const uint32_t diff1 = getUlpDiff(ref1, res1);
                const uint32_t diff2 = getUlpDiff(ref2, res2);
                const uint32_t diff3 = getUlpDiff(ref3, res3);

                if (diff0 > maxDiff || diff1 > maxDiff || diff2 > maxDiff || diff3 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog() << TestLog::Message << "ERROR: Mismatch in value " << valNdx << ",\n"
                                           << "  expected unpackSnorm4x8(" << tcu::toHex(inputs[valNdx]) << ") = "
                                           << "vec4(" << HexFloat(ref0) << ", " << HexFloat(ref1) << ", "
                                           << HexFloat(ref2) << ", " << HexFloat(ref3) << ")"
                                           << ", got vec4(" << HexFloat(res0) << ", " << HexFloat(res1) << ", "
                                           << HexFloat(res2) << ", " << HexFloat(res3) << ")"
                                           << "\n  ULP diffs = (" << diff0 << ", " << diff1 << ", " << diff2 << ", "
                                           << diff3 << "), max diff = " << maxDiff << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }
};

class PackUnorm4x8Case : public ShaderPackingFunctionCase
{
public:
    PackUnorm4x8Case(Context &context, glu::ShaderType shaderType, glu::Precision precision)
        : ShaderPackingFunctionCase(
              context,
              (string("packunorm4x8") + getPrecisionPostfix(precision) + getShaderTypePostfix(shaderType)).c_str(),
              "packUnorm4x8", shaderType)
        , m_precision(precision)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_FLOAT_VEC4, precision)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = packUnorm4x8(in0);";
    }

    IterateResult iterate(void)
    {
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<tcu::Vec4> inputs;
        std::vector<uint32_t> outputs;
        const int                    maxDiff = m_precision == glu::PRECISION_HIGHP    ? 1    : // Rounding only.
                                                  m_precision == glu::PRECISION_MEDIUMP    ? 1    : // (2^-10) * (2^8) + 1
                                                  m_precision == glu::PRECISION_LOWP    ? 2    : 0;    // (2^-8) * (2^8) + 1

        // Special values to check.
        inputs.push_back(tcu::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
        inputs.push_back(tcu::Vec4(-1.0f, 1.0f, -1.0f, 1.0f));
        inputs.push_back(tcu::Vec4(0.5f, -0.5f, -0.5f, 0.5f));
        inputs.push_back(tcu::Vec4(-1.5f, 1.5f, -1.5f, 1.5f));
        inputs.push_back(tcu::Vec4(0.25f, -0.75f, -0.25f, 0.75f));

        // Random values, mostly in range.
        for (int ndx = 0; ndx < 15; ndx++)
        {
            const float x = rnd.getFloat() * 1.25f - 0.125f;
            const float y = rnd.getFloat() * 1.25f - 0.125f;
            const float z = rnd.getFloat() * 1.25f - 0.125f;
            const float w = rnd.getFloat() * 1.25f - 0.125f;
            inputs.push_back(tcu::Vec4(x, y, z, w));
        }

        // Large random values.
        for (int ndx = 0; ndx < 80; ndx++)
        {
            const float x = rnd.getFloat() * 1e6f - 1e5f;
            const float y = rnd.getFloat() * 1e6f - 1e5f;
            const float z = rnd.getFloat() * 1e6f - 1e5f;
            const float w = rnd.getFloat() * 1e6f - 1e5f;
            inputs.push_back(tcu::Vec4(x, y, z, w));
        }

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < (int)inputs.size(); valNdx++)
            {
                const uint16_t ref0 = (uint8_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].x(), 0.0f, 1.0f) * 255.0f), 0, (1 << 8) - 1);
                const uint16_t ref1 = (uint8_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].y(), 0.0f, 1.0f) * 255.0f), 0, (1 << 8) - 1);
                const uint16_t ref2 = (uint8_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].z(), 0.0f, 1.0f) * 255.0f), 0, (1 << 8) - 1);
                const uint16_t ref3 = (uint8_t)de::clamp(
                    deRoundFloatToInt32(de::clamp(inputs[valNdx].w(), 0.0f, 1.0f) * 255.0f), 0, (1 << 8) - 1);
                const uint32_t ref =
                    (uint32_t(ref3) << 24) | (uint32_t(ref2) << 16) | (uint32_t(ref1) << 8) | uint32_t(ref0);
                const uint32_t res  = outputs[valNdx];
                const uint16_t res0 = (uint8_t)(res & 0xff);
                const uint16_t res1 = (uint8_t)((res >> 8) & 0xff);
                const uint16_t res2 = (uint8_t)((res >> 16) & 0xff);
                const uint16_t res3 = (uint8_t)((res >> 24) & 0xff);
                const int diff0     = de::abs((int)ref0 - (int)res0);
                const int diff1     = de::abs((int)ref1 - (int)res1);
                const int diff2     = de::abs((int)ref2 - (int)res2);
                const int diff3     = de::abs((int)ref3 - (int)res3);

                if (diff0 > maxDiff || diff1 > maxDiff || diff2 > maxDiff || diff3 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog()
                            << TestLog::Message << "ERROR: Mismatch in value " << valNdx << ", expected packUnorm4x8("
                            << inputs[valNdx] << ") = " << tcu::toHex(ref) << ", got " << tcu::toHex(res)
                            << "\n  diffs = " << tcu::IVec4(diff0, diff1, diff2, diff3) << ", max diff = " << maxDiff
                            << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }

private:
    glu::Precision m_precision;
};

class UnpackUnorm4x8Case : public ShaderPackingFunctionCase
{
public:
    UnpackUnorm4x8Case(Context &context, glu::ShaderType shaderType)
        : ShaderPackingFunctionCase(context, (string("unpackunorm4x8") + getShaderTypePostfix(shaderType)).c_str(),
                                    "unpackUnorm4x8", shaderType)
    {
        m_spec.inputs.push_back(Symbol("in0", glu::VarType(glu::TYPE_UINT, glu::PRECISION_HIGHP)));
        m_spec.outputs.push_back(Symbol("out0", glu::VarType(glu::TYPE_FLOAT_VEC4, glu::PRECISION_HIGHP)));

        m_spec.source = "out0 = unpackUnorm4x8(in0);";
    }

    IterateResult iterate(void)
    {
        const uint32_t maxDiff = 1; // Rounding error.
        de::Random rnd(deStringHash(getName()) ^ 0x776002);
        std::vector<uint32_t> inputs;
        std::vector<tcu::Vec4> outputs;

        inputs.push_back(0x00000000u);
        inputs.push_back(0x7fff8000u);
        inputs.push_back(0x80007fffu);
        inputs.push_back(0xffffffffu);
        inputs.push_back(0x0001fffeu);

        // Random values.
        for (int ndx = 0; ndx < 95; ndx++)
            inputs.push_back(rnd.getUint32());

        outputs.resize(inputs.size());

        m_testCtx.getLog() << TestLog::Message << "Executing shader for " << inputs.size() << " input values"
                           << tcu::TestLog::EndMessage;

        {
            const void *in = &inputs[0];
            void *out      = &outputs[0];

            m_executor->useProgram();
            m_executor->execute((int)inputs.size(), &in, &out);
        }

        // Verify
        {
            const int numValues = (int)inputs.size();
            const int maxPrints = 10;
            int numFailed       = 0;

            for (int valNdx = 0; valNdx < (int)inputs.size(); valNdx++)
            {
                const uint8_t in0 = (uint8_t)(inputs[valNdx] & 0xff);
                const uint8_t in1 = (uint8_t)((inputs[valNdx] >> 8) & 0xff);
                const uint8_t in2 = (uint8_t)((inputs[valNdx] >> 16) & 0xff);
                const uint8_t in3 = (uint8_t)(inputs[valNdx] >> 24);
                const float ref0  = de::clamp(float(in0) / 255.f, 0.0f, 1.0f);
                const float ref1  = de::clamp(float(in1) / 255.f, 0.0f, 1.0f);
                const float ref2  = de::clamp(float(in2) / 255.f, 0.0f, 1.0f);
                const float ref3  = de::clamp(float(in3) / 255.f, 0.0f, 1.0f);
                const float res0  = outputs[valNdx].x();
                const float res1  = outputs[valNdx].y();
                const float res2  = outputs[valNdx].z();
                const float res3  = outputs[valNdx].w();

                const uint32_t diff0 = getUlpDiff(ref0, res0);
                const uint32_t diff1 = getUlpDiff(ref1, res1);
                const uint32_t diff2 = getUlpDiff(ref2, res2);
                const uint32_t diff3 = getUlpDiff(ref3, res3);

                if (diff0 > maxDiff || diff1 > maxDiff || diff2 > maxDiff || diff3 > maxDiff)
                {
                    if (numFailed < maxPrints)
                    {
                        m_testCtx.getLog() << TestLog::Message << "ERROR: Mismatch in value " << valNdx << ",\n"
                                           << "  expected unpackUnorm4x8(" << tcu::toHex(inputs[valNdx]) << ") = "
                                           << "vec4(" << HexFloat(ref0) << ", " << HexFloat(ref1) << ", "
                                           << HexFloat(ref2) << ", " << HexFloat(ref3) << ")"
                                           << ", got vec4(" << HexFloat(res0) << ", " << HexFloat(res1) << ", "
                                           << HexFloat(res2) << ", " << HexFloat(res3) << ")"
                                           << "\n  ULP diffs = (" << diff0 << ", " << diff1 << ", " << diff2 << ", "
                                           << diff3 << "), max diff = " << maxDiff << TestLog::EndMessage;
                    }
                    else if (numFailed == maxPrints)
                        m_testCtx.getLog() << TestLog::Message << "..." << TestLog::EndMessage;

                    numFailed += 1;
                }
            }

            m_testCtx.getLog() << TestLog::Message << (numValues - numFailed) << " / " << numValues << " values passed"
                               << TestLog::EndMessage;

            m_testCtx.setTestResult(numFailed == 0 ? QP_TEST_RESULT_PASS : QP_TEST_RESULT_FAIL,
                                    numFailed == 0 ? "Pass" : "Result comparison failed");
        }

        return STOP;
    }
};

ShaderPackingFunctionTests::ShaderPackingFunctionTests(Context &context)
    : TestCaseGroup(context, "pack_unpack", "Floating-point pack and unpack function tests")
{
}

ShaderPackingFunctionTests::~ShaderPackingFunctionTests(void)
{
}

void ShaderPackingFunctionTests::init(void)
{
    // New built-in functions in GLES 3.1
    {
        const glu::ShaderType allShaderTypes[] = {glu::SHADERTYPE_VERTEX,
                                                  glu::SHADERTYPE_TESSELLATION_CONTROL,
                                                  glu::SHADERTYPE_TESSELLATION_EVALUATION,
                                                  glu::SHADERTYPE_GEOMETRY,
                                                  glu::SHADERTYPE_FRAGMENT,
                                                  glu::SHADERTYPE_COMPUTE};

        // packSnorm4x8
        for (int prec = 0; prec < glu::PRECISION_LAST; prec++)
        {
            for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(allShaderTypes); shaderTypeNdx++)
                addChild(new PackSnorm4x8Case(m_context, allShaderTypes[shaderTypeNdx], glu::Precision(prec)));
        }

        // unpackSnorm4x8
        for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(allShaderTypes); shaderTypeNdx++)
            addChild(new UnpackSnorm4x8Case(m_context, allShaderTypes[shaderTypeNdx]));

        // packUnorm4x8
        for (int prec = 0; prec < glu::PRECISION_LAST; prec++)
        {
            for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(allShaderTypes); shaderTypeNdx++)
                addChild(new PackUnorm4x8Case(m_context, allShaderTypes[shaderTypeNdx], glu::Precision(prec)));
        }

        // unpackUnorm4x8
        for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(allShaderTypes); shaderTypeNdx++)
            addChild(new UnpackUnorm4x8Case(m_context, allShaderTypes[shaderTypeNdx]));
    }

    // GLES 3 functions in new shader types.
    {
        const glu::ShaderType newShaderTypes[] = {glu::SHADERTYPE_GEOMETRY, glu::SHADERTYPE_COMPUTE};

        // packSnorm2x16
        for (int prec = 0; prec < glu::PRECISION_LAST; prec++)
        {
            for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(newShaderTypes); shaderTypeNdx++)
                addChild(new PackSnorm2x16Case(m_context, newShaderTypes[shaderTypeNdx], glu::Precision(prec)));
        }

        // unpackSnorm2x16
        for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(newShaderTypes); shaderTypeNdx++)
            addChild(new UnpackSnorm2x16Case(m_context, newShaderTypes[shaderTypeNdx]));

        // packUnorm2x16
        for (int prec = 0; prec < glu::PRECISION_LAST; prec++)
        {
            for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(newShaderTypes); shaderTypeNdx++)
                addChild(new PackUnorm2x16Case(m_context, newShaderTypes[shaderTypeNdx], glu::Precision(prec)));
        }

        // unpackUnorm2x16
        for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(newShaderTypes); shaderTypeNdx++)
            addChild(new UnpackUnorm2x16Case(m_context, newShaderTypes[shaderTypeNdx]));

        // packHalf2x16
        for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(newShaderTypes); shaderTypeNdx++)
            addChild(new PackHalf2x16Case(m_context, newShaderTypes[shaderTypeNdx]));

        // unpackHalf2x16
        for (int shaderTypeNdx = 0; shaderTypeNdx < DE_LENGTH_OF_ARRAY(newShaderTypes); shaderTypeNdx++)
            addChild(new UnpackHalf2x16Case(m_context, newShaderTypes[shaderTypeNdx]));
    }
}

} // namespace Functional
} // namespace gles31
} // namespace deqp
