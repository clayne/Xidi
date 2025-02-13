/***************************************************************************************************
 * Xidi
 *   DirectInput interface for XInput controllers.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016-2025
 ***********************************************************************************************//**
 * @file VirtualControllerTest.cpp
 *   Unit tests for virtual controller objects.
 **************************************************************************************************/

#include "VirtualController.h"

#include <bitset>
#include <cmath>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <memory>
#include <optional>

#include <Infra/Test/TestCase.h>

#include "ApiWindows.h"
#include "ControllerTypes.h"
#include "ElementMapper.h"
#include "ForceFeedbackDevice.h"
#include "MockPhysicalController.h"
#include "StateChangeEventBuffer.h"

namespace XidiTest
{
  using namespace ::Xidi;
  using ::Xidi::Controller::AxisMapper;
  using ::Xidi::Controller::ButtonMapper;
  using ::Xidi::Controller::EAxis;
  using ::Xidi::Controller::EButton;
  using ::Xidi::Controller::EElementType;
  using ::Xidi::Controller::EPhysicalButton;
  using ::Xidi::Controller::EPhysicalDeviceStatus;
  using ::Xidi::Controller::EPhysicalStick;
  using ::Xidi::Controller::EPhysicalTrigger;
  using ::Xidi::Controller::EPovDirection;
  using ::Xidi::Controller::Mapper;
  using ::Xidi::Controller::PovMapper;
  using ::Xidi::Controller::SPhysicalState;
  using ::Xidi::Controller::StateChangeEventBuffer;
  using ::Xidi::Controller::TControllerIdentifier;
  using ::Xidi::Controller::VirtualController;

  /// Axis to use when testing with a single axis.
  static constexpr EAxis kTestSingleAxis = EAxis::X;

  /// Test mapper for axis property tests. Contains a single axis.
  static const Mapper kTestSingleAxisMapper(
      {.stickLeftX = std::make_unique<AxisMapper>(kTestSingleAxis)});

  /// Test mapper used for larger controller state tests.
  /// Describes a virtual controller with 4 axes, 4 buttons, and a POV.
  /// Contains only a subset of the XInput controller elements.
  static const Mapper kTestMapper(
      {.stickLeftX = std::make_unique<AxisMapper>(EAxis::X),
       .stickLeftY = std::make_unique<AxisMapper>(EAxis::Y),
       .stickRightX = std::make_unique<AxisMapper>(EAxis::RotX),
       .stickRightY = std::make_unique<AxisMapper>(EAxis::RotY),
       .dpadUp = std::make_unique<PovMapper>(EPovDirection::Up),
       .dpadDown = std::make_unique<PovMapper>(EPovDirection::Down),
       .dpadLeft = std::make_unique<PovMapper>(EPovDirection::Left),
       .dpadRight = std::make_unique<PovMapper>(EPovDirection::Right),
       .buttonA = std::make_unique<ButtonMapper>(EButton::B1),
       .buttonB = std::make_unique<ButtonMapper>(EButton::B2),
       .buttonX = std::make_unique<ButtonMapper>(EButton::B3),
       .buttonY = std::make_unique<ButtonMapper>(EButton::B4)});

  /// Number of milliseconds to wait before declaring a timeout while waiting for a state change
  /// event to be signalled. Should be some small multiple of the approximate default system time
  /// slice length.
  static constexpr DWORD kTestStateChangeEventTimeoutMilliseconds = 100;

  /// Modifies a controller state object by applying to it an updated value contained within a state
  /// change event.
  /// @param [in] eventData State change event data.
  /// @param [in,out] controllerState Controller state object to be modified.
  static void ApplyUpdateToControllerState(
      const StateChangeEventBuffer::SEventData& eventData, Controller::SState& controllerState)
  {
    switch (eventData.element.type)
    {
      case EElementType::Axis:
        controllerState[eventData.element.axis] = eventData.value.axis;
        break;

      case EElementType::Button:
        controllerState[eventData.element.button] = eventData.value.button;
        break;

      case EElementType::Pov:
        controllerState.povDirection = eventData.value.povDirection;
        break;
    }
  }

  /// Creates a button set given a compile-time-constant list of buttons.
  /// @param [in] buttons Initializer list containing all of the desired buttons to be added to the
  /// set.
  /// @return Button set representation of the button list.
  static constexpr std::bitset<(int)EPhysicalButton::Count> ButtonSet(
      std::initializer_list<EPhysicalButton> buttons)
  {
    std::bitset<(int)EPhysicalButton::Count> buttonSet;

    for (auto button : buttons)
      buttonSet[(int)button] = true;

    return buttonSet;
  }

  /// Computes and returns the deadzone value that corresponds to the specified percentage of an
  /// axis' physical range of motion.
  /// @param [in] pct Desired percentage.
  /// @return Corresponding deadzone value.
  static constexpr uint32_t DeadzoneValueByPercentage(uint32_t pct)
  {
    return ((VirtualController::kAxisDeadzoneMax - VirtualController::kAxisDeadzoneMin) * pct) /
        100;
  }

  /// Computes and returns the saturation value that corresponds to the specified percentage of an
  /// axis' physical range of motion.
  /// @param [in] pct Desired percentage.
  /// @return Corresponding saturation value.
  static constexpr uint32_t SaturationValueByPercentage(uint32_t pct)
  {
    return ((VirtualController::kAxisSaturationMax - VirtualController::kAxisSaturationMin) * pct) /
        100;
  }

  /// Helper function for performing the boilerplate operations needed to ask a virtual controller
  /// object to apply axis properties to an input axis value and retrieve the result.
  /// @param [in] controller Virtual controller object pre-configured with a mapper and the right
  /// axis properties for the test.
  /// @param [in] inputAxisValue Axis value to provide as input.
  /// @return Result obtained from the virtual controller transforming the input axis values using
  /// the properties with which it was previously configured.
  static int32_t GetAxisPropertiesApplyResult(
      const VirtualController& controller, int32_t inputAxisValue)
  {
    Controller::SState controllerState;
    ZeroMemory(&controllerState, sizeof(controllerState));
    controllerState[kTestSingleAxis] = inputAxisValue;

    controller.ApplyProperties(controllerState);
    return controllerState[kTestSingleAxis];
  }

  /// Main test body for all axis property tests.
  /// Axis properties are deadzone, range, and saturation. The net result is to divide the expected
  /// output values into 5 regions. Region 1 is the negative saturation region, from extreme
  /// negative to the negative saturation cutoff point, in which output values are always the
  /// configured range minimum. Region 2 is the negative axis region, from negative saturation
  /// cutoff to negative deadzone cutoff, in which output values steadily progress from configured
  /// range minimum to neutral. Region 3 is the deadzone region, from negative deadzone cutoff to
  /// positive deadzone cutoff, in which output values are always the configured range neutral.
  /// Region 4 is the positive axis region, from positive deadzone cutoff to positive saturation
  /// cutoff, in which output values steadily progress from configured range neutral to maximum.
  /// Region 5 is the positive saturation region, from positive saturation cutoff to extreme
  /// positive, in which output values are always the configured range maximum. Throughout the test
  /// monotonicity of the axis output is also verified. See DirectInput documentation for more
  /// information on how properties work, which in turn covers allowed values for the parameters.
  /// @param [in] rangeMin Minimum range value with which to configure the axis.
  /// @param [in] rangeMax Maximum range value with which to configure the axis.
  /// @param [in] deadzone Deadzone value with which to configure the axis, expressed as a
  /// proportion of the axis' physical range of motion.
  /// @param [in] saturation Saturation value with which to configure the axis, expressed as a
  /// proportion of the axis' physical range of motion.
  static void TestVirtualControllerApplyAxisProperties(
      int32_t rangeMin,
      int32_t rangeMax,
      uint32_t deadzone = VirtualController::kAxisDeadzoneMin,
      uint32_t saturation = VirtualController::kAxisSaturationMax)
  {
    const int32_t rangeNeutral = ((rangeMin + rangeMax) / 2);

    // Cutoff points between regions.
    const int32_t rawSaturationCutoffNegative = Controller::kAnalogValueNeutral +
        ((int32_t)((double)(Controller::kAnalogValueMin - Controller::kAnalogValueNeutral) *
                   ((double)saturation / (double)VirtualController::kAxisSaturationMax)));
    const int32_t rawDeadzoneCutoffNegative = Controller::kAnalogValueNeutral +
        ((int32_t)((double)(Controller::kAnalogValueMin - Controller::kAnalogValueNeutral) *
                   ((double)deadzone / (double)VirtualController::kAxisDeadzoneMax)));
    const int32_t rawDeadzoneCutoffPositive = Controller::kAnalogValueNeutral +
        ((int32_t)((double)(Controller::kAnalogValueMax - Controller::kAnalogValueNeutral) *
                   ((double)deadzone / (double)VirtualController::kAxisDeadzoneMax)));
    const int32_t rawSaturationCutoffPositive = Controller::kAnalogValueNeutral +
        ((int32_t)((double)(Controller::kAnalogValueMax - Controller::kAnalogValueNeutral) *
                   ((double)saturation / (double)VirtualController::kAxisSaturationMax)));

    // Output monotonicity check variable.
    int32_t lastOutputAxisValue = rangeMin;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(true == controller.SetAxisDeadzone(kTestSingleAxis, deadzone));
    TEST_ASSERT(true == controller.SetAxisRange(kTestSingleAxis, rangeMin, rangeMax));
    TEST_ASSERT(true == controller.SetAxisSaturation(kTestSingleAxis, saturation));
    TEST_ASSERT(controller.GetAxisDeadzone(kTestSingleAxis) == deadzone);
    TEST_ASSERT(controller.GetAxisRange(kTestSingleAxis) == std::make_pair(rangeMin, rangeMax));
    TEST_ASSERT(controller.GetAxisSaturation(kTestSingleAxis) == saturation);

    // Region 1
    for (int32_t inputAxisValue = Controller::kAnalogValueMin;
         inputAxisValue < rawSaturationCutoffNegative;
         ++inputAxisValue)
    {
      const int32_t expectedOutputAxisValue = rangeMin;
      const int32_t actualOutputAxisValue =
          GetAxisPropertiesApplyResult(controller, inputAxisValue);
      TEST_ASSERT(actualOutputAxisValue == expectedOutputAxisValue);
      TEST_ASSERT(actualOutputAxisValue >= lastOutputAxisValue);
      lastOutputAxisValue = actualOutputAxisValue;
    }

    // Region 2
    // Allow for a small amount of mathematical imprecision by checking for an absolute value
    // difference instead of equality.
    for (int32_t inputAxisValue = rawSaturationCutoffNegative;
         inputAxisValue < rawDeadzoneCutoffNegative;
         ++inputAxisValue)
    {
      const double regionStepSize = (double)(rangeNeutral - rangeMin) /
          (double)(rawDeadzoneCutoffNegative - rawSaturationCutoffNegative);
      const double expectedOutputAxisValue = (double)rangeMin +
          ((double)(inputAxisValue - rawSaturationCutoffNegative) * regionStepSize);
      const int32_t actualOutputAxisValue =
          GetAxisPropertiesApplyResult(controller, inputAxisValue);
      TEST_ASSERT(abs(actualOutputAxisValue - expectedOutputAxisValue) <= 1.0);
      TEST_ASSERT(actualOutputAxisValue >= lastOutputAxisValue);
      lastOutputAxisValue = actualOutputAxisValue;
    }

    // Region 3
    for (int32_t inputAxisValue = rawDeadzoneCutoffNegative;
         inputAxisValue <= rawDeadzoneCutoffPositive;
         ++inputAxisValue)
    {
      const int32_t expectedOutputAxisValue = rangeNeutral;
      const int32_t actualOutputAxisValue =
          GetAxisPropertiesApplyResult(controller, inputAxisValue);
      TEST_ASSERT(actualOutputAxisValue == expectedOutputAxisValue);
      TEST_ASSERT(actualOutputAxisValue >= lastOutputAxisValue);
      lastOutputAxisValue = actualOutputAxisValue;
    }

    // Region 4
    // Allow for a small amount of mathematical imprecision by checking for an absolute value
    // difference instead of equality.
    for (int32_t inputAxisValue = (1 + rawDeadzoneCutoffPositive);
         inputAxisValue <= rawSaturationCutoffPositive;
         ++inputAxisValue)
    {
      const double regionStepSize = (double)(rangeMax - rangeNeutral) /
          (double)(rawSaturationCutoffPositive - rawDeadzoneCutoffPositive);
      const double expectedOutputAxisValue = (double)rangeNeutral +
          ((double)(inputAxisValue - rawDeadzoneCutoffPositive) * regionStepSize);
      const int32_t actualOutputAxisValue =
          GetAxisPropertiesApplyResult(controller, inputAxisValue);
      TEST_ASSERT(abs(actualOutputAxisValue - expectedOutputAxisValue) <= 1.0);
      TEST_ASSERT(actualOutputAxisValue >= lastOutputAxisValue);
      lastOutputAxisValue = actualOutputAxisValue;
    }

    // Region 5
    for (int32_t inputAxisValue = (1 + rawSaturationCutoffPositive);
         inputAxisValue <= Controller::kAnalogValueMax;
         ++inputAxisValue)
    {
      const int32_t expectedOutputAxisValue = rangeMax;
      const int32_t actualOutputAxisValue =
          GetAxisPropertiesApplyResult(controller, inputAxisValue);
      TEST_ASSERT(actualOutputAxisValue == expectedOutputAxisValue);
      TEST_ASSERT(actualOutputAxisValue >= lastOutputAxisValue);
      lastOutputAxisValue = actualOutputAxisValue;
    }
  }

  // Verifies that virtual controllers correctly retrieve and return their associated capabilities.
  TEST_CASE(VirtualController_GetCapabilities)
  {
    const Mapper* mappers[] = {&kTestSingleAxisMapper, &kTestMapper};

    for (auto mapper : mappers)
    {
      MockPhysicalController physicalController(0, *mapper);
      VirtualController controller(0);

      TEST_ASSERT(mapper->GetCapabilities() == controller.GetCapabilities());
    }
  }

  // Verifies that virtual controllers correctly fill in controller state structures based on data
  // received from XInput controllers. Each time the virtual controller queries XInput it gets a new
  // data packet.
  TEST_CASE(VirtualController_GetState_Nominal)
  {
    constexpr TControllerIdentifier kControllerIndex = 2;
    constexpr SPhysicalState kPhysicalStates[] = {
        {.deviceStatus = EPhysicalDeviceStatus::Ok, .button = ButtonSet({EPhysicalButton::A})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok, .button = ButtonSet({EPhysicalButton::B})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok, .button = ButtonSet({EPhysicalButton::X})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok, .button = ButtonSet({EPhysicalButton::Y})}};

    // Button assignments are based on the mapper defined at the top of this file.
    constexpr Controller::SState kExpectedStates[] = {
        {.button = 0b0001}, // A
        {.button = 0b0010}, // B
        {.button = 0b0100}, // X
        {.button = 0b1000}, // Y
    };

    MockPhysicalController physicalController(kControllerIndex, kTestMapper);
    VirtualController controller(kControllerIndex);
    controller.SetAllAxisRange(Controller::kAnalogValueMin, Controller::kAnalogValueMax);

    for (int i = 0; i < _countof(kExpectedStates); ++i)
    {
      controller.RefreshState(
          kTestMapper.MapStatePhysicalToVirtual(kPhysicalStates[i], kControllerIndex));

      const Controller::SState actualState = controller.GetState();
      TEST_ASSERT(actualState == kExpectedStates[i]);
    }
  }

  // Verifies that virtual controllers report everything neutral when no controller input is
  // provided and no properties have been set. In this test case no physical state has been supplied
  // to the virtual controller.
  TEST_CASE(VirtualController_GetState_InitialDefault)
  {
    constexpr TControllerIdentifier kControllerIndex = 1;

    constexpr int32_t kExpectedNeutralAnalogValue =
        (VirtualController::kRangeMinDefault + VirtualController::kRangeMaxDefault) / 2;
    constexpr Controller::SState kExpectedState = {
        .axis = {
            kExpectedNeutralAnalogValue,
            kExpectedNeutralAnalogValue,
            0,
            kExpectedNeutralAnalogValue,
            kExpectedNeutralAnalogValue,
            0}};

    MockPhysicalController physicalController(kControllerIndex, kTestMapper);
    VirtualController controller(kControllerIndex);

    const Controller::SState actualState = controller.GetState();
    TEST_ASSERT(actualState == kExpectedState);
  }

  // Verifies that virtual controllers correctly fill in controller state structures based on data
  // received from XInput controllers. Each time the virtual controller queries XInput it gets the
  // same data packet.
  TEST_CASE(VirtualController_GetState_SameState)
  {
    constexpr TControllerIdentifier kControllerIndex = 3;
    constexpr SPhysicalState kPhysicalState = {
        .deviceStatus = EPhysicalDeviceStatus::Ok,
        .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::X})};

    // Button assignments are based on the mapper defined at the top of this file.
    constexpr Controller::SState kExpectedStates[] = {
        {.button = 0b0101}, // A, X
        {.button = 0b0101}, // A, X
        {.button = 0b0101}, // A, X
        {.button = 0b0101}  // A, X
    };

    MockPhysicalController physicalController(kControllerIndex, kTestMapper);
    VirtualController controller(kControllerIndex);

    controller.SetAllAxisRange(Controller::kAnalogValueMin, Controller::kAnalogValueMax);

    for (const auto& expectedState : kExpectedStates)
    {
      controller.RefreshState(
          kTestMapper.MapStatePhysicalToVirtual(kPhysicalState, kControllerIndex));

      const Controller::SState actualState = controller.GetState();
      TEST_ASSERT(actualState == expectedState);
    }
  }

  // Verifies that virtual controllers are correctly reported as being completely neutral when an
  // XInput error occurs.
  TEST_CASE(VirtualController_GetState_XInputErrorMeansNeutral)
  {
    constexpr TControllerIdentifier kControllerIndex = 1;

    // It is not obvious from documentation how packet numbers are supposed to behave across error
    // conditions. Nominal case is packet number increases, and the other two possibilities are
    // packet number stays the same or decreases. All three are tested below in that order.
    constexpr SPhysicalState kPhysicalStates[] = {
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::Y})},
        {.deviceStatus = EPhysicalDeviceStatus::NotConnected},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::Y})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::B, EPhysicalButton::Y})},
        {.deviceStatus = EPhysicalDeviceStatus::Error},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::B, EPhysicalButton::Y})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::X, EPhysicalButton::Y})},
        {.deviceStatus = EPhysicalDeviceStatus::Error},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::X, EPhysicalButton::Y})}};

    // When XInput calls fail, the controller state should be completely neutral.
    // Button assignments are based on the mapper defined at the top of this file.
    constexpr Controller::SState kExpectedStates[] = {
        {.button = 0b1001}, // A, Y
        {},
        {.button = 0b1001}, // A, Y
        {.button = 0b1010}, // B, Y
        {},
        {.button = 0b1010}, // B, Y
        {.button = 0b1100}, // X, Y
        {},
        {.button = 0b1100} // X, Y
    };

    MockPhysicalController physicalController(kControllerIndex, kTestMapper);
    VirtualController controller(kControllerIndex);

    controller.SetAllAxisRange(Controller::kAnalogValueMin, Controller::kAnalogValueMax);

    for (int i = 0; i < _countof(kExpectedStates); ++i)
    {
      controller.RefreshState(
          kTestMapper.MapStatePhysicalToVirtual(kPhysicalStates[i], kControllerIndex));

      const Controller::SState actualState = controller.GetState();
      TEST_ASSERT(actualState == kExpectedStates[i]);
    }
  }

  // Verifies that attempting to obtain a controller lock results in an object that does, in fact,
  // own the mutex with which it is associated.
  TEST_CASE(VirtualController_Lock)
  {
    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    auto lock = controller.Lock();
    TEST_ASSERT(true == lock.owns_lock());
  }

  // Nominal case. Default property values.
  TEST_CASE(VirtualController_ApplyAxisProperties_Nominal)
  {
    TestVirtualControllerApplyAxisProperties(
        Controller::kAnalogValueMin,
        Controller::kAnalogValueMax,
        VirtualController::kAxisDeadzoneMin,
        VirtualController::kAxisSaturationMax);
  }

  // Deadzone sweep in increments of 5%, no saturation.
  TEST_CASE(VirtualController_ApplyAxisProperties_Deadzone)
  {
    constexpr int32_t kDeadzoneIncrement = DeadzoneValueByPercentage(5);

    for (uint32_t deadzone = VirtualController::kAxisDeadzoneMin;
         deadzone <= VirtualController::kAxisDeadzoneMax;
         deadzone += kDeadzoneIncrement)
      TestVirtualControllerApplyAxisProperties(
          Controller::kAnalogValueMin,
          Controller::kAnalogValueMax,
          deadzone,
          VirtualController::kAxisSaturationMax);
  }

  // Saturation sweep in increments of 5%, no deadzone.
  TEST_CASE(VirtualController_ApplyAxisProperties_Saturation)
  {
    constexpr int32_t kSaturationIncrement = SaturationValueByPercentage(5);

    for (uint32_t saturation = VirtualController::kAxisSaturationMin;
         saturation <= VirtualController::kAxisSaturationMax;
         saturation += kSaturationIncrement)
      TestVirtualControllerApplyAxisProperties(
          Controller::kAnalogValueMin,
          Controller::kAnalogValueMax,
          VirtualController::kAxisDeadzoneMin,
          saturation);
  }

  // Range is a large pair of values centered at zero. Tested first without deadzone or saturation
  // and then with two different fairly common configurations of deadzone and saturation.
  TEST_CASE(VirtualController_ApplyAxisProperties_RangeLarge)
  {
    TestVirtualControllerApplyAxisProperties(-10000000, 10000000);
    TestVirtualControllerApplyAxisProperties(
        -10000000, 10000000, DeadzoneValueByPercentage(10), SaturationValueByPercentage(90));
    TestVirtualControllerApplyAxisProperties(
        -10000000, 10000000, DeadzoneValueByPercentage(25), SaturationValueByPercentage(75));
  }

  // Range is a large pair of values all of which are positive. Tested first without deadzone or
  // saturation and then with two different fairly common configurations of deadzone and saturation.
  TEST_CASE(VirtualController_ApplyAxisProperties_RangeLargePositive)
  {
    TestVirtualControllerApplyAxisProperties(0, 10000000);
    TestVirtualControllerApplyAxisProperties(
        0, 10000000, DeadzoneValueByPercentage(10), SaturationValueByPercentage(90));
    TestVirtualControllerApplyAxisProperties(
        0, 10000000, DeadzoneValueByPercentage(25), SaturationValueByPercentage(75));
  }

  // Range is a large pair of values all of which are negative. Tested first without deadzone or
  // saturation and then with two different fairly common configurations of deadzone and saturation.
  TEST_CASE(VirtualController_ApplyAxisProperties_RangeLargeNegative)
  {
    TestVirtualControllerApplyAxisProperties(-10000000, 0);
    TestVirtualControllerApplyAxisProperties(
        -10000000, 0, DeadzoneValueByPercentage(10), SaturationValueByPercentage(90));
    TestVirtualControllerApplyAxisProperties(
        -10000000, 0, DeadzoneValueByPercentage(25), SaturationValueByPercentage(75));
  }

  // Range is a small pair of values centered at zero. Tested first without deadzone or saturation
  // and then with two different fairly common configurations of deadzone and saturation.
  TEST_CASE(VirtualController_ApplyAxisProperties_RangeSmall)
  {
    TestVirtualControllerApplyAxisProperties(-100, 100);
    TestVirtualControllerApplyAxisProperties(
        -100, 100, DeadzoneValueByPercentage(10), SaturationValueByPercentage(90));
    TestVirtualControllerApplyAxisProperties(
        -10000000, 0, DeadzoneValueByPercentage(25), SaturationValueByPercentage(75));
  }

  // Range is a small pair of values all of which are positive. Tested first without deadzone or
  // saturation and then with two different fairly common configurations of deadzone and saturation.
  TEST_CASE(VirtualController_ApplyAxisProperties_RangeSmallPositive)
  {
    TestVirtualControllerApplyAxisProperties(0, 100);
    TestVirtualControllerApplyAxisProperties(
        0, 100, DeadzoneValueByPercentage(10), SaturationValueByPercentage(90));
    TestVirtualControllerApplyAxisProperties(
        -10000000, 0, DeadzoneValueByPercentage(25), SaturationValueByPercentage(75));
  }

  // Range is a small pair of values all of which are negative. Tested first without deadzone or
  // saturation and then with two different fairly common configurations of deadzone and saturation.
  TEST_CASE(VirtualController_ApplyAxisProperties_RangeSmallNegative)
  {
    TestVirtualControllerApplyAxisProperties(-100, 0);
    TestVirtualControllerApplyAxisProperties(
        -100, 0, DeadzoneValueByPercentage(10), SaturationValueByPercentage(90));
    TestVirtualControllerApplyAxisProperties(
        -10000000, 0, DeadzoneValueByPercentage(25), SaturationValueByPercentage(75));
  }

  // Transformations are disabled. Properties are set but they should be ignored.
  TEST_CASE(VirtualController_ApplyAxisProperties_TransformationsDisabled)
  {
    constexpr uint32_t kTestDeadzone = DeadzoneValueByPercentage(40);
    constexpr uint32_t kTestSaturation = SaturationValueByPercentage(60);
    constexpr std::pair<int32_t, int32_t> kTestRange = std::make_pair(-10, 10);

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(true == controller.GetAxisTransformationsEnabled(kTestSingleAxis));
    controller.SetAxisTransformationsEnabled(kTestSingleAxis, false);
    TEST_ASSERT(false == controller.GetAxisTransformationsEnabled(kTestSingleAxis));

    TEST_ASSERT(true == controller.SetAxisDeadzone(kTestSingleAxis, kTestDeadzone));
    TEST_ASSERT(
        true == controller.SetAxisRange(kTestSingleAxis, kTestRange.first, kTestRange.second));
    TEST_ASSERT(true == controller.SetAxisSaturation(kTestSingleAxis, kTestSaturation));

    for (int32_t inputAxisValue = Controller::kAnalogValueMin;
         inputAxisValue <= Controller::kAnalogValueMax;
         ++inputAxisValue)
    {
      const int32_t& expectedAxisValue = inputAxisValue;
      const int32_t actualAxisValue = GetAxisPropertiesApplyResult(controller, inputAxisValue);
      TEST_ASSERT(actualAxisValue == expectedAxisValue);
    }
  }

  // Valid deadzone value set on a single axis and then on all axes.
  TEST_CASE(VirtualController_SetProperty_DeadzoneValid)
  {
    constexpr uint32_t kTestDeadzoneValue = VirtualController::kAxisDeadzoneDefault / 2;
    constexpr EAxis kTestDeadzoneAxis = EAxis::RotX;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(true == controller.SetAxisDeadzone(kTestDeadzoneAxis, kTestDeadzoneValue));

    for (int i = 0; i < (int)EAxis::Count; ++i)
    {
      if ((int)kTestDeadzoneAxis == i)
        TEST_ASSERT(kTestDeadzoneValue == controller.GetAxisDeadzone((EAxis)i));
      else
        TEST_ASSERT(
            VirtualController::kAxisDeadzoneDefault == controller.GetAxisDeadzone((EAxis)i));
    }

    TEST_ASSERT(true == controller.SetAllAxisDeadzone(kTestDeadzoneValue));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(kTestDeadzoneValue == controller.GetAxisDeadzone((EAxis)i));
  }

  // Invalid deadzone value set on a single axis and then on all axes.
  TEST_CASE(VirtualController_SetProperty_DeadzoneInvalid)
  {
    constexpr uint32_t kTestDeadzoneValue = VirtualController::kAxisDeadzoneMax + 1;
    constexpr EAxis kTestDeadzoneAxis = EAxis::RotX;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(false == controller.SetAxisDeadzone(kTestDeadzoneAxis, kTestDeadzoneValue));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(VirtualController::kAxisDeadzoneDefault == controller.GetAxisDeadzone((EAxis)i));

    TEST_ASSERT(false == controller.SetAllAxisDeadzone(kTestDeadzoneValue));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(VirtualController::kAxisDeadzoneDefault == controller.GetAxisDeadzone((EAxis)i));
  }

  // Valid force feedback gain value.
  TEST_CASE(VirtualController_SetProperty_ForceFeedbackGainValid)
  {
    constexpr uint32_t kTestFfGainValue = VirtualController::kFfGainDefault / 2;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(true == controller.SetForceFeedbackGain(kTestFfGainValue));
  }

  // Invalid force feedback gain value.
  TEST_CASE(VirtualController_SetProperty_ForceFeedbackGainInvalid)
  {
    constexpr uint32_t kTestFfGainValue = VirtualController::kFfGainMax + 1;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(false == controller.SetForceFeedbackGain(kTestFfGainValue));
  }

  // Valid range values set on a single axis and then on all axes.
  TEST_CASE(VirtualController_SetProperty_RangeValid)
  {
    constexpr std::pair<int32_t, int32_t> kTestRangeValue = std::make_pair(-100, 50000);
    constexpr EAxis kTestRangeAxis = EAxis::Y;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(
        true ==
        controller.SetAxisRange(kTestRangeAxis, kTestRangeValue.first, kTestRangeValue.second));

    for (int i = 0; i < (int)EAxis::Count; ++i)
    {
      if ((int)kTestRangeAxis == i)
        TEST_ASSERT(kTestRangeValue == controller.GetAxisRange((EAxis)i));
      else
        TEST_ASSERT(
            std::make_pair(
                VirtualController::kRangeMinDefault, VirtualController::kRangeMaxDefault) ==
            controller.GetAxisRange((EAxis)i));
    }

    TEST_ASSERT(true == controller.SetAllAxisRange(kTestRangeValue.first, kTestRangeValue.second));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(kTestRangeValue == controller.GetAxisRange((EAxis)i));
  }

  // Invalid range values set on a single axis and then on all axes.
  TEST_CASE(VirtualController_SetProperty_RangeInvalid)
  {
    constexpr std::pair<int32_t, int32_t> kTestRangeValue = std::make_pair(50000, 50000);
    constexpr EAxis kTestRangeAxis = EAxis::Y;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(
        false ==
        controller.SetAxisRange(kTestRangeAxis, kTestRangeValue.first, kTestRangeValue.second));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(
          std::make_pair(
              VirtualController::kRangeMinDefault, VirtualController::kRangeMaxDefault) ==
          controller.GetAxisRange((EAxis)i));

    TEST_ASSERT(false == controller.SetAllAxisRange(kTestRangeValue.first, kTestRangeValue.second));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(
          std::make_pair(
              VirtualController::kRangeMinDefault, VirtualController::kRangeMaxDefault) ==
          controller.GetAxisRange((EAxis)i));
  }

  // Valid saturation value set on a single axis and then on all axes.
  TEST_CASE(VirtualController_SetProperty_SaturationValid)
  {
    constexpr uint32_t kTestSaturationValue = VirtualController::kAxisSaturationDefault / 2;
    constexpr EAxis kTestSaturationAxis = EAxis::RotY;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(true == controller.SetAxisSaturation(kTestSaturationAxis, kTestSaturationValue));

    for (int i = 0; i < (int)EAxis::Count; ++i)
    {
      if ((int)kTestSaturationAxis == i)
        TEST_ASSERT(kTestSaturationValue == controller.GetAxisSaturation((EAxis)i));
      else
        TEST_ASSERT(
            VirtualController::kAxisSaturationDefault == controller.GetAxisSaturation((EAxis)i));
    }

    TEST_ASSERT(true == controller.SetAllAxisSaturation(kTestSaturationValue));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(kTestSaturationValue == controller.GetAxisSaturation((EAxis)i));
  }

  // Invalid saturation value set on a single axis and then on all axes.
  TEST_CASE(VirtualController_SetProperty_SaturationInvalid)
  {
    constexpr uint32_t kTestSaturationValue = VirtualController::kAxisSaturationMax + 1;
    constexpr EAxis kTestSaturationAxis = EAxis::RotY;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(false == controller.SetAxisSaturation(kTestSaturationAxis, kTestSaturationValue));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(
          VirtualController::kAxisSaturationDefault == controller.GetAxisSaturation((EAxis)i));

    TEST_ASSERT(false == controller.SetAllAxisSaturation(kTestSaturationValue));

    for (int i = 0; i < (int)EAxis::Count; ++i)
      TEST_ASSERT(
          VirtualController::kAxisSaturationDefault == controller.GetAxisSaturation((EAxis)i));
  }

  // Valid property changes that should result in a transformation being applied to the current
  // controller state view even without a state change.
  TEST_CASE(VirtualController_SetProperty_AutoApplyToExistingState)
  {
    constexpr int32_t kTestOldAxisRangeMin = 0;
    constexpr int32_t kTestOldAxisRangeMax = 32768;
    constexpr int32_t kTestOldAxisRangeExpectedNeutralValue =
        (kTestOldAxisRangeMin + kTestOldAxisRangeMax) / 2;

    constexpr int32_t kTestNewAxisRangeMin = 500;
    constexpr int32_t kTestNewAxisRangeMax = 1000;
    constexpr int32_t kTestNewAxisRangeExpectedNeutralValue =
        (kTestNewAxisRangeMin + kTestNewAxisRangeMax) / 2;

    constexpr SPhysicalState kPhysicalState = {.deviceStatus = EPhysicalDeviceStatus::Ok};
    constexpr Controller::SState kExpectedStateBefore = {
        .axis = {
            kTestOldAxisRangeExpectedNeutralValue,
            kTestOldAxisRangeExpectedNeutralValue,
            0,
            kTestOldAxisRangeExpectedNeutralValue,
            kTestOldAxisRangeExpectedNeutralValue,
            0}};
    constexpr Controller::SState kExpectedStateAfter = {
        .axis = {
            kTestNewAxisRangeExpectedNeutralValue,
            kTestNewAxisRangeExpectedNeutralValue,
            0,
            kTestNewAxisRangeExpectedNeutralValue,
            kTestNewAxisRangeExpectedNeutralValue,
            0}};

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    controller.RefreshState(kTestMapper.MapStatePhysicalToVirtual(kPhysicalState, 0));
    controller.SetAllAxisRange(kTestOldAxisRangeMin, kTestOldAxisRangeMax);
    const Controller::SState actualStateBefore = controller.GetState();
    TEST_ASSERT(actualStateBefore == kExpectedStateBefore);

    controller.SetAllAxisRange(kTestNewAxisRangeMin, kTestNewAxisRangeMax);
    const Controller::SState actualStateAfter = controller.GetState();
    TEST_ASSERT(actualStateAfter == kExpectedStateAfter);
  }

  // Verifies that by default buffered events are disabled.
  TEST_CASE(VirtualController_EventBuffer_DefaultDisabled)
  {
    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    TEST_ASSERT(0 == controller.GetEventBufferCapacity());
  }

  // Verifies that buffered events can be enabled.
  TEST_CASE(VirtualController_EventBuffer_CanEnable)
  {
    constexpr uint32_t kEventBufferCapacity = 64;

    MockPhysicalController physicalController(0, kTestMapper);
    VirtualController controller(0);

    controller.SetEventBufferCapacity(kEventBufferCapacity);
    TEST_ASSERT(kEventBufferCapacity == controller.GetEventBufferCapacity());
  }

  // Applies some neutral state updates to the virtual controller and verifies that no events are
  // generated.
  TEST_CASE(VirtualController_EventBuffer_Neutral)
  {
    constexpr TControllerIdentifier kControllerIndex = 0;
    constexpr uint32_t kEventBufferCapacity = 64;

    constexpr SPhysicalState kPhysicalStates[] = {
        {.deviceStatus = EPhysicalDeviceStatus::Ok},
        {.deviceStatus = EPhysicalDeviceStatus::Ok},
        {.deviceStatus = EPhysicalDeviceStatus::Ok}};

    MockPhysicalController physicalController(kControllerIndex, kTestMapper);
    VirtualController controller(kControllerIndex);

    controller.SetEventBufferCapacity(kEventBufferCapacity);

    for (int i = 0; i < _countof(kPhysicalStates); ++i)
      controller.RefreshState(
          kTestMapper.MapStatePhysicalToVirtual(kPhysicalStates[i], kControllerIndex));

    TEST_ASSERT(0 == controller.GetEventBufferCount());
  }

  // Applies some actual state updates to the virtual controller and verifies that events are
  // correctly generated. The final view of controller state should be the same regardless of
  // whether it is obtained via snapshot or via buffered events.
  TEST_CASE(VirtualController_EventBuffer_MultipleUpdates)
  {
    constexpr TControllerIdentifier kControllerIndex = 0;
    constexpr uint32_t kEventBufferCapacity = 64;

    // Avoid using vertical components of the analog sticks to avoid having to worry about axis
    // inversion.
    constexpr SPhysicalState kPhysicalStates[] = {
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .stick = {1111, 0, 2222, 0},
         .button = ButtonSet({EPhysicalButton::A})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .stick = {3333, 0, 4444, 0},
         .button = ButtonSet({EPhysicalButton::A})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .stick = {-5555, 0, -6666, 0},
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::Y, EPhysicalButton::DpadUp})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::DpadLeft})}};

    // Values come from the mapper at the top of this file.
    constexpr Controller::SState kExpectedControllerStates[] = {
        {.axis = {1111, 0, 0, 2222, 0, 0}, .button = 0b0001},
        {.axis = {3333, 0, 0, 4444, 0, 0}, .button = 0b0001},
        {.axis = {-5555, 0, 0, -6666, 0, 0},
         .button = 0b1001,
         .povDirection = {.components = {true, false, false, false}}},
        {.axis = {0, 0, 0, 0, 0, 0},
         .button = 0b0000,
         .povDirection = {.components = {false, false, true, false}}}};

    static_assert(
        _countof(kPhysicalStates) == _countof(kExpectedControllerStates),
        "Mismatch between number of physical and virtual controller states.");

    // Each iteration of the loop adds one more event to the test.
    // First iteration tests only a single state change, second iteration tests two state changes,
    // and so on.
    for (unsigned int i = 1; i <= _countof(kPhysicalStates); ++i)
    {
      MockPhysicalController physicalController(kControllerIndex, kTestMapper);
      VirtualController controller(kControllerIndex);

      controller.SetAllAxisRange(Controller::kAnalogValueMin, Controller::kAnalogValueMax);
      controller.SetEventBufferCapacity(kEventBufferCapacity);

      uint32_t lastEventCount = controller.GetEventBufferCount();
      TEST_ASSERT(0 == lastEventCount);
      for (unsigned int j = 0; j < i; ++j)
      {
        controller.RefreshState(
            kTestMapper.MapStatePhysicalToVirtual(kPhysicalStates[j], kControllerIndex));

        TEST_ASSERT(controller.GetEventBufferCount() > lastEventCount);
        lastEventCount = controller.GetEventBufferCount();
      }

      Controller::SState actualStateFromSnapshot = controller.GetState();
      Controller::SState actualStateFromBufferedEvents;
      ZeroMemory(&actualStateFromBufferedEvents, sizeof(actualStateFromBufferedEvents));

      for (unsigned int j = 0; j < controller.GetEventBufferCount(); ++j)
        ApplyUpdateToControllerState(
            controller.GetEventBufferEvent(j).data, actualStateFromBufferedEvents);

      TEST_ASSERT(actualStateFromSnapshot == kExpectedControllerStates[i - 1]);
      TEST_ASSERT(actualStateFromBufferedEvents == kExpectedControllerStates[i - 1]);
    }
  }

  // Applies some actual state updates to the virtual controller and verifies that events are
  // correctly generated, with certain controller elements filtered out. Similar to above, but the
  // expected states are different because of the event filters.
  TEST_CASE(VirtualController_EventBuffer_UpdatesWithFilter)
  {
    constexpr TControllerIdentifier kControllerIndex = 0;
    constexpr uint32_t kEventBufferCapacity = 64;

    constexpr SPhysicalState kPhysicalStates[] = {
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .stick = {1111, 2222, 0, 0},
         .button = ButtonSet({EPhysicalButton::A})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .stick = {3333, 4444, 0, 0},
         .button = ButtonSet({EPhysicalButton::A})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .stick = {-5555, -6666, 0, 0},
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::Y, EPhysicalButton::DpadUp})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::DpadLeft})}};

    // Values come from the mapper at the top of this file.
    // Because the axes are filtered out using an event filter, their values are expected to be 0
    // irrespective of the values retrieved from XInput.
    constexpr Controller::SState kExpectedControllerStates[] = {
        {.button = 0b0001},
        {.button = 0b0001},
        {.button = 0b1001, .povDirection = {.components = {true, false, false, false}}},
        {.button = 0b0000, .povDirection = {.components = {false, false, true, false}}}};

    static_assert(
        _countof(kPhysicalStates) == _countof(kExpectedControllerStates),
        "Mismatch between number of physical and virtual controller states.");

    // Each iteration of the loop adds one more event to the test.
    // First iteration tests only a single state change, second iteration tests two state changes,
    // and so on.
    for (unsigned int i = 1; i <= _countof(kPhysicalStates); ++i)
    {
      MockPhysicalController physicalController(kControllerIndex, kTestMapper);
      VirtualController controller(kControllerIndex);

      controller.SetAllAxisRange(Controller::kAnalogValueMin, Controller::kAnalogValueMax);
      controller.SetEventBufferCapacity(kEventBufferCapacity);
      controller.EventFilterRemoveElement({.type = EElementType::Axis, .axis = EAxis::X});
      controller.EventFilterRemoveElement({.type = EElementType::Axis, .axis = EAxis::Y});

      uint32_t lastEventCount = controller.GetEventBufferCount();
      TEST_ASSERT(0 == lastEventCount);
      for (unsigned int j = 0; j < i; ++j)
      {
        controller.RefreshState(
            kTestMapper.MapStatePhysicalToVirtual(kPhysicalStates[j], kControllerIndex));

        TEST_ASSERT(controller.GetEventBufferCount() >= lastEventCount);
        lastEventCount = controller.GetEventBufferCount();
      }

      Controller::SState actualStateFromBufferedEvents;
      ZeroMemory(&actualStateFromBufferedEvents, sizeof(actualStateFromBufferedEvents));

      for (unsigned int j = 0; j < controller.GetEventBufferCount(); ++j)
        ApplyUpdateToControllerState(
            controller.GetEventBufferEvent(j).data, actualStateFromBufferedEvents);

      TEST_ASSERT(actualStateFromBufferedEvents == kExpectedControllerStates[i - 1]);
    }
  }

  // Submits multiple physical state changes to the physical controller associated with a virtual
  // controller such that every single physical state change causes a virtual controller state
  // change. Enables state change notifications and verifies that each physical controller state
  // change causes a notification to be fired.
  TEST_CASE(VirtualController_StateChangeNotification_Nominal)
  {
    constexpr TControllerIdentifier kControllerIndex = 2;

    // All of the buttons used in these physical states are part of the test mapper defined at the
    // top of this file.
    constexpr SPhysicalState kPhysicalStates[] = {
        {.deviceStatus = EPhysicalDeviceStatus::Ok},
        {.deviceStatus = EPhysicalDeviceStatus::Ok, .button = ButtonSet({EPhysicalButton::A})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::B})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::B, EPhysicalButton::DpadLeft})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::B, EPhysicalButton::DpadLeft})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok, .button = ButtonSet({EPhysicalButton::B})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok}};

    const HANDLE stateChangeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    TEST_ASSERT((nullptr != stateChangeEvent) && (INVALID_HANDLE_VALUE != stateChangeEvent));

    MockPhysicalController physicalController(
        kControllerIndex, kTestMapper, kPhysicalStates, _countof(kPhysicalStates));

    VirtualController controller(kControllerIndex);
    controller.SetStateChangeEvent(stateChangeEvent);

    for (int i = 1; i < _countof(kPhysicalStates); ++i)
    {
      physicalController.RequestAdvancePhysicalState();
      TEST_ASSERT(
          WAIT_OBJECT_0 ==
          WaitForSingleObject(stateChangeEvent, kTestStateChangeEventTimeoutMilliseconds));
    }
  }

  // Submits multiple physical state changes to the physical controller associated with a virtual
  // controller such that every other physical state change causes a virtual controller state
  // change. Enables state change notifications and verifies that each physical controller state
  // change causes a notification to be fired if there is a corresponding virtual controller state
  // change.
  TEST_CASE(VirtualController_StateChangeNotification_SomePhysicalStatesIneffective)
  {
    constexpr TControllerIdentifier kControllerIndex = 3;

    // Only some of the buttons used in these physical states are part of the test mapper defined at
    // the top of this file. Left and right shoulder buttons are not mapped to any virtual
    // controller element, so pressing and releasing them counts as a physical controller state
    // change but not as a virtual controller state change.
    constexpr SPhysicalState kPhysicalStates[] = {
        {.deviceStatus = EPhysicalDeviceStatus::Ok},
        {.deviceStatus = EPhysicalDeviceStatus::Ok, .button = ButtonSet({EPhysicalButton::A})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::LB})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::LB, EPhysicalButton::DpadUp})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet(
             {EPhysicalButton::A,
              EPhysicalButton::LB,
              EPhysicalButton::DpadUp,
              EPhysicalButton::RB})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::LB, EPhysicalButton::RB})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok,
         .button = ButtonSet({EPhysicalButton::A, EPhysicalButton::LB})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok, .button = ButtonSet({EPhysicalButton::LB})},
        {.deviceStatus = EPhysicalDeviceStatus::Ok}};
    static_assert(
        0 != (_countof(kPhysicalStates) % 2),
        "An even number of states is required beyond the initial physical state.");

    const HANDLE stateChangeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    TEST_ASSERT((nullptr != stateChangeEvent) && (INVALID_HANDLE_VALUE != stateChangeEvent));

    MockPhysicalController physicalController(
        kControllerIndex, kTestMapper, kPhysicalStates, _countof(kPhysicalStates));

    VirtualController controller(kControllerIndex);
    controller.SetStateChangeEvent(stateChangeEvent);

    for (int i = 1; i < _countof(kPhysicalStates); i += 2)
    {
      physicalController.RequestAdvancePhysicalState();
      TEST_ASSERT(
          WAIT_OBJECT_0 ==
          WaitForSingleObject(stateChangeEvent, kTestStateChangeEventTimeoutMilliseconds));

      physicalController.RequestAdvancePhysicalState();
      TEST_ASSERT(
          WAIT_TIMEOUT ==
          WaitForSingleObject(stateChangeEvent, kTestStateChangeEventTimeoutMilliseconds));
    }
  }

  // Verifies that a single virtual controller can register and unregister successfully, and this
  // changes the device pointer it returns.
  TEST_CASE(VirtualController_ForceFeedback_Nominal)
  {
    constexpr TControllerIdentifier kControllerIndex = 1;
    constexpr SPhysicalState kPhysicalState = {.deviceStatus = EPhysicalDeviceStatus::Ok};

    MockPhysicalController physicalController(kControllerIndex, kTestMapper, &kPhysicalState, 1);
    const Controller::ForceFeedback::Device* const kForceFeedbackDeviceAddress =
        &physicalController.GetForceFeedbackDevice();

    VirtualController controller(kControllerIndex);

    TEST_ASSERT(false == controller.ForceFeedbackIsRegistered());
    TEST_ASSERT(nullptr == controller.ForceFeedbackGetDevice());
    TEST_ASSERT(
        false == physicalController.IsVirtualControllerRegisteredForForceFeedback(&controller));

    TEST_ASSERT(true == controller.ForceFeedbackRegister());
    TEST_ASSERT(kForceFeedbackDeviceAddress == controller.ForceFeedbackGetDevice());
    TEST_ASSERT(
        true == physicalController.IsVirtualControllerRegisteredForForceFeedback(&controller));

    controller.ForceFeedbackUnregister();
    TEST_ASSERT(false == controller.ForceFeedbackIsRegistered());
    TEST_ASSERT(nullptr == controller.ForceFeedbackGetDevice());
    TEST_ASSERT(
        false == physicalController.IsVirtualControllerRegisteredForForceFeedback(&controller));
  }

  // Verifies that multiple virtual controllers are allowed to register at a time.
  TEST_CASE(VirtualController_ForceFeedback_MultipleRegistrations)
  {
    constexpr TControllerIdentifier kControllerIndex = 1;
    constexpr SPhysicalState kPhysicalState = {.deviceStatus = EPhysicalDeviceStatus::Ok};

    MockPhysicalController physicalController(kControllerIndex, kTestMapper, &kPhysicalState, 1);
    const Controller::ForceFeedback::Device* const kForceFeedbackDeviceAddress =
        &physicalController.GetForceFeedbackDevice();

    VirtualController controller(kControllerIndex);
    VirtualController controller2(kControllerIndex);

    TEST_ASSERT(false == controller.ForceFeedbackIsRegistered());
    TEST_ASSERT(false == controller2.ForceFeedbackIsRegistered());

    TEST_ASSERT(true == controller.ForceFeedbackRegister());
    TEST_ASSERT(true == controller.ForceFeedbackIsRegistered());
    TEST_ASSERT(true == controller2.ForceFeedbackRegister());
    TEST_ASSERT(true == controller2.ForceFeedbackIsRegistered());
  }

  // Verifies that registration is idempotent. Calling the registration method should continually
  // return success if the controller is registered.
  TEST_CASE(VirtualController_ForceFeedback_Idempotent)
  {
    constexpr TControllerIdentifier kControllerIndex = 1;
    constexpr SPhysicalState kPhysicalState = {.deviceStatus = EPhysicalDeviceStatus::Ok};

    MockPhysicalController physicalController(kControllerIndex, kTestMapper, &kPhysicalState, 1);
    const Controller::ForceFeedback::Device* const kForceFeedbackDeviceAddress =
        &physicalController.GetForceFeedbackDevice();

    VirtualController controller(kControllerIndex);

    for (int i = 0; i < 100; ++i)
      TEST_ASSERT(true == controller.ForceFeedbackRegister());

    TEST_ASSERT(kForceFeedbackDeviceAddress == controller.ForceFeedbackGetDevice());
    TEST_ASSERT(
        true == physicalController.IsVirtualControllerRegisteredForForceFeedback(&controller));
  }

  // Verifies that virtual controllers automatically unregister themselves upon destruction.
  TEST_CASE(VirtualController_ForceFeedback_UnregisterOnDestruction)
  {
    constexpr TControllerIdentifier kControllerIndex = 1;
    constexpr SPhysicalState kPhysicalState = {.deviceStatus = EPhysicalDeviceStatus::Ok};

    MockPhysicalController physicalController(kControllerIndex, kTestMapper, &kPhysicalState, 1);
    const Controller::ForceFeedback::Device* const kForceFeedbackDeviceAddress =
        &physicalController.GetForceFeedbackDevice();

    VirtualController* controller = new VirtualController(kControllerIndex);
    TEST_ASSERT(true == controller->ForceFeedbackRegister());
    TEST_ASSERT(
        true == physicalController.IsVirtualControllerRegisteredForForceFeedback(controller));

    delete controller;
    TEST_ASSERT(
        false == physicalController.IsVirtualControllerRegisteredForForceFeedback(controller));
  }
} // namespace XidiTest
