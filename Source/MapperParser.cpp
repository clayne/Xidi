/***************************************************************************************************
 * Xidi
 *   DirectInput interface for XInput controllers.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016-2025
 ***********************************************************************************************//**
 * @file MapperParser.cpp
 *   Implementation of functionality for parsing pieces of mapper objects from strings,
 *   typically supplied in a configuration file.
 **************************************************************************************************/

#include "MapperParser.h"

#include <climits>
#include <cstddef>
#include <cwchar>
#include <cwctype>
#include <map>
#include <optional>
#include <string_view>

#include <Infra/Core/Strings.h>
#include <Infra/Core/ValueOrError.h>

#include "ApiDirectInput.h"
#include "ControllerTypes.h"
#include "ElementMapper.h"
#include "ForceFeedbackTypes.h"
#include "Keyboard.h"
#include "Mapper.h"
#include "Mouse.h"

namespace Xidi
{
  namespace Controller
  {
    namespace MapperParser
    {
      /// Maximum recursion depth allowed for an element mapper string.
      /// Should be at least one more than the total number of element mapper types that accept
      /// underlying element mappers.
      static constexpr unsigned int kElementMapperMaxRecursionDepth = 4;

      /// Character used inside an element mapper string to indicate the beginning of a parameter
      /// list.
      static constexpr wchar_t kCharElementMapperBeginParams = '(';

      /// Character used inside an element mapper string to indicate the end of a parameter list.
      static constexpr wchar_t kCharElementMapperEndParams = L')';

      /// Character used inside an element mapper string to indicate a separation between
      /// parameters.
      static constexpr wchar_t kCharElementMapperParamSeparator = L',';

      /// Set of characters that are considered whitespace for the purpose of parsing element mapper
      /// strings.
      static constexpr wchar_t kCharSetWhitespace[] = L" \t";

      /// Set of characters that separate an element mapper type from the rest of the input string.
      /// For example, if parsing "SplitMapper ( Null, Axis(X, +) )" then we need to stop at the
      /// comma when extracting the type that corresponds to the "Null" portion of the string. As
      /// another example, if parsing "SplitMapper ( Axis(X, +), Null )" then we need to stop at the
      /// close parenthesis when extracting the type that corresponds to the "Null" portion of the
      /// string. Terminating null character is needed so that this array acts as a null-terminated
      /// string.
      static constexpr wchar_t kCharSetElementMapperTypeSeparator[] = {
          kCharElementMapperBeginParams,
          kCharElementMapperEndParams,
          kCharElementMapperParamSeparator,
          L'\0'};

      /// Type for all functions that attempt to build individual element mappers given a parameter
      /// string.
      using TMakeElementMapperFunc = ElementMapperOrError (*)(std::wstring_view);

      /// Type for all functions that attempt to build force feedback actuator description objects
      /// given a parameter string.
      using TMakeForceFeedbackActuatorFunc = ForceFeedbackActuatorOrError (*)(std::wstring_view);

      /// Holds parameters for creating various types of axis mapper objects, where those mapper
      /// objects include an axis enumerator and an axis direction enumerator.
      /// @tparam AxisEnumType Axis enumeration type that identifies the target axis.
      template <typename AxisEnumType> struct SAxisParams
      {
        AxisEnumType axis;
        EAxisDirection direction;
      };

      /// Type alias for enabling axis parameter parsing to indicate a semantically-rich error on
      /// parse failure.
      /// @tparam AxisEnumType Axis enumeration type that identifies the target axis.
      template <typename AxisEnumType> using AxisParamsOrError =
          Infra::ValueOrError<SAxisParams<AxisEnumType>, std::wstring>;

      /// Attempts to map a string to an axis direction enumerator.
      /// @param [in] directionString String supposedly representing an axis direction.
      /// @return Corresponding axis direction enumerator, if the string is parseable as such.
      static std::optional<EAxisDirection> AxisDirectionFromString(
          std::wstring_view directionString)
      {
        // Map of strings representing axis directions to axis direction enumerators.
        static const std::map<std::wstring_view, EAxisDirection> kDirectionStrings = {
            {L"bidir", EAxisDirection::Both},         {L"Bidir", EAxisDirection::Both},
            {L"BiDir", EAxisDirection::Both},         {L"BIDIR", EAxisDirection::Both},
            {L"bidirectional", EAxisDirection::Both}, {L"Bidirectional", EAxisDirection::Both},
            {L"BiDirectional", EAxisDirection::Both}, {L"BIDIRECTIONAL", EAxisDirection::Both},
            {L"both", EAxisDirection::Both},          {L"Both", EAxisDirection::Both},
            {L"BOTH", EAxisDirection::Both},

            {L"+", EAxisDirection::Positive},         {L"+ve", EAxisDirection::Positive},
            {L"pos", EAxisDirection::Positive},       {L"Pos", EAxisDirection::Positive},
            {L"POS", EAxisDirection::Positive},       {L"positive", EAxisDirection::Positive},
            {L"Positive", EAxisDirection::Positive},  {L"POSITIVE", EAxisDirection::Positive},

            {L"-", EAxisDirection::Negative},         {L"-ve", EAxisDirection::Negative},
            {L"neg", EAxisDirection::Negative},       {L"Neg", EAxisDirection::Negative},
            {L"NEG", EAxisDirection::Negative},       {L"negative", EAxisDirection::Negative},
            {L"Negative", EAxisDirection::Negative},  {L"NEGATIVE", EAxisDirection::Negative}};

        const auto directionIter = kDirectionStrings.find(directionString);
        if (kDirectionStrings.cend() == directionIter) return std::nullopt;

        return directionIter->second;
      }

      /// Attempts to map a string to an axis type enumerator. This generic version does nothing.
      /// @tparam AxisEnumType Axis enumeration type that identifies the target axis.
      /// @param [in] axisString String supposedly representing an axis type.
      /// @return Corresponding axis type enumerator, if the string is parseable as such.
      template <typename AxisEnumType> static std::optional<AxisEnumType> AxisTypeFromString(
          std::wstring_view axisString)
      {
        return std::nullopt;
      }

      /// Attempts to map a string to an axis type enumerator, specialized for analog axes.
      /// @param [in] axisString String supposedly representing an axis type.
      /// @return Corresponding axis type enumerator, if the string is parseable as such.
      template <> static std::optional<EAxis> AxisTypeFromString(std::wstring_view axisString)
      {
        // Map of strings representing axes to axis enumerators.
        static const std::map<std::wstring_view, EAxis> kAxisStrings = {
            {L"x", EAxis::X},       {L"X", EAxis::X},

            {L"y", EAxis::Y},       {L"Y", EAxis::Y},

            {L"z", EAxis::Z},       {L"Z", EAxis::Z},

            {L"rx", EAxis::RotX},   {L"Rx", EAxis::RotX},   {L"rX", EAxis::RotX},
            {L"RX", EAxis::RotX},   {L"rotx", EAxis::RotX}, {L"rotX", EAxis::RotX},
            {L"Rotx", EAxis::RotX}, {L"RotX", EAxis::RotX},

            {L"ry", EAxis::RotY},   {L"Ry", EAxis::RotY},   {L"rY", EAxis::RotY},
            {L"RY", EAxis::RotY},   {L"roty", EAxis::RotY}, {L"rotY", EAxis::RotY},
            {L"Roty", EAxis::RotY}, {L"RotY", EAxis::RotY},

            {L"rz", EAxis::RotZ},   {L"Rz", EAxis::RotZ},   {L"rZ", EAxis::RotZ},
            {L"RZ", EAxis::RotZ},   {L"rotz", EAxis::RotZ}, {L"rotZ", EAxis::RotZ},
            {L"Rotz", EAxis::RotZ}, {L"RotZ", EAxis::RotZ}};

        const auto axisIter = kAxisStrings.find(axisString);
        if (kAxisStrings.cend() == axisIter) return std::nullopt;

        return axisIter->second;
      }

      /// Attempts to map a string to an axis type enumerator, specialized for mouse axes.
      /// @param [in] axisString String supposedly representing an axis type.
      /// @return Corresponding axis type enumerator, if the string is parseable as such.
      template <> static std::optional<Mouse::EMouseAxis> AxisTypeFromString(
          std::wstring_view axisString)
      {
        // Map of strings representing mouse axes to mouse axis enumerators.
        static const std::map<std::wstring_view, Mouse::EMouseAxis> kMouseAxisStrings = {
            {L"x", Mouse::EMouseAxis::X},
            {L"X", Mouse::EMouseAxis::X},
            {L"h", Mouse::EMouseAxis::X},
            {L"H", Mouse::EMouseAxis::X},
            {L"horiz", Mouse::EMouseAxis::X},
            {L"Horiz", Mouse::EMouseAxis::X},
            {L"horizontal", Mouse::EMouseAxis::X},
            {L"Horizontal", Mouse::EMouseAxis::X},

            {L"y", Mouse::EMouseAxis::Y},
            {L"Y", Mouse::EMouseAxis::Y},
            {L"v", Mouse::EMouseAxis::Y},
            {L"V", Mouse::EMouseAxis::Y},
            {L"vert", Mouse::EMouseAxis::Y},
            {L"Vert", Mouse::EMouseAxis::Y},
            {L"vertical", Mouse::EMouseAxis::Y},
            {L"Vertical", Mouse::EMouseAxis::Y},

            {L"wheelh", Mouse::EMouseAxis::WheelHorizontal},
            {L"wheelH", Mouse::EMouseAxis::WheelHorizontal},
            {L"WheelH", Mouse::EMouseAxis::WheelHorizontal},
            {L"wheelx", Mouse::EMouseAxis::WheelHorizontal},
            {L"wheelX", Mouse::EMouseAxis::WheelHorizontal},
            {L"WheelX", Mouse::EMouseAxis::WheelHorizontal},
            {L"wheelHorizontal", Mouse::EMouseAxis::WheelHorizontal},
            {L"WheelHorizontal", Mouse::EMouseAxis::WheelHorizontal},

            {L"wheelv", Mouse::EMouseAxis::WheelVertical},
            {L"wheelV", Mouse::EMouseAxis::WheelVertical},
            {L"WheelV", Mouse::EMouseAxis::WheelVertical},
            {L"wheely", Mouse::EMouseAxis::WheelVertical},
            {L"wheelY", Mouse::EMouseAxis::WheelVertical},
            {L"WheelY", Mouse::EMouseAxis::WheelVertical},
            {L"wheelVertical", Mouse::EMouseAxis::WheelVertical},
            {L"WheelVertical", Mouse::EMouseAxis::WheelVertical}};

        const auto mouseAxisIter = kMouseAxisStrings.find(axisString);
        if (kMouseAxisStrings.cend() == mouseAxisIter) return std::nullopt;

        return mouseAxisIter->second;
      }

      /// Identifies the end position of the first parameter in the supplied string which should be
      /// a parameter list. Example: "RotY, +" would identify the position of the comma. Example:
      /// "Split(Button(1), Button(2)), Split(Button(3), Button(4))" would identify the position of
      /// the second comma. Example: "RotY" would return a value of 4, the length of the string,
      /// indicating the entire string is the first parameter.
      /// @return End position of the first parameter in the supplied string, or `npos` if the input
      /// string is invalid thus leading to a parse error.
      static size_t FindFirstParameterEndPosition(std::wstring_view paramListString)
      {
        unsigned int depth = 0;

        for (size_t pos = 0; pos < paramListString.length(); ++pos)
        {
          switch (paramListString[pos])
          {
            case kCharElementMapperBeginParams:
              depth += 1;
              break;

            case kCharElementMapperEndParams:
              if (0 == depth) return std::wstring_view::npos;
              depth -= 1;
              break;

            case kCharElementMapperParamSeparator:
              if (0 == depth) return pos;
              break;

            default:
              break;
          }
        }

        if (0 != depth)
          return std::wstring_view::npos;
        else
          return paramListString.length();
      }

      /// Identifies the end position of the parameter list given a string that starts a parameter
      /// list. For example, if the element mapper string is "Axis(RotY, +)" then the input string
      /// should be "RotY, +)" and this function will identify the position of the closing
      /// parenthesis.
      /// @param [in] paramListString Parameter list input string to search.
      /// @return Position of the end of the parameter list if the input string is valid and
      /// contains correct balance, or `npos` otherwise.
      static size_t FindParamListEndPosition(std::wstring_view paramListString)
      {
        unsigned int depth = 1;

        for (size_t pos = paramListString.find_first_not_of(kCharSetWhitespace);
             pos < paramListString.length();
             ++pos)
        {
          switch (paramListString[pos])
          {
            case kCharElementMapperBeginParams:
              depth += 1;
              break;

            case kCharElementMapperEndParams:
              depth -= 1;
              if (0 == depth) return pos;
              break;

            default:
              break;
          }
        }

        return std::wstring_view::npos;
      }

      /// Common logic for parsing various types of axis mapper parameters from an axis mapper
      /// string.
      /// @tparam AxisEnumType Axis enumeration type that identifies the target axis.
      /// @param [in] params Parameter string.
      /// @return Structure containing the parsed parameters if parsing was successful, error
      /// message otherwise.
      template <typename AxisEnumType> static AxisParamsOrError<AxisEnumType> ParseAxisParams(
          std::wstring_view params)
      {
        SParamStringParts paramParts =
            ExtractParameterListStringParts(params).value_or(SParamStringParts());

        // First parameter is required. It is a string that specifies the target axis.
        if (true == paramParts.first.empty()) return L"Missing or unparseable axis";

        const std::optional<AxisEnumType> maybeAxis =
            AxisTypeFromString<AxisEnumType>(paramParts.first);
        if (false == maybeAxis.has_value())
          return Infra::Strings::Format(
                     L"%s: Unrecognized axis", std::wstring(paramParts.first).c_str())
              .Data();

        const AxisEnumType axis = maybeAxis.value();

        // Second parameter is optional. It is a string that specifies the axis direction, with the
        // default being both.
        EAxisDirection axisDirection = EAxisDirection::Both;

        paramParts =
            ExtractParameterListStringParts(paramParts.remaining).value_or(SParamStringParts());
        if (false == paramParts.first.empty())
        {
          // It is an error for a second parameter to be present but invalid.
          const std::optional<EAxisDirection> maybeAxisDirection =
              AxisDirectionFromString(paramParts.first);
          if (false == maybeAxisDirection.has_value())
            return Infra::Strings::Format(
                       L"%s: Unrecognized axis direction", std::wstring(paramParts.first).c_str())
                .Data();

          axisDirection = maybeAxisDirection.value();
        }

        // No further parameters allowed.
        if (false == paramParts.remaining.empty())
          return Infra::Strings::Format(
                     L"\"%s\" is extraneous", std::wstring(paramParts.remaining).c_str())
              .Data();

        return SAxisParams<AxisEnumType>({.axis = axis, .direction = axisDirection});
      }

      /// Parses a relatively small unsigned integer value from the supplied input string.
      /// A maximum of 8 characters are permitted, meaning any parsed values are guaranteed to fit
      /// into 32 bits. This function will fail if the input string is too long or if it does not
      /// entirely represent an unsigned integer value.
      /// @param [in] uintString String from which to parse.
      /// @param [in] base Representation base of the number, which defaults to auto-detecting the
      /// base using the prefix in the string.
      /// @return Parsed integer value if successful.
      static std::optional<unsigned int> ParseUnsignedInteger(
          std::wstring_view uintString, int base = 0)
      {
        static constexpr size_t kMaxChars = 8;
        if (true == uintString.empty()) return std::nullopt;
        if (uintString.length() > kMaxChars) return std::nullopt;

        // Create a null-terminated version of the number by copying the digits into a small buffer.
        wchar_t convertBuffer[1 + kMaxChars];
        convertBuffer[kMaxChars] = L'\0';
        for (size_t i = 0; i < kMaxChars; ++i)
        {
          if (i < uintString.length())
          {
            if (iswalnum(uintString[i]))
              convertBuffer[i] = uintString[i];
            else
              return std::nullopt;
          }
          else
          {
            convertBuffer[i] = L'\0';
            break;
          }
        }

        wchar_t* endptr = nullptr;
        unsigned int parsedValue = (unsigned int)wcstoul(convertBuffer, &endptr, base);

        // Verify that the whole string was consumed.
        if (L'\0' != *endptr) return std::nullopt;

        return parsedValue;
      }

      /// Parses a string representation of a DirectInput keyboard scancode into an integer.
      /// This function will fail if the input string is too long or if it does not represent a
      /// keyboard scancode.
      /// @param [in] kbString String from which to parse.
      /// @return Parsed integer value if successful.
      static std::optional<unsigned int> ParseKeyboardScancode(std::wstring_view kbString)
      {
        // Map of strings representing keyboard scancodes to the keyboard scancodes themselves.
        // One pair exists per DIK_* constant. Comparisons with the input string are
        // case-insensitive because the input string is converted to uppercase to match the contents
        // of this map.
        static const std::map<std::wstring_view, unsigned int> kKeyboardScanCodeStrings = {

            // Convenience aliases
            {L"ESC", DIK_ESCAPE},
            {L"ENTER", DIK_RETURN},
            {L"SCROLLLOCK", DIK_SCROLL},

            // DIK_ constants
            {L"ESCAPE", DIK_ESCAPE},
            {L"1", DIK_1},
            {L"2", DIK_2},
            {L"3", DIK_3},
            {L"4", DIK_4},
            {L"5", DIK_5},
            {L"6", DIK_6},
            {L"7", DIK_7},
            {L"8", DIK_8},
            {L"9", DIK_9},
            {L"0", DIK_0},
            {L"MINUS", DIK_MINUS},
            {L"EQUALS", DIK_EQUALS},
            {L"BACK", DIK_BACK},
            {L"TAB", DIK_TAB},
            {L"Q", DIK_Q},
            {L"W", DIK_W},
            {L"E", DIK_E},
            {L"R", DIK_R},
            {L"T", DIK_T},
            {L"Y", DIK_Y},
            {L"U", DIK_U},
            {L"I", DIK_I},
            {L"O", DIK_O},
            {L"P", DIK_P},
            {L"LBRACKET", DIK_LBRACKET},
            {L"RBRACKET", DIK_RBRACKET},
            {L"RETURN", DIK_RETURN},
            {L"LCONTROL", DIK_LCONTROL},
            {L"A", DIK_A},
            {L"S", DIK_S},
            {L"D", DIK_D},
            {L"F", DIK_F},
            {L"G", DIK_G},
            {L"H", DIK_H},
            {L"J", DIK_J},
            {L"K", DIK_K},
            {L"L", DIK_L},
            {L"SEMICOLON", DIK_SEMICOLON},
            {L"APOSTROPHE", DIK_APOSTROPHE},
            {L"GRAVE", DIK_GRAVE},
            {L"LSHIFT", DIK_LSHIFT},
            {L"BACKSLASH", DIK_BACKSLASH},
            {L"Z", DIK_Z},
            {L"X", DIK_X},
            {L"C", DIK_C},
            {L"V", DIK_V},
            {L"B", DIK_B},
            {L"N", DIK_N},
            {L"M", DIK_M},
            {L"COMMA", DIK_COMMA},
            {L"PERIOD", DIK_PERIOD},
            {L"SLASH", DIK_SLASH},
            {L"RSHIFT", DIK_RSHIFT},
            {L"MULTIPLY", DIK_MULTIPLY},
            {L"LMENU", DIK_LMENU},
            {L"SPACE", DIK_SPACE},
            {L"CAPITAL", DIK_CAPITAL},
            {L"F1", DIK_F1},
            {L"F2", DIK_F2},
            {L"F3", DIK_F3},
            {L"F4", DIK_F4},
            {L"F5", DIK_F5},
            {L"F6", DIK_F6},
            {L"F7", DIK_F7},
            {L"F8", DIK_F8},
            {L"F9", DIK_F9},
            {L"F10", DIK_F10},
            {L"NUMLOCK", DIK_NUMLOCK},
            {L"SCROLL", DIK_SCROLL},
            {L"NUMPAD7", DIK_NUMPAD7},
            {L"NUMPAD8", DIK_NUMPAD8},
            {L"NUMPAD9", DIK_NUMPAD9},
            {L"SUBTRACT", DIK_SUBTRACT},
            {L"NUMPAD4", DIK_NUMPAD4},
            {L"NUMPAD5", DIK_NUMPAD5},
            {L"NUMPAD6", DIK_NUMPAD6},
            {L"ADD", DIK_ADD},
            {L"NUMPAD1", DIK_NUMPAD1},
            {L"NUMPAD2", DIK_NUMPAD2},
            {L"NUMPAD3", DIK_NUMPAD3},
            {L"NUMPAD0", DIK_NUMPAD0},
            {L"DECIMAL", DIK_DECIMAL},
            {L"OEM_102", DIK_OEM_102},
            {L"F11", DIK_F11},
            {L"F12", DIK_F12},
            {L"F13", DIK_F13},
            {L"F14", DIK_F14},
            {L"F15", DIK_F15},
            {L"KANA", DIK_KANA},
            {L"ABNT_C1", DIK_ABNT_C1},
            {L"CONVERT", DIK_CONVERT},
            {L"NOCONVERT", DIK_NOCONVERT},
            {L"YEN", DIK_YEN},
            {L"ABNT_C2", DIK_ABNT_C2},
            {L"NUMPADEQUALS", DIK_NUMPADEQUALS},
            {L"PREVTRACK", DIK_PREVTRACK},
            {L"AT", DIK_AT},
            {L"COLON", DIK_COLON},
            {L"UNDERLINE", DIK_UNDERLINE},
            {L"KANJI", DIK_KANJI},
            {L"STOP", DIK_STOP},
            {L"AX", DIK_AX},
            {L"UNLABELED", DIK_UNLABELED},
            {L"NEXTTRACK", DIK_NEXTTRACK},
            {L"NUMPADENTER", DIK_NUMPADENTER},
            {L"RCONTROL", DIK_RCONTROL},
            {L"MUTE", DIK_MUTE},
            {L"CALCULATOR", DIK_CALCULATOR},
            {L"PLAYPAUSE", DIK_PLAYPAUSE},
            {L"MEDIASTOP", DIK_MEDIASTOP},
            {L"VOLUMEDOWN", DIK_VOLUMEDOWN},
            {L"VOLUMEUP", DIK_VOLUMEUP},
            {L"WEBHOME", DIK_WEBHOME},
            {L"NUMPADCOMMA", DIK_NUMPADCOMMA},
            {L"DIVIDE", DIK_DIVIDE},
            {L"SYSRQ", DIK_SYSRQ},
            {L"RMENU", DIK_RMENU},
            {L"PAUSE", DIK_PAUSE},
            {L"HOME", DIK_HOME},
            {L"UP", DIK_UP},
            {L"PRIOR", DIK_PRIOR},
            {L"LEFT", DIK_LEFT},
            {L"RIGHT", DIK_RIGHT},
            {L"END", DIK_END},
            {L"DOWN", DIK_DOWN},
            {L"NEXT", DIK_NEXT},
            {L"INSERT", DIK_INSERT},
            {L"DELETE", DIK_DELETE},
            {L"LWIN", DIK_LWIN},
            {L"RWIN", DIK_RWIN},
            {L"APPS", DIK_APPS},
            {L"POWER", DIK_POWER},
            {L"SLEEP", DIK_SLEEP},
            {L"WAKE", DIK_WAKE},
            {L"WEBSEARCH", DIK_WEBSEARCH},
            {L"WEBFAVORITES", DIK_WEBFAVORITES},
            {L"WEBREFRESH", DIK_WEBREFRESH},
            {L"WEBSTOP", DIK_WEBSTOP},
            {L"WEBFORWARD", DIK_WEBFORWARD},
            {L"WEBBACK", DIK_WEBBACK},
            {L"MYCOMPUTER", DIK_MYCOMPUTER},
            {L"MAIL", DIK_MAIL},
            {L"MEDIASELECT", DIK_MEDIASELECT},
            {L"BACKSPACE", DIK_BACKSPACE},
            {L"NUMPADSTAR", DIK_NUMPADSTAR},
            {L"LALT", DIK_LALT},
            {L"CAPSLOCK", DIK_CAPSLOCK},
            {L"NUMPADMINUS", DIK_NUMPADMINUS},
            {L"NUMPADPLUS", DIK_NUMPADPLUS},
            {L"NUMPADPERIOD", DIK_NUMPADPERIOD},
            {L"NUMPADSLASH", DIK_NUMPADSLASH},
            {L"RALT", DIK_RALT},
            {L"UPARROW", DIK_UPARROW},
            {L"PGUP", DIK_PGUP},
            {L"LEFTARROW", DIK_LEFTARROW},
            {L"RIGHTARROW", DIK_RIGHTARROW},
            {L"DOWNARROW", DIK_DOWNARROW},
            {L"PGDN", DIK_PGDN}};

        static constexpr size_t kMaxChars = 24;
        if (kbString.length() >= kMaxChars) return std::nullopt;

        static constexpr std::wstring_view kOptionalPrefix = L"DIK_";
        if (true == kbString.starts_with(kOptionalPrefix))
          kbString.remove_prefix(kOptionalPrefix.length());
        if (true == kbString.empty()) return std::nullopt;

        // Create an uppercase null-terminated version of the scancode string by copying the
        // characters into a small buffer while also transforming them.
        wchar_t convertBuffer[1 + kMaxChars];
        convertBuffer[kMaxChars] = L'\0';
        for (size_t i = 0; i < kMaxChars; ++i)
        {
          if (i < kbString.length())
          {
            convertBuffer[i] = std::towupper(kbString[i]);
          }
          else
          {
            convertBuffer[i] = L'\0';
            break;
          }
        }

        const auto keyboardScanCodeIter = kKeyboardScanCodeStrings.find(convertBuffer);
        if (kKeyboardScanCodeStrings.cend() == keyboardScanCodeIter)
          return std::nullopt;
        else
          return keyboardScanCodeIter->second;
      }

      /// Parses a string representation of a mouse button into a mouse button enumerator.
      /// This function will fail if the input string is too long or if it does not entirely
      /// represent a mouse button.
      /// @param [in] mbString String from which to parse.
      /// @return Parsed mouse button enumerator if successful.
      static std::optional<Mouse::EMouseButton> ParseMouseButton(std::wstring_view mbString)
      {
        // Map of strings representing mouse buttons.
        static const std::map<std::wstring_view, Mouse::EMouseButton> kMouseButtonStrings = {

            // Left button
            {L"left", Mouse::EMouseButton::Left},
            {L"Left", Mouse::EMouseButton::Left},
            {L"leftbutton", Mouse::EMouseButton::Left},
            {L"Leftbutton", Mouse::EMouseButton::Left},
            {L"LeftButton", Mouse::EMouseButton::Left},

            // Middle button, often also the button beneath the mouse wheel
            {L"mid", Mouse::EMouseButton::Middle},
            {L"Mid", Mouse::EMouseButton::Middle},
            {L"middle", Mouse::EMouseButton::Middle},
            {L"Middle", Mouse::EMouseButton::Middle},
            {L"middlebutton", Mouse::EMouseButton::Middle},
            {L"Middlebutton", Mouse::EMouseButton::Middle},
            {L"MiddleButton", Mouse::EMouseButton::Middle},
            {L"wheel", Mouse::EMouseButton::Middle},
            {L"Wheel", Mouse::EMouseButton::Middle},
            {L"wheelbutton", Mouse::EMouseButton::Middle},
            {L"WheelButton", Mouse::EMouseButton::Middle},

            // Right button
            {L"right", Mouse::EMouseButton::Right},
            {L"Right", Mouse::EMouseButton::Right},
            {L"rightbutton", Mouse::EMouseButton::Right},
            {L"Rightbutton", Mouse::EMouseButton::Right},
            {L"RightButton", Mouse::EMouseButton::Right},

            // X1 button, often also used as "back" in internet browsers
            {L"x1", Mouse::EMouseButton::X1},
            {L"X1", Mouse::EMouseButton::X1},
            {L"x1button", Mouse::EMouseButton::X1},
            {L"X1Button", Mouse::EMouseButton::X1},
            {L"back", Mouse::EMouseButton::X1},
            {L"Back", Mouse::EMouseButton::X1},
            {L"backbutton", Mouse::EMouseButton::X1},
            {L"Backbutton", Mouse::EMouseButton::X1},
            {L"BackButton", Mouse::EMouseButton::X1},

            // X2 button, often also used as "forward" in internet browsers
            {L"x2", Mouse::EMouseButton::X2},
            {L"X2", Mouse::EMouseButton::X2},
            {L"x2button", Mouse::EMouseButton::X2},
            {L"X2Button", Mouse::EMouseButton::X2},
            {L"forward", Mouse::EMouseButton::X2},
            {L"Forward", Mouse::EMouseButton::X2},
            {L"forwardbutton", Mouse::EMouseButton::X2},
            {L"Forwardbutton", Mouse::EMouseButton::X2},
            {L"ForwardButton", Mouse::EMouseButton::X2}};

        const auto mouseButtonIter = kMouseButtonStrings.find(mbString);
        if (kMouseButtonStrings.cend() == mouseButtonIter)
          return std::nullopt;
        else
          return mouseButtonIter->second;
      }

      /// Trims all whitespace from the back of the supplied string.
      /// @param [in] stringToTrim Input string to be trimmed.
      /// @return Trimmed version of the input string, which may be empty if the input string is
      /// entirely whitespace.
      static inline std::wstring_view TrimWhitespaceBack(std::wstring_view stringToTrim)
      {
        const size_t lastNonWhitespacePosition = stringToTrim.find_last_not_of(kCharSetWhitespace);
        if (std::wstring_view::npos == lastNonWhitespacePosition) return std::wstring_view();

        return stringToTrim.substr(0, 1 + lastNonWhitespacePosition);
      }

      /// Trims all whitespace from the front of the supplied string.
      /// @param [in] stringToTrim Input string to be trimmed.
      /// @return Trimmed version of the input string, which may be empty if the input string is
      /// entirely whitespace.
      static inline std::wstring_view TrimWhitespaceFront(std::wstring_view stringToTrim)
      {
        const size_t firstNonWhitespacePosition =
            stringToTrim.find_first_not_of(kCharSetWhitespace);
        if (std::wstring_view::npos == firstNonWhitespacePosition) return std::wstring_view();

        return stringToTrim.substr(firstNonWhitespacePosition);
      }

      /// Trims all whitespace from the front and back of the supplied string.
      /// @param [in] stringToTrim Input string to be trimmed.
      /// @return Trimmed version of the input string, which may be empty if the input string is
      /// entirely whitespace.
      static inline std::wstring_view TrimWhitespace(std::wstring_view stringToTrim)
      {
        return TrimWhitespaceBack(TrimWhitespaceFront(stringToTrim));
      }

      std::optional<unsigned int> FindControllerElementIndex(
          std::wstring_view controllerElementString)
      {
        // Map of strings representing controller elements to indices within the element map data
        // structure. One pair exists per field in the SElementMap structure.
        static const std::map<std::wstring_view, unsigned int> kControllerElementStrings = {
            {L"StickLeftX", ELEMENT_MAP_INDEX_OF(stickLeftX)},
            {L"StickLeftY", ELEMENT_MAP_INDEX_OF(stickLeftY)},
            {L"StickRightX", ELEMENT_MAP_INDEX_OF(stickRightX)},
            {L"StickRightY", ELEMENT_MAP_INDEX_OF(stickRightY)},
            {L"DpadUp", ELEMENT_MAP_INDEX_OF(dpadUp)},
            {L"DpadDown", ELEMENT_MAP_INDEX_OF(dpadDown)},
            {L"DpadLeft", ELEMENT_MAP_INDEX_OF(dpadLeft)},
            {L"DpadRight", ELEMENT_MAP_INDEX_OF(dpadRight)},
            {L"TriggerLT", ELEMENT_MAP_INDEX_OF(triggerLT)},
            {L"TriggerRT", ELEMENT_MAP_INDEX_OF(triggerRT)},
            {L"ButtonA", ELEMENT_MAP_INDEX_OF(buttonA)},
            {L"ButtonB", ELEMENT_MAP_INDEX_OF(buttonB)},
            {L"ButtonX", ELEMENT_MAP_INDEX_OF(buttonX)},
            {L"ButtonY", ELEMENT_MAP_INDEX_OF(buttonY)},
            {L"ButtonLB", ELEMENT_MAP_INDEX_OF(buttonLB)},
            {L"ButtonRB", ELEMENT_MAP_INDEX_OF(buttonRB)},
            {L"ButtonBack", ELEMENT_MAP_INDEX_OF(buttonBack)},
            {L"ButtonStart", ELEMENT_MAP_INDEX_OF(buttonStart)},
            {L"ButtonLS", ELEMENT_MAP_INDEX_OF(buttonLS)},
            {L"ButtonRS", ELEMENT_MAP_INDEX_OF(buttonRS)}};

        const auto controllerElementIter = kControllerElementStrings.find(controllerElementString);
        if (kControllerElementStrings.cend() == controllerElementIter)
          return std::nullopt;
        else
          return controllerElementIter->second;
      }

      std::optional<unsigned int> FindForceFeedbackActuatorIndex(std::wstring_view ffActuatorString)
      {
        // Map of strings representing controller elements to indices within the element map data
        // structure. One pair exists per field in the SForceFeedbackActuatorMap structure.
        static const std::map<std::wstring_view, unsigned int> kForceFeedbackActuatorStrings = {
            {L"ForceFeedback.LeftMotor", FFACTUATOR_MAP_INDEX_OF(leftMotor)},
            {L"ForceFeedback.RightMotor", FFACTUATOR_MAP_INDEX_OF(rightMotor)}};

        const auto ffActuatorIter = kForceFeedbackActuatorStrings.find(ffActuatorString);
        if (kForceFeedbackActuatorStrings.cend() == ffActuatorIter)
          return std::nullopt;
        else
          return ffActuatorIter->second;
      }

      ElementMapperOrError ElementMapperFromString(std::wstring_view elementMapperString)
      {
        const std::optional<unsigned int> maybeRecursionDepth =
            ComputeRecursionDepth(elementMapperString);
        if (false == maybeRecursionDepth.has_value())
          return Infra::Strings::Format(L"Syntax error: Unbalanced parentheses").Data();

        const unsigned int kRecursionDepth = ComputeRecursionDepth(elementMapperString).value();
        if (kRecursionDepth > kElementMapperMaxRecursionDepth)
          return Infra::Strings::Format(
                     L"Nesting depth %u exceeds limit of %u",
                     kRecursionDepth,
                     kElementMapperMaxRecursionDepth)
              .Data();

        SElementMapperParseResult parseResult = ParseSingleElementMapper(elementMapperString);
        if (false == parseResult.maybeElementMapper.HasValue())
          return std::move(parseResult.maybeElementMapper);
        else if (false == parseResult.remainingString.empty())
          return Infra::Strings::Format(
                     L"\"%s\" is extraneous", std::wstring(parseResult.remainingString).c_str())
              .Data();

        return std::move(parseResult.maybeElementMapper);
      }

      ForceFeedbackActuatorOrError ForceFeedbackActuatorFromString(
          std::wstring_view ffActuatorString)
      {
        const std::optional<unsigned int> maybeRecursionDepth =
            ComputeRecursionDepth(ffActuatorString);
        if (false == maybeRecursionDepth.has_value())
          return Infra::Strings::Format(L"Syntax error: Unbalanced parentheses").Data();

        const unsigned int kRecursionDepth = ComputeRecursionDepth(ffActuatorString).value();
        if (kRecursionDepth > 1) return L"Nesting is not allowed for force feedback actuators";

        return ParseForceFeedbackActuator(ffActuatorString);
      }

      std::optional<unsigned int> ComputeRecursionDepth(std::wstring_view elementMapperString)
      {
        unsigned int recursionDepth = 0;
        unsigned int maxRecursionDepth = 0;

        for (auto elementMapperChar : elementMapperString)
        {
          switch (elementMapperChar)
          {
            case kCharElementMapperBeginParams:
              recursionDepth += 1;
              if (recursionDepth > maxRecursionDepth) maxRecursionDepth = recursionDepth;
              break;

            case kCharElementMapperEndParams:
              if (0 == recursionDepth) return std::nullopt;
              recursionDepth -= 1;
              break;

            default:
              break;
          }
        }

        if (0 != recursionDepth) return std::nullopt;

        return maxRecursionDepth;
      }

      std::optional<SStringParts> ExtractElementMapperStringParts(
          std::wstring_view elementMapperString)
      {
        // First, look for the end of the "type" part of the string. This just means looking for the
        // first possible separator character. Example: "Axis(X)" means the type is "Axis" Example:
        // "Split(Null, Pov(Up))" means the type is "Split" Example: "Null, Pov(Up)" (parameters in
        // the above example) means the type is "Null"
        const size_t separatorPosition =
            elementMapperString.find_first_of(kCharSetElementMapperTypeSeparator);

        if (std::wstring_view::npos == separatorPosition)
        {
          // No separator characters were found at all.
          // Example: "Null" in which case we got to the end of the string with no separator
          // character identified.

          // The entire string is consumed and is the type. There are no parameters and no remaining
          // parts to the string.
          return SStringParts({.type = TrimWhitespace(elementMapperString)});
        }
        else if (kCharElementMapperBeginParams != elementMapperString[separatorPosition])
        {
          // A separator character was found but it does not begin a parameter list.
          // Example: "Null, Pov(Up)" in which case we parsed the "Null" and stopped at the comma.

          // The only possible separator character in this situation is a comma, otherwise it is an
          // error.
          if (kCharElementMapperParamSeparator != elementMapperString[separatorPosition])
            return std::nullopt;

          // Type string goes up to the separator character and the remaining part of the string
          // comes afterwards.
          const std::wstring_view typeString =
              TrimWhitespace(elementMapperString.substr(0, separatorPosition));
          const std::wstring_view remainingString =
              TrimWhitespace(elementMapperString.substr(1 + separatorPosition));

          // If the remaining string is empty, it means the comma is a dangling comma which is a
          // syntax error.
          if (true == remainingString.empty()) return std::nullopt;

          return SStringParts({.type = typeString, .remaining = remainingString});
        }
        else
        {
          // A separator character was found and it does begin a parameter list.
          // Example: "Axis(X)" and "Split(Null, Pov(Up))" in both of which cases we stopped at the
          // open parenthesis character.
          const size_t paramListStartPos = 1 + separatorPosition;
          const size_t paramListLength =
              FindParamListEndPosition(elementMapperString.substr(paramListStartPos));
          const size_t paramListEndPos = paramListLength + paramListStartPos;

          // It is an error if a parameter list starting character was found with no matching end
          // character.
          if (std::wstring_view::npos == paramListLength) return std::nullopt;

          // Figure out what part of the string is remaining. It is possible that there is nothing
          // left or that we stopped at a comma. Example: "Axis(X)" in which case "Axis" is the
          // type, "X" is the parameter string, and there is nothing left as a remainder. This is
          // true whether or not there are dangling whitespace characters at the end of the input.
          // Example: "Split(Button(1), Button(2)), Split(Button(3), Button(4))" in which case
          // "Split" is the type, "Button(1), Button(2)" is the parameter string, and we need to
          // skip over the comma to obtain "Split(Button(3), Button(4))" as the remaining string.
          std::wstring_view possibleRemainingString =
              TrimWhitespaceFront(elementMapperString.substr(1 + paramListEndPos));
          if (false == possibleRemainingString.empty())
          {
            // The only possible separator that would have given rise to this situation is a comma.
            // Any other character at this point indicates an error.
            if (kCharElementMapperParamSeparator != possibleRemainingString.front())
              return std::nullopt;

            // If after skipping over the comma there is nothing left, then the comma is a dangling
            // comma which is an error.
            possibleRemainingString = TrimWhitespace(possibleRemainingString.substr(1));
            if (true == possibleRemainingString.empty()) return std::nullopt;
          }

          const std::wstring_view typeString =
              TrimWhitespace(elementMapperString.substr(0, separatorPosition));
          const std::wstring_view paramString =
              TrimWhitespace(elementMapperString.substr(paramListStartPos, paramListLength));

          // Empty parameter lists are not allowed.
          if (true == paramString.empty()) return std::nullopt;

          const std::wstring_view remainingString = possibleRemainingString;

          return SStringParts(
              {.type = typeString, .params = paramString, .remaining = remainingString});
        }
      }

      std::optional<SStringParts> ExtractForceFeedbackActuatorStringParts(
          std::wstring_view ffActuatorString)
      {
        std::optional<SStringParts> maybeStringParts =
            ExtractElementMapperStringParts(ffActuatorString);

        if (false == maybeStringParts.has_value()) return std::nullopt;

        if (false == maybeStringParts.value().remaining.empty()) return std::nullopt;

        return maybeStringParts;
      }

      std::optional<SParamStringParts> ExtractParameterListStringParts(
          std::wstring_view paramListString)
      {
        const size_t firstParamEndPosition = FindFirstParameterEndPosition(paramListString);

        if (std::wstring_view::npos == firstParamEndPosition) return std::nullopt;

        const std::wstring_view firstParamString =
            TrimWhitespace(paramListString.substr(0, firstParamEndPosition));

        if (paramListString.length() == firstParamEndPosition)
        {
          // Entire input string was consumed and no comma was located.
          return SParamStringParts({.first = firstParamString});
        }

        const std::wstring_view remainingString =
            TrimWhitespace(paramListString.substr(1 + firstParamEndPosition));

        if (true == remainingString.empty())
        {
          // A comma was located but nothing appears after it.
          // This is an error because it indicates a dangling comma.
          return std::nullopt;
        }

        return SParamStringParts({.first = firstParamString, .remaining = remainingString});
      }

      ElementMapperOrError MakeAxisMapper(std::wstring_view params)
      {
        const AxisParamsOrError<EAxis> maybeAxisMapperParams = ParseAxisParams<EAxis>(params);
        if (true == maybeAxisMapperParams.HasError())
          return Infra::Strings::Format(L"Axis: %s", maybeAxisMapperParams.Error().c_str()).Data();

        return std::make_unique<AxisMapper>(
            maybeAxisMapperParams.Value().axis, maybeAxisMapperParams.Value().direction);
      }

      ElementMapperOrError MakeButtonMapper(std::wstring_view params)
      {
        const std::optional<unsigned int> maybeButtonNumber = ParseUnsignedInteger(params, 10);
        if (false == maybeButtonNumber.has_value())
          return Infra::Strings::Format(
                     L"Button: Parameter \"%s\" must be a number between 1 and %u",
                     std::wstring(params).c_str(),
                     (unsigned int)EButton::Count)
              .Data();

        const unsigned int kButtonNumber = maybeButtonNumber.value() - 1;
        if (kButtonNumber >= (unsigned int)EButton::Count)
          return Infra::Strings::Format(
                     L"Button: Parameter \"%s\" must be a number between 1 and %u",
                     std::wstring(params).c_str(),
                     (unsigned int)EButton::Count)
              .Data();

        return std::make_unique<ButtonMapper>((EButton)kButtonNumber);
      }

      ElementMapperOrError MakeCompoundMapper(std::wstring_view params)
      {
        CompoundMapper::TElementMappers elementMappers;
        SElementMapperParseResult elementMapperResult = {
            .maybeElementMapper = nullptr, .remainingString = params};

        // Parse element mappers one at a time.
        // At least one underlying element mapper is required, with all the rest being optional.
        for (size_t i = 0; i < elementMappers.size(); ++i)
        {
          elementMapperResult = ParseSingleElementMapper(elementMapperResult.remainingString);
          if (false == elementMapperResult.maybeElementMapper.HasValue())
            return Infra::Strings::Format(
                       L"Compound: Parameter %u: %s",
                       (unsigned int)(1 + i),
                       elementMapperResult.maybeElementMapper.Error().c_str())
                .Data();

          elementMappers[i] = std::move(elementMapperResult.maybeElementMapper.Value());

          if (true == elementMapperResult.remainingString.empty()) break;
        }

        // No further parameters allowed.
        // Specifying too many underlying element mappers is an error.
        if (false == elementMapperResult.remainingString.empty())
          return Infra::Strings::Format(
                     L"Compound: Number of parameters exceeds limit of %u",
                     (unsigned int)elementMappers.size())
              .Data();

        return std::make_unique<CompoundMapper>(std::move(elementMappers));
      }

      ElementMapperOrError MakeDigitalAxisMapper(std::wstring_view params)
      {
        const AxisParamsOrError<EAxis> maybeAxisMapperParams = ParseAxisParams<EAxis>(params);
        if (true == maybeAxisMapperParams.HasError())
          return Infra::Strings::Format(L"DigitalAxis: %s", maybeAxisMapperParams.Error().c_str())
              .Data();

        return std::make_unique<DigitalAxisMapper>(
            maybeAxisMapperParams.Value().axis, maybeAxisMapperParams.Value().direction);
      }

      ElementMapperOrError MakeInvertMapper(std::wstring_view params)
      {
        SElementMapperParseResult elementMapperResult = ParseSingleElementMapper(params);

        if (false == elementMapperResult.maybeElementMapper.HasValue())
          return Infra::Strings::Format(
                     L"Invert: Parameter 1: %s",
                     elementMapperResult.maybeElementMapper.Error().c_str())
              .Data();
        else if (false == elementMapperResult.remainingString.empty())
          return Infra::Strings::Format(
                     L"Invert: \"%s\" is extraneous",
                     std::wstring(elementMapperResult.remainingString).c_str())
              .Data();

        return std::make_unique<InvertMapper>(
            std::move(elementMapperResult.maybeElementMapper.Value()));
      }

      ElementMapperOrError MakeKeyboardMapper(std::wstring_view params)
      {
        // First try parsing a friendly string representation of the keyboard scan code (i.e.
        // strings that look like "DIK_*" constants, the "DIK_" prefix being optional). If that
        // fails, try interpreting the scan code as an unsigned integer directly, with the
        // possibility that it could be represented in decimal, octal, or hexadecimal. If both
        // attempts fail then the scancode cannot be parsed, which is an error.
        std::optional<unsigned int> maybeKeyScanCode = ParseKeyboardScancode(params);
        if (false == maybeKeyScanCode.has_value()) maybeKeyScanCode = ParseUnsignedInteger(params);
        if (false == maybeKeyScanCode.has_value())
          return Infra::Strings::Format(
                     L"Keyboard: \"%s\" must map to a scan code between 0 and %u",
                     std::wstring(params).c_str(),
                     (Keyboard::kVirtualKeyboardKeyCount - 1))
              .Data();

        const unsigned int kKeyScanCode = maybeKeyScanCode.value();
        if (kKeyScanCode >= Keyboard::kVirtualKeyboardKeyCount)
          return Infra::Strings::Format(
                     L"Keyboard: \"%s\" must map to a scan code between 0 and %u",
                     std::wstring(params).c_str(),
                     (Keyboard::kVirtualKeyboardKeyCount - 1))
              .Data();

        return std::make_unique<KeyboardMapper>((Keyboard::TKeyIdentifier)kKeyScanCode);
      }

      ElementMapperOrError MakeMouseAxisMapper(std::wstring_view params)
      {
        const AxisParamsOrError<Mouse::EMouseAxis> maybeMouseAxisMapperParams =
            ParseAxisParams<Mouse::EMouseAxis>(params);
        if (true == maybeMouseAxisMapperParams.HasError())
          return Infra::Strings::Format(
                     L"MouseAxis: %s", maybeMouseAxisMapperParams.Error().c_str())
              .Data();

        return std::make_unique<MouseAxisMapper>(
            maybeMouseAxisMapperParams.Value().axis, maybeMouseAxisMapperParams.Value().direction);
      }

      ElementMapperOrError MakeMouseButtonMapper(std::wstring_view params)
      {
        std::optional<Mouse::EMouseButton> maybeMouseButton = ParseMouseButton(params);
        if (false == maybeMouseButton.has_value())
          return Infra::Strings::Format(
                     L"MouseButton: \"%s\" must map to a valid mouse button",
                     std::wstring(params).c_str())
              .Data();

        const Mouse::EMouseButton mouseButton = maybeMouseButton.value();
        return std::make_unique<MouseButtonMapper>(mouseButton);
      }

      ElementMapperOrError MakeNullMapper(std::wstring_view params)
      {
        if (false == params.empty())
          return Infra::Strings::Format(L"Null: \"%s\" is extraneous", std::wstring(params).c_str())
              .Data();

        return nullptr;
      }

      ElementMapperOrError MakePovMapper(std::wstring_view params)
      {
        // Map of strings representing axes to POV direction.
        static const std::map<std::wstring_view, EPovDirection> kPovDirectionStrings = {
            {L"u", EPovDirection::Up},        {L"U", EPovDirection::Up},
            {L"up", EPovDirection::Up},       {L"Up", EPovDirection::Up},
            {L"UP", EPovDirection::Up},

            {L"d", EPovDirection::Down},      {L"D", EPovDirection::Down},
            {L"dn", EPovDirection::Down},     {L"Dn", EPovDirection::Down},
            {L"DN", EPovDirection::Down},     {L"down", EPovDirection::Down},
            {L"Down", EPovDirection::Down},   {L"DOWN", EPovDirection::Down},

            {L"l", EPovDirection::Left},      {L"L", EPovDirection::Left},
            {L"lt", EPovDirection::Left},     {L"Lt", EPovDirection::Left},
            {L"LT", EPovDirection::Left},     {L"left", EPovDirection::Left},
            {L"Left", EPovDirection::Left},   {L"LEFT", EPovDirection::Left},

            {L"r", EPovDirection::Right},     {L"R", EPovDirection::Right},
            {L"rt", EPovDirection::Right},    {L"Rt", EPovDirection::Right},
            {L"RT", EPovDirection::Right},    {L"right", EPovDirection::Right},
            {L"Right", EPovDirection::Right}, {L"RIGHT", EPovDirection::Right},
        };

        const auto povDirectionIter = kPovDirectionStrings.find(params);
        if (kPovDirectionStrings.cend() == povDirectionIter)
          return Infra::Strings::Format(
                     L"Pov: %s: Unrecognized POV direction", std::wstring(params).c_str())
              .Data();

        return std::make_unique<PovMapper>(povDirectionIter->second);
      }

      ElementMapperOrError MakeSplitMapper(std::wstring_view params)
      {
        // First parameter is required. It is a string that specifies the positive element mapper.
        SElementMapperParseResult positiveElementMapperResult = ParseSingleElementMapper(params);
        if (false == positiveElementMapperResult.maybeElementMapper.HasValue())
          return Infra::Strings::Format(
                     L"Split: Parameter 1: %s",
                     positiveElementMapperResult.maybeElementMapper.Error().c_str())
              .Data();

        // Second parameter is required. It is a string that specifies the negative element mapper.
        SElementMapperParseResult negativeElementMapperResult =
            ParseSingleElementMapper(positiveElementMapperResult.remainingString);
        if (false == negativeElementMapperResult.maybeElementMapper.HasValue())
          return Infra::Strings::Format(
                     L"Split: Parameter 2: %s",
                     negativeElementMapperResult.maybeElementMapper.Error().c_str())
              .Data();

        // No further parameters allowed.
        if (false == negativeElementMapperResult.remainingString.empty())
          return Infra::Strings::Format(
                     L"Split: \"%s\" is extraneous",
                     std::wstring(negativeElementMapperResult.remainingString).c_str())
              .Data();

        return std::make_unique<SplitMapper>(
            std::move(positiveElementMapperResult.maybeElementMapper.Value()),
            std::move(negativeElementMapperResult.maybeElementMapper.Value()));
      }

      ForceFeedbackActuatorOrError MakeForceFeedbackActuatorDefault(std::wstring_view params)
      {
        if (false == params.empty())
          return Infra::Strings::Format(
                     L"Default: \"%s\" is extraneous", std::wstring(params).c_str())
              .Data();

        const ForceFeedback::SActuatorElement actuatorElement =
            Mapper::kDefaultForceFeedbackActuator;

        return actuatorElement;
      }

      ForceFeedbackActuatorOrError MakeForceFeedbackActuatorDisabled(std::wstring_view params)
      {
        if (false == params.empty())
          return Infra::Strings::Format(
                     L"Disabled: \"%s\" is extraneous", std::wstring(params).c_str())
              .Data();

        const ForceFeedback::SActuatorElement actuatorElement = {.isPresent = false};

        return actuatorElement;
      }

      ForceFeedbackActuatorOrError MakeForceFeedbackActuatorSingleAxis(std::wstring_view params)
      {
        const AxisParamsOrError<EAxis> maybeAxisMapperParams = ParseAxisParams<EAxis>(params);
        if (true == maybeAxisMapperParams.HasError())
          return Infra::Strings::Format(L"SingleAxis: %s", maybeAxisMapperParams.Error().c_str())
              .Data();

        const ForceFeedback::SActuatorElement actuatorElement = {
            .isPresent = true,
            .mode = ForceFeedback::EActuatorMode::SingleAxis,
            .singleAxis = {
                .axis = maybeAxisMapperParams.Value().axis,
                .direction = maybeAxisMapperParams.Value().direction}};

        return actuatorElement;
      }

      ForceFeedbackActuatorOrError MakeForceFeedbackActuatorMagnitudeProjection(
          std::wstring_view params)
      {
        SParamStringParts paramParts =
            ExtractParameterListStringParts(params).value_or(SParamStringParts());

        // First parameter is required. It is a string that specifies the first axis in the
        // projection.
        if (true == paramParts.first.empty())
          return L"MagnitudeProjection: Missing or unparseable first axis";

        const std::optional<EAxis> maybeAxisFirst = AxisTypeFromString<EAxis>(paramParts.first);
        if (false == maybeAxisFirst.has_value())
          return Infra::Strings::Format(
                     L"MagnitudeProjection: %s: Unrecognized first axis",
                     std::wstring(paramParts.first).c_str())
              .Data();

        const EAxis axisFirst = maybeAxisFirst.value();

        // Second parameter is required. It is a string that specifies the second axis in the
        // projection.
        paramParts =
            ExtractParameterListStringParts(paramParts.remaining).value_or(SParamStringParts());
        if (true == paramParts.first.empty())
          return L"MagnitudeProjection: Missing or unparseable second axis";

        const std::optional<EAxis> maybeAxisSecond = AxisTypeFromString<EAxis>(paramParts.first);
        if (false == maybeAxisSecond.has_value())
          return Infra::Strings::Format(
                     L"MagnitudeProjection: %s: Unrecognized second axis",
                     std::wstring(paramParts.first).c_str())
              .Data();

        const EAxis axisSecond = maybeAxisSecond.value();
        if (axisFirst == axisSecond) return L"MagnitudeProjection: Axes must be different";

        // No further parameters allowed.
        if (false == paramParts.remaining.empty())
          return Infra::Strings::Format(
                     L"\"%s\" is extraneous", std::wstring(paramParts.remaining).c_str())
              .Data();

        const ForceFeedback::SActuatorElement actuatorElement = {
            .isPresent = true,
            .mode = ForceFeedback::EActuatorMode::MagnitudeProjection,
            .magnitudeProjection = {.axisFirst = axisFirst, .axisSecond = axisSecond}};

        return actuatorElement;
      }

      SElementMapperParseResult ParseSingleElementMapper(std::wstring_view elementMapperString)
      {
        static const std::map<std::wstring_view, TMakeElementMapperFunc>
            kMakeElementMapperFunctions = {
                {L"axis", &MakeAxisMapper},
                {L"Axis", &MakeAxisMapper},

                {L"button", &MakeButtonMapper},
                {L"Button", &MakeButtonMapper},

                {L"compound", &MakeCompoundMapper},
                {L"Compound", &MakeCompoundMapper},

                {L"digitalaxis", &MakeDigitalAxisMapper},
                {L"digitalAxis", &MakeDigitalAxisMapper},
                {L"Digitalaxis", &MakeDigitalAxisMapper},
                {L"DigitalAxis", &MakeDigitalAxisMapper},

                {L"invert", &MakeInvertMapper},
                {L"Invert", &MakeInvertMapper},

                {L"keyboard", &MakeKeyboardMapper},
                {L"Keyboard", &MakeKeyboardMapper},
                {L"keystroke", &MakeKeyboardMapper},
                {L"Keystroke", &MakeKeyboardMapper},
                {L"KeyStroke", &MakeKeyboardMapper},

                {L"mouseaxis", &MakeMouseAxisMapper},
                {L"Mouseaxis", &MakeMouseAxisMapper},
                {L"MouseAxis", &MakeMouseAxisMapper},

                {L"mousebutton", &MakeMouseButtonMapper},
                {L"Mousebutton", &MakeMouseButtonMapper},
                {L"MouseButton", &MakeMouseButtonMapper},

                {L"pov", &MakePovMapper},
                {L"Pov", &MakePovMapper},
                {L"POV", &MakePovMapper},
                {L"povhat", &MakePovMapper},
                {L"povHat", &MakePovMapper},
                {L"Povhat", &MakePovMapper},
                {L"PovHat", &MakePovMapper},

                {L"null", &MakeNullMapper},
                {L"Null", &MakeNullMapper},
                {L"nothing", &MakeNullMapper},
                {L"Nothing", &MakeNullMapper},
                {L"none", &MakeNullMapper},
                {L"None", &MakeNullMapper},
                {L"nil", &MakeNullMapper},
                {L"Nil", &MakeNullMapper},

                {L"split", &MakeSplitMapper},
                {L"Split", &MakeSplitMapper}};

        const std::optional<SStringParts> maybeElementMapperStringParts =
            ExtractElementMapperStringParts(elementMapperString);
        if (false == maybeElementMapperStringParts.has_value())
          return {
              .maybeElementMapper =
                  Infra::Strings::Format(
                      L"\"%s\" contains a syntax error", std::wstring(elementMapperString).c_str())
                      .Data()};

        const SStringParts& elementMapperStringParts = maybeElementMapperStringParts.value();
        if (true == elementMapperStringParts.type.empty())
          return {.maybeElementMapper = L"Missing or unparseable element mapper type."};

        const auto makeElementMapperIter =
            kMakeElementMapperFunctions.find(elementMapperStringParts.type);
        if (kMakeElementMapperFunctions.cend() == makeElementMapperIter)
          return {
              .maybeElementMapper = Infra::Strings::Format(
                                        L"%s: Unrecognized element mapper type",
                                        std::wstring(elementMapperStringParts.type).c_str())
                                        .Data()};

        return {
            .maybeElementMapper = makeElementMapperIter->second(elementMapperStringParts.params),
            .remainingString = elementMapperStringParts.remaining};
      }

      ForceFeedbackActuatorOrError ParseForceFeedbackActuator(std::wstring_view ffActuatorString)
      {
        static const std::map<std::wstring_view, TMakeForceFeedbackActuatorFunc>
            kMakeForceFeedbackActuatorFunctions = {
                {L"disable", &MakeForceFeedbackActuatorDisabled},
                {L"Disable", &MakeForceFeedbackActuatorDisabled},
                {L"disabled", &MakeForceFeedbackActuatorDisabled},
                {L"Disabled", &MakeForceFeedbackActuatorDisabled},
                {L"empty", &MakeForceFeedbackActuatorDisabled},
                {L"Empty", &MakeForceFeedbackActuatorDisabled},
                {L"none", &MakeForceFeedbackActuatorDisabled},
                {L"None", &MakeForceFeedbackActuatorDisabled},
                {L"nothing", &MakeForceFeedbackActuatorDisabled},
                {L"Nothing", &MakeForceFeedbackActuatorDisabled},
                {L"null", &MakeForceFeedbackActuatorDisabled},
                {L"Null", &MakeForceFeedbackActuatorDisabled},
                {L"off", &MakeForceFeedbackActuatorDisabled},
                {L"Off", &MakeForceFeedbackActuatorDisabled},
                {L"unused", &MakeForceFeedbackActuatorDisabled},
                {L"Unused", &MakeForceFeedbackActuatorDisabled},

                {L"default", &MakeForceFeedbackActuatorDefault},
                {L"Default", &MakeForceFeedbackActuatorDefault},

                {L"singleaxis", &MakeForceFeedbackActuatorSingleAxis},
                {L"SingleAxis", &MakeForceFeedbackActuatorSingleAxis},

                {L"magnitudeprojection", &MakeForceFeedbackActuatorMagnitudeProjection},
                {L"MagnitudeProjection", &MakeForceFeedbackActuatorMagnitudeProjection}};

        const std::optional<SStringParts> maybeForceFeedbackActuatorStringParts =
            ExtractForceFeedbackActuatorStringParts(ffActuatorString);
        if (false == maybeForceFeedbackActuatorStringParts.has_value())
          return Infra::Strings::Format(
                     L"\"%s\" contains a syntax error", std::wstring(ffActuatorString).c_str())
              .Data();

        const SStringParts& kForceFeedbackActuatorStringParts =
            maybeForceFeedbackActuatorStringParts.value();
        if (true == kForceFeedbackActuatorStringParts.type.empty())
          return L"Missing or unparseable element mapper type.";

        const auto makeForceFeedbackActuatorIter =
            kMakeForceFeedbackActuatorFunctions.find(kForceFeedbackActuatorStringParts.type);
        if (kMakeForceFeedbackActuatorFunctions.cend() == makeForceFeedbackActuatorIter)
          return Infra::Strings::Format(
                     L"%s: Unrecognized force feedback actuator mode",
                     std::wstring(kForceFeedbackActuatorStringParts.type).c_str())
              .Data();

        return makeForceFeedbackActuatorIter->second(kForceFeedbackActuatorStringParts.params);
      }
    } // namespace MapperParser
  }   // namespace Controller
} // namespace Xidi
