/*****************************************************************************
 * Xidi
 *   DirectInput interface for XInput controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016-2022
 *************************************************************************//**
 * @file ForceFeedbackEffect.cpp
 *   Implementation of internal force feedback effect computations.
 *****************************************************************************/

#include "ForceFeedbackEffect.h"
#include "ForceFeedbackMath.h"

#include <atomic>
#include <memory>
#include <optional>


namespace Xidi
{
    namespace Controller
    {
        namespace ForceFeedback
        {
            // -------- INTERNAL VARIABLES --------------------------------- //

            /// Holds the next available value for a force feedback effect identifier.
            static std::atomic<TEffectIdentifier> nextEffectIdentifier = 0;


            // -------- CONSTRUCTION AND DESTRUCTION ----------------------- //
            // See "ForceFeedbackEffect.h" for documentation.

            Effect::Effect(void) : id(nextEffectIdentifier++), commonParameters()
            {
                // Nothing to do here.
            }


            // -------- CONCRETE INSTANCE METHODS -------------------------- //
            // See "ForceFeedbackEffect.h" for documentation.

            bool ConstantForceEffect::AreTypeSpecificParametersValid(const SConstantForceParameters& newTypeSpecificParameters) const
            {
                return ((newTypeSpecificParameters.magnitude >= kEffectForceMagnitudeMinimum) && (newTypeSpecificParameters.magnitude <= kEffectForceMagnitudeMaximum));
            }

            // --------

            bool PeriodicEffect::AreTypeSpecificParametersValid(const SPeriodicParameters& newTypeSpecificParameters) const
            {
                if ((newTypeSpecificParameters.amplitude < 0) || (newTypeSpecificParameters.amplitude > kEffectForceMagnitudeMaximum))
                    return false;

                if ((newTypeSpecificParameters.offset < kEffectForceMagnitudeMinimum) || (newTypeSpecificParameters.offset > kEffectForceMagnitudeMaximum))
                    return false;

                if ((newTypeSpecificParameters.phase < kEffectAngleMinimum) || (newTypeSpecificParameters.phase > kEffectAngleMaximum))
                    return false;

                if (newTypeSpecificParameters.period < 1)
                    return false;

                return true;
            }

            // --------

            std::unique_ptr<Effect> ConstantForceEffect::Clone(void) const
            {
                return std::make_unique<ConstantForceEffect>(*this);
            }

            // --------

            std::unique_ptr<Effect> SineWaveEffect::Clone(void) const
            {
                return std::make_unique<SineWaveEffect>(*this);
            }

            // --------

            TEffectValue SineWaveEffect::WaveformAmplitude(TEffectValue phase) const
            {
                return TrigonometrySine(phase);
            }

            // --------

            TEffectValue PeriodicEffect::ComputePhase(TEffectTimeMs rawTime) const
            {
                const TEffectValue kRawTimeInPeriods = (TEffectValue)rawTime / (TEffectValue)GetTypeSpecificParameters().value().period;

                TEffectValue currentPhase = std::round(((kRawTimeInPeriods - floorf(kRawTimeInPeriods)) * 36000) + GetTypeSpecificParameters().value().phase);
                if (currentPhase >= 36000)
                    currentPhase -= 36000;

                return currentPhase;
            }

            // --------

            TEffectValue ConstantForceEffect::ComputeRawMagnitude(TEffectTimeMs rawTime) const
            {
                const TEffectValue kMagnitude = GetTypeSpecificParameters().value().magnitude;

                if (kMagnitude >= 0)
                    return ApplyEnvelope(rawTime, kMagnitude);
                else
                    return -ApplyEnvelope(rawTime, -kMagnitude);
            }

            // --------

            TEffectValue PeriodicEffect::ComputeRawMagnitude(TEffectTimeMs rawTime) const
            {
                const TEffectValue kModifiedAmplitude = ApplyEnvelope(rawTime, GetTypeSpecificParameters().value().amplitude);
                const TEffectValue kRawMagnitude = (kModifiedAmplitude * WaveformAmplitude(ComputePhase(rawTime))) + GetTypeSpecificParameters().value().offset;

                return std::min(kEffectForceMagnitudeMaximum, std::max(kEffectForceMagnitudeMinimum, kRawMagnitude));
            }


            // -------- INSTANCE METHODS ----------------------------------- //
            // See "ForceFeedbackEffect.h" for documentation.

            TEffectValue Effect::ApplyEnvelope(TEffectTimeMs rawTime, TEffectValue sustainLevel) const
            {
                if (false == commonParameters.envelope.has_value())
                    return sustainLevel;

                const SEnvelope& envelope = commonParameters.envelope.value();

                if (rawTime < envelope.attackTime)
                {
                    const TEffectTimeMs envelopeTime = rawTime;
                    const TEffectValue envelopeSlope = (sustainLevel - envelope.attackLevel) / envelope.attackTime;
                    return envelope.attackLevel + (envelopeSlope * envelopeTime);
                }
                else if (rawTime > commonParameters.duration.value() - envelope.fadeTime)
                {
                    const TEffectTimeMs envelopeTime = rawTime - (commonParameters.duration.value() - envelope.fadeTime);
                    const TEffectValue envelopeSlope = (envelope.fadeLevel - sustainLevel) / envelope.fadeTime;
                    return sustainLevel + (envelopeSlope * envelopeTime);
                }

                return sustainLevel;
            }

            // --------

            TEffectValue Effect::ComputeMagnitude(TEffectTimeMs time) const
            {
                if (time >= commonParameters.duration.value_or(0))
                    return kEffectForceMagnitudeZero;

                const TEffectTimeMs rawTime = time - (time % commonParameters.samplePeriodForComputations);
                return ComputeRawMagnitude(rawTime) * commonParameters.gainFraction;
            }
        }
    }
}
