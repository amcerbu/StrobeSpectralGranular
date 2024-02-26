#include "Pedal.h"

using namespace daisy;
using namespace amc;

void Pedal::Init(bool boost)
{
	seed.Init(boost);

	InitAnalogControls();
	InitEncoders();
	InitSwitches();
	InitLeds();
	InitMidi();

	switch (SR)
	{
	case 8000:
		SetAudioRate(daisy::SaiHandle::Config::SampleRate::SAI_8KHZ);
		break;
	case 16000:
		SetAudioRate(daisy::SaiHandle::Config::SampleRate::SAI_16KHZ);
		break;
	case 32000:
		SetAudioRate(daisy::SaiHandle::Config::SampleRate::SAI_32KHZ);
		break;
	case 96000:
		SetAudioRate(daisy::SaiHandle::Config::SampleRate::SAI_96KHZ);
		break;
	default:
		SetAudioRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
		break;
	}

	SetBlockSize(16);

	// attach GPIO objects to appropriate pins for later writing (switching relays)
	audioBypassTrigger.Init(daisy::seed::D1, GPIO::Mode::OUTPUT);
	audioMuteTrigger.Init(daisy::seed::D12, GPIO::Mode::OUTPUT);
	SetBypass(true);
	SetMute(false);

	// configure display
	Oled::Config config;
	config.driver_config.transport_config.pin_config.dc = seed.GetPin(DISPLAY_DC_PIN);
	config.driver_config.transport_config.pin_config.reset = seed.GetPin(DISPLAY_RESET_PIN);
	display.Init(config);
}

void Pedal::Delay(size_t milliseconds)
{
	seed.DelayMs(milliseconds);
}

void Pedal::StartAudio(AudioHandle::InterleavingAudioCallback cb)
{
	seed.StartAudio(cb);
}

void Pedal::StartAudio(AudioHandle::AudioCallback cb)
{
	seed.StartAudio(cb);
}

void Pedal::ChangeAudio(AudioHandle::InterleavingAudioCallback cb)
{
	seed.ChangeAudioCallback(cb);
}

void Pedal::ChangeAudio(AudioHandle::AudioCallback cb)
{
	seed.ChangeAudioCallback(cb);
}

void Pedal::StopAudio()
{
	seed.StopAudio();
}

void Pedal::SetAudioRate(SaiHandle::Config::SampleRate samplerate)
{
	seed.SetAudioSampleRate(samplerate);
	SetHidUpdateRates();
}

float Pedal::AudioRate()
{
	return seed.AudioSampleRate();
}

void Pedal::SetBlockSize(size_t size)
{
	seed.SetAudioBlockSize(size);
	SetHidUpdateRates();
}

size_t Pedal::BlockSize()
{
	return seed.AudioBlockSize();
}

float Pedal::CallbackRate()
{
	return seed.AudioCallbackRate();
}

void Pedal::StartAdc() { seed.adc.Start(); } // start analog-digital conversion
void Pedal::StopAdc() { seed.adc.Stop(); } // stop transfering data

void Pedal::ProcessAnalog()
{
	for (size_t i = 0; i < KnobI::KN; i++)
		knobs[i].Process();
}

void Pedal::ProcessDigital()
{
	for (size_t i = 0; i < SwitchI::SN; i++)
		switches[i].Debounce();

	for (size_t i = 0; i < EncoderI::EN; i++)
		encoders[i].Debounce();
}

float Pedal::GetKnob(KnobI k)
{
	size_t id = k < KnobI::KN ? k : 0;
	return knobs[id].Value();
}

void Pedal::SetBypass(bool enabled)
{
	audioBypass = enabled;
	audioBypassTrigger.Write(!audioBypass);
}

void Pedal::SetMute(bool enabled)
{
	audioMute = enabled;
	audioMuteTrigger.Write(audioMute);
}

void Pedal::ClearLeds()
{
	for (size_t i = 0; i < LedI::LN; i++)
		SetLed(static_cast<LedI>(i), 0.0f);
}

void Pedal::SetLed(LedI l, float brightness)
{
	size_t id = l < LedI::LN ? l : 0;
	leds[id].Set(brightness);
}

void Pedal::UpdateLeds()
{
	for (size_t i = 0; i < LedI::LN; i++)
		leds[i].Update();
}

void Pedal::InitAnalogControls()
{
	AdcChannelConfig configs[KnobI::KN]; // one config per knob

	configs[KnobI::K1].InitSingle(seed.GetPin(KNOB_PIN_1));
	configs[KnobI::K2].InitSingle(seed.GetPin(KNOB_PIN_2));
	configs[KnobI::K3].InitSingle(seed.GetPin(KNOB_PIN_3));
	configs[KnobI::K4].InitSingle(seed.GetPin(KNOB_PIN_4));
	configs[KnobI::K5].InitSingle(seed.GetPin(KNOB_PIN_5));
	configs[KnobI::K6].InitSingle(seed.GetPin(KNOB_PIN_6));

	seed.adc.Init(configs, KnobI::KN);
	
	for (int i = 0; i < KnobI::KN; i++) // attach pointers to knobs object
		knobs[i].Init(seed.adc.GetPtr(i), CallbackRate());
}

void Pedal::InitEncoders()
{
	encoders[EncoderI::E1].Init(seed.GetPin(ENCODER_1_A_PIN),
		seed.GetPin(ENCODER_1_B_PIN),
		seed.GetPin(ENCODER_1_BUTTON_PIN));
}

void Pedal::InitSwitches()
{
	switches[SwitchI::S1].Init(seed.GetPin(SWITCH_1_PIN));
	switches[SwitchI::S2].Init(seed.GetPin(SWITCH_2_PIN));
}

void Pedal::InitLeds()
{
	leds[LedI::L1].Init(seed.GetPin(LED_1_PIN), false);
	leds[LedI::L2].Init(seed.GetPin(LED_2_PIN), false);
}

void Pedal::InitMidi()
{
	MidiUartHandler::Config config;
	config.transport_config.rx = seed.GetPin(MIDI_RX_PIN);
	config.transport_config.tx = seed.GetPin(MIDI_TX_PIN);
	midi.Init(config);
}

void Pedal::SetHidUpdateRates()
{
	for (size_t i = 0; i < KnobI::KN; i++)
		knobs[i].SetSampleRate(CallbackRate());

	for (size_t i = 0; i < LedI::LN; i++)
		leds[i].SetSampleRate(CallbackRate());
}