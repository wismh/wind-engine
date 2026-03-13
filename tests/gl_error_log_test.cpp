#include <gtest/gtest.h>

#include "render/gl_error_log.h"

using engine::render::GlErrorLog;
using engine::render::gl_error_name;

TEST(GlErrorLog, NamesTheGlErrors) {
    EXPECT_EQ(gl_error_name(0x0500), "GL_INVALID_ENUM");
    EXPECT_EQ(gl_error_name(0x0502), "GL_INVALID_OPERATION");
    EXPECT_EQ(gl_error_name(0x0506), "GL_INVALID_FRAMEBUFFER_OPERATION");
    EXPECT_EQ(gl_error_name(0x1234), "unknown GL error");
}

TEST(GlErrorLog, ReportsEachErrorOncePerPlace) {
    GlErrorLog log;
    const std::string first = log.report(0x0502, "CmdDrawParticles");
    EXPECT_NE(first.find("GL_INVALID_OPERATION (0x0502) after CmdDrawParticles"), std::string::npos);
    EXPECT_TRUE(log.report(0x0502, "CmdDrawParticles").empty());

    EXPECT_FALSE(log.report(0x0502, "CmdDrawMesh").empty());
    EXPECT_FALSE(log.report(0x0501, "CmdDrawParticles").empty());
}
