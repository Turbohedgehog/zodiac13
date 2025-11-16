#pragma once

#include "OgreInput.h"
#include "OgreBitesPrerequisites.h"
#include <SDL2/SDL.h>

namespace Ogre::z13 {

OgreBites::Event convert(const SDL_Event& in)
{
    OgreBites::Event out;

    out.type = 0;

    switch(in.type)
    {
    case SDL_KEYDOWN:
        out.type = OgreBites::KEYDOWN;
        OGRE_FALLTHROUGH;
    case SDL_KEYUP:
        if(!out.type)
            out.type = OgreBites::KEYUP;
        out.key.repeat = in.key.repeat;
        out.key.keysym.sym = in.key.keysym.sym;
        out.key.keysym.mod = in.key.keysym.mod;
        break;
    case SDL_MOUSEBUTTONUP:
        out.type = OgreBites::MOUSEBUTTONUP;
        OGRE_FALLTHROUGH;
    case SDL_MOUSEBUTTONDOWN:
        if(!out.type)
            out.type = OgreBites::MOUSEBUTTONDOWN;
        out.button.x = in.button.x;
        out.button.y = in.button.y;
        out.button.button = in.button.button;
        out.button.clicks = in.button.clicks;
        break;
    case SDL_MOUSEWHEEL:
        out.type = OgreBites::MOUSEWHEEL;
        out.wheel.y = in.wheel.y;
        break;
    case SDL_MOUSEMOTION:
        out.type = OgreBites::MOUSEMOTION;
        out.motion.x = in.motion.x;
        out.motion.y = in.motion.y;
        out.motion.xrel = in.motion.xrel;
        out.motion.yrel = in.motion.yrel;
        out.motion.windowID = in.motion.windowID;
        break;
    case SDL_FINGERDOWN:
        out.type = OgreBites::FINGERDOWN;
        OGRE_FALLTHROUGH;
    case SDL_FINGERUP:
        if(!out.type)
            out.type = OgreBites::FINGERUP;
        OGRE_FALLTHROUGH;
    case SDL_FINGERMOTION:
        if(!out.type)
            out.type = OgreBites::FINGERMOTION;
        out.tfinger.x = in.tfinger.x;
        out.tfinger.y = in.tfinger.y;
        out.tfinger.dx = in.tfinger.dx;
        out.tfinger.dy = in.tfinger.dy;
        out.tfinger.fingerId = in.tfinger.fingerId;
        break;
    case SDL_TEXTINPUT:
        out.type = OgreBites::TEXTINPUT;
        out.text.chars = in.text.text;
        break;
    case SDL_JOYAXISMOTION:
        out.type = OgreBites::JOYAXISMOTION;
        out.axis.which = in.jaxis.which;
        out.axis.axis = in.jaxis.axis;
        out.axis.value = in.jaxis.value;
        break;
    case SDL_CONTROLLERAXISMOTION:
        out.type = OgreBites::CONTROLLERAXISMOTION;
        out.axis.which = in.caxis.which;
        out.axis.axis = in.caxis.axis;
        out.axis.value = in.caxis.value;
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        out.type = OgreBites::CONTROLLERBUTTONDOWN;
        OGRE_FALLTHROUGH;
    case SDL_CONTROLLERBUTTONUP:
        if(!out.type)
            out.type = OgreBites::CONTROLLERBUTTONUP;
        out.cbutton.which = in.cbutton.which;
        out.cbutton.button = in.cbutton.button;
        break;
    }

    return out;
}

void ProcessEventToListener (const OgreBites::Event& event, OgreBites::InputListener* input_listener) {
    OgreBites::Event scaled = event;
#if 0 // todo: fix getRenderWindow
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    if (event.type == OgreBites::MOUSEMOTION)
    {
        // assumes all windows have the same scale
        float viewScale = getRenderWindow()->getViewPointToPixelScale();
        scaled.motion.x *= viewScale;
        scaled.motion.y *= viewScale;
    }
    else if(event.type == OgreBites::MOUSEBUTTONDOWN || event.type == OgreBites::MOUSEBUTTONUP)
    {
        float viewScale = getRenderWindow()->getViewPointToPixelScale();
        scaled.button.x *= viewScale;
        scaled.button.y *= viewScale;
    }
#endif
#endif

    switch (event.type)
        {
        case OgreBites::KEYDOWN:
            input_listener->keyPressed(event.key);
            break;
        case OgreBites::KEYUP:
            input_listener->keyReleased(event.key);
            break;
        case OgreBites::MOUSEBUTTONDOWN:
            input_listener->mousePressed(scaled.button);
            break;
        case OgreBites::MOUSEBUTTONUP:
            input_listener->mouseReleased(scaled.button);
            break;
        case OgreBites::MOUSEWHEEL:
            input_listener->mouseWheelRolled(event.wheel);
            break;
        case OgreBites::MOUSEMOTION:
            input_listener->mouseMoved(scaled.motion);
            break;
        case OgreBites::FINGERDOWN:
            // for finger down we have to move the pointer first
            input_listener->touchMoved(event.tfinger);
            input_listener->touchPressed(event.tfinger);
            break;
        case OgreBites::FINGERUP:
            input_listener->touchReleased(event.tfinger);
            break;
        case OgreBites::FINGERMOTION:
            input_listener->touchMoved(event.tfinger);
            break;
        case OgreBites::TEXTINPUT:
            input_listener->textInput(event.text);
            break;
        case OgreBites::JOYAXISMOTION:
        case OgreBites::CONTROLLERAXISMOTION:
            input_listener->axisMoved(event.axis);
            break;
        case OgreBites::CONTROLLERBUTTONDOWN:
            input_listener->buttonPressed(event.cbutton);
            break;
        case OgreBites::CONTROLLERBUTTONUP:
            input_listener->buttonReleased(event.cbutton);
            break;
        }
    }

}  // namespace Ogre::z13