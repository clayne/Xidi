/***************************************************************************************************
 * Xidi
 *   DirectInput interface for XInput controllers.
 ***************************************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016-2025
 ***********************************************************************************************//**
 * @file StateChangeEventBuffer.h
 *   Declaration of buffered event functionality for virtual controller state change events.
 **************************************************************************************************/

#pragma once

#include <cstdint>

#include <boost/circular_buffer.hpp>

#include "ControllerTypes.h"

namespace Xidi
{
  namespace Controller
  {
    /// Implements a state change event buffer for a virtual controller. Used for providing buffered
    /// event functionality. Methods are not concurrency-safe, so some form of external concurrency
    /// control is required. Behavior is modelled after DirectInput buffered event documentation.
    /// For example, number of events stored is artificially limited to one less than declared
    /// capacity.
    class StateChangeEventBuffer
    {
    public:

      /// Holds state change event data.
      struct SEventData
      {
        /// Virtual controller element to which the event refers.
        SElementIdentifier element;

        union
        {
          /// Updated axis value, if the controller element type is an axis.
          int32_t axis;

          /// Updated button state, if the controller element type is a button.
          bool button;

          /// Updated POV direction state, if the controller element type is a POV.
          UPovDirection povDirection;
        } value;

        constexpr bool operator==(const SEventData& other) const
        {
          if (other.element.type != element.type) return false;

          switch (element.type)
          {
            case EElementType::Axis:
              return (other.value.axis == value.axis);

            case EElementType::Button:
              return (other.value.button == value.button);

            case EElementType::Pov:
              return (other.value.povDirection == value.povDirection);

            default:
              return true;
          }
        }
      };

      static_assert(sizeof(SEventData) <= 8, "Data structure size constraint violation.");

      /// Holds all the information that encompasses a single controller state change event.
      /// Includes state change event data along with additional metadata.
      /// Each element in an event buffer is an element of this type.
      struct SEvent
      {
        /// Event data, including virtual controller element and updated value.
        SEventData data;

        /// System time in milliseconds when the event was generated.
        uint32_t timestamp;

        /// Chronological sequence number of this event. Supposed to be globally monotonic with
        /// respect to all other input events, but in practice it is locally monotonic with respect
        /// to all virtual controller events.
        uint32_t sequence;
      };

      static_assert(sizeof(SEvent) <= 16, "Data structure size constraint violation.");

      /// Maximum allowed event buffer capacity, measured in number of events. Computed to allow a
      /// maximum of 1MB for event storage.
      static constexpr uint32_t kEventBufferCapacityMax = (1024 * 1024) / sizeof(SEvent);

      /// Constructs an empty event buffer with capacity of 0, which means this event buffer is
      /// disabled until it is enabled by request.
      inline StateChangeEventBuffer(void) : eventBuffer(), eventBufferOverflowed() {}

      /// Allows read-only access to events by index, without performing any bounds-checking. Event
      /// with index 0 is the oldest, and higher indices indicate more recent events.
      /// @param [in] index Index of the desired event.
      /// @return Read-only reference to the event at the desired index.
      inline const SEvent& operator[](uint32_t index) const
      {
        return eventBuffer[index];
      }

      /// Appends a single event to the event buffer, given its data.
      /// @param [in] eventData Event data to append.
      /// @param [in] timestamp Timestamp to apply to the appended event.
      void AppendEvent(SEventData eventData, uint32_t timestamp);

      /// Retrieves and returns the capacity of this event buffer.
      /// @return Event buffer capacity.
      inline uint32_t GetCapacity(void) const
      {
        return (uint32_t)eventBuffer.capacity();
      }

      /// Retrieves and returns the number of events currently present in this event buffer.
      /// @return Event count in this event buffer.
      inline uint32_t GetCount(void) const
      {
        return (uint32_t)eventBuffer.size();
      }

      /// Checks if this event buffer is enabled.
      /// @return `true` if the event buffer is enabled, `false` otherwise.
      inline bool IsEnabled(void) const
      {
        return (0 != GetCapacity());
      }

      /// Checks if an overflow condition has occurred on this buffer that has yet to be cleared.
      /// @return `true` if an overflow condition is present, `false` otherwise.
      inline bool IsOverflowed(void) const
      {
        return eventBufferOverflowed;
      }

      /// Removes and discards the oldest events from the buffer and clears any present overflow
      /// condition. Performs appropriate bounds-checking to ensure at most the specified number
      /// events are removed.
      /// @param [in] numEventsToPop Maximum number of events to remove.
      void PopOldestEvents(uint32_t numEventsToPop);

      /// Sets the capacity of this event buffer.
      /// Disables this event buffer if the specified capacity is equal to 0.
      /// Sets the capacity to #kEventBufferCapacityMax if the specified capacity is greater than
      /// this value. If the specified capacity is less than the number of events currently in the
      /// event buffer, an overflow condition is triggered and the oldest excess events are
      /// discarded. Buffer always maintains one free space, so the actual number of events stored
      /// is one less than capacity. This is to be consistent with documentation for
      /// IDirectInputDevice8::GetDeviceData.
      /// @param [in] capacity Desired event buffer capacity.
      void SetCapacity(uint32_t capacity);

    private:

      /// Underlying event buffer object. Holds all individual event elements.
      boost::circular_buffer<SEvent> eventBuffer;

      /// Overflow flag for the event buffer. Set whenever an operation causes the event buffer to
      /// hit capacity and discard some previously-stored events. Cleared whenever events are
      /// retrieved such that the event buffer goes below capacity.
      bool eventBufferOverflowed;
    };
  } // namespace Controller
} // namespace Xidi
