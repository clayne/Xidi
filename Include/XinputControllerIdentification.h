/*****************************************************************************
 * XinputControllerDirectInput
 *      Hook and helper for older DirectInput games.
 *      Fixes issues associated with certain Xinput-based controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016
 *****************************************************************************
 * XinputControllerIdentification.h
 *      Declaration of helpers for identifying controller types.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"
#include "Hashers.h"

#include <unordered_map>


namespace XinputControllerDirectInput
{
    // Enumerates the known types of Xbox controllers.
    enum EControllerType: USHORT
    {
        Unknown,                                // Something unknown
        
        Xbox360,                                // Xbox 360 controller
        XboxOne,                                // Xbox One controller
    };
    
    // Encapsulates all constants and logic for identifying the controller type.
    // Methods are intended to be called directly rather than through an instance.
    class XinputControllerIdentification
    {
    private:
        // -------- CONSTANTS ------------------------------------------------------ //

        // Maps each known controller's product GUID to its controller type.
        static const std::unordered_map<const GUID, EControllerType> knownControllers;

        
        // -------- CONSTRUCTION AND DESTRUCTION ----------------------------------- //
        
        // Default constructor. Should never be invoked.
        XinputControllerIdentification();
        
        
    public:
        // -------- CLASS METHODS -------------------------------------------------- //
        
        // Returns TRUE if the specified controller is of a known type.
        static BOOL XinputControllerIdentification::IsControllerTypeKnown(REFGUID productGUID);
        
        // Identifies the type of controller based on its product GUID.
        static EControllerType GetControllerType(REFGUID productGUID);
    };
}