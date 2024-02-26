#include <string.h>
#include "daisy_seed.h"
// #include "daisysp.h"

#include "globals.h"
#include "Pedal.h"

#include "wave.h"
#include "buffer.h"
#include "granulator.h"
#include "rms.h"

#include "gesture.h"

// uncomment flags & recompile to change behavior

// #define RESET
// #define HANDLE_MIDI
// #define DEBUG
#define PREALLOCATED // otherwise, dynamically allocated buffer

using namespace daisy;
// using namespace daisysp;
using namespace amc;
using namespace soundmath;

const size_t bsize = 64;

CpuLoadMeter cpu;
float cpu_meter_thresh = 0.25;
float cpu_thresh = 0.7;

bool processed = false;
const size_t framerate = 60;
const size_t oversample = 8;
const size_t tickrate = framerate * oversample;
Pedal hw;

const float holdseconds = 0.5;
const size_t holdtime = tickrate * holdseconds;

const int seconds = 15;
const size_t buffsize = seconds * SR;

#ifdef PREALLOCATED
S DSY_SDRAM_BSS data[buffsize]; // store 10-second buffer in SDRAM
FBuffer<S>* source; // preallocated buffer
#else
Buffer<S>* source; // dynamically allocated buffer
#endif


RMS<S> rms;

// create pointers to DSP objects; construct after hw.Init()
Granulator<S>* granny;

const size_t n_grans = 4;
Granary<S> granaries[n_grans]; // grain request "metronomes"

// create a low-pass filter for ducking the ADC when the effect is powered up
S adc_gain = 1;
S adc_gain_prev = 1;
S adc_damping = 0.99999;

bool effectOn = false;
bool muteOn = false;
bool bypassOn = true;
float brightness = 0;
int tilMuteOff, tilBypassToggle;

int width, height;


const size_t colsPots = 3;
const size_t rowsPots = 2;
const size_t numParams = 7;
const char* paramNames[numParams] = 
{
	"size", "dnst", "jttr", "txtr", "spry", "wrbl", "shft"
};

struct Settings
{
	size_t selector;
	bool active[n_grans];
	S params[n_grans][numParams];
	bool reverses[n_grans];
	// bool speed_editing;

	bool operator!=(const Settings& a) const
	{
		bool value = (a.selector == selector /* && a.speed_editing == speed_editing */);
		for (size_t i = 0; i < n_grans; i++)
		{
			for (size_t j = 0; j < numParams; j++)
				value = value && (a.params[i][j] == params[i][j]);

			value = value && (a.active[i] == active[i] && a.reverses[i] == reverses[i]);
		}
		return !value;
	}
};

PersistentStorage<Settings> Archive(hw.seed.qspi); // persistent
Settings Params; // dynamic

// loads settings from storage
void Load(Settings &Params)
{
	// reference to copy of stored settings
	Settings &LocalSettings = Archive.GetSettings();

	Params.selector = LocalSettings.selector;

	memcpy(Params.active, LocalSettings.active, n_grans * sizeof(bool));
	memcpy(Params.params, LocalSettings.params, n_grans * numParams * sizeof(S));
	memcpy(Params.reverses, LocalSettings.reverses, n_grans * sizeof(bool));
}


bool saved = true;
void Save(Settings &Params)
{
	// reference to copy of stored settings
	Settings &LocalSettings = Archive.GetSettings();

	LocalSettings.selector = Params.selector;

	memcpy(LocalSettings.active, Params.active, n_grans * sizeof(bool));
	memcpy(LocalSettings.params, Params.params, n_grans * numParams * sizeof(S));
	memcpy(LocalSettings.reverses, Params.reverses, n_grans * sizeof(bool));

	Archive.Save();

#ifdef DEBUG
	hw.seed.PrintLine("Initiated save.");
	switch (Archive.GetState())
	{
	case PersistentStorage<Settings>::State::UNKNOWN:
	    hw.seed.PrintLine("Unknown state.");
	    break;
	case PersistentStorage<Settings>::State::FACTORY:
	    hw.seed.PrintLine("Factory state");
	    break;
	case PersistentStorage<Settings>::State::USER:
	    hw.seed.PrintLine("User state");
	    break;
	}
#endif

	saved = true;
}

const size_t textHeight = 8;
const size_t textWidth = 6;
const size_t textHPad = 6;
const size_t chars = 4;
const size_t textHSpace = chars * textWidth + textHPad;

void initParams(Settings& a)
{
	a.selector = 0;
	// a.speed_editing = false;
	memset(a.active, false, n_grans * sizeof(bool));
	memset(a.params, false, n_grans * numParams * sizeof(S));
	memset(a.reverses, true, n_grans * sizeof(bool));
}


// // // // // // // // // // // // // //
//    TYPE GESTURE TO ENTER DFU MODE   //
// // // // // // // // // // // // // //
Pedal::SwitchI S1 = Pedal::SwitchI::S1;
Pedal::SwitchI S2 = Pedal::SwitchI::S2;
Pedal::SwitchI ids[2] = {S1, S2};
Pedal::SwitchI dfu_changes[8] = {S1, S2, S1, S1, S2, S2, S1, S2};
Pedal::SwitchI reset_changes[8] = {S2, S1, S2, S2, S1, S1, S2, S1};
Pedal::SwitchI restore_changes[6] = {S2, S1, S2, S2, S1, S2};

bool initial[2] = {false, false};
Sequence<2, 8> dfu_sequence(&hw, ids, initial, dfu_changes);
Sequence<2, 8> reset_sequence(&hw, ids, initial, reset_changes);
Sequence<2, 6> restore_sequence(&hw, ids, initial, restore_changes);

size_t holdcount = 0;
bool holdready = true;
bool speed_editing = false;
bool just_saved = false;

// old knob values
S old_knob_vals[Pedal::KnobI::KN];
bool knobs_moving[Pedal::KnobI::KN];

S nudge(S param, Pedal::KnobI knob)
{
	static S dead_thresh = 0.0001;
	static S move_thresh = 0.001;
	static S epsilon = 0.00001;

	S now = hw.knobs[knob].Value();
	S before = old_knob_vals[knob];

	S delta = now - before;

	// big move marks the knob as "moving"
	if (abs(delta) > move_thresh)
		knobs_moving[knob] = true;
	else if (abs(delta) < dead_thresh) // not much movement marks the knob as dead
		knobs_moving[knob] = false;

	// if the knob is in motion
	if (knobs_moving[knob])
	{
		S endpoint = delta < 0 ? 0 : 1;
        S change = delta * (abs(param - endpoint) + epsilon) / (abs(now - endpoint) + epsilon);
	    param += 0.5 * (change + delta);
	    param = std::clamp(param, (S)0, (S)1);
	}

	return param;
}

void handleUI(Settings &P)
{
	std::string str;
	const char* cstr;

	dfu_sequence.tick(); // check for DFU mode button-press gesture
	if (dfu_sequence.step()) // returns true if the sequence was completed
	{
		hw.display.Fill(false);
		hw.seed.StopAudio();
		Rectangle boundingBox(0, 0, width, height);
		hw.display.WriteStringAligned("DFU...", Font_16x26, boundingBox, Alignment::centered, true);

		hw.display.Update();

		System::ResetToBootloader();
	}

	reset_sequence.tick();
	if (reset_sequence.step())
	{
		initParams(Params); // restores params to default, but doesn't overwrite persistent storage 
		speed_editing = false;
	}

	restore_sequence.tick();
	if (restore_sequence.step())
	{
		Load(Params); // restores params to default, but doesn't overwrite persistent storage 
		speed_editing = false;
	}

	if (hw.encoders[0].Pressed() && hw.switches[0].Pressed() && hw.switches[1].Pressed())
	{
		if (holdready)
			holdcount += (holdcount <= holdtime);
	}
	else
	{
		holdcount = 0;
		holdready = true;
	}

	if (holdcount > holdtime)
	{
		saved = false;
		holdready = false;
		holdcount = 0;

		just_saved = true;
	}

	// only switch to editing mode if we haven't just saved a preset
	if (hw.encoders[0].FallingEdge())
	{
		if (just_saved)
			just_saved = false;
		else
			speed_editing = !speed_editing;
	}		

	if (speed_editing)
	{
		S the_value = P.params[P.selector][6] + hw.encoders[0].Increment();
		the_value = std::min((S)24.0, std::max((S)-12.0, the_value));
		P.params[P.selector][6] = the_value;
	}
	else
	{
		P.selector = (P.selector + hw.encoders[0].Increment()) % n_grans;
	}

	if (hw.switches[1].RisingEdge() && !hw.encoders[0].Pressed())
	{
		if (speed_editing)
			P.reverses[P.selector] = !P.reverses[P.selector];
		else
			P.active[P.selector] = !P.active[P.selector];
	}


	hw.display.DrawRect(0, 0, width - 1, height / 2, false, true);
	hw.display.DrawRect(colsPots * textHSpace, 4 * textHeight, width - 1, height - 2, false, true);
	for (size_t i = 0; i < colsPots; i++)
	{
		for (size_t j = 0; j < rowsPots; j++)
		{
			size_t knobInd = i + colsPots * j;
			S knobVal = nudge(P.params[P.selector][knobInd], (Pedal::KnobI)knobInd); // don't override values!

			if (speed_editing)
				P.params[P.selector][knobInd] = knobVal;

			Rectangle boundingBox(i * textHSpace, 0 + (2 * j + 1) * textHeight, chars * textWidth, textHeight);

			hw.display.DrawRect(i * textHSpace, 0 + 2 * j * textHeight + 1, i * textHSpace + P.params[P.selector][knobInd] * chars * textWidth, 0 + (2 * j + 1) * textHeight - 2, true);
			hw.display.WriteStringAligned(paramNames[knobInd], Font_6x8, boundingBox, Alignment::centered, true);
		}
	}


	Rectangle shiftBox(colsPots * textHSpace, 0 + textHeight, chars * textWidth, textHeight);
	Rectangle quantityBox(colsPots * textHSpace, 0, chars * textWidth, textHeight);

	int transp = (int)P.params[P.selector][6];
	str = std::string(transp >= 0 ? "+" : "-") + std::string(abs(transp) < 10 ? "0" : "") + std::to_string(abs(transp));
	cstr = str.c_str();
	hw.display.WriteStringAligned(cstr, Font_6x8, quantityBox, Alignment::centeredLeft, true);

	if (speed_editing)
	{
		hw.display.DrawRect(colsPots * textHSpace - 1, 0 + textHeight, colsPots * textHSpace + chars * textWidth - 1, 0 + 2 * textHeight, true, true);
		hw.display.WriteStringAligned(paramNames[numParams - 1], Font_6x8, shiftBox, Alignment::centered, false);
	}
	else
	{
		hw.display.WriteStringAligned(paramNames[numParams - 1], Font_6x8, shiftBox, Alignment::centered, true);
	}

	int x, y;
	x = colsPots * textHSpace;
	y = 0 + 5 * textHeight / 2;
	hw.display.DrawLine(x, y, x + chars * textWidth, y, true);

	if (P.reverses[P.selector])
	{
		hw.display.DrawLine(x + chars * textWidth - 2, y - 2, x + chars * textWidth, y, true);
		hw.display.DrawLine(x + chars * textWidth - 2, y + 2, x + chars * textWidth, y, true);
	}
	else
	{
		hw.display.DrawLine(x + 2, y - 2, x, y, true);
		hw.display.DrawLine(x + 2, y + 2, x, y, true);
	}

	for (size_t i = 0; i < n_grans; i++)
	{
		int x, y, w, h;
		x = colsPots * textHSpace + i * textWidth;
		y = 0 + 3 * textHeight;
		w = textWidth;
		h = textHeight;
		Rectangle granBox(x, y + 1, w, h);
		if (P.active[i])
			hw.display.DrawRect(x - 1, y, x - 1 + w, y + h, true, false);
		if (i == P.selector && !speed_editing)
		{
			str = std::to_string(i + 1);
			cstr = str.c_str();
			hw.display.WriteStringAligned(cstr, Font_6x8, granBox, Alignment::centered, !P.active[i]);
		}
	}

	str = "G:" + std::to_string(granny->activity);
	cstr = str.c_str();
	hw.display.SetCursor(colsPots * textHSpace, 5 * textHeight);
	hw.display.WriteString(cstr, Font_6x8, true);

	str = "C:" + std::to_string((int)(100 * cpu.GetAvgCpuLoad() + 0.5)) + "%";
	cstr = str.c_str();
	hw.display.SetCursor(colsPots * textHSpace, 6 * textHeight + textHeight / 2);
	hw.display.WriteString(cstr, Font_6x8, true);


	if (holdcount > 0)
	{
		float ratio = (float)holdcount / holdtime;
		size_t pixels = (size_t)((float)height * ratio / 4.0);
		for (size_t i = 0; i <= pixels; i++)
			hw.display.DrawRect(i, i, width - i, height / 2 - i, true, false);
	}

	for (size_t i = 0; i < n_grans; i++)
	{
		if (P.active[i])
		{
			granaries[i].instruct((seconds - 1) * P.params[i][2], Granary<S>::jitter);
			granaries[i].instruct((2 * (int)P.reverses[i] - 1) * pow(2, P.params[i][6] / 12.0), Granary<S>::speed);
			granaries[i].instruct(0.01 * P.params[i][5], Granary<S>::warble);
			granaries[i].instruct(P.params[i][0], Granary<S>::size);
			granaries[i].instruct(P.params[i][3], Granary<S>::texture);
			// granaries[i].instruct(40 * P.params[i][1], Granary<S>::density);
			granaries[i].instruct(120 * P.params[i][1] * P.params[i][1], Granary<S>::density);
			granaries[i].instruct(P.params[i][4], Granary<S>::spray);
			granaries[i].instruct(1, Granary<S>::gain);
		}
		else
		{
			granaries[i].instruct(0, Granary<S>::density);
		}
	}

	for (size_t i = 0; i < Pedal::KnobI::KN; i++)
	{
		old_knob_vals[i] = hw.knobs[i].Value();
	}
}


void handleBypass()
{
	bool oldEffectOn = effectOn;
	effectOn ^= (hw.switches[0].RisingEdge() && !hw.encoders[0].Pressed());

	hw.SetBypass(bypassOn);
	hw.SetMute(muteOn);

	if (effectOn != oldEffectOn)
	{
		brightness = (float)effectOn;
		muteOn = true;

		// Set the timing for when the bypass relay should trigger and when to unmute.
		tilMuteOff = 2;
		tilBypassToggle = 1;

		// if (effectOn)
		// {
		// 	adc_gain = 0;
		// 	adc_gain_prev = 0;
		// }
	}

	if (muteOn)
	{
		// Decrement the Sample Counts for the timing of the mute and bypass
		tilMuteOff--;
		tilBypassToggle--;

		// if mute time is up, turn it off.
		if (tilMuteOff < 0)
			muteOn = false;

		// toggle the bypass when it's time (needs to be timed to happen while things are muted, or you get a pop)
		if (tilBypassToggle < 0)
			bypassOn = !effectOn;
	}
}

#ifdef HANDLE_MIDI
void HandleMidi(MidiEvent m)
{
	int note, velocity;
	NoteOnEvent p;

	switch (m.type)
	{
		case NoteOn:
		case NoteOff:
			p = m.AsNoteOn();
			note = p.note;
			velocity = p.velocity;

			// hw.seed.PrintLine("Note: %u, Velocity: %u", note, velocity);
			break;

		default:
			break;
	}
}
#endif

static void Callback(AudioHandle::InterleavingInputBuffer in,
					 AudioHandle::InterleavingOutputBuffer out,
					 size_t size)
{
	cpu.OnBlockStart();
	if (processed)
	{
		hw.ProcessAnalog(); // read controls
		hw.ProcessDigital(); // read controls
		processed = false;
	}

	static S offset, the_size, speed, gain, pan;
	static int x, y, last_x = 0;	
	for (size_t i = 0; i < size; i += 2)
	{
		out[i] = out[i + 1] = 0;

		adc_gain = (1 - adc_damping) * effectOn + adc_damping * adc_gain_prev;
		adc_gain_prev = adc_gain;

		if (!effectOn)
		{
			adc_gain = 0;
			adc_gain_prev = 0;
		}

		S in_sample = adc_gain * in[i];
		source->write(in_sample);


		static int waveform_width = colsPots * textHSpace - textWidth;
		x = (int)(waveform_width * source->get_origin() / (float)source->get_size());

		static S vertical_correction = 3.0;
		// vertical_correction = 1.0 / (0.2 + rms(in_sample));
		// rms.tick();

		y = std::clamp((int)(height * (3 + (vertical_correction * effectOn) * in_sample) / 4.0), height / 2 + 1, height - 1);
		hw.display.DrawPixel(x, y, true);

		if (last_x != x)
		{
			int new_x1 = (x + 1) % (waveform_width);
			int new_x2 = (x + 2) % (waveform_width);
			int new_x3 = (x + 3) % (waveform_width);

			hw.display.DrawLine(new_x1, height / 2 + 1, new_x1, height - 1, false);
			hw.display.DrawLine(new_x2, height / 2 + 1, new_x2, height - 1, true);
			hw.display.DrawLine(new_x3, height / 2 + 1, new_x3, height - 1, false);
		}
		last_x = x;

		if (effectOn)
		{
			for (size_t j = 0; j < n_grans; j++)
			{
				if (granaries[j].parameters(&offset, &the_size, &speed, &gain, &pan) && cpu.GetAvgCpuLoad() < cpu_thresh)
				{
					// hw.seed.PrintLine("Requested: size %f, speed %f, gain %f.", the_size, speed, gain);
					granny->request(offset, the_size, speed, gain, 0);
				}
			}

			out[i] = limiter(in_sample + (*granny)());
			out[i + 1] = out[i];
		}
		else
		{
			out[i] = 0;
			out[i + 1] = 0;
		}


		source->tick();
		granny->tick();
		for (size_t j = 0; j < n_grans; j++)
			granaries[j].tick();
	}

	hw.SetLed((Pedal::LedI)0, effectOn);

	// S load = cpu.GetAvgCpuLoad();
	// static S load_brightness = 0;
	// static const S led_damping = 0.9999;

	// load_brightness = led_damping * load_brightness + (1 - led_damping) * (load < cpu_meter_thresh ? 0 : (load - cpu_meter_thresh) / (1.0 - cpu_meter_thresh));
	// hw.SetLed((Pedal::LedI)1, load_brightness);

	cpu.OnBlockEnd();
}

int main(void)
{
	hw.Init();

#ifdef DEBUG
    hw.seed.StartLog(true);
#endif

	Settings defaults;
	initParams(defaults);
	Archive.Init(defaults);

#ifdef RESET
	Save(defaults);
#endif

	initParams(Params);
	Load(Params);

#ifdef DEBUG
	hw.seed.PrintLine("Booted.");
	switch (Archive.GetState())
	{
	case PersistentStorage<Settings>::State::UNKNOWN:
	    hw.seed.PrintLine("Unknown state.");
	    break;
	case PersistentStorage<Settings>::State::FACTORY:
	    hw.seed.PrintLine("Factory state");
	    break;
	case PersistentStorage<Settings>::State::USER:
	    hw.seed.PrintLine("User state");
	    break;
	}
#endif

#ifdef PREALLOCATED
	source = new FBuffer<S>(data, buffsize); // preallocated buffer
#else
	source = new Buffer<S>(buffsize); // dynamically allocated buffer
#endif
	
	granny = new Granulator<S>(&hann, source);

	hw.display.Fill(false);
	hw.display.Update();
	width = hw.display.Width();
	height = hw.display.Height();
 	
	hw.SetBlockSize(bsize);

	hw.SetMute(muteOn);
	hw.SetBypass(bypassOn);

	cpu.Init(SR, bsize);

	hw.StartAdc();
	hw.StartAudio(Callback);

#ifdef HANDLE_MIDI
	hw.midi.StartReceive();
#endif

	uint32_t freq = System::GetTickFreq();
	uint32_t ticks = (uint32_t)(freq * 1.0 / tickrate);
	int last_update = 0;

	while (true)
	{
		if (!processed)
		{
			handleBypass();
			handleUI(Params);
			processed = true;

			if (!saved)
				Save(Params);
		}

		if (!(last_update % oversample)) // throttle display updates
		{
			hw.display.Update();
			hw.UpdateLeds();
			last_update = 0;
		}

		System::DelayTicks(ticks);
		last_update++;
	}

	delete granny;
	delete source;
}