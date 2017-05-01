#pragma once

#include "App.g.h"
#include "OpenGLES.h"
#include "OpenGLESPage.xaml.h"

namespace unigles
{
    ref class App sealed
    {
    public:
        App();
        virtual void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e) override;

    private:
        OpenGLESPage^ mPage;
        OpenGLES mOpenGLES;
    };
}
