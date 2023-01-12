/*****************************************************************************
 * Xidi
 *   DirectInput interface for XInput controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016-2022
 *************************************************************************//**
 * @file ControllerIdentification.cpp
 *   Implementation of functions for identifying and enumerating Xidi virtual
 *   controllers in the context of DirectInput.
 *****************************************************************************/

#include "ApiBitSet.h"
#include "ApiDirectInput.h"
#include "Configuration.h"
#include "ControllerIdentification.h"
#include "ControllerTypes.h"
#include "Globals.h"
#include "Mapper.h"
#include "Message.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <array>
#include <memory>
#include <optional>


namespace Xidi
{
    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Determines if the specified controller supports force feedback.
    /// @param [in] controllerId Identifier of the controller of interest.
    /// @return `true` if so, `false` otherwise.
    static inline bool DoesControllerSupportForceFeedback(Controller::TControllerIdentifier controllerId)
    {
        const Controller::Mapper* const mapper = Controller::Mapper::GetConfigured((Controller::TControllerIdentifier)controllerId);
        if (nullptr == mapper)
            return false;

        return mapper->GetCapabilities().ForceFeedbackIsSupported();
    }

    /// Extracts and returns the instance index from a Xidi virtual controller's GUID.
    /// Does not verify that the supplied GUID actually represents an XInput instance GUID.
    /// @param [in] xguid Xidi virtual controller's instance GUID.
    /// @return Instance index (a.k.a. XInput player number).
    static inline Controller::TControllerIdentifier ExtractVirtualControllerInstanceFromGuid(REFGUID xguid)
    {
        return (Controller::TControllerIdentifier)((xguid.Data1 >> 16) & 0x0000ffff) - (Controller::TControllerIdentifier)VirtualControllerProductId(0);
    }


    // -------- FUNCTIONS -------------------------------------------------- //
    // See "ControllerIdentification.h" for documentation.

    std::optional<bool> ApproximatelyEqualVendorAndProductId(std::wstring_view controllerStringA, std::wstring_view controllerStringB)
    {
        static constexpr std::wstring_view kVendorIdPrefix = L"VID";
        static constexpr std::wstring_view kProductIdPrefix = L"PID";

        TemporaryVector<std::wstring_view> piecesA = Strings::SplitString(controllerStringA, {L"_", L"&", L"#"});
        TemporaryVector<std::wstring_view> piecesB = Strings::SplitString(controllerStringB, {L"_", L"&", L"#"});

        std::wstring_view vendorIdA, vendorIdB, productIdA, productIdB;
        
        for (size_t i = 0; i < (piecesA.Size() - 1); ++i)
        {
            if (true == Strings::EqualsCaseInsensitive(piecesA[i], kVendorIdPrefix))
                vendorIdA = piecesA[++i];
            else if (true == Strings::EqualsCaseInsensitive(piecesA[i], kProductIdPrefix))
                productIdA = piecesA[++i];
        }

        for (size_t i = 0; i < (piecesB.Size() - 1); ++i)
        {
            if (true == Strings::EqualsCaseInsensitive(piecesB[i], kVendorIdPrefix))
                vendorIdB = piecesB[++i];
            if (true == Strings::EqualsCaseInsensitive(piecesB[i], kProductIdPrefix))
                productIdB = piecesB[++i];
        }

        if (vendorIdA.empty() || vendorIdB.empty() || productIdA.empty() || productIdB.empty())
            return std::nullopt;

        if (false == Strings::EqualsCaseInsensitive(productIdA, productIdB))
            return false;

        if ((vendorIdA.length() == vendorIdB.length()) && (vendorIdA == vendorIdB))
            return Strings::EqualsCaseInsensitive(vendorIdA, vendorIdB);
        else if (vendorIdA.length() < vendorIdB.length())
            return Strings::EqualsCaseInsensitive(vendorIdA, vendorIdB.substr(vendorIdB.length() - vendorIdA.length()));
        else
            return Strings::EqualsCaseInsensitive(vendorIdA.substr(vendorIdA.length() - vendorIdB.length()), vendorIdB);
    }

    // --------

    template <typename EarliestIDirectInputType, typename EarliestIDirectInputDeviceType> bool DoesDirectInputControllerSupportXInput(EarliestIDirectInputType* dicontext, REFGUID instanceGUID, std::wstring* devicePath)
    {
        bool deviceSupportsXInput = false;

        EarliestIDirectInputDeviceType* didevice = nullptr;
        HRESULT result = dicontext->CreateDevice(instanceGUID, &didevice, nullptr);

        if (DI_OK == result)
        {
            // Get the GUID and device path of the DirectInput device.
            DIPROPGUIDANDPATH devinfo;
            ZeroMemory(&devinfo, sizeof(devinfo));

            devinfo.diph.dwHeaderSize = sizeof(devinfo.diph);
            devinfo.diph.dwSize = sizeof(devinfo);
            devinfo.diph.dwHow = DIPH_DEVICE;

            result = didevice->GetProperty(DIPROP_GUIDANDPATH, &devinfo.diph);

            if (DI_OK == result)
            {
                // The documented "best" way of determining if a device supports XInput is to look for "&IG_" in the device path string.
                if (nullptr != wcsstr(devinfo.wszPath, L"&IG_") || nullptr != wcsstr(devinfo.wszPath, L"&ig_"))
                {
                    deviceSupportsXInput = true;

                    if (nullptr != devicePath)
                        *devicePath = devinfo.wszPath;
                }
            }

            didevice->Release();
        }

        return deviceSupportsXInput;
    }

    template bool DoesDirectInputControllerSupportXInput<typename EarliestIDirectInputA, typename EarliestIDirectInputDeviceA>(EarliestIDirectInputA*, REFGUID, std::wstring*);
    template bool DoesDirectInputControllerSupportXInput<typename EarliestIDirectInputW, typename EarliestIDirectInputDeviceW>(EarliestIDirectInputW*, REFGUID, std::wstring*);

    // ---------

    template <typename DeviceInstanceType> BOOL EnumerateVirtualControllers(BOOL(FAR PASCAL* lpCallback)(const DeviceInstanceType*, LPVOID), LPVOID pvRef, bool forceFeedbackRequired)
    {
        std::unique_ptr<DeviceInstanceType> instanceInfo = std::make_unique<DeviceInstanceType>();
        uint32_t numControllersToEnumerate = Controller::kPhysicalControllerCount;

        const uint64_t kActiveVirtualControllerMask = Globals::GetConfigurationData().GetFirstIntegerValue(Strings::kStrConfigurationSectionWorkarounds, Strings::kStrConfigurationSettingWorkaroundsActiveVirtualControllerMask).value_or(UINT64_MAX);

        for (uint32_t idx = 0; idx < numControllersToEnumerate; ++idx)
        {
            if ((true == forceFeedbackRequired) && (false == DoesControllerSupportForceFeedback(idx)))
                continue;

            *instanceInfo = {.dwSize = sizeof(*instanceInfo)};
            FillVirtualControllerInfo(*instanceInfo, idx);

            if (0 != (kActiveVirtualControllerMask & ((uint64_t)1 << idx)))
            {
                Message::OutputFormatted(Message::ESeverity::Info, L"Enumerate: Presenting Xidi virtual controller %u to the application.", (1 + idx));

                if (DIENUM_CONTINUE != lpCallback(instanceInfo.get(), pvRef))
                    return DIENUM_STOP;
            }
        }

        return DIENUM_CONTINUE;
    }

    template BOOL EnumerateVirtualControllers(LPDIENUMDEVICESCALLBACKA, LPVOID, bool);
    template BOOL EnumerateVirtualControllers(LPDIENUMDEVICESCALLBACKW, LPVOID, bool);

    // ---------

    template <typename DeviceInstanceType> void FillVirtualControllerInfo(DeviceInstanceType& instanceInfo, Controller::TControllerIdentifier controllerId)
    {
        instanceInfo.guidInstance = VirtualControllerGuid(controllerId);
        instanceInfo.guidProduct = VirtualControllerGuid(controllerId);
        instanceInfo.dwDevType = DINPUT_DEVTYPE_XINPUT_GAMEPAD;
        FillVirtualControllerName(instanceInfo.tszInstanceName, _countof(instanceInfo.tszInstanceName), controllerId);
        FillVirtualControllerName(instanceInfo.tszProductName, _countof(instanceInfo.tszProductName), controllerId);

        // DirectInput versions 5 and higher include extra members in this structure, and this is indicated on input using the size member of the structure.
        if (instanceInfo.dwSize > offsetof(DeviceInstanceType, tszProductName) + sizeof(DeviceInstanceType::tszProductName))
        {
            if (true == DoesControllerSupportForceFeedback(controllerId))
                instanceInfo.guidFFDriver = kVirtualControllerForceFeedbackDriverGuid;
            else
                instanceInfo.guidFFDriver = {};

            // These fields are zeroed out because Xidi does not currently offer any of the functionality they represent.
            instanceInfo.wUsagePage = 0;
            instanceInfo.wUsage = 0;
        }
    }

    template void FillVirtualControllerInfo(DIDEVICEINSTANCEA&, Controller::TControllerIdentifier);
    template void FillVirtualControllerInfo(DIDEVICEINSTANCEW&, Controller::TControllerIdentifier);

    // ---------

    template <> int FillVirtualControllerName<LPSTR>(LPSTR buf, size_t bufcount, Controller::TControllerIdentifier controllerIndex)
    {
        TemporaryBuffer<CHAR> xidiControllerNameFormatString;
        LoadStringA(Globals::GetInstanceHandle(), IDS_XIDI_CONTROLLERIDENTIFICATION_CONTROLLER_NAME_FORMAT, xidiControllerNameFormatString.Data(), xidiControllerNameFormatString.Capacity());

        return sprintf_s(buf, bufcount, (LPCSTR)xidiControllerNameFormatString.Data(), (controllerIndex + 1));
    }

    template <> int FillVirtualControllerName<LPWSTR>(LPWSTR buf, size_t bufcount, Controller::TControllerIdentifier controllerIndex)
    {
        TemporaryBuffer<WCHAR> xidiControllerNameFormatString;
        LoadStringW(Globals::GetInstanceHandle(), IDS_XIDI_CONTROLLERIDENTIFICATION_CONTROLLER_NAME_FORMAT, xidiControllerNameFormatString.Data(), xidiControllerNameFormatString.Capacity());

        return swprintf_s(buf, bufcount, (LPCWSTR)xidiControllerNameFormatString.Data(), (controllerIndex + 1));
    }

    // ---------

    template <typename StringType> int FillVirtualControllerPath(StringType buf, size_t bufcount, Controller::TControllerIdentifier controllerId)
    {
        // Paths are not currently meaningful, so just a single null character is used to indicate an empty string path.
        if (bufcount > 0)
        {
            buf[0] = 0;
            return 1;
        }

        return 0;
    }

    template int FillVirtualControllerPath(LPSTR buf, size_t bufcount, Controller::TControllerIdentifier controllerId);
    template int FillVirtualControllerPath(LPWSTR buf, size_t bufcount, Controller::TControllerIdentifier controllerId);

    // ---------

    std::optional<Controller::TControllerIdentifier> VirtualControllerIdFromInstanceGuid(REFGUID instanceGUID)
    {
        Controller::TControllerIdentifier xindex = ExtractVirtualControllerInstanceFromGuid(instanceGUID);

        if (xindex < Controller::kPhysicalControllerCount)
        {
            GUID realXInputGUID = VirtualControllerGuid(xindex);
            if (realXInputGUID == instanceGUID)
                return (Controller::TControllerIdentifier)xindex;
        }

        return std::nullopt;
    }
}
