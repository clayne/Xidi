/*****************************************************************************
 * XinputControllerDirectInput
 *      Hook and helper for older DirectInput games.
 *      Fixes issues associated with certain Xinput-based controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016
 *****************************************************************************
 * Base.cpp
 *      Abstract base class for supported control mapping schemes.
 *      Provides common implementations of most core functionality.
 *****************************************************************************/

#include "Mapper/Base.h"

using namespace XinputControllerDirectInput;
using namespace XinputControllerDirectInput::Mapper;


// -------- CONSTRUCTION AND DESTRUCTION ----------------------------------- //
// See "Mapper/Base.h" for documentation.

Base::Base() : instanceToOffset(), offsetToInstance(), mapsValid(FALSE) {}

// ---------

Base::~Base()
{
    instanceToOffset.clear();
    offsetToInstance.clear();
}


// -------- CLASS METHODS -------------------------------------------------- //
// See "Mapper/Base.h" for documentation.

DWORD Base::SizeofInstance(const EInstanceType type)
{
    DWORD szInstance = 0;
    
    switch (type)
    {
    case InstanceTypeAxis:
    case InstanceTypePov:
        szInstance = sizeof(LONG);
        break;

    case InstanceTypeButton:
        szInstance = sizeof(BYTE);
        break;
    }

    return szInstance;
}


// -------- HELPERS -------------------------------------------------------- //

// Returns a friendly name string for the specified axis type by GUID.
LPTSTR AxisTypeToString(REFGUID axisTypeGUID)
{
    if (axisTypeGUID == GUID_XAxis)
        return _T("X Axis");
    if (axisTypeGUID == GUID_YAxis)
        return _T("Y Axis");
    if (axisTypeGUID == GUID_ZAxis)
        return _T("Z Axis");
    if (axisTypeGUID == GUID_RxAxis)
        return _T("X Rotation");
    if (axisTypeGUID == GUID_RyAxis)
        return _T("Y Rotation");
    if (axisTypeGUID == GUID_RzAxis)
        return _T("Z Rotation");

    return _T("Unknown Axis");
}

// Given an array of offsets and a count, checks that they are all unset (FALSE).
// If they are all unset, sets them (to TRUE) and returns TRUE.
// Otherwise, leaves them alone and returns FALSE.
static BOOL CheckAndSetOffsets(BOOL* base, const DWORD count)
{
    for (DWORD i = 0; i < count; ++i)
        if (base[i] != FALSE) return FALSE;

    for (DWORD i = 0; i < count; ++i)
        base[i] = TRUE;

    return TRUE;
}

// ---------

// Given an instance type, list of instances that are used, number of instances in total, and a desired instance to select, attempts to select that instance.
// Checks that the specified instance (by index) is currently unset (FALSE) and, if so, sets it (to TRUE).
// If this operation succeeds, makes and returns an instance identifier using the type and index.
// Otherwise, returns -1 cast to an instance identifier type.
static TInstance SelectInstance(const EInstanceType instanceType, BOOL* instanceUsed, const TInstanceCount instanceCount, const TInstanceIdx instanceToSelect)
{
    TInstance selectedInstance = (TInstance)-1;

    if ((instanceToSelect < instanceCount) && (FALSE == instanceUsed[instanceToSelect]))
    {
        instanceUsed[instanceToSelect] = TRUE;
        selectedInstance = Base::MakeInstanceIdentifier(instanceType, instanceToSelect);
    }
    
    return selectedInstance;
}


// -------- INSTANCE METHODS ----------------------------------------------- //
// See "Mapper/Base.h" for documentation.

HRESULT Base::EnumerateMappedObjects(LPDIENUMDEVICEOBJECTSCALLBACK appCallback, LPVOID appCbParam, DWORD enumerationFlags)
{
    // Obtain the number of objects of each type.
    const TInstanceCount numAxes = NumInstancesOfType(EInstanceType::InstanceTypeAxis);
    const TInstanceCount numPov = NumInstancesOfType(EInstanceType::InstanceTypePov);
    const TInstanceCount numButtons = NumInstancesOfType(EInstanceType::InstanceTypeButton);
    
    // If requested, enumerate axes.
    if (DIDFT_ALL == enumerationFlags || enumerationFlags & DIDFT_AXIS)
    {
        const DWORD baseOffset = 0;
        
        for (TInstanceCount i = 0; i < numAxes; ++i)
        {
            // Allocate and fill a structure to submit to the application, using the heap for security purposes.
            DIDEVICEOBJECTINSTANCE* objectDescriptor = new DIDEVICEOBJECTINSTANCE();
            ZeroMemory(objectDescriptor, sizeof(*objectDescriptor));

            objectDescriptor->dwSize = sizeof(*objectDescriptor);
            objectDescriptor->guidType = AxisTypeFromInstanceNumber((TInstanceIdx)i);
            objectDescriptor->dwOfs = baseOffset + (i * SizeofInstance(EInstanceType::InstanceTypeAxis));
            objectDescriptor->dwType = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i);
            objectDescriptor->dwFlags = 0;
            _tcscpy_s(objectDescriptor->tszName, _countof(objectDescriptor->tszName), AxisTypeToString(objectDescriptor->guidType));

            // Submit the button to the application.
            BOOL appResponse = appCallback(objectDescriptor, appCbParam);

            // Free previously-allocated memory.
            delete objectDescriptor;

            // See if the application requested that the enumeration stop and, if so, honor that request
            switch (appResponse)
            {
            case DIENUM_CONTINUE:
                break;
            case DIENUM_STOP:
                return DI_OK;
            default:
                return DIERR_INVALIDPARAM;
            }
        }
    }

    // If requested, enumerate POVs.
    if (DIDFT_ALL == enumerationFlags || enumerationFlags & DIDFT_POV)
    {
        const DWORD baseOffset = (numAxes * SizeofInstance(EInstanceType::InstanceTypeAxis));

        for (TInstanceCount i = 0; i < numPov; ++i)
        {
            // Allocate and fill a structure to submit to the application, using the heap for security purposes.
            DIDEVICEOBJECTINSTANCE* objectDescriptor = new DIDEVICEOBJECTINSTANCE();
            ZeroMemory(objectDescriptor, sizeof(*objectDescriptor));

            objectDescriptor->dwSize = sizeof(*objectDescriptor);
            objectDescriptor->guidType = GUID_POV;
            objectDescriptor->dwOfs = baseOffset + (i * SizeofInstance(EInstanceType::InstanceTypePov));
            objectDescriptor->dwType = DIDFT_POV | DIDFT_MAKEINSTANCE(i);
            objectDescriptor->dwFlags = 0;
            _stprintf_s(objectDescriptor->tszName, _countof(objectDescriptor->tszName), _T("POV %u"), (unsigned)i);

            // Submit the button to the application.
            BOOL appResponse = appCallback(objectDescriptor, appCbParam);
            
            // Free previously-allocated memory.
            delete objectDescriptor;

            // See if the application requested that the enumeration stop and, if so, honor that request
            switch (appResponse)
            {
            case DIENUM_CONTINUE:
                break;
            case DIENUM_STOP:
                return DI_OK;
            default:
                return DIERR_INVALIDPARAM;
            }
        }
    }

    // If requested, enumerate buttons.
    if (DIDFT_ALL == enumerationFlags || enumerationFlags & DIDFT_BUTTON)
    {
        const DWORD baseOffset = (numAxes * SizeofInstance(EInstanceType::InstanceTypeAxis)) + (numPov * SizeofInstance(EInstanceType::InstanceTypePov));

        for (TInstanceCount i = 0; i < numButtons; ++i)
        {
            // Allocate and fill a structure to submit to the application, using the heap for security purposes.
            DIDEVICEOBJECTINSTANCE* objectDescriptor = new DIDEVICEOBJECTINSTANCE();
            ZeroMemory(objectDescriptor, sizeof(*objectDescriptor));

            objectDescriptor->dwSize = sizeof(*objectDescriptor);
            objectDescriptor->guidType = GUID_Button;
            objectDescriptor->dwOfs = baseOffset + (i * SizeofInstance(EInstanceType::InstanceTypeButton));
            objectDescriptor->dwType = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(i);
            objectDescriptor->dwFlags = 0;
            _stprintf_s(objectDescriptor->tszName, _countof(objectDescriptor->tszName), _T("Button %u"), (unsigned)i);

            // Submit the button to the application.
            BOOL appResponse = appCallback(objectDescriptor, appCbParam);

            // Free previously-allocated memory.
            delete objectDescriptor;

            // See if the application requested that the enumeration stop and, if so, honor that request
            switch (appResponse)
            {
            case DIENUM_CONTINUE:
                break;
            case DIENUM_STOP:
                return DI_OK;
            default:
                return DIERR_INVALIDPARAM;
            }
        }
    }
    
    return DI_OK;
}

// ---------

void Base::FillDeviceCapabilities(LPDIDEVCAPS lpDIDevCaps)
{
    lpDIDevCaps->dwAxes = (DWORD)NumInstancesOfType(EInstanceType::InstanceTypeAxis);
    lpDIDevCaps->dwButtons = (DWORD)NumInstancesOfType(EInstanceType::InstanceTypeButton);
    lpDIDevCaps->dwPOVs = (DWORD)NumInstancesOfType(EInstanceType::InstanceTypePov);
}

// ---------

BOOL Base::IsApplicationDataFormatSet(void)
{
    return mapsValid;
}

// ---------

BOOL Base::IsPropertyHandledByMapper(REFGUID guidProperty)
{
    BOOL propertyHandled = FALSE;

    if (guidProperty == DIPROP_AXISMODE || guidProperty == DIPROP_DEADZONE || guidProperty == DIPROP_RANGE || guidProperty == DIPROP_SATURATION)
        propertyHandled = TRUE;
    
    return propertyHandled;
}

// ---------

HRESULT Base::SetApplicationDataFormat(LPCDIDATAFORMAT lpdf)
{
    // Obtain the number of instances of each type in the mapping by asking the subclass.
    const TInstanceCount numButtons = NumInstancesOfType(EInstanceType::InstanceTypeButton);
    const TInstanceCount numAxes = NumInstancesOfType(EInstanceType::InstanceTypeAxis);
    const TInstanceCount numPov = NumInstancesOfType(EInstanceType::InstanceTypePov);
    
    // Track the next unused instance of each, essentially allowing a "dequeue" operation when the application does not specify a specific instance.
    TInstanceIdx nextUnusedButton = 0;
    TInstanceIdx nextUnusedAxis = 0;
    TInstanceIdx nextUnusedPov = 0;
    
    // Keep track of which instances were added to the mapping of each type as well as each offset.
    // It is an error to specify an instance multiple times, specify a non-existant instance, or specify multiple pieces of information at the same offset.
    BOOL* buttonUsed = new BOOL[numButtons];
    BOOL* axisUsed = new BOOL[numAxes];
    BOOL* povUsed = new BOOL[numPov];
    BOOL* offsetUsed = new BOOL[lpdf->dwDataSize];
    for (TInstanceCount i = 0; i < numButtons; ++i) buttonUsed[i] = FALSE;
    for (TInstanceCount i = 0; i < numAxes; ++i) axisUsed[i] = FALSE;
    for (TInstanceCount i = 0; i < numPov; ++i) povUsed[i] = FALSE;
    for (DWORD i = 0; i < lpdf->dwDataSize; ++i) offsetUsed[i] = FALSE;

    // Initialize the maps by clearing them and marking them invalid
    instanceToOffset.clear();
    offsetToInstance.clear();
    mapsValid = FALSE;
    
    // Iterate over each of the object specifications provided by the application.
    for (DWORD i = 0; i < lpdf->dwNumObjs; ++i)
    {
        LPDIOBJECTDATAFORMAT dataFormat = &lpdf->rgodf[i];
        BOOL invalidParamsDetected = FALSE;

        // Extract information about the instance specified by the application.
        // If any instance is allowed, the specific instance is irrelevant.
        const BOOL allowAnyInstance = ((dataFormat->dwType & DIDFT_INSTANCEMASK) == DIDFT_ANYINSTANCE);
        const TInstanceIdx specificInstance = (TInstanceIdx)DIDFT_GETINSTANCE(dataFormat->dwType);

        if ((dataFormat->dwType & DIDFT_ABSAXIS) && (nextUnusedAxis < numAxes))
        {
            // Pick an axis

            // First check the offsets for overlap with something previously selected
            if (FALSE == CheckAndSetOffsets(&offsetUsed[dataFormat->dwOfs], SizeofInstance(EInstanceType::InstanceTypeAxis)))
                invalidParamsDetected = TRUE;
            else
            {
                if (NULL == dataFormat->pguid)
                {
                    // Any axis type allowed

                    const TInstanceIdx instanceToSelect = allowAnyInstance ? nextUnusedAxis : specificInstance;
                    const TInstance selectedInstance = SelectInstance(EInstanceType::InstanceTypeAxis, axisUsed, numAxes, instanceToSelect);

                    if (selectedInstance > 0)
                    {
                        // Instance was selected successfully, add a mapping
                        instanceToOffset.insert({ selectedInstance, dataFormat->dwOfs });
                        offsetToInstance.insert({ dataFormat->dwOfs, selectedInstance });
                    }
                    else if (!allowAnyInstance)
                    {
                        // Instance was unable to be selected, and a specific instance was specified, so this is an error
                        invalidParamsDetected = TRUE;
                    }
                }
                else
                {
                    // Specific axis type required

                    if (0 != AxisTypeCount(*dataFormat->pguid))
                    {
                        // Axis type exists in the mapping

                        if (allowAnyInstance)
                        {
                            // Any instance allowed, so find the first of this type that is unused, if any
                            TInstanceIdx instanceIndex = 0;
                            TInstanceIdx axisIndex = AxisInstanceIndex(*dataFormat->pguid, instanceIndex++);
                            TInstance selectedInstance = SelectInstance(EInstanceType::InstanceTypeAxis, axisUsed, numAxes, axisIndex);

                            while (selectedInstance < 0 && axisIndex >= 0)
                            {
                                axisIndex = AxisInstanceIndex(*dataFormat->pguid, instanceIndex++);
                                selectedInstance = SelectInstance(EInstanceType::InstanceTypeAxis, axisUsed, numAxes, axisIndex);
                            }

                            if (selectedInstance >= 0)
                            {
                                // Unused instance found, create a mapping
                                instanceToOffset.insert({ MakeInstanceIdentifier(EInstanceType::InstanceTypeAxis, axisIndex), dataFormat->dwOfs });
                                offsetToInstance.insert({ dataFormat->dwOfs, MakeInstanceIdentifier(EInstanceType::InstanceTypeAxis, axisIndex) });
                            }
                        }
                        else
                        {
                            // Specific instance required, so check if it is available
                            TInstanceIdx axisIndex = AxisInstanceIndex(*dataFormat->pguid, specificInstance);

                            if (axisIndex >= 0 && FALSE == axisUsed[axisIndex])
                            {
                                // Axis available, use it
                                axisUsed[axisIndex] = TRUE;
                                instanceToOffset.insert({ MakeInstanceIdentifier(EInstanceType::InstanceTypeAxis, axisIndex), dataFormat->dwOfs });
                                offsetToInstance.insert({ dataFormat->dwOfs, MakeInstanceIdentifier(EInstanceType::InstanceTypeAxis, axisIndex) });
                            }
                            else
                            {
                                // Axis unavailable, this is an error
                                invalidParamsDetected = TRUE;
                            }
                        }
                    }
                    else
                    {
                        // Axis type does not exist in the mapping

                        if (!allowAnyInstance)
                        {
                            // Specified an instance of a non-existant axis, this is an error
                            invalidParamsDetected = TRUE;
                        }
                    }
                }
            }
        }
        else if ((dataFormat->dwType & DIDFT_PSHBUTTON) && (nextUnusedButton < numButtons))
        {
            // Pick a button

            if (NULL == dataFormat->pguid || IsEqualGUID(GUID_Button, *dataFormat->pguid))
            {
                // Type unspecified or specified as a button

                const TInstanceIdx instanceToSelect = allowAnyInstance ? nextUnusedButton : specificInstance;
                const TInstance selectedInstance = SelectInstance(EInstanceType::InstanceTypeButton, buttonUsed, numButtons, instanceToSelect);

                if (selectedInstance > 0)
                {
                    // Instance was selected successfully, add a mapping
                    instanceToOffset.insert({ selectedInstance, dataFormat->dwOfs });
                    offsetToInstance.insert({ dataFormat->dwOfs, selectedInstance });
                }
                else if (!allowAnyInstance)
                {
                    // Instance was unable to be selected, and a specific instance was specified, so this is an error
                    invalidParamsDetected = TRUE;
                }
            }
            else
            {
                // Type specified as a non-button, this is an error
                invalidParamsDetected = TRUE;
            }

        }
        else if ((dataFormat->dwType & DIDFT_POV) && (nextUnusedPov < numPov))
        {
            // Pick a POV

            if (NULL == dataFormat->pguid || IsEqualGUID(GUID_POV, *dataFormat->pguid))
            {
                // Type unspecified or specified as a POV

                const TInstanceIdx instanceToSelect = allowAnyInstance ? nextUnusedPov : specificInstance;
                const TInstance selectedInstance = SelectInstance(EInstanceType::InstanceTypePov, povUsed, numPov, instanceToSelect);

                if (selectedInstance > 0)
                {
                    // Instance was selected successfully, add a mapping
                    instanceToOffset.insert({ selectedInstance, dataFormat->dwOfs });
                    offsetToInstance.insert({ dataFormat->dwOfs, selectedInstance });
                }
                else if (!allowAnyInstance)
                {
                    // Instance was unable to be selected, and a specific instance was specified, so this is an error
                    invalidParamsDetected = TRUE;
                }
            }
            else
            {
                // Type specified as a non-POV, this is an error
                invalidParamsDetected = TRUE;
            }

        }
        else if (allowAnyInstance)
        {
            // No objects available, but no instance specified, so do not do anything this iteration
        }
        else
        {
            // An instance was specified of an object that is not available, this is an error
            invalidParamsDetected = TRUE;
        }

        // Bail in the event of an error
        if (invalidParamsDetected)
        {
            delete[] buttonUsed;
            delete[] axisUsed;
            delete[] povUsed;
            delete[] offsetUsed;

            return DIERR_INVALIDPARAM;
        }

        // Increment all next-unused indices
        while (TRUE == axisUsed[nextUnusedAxis] && nextUnusedAxis < numAxes) nextUnusedAxis += 1;
        while (TRUE == buttonUsed[nextUnusedButton] && nextUnusedButton < numButtons) nextUnusedButton += 1;
        while (TRUE == povUsed[nextUnusedPov] && nextUnusedPov < numPov) nextUnusedPov += 1;
    }
    
    delete[] buttonUsed;
    delete[] axisUsed;
    delete[] povUsed;
    delete[] offsetUsed;

    mapsValid = TRUE;
    return S_OK;
}

// ---------

void Base::ResetApplicationDataFormat(void)
{
    instanceToOffset.clear();
    offsetToInstance.clear();

    mapsValid = FALSE;
}
