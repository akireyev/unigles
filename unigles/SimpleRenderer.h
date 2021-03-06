#pragma once

#include "pch.h"

namespace unigles
{
    class SimpleRenderer
    {
    public:
        SimpleRenderer();
        ~SimpleRenderer();
        void Draw();
        void UpdateWindowSize(GLsizei width, GLsizei height);

    private:
        GLuint mProgram;
        GLsizei mWindowWidth;
        GLsizei mWindowHeight;

        GLint mPositionAttribLocation;
        GLint mColorAttribLocation;

        GLint mModelUniformLocation;
        GLint mViewUniformLocation;
        GLint mProjUniformLocation;
		GLint mTextureLocation;

        GLuint mTexture;
        GLuint mVertexPositionBuffer;
        GLuint mVertexColorBuffer;
        GLuint mIndexBuffer;

        int mDrawCount;
    };
}