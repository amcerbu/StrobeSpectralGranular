#include <string.h>
#include "daisy_seed.h"
#include "daisysp.h"

#include "globals.h"
#include "Pedal.h"
#include "buffer.h"

#include "gesture.h"

using namespace daisy;
using namespace daisysp;
using namespace amc;
using namespace soundmath;

const size_t bsize = 32;

bool processed = false;
const size_t framerate = 90;
const size_t oversample = 8;
const size_t tickrate = framerate * oversample;
Pedal hw;


const S epsilon = 0.001;

const size_t cols = 1;
const size_t n_notes = 6;
const S notes[n_notes] = {mtof(16), mtof(21), mtof(26), mtof(31), mtof(35), mtof(40)};

bool selectable = false;
std::string note_names[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
size_t selection = 0;
S frequency = mtof(48);

Overdrive drive;

S data1[SR / 20 + 1];
S data2[SR / 20 + 1];

FBuffer<S> clean(data1, SR / 20 + 1);
FBuffer<S> dirty(data2, SR / 20 + 1);

S clean_size = 1;
S dirty_size = 1;
S size_damping = 0.9999;

S post_gain = 1;

// const size_t cols = 1;
// const size_t n_notes = 12;
// const S notes[n_notes] = {mtof(12), mtof(19), mtof(14), mtof(21), mtof(16), mtof(23), mtof(18), mtof(13), mtof(20), mtof(15), mtof(22), mtof(17)};

S phases[n_notes];
int xs[n_notes];

bool bars = true;

bool tuning = true;

// create a low-pass filter for ducking the ADC when the effect is powered up
S adc_gain = 1;
S adc_gain_prev = 1;
S adc_damping = 0.9999;

bool effectOn = false;
bool muteOn = false;
bool bypassOn = true;
float brightness = 0;
int tilMuteOff, tilBypassToggle;

const int width = 128, height = 64;

S strobes[n_notes][width / cols];
S r = 0;

const size_t textHeight = 8;
const size_t textWidth = 6;
const size_t textHPad = 6;
const size_t chars = 4;
const size_t textHSpace = chars * textWidth + textHPad;


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

void handleUI()
{
	// std::string str;
	// const char* cstr;

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

	selection += hw.encoders[0].Increment() + 12;
	selection %= 12;
	frequency = mtof(24 + selection);

	// bars ^= (hw.switches[1].RisingEdge());
	selectable ^= (hw.encoders[0].RisingEdge());

	bool switching = (hw.switches[1].RisingEdge());
	tuning ^= switching;
	if (switching)
		hw.display.Fill(false);

	S t = hw.knobs[0].Value();
	drive.SetDrive(0.4 * (1 - t) + 0.9 * t);

	S a = hw.knobs[1].Value();
	post_gain = 0.01 * (1 - a) + 0.2 * a;
	
	r = hw.knobs[5].Value();
}


void handleBypass()
{
	bool oldEffectOn = effectOn;
	effectOn ^= (hw.switches[0].RisingEdge());

	hw.SetBypass(bypassOn);
	hw.SetMute(muteOn);

	if (effectOn != oldEffectOn)
	{
		brightness = (float)effectOn;
		muteOn = true;

		tilMuteOff = 2;
		tilBypassToggle = 1;
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

static void Callback(AudioHandle::InterleavingInputBuffer in,
					 AudioHandle::InterleavingOutputBuffer out,
					 size_t size)
{
	if (processed)
	{
		hw.ProcessAnalog(); // read controls
		hw.ProcessDigital(); // read controls
		processed = false;
	}

	static int x, y;
	for (size_t i = 0; i < size; i += 2)
	{
		adc_gain = (1 - adc_damping) * effectOn + adc_damping * adc_gain_prev;
		adc_gain_prev = adc_gain;

		if (!effectOn)
		{
			adc_gain = 0;
			adc_gain_prev = 0;
		}

		S in_sample = adc_gain * in[i];
		S out_sample = post_gain * drive.Process(in_sample);

		clean.write(in_sample);
		dirty.write(out_sample);

		clean_size = size_damping * clean_size + (1 - size_damping) * in_sample * in_sample;
		dirty_size = size_damping * dirty_size + (1 - size_damping) * out_sample * out_sample;

		static int vspace = height / (n_notes / cols);

		if (tuning)
		{
			if (selectable)
			{
				for (size_t j = 0; j < n_notes; j++)
				{
					x = (int)(phases[j] * width);
					y = std::clamp((int)((j + 0.5) * vspace + 12 * vspace * in_sample), (int)(j * vspace), (int)((j + 1) * vspace - 1));

					if (x != xs[j])
					{
						hw.display.DrawLine(x, j * vspace, x, (j + 1) * vspace - 1, bars && (in_sample > epsilon));
					}

					if (!bars && abs(in_sample) > epsilon)
						hw.display.DrawPixel(x, y, true);
					xs[j] = x;

					phases[j] += (frequency / (1 + j)) / SR;
					if (phases[j] > 1)
						phases[j] -= 1;
				}

				hw.display.SetCursor(0, 0);
				hw.display.WriteString(note_names[selection].c_str(), Font_6x8, true);
			}
			else
			{
				for (size_t k = 0; k < cols; k++)
				{
					for (size_t j = 0; j < n_notes / cols; j++)
					{	
						S phase = phases[j + k * n_notes / cols];
						// x = (int)((k + phase) * width / cols);
						// y = std::clamp((int)((j + 0.5) * vspace + 12 * vspace * in_sample), (int)(j * vspace), (int)((j + 1) * vspace - 1));

						size_t index = (size_t)(phase * width / cols);
						S smoothed = (1 - r) * in_sample + r * strobes[j][index];
						strobes[j][index] = smoothed;
						x = k * width / cols + index;
						y = std::clamp((int)((j + 0.5) * vspace + 12 * vspace * smoothed), (int)(j * vspace), (int)((j + 1) * vspace - 1));

						if (x != xs[j + k * n_notes / cols])
						{
							hw.display.DrawLine(x, j * vspace, x, (j + 1) * vspace - 1, bars && (smoothed > epsilon));
						}

						if (!bars && abs(smoothed) > epsilon)
							hw.display.DrawPixel(x, y, true);
						xs[j + k * n_notes / cols] = x;

						phases[j + k * n_notes / cols] += notes[j + k * n_notes / cols] / SR;
						if (phases[j + k * n_notes / cols] > 1)
							phases[j + k * n_notes / cols] -= 1;
					}
				}	
			}
			
			out_sample = in_sample;
		}
		else
		{
			S clean_scaling = 32 * 0.875 / std::max((S)0.125, std::sqrt(clean_size));
			S dirty_scaling = 32 * 0.875 / std::max((S)0.125, std::sqrt(dirty_size));

			hw.display.DrawPixel(3 * width / 4 + clean_scaling * in_sample, height / 2 + clean_scaling * clean(SR / 20), true);
			hw.display.DrawPixel(width / 4 + dirty_scaling * out_sample, height / 2 + dirty_scaling * dirty(SR / 20), true);
		}

		out[i] = out_sample;
		out[i + 1] = out_sample;

		clean.tick();
		dirty.tick();
	}

	hw.SetLed((Pedal::LedI)0, effectOn);
}

int main(void)
{
	memset(phases, 0, n_notes * sizeof(S));
	memset(xs, -1, n_notes * sizeof(S));
	memset(strobes, 0, n_notes * width / cols * sizeof(S));

	hw.Init();

	hw.display.Fill(false);
	hw.display.Update();
 	
	hw.SetBlockSize(bsize);

	hw.SetMute(muteOn);
	hw.SetBypass(bypassOn);

	hw.StartAdc();
	hw.SetAudioRate(SaiHandle::Config::SampleRate::SAI_96KHZ);
	hw.StartAudio(Callback);

	uint32_t freq = System::GetTickFreq();
	uint32_t ticks = (uint32_t)(freq * 1.0 / tickrate);
	int last_update = 0;

	while (true)
	{
		if (!processed)
		{
			handleBypass();
			handleUI();
			processed = true;
		}

		if (!(last_update % oversample)) // throttle display updates
		{
			hw.display.Update();
			if (!tuning)
				hw.display.Fill(false);

			hw.UpdateLeds();
			last_update = 0;
		}

		System::DelayTicks(ticks);
		last_update++;
	}
}