#pragma once
#ifndef PEDAL

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "globals.h"
#include "hardware.h"

using namespace daisy;
using Oled = OledDisplay<SSD130x4WireSpi128x64Driver>;

namespace amc
{
	class Pedal
	{
	public:
		enum KnobI
		{ K1 = 0, K2, K3, K4, K5, K6, KN };

		enum SwitchI
		{ S1 = 0, S2, SN };

		enum EncoderI
		{ E1 = 0, EN };

		enum LedI
		{ L1 = 0, L2, LN };

		DaisySeed seed;
		Oled display;

		AnalogControl knobs[KnobI::KN];
		Encoder encoders[EncoderI::EN];
		Switch switches[SwitchI::SN];
		Led leds[LedI::LN];

		MidiUartHandler midi;

		GPIO audioBypassTrigger;
		bool audioBypass;

		GPIO audioMuteTrigger;
		bool audioMute;


		Pedal() { }
		~Pedal() { }

		void Init(bool boost = true); // expose CPU boost option
		void Init(float controlRate, bool boost = true); // expose CPU boost option

		void Delay(size_t milliseconds);

		void StartAudio(AudioHandle::InterleavingAudioCallback cb); // interleaved
		void StartAudio(AudioHandle::AudioCallback cb); // multichannel

		void ChangeAudio(AudioHandle::InterleavingAudioCallback cb); // interleaved
		void ChangeAudio(AudioHandle::AudioCallback cb); // multichannel

		void StopAudio(); // shut down audio stream

		void SetAudioRate(SaiHandle::Config::SampleRate samplerate); // set sample rate
		float AudioRate(); // get current sample rate

		void SetBlockSize(size_t size); // samples per channel per callback
		size_t BlockSize(); // get current block size

		float CallbackRate(); // frequency with which callback is invoked

		void StartAdc(); // start analog-digital conversion
		void StopAdc(); // stop transfering data

		void ProcessAnalog(); // process analog controls
		void ProcessDigital(); // process analog controls

		void ProcessControls()
		{
			ProcessAnalog();
			ProcessDigital();
		}

		float GetKnob(KnobI k);

		void SetBypass(bool enabled);
		void SetMute(bool enabled);

		void ClearLeds();
		void SetLed(LedI l, float brightness);

		void UpdateLeds();

	private:
		void InitAnalogControls();
		void InitEncoders();
		void InitSwitches();
		void InitLeds();

		void InitMidi();

		void SetHidUpdateRates();

	    inline uint16_t* adc_ptr(const uint8_t chn) { return seed.adc.GetPtr(chn); }
	};
}

#define PEDAL
#endif