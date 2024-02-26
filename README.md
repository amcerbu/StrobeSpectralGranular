# Reprogrammable Effects Pedals on the Daisy Seed Platform
This is the Git repository associated with the preprint for ICMC 2024; this hosts the code for firmware configured to run on [Keith Shepherd's 125b guitar pedal design](https://github.com/bkshepherd/DaisySeedProjects). 

### Current examples:
* `fourier`. Realtime spectral processing (STFT), along the lines of the Max/MSP object `pfft~`. The Daisy can handle four overlaps at N = 4096, or eight at N = 2048. As configured, the bottom-right potentiometer controls a "cutoff" amplitude ratio. Frequencies whose amplitudes are above or below that ratio of the average amplitude are attenuated to different degrees, according to the positions of the other two knobs in that row. At one extreme, the effect behaves like a de-noiser (wideband sounds are attenuated, periodic signals pass); at the other extreme, you hear only the noise and little harmonic content. Also displays the frequency-domain signal in realtime. Thanks to Émilie Gillet for `shy_fft.h`, an ARM-optimized fast Fourier transform implementation.  
The frequency-domain processor now also implements a phase vocoder; it can do accurate frequency detection by comparing the phases of high-amplitude bins across consecutive STFT windows. In progress are a frequency-domain pitch-shifter and a spectral freeze.

* `granny`. A realtime granular synthesizer. Adjust the parameters of the four grain generators by turning the encoder. Click the encoder to toggle parameter-modification mode. Within that mode, the six potentiometers change the parameters labeled on the OLED, the right footswitch toggles grain direction (forward / back), and the encoder modifies grain transposition (from -12 to +24 semitones).  
When not in parameter-modification mode, a grain generator can be toggled on or off with the right footswitch (status indicated on the OLED). The left footswitch toggles between true bypass and the effect (indicated by the right LED). CPU load is indicated by left LED (brighter = heavier!). Parameters can be saved to persistent storage by clicking and holding the encoder and both footswitches for several seconds (until the OLED animation ends).  
To reset the set of parameters, use the DFU buttonpress sequence listed below, but with left and right interchanged. This won't overwrite your preset but allows you to start with a blank slate. 

* `tuner`. Strobe tuner (and distortion effect: toggle with right footswitch). 

### Compiling
Each project has its own makefile. You'll need to modify the variables `DAISYSP_DIR` and `LIBDAISY_DIR` to point to your local installations of [DaisySP](https://github.com/electro-smith/DaisySP) and [libDaisy](https://github.com/electro-smith/libDaisy). Some projects also currently depend on the open-source, header-only linear algebra library [Eigen](https://eigen.tuxfamily.org/index.php?title=Main_Page). For those projects you'll need to modify the variable `EIGEN_DIR` as well.

Make sure you have the Daisy toolchain installed and that your Daisy Seed is connected by USB and in DFU mode. Then you can compile a project & flash it to the Seed with the command:

    make clean && make && make program-dfu

### General remarks
Once the Daisy is inside the pedal enclosure, it's inconvenient to access the Boot / Reset buttons for firmware updates. The `dHuygens` projects listen for sequences of buttonpresses (of the physical footswitches on the 125b pedal), and map those to various parameter changes. For example, the Seed can be put into DFU mode by executing the following sequence:

    down left, down right, up left, down left, up right, down right, up left, up right

(Here, `down` means hold the corresponding button until told to release it with `up`.)
