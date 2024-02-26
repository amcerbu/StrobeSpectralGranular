#include <string.h>
#include "daisy_seed.h"
#include "daisysp.h"

#include "globals.h"
#include "Pedal.h"
#include "gesture.h"

#include "fourier.h"
#include "synth.h"
#include "filter.h"

// #define DEBUG
#define VECTRAL
// #define SYNTHETIC
// #define PRINTFREQS
// #define TRANSPOSE

using namespace daisy;
using namespace daisysp;
using namespace amc;
using namespace soundmath;

const size_t bsize = 256;

CpuLoadMeter cpu;
float cpu_thresh = 0.7;

bool processed = false;
const size_t framerate = 30;
const size_t oversample = 8;
const size_t tickrate = framerate * oversample;
Pedal hw;

const size_t order = 11;
const size_t N = (1 << order);
const S sqrtN = sqrt(N); // (order % 2) ? (size_t)(sqrt(2) * (1 << order / 2)) : (1 << order / 2);
const size_t offset = N / 2;
const size_t laps = 8;
const size_t buffsize = 2 * laps * N;

#ifdef SYNTHETIC
const size_t n_synthetic = 7;
int synthetic_freqs[n_synthetic] = {123, 458, 99, 338, 512, 997, 8000};
Synth<S>* synthesizers[n_synthetic];
#endif

S adc_gain = 1;
S adc_gain_prev = 1;
S adc_damping = 0.9999;

// audio is read into in, FFT'd into middle, processed in the Fourier domain
// and stored in out, then iFFT'd back into in (a convoluted collection of circular buffers)
S in[buffsize]; // buffers for the STFT object
S middle[buffsize]; 
S out[buffsize];

S notes[N / 2]; // lookup table for ftom; used for plotting

ShyFFT<S, N, RotationPhasor>* fft; // fft object
Fourier<S, N>* stft; // stft object

bool effectOn = false;
bool muteOn = false;
bool bypassOn = true;
float brightness = 0;
int tilMuteOff, tilBypassToggle;

int width, height;

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

void handleUI();

int transposition = 0;
int fine_transposition = 0; // cents
int fine_increment = 5;
S freq_ratio = 1;

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

	for (size_t i = 0; i < size; i += 2)
	{
		out[i] = 0;
		out[i + 1] = 0;
		S in_sample = 0, out_sample = 0;

#ifdef SYNTHETIC
		// synthetic_freqs[0] = 1000 + 100 * transposition + fine_transposition;
		// synthesizers[0]->freqmod(synthetic_freqs[0]);

		for (size_t k = 0; k < n_synthetic; k++)
		{
			in_sample += (1 + hw.switches[1].Pressed()) * (*synthesizers[k])() / n_synthetic;
			synthesizers[k] -> tick();
		}
#else
		in_sample = adc_gain * in[i];
		adc_gain = (1 - adc_damping) * effectOn + adc_damping * adc_gain_prev;
		adc_gain_prev = adc_gain;
#endif

		stft->write(in_sample);
		out_sample = stft->read();

		if (effectOn)
			out[i] = out[i + 1] = out_sample;
		else
			out[i] = out[i + 1] = 0;
	}

	hw.SetLed((Pedal::LedI)0, effectOn);
	float brightness = cpu.GetAvgCpuLoad() < cpu_thresh ? 0 : (cpu.GetAvgCpuLoad() - cpu_thresh) / (1.0 - cpu_thresh);
	hw.SetLed((Pedal::LedI)1, brightness);

	cpu.OnBlockEnd();
}


S alpha = 0, beta = 0, thresh = 0, noise_floor = 0; // parameters for denoise process

// shy_fft packs arrays as [real, real, real, ..., imag, imag, imag, ...]
inline int denoise(const S* in, S* out)
{
	S average = 0;
	for (size_t i = 0; i < N; i++)
	{
		out[i] = 0; 
		average += in[i] * in[i];
	}

	average /= N;

	for (size_t i = 0; i < N / 2; i++)
	{
		if ((in[i] * in[i] + in[i + offset] * in[i + offset]) < thresh * thresh * average)
		{
			out[i] = alpha * in[i];
			out[i + offset] = alpha * in[i];
		}
		else
		{
			out[i] = beta * in[i];
			out[i + offset] = beta * in[i + offset];
		}
	}

	return 0;
}

/*
average change in phase equals "true frequency"
we don't want to go to the trouble of actually computing the phase of the complex number in[j] + in[j + offset]i
instead, we can find the phase difference between a + bi and c + di by computing the phase of (a + bi)(c - di)
*/
S old_in[N]; // stores last FFT frame
S hop_phasor[2 * laps]; // stores hop phasor
S freqs[N / 2];
bool hot[N / 2];

S release_ratio = 0;

#ifdef VECTRAL
S old_freqs[N / 2];
S epsilon = 0.1;
#endif

S biggest = 0;
size_t peak_index = 0;
inline int pitch_shift(const S* in, S* out)
{
#ifdef TRANSPOSE
	// conveniently, current hasn't been updated yet, and no one else writes to middle
	S* old_out = middle + N * stft->current;
#endif

	S average = 0;
	for (size_t i = 0; i < N; i++)
	{
		out[i] = 0; 
		average += in[i] * in[i];
	}
	average /= N;

	int phasor_index = 0;
	biggest = 0;
	peak_index = 0;
	for (size_t j = 0; j < N / 2; j++)
	{
		std::complex<S> a(in[j], in[j + offset]);
		std::complex<S> b(old_in[j], old_in[j + offset]);

		S a_norm = std::norm(a);

		std::complex<S> hopper(hop_phasor[phasor_index], hop_phasor[phasor_index + laps]);

		phasor_index++;
		phasor_index %= laps;

		// approximate normalizations
		a = (S)2 * a / (1 + a_norm);
		b = (S)2 * b / (1 + std::norm(b));

		std::complex<S> phase_change = a * std::conj(b);
		std::complex<S> hop_corrected = phase_change * hopper;

		// you're hot if you're high-amplitude now, or were hot recently and aren't too low-amplitude now.
		S hot_thresh = sqrtN * noise_floor + thresh * thresh * average;
		bool hot_now = a_norm > hot_thresh || (hot[j] && a_norm > release_ratio * hot_thresh);

		if (hot_now) // if we're hot now
		{
			S angle = std::arg(hop_corrected); // sorry for expensive call

			// true frequency is bin frequency plus a correction term coming from the measured angle. 
			// when "vectral" processing is enabled,  we low-pass filtering the value across windows
			S freq = SR * ((S)j / N - angle / (2 * PI * stft->stride));

#ifdef VECTRAL
			if (hot[j]) // smooth out frequency estimate if we were hot before
				freqs[j] = epsilon * freq + (1 - epsilon) * old_freqs[j];
			else // clean start if we didn't have any running estimate already
				freqs[j] = freq; 
#else
			// not vectral: just store the current vocoder-measured center frequency
			freqs[j] = freq;
#endif
#ifdef TRANSPOSE
			size_t new_bin = (size_t)(freq_ratio * j + 0.5); // round to nearest bin

			// hop-corrected phase change
			S new_angle = 2 * PI * stft->stride * ((S)new_bin / N - freq_ratio * freq / SR); 
			// m + p i = e^(2 pi i angle) // figure out how to do this
			// phase change = (m + p i)(e - f i)

			// at the moment, out == middle + N * (stft->current - 1) % (2 * laps)
			// thus we can get the last output of this function by looking at middle + N * (stft->current - 2) % (2 * laps)

			std::complex<S> transp_phase(cycle(new_angle + 0.5), cycle(new_angle));

			hopper = {hop_phasor[new_bin % laps], hop_phasor[new_bin % laps + laps]};
			std::complex<S> transp_hopper = transp_phase * std::conj(hopper);

			// S q = m * e + p * f; // q + r i is the phase change of new_bin,
			// S t = p * e - m * f; // relative to its previous value

			std::complex<S> c(old_out[new_bin], old_out[new_bin + offset]);
			S c_norm = std::norm(c);

			std::complex<S> new_out = (1 + a_norm) * transp_hopper * c / (1 + c_norm);
			out[new_bin] += beta * std::real(new_out);
			out[new_bin + offset] += beta * std::imag(new_out);
#else
			out[j] = beta * in[j];
			out[j + offset] = beta * in[j + offset];
#endif
		}
		else
		{
			// with a quirky Hilbert transform for these guys
			out[j] = (1 - beta) * in[j + offset];
			out[j + offset] = -(1 - beta) * in[j];
		}

		hot[j] = hot_now;

		// doesn't hurt to keep track of peak amplitude and index of that peak
		if (a_norm > biggest)
		{
			biggest = a_norm;
			peak_index = j;
		}

	}

	// update the old guy (copy current complex amplitudes into our backup array for next time)
	memcpy(old_in, in, N * sizeof(S));

#ifdef VECTRAL
	// similarly, keep the old FFT frame for next run
	memcpy(old_freqs, freqs, (N / 2) * sizeof(S));
#endif
	return 0;
}


int main(void)
{
	hw.Init();

#ifdef DEBUG
    hw.seed.StartLog(true);
    hw.seed.PrintLine("Booted.");
#endif

	for (size_t i = 0; i < N / 2; i++)
	{
		S phase = (S)i / (N / 2);
		S freq = SR * phase / 2;
		S note = ftom(freq);
		notes[i] = note;

		hot[i] = false;
	}

	for (size_t i = 0; i < laps; i++)
	{
		hop_phasor[i] = cos((2.0 * PI * i) / laps);
		hop_phasor[i + laps] = sin((2.0 * PI * i) / laps);
	}

#ifdef DEBUG
	hw.seed.PrintLine("Initialized vocoder lookup tables.");
	hw.seed.PrintLine("Initialized hardware.");
#endif

#ifdef SYNTHETIC
	for (size_t k = 0; k < n_synthetic; k++)
		synthesizers[k] = new Synth<S>(&cycle, synthetic_freqs[k]);
#endif

	fft = new ShyFFT<S, N, RotationPhasor>();
	fft->Init();
	stft = new Fourier<S, N>(pitch_shift, fft, laps, in, middle, out);

#ifdef DEBUG
	hw.seed.PrintLine("Initialized FFT objects.");
#endif

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

#ifdef DEBUG
	hw.seed.PrintLine("Would now begin audio callback. Hanging instead.");

	while (true) { }
#endif

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

		if (!(last_update % oversample))
		{
			hw.display.Update();
			hw.UpdateLeds();
			last_update = 0;
		}

		System::DelayTicks(ticks);
		last_update++;
	}

	delete stft;
	delete fft;

#ifdef SYNTHETIC
	for (size_t k = 0; k < n_synthetic; k++)
		delete synthesizers[k];
#endif
}


// // // // // // // // // // // // // //
//    TYPE GESTURE TO ENTER DFU MODE   //
// // // // // // // // // // // // // //
Pedal::SwitchI S1 = Pedal::SwitchI::S1;
Pedal::SwitchI S2 = Pedal::SwitchI::S2;
Pedal::SwitchI ids[2] = {S1, S2};
Pedal::SwitchI changes[8] = {S1, S2, S1, S1, S2, S2, S1, S2};
bool initial[2] = {false, false};
Sequence<2, 8> sequence(&hw, ids, initial, changes);

S vscale = 1;
S scale_smooth = 0.01;
void handleUI()
{
	hw.display.Fill(false);

	sequence.tick(); // check for DFU mode button-press gesture
	if (sequence.step()) // returns true if the sequence was completed
	{
		hw.display.Fill(false);
		hw.seed.StopAudio();
		Rectangle boundingBox(0, 0, width, height);
		hw.display.WriteStringAligned("DFU...", Font_16x26, boundingBox, Alignment::centered, true);

		hw.display.Update();

		System::ResetToBootloader();
	}

	std::string str1, str2, str3;

#ifdef PRINTFREQS
	const char* cstr;
	
	const size_t rows = 2;
	const size_t cols = 4;

	size_t k = 0;
	for (size_t j = 0; j < N / 2; j++)
	{
		if (hot[j])
		{
			str2 = std::to_string((int)(freqs[j] + 0.5));
			cstr = (str2).c_str();
			
			hw.display.SetCursor(32 * (k / rows), 8 * (k % rows));
			hw.display.WriteString(cstr, Font_6x8, true); // displays current frequency with peak amplitude

			k++;
		}

		if (k > rows * cols)
			break;
	}
#endif


	int x, y;
	int base = height * 0.9;
	S peak = sqrt(biggest);
	vscale = scale_smooth * (peak < sqrtN ? sqrtN : peak) + (1 - scale_smooth) * vscale;
	size_t current = stft->current;

	// display frequencies logarithmically; display those whose corresponding MIDI note lives in [0,128)
	for (size_t i = 0; i < N / 2; i++)
	{
		x = (int)notes[i]; // works out that the MIDI range equals the screen width!
		if (x < 0)
			continue;
		if (x >= width) // really high frequencies don't need to get drawn (if their amplitudes aren't low we've got problems)
			break;

		S real = middle[i + current * N];
		S imag = middle[i + offset + current * N];

		S magn = sqrt(real * real + imag * imag);

	#ifdef PRINTFREQS
		y = (int)(0.6 * height * magn / vscale);
	#else
		y = (int)(0.8 * height * magn / vscale);
	#endif

		if (hot[i]) // draw a little extra for high-amplitude bins
		{
			hw.display.DrawPixel(x + 1, base - y, true);
			hw.display.DrawPixel(x - 1, base - y, true);
			hw.display.DrawPixel(x, base - y + 1, true);
			hw.display.DrawPixel(x, base - y - 1, true);
		}
		else
			hw.display.DrawPixel(x, base - y, true);
	}

	// read knob values
	noise_floor = hw.knobs[0].Value(); // denoise kills things below this amp
	release_ratio = hw.knobs[2].Value(); // things that are hot and fall below this amp become cold

	// alpha = hw.knobs[3].Value(); // gain for frequencies with below-average amplitudes
	beta = hw.knobs[4].Value(); // gain for frequencies with above-average amplitudes
	thresh = 50 * hw.knobs[5].Value(); // multipler for determining what's above- and below-average

	if (hw.encoders[0].Pressed())
	{
		fine_transposition += fine_increment * hw.encoders[0].Increment();
		if (fine_transposition > 50)
		{
			fine_transposition -= 100;
			transposition++;

		}
		else if (fine_transposition < -50)
		{
			fine_transposition += 100;
			transposition--;
		}
	}
	else
	{
		int change = hw.encoders[0].Increment();
		if (change) // if the encoder was turned but not pressed
		{
			if (fine_transposition)
			{
				// if we were above a whole number of steps before, keep it moving in the same direction
				transposition += change * (change * fine_transposition > 0);
				fine_transposition = 0;
			}
			else
				transposition += change;

			freq_ratio = pow(2, (transposition + 0.01 * fine_transposition) / 12);
		}
	}

#ifdef TRANSPOSE
	std::string prefix = transposition >= 0 ? "+" : "-";
	std::string fine_prefix = fine_transposition >= 0 ? "+" : "-";
	std::string fine_string = std::to_string(abs(fine_transposition));
	str1 = std::to_string(abs(transposition));
	cstr = (prefix + str1 + " " + fine_prefix + fine_string).c_str();
			
	hw.display.SetCursor(0, height - 17);
	hw.display.WriteString(cstr, Font_6x8, true); // displays current frequency with peak amplitude
#endif
}